/**
 * @file
 *
 * The lean cover (the fast stroke coverage, lean.c) against the exact coverage on outlines that do not overlap themselves:
 * boxes, a triangle, a star, a many-sided polygon, through windows that cut them and shifts that move them, with the scratch
 * left all zero afterwards.  Where nothing overlaps the two agree to the rounding of a float, which is what the tolerance
 * allows; where pieces overlap the lean one sums them, which is its contract and not tested here.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace sh;

namespace {

struct Flat {
  GSHGEOM_Device_Path * path = nullptr;
  GSHGEOM_Flat_Path * flat = nullptr;
  explicit Flat(const std::vector<std::vector<std::pair<int32_t, int32_t>>> & polys) {
    EXPECT_EQ(gshgeom_device_path_create(nullptr, nullptr, &path), GSHGEOM_OK);
    for (const auto & c : polys) {
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
  ~Flat() {
    gshgeom_flat_path_destroy(flat);
    gshgeom_device_path_destroy(path);
  }
  Flat(const Flat &) = delete;
  Flat & operator=(const Flat &) = delete;
};

using Poly = std::vector<std::pair<int32_t, int32_t>>;

Poly regular(int sides, double cx, double cy, double r, double turn) {
  Poly p;
  for (int i = 0; i < sides; i++) {
    double a = turn + 2.0 * M_PI * i / sides;
    p.push_back({(int32_t)std::lround((cx + r * std::cos(a)) * 64.0), (int32_t)std::lround((cy + r * std::sin(a)) * 64.0)});
  }
  return p;
}

Poly star(int points, double cx, double cy, double outer, double inner) {
  Poly p;
  for (int i = 0; i < 2 * points; i++) {
    double a = M_PI * i / points;
    double r = i % 2 ? inner : outer;
    p.push_back({(int32_t)std::lround((cx + r * std::cos(a)) * 64.0), (int32_t)std::lround((cy + r * std::sin(a)) * 64.0)});
  }
  return p;
}

struct Spans {
  int64_t x0, y0;      // the rectangle's origin, the spans' frame (the paths' frame, moved by the shift)
  int64_t sx, sy;      // the shift the receiver adds
  int64_t w, h;
  std::vector<uint8_t> cov;
  size_t calls = 0;
};

void collect(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  Spans * s = static_cast<Spans *>(ctx);
  s->calls++;
  int64_t py = y + s->sy - s->y0;
  ASSERT_GE(py, 0);
  ASSERT_LT(py, s->h);
  for (size_t i = 0; i < n; i++) {
    int64_t px = x + (int64_t)i + s->sx - s->x0;
    ASSERT_GE(px, 0);
    ASSERT_LT(px, s->w);
    s->cov[(size_t)(py * s->w + px)] = m8[i];
  }
}

struct Worst {
  int max = 0;
  size_t over = 0;
};

/** The lean coverage of @p polys over @p rect, shifted by (@p sx, @p sy), against the exact coverage of the unshifted paths. */
Worst compare(const std::vector<Poly> & polys, GSHCPU_Rect rect, int64_t sx, int64_t sy) {
  Flat flat(polys);
  const int64_t w = rect.x1 - rect.x0, h = rect.y1 - rect.y0;
  // The exact one, in the paths' frame: the rectangle moved back by the shift.
  GSHCPU_Rect back = rect;
  back.x0 -= sx;
  back.x1 -= sx;
  back.y0 -= sy;
  back.y1 -= sy;
  std::vector<uint8_t> want((size_t)(w * h), 0);
  {
    std::vector<int64_t> acc(2 * (size_t)(w + 1) * GSHCPU_RENDER_BAND_ROWS, 0);
    uint64_t cells = 0;
    const GSHGEOM_Flat_Path * paths[1] = {flat.flat};
    EXPECT_EQ(gshcpu_render_cover_counted(paths, 1, GSHGEOM_FILL_NONZERO, back, back, want.data(), acc.data(), UINT64_MAX, &cells),
        GSHCPU_OK);
  }
  const size_t bytes = 2 * (size_t)(w + 1) * GSHCPU_RENDER_BAND_ROWS * sizeof(int64_t);
  std::vector<int64_t> scratch(bytes / sizeof(int64_t), 0);
  std::vector<uint8_t> line((size_t)w + 16, 0);
  Spans s;
  s.x0 = rect.x0;
  s.y0 = rect.y0;
  s.sx = sx;
  s.sy = sy;
  s.w = w;
  s.h = h;
  s.cov.assign((size_t)(w * h), 0);
  EXPECT_EQ(gshcpu_render_lean_cover(flat.flat, sx, sy, rect, scratch.data(), bytes, line.data(), collect, &s), GSHCPU_OK);
  for (int64_t v : scratch) {
    EXPECT_EQ(v, 0) << "the scratch is left all zero";
    if (v != 0) {
      break;
    }
  }
  Worst worst;
  for (size_t i = 0; i < want.size(); i++) {
    int d = std::abs((int)s.cov[i] - (int)want[i]);
    worst.max = std::max(worst.max, d);
    if (d > 3) {
      worst.over++;
    }
  }
  return worst;
}

