/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape CPU.
 *
 * Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape CPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * The general tier of a union clip (shape-exact-primitives story A3b; CG1 to CG12): an overlapping union drawn with the exact area of
 * the union of its members' regions.  The oracle's vectors byte for byte on every route (the default tiers, the general tier forced, the
 * exact step made to take disjoint members as one group), the general tier against the fast tier on hundreds of generated disjoint
 * unions, against the stroke's exact step as a second engine, the invariants of a union (member order, orientation, whole-pixel
 * translation, a region against itself), and every refusal that remains: the limits, the scratch, the unprovable member, the damage.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace cuh;

namespace {

/** The options of a render with the fast tier off, so that every union is the general tier's (CG10). */
Options general_only() {
  Options o;
  o.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION;
  return o;
}

/** ... and with the exact step made to take the members of a union as one group (the switch of CG2). */
Options one_group() {
  Options o;
  o.tiers = (GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION) | GSHCPU_RENDER_FORCE_ONE_GROUP;
  return o;
}

Options with_limits(const GSHCPU_Limits * l) {
  Options o;
  o.limits = l;
  return o;
}

Draw draw_union(Scene & s, const std::vector<GSHSCENE_Clip_Member> & members, uint32_t w, uint32_t h, const Options & o = Options(),
    const GSHGEOM_Map * map = nullptr) {
  return draw(clipped_fill(s, make_union(s, members), w, h, map), w, h, o);
}

/** The cases of the vectors whose members overlap themselves inside a pixel: a group of several takes the exact area of its region where a member alone takes AD-12's number (CG3). */
bool self_overlapping(const std::string & name) {
  return name == "ring-and-rect" || name == "self-overlap-beside" || name.rfind("g-selfoverlap", 0) == 0 || name.rfind("g-rand", 0) == 0 ||
      name == "g-star-rules";
}

struct Gen {
  std::mt19937 g;
  explicit Gen(uint32_t seed) : g(seed) {}
  int below(int n) {
    return (int)(g() % (uint32_t)n);
  }
  int between(int lo, int hi) {  // inclusive
    return lo + below(hi - lo + 1);
  }
};

using Poly = std::vector<std::pair<int, int>>;  // lattice vertices, 26.6

Poly rect_poly(int x0, int y0, int x1, int y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

int64_t cross(const std::pair<int, int> & o, const std::pair<int, int> & a, const std::pair<int, int> & b) {
  return (int64_t)(a.first - o.first) * (b.second - o.second) - (int64_t)(a.second - o.second) * (b.first - o.first);
}

/** The convex hull of @p pts (Andrew's monotone chain), counter-clockwise on screen (y down) as the shoelace sign says. */
Poly hull(std::vector<std::pair<int, int>> pts) {
  std::sort(pts.begin(), pts.end());
  pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
  if (pts.size() < 3) {
    return pts;
  }
  Poly h(2 * pts.size());
  size_t k = 0;
  for (size_t i = 0; i < pts.size(); i++) {
    while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) {
      k--;
    }
    h[k++] = pts[i];
  }
  for (size_t i = pts.size() - 1, t = k + 1; i > 0; i--) {
    while (k >= t && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0) {
      k--;
    }
    h[k++] = pts[i - 1];
  }
  h.resize(k - 1);
  return h;
}

/** A convex polygon of 3 to @p most points inside [x0, x1] x [y0, y1], the corners of the box among them with chance 1 in @p corner. */
Poly convex_in(Gen & g, int x0, int y0, int x1, int y1, int corner) {
  std::vector<std::pair<int, int>> pts;
  const int n = g.between(3, 8);
  for (int i = 0; i < n; i++) {
    pts.push_back({g.between(x0, x1), g.between(y0, y1)});
  }
  if (g.below(corner) == 0) {
    pts.push_back({x0, y0});
    pts.push_back({x1, y0});
    pts.push_back({x1, y1});
    pts.push_back({x0, y1});
  }
  return hull(pts);
}

double signed_area2(const Poly & p) {
  double a = 0;
  for (size_t i = 0; i < p.size(); i++) {
    const auto & u = p[i];
    const auto & v = p[(i + 1) % p.size()];
    a += (double)u.first * v.second - (double)v.first * u.second;
  }
  return a;
}

Poly reversed(Poly p) {
  std::reverse(p.begin(), p.end());
  return p;
}

}  // namespace

/* --- the vectors, by every route ------------------------------------------------------------------------------------- */

