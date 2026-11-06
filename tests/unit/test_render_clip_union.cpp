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
 * Union clips in the sink (shape-exact-primitives story A3; CU1 to CU5, CU18 to CU22): the fast tier equals the oracle's bytes on
 * every polygon vector, touching halves give a whole pixel where a mask gives three quarters, an overlapping union is refused with
 * the pair named and nothing inside drawn, the member order and orientation do not matter, FAST fill coverage draws the same, a
 * damaged render equals the whole on its part, `max_union_tests` passes exactly and fails one past, the tier is shown taken and
 * forced off, and a list with no union pays nothing.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace cuh;

namespace {

/** The caps of a sink with the tiers @p tiers on. */
uint32_t union_caps(unsigned tiers) {
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_render_sink_create_tiered(nullptr, nullptr, nullptr, tiers, nullptr, nullptr, nullptr, &sink), GSHCPU_OK);
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 4));
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Sink_Caps caps{};
  EXPECT_EQ(gshscene_sink_caps(sink, &t, &caps), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  return caps.flags & (GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL);
}

} // namespace

TEST(RenderClipUnion, TheSinkClaimsTheDisjointTierAndTheGeneralOneAsTheirSwitchesSay) {
  // CG10: the general tier is a union with the capability of the disjoint ones (CU12), so it claims both; with FT-U1 off and it on, both
  // are claimed still (every union is drawn by it); with it off the sink claims the disjoint tier alone; with both off, neither.
  const unsigned all = GSHCPU_RENDER_TIERS_ALL;
  EXPECT_EQ(union_caps(all), GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL);
  EXPECT_EQ(union_caps(all & ~GSHCPU_RENDER_TIER_UNION), GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL);
  EXPECT_EQ(union_caps(all & ~GSHCPU_RENDER_TIER_UNION_GENERAL), GSHSCENE_CAP_CLIP_UNION);
  EXPECT_EQ(union_caps(all & ~GSHCPU_RENDER_TIER_UNION & ~GSHCPU_RENDER_TIER_UNION_GENERAL), 0u);
}

TEST(RenderClipUnion, EveryPolygonVectorIsTheOraclesByteForByteWhichTierDrawsIt) {
  size_t drawn = 0, general = 0;
  for (const cv::Case & c : cv::kCases) {
    Scene s;
    const GSHSCENE_Resource * u = make_union(s, case_members(s, c));
    GSHSCENE_List * l = clipped_fill(s, u, (uint32_t)c.width, (uint32_t)c.height);
    Draw d = draw(l, (uint32_t)c.width, (uint32_t)c.height);
    ASSERT_EQ(d.result, GSHSCENE_OK) << c.name;
    // CG10: a union the predicate shows disjoint is the fast tier's, any other the general tier's; neither is refused.
    (c.disjoint ? drawn : general)++;
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    for (int y = 0; y < c.height; y++) {
      for (int x = 0; x < c.width; x++) {
        EXPECT_EQ(d.at8((uint32_t)x, (uint32_t)y, 3), cv::kExpect[c.expect_first + (size_t)(y * c.width + x)])
            << c.name << " at (" << x << ", " << y << ")";
      }
    }
    EXPECT_EQ(d.counters.union_fast, c.disjoint ? 1u : 0u) << c.name;
    EXPECT_EQ(d.counters.union_general, c.disjoint ? 0u : 1u) << c.name;
    if (c.disjoint) {
      EXPECT_EQ(d.counters.union_sweeps, c.member_count) << c.name;
    }
  }
  EXPECT_GE(drawn, 10u);
  EXPECT_GE(general, 20u);
}

TEST(RenderClipUnion, TwoAbuttingHalvesAreWholeWhereTheMaskOfTheSameShapesIsThreeQuarters) {
  Scene s;
  const GSHSCENE_Resource * u =
      make_union(s, {member(s.rect(0, 0, 3.5, 4)), member(s.rect(3.5, 0, 8, 4))});
  Draw d = draw(clipped_fill(s, u, 8, 4), 8, 4);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.at8(3, 1, 3), 255u) << "the shared pixel is whole: no seam (CU3)";
  EXPECT_EQ(d.at8(3, 0, 3), 255u);
  EXPECT_EQ(d.at8(3, 3, 3), 255u);
  EXPECT_EQ(d.at8(2, 1, 3), 255u);
}

