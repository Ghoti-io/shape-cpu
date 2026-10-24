/**
 * @file
 *
 * The exact union coverage of a stroke part (strokes story S4b, UC1 to UC11): the scan converter's exact step against an
 * independent instrument (a scanline sweep of the whole polygon set in `long double`), on hand-built and random sets of
 * pieces, through windows, orders and the sparse sweep, its limits, and the sink's field `max_exact_fragments` (UC9).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <cstring>

using namespace sh;
using stroke_gate::Contour;
using stroke_gate::Ops;
using stroke_gate::Params;
using stroke_gate::Pt;

namespace {

Pt pt(int64_t x, int64_t y) {
  Pt p;
  p.x = x;
  p.y = y;
  return p;
}

/** A rectangle, positively wound (the right edge goes down), the corners in 26.6. */
Contour box(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)};
}

/** The signed area of a contour (twice, y down): positive for the winding the expansion's pieces have. */
int64_t shoelace(const Contour & c) {
  int64_t a = 0;
  for (size_t i = 0; i < c.size(); i++) {
    const Pt & p = c[i];
    const Pt & q = c[(i + 1) % c.size()];
    a += p.x * q.y - q.x * p.y;
  }
  return a;
}

Contour positive(Contour c) {
  if (shoelace(c) < 0) {
    std::reverse(c.begin(), c.end());
  }
  return c;
}