TEST(RenderClipUnionGeneral, EveryVectorIsTheOraclesByteForByteOnTheDefaultTiersAndWithTheGeneralTierForced) {
  size_t checked = 0;
  for (const cv::Case & c : cv::kCases) {
    Scene s;
    const GSHSCENE_Resource * u = make_union(s, case_members(s, c));
    GSHSCENE_List * l = clipped_fill(s, u, (uint32_t)c.width, (uint32_t)c.height);
    const Draw by_default = draw(l, (uint32_t)c.width, (uint32_t)c.height);
    const Draw forced = draw(l, (uint32_t)c.width, (uint32_t)c.height, general_only());
    ASSERT_EQ(by_default.result, GSHSCENE_OK) << c.name;
    ASSERT_EQ(forced.result, GSHSCENE_OK) << c.name;
    EXPECT_EQ(by_default.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    EXPECT_EQ(forced.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    EXPECT_EQ(forced.counters.union_fast, 0u) << c.name;
    EXPECT_EQ(forced.counters.union_general, 1u) << c.name;
    if (c.disjoint) {
      EXPECT_EQ(forced.counters.union_groups, 0u) << c.name << ": members whose boxes are disjoint are groups of one";
      EXPECT_EQ(forced.counters.union_exact, 0u) << c.name;
    } else {
      EXPECT_GE(by_default.counters.union_groups + (size_t)0, 0u);
    }
    for (int y = 0; y < c.height; y++) {
      for (int x = 0; x < c.width; x++) {
        const uint8_t want = cv::kExpect[c.expect_first + (size_t)(y * c.width + x)];
        EXPECT_EQ(by_default.at8((uint32_t)x, (uint32_t)y, 3), want) << c.name << " default at (" << x << ", " << y << ")";
        EXPECT_EQ(forced.at8((uint32_t)x, (uint32_t)y, 3), want) << c.name << " forced at (" << x << ", " << y << ")";
      }
    }
    checked++;
  }
  EXPECT_EQ(checked, sizeof cv::kCases / sizeof cv::kCases[0]);
  EXPECT_GE(checked, 50u);
}

TEST(RenderClipUnionGeneral, TheExactStepTakingDisjointMembersAsOneGroupGivesTheFastTiersBytes) {
  // The exact step computes the union of members whose boxes are disjoint, which touch in a pixel (abutting at x = 3.5: both have pieces
  // in it): it must give the sum the fast tier adds, for every vector the members of which do not overlap themselves (CG3, CG4).
  size_t checked = 0;
  size_t exact_cells = 0;
  for (const cv::Case & c : cv::kCases) {
    if (!c.disjoint || self_overlapping(c.name)) {
      continue;
    }
    Scene s;
    GSHSCENE_List * l = clipped_fill(s, make_union(s, case_members(s, c)), (uint32_t)c.width, (uint32_t)c.height);
    const Draw one = draw(l, (uint32_t)c.width, (uint32_t)c.height, one_group());
    ASSERT_EQ(one.result, GSHSCENE_OK) << c.name;
    EXPECT_EQ(one.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    EXPECT_EQ(one.counters.union_groups, 1u) << c.name;
    exact_cells += one.counters.union_exact;
    for (int y = 0; y < c.height; y++) {
      for (int x = 0; x < c.width; x++) {
        EXPECT_EQ(one.at8((uint32_t)x, (uint32_t)y, 3), cv::kExpect[c.expect_first + (size_t)(y * c.width + x)])
            << c.name << " at (" << x << ", " << y << ")";
      }
    }
    checked++;
  }
  EXPECT_GE(checked, 8u);
  EXPECT_GT(exact_cells, 0u) << "the exact step was taken for pixels where two members have pieces";
}

TEST(RenderClipUnionGeneral, ASelfOverlappingMemberBesideTheGroupKeepsItsOwnNumberAndInsideItTheExactArea) {
  Scene s;
  // The doubled rectangle of 48 by 64 units is in a pixel the sum of its contours' areas, 12288 units, held at 8192 (AD-12), where its region is 6144.
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  for (int k = 0; k < 2; k++) {
    gshgeom_path_move_to(p, GSHGEOM_Point{256 / 64.0, 0, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{304 / 64.0, 0, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{304 / 64.0, 1, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{256 / 64.0, 1, 0});
    gshgeom_path_close(p);
  }
  const GSHSCENE_Resource * twice = s.path(p);
  const GSHSCENE_Resource * a = s.rect(0, 0, 1.6, 1.6);
  const GSHSCENE_Resource * b = s.rect(0.9, 0.5, 2.4, 2.4);
  // Apart from the group (its box is disjoint from theirs): the number AD-12 defines for the member, 8192, a whole pixel.
  Draw apart = draw_union(s, {member(a), member(b), member(twice)}, 6, 3);
  ASSERT_EQ(apart.result, GSHSCENE_OK);
  EXPECT_EQ(apart.counters.union_general, 1u);
  EXPECT_EQ(apart.counters.union_groups, 1u) << "the pair is a group of two; the doubled member is a group of one";
  EXPECT_EQ(apart.at8(4, 0, 3), 255u);
  // The same member in a group (a small one inside it, whose box meets its box): the area of its region, 6144 units, 192 levels.
  const GSHSCENE_Resource * partner = s.rect(4.5, 0.25, 4.6, 0.5);
  Draw inside = draw_union(s, {member(twice), member(partner)}, 6, 1);
  ASSERT_EQ(inside.result, GSHSCENE_OK);
  EXPECT_EQ(inside.counters.union_groups, 1u);
  EXPECT_EQ(inside.at8(4, 0, 3), 192u) << "the region of the doubled rectangle, 48 of 64 units wide, not its saturating sum";
}

/* --- the general tier against the fast tier ---------------------------------------------------------------------------- */

TEST(RenderClipUnionGeneral, TheGeneralTierEqualsTheFastTierOnGeneratedDisjointUnions) {
  // 400 unions of members that touch along lattice lines and do not overlap, each a convex polygon or the box itself, each under a rule
  // and an orientation of its own: the default tiers (FT-U1), the general tier, and the exact step taking the members as one group
  // must give the same bytes (AD-13's pairing, CG4).
  Gen g(20261106u);
  size_t unions = 0;
  size_t touching_pieces = 0;
  for (int round = 0; round < 400; round++) {
    const int W = 10, H = 6;  // pixels
    // Cut the canvas into boxes by a few vertical lines, and each strip by a few horizontal ones; every cut on a multiple of 1/16 pixel.
    std::vector<std::array<int, 4>> boxes;
    std::vector<int> xs = {0, W * 64};
    for (int i = g.between(1, 3); i > 0; i--) {
      xs.push_back(g.between(1, W * 4 - 1) * 16);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i++) {
      std::vector<int> ys = {0, H * 64};
      for (int k = g.between(0, 2); k > 0; k--) {
        ys.push_back(g.between(1, H * 4 - 1) * 16);
      }
      std::sort(ys.begin(), ys.end());
      ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
      for (size_t j = 0; j + 1 < ys.size(); j++) {
        boxes.push_back({xs[i], ys[j], xs[i + 1], ys[j + 1]});
      }
    }
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    for (const auto & b : boxes) {
      if (g.below(5) == 0) {
        continue;  // a hole in the canvas
      }
      Poly poly = g.below(2) ? rect_poly(b[0], b[1], b[2], b[3]) : convex_in(g, b[0], b[1], b[2], b[3], 2);
      if (poly.size() < 3 || std::fabs(signed_area2(poly)) < 1.0) {
        continue;
      }
      if (g.below(2)) {
        poly = reversed(poly);
      }
      members.push_back(member(lattice_polygon(s, poly), g.below(3) == 0 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO));
    }
    if (members.size() < 2) {
      continue;
    }
    // A member that can touch no pixel is in the population too (CG4: neither tier may refuse the union for it, nor draw it differently):
    // a hairline on a pixel boundary, a zero-height line, and one point twice, in the render and 20 million pixels away (the sink's range
    // is passed by its geometry before its 26.6 box is).  The population of this test did not hold one before pass B, and the fast tier
    // refused the push for such a member where the general tier skipped it.
    if (g.below(3) == 0) {
      const double far = g.below(2) ? 2.0e7 : 0.0, nx = (double)g.between(0, W), ny = (double)g.between(0, H);
      switch (g.below(3)) {
        case 0: members.insert(members.begin() + g.below((int)members.size() + 1), member(s.polygon({far + nx, ny, far + nx, ny + 3.0})));
          break;
        case 1: members.push_back(member(s.polygon({far + nx, ny, far + nx + 4.0, ny})));
          break;
        default: members.insert(members.begin(), member(s.polygon({far - nx, ny, far - nx, ny})));
          break;
      }
    }
    GSHSCENE_List * l = clipped_fill(s, make_union(s, members), W, H);
    const Draw fast = draw(l, W, H);
    const Draw forced = draw(l, W, H, general_only());
    const Draw one = draw(l, W, H, one_group());
    ASSERT_EQ(fast.result, GSHSCENE_OK) << round;
    ASSERT_EQ(forced.result, GSHSCENE_OK) << round;
    ASSERT_EQ(one.result, GSHSCENE_OK) << round;
    EXPECT_EQ(fast.counters.union_fast, 1u) << round << ": the members' boxes are disjoint";
    EXPECT_EQ(forced.counters.union_general, 1u) << round;
    EXPECT_EQ(alphas(forced), alphas(fast)) << "round " << round << ": the general tier differs from the fast tier";
    EXPECT_EQ(alphas(one), alphas(fast)) << "round " << round << ": the exact step on one group differs from the sum";
    touching_pieces += one.counters.union_exact;
    unions++;
  }
  EXPECT_GE(unions, 300u);
  EXPECT_GT(touching_pieces, 1000u) << "the exact step was taken in many pixels where members meet";
}

TEST(RenderClipUnionGeneral, TheGeneralTierEqualsTheFastTierWithMembersThatOverlapThemselves) {
  // A member whose contours overlap inside a pixel has AD-12's saturating-sum number, beside members that are not in its group: both tiers
  // add the same (CG3).  Members: a doubled rectangle, a star of the nonzero rule and a ring, in boxes that do not meet.
  Scene s;
  Poly star = {{96, 0}, {156, 186}, {0, 70}, {192, 70}, {36, 186}};
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  for (int k = 0; k < 3; k++) {
    gshgeom_path_move_to(p, GSHGEOM_Point{4.0, 0.25, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{5.3, 0.25, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{5.3, 1.6, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{4.0, 1.6, 0});
    gshgeom_path_close(p);
  }
  const GSHSCENE_Resource * thrice = s.path(p);
  GSHGEOM_Path * q = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &q);
  for (auto & v : star) {
    GSHGEOM_Point pt{7.0 + v.first / 64.0, v.second / 64.0, 0};
    (&v == &star[0]) ? gshgeom_path_move_to(q, pt) : gshgeom_path_line_to(q, pt);
  }
  gshgeom_path_close(q);
  const GSHSCENE_Resource * star_path = s.path(q);
  const std::vector<GSHSCENE_Clip_Member> members = {member(thrice), member(star_path), member(s.rect(0.2, 0.2, 3.4, 3.1))};
  GSHSCENE_List * l = clipped_fill(s, make_union(s, members), 11, 4);
  const Draw fast = draw(l, 11, 4);
  const Draw forced = draw(l, 11, 4, general_only());
  ASSERT_EQ(fast.result, GSHSCENE_OK);
  EXPECT_EQ(fast.counters.union_fast, 1u) << "the three boxes are disjoint";
  EXPECT_EQ(forced.counters.union_general, 1u);
  EXPECT_EQ(forced.counters.union_groups, 0u) << "three groups of one";
  EXPECT_EQ(alphas(forced), alphas(fast));
  EXPECT_EQ(fast.at8(4, 0, 3), 255u) << "the tripled rectangle's pixel, AD-12's number";
  // The saturating sum is what the single member has too: the union of it alone is the single clip.
  const Draw alone = draw(clipped_fill(s, thrice, 11, 4), 11, 4);
  EXPECT_EQ(alone.at8(4, 0, 3), fast.at8(4, 0, 3));
}

/* --- a second engine: the exact step of a stroke part -------------------------------------------------------------------- */

namespace {

struct Flat2 {
  GSHGEOM_Device_Path * path = nullptr;
  GSHGEOM_Flat_Path * flat = nullptr;
  explicit Flat2(const std::vector<Poly> & polys) {
    EXPECT_EQ(gshgeom_device_path_create(nullptr, nullptr, &path), GSHGEOM_OK);
    for (const Poly & c : polys) {
      for (size_t i = 0; i < c.size(); i++) {
        GSHGEOM_Device_Point p;
        p.x = c[i].first;
        p.y = c[i].second;
        i == 0 ? gshgeom_device_path_move_to(path, p) : gshgeom_device_path_line_to(path, p);
      }
      gshgeom_device_path_close(path);
    }
    EXPECT_EQ(gshgeom_device_path_flatten(path, nullptr, nullptr, &flat), GSHGEOM_OK);
  }
  ~Flat2() {
    gshgeom_flat_path_destroy(flat);
    gshgeom_device_path_destroy(path);
  }
};

/** The bytes the exact step of a stroke part gives for the nonzero fill of @p polys (positively wound): the other engine. */
std::vector<uint8_t> stroke_engine(const std::vector<Poly> & polys, int w, int h) {
  Flat2 f(polys);
  std::vector<uint8_t> out((size_t)(w * h), 0xee);
  std::vector<int64_t> acc(2 * (size_t)(w + 1) * GSHCPU_RENDER_BAND_ROWS, 0);
  std::vector<unsigned char> raw(gshcpu_exact_bytes((size_t)w, 512, 1) + 16);
  GSHCPU_Exact ex;
  gshcpu_exact_init(&ex, (void *)(((uintptr_t)raw.data() + 15u) & ~(uintptr_t)15u), (size_t)w, 512, 1);
  ex.eps = GSHCPU_RENDER_EXACT_EPS;
  const GSHGEOM_Flat_Path * paths[1] = {f.flat};
  GSHCPU_Rect rect{0, 0, w, h};
  uint64_t cells = 0;
  EXPECT_EQ(gshcpu_render_cover_exact(paths, 1, GSHGEOM_FILL_NONZERO, rect, rect, out.data(), acc.data(), UINT64_MAX, &cells, &ex), GSHCPU_OK);
  return out;
}

}  // namespace

TEST(RenderClipUnionGeneral, AnOverlappingUnionOfPositiveConvexPolygonsIsTheStrokeStepsAreaWithinTheRoundingOfAHalfUnit) {
  // The union of the nonzero fills of positively wound convex polygons is the nonzero fill of all their contours at once, whose exact area
  // the exact step of a stroke part (UC1 to UC7) computes in its own way and rounds to the nearest unit where the general tier takes the
  // floor (CU2): the two bytes are the same, or the stroke step's is one more where the area is within half a unit of the next level.
  Gen g(7u);
  size_t pixels = 0, equal = 0;
  for (int round = 0; round < 120; round++) {
    const int W = 5, H = 4;
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    std::vector<Poly> polys;
    const int k = g.between(2, 5);
    for (int i = 0; i < k; i++) {
      Poly poly = convex_in(g, g.between(0, 6 * 64), g.between(0, 4 * 64), g.between(6 * 64, 5 * 64 + 160), g.between(4 * 64, 4 * 64 + 100), 4);
      poly = hull(poly);
      if (poly.size() < 3) {
        continue;
      }
      if (signed_area2(poly) < 0) {
        poly = reversed(poly);
      }
      if (signed_area2(poly) < 64.0) {
        continue;
      }
      polys.push_back(poly);
      members.push_back(member(lattice_polygon(s, poly)));
    }
    if (members.size() < 2) {
      continue;
    }
    // the stroke step wants the orientation of the expansion's pieces: try the polygons and, if it gives nothing, their reverses
    std::vector<uint8_t> other = stroke_engine(polys, W, H);
    if (std::all_of(other.begin(), other.end(), [](uint8_t b) { return b == 0; })) {
      for (Poly & p : polys) {
        p = reversed(p);
      }
      other = stroke_engine(polys, W, H);
    }
    const Draw d = draw_union(s, members, W, H);
    ASSERT_EQ(d.result, GSHSCENE_OK) << round;
    ASSERT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u) << round;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        const int mine = d.at8((uint32_t)x, (uint32_t)y, 3);
        const int theirs = other[(size_t)(y * W + x)];
        EXPECT_GE(theirs, mine) << round << " at (" << x << ", " << y << ")";
        EXPECT_LE(theirs, mine + 1) << round << " at (" << x << ", " << y << ")";
        pixels++;
        equal += mine == theirs;
      }
    }
  }
  EXPECT_GT(pixels, 1500u);
  EXPECT_GE(equal * 1000u, pixels * 995u) << "the bytes are the same but where the area is within half a unit below a level";
}

/* --- the invariants of a union ---------------------------------------------------------------------------------------- */

TEST(RenderClipUnionGeneral, TheMemberOrderTheOrientationAndAWholePixelShiftDoNotChangeTheCoverage) {
  Gen g(99u);
  for (int round = 0; round < 80; round++) {
    const int W = 7, H = 5;
    std::vector<Poly> polys;
    std::vector<GSHGEOM_Fill_Rule> rules;
    for (int i = g.between(2, 5); i > 0; i--) {
      Poly p = convex_in(g, g.between(0, 3 * 64), g.between(0, 2 * 64), g.between(3 * 64, 6 * 64 + 20), g.between(2 * 64, 4 * 64 + 20), 3);
      if (p.size() < 3 || std::fabs(signed_area2(p)) < 64.0) {
        continue;
      }
      polys.push_back(p);
      rules.push_back(g.below(3) == 0 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO);
    }
    if (polys.size() < 2) {
      continue;
    }
    auto build = [&](Scene & s, bool flip, bool reverse_order) {
      std::vector<GSHSCENE_Clip_Member> m;
      for (size_t i = 0; i < polys.size(); i++) {
        m.push_back(member(lattice_polygon(s, flip ? reversed(polys[i]) : polys[i]), rules[i]));
      }
      if (reverse_order) {
        std::reverse(m.begin(), m.end());
      }
      return m;
    };
    Scene a, b, c, d;
    const Draw base = draw_union(a, build(a, false, false), W, H);
    const Draw swapped = draw_union(b, build(b, false, true), W, H);
    const Draw flipped = draw_union(c, build(c, true, false), W, H);
    ASSERT_EQ(base.result, GSHSCENE_OK) << round;
    EXPECT_EQ(alphas(swapped), alphas(base)) << "member order, round " << round;
    EXPECT_EQ(alphas(flipped), alphas(base)) << "orientation, round " << round << " (the magnitude fold)";
    // The same union shifted by two pixels right and one down under the item's map, in a canvas two and one wider: the bytes move.
    const GSHGEOM_Map shift = translate(2, 1);
    const Draw moved = draw_union(d, build(d, false, false), W + 2, H + 1, Options(), &shift);
    ASSERT_EQ(moved.result, GSHSCENE_OK) << round;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        EXPECT_EQ(moved.at8((uint32_t)(x + 2), (uint32_t)(y + 1), 3), base.at8((uint32_t)x, (uint32_t)y, 3)) << round << " at (" << x << ", " << y << ")";
      }
    }
  }
}

TEST(RenderClipUnionGeneral, ARegionUnitedWithItselfUnderBothRulesIsTheSingleClip) {
  // A simple polygon under nonzero and under even-odd is one region twice: the union is its coverage, the single clip's, at every pixel.
  Gen g(5u);
  for (int round = 0; round < 60; round++) {
    Scene s;
    Poly p = convex_in(g, 10, 10, 6 * 64 - 10, 4 * 64 - 10, 3);
    if (p.size() < 3 || std::fabs(signed_area2(p)) < 128.0) {
      continue;
    }
    const GSHSCENE_Resource * path = lattice_polygon(s, p);
    const Draw single = draw(clipped_fill(s, path, 6, 4), 6, 4);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint wh = white();
    ASSERT_EQ(gshscene_list_clip_push(l, path, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 6, 4), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    const Draw one = draw(l, 6, 4);
    const Draw twice = draw_union(s, {member(path, GSHGEOM_FILL_NONZERO), member(path, GSHGEOM_FILL_EVEN_ODD)}, 6, 4);
    ASSERT_EQ(twice.result, GSHSCENE_OK) << round;
    EXPECT_EQ(twice.counters.union_general, 1u) << "the two members' boxes are the same: one group";
    EXPECT_EQ(alphas(twice), alphas(one)) << round;
  }
}

TEST(RenderClipUnionGeneral, TheCoverageOfAUnionLiesBetweenItsLargestMemberAndTheSumOfThem) {
  Gen g(31u);
  for (int round = 0; round < 60; round++) {
    const int W = 6, H = 4;
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    std::vector<Draw> alone;
    for (int i = g.between(2, 5); i > 0; i--) {
      Poly p = convex_in(g, g.between(0, 3 * 64), g.between(0, 2 * 64), g.between(3 * 64, 5 * 64), g.between(2 * 64, 4 * 64), 3);
      if (p.size() < 3 || std::fabs(signed_area2(p)) < 64.0) {
        continue;
      }
      const GSHSCENE_Resource * path = lattice_polygon(s, p);
      members.push_back(member(path));
      alone.push_back(draw(clipped_fill(s, path, W, H), W, H));
    }
    if (members.size() < 2) {
      continue;
    }
    const Draw all = draw_union(s, members, W, H);
    ASSERT_EQ(all.result, GSHSCENE_OK);
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        int most = 0, sum = 0;
        for (const Draw & a : alone) {
          most = std::max<int>(most, a.at8((uint32_t)x, (uint32_t)y, 3));
          sum += a.at8((uint32_t)x, (uint32_t)y, 3);
        }
        const int got = all.at8((uint32_t)x, (uint32_t)y, 3);
        EXPECT_GE(got, most) << round << " at (" << x << ", " << y << ")";
        EXPECT_LE(got, std::min(255, sum + (int)alone.size() - 1)) << round << " at (" << x << ", " << y << ")";
      }
    }
  }
}

TEST(RenderClipUnionGeneral, TheMostMembersOfAUnionInOneChainAreOneGroupOfThatManyParts) {
  // 1024 rectangles 40/64 wide and 13/64 apart, a pixel high: each overlaps the next three, the chain is one group; the union is the
  // rectangle from 0 to (1023 * 13 + 40) / 64 pixels, whose coverage is closed form.
  GSHSCENE_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.max_clip_members = GSHSCENE_CLIP_MEMBERS_CEILING;
  Scene s(&limits);
  std::vector<GSHSCENE_Clip_Member> members;
  for (int i = 0; i < 1024; i++) {
    members.push_back(member(lattice_polygon(s, rect_poly(i * 13, 0, i * 13 + 40, 64))));
  }
  const int total = 1023 * 13 + 40;  // in 1/64 pixel
  const uint32_t W = (uint32_t)((total + 63) / 64);
  const GSHSCENE_Resource * u = make_union(s, members);
  GSHSCENE_List * l = clipped_fill(s, u, W, 1);
  Draw d = draw(l, W, 1);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_EQ(d.counters.union_groups, 1u);
  for (uint32_t x = 0; x < W; x++) {
    const int covered = std::min(64, total - (int)x * 64);
    EXPECT_EQ(d.at8(x, 0, 3), (uint8_t)std::min(255, covered * 64 * 2 / 32)) << x;
  }
  // The exact step taking them as one group in a different route (forced general, one group): the same; and in the caller's memory.
  Draw one = draw(l, W, 1, one_group());
  EXPECT_EQ(alphas(one), alphas(d));
  Options cm;
  cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw in_memory = draw(l, W, 1, cm);
  ASSERT_EQ(in_memory.result, GSHSCENE_OK);
  EXPECT_EQ(alphas(in_memory), alphas(d));
}

TEST(RenderClipUnionGeneral, ANestedMemberThatNoEdgeOfItsOwnReachesInAPixelMakesTheGroupWholeThere) {
  // A big rectangle holds the pixel (2, 1) whole with no piece of its own in it, while two small members cross inside that pixel: the
  // pixel is whole (the marks' high bit, CG7).
  Scene s;
  const GSHSCENE_Resource * big = s.rect(0.5, 0.5, 5.5, 3.5);
  const GSHSCENE_Resource * a = lattice_polygon(s, {{130, 70}, {190, 80}, {150, 120}});
  const GSHSCENE_Resource * b = lattice_polygon(s, {{140, 66}, {180, 126}, {135, 120}});
  Draw d = draw_union(s, {member(big), member(a), member(b)}, 6, 4);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.union_groups, 1u);
  EXPECT_EQ(d.at8(2, 1, 3), 255u);
  EXPECT_EQ(d.at8(3, 2, 3), 255u);
  EXPECT_EQ(d.at8(0, 0, 3), 64u);
}

/* --- exact where it must be -------------------------------------------------------------------------------------------- */

TEST(RenderClipUnionGeneral, TheExactStepIsTakenInThePixelsWithTwoOrMorePiecesAndOnlyInThose) {
  // Two squares of a pixel and a half, the second a pixel right and down: the pieces of the first are 1 in (0, 0) and (0, 1) (its left edge), 2 in (1, 0) and
  // (1, 1) (its right edge and the flat piece of its top or bottom edge, which counts where it starts at the pixel's left side); the second's are 1 in
  // (1, 1) and (1, 2), and 2 in (2, 1) and (2, 2).  The pixels with two or more in all are (1, 0), (1, 1), (2, 1) and (2, 2): four, with 2 + 3 + 2 + 2 = 9 pieces.
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0.25, 0.25, 1.75, 1.75)), member(s.rect(1.25, 1.25, 2.75, 2.75))});
  Draw d = draw(clipped_fill(s, u, 3, 3), 3, 3);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.union_groups, 1u);
  EXPECT_EQ(d.counters.union_exact, 4u);
  EXPECT_EQ(d.counters.union_pieces, 9u);
  // Closed form: the union of [0.25, 1.75]^2 and [1.25, 2.75]^2 (the overlap is the square [1.25, 1.75]^2 of a quarter).
  EXPECT_EQ(d.at8(1, 1, 3), 224u) << "(1, 1): the first covers [1, 1.75]^2 and the second [1.25, 2]^2, and the union leaves two corners of 1/16 each: 7/8";
}