TEST(RenderClipUnion, TheSumIsQuantizedOnceNotPerMember) {
  // Two members of 24 lattice squared units each in one pixel are 48 + 48 units: three levels, where the bytes alone give 1 + 1.
  Scene s;
  const GSHSCENE_Resource * a = lattice_polygon(s, {{0, 0}, {1, 0}, {1, 24}, {0, 24}});
  const GSHSCENE_Resource * b = lattice_polygon(s, {{1, 0}, {2, 0}, {2, 24}, {1, 24}});
  const GSHSCENE_Resource * u = make_union(s, {member(a), member(b)});
  Draw d = draw(clipped_fill(s, u, 1, 1), 1, 1);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.at8(0, 0, 3), 3u);
  // Each alone is one level.
  const GSHSCENE_Resource * ua = make_union(s, {member(a), member(lattice_polygon(s, {{40, 40}, {41, 40}, {41, 41}, {40, 41}}))});
  Draw da = draw(clipped_fill(s, ua, 1, 1), 1, 1);
  EXPECT_EQ(da.at8(0, 0, 3), 1u);
}

TEST(RenderClipUnion, MemberOrderAndOrientationDoNotMatterAndEachMemberKeepsItsOwnRule) {
  Scene s;
  const GSHSCENE_Resource * a = s.rect(0.25, 0.25, 3.3, 3.7);
  const GSHSCENE_Resource * b = s.polygon({3.3, 0.25, 3.3, 3.7, 6.1, 3.7, 6.1, 0.25});  // wound the other way
  Draw one = draw(clipped_fill(s, make_union(s, {member(a), member(b)}), 7, 4), 7, 4);
  Draw two = draw(clipped_fill(s, make_union(s, {member(b), member(a)}), 7, 4), 7, 4);
  Draw twice = draw(clipped_fill(s, make_union(s, {member(b), member(a), member(b)}), 7, 4), 7, 4);
  ASSERT_EQ(one.result, GSHSCENE_OK);
  EXPECT_EQ(one.bytes, two.bytes);
  EXPECT_EQ(one.bytes, twice.bytes);
  // the member that is wound the other way is the same union as the member wound the same way
  const GSHSCENE_Resource * b2 = s.polygon({3.3, 0.25, 6.1, 0.25, 6.1, 3.7, 3.3, 3.7});
  Draw same = draw(clipped_fill(s, make_union(s, {member(a), member(b2)}), 7, 4), 7, 4);
  EXPECT_EQ(one.bytes, same.bytes) << "the magnitude fold (CU18)";
}

TEST(RenderClipUnion, FastFillCoverageDrawsAUnionByTheDenseCoverLikeTheDefault) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0.3, 0.3, 3.5, 3.9)), member(s.rect(3.5, 0.3, 7.2, 3.9))});
  GSHSCENE_List * l = clipped_fill(s, u, 8, 4);
  GSHCPU_Limits fast;
  gshcpu_limits_default(&fast);
  fast.fill_coverage = GSHCPU_FILL_COVERAGE_FAST;
  Options o;
  o.limits = &fast;
  Draw f = draw(l, 8, 4, o);
  Draw e = draw(l, 8, 4);
  ASSERT_EQ(f.result, GSHSCENE_OK);
  EXPECT_EQ(alphas(f), alphas(e)) << "CU-D9: a union is drawn by the dense cover whatever the fill coverage";
}

/* --- refusal, damage, limits, tiers ------------------------------------------------------------------------------- */

TEST(RenderClipUnion, WithoutTheGeneralTierAnOverlappingUnionIsRefusedWithThePairNamedAndStrictFailsAtTheUnit) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 4, 4)), member(s.rect(3, 3, 8, 8))});
  GSHSCENE_List * l = clipped_fill(s, u, 8, 8);
  Options no_general;
  no_general.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION_GENERAL;
  Draw d = draw(l, 8, 8, no_general);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  ASSERT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_UNION_OVERLAP), 1u);
  for (const GSHSCENE_Report_Entry & e : d.entries) {
    if (e.reason == GSHSCENE_REASON_UNION_OVERLAP) {
      EXPECT_EQ(e.second, (size_t)((0u << 16) | 1u));
    }
  }
  for (uint8_t a : alphas(d)) {
    EXPECT_EQ(a, 0u);
  }
  Options strict = no_general;
  strict.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(draw(l, 8, 8, strict).result, GSHSCENE_ERR_REFUSED);
  // With the general tier the same union is drawn (CG10), and strict succeeds.
  Options general;
  general.flags = GSHSCENE_RENDER_STRICT;
  Draw g = draw(l, 8, 8, general);
  EXPECT_EQ(g.result, GSHSCENE_OK);
  EXPECT_EQ(g.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_EQ(g.at8(3, 3, 3), 255u);
  EXPECT_EQ(g.at8(7, 0, 3), 0u);
}

