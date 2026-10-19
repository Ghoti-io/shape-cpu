/**
 * @file
 *
 * Coverage under hostile input (CAP-15, AD-19): each limit by its bound, the
 * whole 26.6 range without a wrap, and the allocator failing at every call.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "alloc_tracker.h"
#include "coverage_helpers.h"
#include "known_answers.h"

#include <climits>
#include <cstdint>

using namespace cov;

namespace {

GSHCPU_Limits limits_with(size_t dim, size_t bytes, size_t cells, size_t points) {
  GSHCPU_Limits l;
  gshcpu_limits_default(&l);
  l.max_dimension = dim;
  l.max_bytes = bytes;
  l.max_cells = cells;
  l.max_points = points;
  return l;
}

Contour square(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

/** A diamond of the given half size at a centre: slanted, so cells are crossed. */
Contour diamond(int32_t cx, int32_t cy, int32_t r) {
  return {{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}};
}

}  // namespace

TEST(CoverageLimits, TheDefaultsAreWhatTheHeaderSays) {
  GSHCPU_Limits l;
  gshcpu_limits_default(&l);
  EXPECT_EQ(l.max_dimension, 16384u);
  EXPECT_EQ(l.max_bytes, (size_t)256 * 1024 * 1024);
  EXPECT_EQ(l.max_cells, (size_t)1 << 22);
  EXPECT_EQ(l.max_points, 0u);
  for (unsigned char b : l._reserved) {
    EXPECT_EQ(b, 0);
  }
  gshcpu_limits_default(nullptr);   // ignored
  EXPECT_LT((size_t)GSHCPU_MAX_DIMENSION_CEILING * 64, (size_t)INT32_MAX);
}

TEST(CoverageLimits, TheDimensionIsAcceptedAtTheCapAndRefusedOnePast) {
  Path path;
  path.polygon(square(px(1), px(1), px(3), px(3)));
  for (uint32_t dim : {4u, 9u, 64u}) {
    const GSHCPU_Limits l = limits_with(dim, SIZE_MAX, 0, 0);
    Cov ok, wide, tall;
    EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, dim, 4, &l,
                  nullptr, &ok.c), GSHCPU_OK) << dim;
    wide.c.width = 5;
    EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, dim + 1, 4,
                  &l, nullptr, &wide.c), GSHCPU_ERR_LIMIT) << dim;
    EXPECT_EQ(wide.c.width, 5u) << "output untouched";
    wide.c.width = 0;
    EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 4, dim + 1,
                  &l, nullptr, &tall.c), GSHCPU_ERR_LIMIT) << dim;
  }
}

TEST(CoverageLimits, TheBytesAreAcceptedAtTheCapAndRefusedOnePast) {
  Path path;
  path.polygon(square(px(1), px(1), px(3), px(3)));
  const GSHCPU_Limits at = limits_with(0, 5 * 6, 0, 0);
  const GSHCPU_Limits under = limits_with(0, 5 * 6 - 1, 0, 0);
  Cov ok, no;
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 6, &at,
                nullptr, &ok.c), GSHCPU_OK);
  no.c.height = 3;
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 6, &under,
                nullptr, &no.c), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(no.c.height, 3u);
  no.c.height = 0;
}

TEST(CoverageLimits, TheCeilingBindsWhateverTheCallerAsks) {
  Path path;
  path.polygon(square(px(1), px(1), px(3), px(3)));
  const GSHCPU_Limits l = limits_with(SIZE_MAX, SIZE_MAX, 0, 0);
  Cov out;
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0,
                (uint32_t)GSHCPU_MAX_DIMENSION_CEILING + 1, 1, &l, nullptr,
                &out.c), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 1,
                (uint32_t)GSHCPU_MAX_DIMENSION_CEILING + 1, &l, nullptr,
                &out.c), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(out.c.data, nullptr);
}