TEST(RenderClipUnionGeneral, MorePiecesInAPixelThanTheMarksCountAreSaturatedAndNeverCarryIntoTheWholePixelMark) {
  // Eight members in boxes apart inside one pixel, each a polygon of 24 vertices: over 150 pieces in the pixel, past the 127 the marks hold.  They
  // are a disjoint union, so the fast tier adds their areas, and the exact step on one group must give the same.
  Scene s;
  std::vector<GSHSCENE_Clip_Member> members;
  for (int k = 0; k < 8; k++) {
    const int x0 = 2 + (k % 4) * 15, y0 = 2 + (k / 4) * 31;
    Poly v;
    for (int i = 0; i < 24; i++) {
      // a star-ish ring of 24 lattice points inside a 15 by 15 box
      const int ring = i % 2 ? 7 : 4;
      const int dx = (int)std::lround(ring * std::cos(i * 3.14159265358979 / 12.0)), dy = (int)std::lround(ring * std::sin(i * 3.14159265358979 / 12.0));
      v.push_back({x0 + 8 + dx, y0 + 8 + dy});
    }
    members.push_back(member(lattice_polygon(s, v)));
  }
  GSHSCENE_List * l = clipped_fill(s, make_union(s, members), 1, 1);
  const Draw fast = draw(l, 1, 1);
  const Draw one = draw(l, 1, 1, one_group());
  ASSERT_EQ(fast.result, GSHSCENE_OK);
  ASSERT_EQ(one.result, GSHSCENE_OK);
  EXPECT_EQ(fast.counters.union_fast, 1u);
  EXPECT_GT(one.counters.union_pieces, 127u) << "more than the marks count";
  EXPECT_EQ(one.at8(0, 0, 3), fast.at8(0, 0, 3));
  EXPECT_LT(fast.at8(0, 0, 3), 255u);
  EXPECT_GT(fast.at8(0, 0, 3), 0u);
}