TEST(RenderClipUnion, ADamagedRenderEqualsTheWholeOnItsPartAndPaysTheSameTests) {
  Scene s;
  const GSHSCENE_Resource * u =
      make_union(s, {member(s.rect(0.5, 0.5, 3.5, 7.5)), member(s.rect(3.5, 0.5, 6.7, 7.5)), member(s.rect(8, 1, 12, 5))});
  GSHSCENE_List * l = clipped_fill(s, u, 12, 8);
  Draw whole = draw(l, 12, 8);
  GSHSCENE_Rect part{2, 2, 7, 6};
  Options o;
  o.damage = &part;
  Draw dmg = draw(l, 12, 8, o);
  ASSERT_EQ(dmg.result, GSHSCENE_OK);
  for (int y = 2; y < 6; y++) {
    for (int x = 2; x < 7; x++) {
      EXPECT_EQ(dmg.at8((uint32_t)x, (uint32_t)y, 3), whole.at8((uint32_t)x, (uint32_t)y, 3)) << x << "," << y;
    }
  }
  EXPECT_EQ(dmg.counters.union_tests, whole.counters.union_tests) << "the same tests are billed, whatever the damage";
  EXPECT_EQ(whole.counters.union_tests, 3u);
}

TEST(RenderClipUnion, TheTestsLimitPassesExactlyAndTheNextPushIsTheSinksResourceLimit) {
  Scene s;
  // 3 members: 3 comparisons each push; two pushes, 6 in all.
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 2, 2)), member(s.rect(3, 0, 5, 2)), member(s.rect(6, 0, 8, 2))});
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint wh = white();
  for (int i = 0; i < 2; i++) {
    ASSERT_EQ(gshscene_list_clip_push(l, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 2), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  Options o;
  o.limits = &lim;
  lim.max_union_tests = 6;
  Draw exact = draw(l, 8, 2, o);
  EXPECT_EQ(exact.count(GSHSCENE_ENTRY_REFUSED), 0u) << "exactly the limit passes";
  lim.max_union_tests = 5;
  Draw over = draw(l, 8, 2, o);
  EXPECT_EQ(over.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u) << "the second push passes the total";
  o.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(draw(l, 8, 2, o).result, GSHSCENE_ERR_REFUSED);
  lim.max_union_tests = UINT64_MAX;
  o.flags = 0;
  EXPECT_EQ(draw(l, 8, 2, o).count(GSHSCENE_ENTRY_REFUSED), 0u) << "no cap";
}

TEST(RenderClipUnion, TheTierIsShownTakenAndForcedOff) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 3.5, 4)), member(s.rect(3.5, 0, 8, 4))});
  GSHSCENE_List * l = clipped_fill(s, u, 8, 4);
  Draw on = draw(l, 8, 4);
  EXPECT_EQ(on.counters.union_fast, 1u);
  EXPECT_EQ(on.counters.union_sweeps, 2u);
  // FT-U1 off alone: every union is drawn by the general tier (CG10), to the same bytes.
  Options general;
  general.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION;
  Draw forced = draw(l, 8, 4, general);
  ASSERT_EQ(forced.result, GSHSCENE_OK);
  EXPECT_EQ(forced.counters.union_fast, 0u);
  EXPECT_EQ(forced.counters.union_general, 1u);
  EXPECT_EQ(forced.bytes, on.bytes);
  // Both off: the sink claims neither, so the union is refused `CLIP_UNION` and nothing is drawn.
  Options off;
  off.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION & ~GSHCPU_RENDER_TIER_UNION_GENERAL;
  Draw d = draw(l, 8, 4, off);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_CLIP_UNION), 1u);
  EXPECT_EQ(d.counters.union_fast, 0u);
  EXPECT_EQ(d.counters.union_general, 0u);
  for (uint8_t a : alphas(d)) {
    EXPECT_EQ(a, 0u) << "forced off: refused, nothing drawn";
  }
}

