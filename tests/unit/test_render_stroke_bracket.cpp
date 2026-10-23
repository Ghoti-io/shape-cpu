/**
 * @file
 *
 * The coverage-level bracket of a stroke against the geometric oracle (strokes story S4, SC5, F4) and the union gate
 * (strokes story S4b, UC12): every join, cap and width space, dashes, degenerate subpaths and hairlines, both alignments,
 * and the lens under rotation with every join.  The allowance for overlapping pieces is one level (UC12: the sink's
 * coverage is the area of the union of the pieces, so what is left is rounding) and the gate fails past it.  The union
 * gate holds every pixel of every case within one and a half levels of the area an independent instrument finds for the
 * union of the outline's pieces.  Both are seen to fail: on the build with the exact step off, which is the stopgap of F4
 * (it passes only at 64/255 and fails the union gate on the dashes), and on the instrument planted with the sum of the
 * pieces' areas, which fails its own self-check against closed forms.  A failing case prints a record of its worst pixels
 * and the pieces of the outline that cover them.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

using namespace sh;
using stroke_gate::Ops;
using stroke_gate::Params;

namespace {

const Ops kClosed = {{'M', {3.3, 4.2}}, {'L', {19.6, 6.1}}, {'L', {9.2, 18.4}}, {'Q', {3.0, 14.0, 4.4, 9.0}}, {'Z', {}}};
const Ops kOpen = {{'M', {3.0, 3.0}}, {'L', {18.0, 4.0}}, {'L', {6.0, 12.0}}, {'Q', {14.0, 14.0, 19.0, 19.0}}};
const Ops kLens = {{'M', {12.0, 4.0}}, {'C', {20.0, 4.0, 20.0, 16.0, 12.0, 16.0}}, {'C', {4.0, 16.0, 4.0, 4.0, 12.0, 4.0}}, {'Z', {}}};
const Ops kStar = {{'M', {12.0, 2.5}}, {'L', {16.0, 19.0}}, {'L', {3.0, 8.0}}, {'L', {21.0, 8.0}}, {'L', {8.0, 19.0}}, {'Z', {}}};
const Ops kMixed = {{'M', {3.0, 3.0}}, {'L', {14.0, 3.5}}, {'L', {13.0, 13.0}}, {'L', {4.0, 12.5}}, {'Z', {}},
    {'M', {16.0, 6.0}}, {'L', {23.0, 19.0}}, {'L', {17.0, 22.0}}};
const Ops kDot = {{'M', {10.0, 10.0}}, {'L', {10.0, 10.0}}, {'M', {15.0, 15.0}}, {'Z', {}}};
// a strip 0.6 wide with its edge at x = 4.3: stroked at width 0.2 its aligned band lies on the fill's edge, off a pixel's centre
const Ops kStrip = {{'M', {4.3, 3.0}}, {'L', {4.9, 3.0}}, {'L', {4.9, 15.0}}, {'L', {4.3, 15.0}}, {'Z', {}}};

Params params(double width = 3.0, uint32_t join = GSHGEOM_STROKE_JOIN_ROUND, uint32_t cap = GSHGEOM_STROKE_CAP_BUTT) {
  Params p;
  p.s.width = width;
  p.s.join = join;
  p.s.cap = cap;
  return p;
}

/**
 * True under Valgrind, whose x87 is a double: the union gate's stretches are summed in `long double` (stroke_gate.h) and
 * its bracket separates pixels that differ by less than a double's rounding in a few cases, so under Valgrind the gate
 * reports violations the same render does not have natively (the picture it prints shows the library's stroke whole and the
 * gate's expectation wrong).  The library holds no `long double`; the instrument does.
 */
bool under_valgrind() {
  const char * preload = std::getenv("LD_PRELOAD");
  return std::getenv("VALGRIND_LAUNCHER") != nullptr || (preload && std::strstr(preload, "vgpreload"));
}

GSHGEOM_Map frac_map() {
  return translation(0.375, 0.625);
}

}  // namespace

/* --- SC5: the coverage bracket against the geometric oracle --------------------------------------------------------- */