TEST(CoverageLimits, TheCellsAreAcceptedAtTheCapAndRefusedOnePast) {
  // The number of entries the accumulator makes for each known path is written
  // by the independent generator (its model counts the times the cell changed
  // along the walk, which is what the array holds), so the bound is pinned and
  // not measured from the library.
  size_t checked = 0;
  for (const known::Fill & k : known::fills()) {
    Path path;
    for (const auto & contour : k.contours) {
      path.polygon(Contour(contour.begin(), contour.end()));
    }
    ASSERT_GT(k.cells, 0u) << k.name;
    const GSHCPU_Limits at = limits_with(0, 0, k.cells, 0);
    const GSHCPU_Limits under = limits_with(0, 0, k.cells - 1, 0);
    Cov ok, no;
    ASSERT_EQ(gshcpu_coverage_fill(path, static_cast<GSHGEOM_Fill_Rule>(k.fill),
                  k.left, k.top, k.width, k.height, &at, nullptr, &ok.c),
        GSHCPU_OK) << k.name << " at " << k.cells;
    EXPECT_EQ(gshcpu_coverage_hash(ok.ptr()), k.hash) << "the cap changes no pixel: " << k.name;
    no.c.width = 8;
    EXPECT_EQ(gshcpu_coverage_fill(path, static_cast<GSHGEOM_Fill_Rule>(k.fill),
                  k.left, k.top, k.width, k.height, &under, nullptr, &no.c),
        GSHCPU_ERR_LIMIT) << k.name << " under " << k.cells;
    EXPECT_EQ(no.c.width, 8u) << "output untouched";
    no.c.width = 0;
    checked += 1;
  }
  EXPECT_GE(checked, 90u);
}

TEST(CoverageLimits, ThePointsAreAcceptedAtTheCapAndRefusedOnePast) {
  Path path;
  path.polygon(square(px(1), px(1), px(3), px(3)));   // four points
  const GSHCPU_Limits at = limits_with(0, 0, 0, 4);
  const GSHCPU_Limits under = limits_with(0, 0, 0, 3);
  Cov ok, no;
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 5, &at,
                nullptr, &ok.c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 5, &under,
                nullptr, &no.c), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, &under,
                nullptr, &no.c), GSHCPU_ERR_LIMIT);
  gshcpu_coverage_destroy(&ok.c);   // (a result that is overwritten is a result leaked)
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, &at, nullptr,
                &ok.c), GSHCPU_OK);
}

namespace {

/** floor(a / 64) and ceil(a / 64), written out and not shared with the library. */
int64_t floor64(int64_t a) { return a >= 0 ? a / 64 : -((-a + 63) / 64); }
int64_t ceil64(int64_t a) { return -floor64(-a); }

}  // namespace

TEST(CoverageLimits, TheExtentOfAPathIsTheLimitOfFromPathOnEveryAxisAndSign) {
  // The working bitmap is the extent of the flattened path grown to whole
  // pixels and then by a pixel on every side, so its width is
  // ceil(max / 64) + 1 - (floor(min / 64) - 1). Written here with its own
  // division, for extents that are and are not whole pixels, positive,
  // negative and straddling zero, each axis alone.
  const int64_t spans[][2] = {
      {0, 14 * 64},          // whole pixels from the origin
      {0, 14 * 64 + 1},      // one unit into a pixel
      {1, 14 * 64},          // one unit after a boundary
      {-1, 6 * 64 - 1},      // straddles zero, both ends inside a pixel
      {-64, 9 * 64},         // a negative boundary
      {-15 * 64 - 5, -64 - 9},   // all negative, neither end a boundary
      {-15 * 64, -64},       // all negative, both on boundaries
      {-3 * 64 - 63, -3 * 64 - 62},   // a sliver inside one negative pixel
  };
  for (const auto & span : spans) {
    for (int axis = 0; axis < 2; ++axis) {
      const int32_t lo = (int32_t)span[0], hi = (int32_t)span[1];
      const int32_t depth = 64 * 3 + 5;   // the other axis: three pixels and a bit
      Path path;
      if (axis == 0) {
        path.polygon({{lo, 0}, {hi, 0}, {hi, depth}, {lo, depth}});
      }
      else {
        path.polygon({{0, lo}, {0, hi}, {depth, hi}, {depth, lo}});
      }
      const int64_t want = ceil64(hi) + 1 - (floor64(lo) - 1);
      const int64_t other = ceil64(depth) + 1 - (floor64(0) - 1);
      const int64_t big = want > other ? want : other;
      const GSHCPU_Limits at = limits_with((size_t)big, 0, 0, 0);
      const GSHCPU_Limits under = limits_with((size_t)big - 1, 0, 0, 0);
      Cov ok, no;
      EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, &at, nullptr, &ok.c), GSHCPU_OK)
          << "span " << lo << ".." << hi << " axis " << axis << " want " << want;
      no.c.width = 4;
      EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, &under, nullptr, &no.c), GSHCPU_ERR_LIMIT)
          << "span " << lo << ".." << hi << " axis " << axis << " want " << want;
      EXPECT_EQ(no.c.width, 4u);
      no.c.width = 0;
    }
  }
}