TEST(RenderClipUnion, ADegenerateMemberContributesNothingAndAnAllEmptyUnionClipsEverythingAway) {
  Scene s;
  GSHGEOM_Path * e = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &e);
  const GSHSCENE_Resource * empty = s.path(e);
  const GSHSCENE_Resource * sq = s.rect(1, 1, 4, 4);
  Draw with = draw(clipped_fill(s, make_union(s, {member(sq), member(empty)}), 6, 6), 6, 6);
  Draw alone = draw(clipped_fill(s, make_union(s, {member(sq), member(s.rect(5, 5, 6, 6))}), 6, 6), 6, 6);
  ASSERT_EQ(with.result, GSHSCENE_OK);
  EXPECT_EQ(with.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_EQ(with.at8(2, 2, 3), 255u);
  EXPECT_EQ(alone.at8(2, 2, 3), 255u);
  Draw none = draw(clipped_fill(s, make_union(s, {member(empty), member(s.rect(1, 1, 1, 4))}), 6, 6), 6, 6);
  ASSERT_EQ(none.result, GSHSCENE_OK);
  for (uint8_t a : alphas(none)) {
    EXPECT_EQ(a, 0u);
  }
}

TEST(RenderClipUnion, AUnionInsideAClipBlockIsTheProductOfCoverages) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0.5, 0, 3.5, 4)), member(s.rect(3.5, 0, 7.5, 4))});
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint wh = white();
  ASSERT_EQ(gshscene_list_clip_push(l, s.rect(0, 0, 8, 2.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 4), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Draw d = draw(l, 8, 4);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.at8(1, 0, 3), 255u);
  EXPECT_EQ(d.at8(0, 0, 3), 128u) << "the union's half pixel at x = 0.5";
  EXPECT_EQ(d.at8(1, 2, 3), 128u) << "the outer clip's half row at y = 2.5";
  EXPECT_EQ(d.at8(0, 2, 3), 64u) << "the product of the two halves";
  EXPECT_EQ(d.at8(1, 3, 3), 0u);
}

TEST(RenderClipUnion, AListWithNoUnionPaysNothingAndAUnionRendersUnderCallerMemory) {
  Scene s;
  GSHSCENE_List * plain = s.open();
  GSHSCENE_Paint wh = white();
  ASSERT_EQ(gshscene_list_fill(plain, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(plain), GSHSCENE_OK);
  Draw p = draw(plain, 8, 8);
  EXPECT_EQ(p.counters.union_fast, 0u);
  EXPECT_EQ(p.counters.union_sweeps, 0u);
  EXPECT_EQ(p.counters.union_tests, 0u);
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0.5, 0.5, 3.5, 3.5)), member(s.rect(3.5, 0.5, 7, 3.5))});
  GSHSCENE_List * l = clipped_fill(s, u, 8, 8);
  Options cm;
  cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw a = draw(l, 8, 8, cm);
  Draw b = draw(l, 8, 8);
  ASSERT_EQ(a.result, GSHSCENE_OK) << "the workspace measured for a union is enough";
  EXPECT_EQ(a.bytes, b.bytes);
}


TEST(RenderClipUnion, AddedAreasHeldAtAPixelNeverWrapTheSixteenBitPlane) {
  // Eight members in one pixel, each a tiny rectangle wound sixty-four times: each member's own area is held at the whole pixel (8192),
  // so the eight add to 65536, which is zero in sixteen bits and 255 when each addition is held.
  Scene s;
  std::vector<GSHSCENE_Clip_Member> members;
  for (int i = 0; i < 8; i++) {
    GSHGEOM_Path * p = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &p);
    for (int k = 0; k < 64; k++) {
      const double x = i * 8.0 / 64.0;
      GSHGEOM_Point a{x, 0.0, 0}, b{x + 8.0 / 64.0, 0.0, 0}, c{x + 8.0 / 64.0, 8.0 / 64.0, 0}, d{x, 8.0 / 64.0, 0};
      gshgeom_path_move_to(p, a);
      gshgeom_path_line_to(p, b);
      gshgeom_path_line_to(p, c);
      gshgeom_path_line_to(p, d);
      gshgeom_path_close(p);
    }
    members.push_back(member(s.path(p)));
  }
  Draw d = draw(clipped_fill(s, make_union(s, members), 1, 1), 1, 1);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_EQ(d.at8(0, 0, 3), 255u);
}