TEST(RenderClipUnionGeneral, AUnionThatReachesPastTheTargetIsDrawnOnlyWhereTheTargetIsAsTheSameUnionOnABiggerOneIs) {
  Scene s;
  const GSHSCENE_Resource * a = lattice_polygon(s, {{-90, -40}, {250, 10}, {120, 190}});
  const GSHSCENE_Resource * b = lattice_polygon(s, {{-40, 150}, {230, -60}, {300, 160}});
  const GSHSCENE_Resource * u = make_union(s, {member(a), member(b), member(s.rect(1.2, 0.7, 5.3, 3.9))});
  const Draw small = draw(clipped_fill(s, u, 4, 3), 4, 3);
  // The same union moved by (3, 2) pixels on a canvas 3 and 2 bigger in each direction on the far side too.
  const GSHGEOM_Map shift = translate(3, 2);
  const Draw big = draw(clipped_fill(s, u, 10, 8, &shift), 10, 8);
  ASSERT_EQ(small.result, GSHSCENE_OK);
  ASSERT_EQ(big.result, GSHSCENE_OK);
  EXPECT_EQ(small.count(GSHSCENE_ENTRY_REFUSED), 0u);
  for (uint32_t y = 0; y < 3; y++) {
    for (uint32_t x = 0; x < 4; x++) {
      EXPECT_EQ(small.at8(x, y, 3), big.at8(x + 3, y + 2, 3)) << x << "," << y;
    }
  }
}

TEST(RenderClipUnionGeneral, ADamagedRenderInCallerMemoryEqualsTheWholeAndTheMeasureHoldsTheWholeRendersWork) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(lattice_polygon(s, {{10, 10}, {500, 50}, {40, 300}})),
      member(lattice_polygon(s, {{20, 280}, {490, 20}, {420, 380}}))});
  GSHSCENE_List * l = clipped_fill(s, u, 9, 7);
  GSHSCENE_Rect part{3, 2, 5, 4};
  Options o;
  o.damage = &part;
  o.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  const Draw dmg = draw(l, 9, 7, o);
  ASSERT_EQ(dmg.result, GSHSCENE_OK) << "the measure holds the whole render's rectangle of the union, whatever the damage";
  const Draw whole = draw(l, 9, 7);
  for (uint32_t y = 2; y < 4; y++) {
    for (uint32_t x = 3; x < 5; x++) {
      EXPECT_EQ(dmg.at8(x, y, 3), whole.at8(x, y, 3));
    }
  }
}

/* --- an independent reference: point sampling ----------------------------------------------------------------------------- */

namespace {

/** The winding number of the polygon @p v (lattice vertices) about the point (@p px, @p py) in 1/24 of a lattice unit, exact; a point on an edge counts as outside. */
int winding_at(const Poly & v, int64_t px, int64_t py) {
  int w = 0;
  for (size_t i = 0; i < v.size(); i++) {
    const int64_t ax = (int64_t)v[i].first * 24, ay = (int64_t)v[i].second * 24;
    const int64_t bx = (int64_t)v[(i + 1) % v.size()].first * 24, by = (int64_t)v[(i + 1) % v.size()].second * 24;
    const int64_t side = (bx - ax) * (py - ay) - (px - ax) * (by - ay);
    if (ay <= py) {
      if (by > py && side > 0) {
        w++;
      }
    } else if (by <= py && side < 0) {
      w--;
    }
  }
  return w;
}

/** The area, in levels of 1/255 pixel, of the union of the regions of @p polys under @p rules in pixel (px, py), by @p n by @p n samples. */
double sampled_level(const std::vector<Poly> & polys, const std::vector<bool> & even_odd, int px, int py, int n) {
  int64_t inside = 0;
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) {
      // the sample's place in lattice units is 64 (p + (i + 1/3) / n): in 1/24 units, 24 * 64 * (p * n + i + 1/3) / n
      const int64_t X = (24 * 64 * ((int64_t)px * n + i) + 8 * 64) / n, Y = (24 * 64 * ((int64_t)py * n + j) + 8 * 64) / n;
      for (size_t m = 0; m < polys.size(); m++) {
        const int w = winding_at(polys[m], X, Y);
        if (even_odd[m] ? (w & 1) != 0 : w != 0) {
          inside++;
          break;
        }
      }
    }
  }
  return 255.0 * (double)inside / ((double)n * (double)n);
}

}  // namespace

TEST(RenderClipUnionGeneral, AGroupOfRandomPolygonsThatCrossThemselvesAndEachOtherAgreesWithPointSamplingUnderBothRules) {
  // Polygons of three to six vertices, some vertices on the pixels' lines, under both rules, in one group: the areas of their union by 224 by 224 samples
  // a pixel are within four levels of the sink's (a sample grid's own error is a level or two on a pixel the edges cross a few times), which no bookkeeping of the windings of several parts that went wrong could be.
  Gen g(424242u);
  double worst = 0.0;
  size_t unions = 0;
  for (int round = 0; round < 160; round++) {
    const int W = 3, H = 3;
    std::vector<Poly> polys;
    std::vector<bool> even_odd;
    const int k = g.between(2, 5);
    for (int i = 0; i < k; i++) {
      Poly p;
      for (int v = g.between(3, 6); v > 0; v--) {
        auto coord = [&]() { return g.below(4) == 0 ? g.between(0, 3) * 64 : g.between(0, 191); };
        const int x = coord(), y = coord();
        p.push_back({x, y});
      }
      polys.push_back(p);
      even_odd.push_back(g.below(2) == 1);
    }
    // One group: the boxes of the members chain together (the sink's relation: the interiors of the lattice boxes meet).
    auto box = [](const Poly & p) {
      std::array<int, 4> b = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
      for (auto & v : p) {
        b[0] = std::min(b[0], v.first);
        b[1] = std::min(b[1], v.second);
        b[2] = std::max(b[2], v.first);
        b[3] = std::max(b[3], v.second);
      }
      return b;
    };
    std::vector<int> root(polys.size());
    for (size_t i = 0; i < root.size(); i++) {
      root[i] = (int)i;
    }
    std::function<int(int)> find = [&](int i) { return root[i] == i ? i : root[i] = find(root[i]); };
    for (size_t i = 0; i < polys.size(); i++) {
      for (size_t j = i + 1; j < polys.size(); j++) {
        const auto a = box(polys[i]), b = box(polys[j]);
        if (std::max(a[0], b[0]) < std::min(a[2], b[2]) && std::max(a[1], b[1]) < std::min(a[3], b[3])) {
          root[find((int)j)] = find((int)i);
        }
      }
    }
    bool one = true;
    for (size_t i = 0; i < polys.size(); i++) {
      one = one && find((int)i) == find(0);
    }
    if (!one) {
      continue;
    }
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    for (size_t i = 0; i < polys.size(); i++) {
      members.push_back(member(lattice_polygon(s, polys[i]), even_odd[i] ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO));
    }
    const Draw d = draw_union(s, members, W, H);
    ASSERT_EQ(d.result, GSHSCENE_OK) << round;
    if (d.count(GSHSCENE_ENTRY_REFUSED) != 0) {
      continue;
    }
    ASSERT_EQ(d.counters.union_general, 1u) << round;
    unions++;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        const double want = sampled_level(polys, even_odd, x, y, 224);
        const double got = (double)d.at8((uint32_t)x, (uint32_t)y, 3);
        worst = std::max(worst, std::fabs(want - got));
        EXPECT_NEAR(got, want, 4.0) << "round " << round << " at (" << x << ", " << y << ")";
      }
    }
  }
  EXPECT_GE(unions, 100u);
  EXPECT_LT(worst, 4.0);
  RecordProperty("worst_level_difference", std::to_string(worst));
}