namespace {

struct GateCase {
  std::string name;
  const Ops * ops;
  Params p;
  GSHGEOM_Map map;
  int alignment = 0;
  bool even_odd = false;
};


/** The pieces of the outline (by part) whose polygon holds samples of pixel (px, py), for a hand-over to S2's reviser. */
std::string pieces_at(GateCase & c, int px, int py) {
  GSHGEOM_Path * path = stroke_gate::build(*c.ops);
  GSHGEOM_Map canon;
  int64_t sx = 0, sy = 0;
  gshgeom_stroke_canonical_map(&c.map, &canon, &sx, &sy);
  GSHGEOM_Stroke_Outline * o = nullptr;
  std::string out;
  if (gshgeom_stroke_expand(path, &c.p.get(), &canon, nullptr, nullptr, &o) == GSHGEOM_OK) {
    const char * names[3] = {"body", "aligned", "clip"};
    for (int part = 0; part < 2; part++) {
      const GSHGEOM_Flat_Path * fp = part == 0 ? gshgeom_stroke_outline_body(o) : gshgeom_stroke_outline_aligned(o);
      if (!fp) continue;
      const GSHGEOM_Device_Point * pts = gshgeom_flat_path_points(fp);
      for (size_t sub = 0; sub < gshgeom_flat_path_subpath_count(fp); sub++) {
        GSHGEOM_Flat_Subpath fs;
        gshgeom_flat_path_subpath(fp, sub, &fs);
        int hits = 0;
        for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
          double qx = px - (double)sx + (i + 0.5) / 8, qy = py - (double)sy + (j + 0.5) / 8;
          int w = 0;
          for (size_t k = 0; k < fs.count; k++) {
            double ax = pts[fs.first + k].x / 64.0, ay = pts[fs.first + k].y / 64.0;
            double bx = pts[fs.first + (k + 1) % fs.count].x / 64.0, by = pts[fs.first + (k + 1) % fs.count].y / 64.0;
            if ((ay <= qy) != (by <= qy) && ax + (qy - ay) / (by - ay) * (bx - ax) > qx) w += by > ay ? 1 : -1;
          }
          hits += w != 0;
        }
        if (hits) {
          char b[120];
          std::snprintf(b, sizeof b, " [%s#%zu %zupts %d/64]", names[part], sub, fs.count, hits);
          out += b;
        }
      }
    }
    gshgeom_stroke_outline_destroy(o);
  }
  gshgeom_path_destroy(path);
  return out;
}

/** One line a case, and its three worst pixels with the pieces that cover them. */
void record(GateCase & c, const stroke_gate::Result & r, const std::vector<uint8_t> & alpha, int size) {
  std::vector<std::pair<double, int>> top;
  for (int i = 0; i < size * size; i++) {
    if (r.upper[i] < 0) continue;
    top.push_back({(alpha[i] / 255.0 - r.upper[i] - 2.0 / 255.0) * 255.0, i});
  }
  std::sort(top.rbegin(), top.rend());
  std::printf("REC case|%s|worst %.1f\n", c.name.c_str(), r.worst_over);
  for (int k = 0; k < 3 && k < (int)top.size() && top[k].first > 8.0; k++) {
    int x = top[k].second % size, y = top[k].second / size;
    std::printf("REC   pixel (%d,%d) k %.4f upper %.4f over %.1f%s\n", x, y, alpha[top[k].second] / 255.0,
        r.upper[top[k].second], top[k].first, pieces_at(c, x, y).c_str());
  }
}

/** What one case found: the bracket's and the union gate's. */
struct Found {
  stroke_gate::Result bracket;
  stroke_gate::UnionResult union_gate;
};