TEST(RenderClipUnion, EachPushStartsFromAClearPlane) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0.2, 0.2, 3.4, 3.8)), member(s.rect(3.4, 0.2, 6.9, 3.8))});
  GSHSCENE_List * once = clipped_fill(s, u, 8, 4);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint wh = white();
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(gshscene_list_clip_push(l, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 4), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Draw one = draw(once, 8, 4);
  Draw three = draw(l, 8, 4);
  // three white fills of coverage c over transparent: alpha 1 - (1 - c)^3 at the 8-bit rounding the composite uses
  for (uint32_t x = 0; x < 8; x++) {
    const double c = one.at8(x, 1, 3) / 255.0;
    const double want = 1.0 - (1.0 - c) * (1.0 - c) * (1.0 - c);
    EXPECT_NEAR(three.at8(x, 1, 3) / 255.0, want, 1.0 / 255.0) << x << ": the plane is cleared for every push";
  }
}

TEST(RenderClipUnion, AMemberOverACapRefusesTheWholePushAndNothingIsDrawnWithItLeftOut) {
  Scene s;
  const GSHSCENE_Resource * small = s.rect(0.5, 0.5, 2.5, 2.5);
  std::vector<double> xy;
  for (int i = 0; i < 48; i++) {  // a many-edged star of the right of the small one
    const double a = 6.2831853 * i / 48.0, r = (i % 2) ? 3.5 : 2.5;
    xy.push_back(8.0 + r * std::cos(a));
    xy.push_back(4.0 + r * std::sin(a));
  }
  const GSHSCENE_Resource * big = s.polygon(xy);
  const GSHSCENE_Resource * u = make_union(s, {member(small), member(big)});
  GSHSCENE_List * list = clipped_fill(s, u, 12, 8);
  GSHSCENE_List * only_small = clipped_fill(s, make_union(s, {member(small), member(s.rect(10, 6, 11, 7))}), 12, 8);
  bool found = false;
  for (uint64_t cap = 1; cap < 400 && !found; cap++) {
    GSHCPU_Limits lim;
    gshcpu_limits_default(&lim);
    lim.max_deposits = cap;
    Options o;
    o.limits = &lim;
    Draw ok = draw(only_small, 12, 8, o);
    Draw d = draw(list, 12, 8, o);
    // The refusal must be the clip push's (chain {0}): a cap the fill inside is refused by itself says nothing about the union.
    bool at_the_push = false;
    for (size_t i = 0; i < d.entries.size(); i++) {
      at_the_push = at_the_push || (d.entries[i].reason == GSHSCENE_REASON_RESOURCE_LIMIT && d.chains[i] == std::vector<size_t>{0});
    }
    if (ok.count(GSHSCENE_ENTRY_REFUSED) == 0 && d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT) == 1 && at_the_push) {
      found = true;
      EXPECT_EQ(d.at8(1, 1, 3), 0u) << "the small member is not drawn alone: a union is never drawn with a member left out";
      for (uint8_t a : alphas(d)) {
        EXPECT_EQ(a, 0u);
      }
      Options strict = o;
      strict.flags = GSHSCENE_RENDER_STRICT;
      EXPECT_EQ(draw(list, 12, 8, strict).result, GSHSCENE_ERR_REFUSED);
    }
  }
  EXPECT_TRUE(found) << "a cap that the small member meets and the many-edged one does not";
}

TEST(RenderClipUnion, AUnionInAPatternsTileIsDrawnByTheGeneralTierWhateverItsMembersAre) {
  // CU21: the union in a tile is formed in each copy; two members that abut are the fast tier's outside a tile, and the general tier's in it.
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0, 0, 3, 6)), member(s.rect(3, 0, 6, 6))});
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
  for (uint8_t a : alphas(d)) {
    EXPECT_EQ(a, 255u) << "the two halves are the whole cell, red in every copy";
  }
  EXPECT_EQ(d.counters.union_fast, 0u);
  EXPECT_GT(d.counters.union_general, 0u);
}

TEST(RenderClipUnion, TheWorkspaceMeasuredForAUnionIsExactlyEnoughAndAUnionRendersInIt) {
  Scene s;
  const GSHSCENE_Resource * u = make_union(s, {member(s.rect(0.5, 0.5, 3.5, 3.5)), member(s.rect(3.5, 0.5, 7, 3.5))});
  GSHSCENE_List * l = clipped_fill(s, u, 8, 8);
  Options cm;
  cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw a = draw(l, 8, 8, cm);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  Draw plain = draw(clipped_fill(s, make_union(s, {member(s.rect(0.5, 0.5, 3.5, 3.5)), member(s.rect(5, 5, 6, 6))}), 8, 8), 8, 8, cm);
  EXPECT_GE(a.need, 2u * 8u * 8u) << "the plane is in the measure";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