TEST(CoverageLimits, TheDefaultsApplyWhenNoLimitsAreGiven) {
  Path path;
  path.polygon({{0, 0}, {32, 0}, {32, 32}, {0, 32}});   // inside a one pixel rectangle
  Cov ok, no;
  const uint32_t edge = (uint32_t)GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, edge, 1, nullptr, nullptr, &ok.c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, edge + 1, 1, nullptr, nullptr, &no.c), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 1, edge + 1, nullptr, nullptr, &no.c), GSHCPU_ERR_LIMIT);
  // Zero in a field is the default for that field and not "none".
  GSHCPU_Limits zero{};
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, edge + 1, 1, &zero, nullptr, &no.c), GSHCPU_ERR_LIMIT);
  gshcpu_coverage_destroy(&ok.c);   // (a result that is overwritten is a result leaked)
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 3, 3, &zero, nullptr, &ok.c), GSHCPU_OK);
}

// --- the 26.6 range -------------------------------------------------------------

TEST(CoverageRange, ATinyShapeAtEitherEndOfTheRangeIsTheShapeAtTheOriginMoved) {
  const std::vector<Contour> shapes = {
      square(px(2), px(3), px(6), px(5) + 17),
      diamond(px(5) + 9, px(5) + 21, px(4) + 3),
      {{px(1), px(1)}, {px(7) + 11, px(2) + 5}, {px(3) + 7, px(6) + 33}},
  };
  const int64_t top = (int64_t)(INT32_MAX / 64) * 64;   // the largest multiple of 64
  const int64_t lows[] = {INT32_MIN, INT32_MIN + 1000 * 64, -((int64_t)1 << 20)};
  const int64_t highs[] = {top - 64 * 10, top - 64 * 1000};
  for (const Contour & shape : shapes) {
    for (int fill = 0; fill < GSHGEOM_FILL_RULE_COUNT; ++fill) {
      Path home;
      home.polygon(shape);
      Cov origin;
      ASSERT_EQ(gshcpu_coverage_from_path(home, static_cast<GSHGEOM_Fill_Rule>(fill),
                    nullptr, nullptr, &origin.c), GSHCPU_OK);
      ASSERT_GT(origin.c.width, 0u);
      for (int64_t base : {lows[0], lows[1], lows[2], highs[0], highs[1]}) {
        // base is a multiple of 64 (a whole number of pixels).
        ASSERT_EQ(base % 64, 0);
        for (int axis = 0; axis < 3; ++axis) {   // x only, y only, both
          const int64_t dx = axis != 1 ? base : 0, dy = axis != 0 ? base : 0;
          Contour moved;
          bool fits = true;
          for (const Pt & p : shape) {
            const int64_t x = p.first + dx, y = p.second + dy;
            fits = fits && x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN
                && y <= INT32_MAX;
            moved.push_back({(int32_t)x, (int32_t)y});
          }
          ASSERT_TRUE(fits) << base;
          Path far;
          far.polygon(moved);
          Cov out;
          ASSERT_EQ(gshcpu_coverage_from_path(far, static_cast<GSHGEOM_Fill_Rule>(fill),
                        nullptr, nullptr, &out.c), GSHCPU_OK)
              << "base " << base << " axis " << axis;
          EXPECT_EQ(out.hex(), origin.hex()) << "base " << base << " axis " << axis;
          EXPECT_EQ(out.c.width, origin.c.width);
          EXPECT_EQ(out.c.height, origin.c.height);
          EXPECT_EQ((int64_t)out.c.left, origin.c.left + dx / 64);
          EXPECT_EQ((int64_t)out.c.top, origin.c.top + dy / 64);
        }
      }
    }
  }
}

