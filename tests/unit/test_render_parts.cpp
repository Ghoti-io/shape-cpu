/**
 * @file
 *
 * The sink's parts (story 13b): the clipped scan converter against story 9's
 * (every pixel of every window equal to the whole path's coverage there), the
 * workspace arena and its measure, the D8 weight, the stores of the write, and
 * the operators' table.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "render_helpers.h"

#include <ghoti.io/math/elementary.h>

#include <cmath>
#include <limits>
#include <random>

using namespace rh;

namespace {

/** A device path and its flattening. */
struct Device {
  GSHGEOM_Device_Path * path = nullptr;
  GSHGEOM_Flat_Path * flat = nullptr;
  Device() {
    gshgeom_device_path_create(nullptr, nullptr, &path);
  }
  ~Device() {
    gshgeom_flat_path_destroy(flat);
    gshgeom_device_path_destroy(path);
  }
  Device(const Device &) = delete;
  Device & operator=(const Device &) = delete;
  void flatten() {
    ASSERT_EQ(gshgeom_device_path_flatten(path, nullptr, nullptr, &flat), GSHGEOM_OK);
  }
};

GSHGEOM_Device_Point dp(int32_t x, int32_t y) {
  GSHGEOM_Device_Point p;
  p.x = x;
  p.y = y;
  return p;
}

/** Random subpaths of lines and curves inside [lo, hi) in 26.6, some open. */
void random_path(std::mt19937 & rng, Device & d, int32_t lo, int32_t hi) {
  std::uniform_int_distribution<int32_t> c(lo, hi - 1);
  int subpaths = 1 + (int)(rng() % 3);
  for (int s = 0; s < subpaths; s++) {
    gshgeom_device_path_move_to(d.path, dp(c(rng), c(rng)));
    int n = 2 + (int)(rng() % 7);
    for (int k = 0; k < n; k++) {
      switch (rng() % 4) {
        case 0:
        case 1:
          gshgeom_device_path_line_to(d.path, dp(c(rng), c(rng)));
          break;
        case 2:
          gshgeom_device_path_quadratic_to(d.path, dp(c(rng), c(rng)), dp(c(rng), c(rng)));
          break;
        default:
          gshgeom_device_path_cubic_to(d.path, dp(c(rng), c(rng)), dp(c(rng), c(rng)), dp(c(rng), c(rng)));
          break;
      }
    }
    if (rng() % 3) {
      gshgeom_device_path_close(d.path);
    }
  }
  d.flatten();
}

/** The coverage of @p rect by the clipped scan converter. */
std::vector<uint8_t> cover(const GSHGEOM_Flat_Path * flat, GSHGEOM_Fill_Rule rule, GSHCPU_Rect rect, uint64_t * cells = nullptr) {
  size_t w = (size_t)(rect.x1 - rect.x0);
  size_t h = (size_t)(rect.y1 - rect.y0);
  std::vector<uint8_t> out(w * h, 0xee);
  std::vector<int64_t> acc(2 * (w + 1) * GSHCPU_RENDER_BAND_ROWS, 77);
  uint64_t n = 0;
  const GSHGEOM_Flat_Path * paths[1] = {flat};
  EXPECT_EQ(gshcpu_render_cover(paths, 1, rule, rect, out.data(), acc.data(), UINT64_MAX, &n), GSHCPU_OK);
  if (cells) {
    *cells = n;
  }
  return out;
}

} // namespace

TEST(RenderCover, EveryWindowIsTheWholePathsCoverageThere) {
  std::mt19937 rng(91);
  int windows = 0;
  for (int round = 0; round < 400; round++) {
    Device d;
    random_path(rng, d, -10 * 64, 50 * 64);
    for (GSHGEOM_Fill_Rule rule : {GSHGEOM_FILL_NONZERO, GSHGEOM_FILL_EVEN_ODD}) {
      // Story 9's coverage of the path in a rectangle holding it with its margin.
      GSHCPU_Coverage whole;
      ASSERT_EQ(gshcpu_coverage_fill(d.path, rule, -12, -12, 64, 64, nullptr, nullptr, &whole), GSHCPU_OK);
      // The same rectangle through the clipped converter.
      GSHCPU_Rect all{-12, -12, 52, 52};
      std::vector<uint8_t> full = cover(d.flat, rule, all);
      for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
          ASSERT_EQ(full[(size_t)(y * 64 + x)], gshcpu_coverage_at(&whole, (uint32_t)x, (uint32_t)y)) << round << " " << x << "," << y;
        }
      }
      // Windows anywhere, the path partly or wholly outside them.
      for (int k = 0; k < 6; k++) {
        int64_t x0 = -12 + (int64_t)(rng() % 64);
        int64_t y0 = -12 + (int64_t)(rng() % 64);
        int64_t x1 = x0 + 1 + (int64_t)(rng() % (uint32_t)(52 - x0));
        int64_t y1 = y0 + 1 + (int64_t)(rng() % (uint32_t)(52 - y0));
        GSHCPU_Rect win{x0, y0, x1, y1};
        std::vector<uint8_t> part = cover(d.flat, rule, win);
        size_t w = (size_t)(x1 - x0);
        for (int64_t y = y0; y < y1; y++) {
          for (int64_t x = x0; x < x1; x++) {
            ASSERT_EQ(part[(size_t)(y - y0) * w + (size_t)(x - x0)], full[(size_t)((y + 12) * 64 + (x + 12))])
                << round << " window " << x0 << "," << y0 << "," << x1 << "," << y1 << " at " << x << "," << y;
          }
        }
        windows++;
      }
      gshcpu_coverage_destroy(&whole);
    }
  }
  EXPECT_EQ(windows, 400 * 2 * 6);
}

TEST(RenderCover, AHugeShapeIsExactInASmallWindow) {
  // Edges far outside the window on every side, crossing it: the window's coverage is the same as in a larger one.
  std::mt19937 rng(7);
  for (int round = 0; round < 200; round++) {
    Device d;
    random_path(rng, d, -4000000 * 64 / 1000, 4000000 * 64 / 1000);
    GSHCPU_Rect small{-3, -5, 9, 11};
    GSHCPU_Rect big{-40, -70, 60, 50};
    for (GSHGEOM_Fill_Rule rule : {GSHGEOM_FILL_NONZERO, GSHGEOM_FILL_EVEN_ODD}) {
      std::vector<uint8_t> a = cover(d.flat, rule, small);
      std::vector<uint8_t> b = cover(d.flat, rule, big);
      for (int64_t y = small.y0; y < small.y1; y++) {
        for (int64_t x = small.x0; x < small.x1; x++) {
          ASSERT_EQ(a[(size_t)((y - small.y0) * 12 + (x - small.x0))], b[(size_t)((y - big.y0) * 100 + (x - big.x0))])
              << round << " at " << x << "," << y;
        }
      }
    }
  }
}