GSHCPU_Rect rect_of(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  GSHCPU_Rect r;
  r.x0 = x0;
  r.y0 = y0;
  r.x1 = x1;
  r.y1 = y1;
  return r;
}

}  // namespace

TEST(RenderLean, AgreesWithTheExactCoverageOnOutlinesThatDoNotOverlap) {
  const std::vector<std::vector<Poly>> shapes = {
      {{{10 * 64 + 20, 7 * 64 + 5}, {40 * 64 + 33, 7 * 64 + 5}, {40 * 64 + 33, 25 * 64 + 17}, {10 * 64 + 20, 25 * 64 + 17}}},
      {{{5 * 64, 30 * 64 + 9}, {50 * 64 + 7, 12 * 64 + 40}, {33 * 64 + 21, 3 * 64 + 2}}},
      {star(5, 30.3, 20.7, 18.1, 7.3)},
      {regular(37, 28.2, 19.6, 17.4, 0.3)},
      {star(11, 30.0, 20.0, 19.0, 4.0), },
  };
  for (size_t k = 0; k < shapes.size(); k++) {
    Worst w = compare(shapes[k], rect_of(0, 0, 64, 48), 0, 0);
    EXPECT_LE(w.max, 3) << "shape " << k;
    EXPECT_EQ(w.over, 0u) << "shape " << k;
  }
}

TEST(RenderLean, AWindowThatCutsTheShapeAndAShiftThatMovesIt) {
  const std::vector<Poly> shape = {star(7, 30.3, 24.7, 22.1, 9.3)};
  const GSHCPU_Rect windows[] = {rect_of(0, 0, 64, 48), rect_of(10, 8, 40, 36), rect_of(-5, -5, 20, 20), rect_of(25, 15, 80, 60),
      rect_of(0, 0, 7, 48)};
  const int64_t shifts[][2] = {{0, 0}, {3, -2}, {-6, 5}, {40, 40}};
  for (const GSHCPU_Rect & r : windows) {
    for (const auto & sh : shifts) {
      Worst w = compare(shape, rect_of(r.x0 + sh[0], r.y0 + sh[1], r.x1 + sh[0], r.y1 + sh[1]), sh[0], sh[1]);
      EXPECT_LE(w.max, 3) << "window " << r.x0 << "," << r.y0 << " shift " << sh[0] << "," << sh[1];
    }
  }
}

TEST(RenderLean, MoreRowsThanABandAndAnEmptyRectangle) {
  const std::vector<Poly> tall = {{{10 * 64, 2 * 64 + 9}, {40 * 64 + 5, 3 * 64}, {35 * 64, 190 * 64 + 40}, {12 * 64 + 20, 188 * 64}}};
  Worst w = compare(tall, rect_of(0, 0, 50, 200), 0, 0);
  EXPECT_LE(w.max, 3);
  Flat f(tall);
  std::vector<int64_t> scratch(64, 0);
  EXPECT_EQ(gshcpu_render_lean_cover(f.flat, 0, 0, rect_of(5, 5, 5, 9), scratch.data(), 64 * 8, nullptr, collect, nullptr), GSHCPU_OK);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