TEST(CoverageRange, ACurveAtTheFarCornerIsTheCurveAtTheOriginMoved) {
  Path home;
  home.move(px(1), px(4)).quad(px(3), px(-1), px(6), px(4)).cubic(px(5), px(6), px(2), px(7), px(1), px(4)).close();
  const int32_t d = INT32_MAX - px(40) + 1;   // a multiple of 64 and near the top
  ASSERT_EQ(d % 64, 0);
  Path far;
  far.move(px(1) + d, px(4) + d).quad(px(3) + d, px(-1) + d, px(6) + d, px(4) + d)
      .cubic(px(5) + d, px(6) + d, px(2) + d, px(7) + d, px(1) + d, px(4) + d).close();
  Cov a, b;
  ASSERT_EQ(gshcpu_coverage_from_path(home, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &a.c), GSHCPU_OK);
  ASSERT_EQ(gshcpu_coverage_from_path(far, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &b.c), GSHCPU_OK);
  EXPECT_EQ(a.hex(), b.hex());
  EXPECT_EQ((int64_t)b.c.left, a.c.left + d / 64);
}

TEST(CoverageRange, AShapeThatSpansTheRangeIsRefusedNotWrapped) {
  Path path;
  path.polygon({{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN}, {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MAX}});
  Cov out;
  out.c.width = 3;
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &out.c), GSHCPU_ERR_LIMIT);
  const GSHCPU_Limits l = limits_with(SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX);
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, &l, nullptr, &out.c), GSHCPU_ERR_LIMIT)
      << "the ceiling holds when every cap is lifted";
  EXPECT_EQ(out.c.width, 3u);
  out.c.width = 0;
  // Just inside the ceiling is accepted, and just past it is not.
  Path edge;
  const int32_t span = (int32_t)(GSHCPU_MAX_DIMENSION_CEILING - 2) * 64;
  edge.polygon({{0, 0}, {span, 0}, {span, 64}, {0, 64}});
  const GSHCPU_Limits big = limits_with(SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX);
  Cov ok;
  EXPECT_EQ(gshcpu_coverage_from_path(edge, GSHGEOM_FILL_NONZERO, &big, nullptr, &ok.c), GSHCPU_OK);
  Path past;
  past.polygon({{0, 0}, {span + 64, 0}, {span + 64, 64}, {0, 64}});
  EXPECT_EQ(gshcpu_coverage_from_path(past, GSHGEOM_FILL_NONZERO, &big, nullptr, &out.c), GSHCPU_ERR_LIMIT);
}

TEST(CoverageRange, ThePointsAtTheExtremesOfInt32GiveNoWrapInTheRectangleFill) {
  // A rectangle whose origin is the largest representable one, with the path
  // inside it, and another with the smallest: the relative coordinates are
  // computed in 64 bits.
  const int32_t left_hi = INT32_MAX / 64 - 6;
  Path hi;
  hi.polygon(square((left_hi + 1) * 64, 64, (left_hi + 3) * 64, 3 * 64));
  Cov a;
  ASSERT_EQ(gshcpu_coverage_fill(hi, GSHGEOM_FILL_NONZERO, left_hi, 0, 5, 5,
                nullptr, nullptr, &a.c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&a.c, 1, 1), 255);
  EXPECT_EQ(gshcpu_coverage_at(&a.c, 0, 1), 0);
  EXPECT_EQ(a.c.left, left_hi);

  const int32_t left_lo = INT32_MIN / 64 + 1;
  Path lo;
  lo.polygon(square((left_lo + 1) * 64, 64, (left_lo + 3) * 64, 3 * 64));
  Cov b;
  ASSERT_EQ(gshcpu_coverage_fill(lo, GSHGEOM_FILL_NONZERO, left_lo, 0, 5, 5,
                nullptr, nullptr, &b.c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&b.c, 1, 1), 255);
  EXPECT_EQ(a.hex(), b.hex());
  // A point of the path far outside the rectangle is invalid, however far.
  Path out;
  out.polygon({{INT32_MIN, 0}, {INT32_MAX, 0}, {INT32_MAX, 64}});
  Cov c;
  EXPECT_EQ(gshcpu_coverage_fill(out, GSHGEOM_FILL_NONZERO, 0, 0, 8, 8, nullptr,
                nullptr, &c.c), GSHCPU_ERR_INVALID);
}