/** The polygons as a flat path of closed subpaths. */
struct Flat {
  GSHGEOM_Device_Path * path = nullptr;
  GSHGEOM_Flat_Path * flat = nullptr;
  explicit Flat(const std::vector<Contour> & polys) {
    EXPECT_EQ(gshgeom_device_path_create(nullptr, nullptr, &path), GSHGEOM_OK);
    for (const Contour & c : polys) {
      for (size_t i = 0; i < c.size(); i++) {
        GSHGEOM_Device_Point p;
        p.x = (int32_t)c[i].x;
        p.y = (int32_t)c[i].y;
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

/** The memory of the exact step for walks up to @p width cells wide. */
struct ExactMem {
  std::vector<unsigned char> raw;
  GSHCPU_Exact ex;
  ExactMem(size_t width, uint64_t cap) {
    raw.resize(gshcpu_exact_bytes(width, cap, 1) + 16);
    uintptr_t at = ((uintptr_t)raw.data() + 15u) & ~(uintptr_t)15u;
    gshcpu_exact_init(&ex, (void *)at, width, cap, 1);
    ex.eps = GSHCPU_RENDER_EXACT_EPS;
  }
};

struct Covered {
  GSHCPU_Result result = GSHCPU_OK;
  std::vector<uint8_t> k;
  uint64_t cells = 0;
  uint64_t failures = 0;
  uint64_t exact_cells = 0;
  uint64_t work = 0;
  uint64_t replays = 0;
};

GSHCPU_Rect rect_of(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  GSHCPU_Rect r;
  r.x0 = x0;
  r.y0 = y0;
  r.x1 = x1;
  r.y1 = y1;
  return r;
}

/** The exact coverage of @p flat over @p rect, the cells counted over @p walk, at most @p cap pieces to a cell. */
Covered cover_exact(const Flat & flat, GSHCPU_Rect rect, GSHCPU_Rect walk, uint64_t cap = 512,
    uint64_t max_deposits = UINT64_MAX) {
  Covered c;
  size_t w = (size_t)(rect.x1 - rect.x0);
  size_t h = (size_t)(rect.y1 - rect.y0);
  c.k.assign(w * h, 0xee);
  std::vector<int64_t> acc(2 * (w + 1) * GSHCPU_RENDER_BAND_ROWS, 77);
  ExactMem mem((size_t)(walk.x1 - walk.x0), cap);
  const GSHGEOM_Flat_Path * paths[1] = {flat.flat};
  c.result = gshcpu_render_cover_exact(paths, 1, GSHGEOM_FILL_NONZERO, rect, walk, c.k.data(), acc.data(), max_deposits, &c.cells,
      &mem.ex);
  c.failures = mem.ex.failures;
  c.exact_cells = mem.ex.cells;
  c.work = mem.ex.work;
  c.replays = mem.ex.replays;
  return c;
}

Covered cover_exact(const Flat & flat, GSHCPU_Rect rect, uint64_t cap = 512) {
  return cover_exact(flat, rect, rect, cap);
}

/** The sparse sweep of the same: the spans it hands over written into a raster of zeros. */
struct SpanCtx {
  GSHCPU_Rect rect;
  std::vector<uint8_t> * out;
};

void span_into(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  SpanCtx * s = (SpanCtx *)ctx;
  size_t w = (size_t)(s->rect.x1 - s->rect.x0);
  for (size_t i = 0; i < n; i++) {
    (*s->out)[(size_t)(y - s->rect.y0) * w + (size_t)(x - s->rect.x0) + i] = m8[i];
  }
}

Covered cover_sparse(const Flat & flat, GSHCPU_Rect rect, uint64_t cap = 512) {
  Covered c;
  size_t w = (size_t)(rect.x1 - rect.x0);
  size_t h = (size_t)(rect.y1 - rect.y0);
  c.k.assign(w * h, 0);
  std::vector<int64_t> acc(2 * (w + 1) * GSHCPU_RENDER_BAND_ROWS, 0);
  std::vector<uint8_t> line(w + 1);
  ExactMem mem(w, cap);
  SpanCtx ctx = {rect, &c.k};
  const GSHGEOM_Flat_Path * paths[1] = {flat.flat};
  c.result = gshcpu_render_cover_sparse(paths, 1, GSHGEOM_FILL_NONZERO, rect, rect, acc.data(), line.data(), UINT64_MAX, &c.cells,
      span_into, &ctx, &mem.ex);
  c.failures = mem.ex.failures;
  c.exact_cells = mem.ex.cells;
  c.work = mem.ex.work;
  c.replays = mem.ex.replays;
  return c;
}

/** The instrument's area of the union of @p polys in each pixel of a @p w by @p h raster at the origin, in levels. */
std::vector<double> reference(const std::vector<Contour> & polys, int w, int h) {
  stroke_gate::Part body;
  for (const Contour & c : polys) {
    body.contours.push_back(stroke_gate::cut_contour(c));
  }
  std::vector<double> a = stroke_gate::union_area(body, nullptr, nullptr, w, h, 768);
  for (double & v : a) {
    v *= 255.0;
  }
  return a;
}

/** The worst |k - 255 A| over the pixels, in levels. */
double worst_off(const Covered & c, const std::vector<double> & ref) {
  double w = 0;
  for (size_t i = 0; i < ref.size(); i++) {
    w = std::max(w, std::fabs(c.k[i] - ref[i]));
  }
  return w;
}

constexpr int64_t U = 64;  // a pixel

}  // namespace

/* --- UC1, UC3 to UC7: closed forms ----------------------------------------------------------------------------------- */

TEST(RenderExact, NestedEdgesGiveTheUnionNotTheSum) {
  // Two squares whose right edges are x = 30 and x = 40 inside one pixel: the union is 40 of 64, the sum 70 of 64.
  Flat f({box(-100, -100, 30, 164), box(-100, -100, 40, 164)});
  Covered c = cover_exact(f, rect_of(0, 0, 1, 1));
  ASSERT_EQ(c.result, GSHCPU_OK);
  EXPECT_EQ(c.k[0], 160) << "40/64 of 256";
  EXPECT_EQ(c.failures, 0u);
  EXPECT_EQ(c.exact_cells, 1u);
  // The dense sum, the stopgap, is 70/64 of the pixel: saturated.
  std::vector<uint8_t> dense(1);
  std::vector<int64_t> acc(2 * 2 * GSHCPU_RENDER_BAND_ROWS);
  uint64_t cells = 0;
  const GSHGEOM_Flat_Path * paths[1] = {f.flat};
  ASSERT_EQ(gshcpu_render_cover(paths, 1, GSHGEOM_FILL_NONZERO, rect_of(0, 0, 1, 1), dense.data(), acc.data(), UINT64_MAX, &cells),
      GSHCPU_OK);
  EXPECT_EQ(dense[0], 255);
}

TEST(RenderExact, ACellWhollyInsideTheUnionIs255AndASeamInsideItChangesNothing) {
  // Two rectangles side by side sharing the edge x = 32, both wholly crossing the pixel: the seam is a pair of coincident
  // edges of opposite direction, and the pixel is full.
  Flat f({box(-100, -100, 32, 164), box(32, -100, 200, 164)});
  Covered c = cover_exact(f, rect_of(0, 0, 1, 1));
  ASSERT_EQ(c.result, GSHCPU_OK);
  EXPECT_EQ(c.k[0], 255);
  EXPECT_EQ(c.failures, 0u);
}

TEST(RenderExact, CrossingEdgesGiveTheUnionsArea) {
  // A: a band from lower left to upper right, B: the mirror band; they cross in the middle of the pixel.  Two bands of
  // thickness 16 (vertically) and slope 3/4: each covers 981 1/3 of the pixel's 4096 square units, their overlap is a
  // rhombus of 170 2/3, the union 1792 2/3 of 4096 (112 of 256 levels).
  Contour a = positive({pt(0, 40), pt(64, -8), pt(64, 8), pt(0, 56)});
  Contour b = positive({pt(0, 8), pt(64, 56), pt(64, 72), pt(0, 24)});
  std::vector<Contour> polys = {a, b};
  Flat f(polys);
  Covered c = cover_exact(f, rect_of(0, 0, 1, 1));
  ASSERT_EQ(c.result, GSHCPU_OK);
  std::vector<double> ref = reference(polys, 1, 1);
  EXPECT_LE(worst_off(c, ref), 1.5);
  EXPECT_NEAR(c.k[0], 112.0, 1.0) << "the closed form: 1792 2/3 / 4096 of 256";
  EXPECT_NEAR(ref[0], 255.0 * 1792.667 / 4096.0, 1.0) << "and the instrument agrees with it";
}

TEST(RenderExact, EdgesThatMeetAtALatticePointThroughACornerAndCoincidentEdges) {
  // Two squares meeting at a vertex of the lattice, two sharing an edge exactly, two coincident entirely, a triangle whose
  // vertex lies on the other's edge: every one the union's area.
  std::vector<std::vector<Contour>> sets = {
      {box(10, 10, 32, 32), box(32, 32, 54, 54)},
      {box(10, 10, 32, 32), box(32, 10, 54, 32)},
      {box(10, 10, 50, 50), box(10, 10, 50, 50)},
      {box(0, 0, 64, 64), positive({pt(32, 32), pt(80, 0), pt(80, 64)})},
      {box(-20, -20, 30, 30), box(0, 0, 40, 40), box(20, 20, 70, 70)},
      {positive({pt(0, 0), pt(64, 32), pt(0, 64)}), positive({pt(64, 0), pt(0, 32), pt(64, 64)})},
  };
  for (size_t i = 0; i < sets.size(); i++) {
    Flat f(sets[i]);
    Covered c = cover_exact(f, rect_of(0, 0, 1, 1));
    ASSERT_EQ(c.result, GSHCPU_OK) << i;
    std::vector<double> ref = reference(sets[i], 1, 1);
    EXPECT_LE(worst_off(c, ref), 1.5) << "set " << i;
    EXPECT_EQ(c.failures, 0u) << i;
  }
}

TEST(RenderExact, AContourThatTouchesTheLeftSideAndReturnsAVerticalPieceOnItAndAFlatPieceAcrossTheCell) {
  // x = 0 is the pixel's left side: a contour whose vertex lies on it, a piece along it, and flat pieces across the cell
  // (the top of a rectangle) whose end is on it.  The winding at the top of the side is recovered from the cover to the left
  // and checked by its divisibility; the union is the instrument's.
  std::vector<std::vector<Contour>> sets = {
      {positive({pt(-40, 10), pt(0, 10), pt(30, 40), pt(0, 50), pt(-40, 50)}), box(-40, 30, 20, 60)},
      {box(-30, 0, 0, 64), box(0, 0, 30, 64), box(-10, 20, 40, 44)},
      {box(-64, 16, 128, 48), box(-64, 24, 128, 56), box(-64, 8, 128, 40)},
      {box(0, 0, 64, 64), box(-64, -64, 0, 64), box(10, 10, 20, 20)},
      {positive({pt(-20, 20), pt(64, 20), pt(64, 20), pt(-20, 22)}), box(-30, 10, 50, 30)},
  };
  for (size_t i = 0; i < sets.size(); i++) {
    Flat f(sets[i]);
    Covered c = cover_exact(f, rect_of(-1, 0, 3, 1));
    ASSERT_EQ(c.result, GSHCPU_OK) << i;
    std::vector<double> ref = reference(sets[i], 3, 1);
    // the reference is over x from 0; compare the pixels from 0
    double w = 0;
    for (int x = 0; x < 3; x++) {
      w = std::max(w, std::fabs(c.k[(size_t)x + 1] - ref[(size_t)x]));
    }
    EXPECT_LE(w, 1.5) << "set " << i;
    EXPECT_EQ(c.failures, 0u) << "set " << i;
  }
}

TEST(RenderExact, ACellWithNoFragmentsButFlatPiecesIsTheUnionToo) {
  // Two long horizontal strips that overlap partly in height: the pixel between their ends holds no fragment at all, only
  // flat pieces that step the winding at its left side (the cell the count of fragments alone would leave to the sum).
  std::vector<Contour> polys = {box(-200, 10, 400, 40), box(-200, 25, 400, 55)};
  Flat f(polys);
  Covered c = cover_exact(f, rect_of(0, 0, 3, 1));
  ASSERT_EQ(c.result, GSHCPU_OK);
  std::vector<double> ref = reference(polys, 3, 1);
  EXPECT_LE(worst_off(c, ref), 1.5);
  EXPECT_EQ(c.k[1], 180) << "45/64 of 256, not the sum's 60/64";
  EXPECT_EQ(c.failures, 0u);
  // The sparse sweep visits the cell too: nothing deposits there, and the cells after the last deposit are constant to it.
  Covered sparse = cover_sparse(f, rect_of(0, 0, 3, 1));
  ASSERT_EQ(sparse.result, GSHCPU_OK);
  EXPECT_EQ(sparse.k, c.k);
}

/* --- random sets against the instrument ------------------------------------------------------------------------------ */

namespace {

/** A random piece: a rectangle, a triangle or a slanted bar the way an expansion makes them, within the 6 by 6 raster. */
Contour random_piece(std::mt19937 & rng, int64_t lo, int64_t hi) {
  std::uniform_int_distribution<int64_t> c(lo, hi);
  switch (rng() % 3) {
    case 0: {
      int64_t x0 = c(rng), x1 = c(rng), y0 = c(rng), y1 = c(rng);
      if (x0 == x1) x1 += 7;
      if (y0 == y1) y1 += 9;
      return box(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1));
    }
    case 1:
      return positive({pt(c(rng), c(rng)), pt(c(rng), c(rng)), pt(c(rng), c(rng))});
    default: {
      // a bar from p to q of half width h
      int64_t px = c(rng), py = c(rng), qx = c(rng), qy = c(rng);
      if (px == qx && py == qy) qx += 40;
      double dx = (double)(qx - px), dy = (double)(qy - py);
      double len = std::sqrt(dx * dx + dy * dy);
      double h = 6.0 + (double)(rng() % 60) / 8.0;
      int64_t nx = (int64_t)std::llround(-dy / len * h), ny = (int64_t)std::llround(dx / len * h);
      return positive({pt(px + nx, py + ny), pt(qx + nx, qy + ny), pt(qx - nx, qy - ny), pt(px - nx, py - ny)});
    }
  }
}

}  // namespace

TEST(RenderExact, EveryPixelOfRandomSetsOfPiecesIsTheAreaOfTheirUnionAndTheIdentityHolds) {
  std::mt19937 rng(4242);
  double worst = 0;
  long pixels = 0;
  long exact = 0;
  for (int round = 0; round < 120; round++) {
    std::vector<Contour> polys;
    int n = 2 + (int)(rng() % 7);
    for (int i = 0; i < n; i++) {
      polys.push_back(random_piece(rng, -U, 5 * U));
    }
    Flat f(polys);
    Covered c = cover_exact(f, rect_of(0, 0, 4, 4));
    ASSERT_EQ(c.result, GSHCPU_OK) << round;
    ASSERT_EQ(c.failures, 0u) << round;
    std::vector<double> ref = reference(polys, 4, 4);
    double w = worst_off(c, ref);
    EXPECT_LE(w, 1.5) << "round " << round;
    worst = std::max(worst, w);
    pixels += 16;
    exact += (long)c.exact_cells;
  }
  std::printf("exact step vs the instrument: 120 random sets, %ld pixels, %ld made exact, worst %.2f levels\n", pixels, exact, worst);
  EXPECT_GT(exact, 200) << "the sets overlap: the step has work";
}

TEST(RenderExact, NonOverlappingShapesAreTheirAreaToo) {
  // The exact step leaves what does not overlap alone: a lone shape's pixels are its area, as the instrument finds it.
  std::mt19937 rng(7);
  for (int round = 0; round < 60; round++) {
    std::vector<Contour> polys = {random_piece(rng, 0, 4 * U)};
    Flat f(polys);
    Covered c = cover_exact(f, rect_of(0, 0, 4, 4));
    ASSERT_EQ(c.result, GSHCPU_OK);
    std::vector<double> ref = reference(polys, 4, 4);
    EXPECT_LE(worst_off(c, ref), 1.5) << round;
  }
}

TEST(RenderExact, AWindowIsTheWholePixelsThereAndTheLeftCoverComesFromTheFoldColumn) {
  std::mt19937 rng(99);
  for (int round = 0; round < 60; round++) {
    std::vector<Contour> polys;
    int n = 2 + (int)(rng() % 6);
    for (int i = 0; i < n; i++) {
      polys.push_back(random_piece(rng, -U, 6 * U));
    }
    Flat f(polys);
    Covered whole = cover_exact(f, rect_of(0, 0, 7, 7));
    ASSERT_EQ(whole.result, GSHCPU_OK);
    for (int k = 0; k < 6; k++) {
      int64_t x0 = (int64_t)(rng() % 6), y0 = (int64_t)(rng() % 6);
      int64_t x1 = x0 + 1 + (int64_t)(rng() % (uint32_t)(7 - x0)), y1 = y0 + 1 + (int64_t)(rng() % (uint32_t)(7 - y0));
      Covered w = cover_exact(f, rect_of(x0, y0, x1, y1), rect_of(0, 0, 7, 7));
      ASSERT_EQ(w.result, GSHCPU_OK);
      for (int64_t y = y0; y < y1; y++) {
        for (int64_t x = x0; x < x1; x++) {
          ASSERT_EQ(w.k[(size_t)((y - y0) * (x1 - x0) + (x - x0))], whole.k[(size_t)(y * 7 + x)])
              << "round " << round << " window " << x0 << "," << y0 << "," << x1 << "," << y1 << " pixel " << x << "," << y;
        }
      }
    }
  }
}

TEST(RenderExact, TheOrderOfThePiecesAndTheStartOfEachContourChangeNothing) {
  std::mt19937 rng(5);
  for (int round = 0; round < 40; round++) {
    std::vector<Contour> polys;
    int n = 3 + (int)(rng() % 5);
    for (int i = 0; i < n; i++) {
      polys.push_back(random_piece(rng, -U, 5 * U));
    }
    Flat first(polys);
    Covered a = cover_exact(first, rect_of(0, 0, 4, 4));
    std::vector<Contour> shuffled = polys;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    for (Contour & c : shuffled) {
      std::rotate(c.begin(), c.begin() + (long)(rng() % c.size()), c.end());
    }
    Flat second(shuffled);
    Covered b = cover_exact(second, rect_of(0, 0, 4, 4));
    ASSERT_EQ(a.result, GSHCPU_OK);
    EXPECT_EQ(a.k, b.k) << round;
  }
}

TEST(RenderExact, TheSparseSweepIsTheDenseOneEveryPixel) {
  std::mt19937 rng(31);
  for (int round = 0; round < 60; round++) {
    std::vector<Contour> polys;
    int n = 2 + (int)(rng() % 5);
    for (int i = 0; i < n; i++) {
      polys.push_back(random_piece(rng, -U, 6 * U));
    }
    Flat f(polys);
    Covered dense = cover_exact(f, rect_of(0, 0, 7, 7));
    Covered sparse = cover_sparse(f, rect_of(0, 0, 7, 7));
    ASSERT_EQ(dense.result, GSHCPU_OK);
    ASSERT_EQ(sparse.result, GSHCPU_OK);
    EXPECT_EQ(dense.k, sparse.k) << round;
    EXPECT_EQ(sparse.failures, 0u);
  }
}

/* --- UC8: the three parts of an aligned stroke ------------------------------------------------------------------------- */

namespace {

struct Aligned {
  GSHCPU_Result result = GSHCPU_OK;
  std::vector<uint8_t> k;
  uint64_t failures = 0;
  uint64_t exact_cells = 0;
};

/** The coverage of the region body U (aligned & clip) over @p rect, the parts as polygons (an empty list is an absent part). */
Aligned cover_aligned(const std::vector<Contour> & body, const std::vector<Contour> & aligned, const std::vector<Contour> & clip,
    GSHGEOM_Fill_Rule clip_rule, bool complement, GSHCPU_Rect rect, GSHCPU_Rect walk, uint64_t cap = 512) {
  Aligned out;
  const std::vector<Contour> * polys[3] = {&body, &aligned, &clip};
  std::unique_ptr<Flat> flats[3];
  size_t w = (size_t)(rect.x1 - rect.x0);
  size_t h = (size_t)(rect.y1 - rect.y0);
  size_t wide = (size_t)(walk.x1 - walk.x0);
  std::vector<int64_t> acc[3];
  std::vector<uint8_t> parts[3];
  std::vector<unsigned char> raw[3];
  GSHCPU_Exact ex[3];
  GSHCPU_Aligned_Input in[3];
  for (int p = 0; p < 3; p++) {
    if (!polys[p]->empty()) {
      flats[p].reset(new Flat(*polys[p]));
    }
    acc[p].assign(2 * (w + 1) * GSHCPU_RENDER_BAND_ROWS, 31);
    parts[p].assign(w * h, 0xee);
    raw[p].resize(gshcpu_exact_bytes(wide, cap, p == 0 ? 3 : 0) + 16);
    uintptr_t at = ((uintptr_t)raw[p].data() + 15u) & ~(uintptr_t)15u;
    gshcpu_exact_init(&ex[p], (void *)at, wide, cap, p == 0 ? 3 : 0);
    ex[p].eps = GSHCPU_RENDER_EXACT_EPS;
    in[p].path = flats[p] ? flats[p]->flat : nullptr;
    in[p].walk = walk;
    in[p].rule = p == 2 ? clip_rule : GSHGEOM_FILL_NONZERO;
    in[p].acc = acc[p].data();
    in[p].out = parts[p].data();
    in[p].ex = &ex[p];
  }
  out.k.assign(w * h, 0xee);
  uint64_t cells = 0;
  out.result = gshcpu_render_cover_aligned(in, rect, complement, out.k.data(), UINT64_MAX, &cells);
  out.failures = ex[0].failures;
  out.exact_cells = ex[0].cells;
  return out;
}

std::vector<double> reference3(const std::vector<Contour> & body, const std::vector<Contour> & aligned,
    const std::vector<Contour> & clip, int rule, bool complement, int w, int h) {
  stroke_gate::Part b;
  stroke_gate::Part a;
  stroke_gate::Part c;
  for (const Contour & q : body) b.contours.push_back(stroke_gate::cut_contour(q));
  for (const Contour & q : aligned) a.contours.push_back(stroke_gate::cut_contour(q));
  for (const Contour & q : clip) c.contours.push_back(stroke_gate::cut_contour(q));
  c.rule = rule;
  c.complement = complement;
  std::vector<double> area = stroke_gate::union_area(b, aligned.empty() ? nullptr : &a, aligned.empty() ? nullptr : &c, w, h, 768);
  for (double & v : area) {
    v *= 255.0;
  }
  return area;
}

}  // namespace

TEST(RenderExactAligned, TheRegionOfThreePartsIsTheAreaOfBodyUnionAlignedAndClipExactlyInEveryPixel) {
  std::mt19937 rng(77);
  double worst = 0;
  long shared = 0;
  for (int round = 0; round < 100; round++) {
    std::vector<Contour> body;
    std::vector<Contour> aligned;
    std::vector<Contour> clip;
    int nb = (int)(rng() % 4);  // the body may be absent
    for (int i = 0; i < nb; i++) body.push_back(random_piece(rng, -U, 5 * U));
    int na = 1 + (int)(rng() % 4);
    for (int i = 0; i < na; i++) aligned.push_back(random_piece(rng, -U, 5 * U));
    // The clip is the fill region of the closed subpaths: it may overlap itself (a star's), and its orientation is the
    // path's, either sign.
    int nc = 1 + (int)(rng() % 3);
    for (int i = 0; i < nc; i++) {
      Contour q = random_piece(rng, -U, 5 * U);
      if (rng() % 3 == 0) std::reverse(q.begin(), q.end());
      clip.push_back(q);
    }
    bool even_odd = rng() % 2;
    bool complement = rng() % 2;
    Aligned c = cover_aligned(body, aligned, clip, even_odd ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, complement,
        rect_of(0, 0, 4, 4), rect_of(0, 0, 4, 4));
    ASSERT_EQ(c.result, GSHCPU_OK) << round;
    ASSERT_EQ(c.failures, 0u) << round;
    std::vector<double> ref = reference3(body, aligned, clip, even_odd ? 1 : 0, complement, 4, 4);
    double w = 0;
    for (size_t i = 0; i < ref.size(); i++) {
      w = std::max(w, std::fabs(c.k[i] - ref[i]));
    }
    EXPECT_LE(w, 1.5) << "round " << round << (even_odd ? " even-odd" : " nonzero") << (complement ? " complement" : "");
    worst = std::max(worst, w);
    shared += (long)c.exact_cells;
  }
  std::printf("exact step, three parts, vs the instrument: 100 random sets, %ld cells made exact, worst %.2f levels\n", shared, worst);
  EXPECT_GT(shared, 150);
}

TEST(RenderExactAligned, AWindowIsTheWholeRendersPixelsThere) {
  std::mt19937 rng(78);
  for (int round = 0; round < 30; round++) {
    std::vector<Contour> body = {random_piece(rng, -U, 6 * U), random_piece(rng, -U, 6 * U)};
    std::vector<Contour> aligned = {random_piece(rng, -U, 6 * U), random_piece(rng, -U, 6 * U)};
    std::vector<Contour> clip = {random_piece(rng, -U, 6 * U), random_piece(rng, -U, 6 * U)};
    bool complement = rng() % 2;
    Aligned whole = cover_aligned(body, aligned, clip, GSHGEOM_FILL_NONZERO, complement, rect_of(0, 0, 7, 7), rect_of(0, 0, 7, 7));
    ASSERT_EQ(whole.result, GSHCPU_OK);
    for (int k = 0; k < 4; k++) {
      int64_t x0 = (int64_t)(rng() % 6), y0 = (int64_t)(rng() % 6);
      int64_t x1 = x0 + 1 + (int64_t)(rng() % (uint32_t)(7 - x0)), y1 = y0 + 1 + (int64_t)(rng() % (uint32_t)(7 - y0));
      Aligned w = cover_aligned(body, aligned, clip, GSHGEOM_FILL_NONZERO, complement, rect_of(x0, y0, x1, y1), rect_of(0, 0, 7, 7));
      ASSERT_EQ(w.result, GSHCPU_OK);
      for (int64_t y = y0; y < y1; y++) {
        for (int64_t x = x0; x < x1; x++) {
          ASSERT_EQ(w.k[(size_t)((y - y0) * (x1 - x0) + (x - x0))], whole.k[(size_t)(y * 7 + x)])
              << "round " << round << " window " << x0 << "," << y0 << "," << x1 << "," << y1 << " pixel " << x << "," << y;
        }
      }
    }
  }
}

TEST(RenderExactAligned, ACellWithPiecesOfOnePartAloneIsSC5sFormulaWithConstants) {
  // The clip covers the pixel wholly, the aligned part partly (a half), the body not at all: a = 128, c = 255, b = 0 and the
  // formula's k = a; the same with the body wholly covering: 255.
  std::vector<Contour> aligned = {box(-100, -100, 32, 164)};
  std::vector<Contour> clip = {box(-200, -200, 300, 300)};
  Aligned only = cover_aligned({}, aligned, clip, GSHGEOM_FILL_NONZERO, false, rect_of(0, 0, 1, 1), rect_of(0, 0, 1, 1));
  ASSERT_EQ(only.result, GSHCPU_OK);
  EXPECT_EQ(only.k[0], 128);
  Aligned outside = cover_aligned({}, aligned, clip, GSHGEOM_FILL_NONZERO, true, rect_of(0, 0, 1, 1), rect_of(0, 0, 1, 1));
  ASSERT_EQ(outside.result, GSHCPU_OK);
  EXPECT_EQ(outside.k[0], 0) << "the complement of a clip that covers the pixel";
  Aligned covered = cover_aligned({box(-200, -200, 300, 300)}, aligned, clip, GSHGEOM_FILL_NONZERO, false, rect_of(0, 0, 1, 1),
      rect_of(0, 0, 1, 1));
  ASSERT_EQ(covered.result, GSHCPU_OK);
  EXPECT_EQ(covered.k[0], 255);
}

TEST(RenderExactAligned, TheOffCentreStripSC5sFormulaGetsWrongIsTheAreaOfTheIntersection) {
  // The story's case, in a pixel: the aligned band R_c = [0.1, 0.5] (x) lies along the edge of the fill F = x < 0.3 (the
  // clip), so R_c & F = [0.1, 0.3], 0.2 of the pixel, while the band covers 0.4, the clip 0.3 and the formula's independent
  // product says 0.12.  The pixel holds pieces of both parts.
  std::vector<Contour> aligned = {box(6, -100, 32, 164)};   // x in [0.094, 0.5] pixels
  std::vector<Contour> clip = {box(-100, -100, 19, 164)};   // x < 0.297
  Aligned c = cover_aligned({}, aligned, clip, GSHGEOM_FILL_NONZERO, false, rect_of(0, 0, 1, 1), rect_of(0, 0, 1, 1));
  ASSERT_EQ(c.result, GSHCPU_OK);
  EXPECT_EQ(c.k[0], 52) << "13/64 of 256: the area of the intersection, [6, 19] of 64";
  // SC5's formula on the three coverages: the band 26/64, the clip 19/64
  uint32_t b = 0;
  uint32_t a = 104;      // 26/64 of 256
  uint32_t cc = 76;      // 19/64 of 256
  EXPECT_LT(gshcpu_render_stroke_compose(b, a, cc, 0), 40u) << "the product is low";
}

TEST(RenderExact, AnExactCellRefusesPiecesThatDoNotReproduceTheRasterizersSumsAndNeverGuesses) {
  // Two fragments x = 30 and x = 40 down the whole cell, their left edges outside it: the cover to the left is -128 (two edges
  // going up, 64 each), the dense doubled area 2 (2 * 30 * 64 + 10 * 64) = 8960 and the shared 2 * 30 * 64 = 3840.
  GSHCPU_Piece pieces[2] = {{30, 0, 30, 64}, {40, 0, 40, 64}};
  GSHCPU_Exact_Params params;
  std::memset(&params, 0, sizeof params);
  params.parts = 1;
  params.mode = GSHCPU_EXACT_OVERLAP;
  params.eps[0] = GSHCPU_RENDER_EXACT_EPS;
  std::vector<unsigned char> memory(gshcpu_exact_scratch_bytes(8, 1) + 16);
  GSHCPU_Exact_Scratch scratch;
  gshcpu_exact_scratch_init(&scratch, (void *)(((uintptr_t)memory.data() + 15u) & ~(uintptr_t)15u), 8, 1);
  auto run = [&](int64_t a_left, int64_t dense, int64_t * out) {
    GSHCPU_Exact_Cell cell;
    std::memset(&cell, 0, sizeof cell);
    cell.pieces[0] = pieces;
    cell.count[0] = 2;
    cell.a_left[0] = a_left;
    cell.dense[0] = dense;
    uint64_t work = 0;
    return gshcpu_exact_cell(&params, &scratch, &cell, out, &work);
  };
  int64_t out = -1;
  EXPECT_EQ(run(-128, 8960, &out), GSHCPU_EXACT_OK);
  EXPECT_EQ(out, 3840);
  EXPECT_EQ(run(-127, 8960, &out), GSHCPU_EXACT_IDENTITY) << "a cover that no winding at the top of the side explains";
  EXPECT_EQ(run(-192, 8960, &out), GSHCPU_EXACT_IDENTITY) << "a winding of 3 gives another area";
  EXPECT_EQ(run(-128, 9060, &out), GSHCPU_EXACT_IDENTITY) << "an area the pieces do not make";
  EXPECT_EQ(run(-128, 8860, &out), GSHCPU_EXACT_IDENTITY);
  EXPECT_EQ(run(-64 * 256, 8960, &out), GSHCPU_EXACT_IDENTITY) << "the cover of a winding of 256 is not the area of these pieces";
  // The cover and the dense area that a winding of 256 at the top of the side gives these pieces: drawn, no winding is a limit (UC20).
  int64_t deep_left = -64 * 256;
  int64_t deep_dense = -(128 * (deep_left + 128) - 8960);
  EXPECT_EQ(run(deep_left, deep_dense, &out), GSHCPU_EXACT_OK) << "a winding of 256";
  EXPECT_EQ(out, deep_dense - 8192) << "every part of the cell is wound: the union is the cell";
  pieces[1].x1 = 65;
  EXPECT_EQ(run(-128, 8960, &out), GSHCPU_EXACT_IDENTITY) << "a piece that is not in the cell";
  pieces[1].x1 = 40;
  GSHCPU_Piece vertex[2] = {{30, 10, 30, 10}, {40, 0, 40, 64}};
  GSHCPU_Exact_Cell cell;
  std::memset(&cell, 0, sizeof cell);
  cell.pieces[0] = vertex;
  cell.count[0] = 2;
  cell.a_left[0] = -128;
  cell.dense[0] = 8960;
  uint64_t work = 0;
  EXPECT_EQ(gshcpu_exact_cell(&params, &scratch, &cell, &out, &work), GSHCPU_EXACT_IDENTITY) << "a piece of no length";
}

TEST(RenderExact, APieceAndItsReverseCancelAndAreNotWalked) {
  // The cell of the test above with a seam added: a segment two pieces of an outline share, once in each direction, wherever
  // it lies.  Both wind nothing (the cover and the area of the pair are zero), the result is the same, and the walk of the
  // cell with the seam does no more than the walk of the cell without it.
  GSHCPU_Exact_Params params;
  std::memset(&params, 0, sizeof params);
  params.parts = 1;
  params.mode = GSHCPU_EXACT_OVERLAP;
  params.eps[0] = GSHCPU_RENDER_EXACT_EPS;
  std::vector<unsigned char> memory(gshcpu_exact_scratch_bytes(16, 1) + 16);
  GSHCPU_Exact_Scratch scratch;
  gshcpu_exact_scratch_init(&scratch, (void *)(((uintptr_t)memory.data() + 15u) & ~(uintptr_t)15u), 16, 1);
  auto run = [&](const std::vector<GSHCPU_Piece> & pieces, int64_t * out, uint64_t * work) {
    GSHCPU_Exact_Cell cell;
    std::memset(&cell, 0, sizeof cell);
    cell.pieces[0] = pieces.data();
    cell.count[0] = pieces.size();
    cell.a_left[0] = -128;
    cell.dense[0] = 8960;
    *work = 0;
    return gshcpu_exact_cell(&params, &scratch, &cell, out, work);
  };
  int64_t bare = -1;
  int64_t seamed = -1;
  uint64_t bare_work = 0;
  uint64_t seamed_work = 0;
  std::vector<GSHCPU_Piece> pieces = {{30, 0, 30, 64}, {40, 0, 40, 64}};
  ASSERT_EQ(run(pieces, &bare, &bare_work), GSHCPU_EXACT_OK);
  pieces.push_back({10, 5, 22, 60});
  pieces.push_back({22, 60, 10, 5});
  pieces.push_back({0, 20, 64, 20});  // a flat piece across the cell, and its reverse
  pieces.push_back({64, 20, 0, 20});
  ASSERT_EQ(run(pieces, &seamed, &seamed_work), GSHCPU_EXACT_OK);
  EXPECT_EQ(seamed, bare);
  EXPECT_EQ(bare, 3840);
  EXPECT_LE(seamed_work, bare_work + 6u + 64u) << "the pairs cost a visit each and no walk";
  // Two of a kind do not cancel: the same segment the same way twice is a winding of 2 there.
  std::vector<GSHCPU_Piece> twice = {{30, 0, 30, 64}, {30, 0, 30, 64}};
  GSHCPU_Exact_Cell cell;
  std::memset(&cell, 0, sizeof cell);
  cell.pieces[0] = twice.data();
  cell.count[0] = 2;
  cell.a_left[0] = -128;
  cell.dense[0] = 2 * 2 * 30 * 64;
  uint64_t work = 0;
  int64_t out = -1;
  ASSERT_EQ(gshcpu_exact_cell(&params, &scratch, &cell, &out, &work), GSHCPU_EXACT_OK);
  EXPECT_EQ(out, 2 * 30 * 64) << "coincident pieces of one direction keep: a winding of 2 over 30 of 64, shared once";
}

TEST(RenderExact, TheCellsOfABandNeedMoreThanOneWindowOfThePoolAndThePoolsSizeChangesNothing) {
  // 80 bars across a band of 48 pixels: far more pieces in the band's exact cells than the smallest pool (4096) holds.
  std::mt19937 rng(2026);
  std::vector<Contour> polys;
  for (int i = 0; i < 80; i++) {
    polys.push_back(random_piece(rng, -U, 48 * U));
  }
  Flat f(polys);
  Covered small = cover_exact(f, rect_of(0, 0, 48, 14), 512);   // a pool of 4096 pieces
  Covered large = cover_exact(f, rect_of(0, 0, 48, 14), 8192);  // a pool of 8192
  ASSERT_EQ(small.result, GSHCPU_OK);
  ASSERT_EQ(large.result, GSHCPU_OK);
  EXPECT_EQ(small.k, large.k) << "the windows change nothing";
  EXPECT_EQ(small.failures + large.failures, 0u);
  EXPECT_GT(small.exact_cells, 100u);
  std::vector<double> ref = reference(polys, 48, 14);
  EXPECT_LE(worst_off(small, ref), 1.5);
}

namespace {

/** Sets the test switch that turns off the cover's shortcuts (@p mask, render_internal.h), and puts it back. */
struct ShortcutsOff {
  explicit ShortcutsOff(unsigned mask) { gshcpu_cover_shortcuts_off = mask; }
  ~ShortcutsOff() { gshcpu_cover_shortcuts_off = 0; }
};

}  // namespace

TEST(RenderExact, TheWindowsKeepThePiecesTheFirstPassWroteDownAndChangeNoPixelNoCountAndNoWorkFigure) {
  // The windows read the pieces the first pass wrote down (the second pass's pieces in the second pass's order): every result,
  // count and work figure must be the one with the second pass cutting the edges again.  Random sets
  // over rectangles of one band or several, walks as wide as the rectangle and wider, cells capped small enough that the log
  // overflows (the windows then cut the edges) and large enough that it does not.
  std::mt19937 rng(8801);
  uint64_t replayed = 0;
  uint64_t cut_again = 0;
  uint64_t exact = 0;
  for (int round = 0; round < 90; round++) {
    std::vector<Contour> polys;
    int n = 3 + (int)(rng() % 40);
    int64_t w = 6 + (int64_t)(rng() % 26);
    int64_t h = 3 + (int64_t)(rng() % 40);
    for (int i = 0; i < n; i++) {
      polys.push_back(random_piece(rng, -U, w * U));
    }
    Flat f(polys);
    GSHCPU_Rect rect = rect_of(0, 0, w, h);
    GSHCPU_Rect walk = round % 3 == 0 ? rect : rect_of(-(int64_t)(rng() % 3), 0, w + (int64_t)(rng() % 4), h);
    static const uint64_t caps[4] = {24, 64, 512, 8192};
    uint64_t cap = caps[round % 4];
    for (int sparse = 0; sparse < 2; sparse++) {
      Covered off;
      {
        ShortcutsOff guard(GSHCPU_COVER_NO_REPLAY);
        off = sparse ? cover_sparse(f, rect, cap) : cover_exact(f, rect, walk, cap);
      }
      EXPECT_EQ(off.replays, 0u) << "the switch turns the log off";
      for (unsigned mask = 0; mask < 2; mask++) {
        Covered on;
        {
          ShortcutsOff guard(mask);
          on = sparse ? cover_sparse(f, rect, cap) : cover_exact(f, rect, walk, cap);
        }
        ASSERT_EQ(on.result, off.result) << round << " mask " << mask;
        EXPECT_EQ(on.cells, off.cells) << round << " mask " << mask;
        EXPECT_EQ(on.failures, off.failures) << round << " mask " << mask;
        EXPECT_EQ(on.exact_cells, off.exact_cells) << round << " mask " << mask;
        EXPECT_EQ(on.work, off.work) << round << " mask " << mask;
        if (on.result == GSHCPU_OK) {
          EXPECT_EQ(on.k, off.k) << "round " << round << (sparse ? " sparse" : " dense") << " mask " << mask;
          EXPECT_EQ(on.failures, 0u);
        }
        if (mask == 0) {
          replayed += on.replays;
          cut_again += on.replays == 0 && on.exact_cells > 0;
          exact += on.exact_cells;
        } else if (mask & GSHCPU_COVER_NO_REPLAY) {
          EXPECT_EQ(on.replays, 0u);
        }
      }
    }
  }
  EXPECT_GT(replayed, 50u) << "the log is used";
  EXPECT_GT(cut_again, 0u) << "and a band with more pieces than the scratch holds is not";
  EXPECT_GT(exact, 500u);
}

/* --- UC9: the cap, the winding bound ---------------------------------------------------------------------------------- */

namespace {

/** @p n thin strips, each wholly inside the pixel (two fragments), overlapping a few deep: 2n fragments in the pixel. */
std::vector<Contour> strips(int n) {
  std::vector<Contour> out;
  for (int i = 0; i < n; i++) {
    int64_t x = (i % 30) * 2;
    out.push_back(box(x, -100, x + 3, 200));
  }
  return out;
}

}  // namespace

TEST(RenderExact, ACellOfTheCapIsExactAndOnePastItIsTheLimitWithTheFieldRaisedAndLoweredAndTheDefault) {
  std::vector<Contour> polys = strips(256);  // 512 fragments
  Flat f(polys);
  Covered at = cover_exact(f, rect_of(0, 0, 1, 1), 512);
  ASSERT_EQ(at.result, GSHCPU_OK) << "512 fragments at the default";
  std::vector<double> ref = reference(polys, 1, 1);
  EXPECT_LE(worst_off(at, ref), 1.5);
  EXPECT_EQ(at.failures, 0u);
  // one more fragment: a rectangle whose left edge lies outside and right edge crosses
  polys.push_back(box(-100, -100, 5, 200));
  Flat g(polys);
  Covered past = cover_exact(g, rect_of(0, 0, 1, 1), 512);
  EXPECT_EQ(past.result, GSHCPU_ERR_LIMIT) << "513 fragments at 512";
  Covered raised = cover_exact(g, rect_of(0, 0, 1, 1), 513);
  EXPECT_EQ(raised.result, GSHCPU_OK) << "the field raised makes the cell exact";
  Covered high = cover_exact(g, rect_of(0, 0, 1, 1), 4096);
  EXPECT_EQ(high.result, GSHCPU_OK);
  EXPECT_EQ(raised.k, high.k);
  std::vector<double> ref2 = reference(polys, 1, 1);
  EXPECT_LE(worst_off(raised, ref2), 1.5);
  // the same at small values, each exactly at the bound
  for (uint64_t cap : {1u, 2u, 7u, 40u}) {
    std::vector<Contour> s = strips((int)(cap / 2));
    if (cap % 2) {
      s.push_back(box(-100, -100, 5, 200));
    }
    if (s.empty()) {
      continue;
    }
    Flat sf(s);
    EXPECT_EQ(cover_exact(sf, rect_of(0, 0, 1, 1), cap).result, GSHCPU_OK) << cap;
    s.push_back(box(-100, -100, 9, 200));
    Flat sg(s);
    EXPECT_EQ(cover_exact(sg, rect_of(0, 0, 1, 1), cap).result, GSHCPU_ERR_LIMIT) << cap;
  }
}

TEST(RenderExact, TheCapIsMetOverWhatTheWalkHoldsAndNotWhatTheWindowDoes) {
  // The overfull cell lies outside the window: the refusal is the whole render's when the walk is the whole.
  std::vector<Contour> polys = strips(8);
  for (Contour & c : polys) {
    for (Pt & p : c) {
      p.x += 4 * U; // in the pixel at x = 4
    }
  }
  Flat f(polys);
  GSHCPU_Rect walk = rect_of(0, 0, 6, 1);
  EXPECT_EQ(cover_exact(f, rect_of(0, 0, 6, 1), walk, 8).result, GSHCPU_ERR_LIMIT);
  EXPECT_EQ(cover_exact(f, rect_of(0, 0, 2, 1), walk, 8).result, GSHCPU_ERR_LIMIT) << "a window left of it: the walk holds it";
  EXPECT_EQ(cover_exact(f, rect_of(5, 0, 6, 1), walk, 8).result, GSHCPU_ERR_LIMIT) << "and a window right of it";
  EXPECT_EQ(cover_exact(f, rect_of(0, 0, 6, 1), walk, 16).result, GSHCPU_OK);
}

TEST(RenderExact, NoWindingIsALimitAndACellOfAnyDepthIsExact) {
  // Coincident rectangles, as deep as stroked text goes and past it: a winding of 255, 256, 475, 1024 and 1025 is exact (UC20;
  // the step of S4b refused one past 255).
  for (int n : {255, 256, 475, 1024, 1025}) {
    std::vector<Contour> polys;
    for (int i = 0; i < n; i++) {
      polys.push_back(box(-100, -100, 30, 200));
    }
    Flat f(polys);
    Covered c = cover_exact(f, rect_of(0, 0, 1, 1), 2048);
    ASSERT_EQ(c.result, GSHCPU_OK) << n;
    EXPECT_EQ(c.k[0], 120) << "30/64 of 256: the union of coincident rectangles, " << n << " deep";
  }
}

TEST(RenderExact, ADenseCellOfCrossingFragmentsIsExactAndItsWorkIsCounted) {
  // 24 bars through the pixel at 24 different angles: many crossings, so many sub-slabs; the union is the instrument's.
  std::vector<Contour> polys;
  for (int i = 0; i < 24; i++) {
    double a = 3.14159265358979 * i / 24.0;
    int64_t cx = 32, cy = 32;
    int64_t dx = (int64_t)std::llround(std::cos(a) * 90), dy = (int64_t)std::llround(std::sin(a) * 90);
    int64_t nx = (int64_t)std::llround(-std::sin(a) * 2.5), ny = (int64_t)std::llround(std::cos(a) * 2.5);
    polys.push_back(positive({pt(cx - dx + nx, cy - dy + ny), pt(cx + dx + nx, cy + dy + ny), pt(cx + dx - nx, cy + dy - ny),
        pt(cx - dx - nx, cy - dy - ny)}));
  }
  Flat f(polys);
  Covered c = cover_exact(f, rect_of(0, 0, 1, 1));
  ASSERT_EQ(c.result, GSHCPU_OK);
  std::vector<double> ref = reference(polys, 1, 1);
  EXPECT_LE(worst_off(c, ref), 1.5);
  EXPECT_GT(c.work, 24u * 24u) << "the pair comparisons are counted";
  EXPECT_EQ(c.failures, 0u);
}

/* --- the sink: the field, the refusal, caller memory ------------------------------------------------------------------- */

namespace {

/** A comb of @p teeth thin strokes inside the pixel (2, 2): many pieces of one stroke in one pixel. */
Ops comb(int teeth) {
  Ops ops;
  ops.push_back({'M', {2.05, 2.1}});
  for (int i = 0; i < teeth; i++) {
    double y = 2.1 + 0.8 * (i + 1) / teeth;
    ops.push_back({'L', {i % 2 ? 2.05 : 2.9, y - 0.4 / teeth}});
    ops.push_back({'L', {i % 2 ? 2.05 : 2.9, y}});
  }
  return ops;
}

Params comb_style() {
  Params p;
  p.s.width = 0.07;
  p.s.join = GSHGEOM_STROKE_JOIN_MITER;
  p.s.cap = GSHGEOM_STROKE_CAP_SQUARE;
  return p;
}

}  // namespace

TEST(RenderExactSink, TheFieldIsValidatedDefaultedAndTheLeastValueThatDrawsIsTheCellsPieces) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_exact_fragments = GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS + 1;
  GSHSCENE_Sink * bad = nullptr;
  EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &bad), GSHCPU_ERR_INVALID) << "past 2^20: refused at creation";
  EXPECT_EQ(bad, nullptr);
  limits.max_exact_fragments = GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS;
  GSHSCENE_Sink * top = nullptr;
  ASSERT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &top), GSHCPU_OK) << "2^20 is accepted";
  EXPECT_EQ(gshscene_sink_destroy(top), GSHSCENE_OK);

  Ops teeth = comb(10);
  Params p = comb_style();
  Store s;
  const GSHSCENE_List * l = one_stroke(s, teeth, p.get(), translation(0.0, 0.0));
  auto draw = [&](uint64_t field, const GSHSCENE_Rect * damage = nullptr, uint32_t flags = 0) {
    GSHCPU_Limits lim;
    gshcpu_limits_default(&lim);
    lim.max_exact_fragments = field;
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &lim);
    return render_list(sink.sink, l, 6, 6, GSHCPU_FORMAT_RGBA8_STRAIGHT, flags, nullptr, damage);
  };
  Drawn def = draw(0);
  ASSERT_TRUE(def.entries.empty()) << "the default draws it";
  EXPECT_EQ(draw(GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS).alpha, def.alpha) << "zero is the default, 32768";
  // the least field that draws: binary search (a cell's pieces: no more and no less)
  uint64_t lo = 1;
  uint64_t hi = 512;
  while (lo < hi) {
    uint64_t mid = lo + (hi - lo) / 2;
    (draw(mid).entries.empty() ? hi : lo) = draw(mid).entries.empty() ? mid : mid + 1;
  }
  EXPECT_GT(lo, 8u) << "a stroke of many pieces in one pixel";
  Drawn at = draw(lo);
  EXPECT_TRUE(at.entries.empty());
  EXPECT_EQ(at.alpha, def.alpha) << "exact at the bound";
  Drawn under = draw(lo - 1);
  ASSERT_EQ(under.entries.size(), 1u) << "one past it is refused";
  EXPECT_EQ(under.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(under.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  for (uint8_t a : under.alpha) {
    EXPECT_EQ(a, 0) << "the block is skipped";
  }
  // strict: stops with the finished prefix
  Drawn strict = draw(lo - 1, nullptr, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
  // a damage that does not hold the overfull pixel refuses what the whole render refuses; one that holds it too
  GSHSCENE_Rect away = {0, 4, 6, 6};
  Drawn damaged = draw(lo - 1, &away);
  ASSERT_EQ(damaged.entries.size(), 1u);
  EXPECT_EQ(damaged.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  Drawn damaged_ok = draw(lo, &away);
  EXPECT_TRUE(damaged_ok.entries.empty());
  GSHSCENE_Rect around = {1, 1, 4, 4};
  Drawn inside = draw(lo, &around);
  for (int y = 1; y < 4; y++) {
    for (int x = 1; x < 4; x++) {
      EXPECT_EQ(inside.alpha[(size_t)(y * 6 + x)], def.alpha[(size_t)(y * 6 + x)]) << x << "," << y;
    }
  }
  // 2^20 and beyond the default draw the same
  EXPECT_EQ(draw(GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS).alpha, def.alpha);
}

TEST(RenderExactSink, TheCrossingsBudgetIsValidatedDefaultedAndTheLeastValueThatDrawsIsTheCellsMeetings) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  GSHSCENE_Sink * bad = nullptr;
  for (uint64_t v : {GSHCPU_LIMITS_MAX_EXACT_CROSSINGS + 1, GSHCPU_LIMITS_MAX_EXACT_CROSSINGS * 2, (uint64_t)UINT64_MAX}) {
    limits.max_exact_crossings = v;
    EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &bad), GSHCPU_ERR_INVALID) << v << ": past 2^32 is refused at creation";
    EXPECT_EQ(bad, nullptr);
  }
  limits.max_exact_crossings = GSHCPU_LIMITS_MAX_EXACT_CROSSINGS;
  GSHSCENE_Sink * top = nullptr;
  ASSERT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &top), GSHCPU_OK) << "2^32 is accepted";
  EXPECT_EQ(gshscene_sink_destroy(top), GSHSCENE_OK);

  Ops teeth = comb(10);
  Params p = comb_style();
  Store s;
  const GSHSCENE_List * l = one_stroke(s, teeth, p.get(), translation(0.0, 0.0));
  GSHCPU_Render_Counters seen{};
  auto draw = [&](uint64_t budget, const GSHSCENE_Rect * damage = nullptr, uint32_t flags = 0) {
    GSHCPU_Limits lim;
    gshcpu_limits_default(&lim);
    lim.max_exact_crossings = budget;
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &lim);
    Drawn d = render_list(sink.sink, l, 6, 6, GSHCPU_FORMAT_RGBA8_STRAIGHT, flags, nullptr, damage);
    seen = sink.counters;
    return d;
  };
  Drawn def = draw(0);
  ASSERT_TRUE(def.entries.empty()) << "the default draws it";
  uint64_t most = seen.exact_crossings_max;
  ASSERT_GT(most, 3u) << "the stroke's densest pixel makes crossings";
  EXPECT_EQ(draw(GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS).alpha, def.alpha) << "zero is 2^20";
  Drawn at = draw(most);
  EXPECT_TRUE(at.entries.empty()) << "a pixel with exactly the budget's crossings draws";
  EXPECT_EQ(at.alpha, def.alpha);
  Drawn under = draw(most - 1);
  ASSERT_EQ(under.entries.size(), 1u) << "one more is refused";
  EXPECT_EQ(under.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(under.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  for (uint8_t a : under.alpha) {
    EXPECT_EQ(a, 0) << "the block is skipped";
  }
  EXPECT_EQ(draw(1).entries.size(), 1u) << "a budget of 1";
  EXPECT_EQ(draw(2).entries.size(), 1u);
  EXPECT_EQ(draw(most - 1, nullptr, GSHSCENE_RENDER_STRICT).result, GSHSCENE_ERR_REFUSED) << "strict stops with the prefix";
  // The refusal is the whole render's: a damage that does not hold the pixel refuses what the whole render refuses (the pixel
  // is counted though it is not drawn), and one that holds it draws exactly what the whole render draws.
  GSHSCENE_Rect away = {0, 4, 6, 6};
  Drawn damaged = draw(most - 1, &away);
  ASSERT_EQ(damaged.entries.size(), 1u);
  EXPECT_EQ(damaged.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_TRUE(draw(most, &away).entries.empty());
  GSHSCENE_Rect left_of_it = {0, 0, 2, 6};
  EXPECT_EQ(draw(most - 1, &left_of_it).entries.size(), 1u) << "a damage left of the pixel";
  GSHSCENE_Rect right_of_it = {3, 0, 6, 6};
  EXPECT_EQ(draw(most - 1, &right_of_it).entries.size(), 1u) << "a damage right of it";
  GSHSCENE_Rect around = {1, 1, 4, 4};
  Drawn inside = draw(most, &around);
  for (int y = 1; y < 4; y++) {
    for (int x = 1; x < 4; x++) {
      EXPECT_EQ(inside.alpha[(size_t)(y * 6 + x)], def.alpha[(size_t)(y * 6 + x)]) << x << "," << y;
    }
  }
}

TEST(RenderExactSink, TheBudgetCountsAnAlignedStrokesPixelsTogetherOutsideTheDamageToo) {
  // An inside stroke of a closed comb: the body, the aligned part and the clip share the pixels of the teeth, so UC8 makes them
  // exact from the three parts' pieces together, and the crossings of such a pixel are those of all three.
  Ops teeth = comb(10);
  teeth.push_back({'Z', {}});
  Params p = comb_style();
  p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  p.s.width = 0.2;
  Store s;
  const GSHSCENE_List * l = one_stroke(s, teeth, p.get(), translation(0.0, 0.0));
  GSHCPU_Render_Counters seen{};
  auto draw = [&](uint64_t budget, const GSHSCENE_Rect * damage = nullptr) {
    GSHCPU_Limits lim;
    gshcpu_limits_default(&lim);
    lim.max_exact_crossings = budget;
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &lim);
    Drawn d = render_list(sink.sink, l, 6, 6, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, damage);
    seen = sink.counters;
    return d;
  };
  Drawn def = draw(0);
  ASSERT_TRUE(def.entries.empty()) << "the default draws it";
  uint64_t most = seen.exact_crossings_max;
  ASSERT_GT(most, 3u);
  EXPECT_EQ(draw(most).alpha, def.alpha);
  ASSERT_EQ(draw(most - 1).entries.size(), 1u) << "one more is refused";
  GSHSCENE_Rect away = {0, 4, 6, 6};
  Drawn damaged = draw(most - 1, &away);
  ASSERT_EQ(damaged.entries.size(), 1u) << "and a damage away from the pixel refuses it too";
  EXPECT_EQ(damaged.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_TRUE(draw(most, &away).entries.empty());
  GSHSCENE_Rect left_of_it = {0, 0, 2, 6};
  EXPECT_EQ(draw(most - 1, &left_of_it).entries.size(), 1u);
  GSHSCENE_Rect right_of_it = {3, 0, 6, 6};
  EXPECT_EQ(draw(most - 1, &right_of_it).entries.size(), 1u);
}

TEST(RenderExactSink, CallerMemoryHoldsTheExactStepsMemoryAndAllocatesNothing) {
  Ops teeth = comb(10);
  Params p = comb_style();
  Store s;
  const GSHSCENE_List * l = one_stroke(s, teeth, p.get(), translation(0.0, 0.0));
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  Sink sink(GSHCPU_RENDER_TIERS_ALL, nullptr, &alloc);
  Drawn plain = render_list(sink.sink, l, 6, 6);
  ASSERT_EQ(plain.result, GSHSCENE_OK);
  RasterRef probe(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 6, 6));
  GSHSCENE_Target target = target_of(probe.raster);
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(l, &target, report.report);
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> memory(need + 16);
  const long calls = t.calls;
  Drawn caller = render_list(sink.sink, l, 6, 6, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr, nullptr,
      memory.data(), need);
  ASSERT_EQ(caller.result, GSHSCENE_OK);
  EXPECT_EQ(t.calls, calls) << "no allocation by the sink during a render from the caller's memory";
  EXPECT_EQ(caller.alpha, plain.alpha);
  Drawn shorter = render_list(sink.sink, l, 6, 6, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr, nullptr,
      memory.data(), need - 1);
  EXPECT_EQ(shorter.result, GSHSCENE_ERR_LIMIT) << "one byte less is a limit";
  // the exact step's arrays are in the need: a sink with a smaller field needs less
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_exact_fragments = 1u << 16;
  Sink big(GSHCPU_RENDER_TIERS_ALL, &lim);
  size_t bigger = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(big.sink, &req, &bigger), GSHSCENE_OK);
  EXPECT_GT(bigger, need) << "the pool and the scratch grow with the field";
}

TEST(RenderExactSink, EveryStrokeFixtureDrawsWithNothingRefused) {
  // The identity holds on every cell of the corpus: a cell whose pieces did not reproduce the rasterizer's sums would be a
  // refusal, and no fixture has one.
  size_t drawn = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "strokes-", 8) != 0) {
      continue;
    }
    Fixture fx(i);
    for (GSHCPU_Format format : RF_FORMATS) {
      RasterRef r(rf_raster(format, RF_WIDTH, RF_HEIGHT));
      ReportRef report;
      Sink sink;
      GSHSCENE_Target t = target_of(r.raster, &fx.ctx.target_map);
      GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << RF_FIXTURES[i].name;
      for (const GSHSCENE_Report_Entry & e : report.entries()) {
        EXPECT_NE(e.reason, GSHSCENE_REASON_RESOURCE_LIMIT) << RF_FIXTURES[i].name << ": nothing is refused for its pieces";
      }
      drawn++;
    }
  }
  EXPECT_GE(drawn, 11u * 4u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