TEST(RenderClipUnionGeneral, StarPolygonsWhoseEdgesCrossManyTimesInOnePixelAgreeWithPointSamplingUnderBothRules) {
  // The star polygons {n/k} (a vertex joined to the k-th after it) of seven to eleven points, nonzero or even-odd, with a second star beside them and a
  // rectangle across both: dozens of crossings of a member's edges with each other and with the others', in one pixel.  The sample grid is 512 by 512.
  struct Star { int n, k; int cx, cy; bool even_odd; };
  const Star stars[] = {{7, 2, 32, 32, false}, {7, 3, 32, 32, true}, {9, 2, 31, 33, false}, {9, 4, 31, 33, true}, {11, 3, 32, 31, false}, {11, 5, 32, 31, true}};
  double worst = 0.0;
  for (const Star & a : stars) {
    for (const Star & b : {Star{7, 3, 40, 30, true}, Star{9, 4, 24, 34, false}}) {
      std::vector<Poly> polys;
      std::vector<bool> even_odd;
      for (const Star & st : {a, b}) {
        Poly v;
        for (int j = 0; j < st.n; j++) {
          const int i = (j * st.k) % st.n;
          v.push_back({st.cx + (int)std::lround(29.0 * std::cos(2.0 * 3.14159265358979 * i / st.n)),
              st.cy + (int)std::lround(29.0 * std::sin(2.0 * 3.14159265358979 * i / st.n))});
        }
        polys.push_back(v);
        even_odd.push_back(st.even_odd);
      }
      polys.push_back(rect_poly(9, 14, 51, 41));
      even_odd.push_back(false);
      Scene s;
      std::vector<GSHSCENE_Clip_Member> members;
      for (size_t i = 0; i < polys.size(); i++) {
        members.push_back(member(lattice_polygon(s, polys[i]), even_odd[i] ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO));
      }
      const Draw d = draw_union(s, members, 1, 1);
      ASSERT_EQ(d.result, GSHSCENE_OK);
      ASSERT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
      ASSERT_EQ(d.counters.union_groups, 1u);
      const double want = sampled_level(polys, even_odd, 0, 0, 512);
      const double got = (double)d.at8(0, 0, 3);
      worst = std::max(worst, std::fabs(want - got));
      EXPECT_NEAR(got, want, 2.0) << "stars " << a.n << "/" << a.k << " and " << b.n << "/" << b.k;
    }
  }
  RecordProperty("worst_level_difference", std::to_string(worst));
}

TEST(RenderClipUnionGeneral, WhatTheMeasureTakesDoesNotGrowWithTheGeometryOfEveryMemberOnlyWithTheLargestOne) {
  // The geometry of one member is live at a time (CG8): the memory a union render needs for a union of thirty polygons of 3000 vertices is that of two of them, less the few
  // hundred bytes the plan and the arrays a part needs grow by.
  auto need_for = [](int members) {
    Scene s;
    std::vector<GSHSCENE_Clip_Member> m;
    for (int k = 0; k < members; k++) {
      Poly v;
      for (int i = 0; i < 3000; i++) {
        v.push_back({100 + k + (int)std::lround(90.0 * std::cos(2.0 * 3.14159265358979 * i / 3000.0)),
            100 + (int)std::lround(90.0 * std::sin(2.0 * 3.14159265358979 * i / 3000.0))});
      }
      m.push_back(member(lattice_polygon(s, v)));
    }
    GSHSCENE_List * l = clipped_fill(s, make_union(s, m), 5, 5);
    SinkRef sink;
    RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 5, 5));
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    size_t need = 0;
    EXPECT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
    return need;
  };
  const size_t two = need_for(2), thirty = need_for(30);
  EXPECT_GE(thirty, two);
  EXPECT_LT(thirty - two, 32u * 1024u) << "one member's geometry is live at a time";
  EXPECT_GT(two, 3000u * 8u) << "and the geometry of one is in it";
}

TEST(RenderClipUnionGeneral, TheStepsAtTheBottomOfTheLeftSideOfACellNeverChangeALaterCell) {
  // A part's winding steps where its pieces start or end on the left side of a cell; a step at the cell's last row weighs nothing.  Two cells made with one
  // scratch, the first with a step at 64 and the second with none, give the second the area it has with a scratch of its own.
  std::vector<unsigned char> raw(gshcpu_exact_group_scratch_bytes(16, 3) + 16);
  GSHCPU_Group_Scratch gs;
  gshcpu_exact_group_scratch_init(&gs, (void *)(((uintptr_t)raw.data() + 15u) & ~(uintptr_t)15u), 16, 3);
  std::vector<unsigned char> raw2(gshcpu_exact_group_scratch_bytes(16, 3) + 16);
  GSHCPU_Group_Scratch fresh;
  gshcpu_exact_group_scratch_init(&fresh, (void *)(((uintptr_t)raw2.data() + 15u) & ~(uintptr_t)15u), 16, 3);
  std::memset(raw2.data(), 0, raw2.size());
  uint8_t rules[3] = {0, 0, 0};
  GSHCPU_Exact_Params params;
  std::memset(&params, 0, sizeof params);
  params.mode = GSHCPU_EXACT_GROUP;
  params.rule_of = rules;
  params.parts = 2;
  // Cell A: part 0 has a piece that starts on the left side and runs to its last row (0, 20) -> (0, 64) and back along x = 40: a strip 40 wide from row 20 down.
  const GSHCPU_Piece a0[2] = {{0, 20, 0, 64}, {40, 64, 40, 20}};
  const GSHCPU_Piece a1[2] = {{10, 0, 10, 64}, {30, 64, 30, 0}};
  // Cell B: the same two parts without a piece on the left side.
  const GSHCPU_Piece b0[2] = {{5, 0, 5, 64}, {25, 64, 25, 0}};
  const GSHCPU_Piece b1[2] = {{15, 0, 15, 64}, {45, 64, 45, 0}};
  auto cell_of = [&](const GSHCPU_Piece * p0, const GSHCPU_Piece * p1, const GSHCPU_Piece ** pieces, size_t * counts, int64_t * left, int64_t * dense,
                      GSHCPU_Exact_Cell * cell) {
    pieces[0] = p0;
    pieces[1] = p1;
    counts[0] = counts[1] = 2;
    for (int p = 0; p < 2; p++) {
      int64_t cover = 0, area = 0;
      for (size_t i = 0; i < 2; i++) {
        const GSHCPU_Piece q = pieces[p][i];
        cover += (int64_t)q.y1 - q.y0;
        area += ((int64_t)q.y1 - q.y0) * ((int64_t)q.x0 + q.x1);
      }
      // the winding on the left side is 0 everywhere but where part 0's first piece (cell A) crosses it: its steps account for the cover left of the cell
      left[p] = 0;
      dense[p] = GSHCPU_RENDER_EXACT_EPS * (128 * (left[p] + cover) - area);
    }
    std::memset(cell, 0, sizeof *cell);
    cell->gpieces = pieces;
    cell->gcount = counts;
    cell->ga_left = left;
    cell->gdense = dense;
  };
  const GSHCPU_Piece * pa[2];
  const GSHCPU_Piece * pb[2];
  size_t ca[2], cb[2];
  int64_t la[2], da[2], lb[2], db[2];
  GSHCPU_Exact_Cell cell_a, cell_b;
  cell_of(a0, a1, pa, ca, la, da, &cell_a);
  cell_of(b0, b1, pb, cb, lb, db, &cell_b);
  // A's first piece starts at (0, 20) and ends at (0, 64): the winding left of the cell steps by -1 at row 20 and back at row 64, so the cover left of the
  // cell is -44 (UC4: the integral of the winding over the side), and the dense area follows from the pieces as the rasterizer sums them.
  la[0] = -44;
  da[0] = GSHCPU_RENDER_EXACT_EPS * (128 * (la[0] + 0) - (44 * (0 + 0) + (-44) * (40 + 40)));
  params.gscratch = &gs;
  int64_t area = 0, again = 0;
  uint64_t work = 0;
  const int first = gshcpu_exact_cell(&params, &gs.base, &cell_a, &area, &work);
  const int used = gshcpu_exact_cell(&params, &gs.base, &cell_b, &area, &work);
  params.gscratch = &fresh;
  const int alone = gshcpu_exact_cell(&params, &fresh.base, &cell_b, &again, &work);
  EXPECT_EQ(first, GSHCPU_EXACT_OK) << "the first cell holds a piece that ends at (0, 64)";
  EXPECT_EQ(used, GSHCPU_EXACT_OK);
  EXPECT_EQ(alone, GSHCPU_EXACT_OK);
  EXPECT_EQ(area, again) << "the second cell has the same area with the first cell's scratch as with a scratch of its own (the first's status was " << first << ")";
  EXPECT_EQ(again, 2 * 64 * 40) << "the union of the strips [5, 25] and [15, 45]: 40 wide";
}

TEST(RenderClipUnionGeneral, FiveHundredAndTwelveParallelPathsOfOneRegionAreFiveHundredAndTwelveEvenOddPartsOfOneCell) {
  // 512 paths of one rectangle that differ in which of nine points they have on the top edge, all even-odd, in one pixel: the pixel's step has 512 parts,
  // past a byte's 256.  Each part is inside the rectangle, so the union is the rectangle; two parts taken for one would have a winding of two there, which is
  // outside under even-odd, and with every part twinned like that the rectangle would be gone.
  GSHSCENE_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.max_clip_members = GSHSCENE_CLIP_MEMBERS_CEILING;
  Scene s(&limits);
  std::vector<GSHSCENE_Clip_Member> members;
  for (int subset = 0; subset < 512; subset++) {
    Poly v = {{10, 10}};
    for (int bit = 0; bit < 9; bit++) {
      if (subset & (1 << bit)) {
        v.push_back({12 + bit * 4, 10});
      }
    }
    v.push_back({50, 10});
    v.push_back({50, 50});
    v.push_back({10, 50});
    members.push_back(member(lattice_polygon(s, v), GSHGEOM_FILL_EVEN_ODD));
  }
  Draw d = draw_union(s, members, 1, 1);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_EQ(d.counters.union_groups, 1u);
  EXPECT_EQ(d.at8(0, 0, 3), 100u) << "the rectangle of 40 by 40 units: 3200 of 8192 doubled units, 100 levels";
}

TEST(RenderClipUnionGeneral, ThinMembersThatAllCrossEachOtherInOnePixelAgreeWithPointSampling) {
  // Eight slivers from the left side of the pixel to its right, in opposite order at the two ends, so that every pair crosses (28 meetings), under
  // the rules in turn: the exchanges of neighbours follow one another down to chains of four and more, which a gap's records must carry through.
  for (int variant = 0; variant < 4; variant++) {
    std::vector<Poly> polys;
    std::vector<bool> even_odd;
    for (int k = 0; k < 8; k++) {
      const int yl = 6 + k * 7 + (variant & 1 ? k % 3 : 0), yr = 6 + (7 - k) * 7 + (variant & 1 ? (k * 5) % 4 : 0), w = 2 + (k + variant) % 3;
      polys.push_back({{0, yl}, {63, yr}, {63, yr + w}, {0, yl + w}});
      even_odd.push_back(variant >= 2 && k % 2 == 1);
    }
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    for (size_t i = 0; i < polys.size(); i++) {
      members.push_back(member(lattice_polygon(s, polys[i]), even_odd[i] ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO));
    }
    const Draw d = draw_union(s, members, 1, 1);
    ASSERT_EQ(d.result, GSHSCENE_OK) << variant;
    EXPECT_EQ(d.counters.union_groups, 1u) << variant;
    EXPECT_NEAR((double)d.at8(0, 0, 3), sampled_level(polys, even_odd, 0, 0, 512), 2.0) << variant;
  }
}