// --- hostile shapes -----------------------------------------------------------------

TEST(CoverageHostile, ManyCollinearAndRepeatedPointsCostWhatTheyAreWorth) {
  Path path;
  path.move(px(1), px(1));
  for (int i = 0; i < 100000; ++i) {
    path.line(px(1) + (i % 5) * 4, px(1) + (i % 7) * 3);
  }
  path.close();
  Cov out;
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &out.c), GSHCPU_OK);

  Path same;
  same.move(px(3), px(3));
  for (int i = 0; i < 100000; ++i) {
    same.line(px(3), px(3));
  }
  same.close();
  Cov none;
  EXPECT_EQ(gshcpu_coverage_from_path(same, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &none.c), GSHCPU_OK);
  EXPECT_EQ(none.c.width, 0u);
}

TEST(CoverageHostile, ASawtoothAcrossTheBitmapIsBoundedByTheCellCap) {
  // 4,000 edges each crossing the whole 400-pixel height: 1.6 million cells
  // would be needed, and the cap refuses long before.
  Path path;
  path.move(px(1), px(0));
  for (int i = 0; i < 4000; ++i) {
    path.line(px(1) + (i % 2) * 64 + (i / 2 % 5), (i % 2) ? px(0) : px(400));
  }
  path.close();
  const GSHCPU_Limits l = limits_with(0, 0, 50000, 0);
  Cov out;
  out.c.stride = 11;
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, &l, nullptr, &out.c), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(out.c.stride, 11u);
  out.c.stride = 0;
}

TEST(CoverageHostile, APointOneUnitOutsideTheRectangleIsRefused) {
  Cov out;
  for (int side = 0; side < 4; ++side) {
    // A square in a 6 by 6 rectangle with one corner pushed one unit outside:
    // before the first column or row, and onto the far boundary.
    Contour c = {{64, 64}, {5 * 64, 64}, {5 * 64, 5 * 64}, {64, 5 * 64}};
    if (side == 0) c[0].first = -1;
    if (side == 1) c[0].second = -1;
    if (side == 2) c[2].first = 6 * 64;
    if (side == 3) c[2].second = 6 * 64;
    Path path;
    path.polygon(c);
    EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 6, 6, nullptr, nullptr, &out.c), GSHCPU_ERR_INVALID) << side;
    // And the point just inside is accepted.
    if (side == 0) c[0].first = 0;
    if (side == 1) c[0].second = 0;
    if (side == 2) c[2].first = 6 * 64 - 1;
    if (side == 3) c[2].second = 6 * 64 - 1;
    Path inside;
    inside.polygon(c);
    EXPECT_EQ(gshcpu_coverage_fill(inside, GSHGEOM_FILL_NONZERO, 0, 0, 6, 6, nullptr, nullptr, &out.c), GSHCPU_OK) << side;
    gshcpu_coverage_destroy(&out.c);
  }
}

TEST(CoverageHostile, TheCellArrayGrowsGeometrically) {
  // A sawtooth with about 6,000 cells: doubling reallocates a dozen times, a
  // growth by one reallocates thousands.
  tracker::Tracker t;
  GSHGEOM_Allocator a = tracker::allocator_for(&t);
  Path path;
  path.move(px(1), px(0));
  for (int i = 0; i < 60; ++i) {
    path.line(px(1) + (i % 2) * 64 + (i / 2 % 5), (i % 2) ? px(0) : px(100));
  }
  path.close();
  Cov out;
  ASSERT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, nullptr, &a, &out.c), GSHCPU_OK);
  EXPECT_LT(t.calls, 40) << "allocation calls for a path of thousands of cells";
  gshcpu_coverage_destroy(&out.c);
  EXPECT_TRUE(t.live.empty());
}