Found gate_one(GateCase & c, int size = 26, double overlap = stroke_gate::kOverlap, bool quiet = false,
    unsigned tiers = GSHCPU_RENDER_TIERS_ALL, bool aligned_too = false) {
  c.p.s.alignment = (uint32_t)c.alignment;
  c.p.s.alignment_rule = c.even_odd ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
  Store s;
  Sink sink(tiers);
  Drawn d = render_list(sink.sink, one_stroke(s, *c.ops, c.p.get(), c.map), (uint32_t)size, (uint32_t)size);
  EXPECT_EQ(d.result, GSHSCENE_OK) << c.name;
  EXPECT_TRUE(d.entries.empty()) << c.name << ": nothing is refused";
  GSHGEOM_Path * path = stroke_gate::build(*c.ops);
  Found f;
  f.bracket = stroke_gate::bracket(path, c.p.get(), c.map, c.alignment, c.even_odd, size, size, d.alpha, overlap);
  f.union_gate = stroke_gate::union_gate(path, c.p.get(), c.map, size, size, d.alpha, aligned_too, 1024);
  gshgeom_path_destroy(path);
  const stroke_gate::Result & r = f.bracket;
  if (r.violations && !quiet) {
    record(c, r, d.alpha, size);
  }
  static int shown = 0;
  if (r.violations && !quiet) std::printf("case %-40s over %6.1f under %5.1f violations %ld\n", c.name.c_str(), r.worst_over, r.worst_under, r.violations);
  if (r.violations && !quiet && shown++ < 6) {
    std::printf("--- %s: %ld violations, worst over %.1f/255\n%s", c.name.c_str(), r.violations, r.worst_over,
        stroke_gate::picture(r, size, size, d.alpha).c_str());
  }
  return f;
}

/** The worst of each class over the cases that ran (the table the story's change log quotes). */
struct Worst {
  double over = 0;
  double under = 0;
  double unioned = 0;
  long cases = 0;
  void add(const Found & f) {
    over = std::max(over, f.bracket.worst_over);
    under = std::max(under, f.bracket.worst_under);
    unioned = std::max(unioned, f.union_gate.worst);
    cases++;
  }
};

void print_class(const char * name, const Worst & w) {
  std::printf("stroke class %-34s %3ld cases: worst overshoot past the bracket %5.2f/255, undershoot %5.2f/255, off the union %4.2f levels\n", name,
      w.cases, w.over, w.under, w.unioned);
}

}  // namespace

TEST(RenderStrokeBracket, EveryJoinCapAndWidthSpaceStaysInsideTheBracket) {
  if (under_valgrind()) {
    GTEST_SKIP() << "the gate sums in long double, which Valgrind computes as double";
  }
  std::vector<GateCase> cases;
  const GSHGEOM_Map maps[3] = {frac_map(), affine_map(0.9, -0.4, 0.4, 0.9, 1.3125, 0.6875), affine_map(1.1, 0.0, 0.0, 0.55, 0.25, 2.5)};
  const double widths[3] = {1.0, 3.0, 5.5};
  for (uint32_t space = 0; space < 2; space++) {
    for (uint32_t join = 0; join < GSHGEOM_STROKE_JOIN_COUNT; join++) {
      for (uint32_t cap = 0; cap < GSHGEOM_STROKE_CAP_COUNT; cap++) {
        for (int m = 0; m < 3; m++) {
          if ((join + cap + m + space) % 3 != 0) {
            continue; // a third of the product, rotating: every value of every field is met, the run stays short
          }
          GateCase c;
          c.name = "open join " + std::to_string(join) + " cap " + std::to_string(cap) + " space " + std::to_string(space) +
              " map " + std::to_string(m);
          c.ops = &kOpen;
          c.p = params(widths[(join + cap) % 3], join, cap);
          c.p.s.width_space = space;
          c.p.s.miter_limit = (join + m) % 2 ? 2.0 : 4.0;
          c.map = maps[m];
          cases.push_back(c);
        }
      }
      GateCase c;
      c.name = "closed join " + std::to_string(join) + " space " + std::to_string(space);
      c.ops = join % 2 ? &kClosed : &kLens;
      c.p = params(3.0, join);
      c.p.s.width_space = space;
      c.map = maps[join % 3];
      cases.push_back(c);
    }
  }
  stroke_gate::Result total;
  Worst joins;
  for (GateCase & c : cases) {
    Found f = gate_one(c);
    EXPECT_EQ(f.bracket.violations, 0) << c.name << ": " << f.bracket.first;
    EXPECT_EQ(f.union_gate.violations, 0) << c.name << ": " << f.union_gate.first;
    total.pixels += f.bracket.pixels;
    total.decided += f.bracket.decided;
    total.worst_over = std::max(total.worst_over, f.bracket.worst_over);
    total.worst_under = std::max(total.worst_under, f.bracket.worst_under);
    joins.add(f);
  }
  std::printf("stroke bracket (centered): %zu cases, %ld pixels, %ld decided; worst overshoot past the union %.2f/255 (bound %.2f/255), worst undershoot %.2f/255\n",
      cases.size(), total.pixels, total.decided, total.worst_over, stroke_gate::kOverlap * 255.0, total.worst_under);
  print_class("joins and caps, centered", joins);
  EXPECT_GT(total.decided, 1000);
  EXPECT_LE(total.worst_over, stroke_gate::kOverlap * 255.0);
}