TEST(RenderClipUnionGeneral, MoreThanAHundredAndTwentySevenPiecesInAPixelAreHeldByTheMarksAndNeverCarryIntoTheWholePixelMark) {
  // Members in boxes apart inside one pixel, each a polygon of 24 points: 128 to 255 pieces (one past the marks' 127 at the last member), then 256 and more
  // (a byte wraps).  A disjoint union, so the fast tier's sum is the answer, and the exact step on one group must give it.
  for (int members_count : {7, 8, 12}) {
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    for (int k = 0; k < members_count; k++) {
      const int x0 = 2 + (k % 4) * 15, y0 = 2 + (k / 4) * 20;
      Poly v;
      for (int i = 0; i < 24; i++) {
        const int ring = i % 2 ? 7 : 4;
        const int dx = (int)std::lround(ring * std::cos(i * 3.14159265358979 / 12.0)), dy = (int)std::lround(ring * std::sin(i * 3.14159265358979 / 12.0));
        v.push_back({x0 + 8 + dx, y0 + 8 + dy});
      }
      members.push_back(member(lattice_polygon(s, v)));
    }
    GSHSCENE_List * l = clipped_fill(s, make_union(s, members), 1, 1);
    const Draw fast = draw(l, 1, 1);
    const Draw one = draw(l, 1, 1, one_group());
    ASSERT_EQ(one.result, GSHSCENE_OK);
    EXPECT_EQ(fast.counters.union_fast, 1u) << members_count;
    EXPECT_GT(one.counters.union_pieces, 127u) << members_count;
    if (members_count == 7) {
      EXPECT_LT(one.counters.union_pieces, 256u) << "the marks end between 128 and 255: a count kept in the seven bits has the whole-pixel bit set";
    }
    EXPECT_EQ(one.at8(0, 0, 3), fast.at8(0, 0, 3)) << members_count;
    EXPECT_LT(fast.at8(0, 0, 3), 255u);
    EXPECT_GT(fast.at8(0, 0, 3), 0u);
  }
}

TEST(RenderClipUnionGeneral, TheDefaultLimitOnTheKeptPiecesIsPastSixtyFiveThousandAndBelowAHundredAndThirtyOneThousand) {
  // Two zigzag bands whose teeth cross, 35000 edges each, in one row of 547 pixels: about 70000 pieces are kept, which the default (2^17) draws and
  // a limit of 2^16 refuses.
  Scene s;
  const int N = 35000;
  auto band = [&](int shift) {
    Poly v;
    for (int i = 0; i <= N; i++) {
      v.push_back({i + shift, ((i + shift) % 2) * 20});
    }
    v.push_back({N + shift, 40});
    v.push_back({shift, 40});
    return lattice_polygon(s, v);
  };
  const GSHSCENE_Resource * a = band(0);
  const GSHSCENE_Resource * b = band(1);
  const GSHSCENE_Resource * u = make_union(s, {member(a), member(b)});
  const uint32_t W = (uint32_t)((N + 1 + 63) / 64);
  GSHSCENE_List * l = clipped_fill(s, u, W, 1);
  const Draw by_default = draw(l, W, 1);
  ASSERT_EQ(by_default.result, GSHSCENE_OK);
  EXPECT_EQ(by_default.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_GT(by_default.counters.union_pieces, 65536u);
  EXPECT_LT(by_default.counters.union_pieces, 131072u);
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_union_pieces = 65536;
  const Draw smaller = draw(l, W, 1, with_limits(&lim));
  EXPECT_EQ(smaller.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
}

/* --- caps, tiers, counters ------------------------------------------------------------------------------------------------ */

TEST(RenderClipUnionGeneral, TheTiersAreShownTakenAndTheCountersCountThem) {
  Scene s;
  const GSHSCENE_Resource * apart = make_union(s, {member(s.rect(0, 0, 2, 2)), member(s.rect(3, 0, 5, 2))});
  const GSHSCENE_Resource * crossing = make_union(s, {member(lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}})),
      member(lattice_polygon(s, {{20, 100}, {190, 20}, {120, 125}}))});
  Draw a = draw(clipped_fill(s, apart, 6, 2), 6, 2);
  EXPECT_EQ(a.counters.union_fast, 1u);
  EXPECT_EQ(a.counters.union_general, 0u);
  EXPECT_EQ(a.counters.union_groups, 0u);
  EXPECT_EQ(a.counters.union_exact, 0u);
  Draw c = draw(clipped_fill(s, crossing, 4, 2), 4, 2);
  EXPECT_EQ(c.counters.union_fast, 0u);
  EXPECT_EQ(c.counters.union_general, 1u);
  EXPECT_EQ(c.counters.union_groups, 1u);
  EXPECT_GT(c.counters.union_exact, 0u);
  EXPECT_GT(c.counters.union_pieces, 0u);
  EXPECT_EQ(c.counters.union_sweeps, 2u);
  // The tier forced off: the overlapping union is refused UNION_OVERLAP and nothing is drawn.
  Options no_general;
  no_general.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION_GENERAL;
  Draw n = draw(clipped_fill(s, crossing, 4, 2), 4, 2, no_general);
  EXPECT_EQ(n.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_UNION_OVERLAP), 1u);
  EXPECT_EQ(n.counters.union_general, 0u);
  EXPECT_EQ(n.counters.union_exact, 0u);
  // A list with no union pays nothing for the tier.
  GSHSCENE_List * plain = s.open();
  GSHSCENE_Paint wh = white();
  ASSERT_EQ(gshscene_list_fill(plain, s.rect(0, 0, 3, 3), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(plain), GSHSCENE_OK);
  Draw p = draw(plain, 4, 4);
  EXPECT_EQ(p.counters.union_general + p.counters.union_groups + p.counters.union_exact + p.counters.union_pieces, 0u);
}

/* --- the damage ---------------------------------------------------------------------------------------------------------------- */

TEST(RenderClipUnionGeneral, ADamagedRenderEqualsTheWholeOnItsPartAndRefusesWhatTheWholeRefuses) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(lattice_polygon(s, {{10, 10}, {500, 50}, {40, 300}})),
      member(lattice_polygon(s, {{20, 280}, {490, 20}, {420, 380}})), member(s.rect(1, 1, 3, 3))});
  GSHSCENE_List * l = clipped_fill(s, u, 9, 7);
  const Draw whole = draw(l, 9, 7);
  GSHSCENE_Rect part{3, 2, 6, 5};
  Options o;
  o.damage = &part;
  const Draw dmg = draw(l, 9, 7, o);
  ASSERT_EQ(dmg.result, GSHSCENE_OK);
  EXPECT_EQ(dmg.counters.union_general, 1u);
  for (int y = 2; y < 5; y++) {
    for (int x = 3; x < 6; x++) {
      EXPECT_EQ(dmg.at8((uint32_t)x, (uint32_t)y, 3), whole.at8((uint32_t)x, (uint32_t)y, 3)) << x << "," << y;
    }
  }
  EXPECT_EQ(dmg.counters.union_tests, whole.counters.union_tests);
  EXPECT_EQ(dmg.counters.union_exact, whole.counters.union_exact) << "the tier works over the whole render's extent (CG9)";
  // A limit the whole render meets far from the damage: the damaged render meets it too (S4), and draws nothing.
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_union_pieces = 1;
  Options tight = with_limits(&lim);
  const Draw refused_whole = draw(l, 9, 7, tight);
  tight.damage = &part;
  const Draw refused_part = draw(l, 9, 7, tight);
  EXPECT_EQ(refused_whole.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  EXPECT_EQ(refused_part.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  for (uint8_t a : alphas(refused_part)) {
    EXPECT_EQ(a, 0u);
  }
}

/* --- the refusals that remain --------------------------------------------------------------------------------------------- */

namespace {

/** Whether a union push of @p u drawn with @p lim draws (no refusal), over a w by h canvas. */
bool draws(Scene & s, const GSHSCENE_Resource * u, uint32_t w, uint32_t h, const GSHCPU_Limits & lim, bool strict = false) {
  Options o = with_limits(&lim);
  o.flags = strict ? GSHSCENE_RENDER_STRICT : 0;
  Draw d = draw(clipped_fill(s, u, w, h), w, h, o);
  if (strict) {
    return d.result == GSHSCENE_OK;
  }
  return d.result == GSHSCENE_OK && d.count(GSHSCENE_ENTRY_REFUSED) == 0u;
}

}  // namespace

TEST(RenderClipUnionGeneral, ThePiecesTheTierKeepsAreCappedExactlyAndOneMoreIsTheResourceLimit) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(lattice_polygon(s, {{10, 10}, {500, 50}, {40, 300}})),
      member(lattice_polygon(s, {{20, 280}, {490, 20}, {420, 380}}))});
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  uint64_t least = 0;
  for (uint64_t cap = 1; cap < 4096 && !least; cap++) {
    lim.max_union_pieces = cap;
    if (draws(s, u, 8, 7, lim)) {
      least = cap;
    }
  }
  ASSERT_GT(least, 1u);
  lim.max_union_pieces = least;
  EXPECT_TRUE(draws(s, u, 8, 7, lim)) << "exactly the pieces it keeps";
  lim.max_union_pieces = least - 1;
  Draw over = draw(clipped_fill(s, u, 8, 7), 8, 7, with_limits(&lim));
  EXPECT_EQ(over.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u) << "one fewer: the push is the sink's resource limit";
  for (uint8_t a : alphas(over)) {
    EXPECT_EQ(a, 0u) << "the block is skipped: nothing is drawn with the union left out";
  }
  EXPECT_FALSE(draws(s, u, 8, 7, lim, true)) << "strict fails at the clip";
  // The counter agrees with the cap that was just enough.
  lim.max_union_pieces = least;
  Draw ok = draw(clipped_fill(s, u, 8, 7), 8, 7, with_limits(&lim));
  EXPECT_EQ(ok.counters.union_pieces, least);
}