TEST(CoverageHostile, TheEdgeOfTheMarginIsHandled) {
  // Points exactly on the first pixel boundary of the rectangle and one unit
  // before its far edge.
  Path path;
  path.polygon({{0, 0}, {5 * 64 - 1, 0}, {5 * 64 - 1, 5 * 64 - 1}, {0, 5 * 64 - 1}});
  Cov out;
  ASSERT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 5, nullptr, nullptr, &out.c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 0, 0), 255);
  // 63/64 in each direction: the exact value is 256 * 63 * 63 / 4096 = 248.06.
  // The contour runs clockwise on the screen, so its accumulated area is
  // negative, and the fold takes the floor of the signed area before the
  // absolute value: 249 (the moved code's rule, pinned by the known answers).
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 4, 4), 249);
}

// --- allocation failure ---------------------------------------------------------------

TEST(CoverageAlloc, AnEmptyResultLeavesNothingBehind) {
  // The paths are built with the default allocator, so every block the tracker
  // sees belongs to the call under test.
  Path lone;
  lone.move(px(2), px(2));
  Path flat;
  flat.move(px(1), px(2)).line(px(4), px(2)).line(px(1), px(2)).close();
  Path none;
  for (const Path * p : {&lone, &flat, &none}) {
    tracker::Tracker t;
    GSHGEOM_Allocator a = tracker::allocator_for(&t);
    Cov out;
    EXPECT_EQ(gshcpu_coverage_from_path(*p, GSHGEOM_FILL_NONZERO, nullptr, &a, &out.c), GSHCPU_OK);
    EXPECT_EQ(out.c.data, nullptr);
    gshcpu_coverage_destroy(&out.c);
    EXPECT_TRUE(t.live.empty()) << "an empty result left a block behind";
  }
  // A fill of a path with nothing to draw allocates the pixels; the trim
  // releases them again.
  tracker::Tracker t;
  GSHGEOM_Allocator a = tracker::allocator_for(&t);
  Cov blank;
  EXPECT_EQ(gshcpu_coverage_fill(flat, GSHGEOM_FILL_NONZERO, 0, 0, 6, 6, nullptr, &a, &blank.c), GSHCPU_OK);
  ASSERT_NE(blank.c.data, nullptr);
  gshcpu_coverage_trim(&blank.c);
  EXPECT_EQ(blank.c.data, nullptr);
  EXPECT_TRUE(t.live.empty()) << "the trim of an empty fill left its pixels behind";
  gshcpu_coverage_destroy(&blank.c);
}

TEST(CoverageAlloc, NothingTheCallMadeSurvivesAFailure) {
  // The path itself is built with the default allocator, so every live block
  // in the tracker belongs to the call under test.
  Path path;
  path.polygon(diamond(px(10), px(10), px(8)));
  path.polygon(diamond(px(10), px(10), px(4)));
  long calls = 0;
  {
    tracker::Tracker t;
    GSHGEOM_Allocator a = tracker::allocator_for(&t);
    Cov out;
    ASSERT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_EVEN_ODD, nullptr, &a, &out.c), GSHCPU_OK);
    calls = t.calls;
    gshcpu_coverage_destroy(&out.c);
    EXPECT_TRUE(t.live.empty());
  }
  ASSERT_GT(calls, 4);
  for (long k = 0; k < calls; ++k) {
    for (bool persistent : {false, true}) {
      tracker::Tracker t;
      GSHGEOM_Allocator a = tracker::allocator_for(&t);
      t.fail_from = k;
      t.persistent = persistent;
      GSHCPU_Coverage out;
      std::memset(&out, 0xAB, sizeof out);
      EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_EVEN_ODD, nullptr, &a, &out), GSHCPU_ERR_OOM)
          << "at call " << k;
      EXPECT_TRUE(t.live.empty()) << "a failure at call " << k << " leaked";
      EXPECT_EQ(out.width, 0xABABABABu);
      tracker::Tracker t2;
      GSHGEOM_Allocator a2 = tracker::allocator_for(&t2);
      t2.fail_from = k;
      t2.persistent = persistent;
      EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_EVEN_ODD, 0, 0, 22, 22, nullptr, &a2, &out), GSHCPU_ERR_OOM)
          << "fill at call " << k << (k >= t2.calls ? " (past the end)" : "");
      EXPECT_TRUE(t2.live.empty()) << "a failure of fill at call " << k << " leaked";
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