TEST(RenderStrokeBracket, DashesDegenerateSubpathsAndHairlinesStayInsideTheBracket) {
  std::vector<GateCase> cases;
  for (int scope = 0; scope < 2; scope++) {
    for (uint32_t cap = 0; cap < 3; cap++) {
      GateCase c;
      c.name = "dashes scope " + std::to_string(scope) + " cap " + std::to_string(cap);
      c.ops = scope ? &kMixed : &kOpen;
      c.p = params(2.5, GSHGEOM_STROKE_JOIN_MITER, cap);
      c.p.dashes = {4.0, 2.0, 1.0};
      c.p.s.dash_scope = (uint32_t)scope;
      c.p.s.dash_offset = 1.5;
      c.map = frac_map();
      cases.push_back(c);
    }
  }
  for (uint32_t cap = 1; cap < 3; cap++) {
    for (uint32_t omit = 0; omit < 2; omit++) {
      GateCase c;
      c.name = "degenerate cap " + std::to_string(cap) + " omit " + std::to_string(omit);
      c.ops = &kDot;
      c.p = params(5.0, GSHGEOM_STROKE_JOIN_ROUND, cap);
      c.p.s.degenerate = omit;
      c.map = affine_map(1.0, 0.5, 0.0, 1.0, 0.25, 0.75);
      cases.push_back(c);
    }
  }
  for (int shape = 0; shape < 2; shape++) {
    GateCase c;
    c.name = std::string("hairline ") + (shape ? "curve" : "polyline");
    c.ops = shape ? &kLens : &kOpen;
    c.p = params(1.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
    c.p.s.width_space = GSHGEOM_STROKE_WIDTH_DEVICE;
    c.map = affine_map(1.2, 0.0, 0.3, 1.0, 0.125, 0.5);
    cases.push_back(c);
  }
  Worst dashes;
  Worst square;
  Worst others;
  for (GateCase & c : cases) {
    Found f = gate_one(c);
    EXPECT_EQ(f.bracket.violations, 0) << c.name << ": " << f.bracket.first;
    EXPECT_EQ(f.union_gate.violations, 0) << c.name << ": " << f.union_gate.first;
    EXPECT_LE(f.bracket.worst_over, stroke_gate::kOverlap * 255.0) << c.name;
    (c.name.find("dashes") == 0 ? (c.name.back() == '2' ? square : dashes) : others).add(f);
  }
  print_class("dashes, butt and round caps", dashes);
  print_class("dashes, square caps", square);
  print_class("degenerate subpaths and hairlines", others);
}

TEST(RenderStrokeBracket, InsideAndOutsideAlignmentStaysInsideTheBracketAndReportsWhatItExcluded) {
  std::vector<GateCase> cases;
  const Ops * shapes[4] = {&kClosed, &kLens, &kStar, &kMixed};
  for (int shape = 0; shape < 4; shape++) {
    for (int alignment = 1; alignment < 3; alignment++) {
      for (int rule = 0; rule < 2; rule++) {
        if (shape != 2 && rule) {
          continue; // the star is the shape whose rules differ
        }
        GateCase c;
        c.name = "shape " + std::to_string(shape) + (alignment == 1 ? " inside" : " outside") + (rule ? " even-odd" : " nonzero");
        c.ops = shapes[shape];
        c.p = params(shape % 2 ? 2.0 : 3.0, shape == 2 ? GSHGEOM_STROKE_JOIN_MITER : GSHGEOM_STROKE_JOIN_ROUND);
        c.map = shape == 3 ? affine_map(0.95, 0.2, -0.2, 0.95, 0.375, 0.5) : frac_map();
        c.alignment = alignment;
        c.even_odd = rule;
        cases.push_back(c);
      }
    }
  }
  // thin ones: the band is under two pixels wide, so the aligned part and the fill's edge lie in every pixel along the path
  for (int shape = 0; shape < 5; shape++) {
    for (int alignment = 1; alignment < 3; alignment++) {
      GateCase c;
      const Ops * thin[5] = {&kClosed, &kLens, &kStar, &kMixed, &kStrip};
      c.name = "thin shape " + std::to_string(shape) + (alignment == 1 ? " inside" : " outside");
      c.ops = thin[shape];
      c.p = params(shape == 4 ? 0.2 : (shape % 2 ? 0.7 : 0.9), shape == 2 ? GSHGEOM_STROKE_JOIN_MITER : GSHGEOM_STROKE_JOIN_ROUND);
      c.map = shape == 3 ? affine_map(0.95, 0.2, -0.2, 0.95, 0.375, 0.5) : frac_map();
      c.alignment = alignment;
      c.even_odd = shape == 2 && alignment == 2;
      cases.push_back(c);
    }
  }
  long excluded = 0;
  long pixels = 0;
  double over = 0;
  Worst aligned;
  for (GateCase & c : cases) {
    Found f = gate_one(c, 26, stroke_gate::kOverlap, false, GSHCPU_RENDER_TIERS_ALL, true);
    EXPECT_EQ(f.bracket.violations, 0) << c.name << ": " << f.bracket.first;
    EXPECT_EQ(f.union_gate.violations, 0) << c.name << ": " << f.union_gate.first;
    EXPECT_GT(f.union_gate.pixels, 0) << c.name << ": the union gate judged the aligned part too";
    excluded += f.bracket.excluded;
    pixels += f.bracket.pixels;
    over = std::max(over, f.bracket.worst_over);
    aligned.add(f);
  }
  std::printf("stroke bracket (aligned): %zu cases, %ld pixels, %ld excluded around crossings of the aligned part and the fill; worst overshoot %.2f/255\n",
      cases.size(), pixels, excluded, over);
  print_class("aligned strokes", aligned);
  EXPECT_LE(over, stroke_gate::kOverlap * 255.0);
}

TEST(RenderStrokeBracket, TheLensUnderRotationWithEveryJoin) {
  if (under_valgrind()) {
    GTEST_SKIP() << "the gate sums in long double, which Valgrind computes as double";
  }
  const double angles[3][4] = {{1, 0, 0, 1}, {0.9205, -0.3907, 0.3907, 0.9205}, {0.3907, -0.9205, 0.9205, 0.3907}};
  Worst lens;
  for (int a = 0; a < 3; a++) for (uint32_t space = 0; space < 2; space++) for (uint32_t join = 0; join < 5; join++) {
    GateCase c;
    c.name = "lens rot " + std::to_string(a) + " space " + std::to_string(space) + " join " + std::to_string(join);
    c.ops = &kLens;
    c.p = params(3.0, join);
    c.p.s.width_space = space;
    c.map = affine_map(angles[a][0], angles[a][1], angles[a][2], angles[a][3], 3.375 + a, 2.625);
    Found f = gate_one(c);
    EXPECT_EQ(f.bracket.violations, 0) << c.name << ": " << f.bracket.first;
    EXPECT_EQ(f.union_gate.violations, 0) << c.name << ": " << f.union_gate.first;
    lens.add(f);
  }
  print_class("adjacent curve triangles (the lens)", lens);
}

TEST(RenderStrokeBracket, TheGatesAreSeenToFailOnTheBuildWithoutTheExactStep) {
  // The control: the worst case measured before the exact step (dashes with square caps over two subpaths, 61.8/255 past the
  // bracket) is the stopgap build, which has the exact step off.  It passes the stopgap allowance, 64/255, fails the
  // allowance of one level and fails the union gate; the sink with the step on passes all three.
  GateCase c;
  c.name = "control: dashes scope 1 cap 2";
  c.ops = &kMixed;
  c.p = params(2.5, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE);
  c.p.dashes = {4.0, 2.0, 1.0};
  c.p.s.dash_scope = 1;
  c.p.s.dash_offset = 1.5;
  c.map = frac_map();
  const unsigned off = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_EXACT;
  Found stop = gate_one(c, 26, stroke_gate::kOverlapStopgap, true, off);
  EXPECT_EQ(stop.bracket.violations, 0);
  EXPECT_GT(stop.bracket.worst_over, 40.0) << "the case is a worst case: it has real overlap";
  EXPECT_LE(stop.bracket.worst_over, stroke_gate::kOverlapStopgap * 255.0);
  Found tight = gate_one(c, 26, stroke_gate::kOverlap, true, off);
  EXPECT_GT(tight.bracket.violations, 0) << "the gate fails past the allowance of one level on the stopgap build";
  EXPECT_GT(tight.union_gate.violations, 0) << "and the union gate fails it";
  Found exact = gate_one(c, 26, stroke_gate::kOverlap, true);
  EXPECT_EQ(exact.bracket.violations, 0);
  EXPECT_EQ(exact.union_gate.violations, 0);
  EXPECT_LE(exact.bracket.worst_over, stroke_gate::kOverlap * 255.0);
  // The allowance first decided, 16/255, is failed by the stopgap build too.
  Found first = gate_one(c, 26, stroke_gate::kOverlapFirst, true, off);
  EXPECT_GT(first.bracket.violations, 0);
  std::printf("stroke bracket control: the stopgap build is %.1f/255 past the bracket (passes %.1f, fails %.1f on %ld pixels and the union gate by %.1f levels); the exact step: %.2f/255 and %.2f levels\n",
      stop.bracket.worst_over, stroke_gate::kOverlapStopgap * 255.0, stroke_gate::kOverlap * 255.0, tight.bracket.violations,
      tight.union_gate.worst, exact.bracket.worst_over, exact.union_gate.worst);
}

TEST(RenderStrokeBracket, TheUnionGateIsSeenToFailOnThinAlignedStrokesWithoutTheJointStep) {
  // The control of UC8: with the exact step on but each part covered alone and SC5's formula composing them (which treats
  // the three coverages as independent), a thin aligned stroke is wrong where the aligned part and the fill's edge share
  // pixels, by up to 20 levels on an off-centre strip; covered together it is within the gate.
  Worst without;
  Worst with;
  for (int alignment = 1; alignment < 3; alignment++) {
    for (int shape = 0; shape < 3; shape++) {
      GateCase c;
      const Ops * thin[3] = {&kStrip, &kLens, &kClosed};
      c.name = "control: thin shape " + std::to_string(shape) + (alignment == 1 ? " inside" : " outside");
      c.ops = thin[shape];
      c.p = params(shape == 0 ? 0.2 : 0.7, GSHGEOM_STROKE_JOIN_ROUND);
      c.map = frac_map();
      c.alignment = alignment;
      Found off = gate_one(c, 26, stroke_gate::kOverlap, true, GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_JOINT, true);
      Found on = gate_one(c, 26, stroke_gate::kOverlap, true, GSHCPU_RENDER_TIERS_ALL, true);
      EXPECT_EQ(on.union_gate.violations, 0) << c.name << ": " << on.union_gate.first;
      without.add(off);
      with.add(on);
    }
  }
  EXPECT_GT(without.unioned, stroke_gate::kUnionBound) << "SC5's formula in a shared pixel fails the union gate";
  print_class("thin aligned, parts covered alone", without);
  print_class("thin aligned, parts covered together", with);
}

namespace {

/** A closed contour of the 26.6 points @p xy (pixels times 64), positively wound as the expansion's pieces are. */
stroke_gate::Contour rect_contour(double x0, double y0, double x1, double y1) {
  auto pt = [](double x, double y) {
    stroke_gate::Pt p;
    p.x = (int64_t)std::llround(x * 64);
    p.y = (int64_t)std::llround(y * 64);
    return p;
  };
  return {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)};
}

}  // namespace