TEST(RenderClipUnionGeneral, AnOversizedValueOfThePiecesLimitIsInvalidAtSinkCreation) {
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  GSHSCENE_Sink * sink = nullptr;
  lim.max_union_pieces = GSHCPU_LIMITS_MAX_UNION_PIECES;
  EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, &lim, &sink), GSHCPU_OK);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  lim.max_union_pieces = GSHCPU_LIMITS_MAX_UNION_PIECES + 1;
  sink = nullptr;
  EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, &lim, &sink), GSHCPU_ERR_INVALID);
}

TEST(RenderClipUnionGeneral, TheCrossingsBudgetRefusesAPixelPastItAndTheDamageDoesNotChangeIt) {
  // Five slivers cross in pixel (0, 0): ten meetings at most.
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(lattice_polygon(s, {{0, 0}, {60, 20}, {0, 9}})),
      member(lattice_polygon(s, {{0, 30}, {63, 3}, {63, 14}})), member(lattice_polygon(s, {{5, 0}, {30, 63}, {14, 63}})),
      member(lattice_polygon(s, {{60, 0}, {33, 63}, {49, 63}})), member(lattice_polygon(s, {{0, 55}, {63, 40}, {63, 51}}))});
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  uint64_t least = 0;
  for (uint64_t budget = 1; budget < 64 && !least; budget++) {
    lim.max_exact_crossings = budget;
    if (draws(s, u, 1, 1, lim)) {
      least = budget;
    }
  }
  ASSERT_GT(least, 1u) << "a budget of one is refused";
  lim.max_exact_crossings = least;
  EXPECT_TRUE(draws(s, u, 1, 1, lim));
  lim.max_exact_crossings = least - 1;
  Draw over = draw(clipped_fill(s, u, 1, 1), 1, 1, with_limits(&lim));
  EXPECT_EQ(over.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  EXPECT_EQ(over.at8(0, 0, 3), 0u);
  EXPECT_FALSE(draws(s, u, 1, 1, lim, true));
  // The same on a bigger canvas with a damage that does not include the pixel: refused all the same (S4).
  GSHSCENE_Rect elsewhere{2, 2, 4, 4};
  Options o = with_limits(&lim);
  o.damage = &elsewhere;
  Draw part = draw(clipped_fill(s, u, 4, 4), 4, 4, o);
  EXPECT_EQ(part.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
}

TEST(RenderClipUnionGeneral, AllTheMembersPiecesInAPixelAreCappedByMaxExactFragmentsAndOneMoreIsTheResourceLimit) {
  Scene s;
  // Two fans of many edges in one pixel, 24 pieces of each (a piece for each edge, none of them cut): 48 in the pixel.
  auto fan = [&](int sign) {
    Poly v;
    for (int i = 0; i < 24; i++) {
      v.push_back({8 + i * 2, sign > 0 ? 8 + (i * i) % 40 : 55 - (i * i) % 40});
    }
    return lattice_polygon(s, v);
  };
  const GSHSCENE_Resource * u = make_union(s, {member(fan(1)), member(fan(-1))});
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  Draw ok = draw(clipped_fill(s, u, 1, 1), 1, 1, with_limits(&lim));
  ASSERT_EQ(ok.count(GSHSCENE_ENTRY_REFUSED), 0u);
  ASSERT_EQ(ok.counters.union_groups, 1u);
  const uint64_t together = ok.counters.union_pieces;
  ASSERT_GT(together, 24u);
  // The least cap that draws is the pieces of the pixel in all (the scratch of one pixel holds the cap, CG8), not those of a member.
  uint64_t least = 0;
  for (uint64_t cap = 1; cap < 200 && !least; cap++) {
    lim.max_exact_fragments = cap;
    if (draws(s, u, 1, 1, lim)) {
      least = cap;
    }
  }
  EXPECT_EQ(least, together);
  lim.max_exact_fragments = together - 1;
  Draw over = draw(clipped_fill(s, u, 1, 1), 1, 1, with_limits(&lim));
  EXPECT_EQ(over.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  EXPECT_EQ(over.at8(0, 0, 3), 0u);
  EXPECT_FALSE(draws(s, u, 1, 1, lim, true));
  lim.max_exact_fragments = 12;  // under a member's own pieces too
  EXPECT_FALSE(draws(s, u, 1, 1, lim));
}

namespace {

/** Sets a test switch of the library for a scope, and puts it back. */
struct SwitchGuard {
  uint64_t * what;
  uint64_t was;
  SwitchGuard(uint64_t * w, uint64_t value) : what(w), was(*w) {
    *what = value;
  }
  ~SwitchGuard() {
    *what = was;
  }
};

const GSHSCENE_Resource * five_slivers(Scene & s) {
  return make_union(s, {member(lattice_polygon(s, {{0, 0}, {60, 20}, {0, 9}})), member(lattice_polygon(s, {{0, 30}, {63, 3}, {63, 14}})),
      member(lattice_polygon(s, {{5, 0}, {30, 63}, {14, 63}})), member(lattice_polygon(s, {{60, 0}, {33, 63}, {49, 63}})),
      member(lattice_polygon(s, {{0, 55}, {63, 40}, {63, 51}}))});
}

}  // namespace

TEST(RenderClipUnionGeneral, TheCrossingsOfAGroupsPixelAreHeldBelowTheLimitOfTheFloorsOffsetWhateverTheBudget) {
  // CG6: a group's pixel may have no more than 2^25 meetings (the offset of the floor must stay below half a unit), whatever UC19's budget the sink
  // was given; the switch lowers the hold, and the pixel of five crossing slivers (about ten meetings) is refused at it and drawn above it.
  Scene s;
  const GSHSCENE_Resource * u = five_slivers(s);
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_exact_crossings = GSHCPU_LIMITS_MAX_EXACT_CROSSINGS;  // UC19's ceiling, far above the hold
  EXPECT_EQ(gshcpu_exact_group_crossings, GSHCPU_EXACT_GROUP_CROSSINGS);
  EXPECT_EQ(GSHCPU_EXACT_GROUP_CROSSINGS, (uint64_t)1 << 25);
  EXPECT_TRUE(draws(s, u, 1, 1, lim));
  uint64_t least = 0;
  for (uint64_t hold = 1; hold < 64 && !least; hold++) {
    SwitchGuard g(&gshcpu_exact_group_crossings, hold);
    if (draws(s, u, 1, 1, lim)) {
      least = hold;
    }
  }
  ASSERT_GT(least, 1u);
  {
    SwitchGuard g(&gshcpu_exact_group_crossings, least);
    EXPECT_TRUE(draws(s, u, 1, 1, lim));
  }
  {
    SwitchGuard g(&gshcpu_exact_group_crossings, least - 1);
    Draw d = draw(clipped_fill(s, u, 1, 1), 1, 1, with_limits(&lim));
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
    EXPECT_EQ(d.at8(0, 0, 3), 0u);
  }
  // The sink's own budget (UC19) is still the tighter when it is: the same least value as the hold gave.
  uint64_t budget = 0;
  GSHCPU_Limits tight = lim;
  for (uint64_t b = 1; b < 64 && !budget; b++) {
    tight.max_exact_crossings = b;
    if (draws(s, u, 1, 1, tight)) {
      budget = b;
    }
  }
  EXPECT_EQ(budget, least);
}

TEST(RenderClipUnionGeneral, AGroupWhoseRectangleHasMorePixelsThanARecordCanNameIsTheResourceLimit) {
  // A kept record names its pixel of the group by a 32-bit index (CG9): a group over 2^32 - 1 pixels is refused before anything is taken.  The
  // switch lowers the limit to a few pixels so that the check can be seen.
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 3, 3)), member(s.rect(2, 2, 5, 5))});
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  EXPECT_EQ(gshcpu_union_pixels_max, (uint64_t)UINT32_MAX);
  EXPECT_TRUE(draws(s, u, 5, 5, lim));
  {
    SwitchGuard g(&gshcpu_union_pixels_max, 25);  // the group is 5 by 5 pixels
    EXPECT_TRUE(draws(s, u, 5, 5, lim)) << "exactly the pixels it has";
  }
  {
    SwitchGuard g(&gshcpu_union_pixels_max, 24);
    Draw d = draw(clipped_fill(s, u, 5, 5), 5, 5, with_limits(&lim));
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
    for (uint8_t a : alphas(d)) {
      EXPECT_EQ(a, 0u);
    }
  }
}

TEST(RenderClipUnionGeneral, AMemberPastTheCellCapOrTheLatticeRefusesTheWholePush) {
  Scene s;
  std::vector<double> xy;
  for (int i = 0; i < 64; i++) {
    const double a = 6.2831853 * i / 64.0, r = (i % 2) ? 3.5 : 2.0;
    xy.push_back(4.0 + r * std::cos(a));
    xy.push_back(4.0 + r * std::sin(a));
  }
  const GSHSCENE_Resource * big = s.polygon(xy);
  const GSHSCENE_Resource * small = s.rect(1, 1, 5, 5);
  const GSHSCENE_Resource * u = make_union(s, {member(small), member(big)});
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  uint64_t least = 0;
  for (uint64_t cap = 1; cap < 2000 && !least; cap++) {
    lim.max_deposits = cap;
    Draw d = draw(clipped_fill(s, u, 8, 8), 8, 8, with_limits(&lim));
    bool at_the_push = false;
    for (size_t i = 0; i < d.entries.size(); i++) {
      at_the_push = at_the_push || (d.entries[i].reason == GSHSCENE_REASON_RESOURCE_LIMIT && d.chains[i] == std::vector<size_t>{0});
    }
    if (!at_the_push && d.count(GSHSCENE_ENTRY_REFUSED) == 0) {
      least = cap;
    }
  }
  ASSERT_GT(least, 3u);
  lim.max_deposits = least - 1;
  Draw over = draw(clipped_fill(s, u, 8, 8), 8, 8, with_limits(&lim));
  EXPECT_GE(over.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  EXPECT_EQ(over.at8(2, 2, 3), 0u) << "the small member is not drawn alone";
  // A coordinate that cannot be rounded into 26.6 under the map: an unprovable member, the push is the sink's resource limit.
  const GSHSCENE_Resource * far = s.rect(0, 0, 4.0e12, 4.0e12);
  const GSHSCENE_Resource * uf = make_union(s, {member(small), member(far)});
  Draw f = draw(clipped_fill(s, uf, 4, 4), 4, 4);
  EXPECT_EQ(f.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  EXPECT_EQ(f.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_UNION_OVERLAP), 0u) << "the general tier takes it, and it is the sink's limit";
}

TEST(RenderClipUnionGeneral, TheGeneralTierBillsTheComparisonsOfItsGroupsAndTheLimitPassesExactly) {
  Scene s;
  // 4 overlapping members: the predicate stops at the first pair (1 comparison), the groups cost all 6.
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 3, 3)), member(s.rect(2, 0, 5, 3)), member(s.rect(4, 0, 7, 3)), member(s.rect(6, 0, 9, 3))});
  GSHSCENE_List * l = clipped_fill(s, u, 9, 3);
  Draw d = draw(l, 9, 3);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  const uint64_t billed = d.counters.union_tests;
  EXPECT_EQ(billed, 1u + 6u) << "the predicate's comparisons, then the n (n - 1) / 2 of the groups";
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_union_tests = billed;
  EXPECT_EQ(draw(l, 9, 3, with_limits(&lim)).count(GSHSCENE_ENTRY_REFUSED), 0u);
  lim.max_union_tests = billed - 1;
  EXPECT_EQ(draw(l, 9, 3, with_limits(&lim)).count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  // A disjoint union drawn by the fast tier costs only the predicate's; forced general it costs the groups' too.
  const GSHSCENE_Resource * apart = make_union(s, {member(s.rect(0, 0, 1, 1)), member(s.rect(2, 0, 3, 1)), member(s.rect(4, 0, 5, 1))});
  GSHSCENE_List * la = clipped_fill(s, apart, 5, 1);
  EXPECT_EQ(draw(la, 5, 1).counters.union_tests, 3u);
  EXPECT_EQ(draw(la, 5, 1, general_only()).counters.union_tests, 3u + 3u);
}