TEST(RenderCover, BandsJoinAndCellsAreCountedToTheLimit) {
  // A tall shape crosses many bands; its coverage is the same whatever row the window starts on.
  Device d;
  gshgeom_device_path_move_to(d.path, dp(3 * 64 + 17, 0 * 64 + 9));
  gshgeom_device_path_line_to(d.path, dp(9 * 64 + 40, 70 * 64 + 3));
  gshgeom_device_path_line_to(d.path, dp(1 * 64 + 5, 51 * 64 + 50));
  gshgeom_device_path_close(d.path);
  d.flatten();
  GSHCPU_Coverage whole;
  ASSERT_EQ(gshcpu_coverage_from_path(d.path, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &whole), GSHCPU_OK);
  for (int64_t top = -3; top < 20; top++) {
    GSHCPU_Rect win{whole.left, top, (int64_t)whole.left + whole.width, 75};
    std::vector<uint8_t> part = cover(d.flat, GSHGEOM_FILL_NONZERO, win);
    for (int64_t y = top; y < 75; y++) {
      for (int64_t x = win.x0; x < win.x1; x++) {
        uint8_t want = y >= whole.top && y < (int64_t)whole.top + whole.height
            ? gshcpu_coverage_at(&whole, (uint32_t)(x - whole.left), (uint32_t)(y - whole.top))
            : 0;
        ASSERT_EQ(part[(size_t)((y - top) * (int64_t)whole.width + (x - win.x0))], want) << top << " " << x << "," << y;
      }
    }
  }
  gshcpu_coverage_destroy(&whole);
  uint64_t cells = 0;
  GSHCPU_Rect r{0, 0, 12, 72};
  cover(d.flat, GSHGEOM_FILL_NONZERO, r, &cells);
  ASSERT_GT(cells, 10u);
  std::vector<uint8_t> out(12 * 72);
  std::vector<int64_t> acc(2 * 13 * GSHCPU_RENDER_BAND_ROWS);
  const GSHGEOM_Flat_Path * paths[2] = {nullptr, d.flat};
  uint64_t n = 0;
  EXPECT_EQ(gshcpu_render_cover(paths, 2, GSHGEOM_FILL_NONZERO, r, out.data(), acc.data(), cells, &n), GSHCPU_OK);
  EXPECT_EQ(n, cells);
  EXPECT_EQ(gshcpu_render_cover(paths, 2, GSHGEOM_FILL_NONZERO, r, out.data(), acc.data(), cells - 1, &n), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(n, cells - 1);
}

TEST(RenderCover, ACellIsACrossingOfNonzeroHeight) {
  // A shallow edge crosses three columns of one row and has height in one of them only: one cell, not three.
  Device d;
  gshgeom_device_path_move_to(d.path, dp(0, 0));
  gshgeom_device_path_line_to(d.path, dp(130, 1));
  gshgeom_device_path_line_to(d.path, dp(0, 64));
  gshgeom_device_path_close(d.path);
  d.flatten();
  uint64_t cells = 0;
  cover(d.flat, GSHGEOM_FILL_NONZERO, GSHCPU_Rect{0, 0, 4, 2}, &cells);
  EXPECT_EQ(cells, 5u) << "1 for the shallow edge, 3 for the steep one, 1 for the closing one";
  // A pixel-aligned rectangle: a cell per row of each vertical edge.
  Device r;
  gshgeom_device_path_move_to(r.path, dp(2 * 64, 2 * 64));
  gshgeom_device_path_line_to(r.path, dp(5 * 64, 2 * 64));
  gshgeom_device_path_line_to(r.path, dp(5 * 64, 4 * 64));
  gshgeom_device_path_line_to(r.path, dp(2 * 64, 4 * 64));
  gshgeom_device_path_close(r.path);
  r.flatten();
  cover(r.flat, GSHGEOM_FILL_NONZERO, GSHCPU_Rect{0, 0, 10, 10}, &cells);
  EXPECT_EQ(cells, 4u);
  cover(r.flat, GSHGEOM_FILL_NONZERO, GSHCPU_Rect{3, 0, 4, 10}, &cells);
  EXPECT_EQ(cells, 2u) << "left of the window: one folded cell a row; right of it: none";
  cover(r.flat, GSHGEOM_FILL_NONZERO, GSHCPU_Rect{0, 0, 5, 10}, &cells);
  EXPECT_EQ(cells, 2u) << "a crossing in the column just right of the window is not one of its cells";
  // Counted over a larger rectangle than the one drawn: the same rule at the larger one's right edge.
  const GSHGEOM_Flat_Path * paths[1] = {r.flat};
  std::vector<uint8_t> out(3 * 10);
  std::vector<int64_t> acc(2 * 4 * GSHCPU_RENDER_BAND_ROWS);
  ASSERT_EQ(gshcpu_render_cover_counted(paths, 1, GSHGEOM_FILL_NONZERO, GSHCPU_Rect{0, 0, 3, 10}, GSHCPU_Rect{0, 0, 5, 10},
                out.data(), acc.data(), UINT64_MAX, &cells),
      GSHCPU_OK);
  EXPECT_EQ(cells, 2u);
  ASSERT_EQ(gshcpu_render_cover_counted(paths, 1, GSHGEOM_FILL_NONZERO, GSHCPU_Rect{0, 0, 3, 10}, GSHCPU_Rect{0, 0, 6, 10},
                out.data(), acc.data(), UINT64_MAX, &cells),
      GSHCPU_OK);
  EXPECT_EQ(cells, 4u);
}

TEST(RenderCover, TheCellBoundIsItsFormula) {
  // gshcpu_render_cells_bound is a bound by a stated formula (render_internal.h): per edge with height, its rows R in
  // the walk plus min(its column span + R, R times the walk's width).  Pinned on known edges, so that a looser bound
  // (which would only make a damage-limited render count more often) is seen too.  Two pixel-aligned rectangles, A
  // from (2, 2) to (5, 4) and B from (10, 1) to (12, 7), as two subpaths: a vertical edge from y0 to y1 spans the
  // rows floor(y0) to floor(y1), the last of height zero, and has no span; a horizontal edge has no height.
  Device two;
  for (int k = 0; k < 2; k++) {
    int32_t x0 = k ? 10 : 2, y0 = k ? 1 : 2, x1 = k ? 12 : 5, y1 = k ? 7 : 4;
    gshgeom_device_path_move_to(two.path, dp(x0 * 64, y0 * 64));
    gshgeom_device_path_line_to(two.path, dp(x1 * 64, y0 * 64));
    gshgeom_device_path_line_to(two.path, dp(x1 * 64, y1 * 64));
    gshgeom_device_path_line_to(two.path, dp(x0 * 64, y1 * 64));
    gshgeom_device_path_close(two.path);
  }
  two.flatten();
  const GSHGEOM_Flat_Path * paths[1] = {two.flat};
  // Whole: A's edges 3 rows each (3 + 3), B's 7 (7 + 7): 2 * 6 + 2 * 14.
  EXPECT_EQ(gshcpu_render_cells_bound(paths, 1, GSHCPU_Rect{0, 0, 20, 20}, UINT64_MAX), 40u);
  // Rows from 3: A 2 rows, B 5.
  EXPECT_EQ(gshcpu_render_cells_bound(paths, 1, GSHCPU_Rect{0, 3, 20, 20}, UINT64_MAX), 2u * 4u + 2u * 10u);
  // Rows to 4 (below 5): A 3 rows, B 4.
  EXPECT_EQ(gshcpu_render_cells_bound(paths, 1, GSHCPU_Rect{0, 0, 20, 5}, UINT64_MAX), 2u * 6u + 2u * 8u);
  // A shallow edge 40 columns long in a walk 4 wide: 2 rows, min(40 + 2, 2 * 4) columns; its closing edge 2 + 2.
  Device shallow;
  gshgeom_device_path_move_to(shallow.path, dp(0, 0));
  gshgeom_device_path_line_to(shallow.path, dp(40 * 64, 64));
  gshgeom_device_path_line_to(shallow.path, dp(0, 64));
  gshgeom_device_path_close(shallow.path);
  shallow.flatten();
  const GSHGEOM_Flat_Path * one[1] = {shallow.flat};
  EXPECT_EQ(gshcpu_render_cells_bound(one, 1, GSHCPU_Rect{0, 0, 4, 2}, UINT64_MAX), (2u + 8u) + (2u + 2u));
  EXPECT_EQ(gshcpu_render_cells_bound(one, 1, GSHCPU_Rect{0, 0, 100, 2}, UINT64_MAX), (2u + 42u) + (2u + 2u));
  // A limit stops the sum only once it is passed: a sum that reaches it exactly goes on to the next path.
  const GSHGEOM_Flat_Path * both[2] = {shallow.flat, two.flat};
  EXPECT_EQ(gshcpu_render_cells_bound(both, 2, GSHCPU_Rect{0, 0, 4, 2}, 14u), 14u + 2u * 2u)
      << "A's rows 2 and 3 meet the walk's rows 0 and 1 not at all, B's row 1 does: 2 + min(0 + 1, 4) each";
  EXPECT_EQ(gshcpu_render_cells_bound(one, 1, GSHCPU_Rect{0, 0, 20, 20}, UINT64_MAX), 46u);
  EXPECT_GT(gshcpu_render_cells_bound(both, 2, GSHCPU_Rect{0, 0, 20, 20}, 46u), 46u);
}

TEST(RenderCover, CountedOverALargerRectangleItDrawsTheSmallerAndCountsTheLarger) {
  // S4: a damage-limited render draws a part of what the whole render would (rect) and meets the cell cap over the
  // whole of it (walk).  The coverage is the smaller rectangle's, byte for byte; the count, and so the refusal, is the
  // larger's, to the cell; the bound from the edges is never below it.
  std::mt19937 rng(1621);
  int windows = 0;
  int below_bound = 0;
  for (int round = 0; round < 300; round++) {
    Device d;
    random_path(rng, d, -10 * 64, 50 * 64);
    const GSHGEOM_Flat_Path * paths[2] = {d.flat, nullptr};
    for (int k = 0; k < 8; k++) {
      int64_t x0 = -12 + (int64_t)(rng() % 64);
      int64_t y0 = -12 + (int64_t)(rng() % 64);
      int64_t x1 = x0 + 1 + (int64_t)(rng() % (uint32_t)(52 - x0));
      int64_t y1 = y0 + 1 + (int64_t)(rng() % (uint32_t)(52 - y0));
      GSHCPU_Rect walk{x0, y0, x1, y1};
      // A rectangle inside the walk, or an empty one (the item wholly outside the damage).
      GSHCPU_Rect rect{0, 0, 0, 0};
      if (k % 4 != 3) {
        rect.x0 = x0 + (int64_t)(rng() % (uint32_t)(x1 - x0));
        rect.y0 = y0 + (int64_t)(rng() % (uint32_t)(y1 - y0));
        rect.x1 = rect.x0 + 1 + (int64_t)(rng() % (uint32_t)(x1 - rect.x0));
        rect.y1 = rect.y0 + 1 + (int64_t)(rng() % (uint32_t)(y1 - rect.y0));
      }
      GSHGEOM_Fill_Rule rule = k & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
      uint64_t walk_cells = 0;
      cover(d.flat, rule, walk, &walk_cells);
      std::vector<uint8_t> want;
      if (!gshcpu_render_rect_empty(rect)) {
        want = cover(d.flat, rule, rect);
      }
      size_t w = gshcpu_render_rect_empty(rect) ? 0 : (size_t)(rect.x1 - rect.x0);
      size_t h = gshcpu_render_rect_empty(rect) ? 0 : (size_t)(rect.y1 - rect.y0);
      std::vector<uint8_t> out(w * h + 1, 0xee);
      std::vector<int64_t> acc(2 * (w + 1) * GSHCPU_RENDER_BAND_ROWS + 1, 77);
      uint64_t n = 0;
      ASSERT_EQ(gshcpu_render_cover_counted(paths, 2, rule, rect, walk, out.data(), acc.data(), UINT64_MAX, &n), GSHCPU_OK);
      EXPECT_EQ(n, walk_cells) << round << " " << k;
      for (size_t i = 0; i < w * h; i++) {
        ASSERT_EQ(out[i], want[i]) << round << " " << k << " at " << i;
      }
      EXPECT_EQ(out[w * h], 0xee) << "past the rectangle drawn";
      // The cap is met over the walk, to the cell.
      if (walk_cells > 0) {
        EXPECT_EQ(gshcpu_render_cover_counted(paths, 2, rule, rect, walk, out.data(), acc.data(), walk_cells, &n), GSHCPU_OK);
        EXPECT_EQ(gshcpu_render_cover_counted(paths, 2, rule, rect, walk, out.data(), acc.data(), walk_cells - 1, &n),
            GSHCPU_ERR_LIMIT)
            << round << " " << k;
      }
      uint64_t bound = gshcpu_render_cells_bound(paths, 2, walk, UINT64_MAX);
      EXPECT_GE(bound, walk_cells) << round << " " << k;
      below_bound += bound > walk_cells;
      if (bound > 1) {
        EXPECT_GT(gshcpu_render_cells_bound(paths, 2, walk, bound / 2), bound / 2) << "past its limit, it says so";
      }
      windows++;
    }
  }
  EXPECT_EQ(windows, 300 * 8);
  EXPECT_GT(below_bound, 0);
  EXPECT_EQ(gshcpu_render_cells_bound(nullptr, 0, GSHCPU_Rect{0, 0, 10, 10}, 0), 0u);
}

TEST(RenderCover, TheCellBoundHoldsForLongAndHugeEdges) {
  // Edges far outside the walk on every side, steep and shallow: the bound stays above the count and is not
  // overflowed by a width times a height.
  std::mt19937 rng(77);
  for (int round = 0; round < 200; round++) {
    Device d;
    random_path(rng, d, -4000000 * 64 / 1000, 4000000 * 64 / 1000);
    const GSHGEOM_Flat_Path * paths[1] = {d.flat};
    for (GSHCPU_Rect walk : {GSHCPU_Rect{-3, -5, 9, 11}, GSHCPU_Rect{-400, -700, 600, 500}, GSHCPU_Rect{-4000, 0, 4000, 1}}) {
      uint64_t cells = 0;
      cover(d.flat, GSHGEOM_FILL_NONZERO, walk, &cells);
      EXPECT_GE(gshcpu_render_cells_bound(paths, 1, walk, UINT64_MAX), cells) << round;
    }
  }
}

TEST(RenderCover, Rectangles) {
  GSHCPU_Rect a{0, 0, 10, 10};
  GSHCPU_Rect b{5, -2, 20, 7};
  GSHCPU_Rect m = gshcpu_render_rect_meet(a, b);
  EXPECT_EQ(m.x0, 5);
  EXPECT_EQ(m.y0, 0);
  EXPECT_EQ(m.x1, 10);
  EXPECT_EQ(m.y1, 7);
  GSHCPU_Rect j = gshcpu_render_rect_join(a, b);
  EXPECT_EQ(j.x0, 0);
  EXPECT_EQ(j.y0, -2);
  EXPECT_EQ(j.x1, 20);
  EXPECT_EQ(j.y1, 10);
  GSHCPU_Rect e{3, 3, 3, 9};
  EXPECT_TRUE(gshcpu_render_rect_empty(e));
  EXPECT_TRUE(gshcpu_render_rect_empty(GSHCPU_Rect{3, 4, 9, 4}));
  EXPECT_TRUE(gshcpu_render_rect_empty(GSHCPU_Rect{3, 4, 2, 9}));
  EXPECT_FALSE(gshcpu_render_rect_empty(a));
  GSHCPU_Rect c{3, 4, 30, 40};
  GSHCPU_Rect d{7, 2, 11, 50};
  GSHCPU_Rect cd = gshcpu_render_rect_join(c, d);
  EXPECT_EQ(cd.x0, 3);
  EXPECT_EQ(cd.y0, 2);
  EXPECT_EQ(cd.x1, 30);
  EXPECT_EQ(cd.y1, 50);
  GSHCPU_Rect dc = gshcpu_render_rect_meet(d, c);
  EXPECT_EQ(dc.x0, 7);
  EXPECT_EQ(dc.y0, 4);
  EXPECT_EQ(dc.x1, 11);
  EXPECT_EQ(dc.y1, 40);
  GSHCPU_Rect je = gshcpu_render_rect_join(e, b);
  EXPECT_EQ(je.x0, b.x0);
  EXPECT_EQ(je.x1, b.x1);
  GSHCPU_Rect jb = gshcpu_render_rect_join(b, e);
  EXPECT_EQ(jb.y0, b.y0);
  EXPECT_EQ(jb.y1, b.y1);
}

/* --- the arena ------------------------------------------------------------------------------------------- */

namespace {
/** A sequence of calls, as geometry makes them. */
void calls(const GSHGEOM_Allocator * a, std::vector<void *> & keep) {
  void * p = a->malloc_fn(a->ctx, 40);
  void * q = a->calloc_fn(a->ctx, 3, 8);
  keep.push_back(q);
  for (size_t n = 8; n <= 400; n *= 3) {
    p = a->realloc_fn(a->ctx, p, n); // q stands after p: a new block and a copy each time... until p is last
  }
  void * r = a->realloc_fn(a->ctx, nullptr, 17);
  r = a->realloc_fn(a->ctx, r, 33);  // the last: in place
  r = a->realloc_fn(a->ctx, r, 5);   // shrinking in place
  a->free_fn(a->ctx, q);
  void * s = a->malloc_fn(a->ctx, 0);
  keep.push_back(p);
  keep.push_back(r);
  keep.push_back(s);
}
} // namespace

TEST(RenderArena, TheMeasureIsTheSizeThatRepeatsTheCalls) {
  GSHCPU_Arena measure;
  tracker::Tracker t;
  GSHSCENE_Allocator real = tracker::allocator_for(&t);
  gshcpu_render_arena_measure(&measure, &real);
  std::vector<void *> keep;
  calls(&measure.self, keep);
  for (size_t i = 1; i < keep.size(); i++) { // keep[0] was freed by calls(), and its address may be s's now
    measure.self.free_fn(measure.self.ctx, keep[i]);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " live of " << keep.size();
  size_t need = measure.peak;
  ASSERT_GT(need, 0u);
  EXPECT_EQ(need % 16, 0u);
  // An arena of exactly that size repeats them; one 16 smaller cannot.
  alignas(16) static unsigned char memory[4096];
  ASSERT_LE(need, sizeof memory);
  GSHCPU_Arena a;
  gshcpu_render_arena_init(&a, memory, need);
  std::vector<void *> got;
  calls(&a.self, got);
  for (void * p : got) {
    EXPECT_NE(p, nullptr);
  }
  EXPECT_EQ(a.peak, need) << "the arena's high water is the measure's";
  GSHCPU_Arena b;
  gshcpu_render_arena_init(&b, memory, need - 16);
  std::vector<void *> short_of;
  calls(&b.self, short_of);
  bool any_null = false;
  for (void * p : short_of) {
    any_null = any_null || p == nullptr;
  }
  EXPECT_TRUE(any_null) << "an arena smaller than the measure ran the calls anyway";
}

TEST(RenderArena, ACallocdLastBlockGrowsInPlaceInTheMeasureAsInTheArena) {
  tracker::Tracker t;
  GSHSCENE_Allocator real = tracker::allocator_for(&t);
  GSHCPU_Arena m;
  gshcpu_render_arena_measure(&m, &real);
  void * a = m.self.malloc_fn(m.self.ctx, 100);
  void * b = m.self.calloc_fn(m.self.ctx, 4, 8);
  b = m.self.realloc_fn(m.self.ctx, b, 300);
  EXPECT_EQ(m.peak, (16u + 112u) + (16u + 304u)) << "the calloc'd block, last, grew where it was";
  m.self.free_fn(m.self.ctx, a);
  m.self.free_fn(m.self.ctx, b);
  EXPECT_TRUE(t.live.empty());
  alignas(16) static unsigned char memory[1024];
  GSHCPU_Arena r;
  gshcpu_render_arena_init(&r, memory, m.peak);
  a = r.self.malloc_fn(r.self.ctx, 100);
  b = r.self.calloc_fn(r.self.ctx, 4, 8);
  EXPECT_NE(r.self.realloc_fn(r.self.ctx, b, 300), nullptr) << "the measure's size repeats it";
  EXPECT_EQ(r.used, m.peak);
  // Growing the last block past the capacity fails and takes nothing.
  EXPECT_EQ(r.self.realloc_fn(r.self.ctx, b, 400), nullptr);
  EXPECT_EQ(r.used, m.peak);
  (void)a;
}

TEST(RenderArena, BlocksAreAlignedZeroedCopiedAndGrownInPlace) {
  alignas(16) static unsigned char memory[1024];
  GSHCPU_Arena a;
  gshcpu_render_arena_init(&a, memory, sizeof memory);
  const GSHGEOM_Allocator * al = &a.self;
  unsigned char * p = (unsigned char *)al->malloc_fn(al->ctx, 5);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ((uintptr_t)p % 16, 0u);
  EXPECT_EQ(p, memory + 16) << "a 16-byte header precedes every block";
  std::memset(p, 0xab, 5);
  unsigned char * g = (unsigned char *)al->realloc_fn(al->ctx, p, 40);
  EXPECT_EQ(g, p) << "the last block grows where it is";
  EXPECT_EQ(a.used, 16u + 48u);
  std::memset(memory + 256, 0x55, 64);
  unsigned char * z = (unsigned char *)al->calloc_fn(al->ctx, 4, 4);
  ASSERT_NE(z, nullptr);
  for (int i = 0; i < 16; i++) {
    EXPECT_EQ(z[i], 0);
  }
  unsigned char * moved = (unsigned char *)al->realloc_fn(al->ctx, g, 80);
  ASSERT_NE(moved, nullptr);
  EXPECT_NE(moved, g) << "a block that is not the last moves";
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(moved[i], 0xab) << "and its bytes go with it";
  }
  size_t moved_at = (size_t)(moved - memory) - 16u;
  EXPECT_EQ(a.used, moved_at + 16u + 80u);
  unsigned char * shrunk = (unsigned char *)al->realloc_fn(al->ctx, moved, 10);
  EXPECT_EQ(shrunk, moved);
  EXPECT_EQ(a.used, moved_at + 16u + 16u) << "the moved block is the last now, and shrinks where it is";
  size_t used = a.used;
  al->free_fn(al->ctx, shrunk);
  EXPECT_EQ(a.used, used) << "a free keeps the block";
  unsigned char * after = (unsigned char *)al->realloc_fn(al->ctx, nullptr, 3);
  EXPECT_EQ(after, memory + used + 16);
  EXPECT_EQ(al->calloc_fn(al->ctx, SIZE_MAX, 2), nullptr) << "an overflowing calloc";
  EXPECT_EQ(al->malloc_fn(al->ctx, SIZE_MAX - 7), nullptr) << "an overflowing size";
  EXPECT_EQ(al->malloc_fn(al->ctx, 2000), nullptr) << "more than is left";
  al->free_fn(al->ctx, nullptr);
  gshcpu_render_arena_reset(&a);
  EXPECT_EQ(a.used, 0u);
  EXPECT_EQ(al->malloc_fn(al->ctx, 1), memory + 16);
  EXPECT_GE(a.peak, used);
}

TEST(RenderArena, TheMeasurePassesOnTheRealAllocatorsFailure) {
  tracker::Tracker t;
  GSHSCENE_Allocator real = tracker::allocator_for(&t);
  GSHCPU_Arena m;
  gshcpu_render_arena_measure(&m, &real);
  t.fail_from = 0;
  t.persistent = true;
  EXPECT_EQ(m.self.malloc_fn(m.self.ctx, 8), nullptr);
  EXPECT_EQ(m.self.calloc_fn(m.self.ctx, 2, 8), nullptr);
  EXPECT_EQ(m.self.realloc_fn(m.self.ctx, nullptr, 8), nullptr);
  EXPECT_EQ(m.used, 0u) << "a failed call takes nothing";
  t.fail_from = -1;
  void * p = m.self.malloc_fn(m.self.ctx, 8);
  ASSERT_NE(p, nullptr);
  t.fail_from = t.calls;
  EXPECT_EQ(m.self.realloc_fn(m.self.ctx, p, 64), nullptr);
  EXPECT_EQ(m.used, 32u);
  void * q = nullptr;
  t.fail_from = -1;
  q = m.self.malloc_fn(m.self.ctx, 8);
  t.fail_from = t.calls;
  EXPECT_EQ(m.self.realloc_fn(m.self.ctx, p, 64), nullptr) << "a block not the last";
  EXPECT_EQ(m.used, 64u);
  t.fail_from = -1;
  m.self.free_fn(m.self.ctx, p);
  m.self.free_fn(m.self.ctx, q);
  EXPECT_TRUE(t.live.empty());
}

/* --- the arithmetic's parts ----------------------------------------------------------------------------------- */

TEST(RenderArithmetic, TheWeightOfAClipIsTheRoundedProduct) {
  for (uint32_t m = 0; m <= 255; m++) {
    for (uint32_t c = 0; c <= 255; c++) {
      uint32_t w = gshcpu_render_weigh(m, c);
      // The nearest integer to m c / 255, halves up (an odd denominator: there are none).
      double exact = (double)m * (double)c / 255.0;
      ASSERT_LE(std::fabs((double)w - exact), 0.5) << m << " " << c;
      ASSERT_EQ(w, (m * c + 127u) / 255u);
    }
  }
  EXPECT_EQ(gshcpu_render_weigh(255, 200), 200u) << "a full clip changes nothing";
  EXPECT_EQ(gshcpu_render_weigh(200, 255), 200u);
  EXPECT_EQ(gshcpu_render_weigh(128, 128), 64u);
  EXPECT_EQ(gshcpu_render_weigh(1, 127), 0u);
  EXPECT_EQ(gshcpu_render_weigh(1, 128), 1u);
}

TEST(RenderArithmetic, TheEightBitStoreRoundsHalfUpAndClamps) {
  for (int q = 0; q < 255; q++) {
    double boundary = (q + 0.5) / 255.0;
    EXPECT_EQ(gshcpu_render_store8(std::nextafter(boundary, 2.0) + 1e-12), q + 1) << q;
    EXPECT_EQ(gshcpu_render_store8(boundary - 1e-9), q) << q;
    EXPECT_EQ(gshcpu_render_store8((double)q / 255.0), q) << q;
  }
  EXPECT_EQ(gshcpu_render_store8(0.5 / 255.0), 1) << "0.5 exactly: half up";
  EXPECT_EQ(gshcpu_render_store8(127.5 / 255.0), 128);
  EXPECT_EQ(gshcpu_render_store8(0.0), 0);
  EXPECT_EQ(gshcpu_render_store8(-0.0), 0);
  EXPECT_EQ(gshcpu_render_store8(-1e-300), 0);
  EXPECT_EQ(gshcpu_render_store8(-3.0), 0);
  EXPECT_EQ(gshcpu_render_store8(1.0), 255);
  EXPECT_EQ(gshcpu_render_store8(1.5), 255);
  EXPECT_EQ(gshcpu_render_store8(254.4999 / 255.0), 254);
  EXPECT_EQ(gshcpu_render_store8(std::numeric_limits<double>::infinity()), 255);
  EXPECT_EQ(gshcpu_render_store8(std::nan("")), 0);
  EXPECT_EQ(gshcpu_render_store_float(0.1), 0.1f);
  EXPECT_EQ(gshcpu_render_store_float(-2.5), -2.5f);
  EXPECT_EQ(gshcpu_render_store_float(1.0 + 0x1p-24), 1.0f) << "halfway: to even";
  EXPECT_EQ(gshcpu_render_store_float(1.0 + 0x1p-24 + 0x1p-40), std::nextafter(1.0f, 2.0f));
}

TEST(RenderArithmetic, LoadsAndStoresOfEachFormat) {
  // An 8-bit straight pixel: premultiplied on the way in, divided on the way out, the same bytes back.
  for (GSHCPU_Format format : RF_FORMATS) {
    RasterRef r(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    RasterRef back(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    unsigned char * src = (unsigned char *)gimg_raster_pixels(r.raster);
    unsigned char * dst = (unsigned char *)gimg_raster_pixels(back.raster);
    size_t stride = gimg_raster_stride_bytes(r.raster);
    for (uint32_t y = 0; y < RF_HEIGHT; y++) {
      for (uint32_t x = 0; x < RF_WIDTH; x++) {
        double v[4];
        gshcpu_render_load(format, src + y * stride, x, v);
        gshcpu_render_store(format, dst + y * stride, x, v);
        unsigned char s[4];
        rf_backdrop8(x, y, s);
        bool straight = format == GSHCPU_FORMAT_RGBA8_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_STRAIGHT;
        if (straight && s[3] == 0) {
          // Alpha 0: the color is not kept (a pixel no unit touched is never written, render.h).
          for (int i = 0; i < 3; i++) {
            EXPECT_EQ(v[i], 0.0);
          }
          continue;
        }
        ASSERT_EQ(back.pixel(x, y), r.pixel(x, y)) << gshcpu_format_string(format) << " " << x << "," << y;
      }
    }
  }
  unsigned char row[4] = {255, 128, 0, 128};
  double v[4];
  gshcpu_render_load(GSHCPU_FORMAT_RGBA8_STRAIGHT, row, 0, v);
  EXPECT_EQ(v[3], 128.0 / 255.0);
  EXPECT_EQ(v[0], 1.0 * (128.0 / 255.0));
  gshcpu_render_load(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, row, 0, v);
  EXPECT_EQ(v[0], 1.0) << "premultiplied bytes are read as they are, even past their alpha";
  double out[4] = {0.25, 0.5, 2.0, 0.5};
  gshcpu_render_store(GSHCPU_FORMAT_RGBA8_STRAIGHT, row, 0, out);
  EXPECT_EQ(row[0], 128);
  EXPECT_EQ(row[1], 255);
  EXPECT_EQ(row[2], 255) << "clamped";
  EXPECT_EQ(row[3], 128);
  float f[4];
  gshcpu_render_store(GSHCPU_FORMAT_RGBAF32_STRAIGHT, (unsigned char *)f, 0, out);
  EXPECT_EQ(f[2], 4.0f) << "a float is not clamped";
  double zero[4] = {0.25, 0.5, 0.75, 0.0};
  gshcpu_render_store(GSHCPU_FORMAT_RGBAF32_STRAIGHT, (unsigned char *)f, 0, zero);
  EXPECT_EQ(f[0], 0.0f) << "alpha 0: the straight color is 0";
  gshcpu_render_store(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, (unsigned char *)f, 0, zero);
  EXPECT_EQ(f[2], 0.75f) << "premultiplied: stored as it is";
  EXPECT_EQ(gshcpu_render_pixel_bytes(GSHCPU_FORMAT_RGBA8_STRAIGHT), 4u);
  EXPECT_EQ(gshcpu_render_pixel_bytes(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED), 4u);
  EXPECT_EQ(gshcpu_render_pixel_bytes(GSHCPU_FORMAT_RGBAF32_STRAIGHT), 16u);
  EXPECT_EQ(gshcpu_render_pixel_bytes(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED), 16u);
}

TEST(RenderArithmetic, TheOperatorsTableAndBoundedness) {
  const double s[4] = {0.25, 0.125, 0.5, 0.5};
  const double d0[4] = {0.5, 0.25, 0.125, 0.75};
  // (Fa, Fb) of N4 for each operator, PLUS apart.
  const double fa[12] = {1, 0, 1, 0, 1 - 0.75, 0.75, 0, 1 - 0.75, 0, 0.75, 1 - 0.75, 1 - 0.75};
  const double fb[12] = {1 - 0.5, 0, 0, 1, 1, 0, 0.5, 0, 1 - 0.5, 1 - 0.5, 0.5, 1 - 0.5};
  for (uint32_t op = 0; op < 12; op++) {
    double d[4];
    std::memcpy(d, d0, sizeof d);
    gshcpu_render_apply(d, s, 1.0, op);
    for (int i = 0; i < 4; i++) {
      EXPECT_EQ(d[i], fa[op] * s[i] + fb[op] * d0[i]) << op << " " << i;
    }
    std::memcpy(d, d0, sizeof d);
    gshcpu_render_apply(d, s, 0.25, op);
    for (int i = 0; i < 4; i++) {
      EXPECT_EQ(d[i], 0.75 * d0[i] + 0.25 * (fa[op] * s[i] + fb[op] * d0[i])) << op << " " << i;
    }
    const double zero[4] = {0, 0, 0, 0};
    std::memcpy(d, d0, sizeof d);
    gshcpu_render_apply(d, zero, 1.0, op);
    bool kept = std::memcmp(d, d0, sizeof d) == 0;
    EXPECT_EQ(kept, gshcpu_render_bounded(op) != 0) << "bounded: op(0, D) = D, " << op;
  }
  double d[4] = {0.75, 0.5, 0.25, 0.875};
  gshcpu_render_apply(d, s, 1.0, GSHSCENE_COMPOSITE_PLUS);
  EXPECT_EQ(d[0], 1.0);
  EXPECT_EQ(d[1], 0.625);
  EXPECT_EQ(d[2], 0.75);
  EXPECT_EQ(d[3], 1.0);
  EXPECT_TRUE(gshcpu_render_bounded(GSHSCENE_COMPOSITE_PLUS));
  EXPECT_FALSE(gshcpu_render_bounded(GSHSCENE_COMPOSITE_COUNT));
  // An operator apply does not know is not drawn as another (CLEAR, before): it says so and leaves D alone.
  for (uint32_t unknown : {(uint32_t)GSHSCENE_COMPOSITE_COUNT, 31u, 255u}) {
    double e[4];
    std::memcpy(e, d0, sizeof e);
    EXPECT_EQ(gshcpu_render_apply(e, s, 1.0, unknown), 0) << unknown;
    EXPECT_EQ(std::memcmp(e, d0, sizeof e), 0) << "D untouched by unknown operator " << unknown;
  }
}

TEST(RenderArithmetic, EveryBlendModeIsBoundedAndFiniteAtTheCorners) {
  // B3: op_X(0, D) = D exactly, so a group of a blend mode leaves the backdrop where it drew nothing; B9: at the corners
  // +-2^64 every mode is finite and within the saturation.
  const double d0[4] = {0.25, -0.5, 2.0, 0.75};
  const double zero[4] = {0, 0, 0, 0};
  const double corner[] = {0x1p64, -0x1p64, 0.0, 1.0};
  for (uint32_t op = GSHSCENE_COMPOSITE_SCREEN; op <= GSHSCENE_COMPOSITE_LUMINOSITY; op++) {
    EXPECT_TRUE(gshcpu_render_bounded(op)) << op;
    EXPECT_TRUE(gshcpu_render_blends(op)) << op;
    double d[4];
    std::memcpy(d, d0, sizeof d);
    ASSERT_EQ(gshcpu_render_apply(d, zero, 1.0, op), 1);
    EXPECT_EQ(std::memcmp(d, d0, sizeof d), 0) << "bounded: " << op;
    for (double a : corner) {
      for (double b : corner) {
        double cb[3] = {a, b, 0.5}, cs[3] = {b, 0.25, a}, out[3];
        gshcpu_render_mix(op, cb, cs, out);
        for (double v : out) {
          EXPECT_TRUE(std::isfinite(v) && std::fabs(v) <= 0x1p64) << op;
        }
        double s4[4] = {b, a, 0.25, 1.0}, d4[4] = {a, b, -a, 1.0};
        gshcpu_render_apply(d4, s4, 0.5, op);
        for (double v : d4) {
          EXPECT_TRUE(std::isfinite(v)) << op;
        }
      }
    }
  }
  EXPECT_FALSE(gshcpu_render_blends(GSHSCENE_COMPOSITE_PLUS));
  EXPECT_FALSE(gshcpu_render_blends(GSHSCENE_COMPOSITE_COUNT));
}

TEST(RenderArithmetic, AConversionBetweenEqualSpacesCopies) {
  GSHSCENE_Color_Space p3{};
  p3.primaries = GCOL_PRIMARIES_DISPLAY_P3;
  p3.transfer = GCOL_TRANSFER_GAMMA;
  p3.gamma = 2.2;
  GSHSCENE_Color_Space p3b = p3;
  p3b.gamma = 1.8;
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &p3, &p3);
  const double in[4] = {0.25, -0.5, 1.5, 0.5};
  double out[4] = {9, 9, 9, 9};
  gshcpu_render_convert_straight(&c, in, out);
  EXPECT_EQ(out[0], 0.25);
  EXPECT_EQ(out[1], -0.5);
  EXPECT_EQ(out[2], 1.5);
  double pre[4] = {9, 9, 9, 9};
  gshcpu_render_convert_premultiplied(&c, in, pre);
  EXPECT_EQ(std::memcmp(pre, in, sizeof pre), 0);
  // Spaces that differ only in their gamma are not equal: the colour moves.
  gshcpu_render_convert_prepare(&c, &p3, &p3b);
  EXPECT_FALSE(c.same);
  EXPECT_FALSE(c.matrix);
  gshcpu_render_convert_straight(&c, in, out);
  EXPECT_NE(out[0], 0.25);
}

TEST(RenderArithmetic, TransfersAndTheirKnees) {
  GSHSCENE_Color_Space srgb{};
  srgb.primaries = GCOL_PRIMARIES_SRGB;
  srgb.transfer = GCOL_TRANSFER_SRGB;
  GSHSCENE_Color_Space lin = srgb;
  lin.transfer = GCOL_TRANSFER_LINEAR;
  GSHSCENE_Color_Space gamma = srgb;
  gamma.transfer = GCOL_TRANSFER_GAMMA;
  gamma.gamma = 2.2;
  EXPECT_EQ(gshcpu_render_decode(&lin, -0.3), -0.3);
  EXPECT_EQ(gshcpu_render_encode(&lin, 7.0), 7.0);
  EXPECT_EQ(gshcpu_render_decode(&srgb, 0.04045), 0.04045 / 12.92) << "the knee is on the linear side";
  EXPECT_EQ(gshcpu_render_decode(&srgb, -0.5), -0.5 / 12.92) << "the linear segment continues below zero";
  EXPECT_EQ(gshcpu_render_encode(&srgb, 0.0031308), 0.0031308 * 12.92);
  EXPECT_NEAR(gshcpu_render_decode(&srgb, 0.5), 0.21404114048223255, 1e-15);
  EXPECT_NEAR(gshcpu_render_encode(&srgb, 0.21404114048223255), 0.5, 1e-15);
  EXPECT_NEAR(gshcpu_render_decode(&srgb, 0.0404500001), 0.04045 / 12.92, 1e-8) << "continuous at the knee, to the curve's own 1e-8";
  EXPECT_EQ(gshcpu_render_decode(&gamma, 0.0), 0.0);
  EXPECT_EQ(gshcpu_render_encode(&gamma, 0.0), 0.0);
  EXPECT_NEAR(gshcpu_render_decode(&gamma, 0.5), 0.21763764082403103, 1e-15);
  EXPECT_NEAR(gshcpu_render_decode(&gamma, -0.5), -0.21763764082403103, 1e-15) << "odd about zero";
  EXPECT_NEAR(gshcpu_render_encode(&gamma, -0.21763764082403103), -0.5, 1e-15);
  EXPECT_NEAR(gshcpu_render_encode(&gamma, 0.21763764082403103), 0.5, 1e-15);
  // The matrices: each gamut's white goes to D65's XYZ.
  for (int p = GCOL_PRIMARIES_SRGB; p <= GCOL_PRIMARIES_BT2020; p++) {
    if (p != GCOL_PRIMARIES_SRGB && p != GCOL_PRIMARIES_ADOBE_RGB && p != GCOL_PRIMARIES_DISPLAY_P3 &&
        p != GCOL_PRIMARIES_BT2020) {
      continue;
    }
    GSHCPU_Mat3 m;
    gshcpu_render_rgb_to_xyz(p, &m);
    double x = m.m[0][0] + m.m[0][1] + m.m[0][2];
    double y = m.m[1][0] + m.m[1][1] + m.m[1][2];
    double z = m.m[2][0] + m.m[2][1] + m.m[2][2];
    EXPECT_NEAR(x, 0.3127 / 0.3290, 1e-14) << p;
    EXPECT_NEAR(y, 1.0, 1e-14) << p;
    EXPECT_NEAR(z, (1 - 0.3127 - 0.3290) / 0.3290, 1e-14) << p;
  }
  GSHCPU_Mat3 s709;
  gshcpu_render_rgb_to_xyz(GCOL_PRIMARIES_SRGB, &s709);
  EXPECT_NEAR(s709.m[0][0], 0.41239079926595934, 1e-14) << "the published sRGB matrix";
  EXPECT_NEAR(s709.m[1][1], 0.71516867876775593, 1e-14);
  EXPECT_NEAR(s709.m[2][2], 0.95053215224966058, 1e-14);
}

// --- N1's saturation (shape-scene's paint.h; Corey, 2026-10-21) ---------------------------------------------------------

namespace {

GSHSCENE_Color_Space gspace(GCOL_Primaries p, double g) {
  GSHSCENE_Color_Space s{};
  s.primaries = p;
  s.transfer = GCOL_TRANSFER_GAMMA;
  s.gamma = g;
  return s;
}

const double kSat = 0x1p64;
const double k16 = 0x1p16;

}  // namespace

TEST(RenderSaturation, TheClampAndTheUnpremultiply) {
  EXPECT_EQ(gshcpu_render_saturate(kSat), kSat);
  EXPECT_EQ(gshcpu_render_saturate(std::nextafter(kSat, INFINITY)), kSat);
  EXPECT_EQ(gshcpu_render_saturate(-std::nextafter(kSat, INFINITY)), -kSat);
  EXPECT_EQ(gshcpu_render_saturate(-INFINITY), -kSat);
  EXPECT_EQ(gshcpu_render_saturate(1.5), 1.5);
  EXPECT_TRUE(std::signbit(gshcpu_render_saturate(-0.0))) << "a value inside, bit for bit";
  EXPECT_EQ(gshcpu_render_unpremultiply(3.0, 4.0), 0.75);
  EXPECT_EQ(gshcpu_render_unpremultiply(1.0, -0.5), -2.0);
  EXPECT_EQ(gshcpu_render_unpremultiply(0x1p63, 0.5), kSat) << "exactly at the bound";
  EXPECT_EQ(gshcpu_render_unpremultiply(std::nextafter(0x1p63, 0.0), 0.5), std::nextafter(kSat, 0.0)) << "just inside";
  EXPECT_EQ(gshcpu_render_unpremultiply(kSat, 0.5), kSat);
  EXPECT_EQ(gshcpu_render_unpremultiply(-1.0, 0x1p-1074), -kSat) << "a division that would overflow is not taken";
  EXPECT_EQ(gshcpu_render_unpremultiply(1.0, -0x1p-1074), -kSat);
}

TEST(RenderSaturation, EachStepOfAConversionSaturates) {
  const GSHSCENE_Color_Space g10 = gspace(GCOL_PRIMARIES_SRGB, 10.0);
  const GSHSCENE_Color_Space g01 = gspace(GCOL_PRIMARIES_SRGB, 0.1);
  const GSHSCENE_Color_Space g2 = gspace(GCOL_PRIMARIES_SRGB, 2.0);
  const GSHSCENE_Color_Space bt_g10 = gspace(GCOL_PRIMARIES_BT2020, 10.0);
  GSHSCENE_Color_Space lin{};
  lin.primaries = GCOL_PRIMARIES_SRGB;
  lin.transfer = GCOL_TRANSFER_LINEAR;
  GSHCPU_Convert c;
  double out[3];
  // The decode: 2^160 saturates to 2^64 before a gamma-2 encode, which gives 2^32 (2^80 without it).
  const double at[3] = {k16, -k16, 0.0};
  gshcpu_render_convert_prepare(&c, &g10, &g2);
  gshcpu_render_convert_straight(&c, at, out);
  EXPECT_EQ(out[0], 0x1p32);
  EXPECT_EQ(out[1], -0x1p32);
  // The encode: a linear 2^16 into gamma 0.1 is 2^160, saturated.
  gshcpu_render_convert_prepare(&c, &lin, &g01);
  gshcpu_render_convert_straight(&c, at, out);
  EXPECT_EQ(out[0], kSat);
  EXPECT_EQ(out[1], -kSat);
  // Both: gamma 10 to gamma 0.1 would be the 100th power.
  gshcpu_render_convert_prepare(&c, &g10, &g01);
  gshcpu_render_convert_straight(&c, at, out);
  EXPECT_EQ(out[0], kSat);
  EXPECT_EQ(out[1], -kSat);
  // The matrix: BT.2020 to sRGB takes (2^64, -2^64, -2^64) to (2.32, -1.25, -1.04) times 2^64, each saturated, so
  // the gamma-10 encode is (2^64)^(1/10) on each.
  const double bt[3] = {k16, -k16, -k16};
  gshcpu_render_convert_prepare(&c, &bt_g10, &g10);
  ASSERT_TRUE(c.matrix);
  gshcpu_render_convert_straight(&c, bt, out);
  const double tenth = gmath_pow(kSat, 1.0 / 10.0);
  EXPECT_EQ(out[0], tenth);
  EXPECT_EQ(out[1], -tenth);
  EXPECT_EQ(out[2], -tenth);
  // The un-premultiply of a group's conversion: -2^64 at alpha 1/2 is -2^65, saturated before the decode.
  const double pre[4] = {-kSat, 0.0, 0.0, 0.5};
  double moved[4];
  gshcpu_render_convert_prepare(&c, &g01, &lin);
  gshcpu_render_convert_premultiplied(&c, pre, moved);
  EXPECT_EQ(moved[0], -gmath_pow(kSat, 0.1) * 0.5);
  EXPECT_EQ(moved[3], 0.5);
}

TEST(RenderSaturation, EveryApplySaturatesAndAStraightStoreToo) {
  // PLUS of two values at -2^64 (alpha clamped to 1 from above).
  double d[4] = {-kSat, 0.5, 0.0, 1.0};
  const double s[4] = {-kSat, 0.75, 0.0, 1.0};
  gshcpu_render_apply(d, s, 1.0, GSHSCENE_COMPOSITE_PLUS);
  EXPECT_EQ(d[0], -kSat);
  EXPECT_EQ(d[1], 1.0);
  EXPECT_EQ(d[3], 1.0);
  // A float backdrop past the bound: source-over at half weight saturates the result.
  double big[4] = {0x1p100, 0.0, 0.0, 1.0};
  const double none[4] = {0.0, 0.0, 0.0, 0.0};
  gshcpu_render_apply(big, none, 0.5, GSHSCENE_COMPOSITE_SRC_OVER);
  EXPECT_EQ(big[0], kSat);
  // A straight store whose C / A passes the bound.
  unsigned char px[16];
  const double v[4] = {-kSat, kSat, 0.25, 0.5};
  gshcpu_render_store(GSHCPU_FORMAT_RGBAF32_STRAIGHT, px, 0, v);
  float f[4];
  std::memcpy(f, px, sizeof f);
  EXPECT_EQ(f[0], -0x1p64f);
  EXPECT_EQ(f[1], 0x1p64f);
  EXPECT_EQ(f[2], 0.5f);
  EXPECT_EQ(f[3], 0.5f);
}

TEST(RenderSaturation, AGradientsUnpremultiplySaturatesWhereDoublesWouldPassTheBound) {
  // G4 with the flag: stops of color 2^64 and alphas of one and two subnormal steps.  At t = 1/4 the alpha rounds to
  // 2^-1074 while the interpolant of the color is exact, so a plain division would give 1.25 * 2^64.
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 4.0;
  g.premultiplied = 1;
  g.stop_count = 2;
  GSHCPU_Stop stops[2];
  std::memset(stops, 0, sizeof stops);
  stops[0].offset = 0.0;
  stops[1].offset = 1.0;
  for (int k = 0; k < 2; k++) {
    stops[k].rgba[0] = kSat;
    stops[k].rgba[1] = -kSat;
  }
  stops[0].rgba[3] = 0x1p-1074;
  stops[1].rgba[3] = 0x1p-1073;
  gshcpu_render_gradient_stops(stops, 2, 1);
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  GSHCPU_Gradient gr;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &id, 1.0), GSHCPU_OK);
  double out[4];
  gshcpu_render_gradient_color(&gr, 0.25, out);
  EXPECT_EQ(out[0], kSat);
  EXPECT_EQ(out[1], -kSat);
  EXPECT_EQ(out[3], 0x1p-1074);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