TEST(RenderStrokeBracket, TheUnionInstrumentIsRightOnClosedFormsAndTheSumOfPiecesIsSeenToFail) {
  // Two squares of side 2 overlapping in a square of side 1: A = [0.5, 2.5]^2, B = [1.5, 3.5]^2.  In pixel (1, 1) A covers it
  // all and B the quarter [1.5, 2]^2: the union is 1, the sum 1.25; pixel (2, 2) is B's whole against A's quarter: 1 and
  // 1.25; pixel (1, 2) is A's [1,2]x[2,2.5] (a half) and B's [1.5,2]x[2,3] (a half) overlapping in a quarter: 0.75 and 1.
  stroke_gate::Part body;
  body.contours.push_back(stroke_gate::cut_contour(rect_contour(0.5, 0.5, 2.5, 2.5)));
  body.contours.push_back(stroke_gate::cut_contour(rect_contour(1.5, 1.5, 3.5, 3.5)));
  std::vector<double> u = stroke_gate::union_area(body, nullptr, nullptr, 5, 5, 512);
  auto at = [&](const std::vector<double> & v, int x, int y) { return v[(size_t)y * 5 + (size_t)x]; };
  EXPECT_NEAR(at(u, 1, 1), 1.0, 1e-3);
  EXPECT_NEAR(at(u, 2, 2), 1.0, 1e-3);
  EXPECT_NEAR(at(u, 1, 2), 0.75, 1e-3);
  EXPECT_NEAR(at(u, 2, 1), 0.75, 1e-3);
  EXPECT_NEAR(at(u, 0, 0), 0.25, 1e-3);
  EXPECT_NEAR(at(u, 3, 3), 0.25, 1e-3);
  EXPECT_NEAR(at(u, 0, 3), 0.0, 1e-3);
  // The plant: the instrument that adds each piece's own area instead of taking the union disagrees with every closed form
  // at the overlap, so a self-check of this kind fails it.
  std::vector<double> sum = stroke_gate::union_area(body, nullptr, nullptr, 5, 5, 512, stroke_gate::Rule::SumOfPieces);
  EXPECT_NEAR(at(sum, 1, 1), 1.25, 1e-3);
  EXPECT_GT(std::fabs(at(sum, 1, 1) - 1.0), 0.2) << "the plant fails the self-check";
  EXPECT_GT(std::fabs(at(sum, 1, 2) - 0.75), 0.2);
  // The aligned region: body U (aligned & clip), clip nonzero or complemented.
  stroke_gate::Part aligned;
  stroke_gate::Part clip;
  aligned.contours.push_back(stroke_gate::cut_contour(rect_contour(0.0, 0.0, 4.0, 1.5)));
  clip.contours.push_back(stroke_gate::cut_contour(rect_contour(1.0, 0.0, 3.0, 4.0)));
  stroke_gate::Part none;
  std::vector<double> inside = stroke_gate::union_area(none, &aligned, &clip, 5, 5, 512);
  EXPECT_NEAR(at(inside, 1, 0), 1.0, 1e-3);
  EXPECT_NEAR(at(inside, 1, 1), 0.5, 1e-3);
  EXPECT_NEAR(at(inside, 0, 0), 0.0, 1e-3);
  clip.complement = true;
  std::vector<double> outside = stroke_gate::union_area(none, &aligned, &clip, 5, 5, 512);
  EXPECT_NEAR(at(outside, 0, 0), 1.0, 1e-3);
  EXPECT_NEAR(at(outside, 1, 0), 0.0, 1e-3);
  EXPECT_NEAR(at(outside, 3, 1), 0.5, 1e-3);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