TEST(RenderClipUnionGeneral, AUnionInAPatternsTileIsTheUnionDrawnInEachCopy) {
  // CU21: overlapping members in a tile, drawn as the tile's picture instanced at each copy's place is.
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 4, 6)), member(s.rect(3, 0, 6, 6))});
  GSHSCENE_List * t = s.open();
  GSHSCENE_Paint red = rh::solid(1.0, 0.0, 0.0, 1.0);
  ASSERT_EQ(gshscene_list_clip_push(t, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(t, s.rect(0, 0, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(t), GSHSCENE_OK);
  const GSHSCENE_Resource * tile = s.picture(t);
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = tile;
  pt.w = 6;
  pt.h = 6;
  pt.x_step = 6;
  pt.y_step = 6;
  const GSHSCENE_Resource * pattern = nullptr;
  ASSERT_EQ(gshscene_store_intern_pattern(s.store, &pt, &pattern), GSHSCENE_OK);
  s.held.push_back(pattern);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint pp{};
  pp.kind = GSHSCENE_PAINT_PATTERN;
  pp.resource = pattern;
  pp.rgba[3] = 1.0;
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 12, 12), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Draw d = draw(l, 12, 12);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
  GSHSCENE_List * direct = s.open();
  for (double y : {0.0, 6.0}) {
    for (double x : {0.0, 6.0}) {
      GSHGEOM_Map at;
      ASSERT_EQ(gshgeom_map_translate(2, x, y, 0.0, &at), GSHGEOM_OK);
      ASSERT_EQ(gshscene_list_instance(direct, tile, &at, 0, 0.0), GSHSCENE_OK);
    }
  }
  ASSERT_EQ(gshscene_list_close(direct), GSHSCENE_OK);
  Draw e = draw(direct, 12, 12);
  ASSERT_EQ(e.result, GSHSCENE_OK);
  EXPECT_EQ(d.bytes, e.bytes);
  EXPECT_GT(d.counters.union_groups, 0u);
  EXPECT_NE(d.hash, 0u);
}

TEST(RenderClipUnionGeneral, TheWorkspaceMeasuredForAnOverlappingUnionIsEnoughAndOneByteLessIsNot) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}})),
      member(lattice_polygon(s, {{20, 100}, {190, 20}, {120, 125}})), member(s.rect(0.2, 0.2, 2, 1.8))});
  GSHSCENE_List * l = clipped_fill(s, u, 4, 3);
  Options cm;
  cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw a = draw(l, 4, 3, cm);
  Draw b = draw(l, 4, 3);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_GT(a.need, b.need - 1u);
  // One byte short of the measure is refused, and nothing is drawn.
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 3));
  SinkRef sink;
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> mem(need);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  req.workspace = mem.data();
  req.workspace_bytes = need - 1u;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT);
  const std::string untouched = r.bytes();
  EXPECT_TRUE(std::all_of(untouched.begin(), untouched.end(), [](char c) { return c == 0; }));
  // The pieces limit is in the measure: a smaller limit asks for less memory.
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_union_pieces = 64;
  Draw small = draw(l, 4, 3, [&] { Options o = with_limits(&lim); o.flags = GSHSCENE_RENDER_CALLER_MEMORY; return o; }());
  EXPECT_LT(small.need, a.need);
}

TEST(RenderClipUnionGeneral, ADeclaredBoundCoversAnOverlappingUnionPushAndItsWork) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}})),
      member(lattice_polygon(s, {{20, 100}, {190, 20}, {120, 125}}))});
  Draw d = draw(clipped_fill(s, u, 4, 3), 4, 3);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  SinkRef sink;
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 3));
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Sink_Caps caps{};
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
  EXPECT_LE(d.summary.work_max, caps.cancel_work_bound);
  EXPECT_GT(d.summary.work_max, 12u) << "the passes over the pixels, the members and the exact step are counted";
}

/* --- the exact step of a group, directly ---------------------------------------------------------------------------------- */

TEST(RenderClipUnionGeneral, TheStepRefusesMoreThanItsPartsAndMissingRules) {
  std::vector<unsigned char> raw(gshcpu_exact_group_scratch_bytes(16, 3) + 16);
  GSHCPU_Group_Scratch gs;
  gshcpu_exact_group_scratch_init(&gs, (void *)(((uintptr_t)raw.data() + 15u) & ~(uintptr_t)15u), 16, 3);
  GSHCPU_Piece down{10, 0, 10, 64}, up{30, 64, 30, 0};
  const GSHCPU_Piece pair[2] = {down, up};
  const GSHCPU_Piece * pieces[4] = {pair, pair, pair, pair};
  size_t counts[4] = {2, 2, 2, 2};
  int64_t left[4] = {0, 0, 0, 0};
  // dense = eps (128 (left + cover) - area): the pieces' own sums, as the rasterizer's identity says
  int64_t dense[4];
  for (int i = 0; i < 4; i++) {
    dense[i] = GSHCPU_RENDER_EXACT_EPS * (128 * (0 + 0) - (64 * (10 + 10) + (-64) * (30 + 30)));
  }
  uint8_t rules[4] = {0, 0, 0, 0};
  GSHCPU_Exact_Cell cell;
  std::memset(&cell, 0, sizeof cell);
  cell.gpieces = pieces;
  cell.gcount = counts;
  cell.ga_left = left;
  cell.gdense = dense;
  GSHCPU_Exact_Params params;
  std::memset(&params, 0, sizeof params);
  params.mode = GSHCPU_EXACT_GROUP;
  params.gscratch = &gs;
  params.rule_of = rules;
  params.parts = 3;
  int64_t area = -1;
  uint64_t work = 0;
  EXPECT_EQ(gshcpu_exact_cell(&params, &gs.base, &cell, &area, &work), GSHCPU_EXACT_OK);
  EXPECT_EQ(area, 2 * 64 * 20) << "three copies of one strip 20 wide: the union is the strip, doubled units";
  params.parts = 4;
  EXPECT_EQ(gshcpu_exact_cell(&params, &gs.base, &cell, &area, &work), GSHCPU_EXACT_LIMIT) << "more parts than the scratch holds";
  params.parts = 3;
  params.rule_of = nullptr;
  EXPECT_EQ(gshcpu_exact_cell(&params, &gs.base, &cell, &area, &work), GSHCPU_EXACT_LIMIT) << "no rules";
  params.rule_of = rules;
  params.gscratch = nullptr;
  EXPECT_EQ(gshcpu_exact_cell(&params, &gs.base, &cell, &area, &work), GSHCPU_EXACT_LIMIT) << "no scratch of parts";
  // Even-odd on every part: three equal strips are covered three times, an odd number, so the region is the strip again; two parts
  // under even-odd would cancel only if they were one region's winding, which parts are not (each is its own rule's test).
  params.gscratch = &gs;
  rules[0] = rules[1] = rules[2] = 1;
  EXPECT_EQ(gshcpu_exact_cell(&params, &gs.base, &cell, &area, &work), GSHCPU_EXACT_OK);
  EXPECT_EQ(area, 2 * 64 * 20);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(RenderClipUnionGeneral, ThinMembersOfRandomOrientationRuleAndSlopeThatCrossInOnePixelAgreeWithPointSampling) {
  // 1500 fans of 3 to 8 slivers, the ends in any order, either winding, either rule: the exchanges of neighbours down to chains, where a gap
  // keeps the winding of the fragment on each side of it, are taken in every order a pixel can have them.
  uint64_t rng = 0x9e3779b97f4a7c15ull;
  auto next = [&]() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
  int bad = 0;
  for (int seed = 0; seed < 1500; seed++) {
    const int n = 3 + (int)(next() % 6);
    std::vector<Poly> polys;
    std::vector<bool> even_odd;
    for (int k = 0; k < n; k++) {
      const int yl = 4 + (int)(next() % 50), yr = 4 + (int)(next() % 50), w = 2 + (int)(next() % 5);
      Poly p = {{0, yl}, {63, yr}, {63, yr + w}, {0, yl + w}};
      if (next() % 2) {
        std::reverse(p.begin(), p.end());
      }
      polys.push_back(p);
      even_odd.push_back(next() % 3 == 0);
    }
    Scene s;
    std::vector<GSHSCENE_Clip_Member> members;
    for (size_t i = 0; i < polys.size(); i++) {
      members.push_back(member(lattice_polygon(s, polys[i]), even_odd[i] ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO));
    }
    const Draw d = draw_union(s, members, 1, 1);
    ASSERT_EQ(d.result, GSHSCENE_OK) << seed;
    const double ref = sampled_level(polys, even_odd, 0, 0, 256);
    if (std::abs((double)d.at8(0, 0, 3) - ref) > 3.0) {
      bad++;
      ADD_FAILURE() << "seed " << seed << " n " << n << " got " << (int)d.at8(0, 0, 3) << " ref " << ref;
    }
  }
  EXPECT_EQ(bad, 0);
}
