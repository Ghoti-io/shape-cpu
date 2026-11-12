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
 * The period tier and its cache (masks-images-patterns story R2; render.h, PC9 and PC10): a pattern whose step vectors are whole device
 * pixels is drawn from one window of its lattice picture, and that is the general tier's picture bit for bit, at every pixel, for every
 * lattice and tile of the cases below, in every format.  The tier is seen taken (its counter) and seen not taken where it does not apply;
 * the limits it meets are the general tier's; a hit of the cache is the miss; and a cache that is bypassed, evicted or dropped changes no pixel.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rh;

namespace {

/** A sink with its counters and the tiers named. */
struct TSink {
  GSHSCENE_Sink * sink = nullptr;
  GSHCPU_Render_Counters counters{};
  explicit TSink(unsigned tiers, const GSHCPU_Limits * limits = nullptr) {
    EXPECT_EQ(gshcpu_render_sink_create_tiered(nullptr, nullptr, limits, tiers, &counters, nullptr, nullptr, &sink), GSHCPU_OK);
  }
  ~TSink() {
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
  TSink(const TSink &) = delete;
  TSink & operator=(const TSink &) = delete;
};

uint64_t held(const GSHSCENE_Sink * sink) {
  uint64_t n = 0;
  EXPECT_EQ(gshscene_sink_static_bytes(sink, &n), GSHSCENE_OK);
  return n;
}

constexpr unsigned kAll = GSHCPU_RENDER_TIERS_ALL;
constexpr unsigned kNoPeriod = GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_PERIOD;
constexpr unsigned kNoCache = GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_CACHE;

struct Out {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::string bytes;
  GSHSCENE_Report_Summary summary{};
  std::vector<GSHSCENE_Report_Entry> entries;
};

Out draw(GSHSCENE_Sink * sink, const GSHSCENE_List * list, uint32_t w, uint32_t h, GSHCPU_Format format = GSHCPU_FORMAT_RGBA8_STRAIGHT,
    uint32_t flags = 0, const GSHSCENE_Rect * damage = nullptr, void * workspace = nullptr, size_t workspace_bytes = 0) {
  RasterRef raster(rf_raster(format, w, h));
  unsigned char * px = (unsigned char *)gimg_raster_pixels(raster.raster);
  std::memset(px, 0, gimg_raster_stride_bytes(raster.raster) * h);
  ReportRef report;
  GSHSCENE_Target t = target_of(raster.raster);
  t.damage = damage;
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  req.workspace = workspace;
  req.workspace_bytes = workspace_bytes;
  Out o;
  o.result = gshscene_sink_render(sink, &req);
  o.bytes = raster.bytes();
  o.summary = report.summary();
  o.entries = report.entries();
  return o;
}

/**
 * One scene: a pattern of the tile `kind` over the cell (0, 0, cw, ch) at steps (sx, sy) under the map @p a..f (a 2D affine), filling a
 * 72 by 56 target.  The tiles hold what the period must reproduce: shapes across the cell's edge, a gradient, a stroke that ends at the
 * cell edge and one that crosses it, a clip block, an opacity group and (kind 1) a MULTIPLY group, the foreground (kind 2), an image (kind 3).
 */
struct Scene {
  RF_Ctx ctx;
  GSHSCENE_List * list = nullptr;
  const GSHSCENE_Resource * pattern = nullptr;
  Scene(int kind, double cw, double ch, double sx, double sy, double a, double b, double c, double d, double tx, double ty, uint32_t rx = 1,
      uint32_t ry = 1, double fg_alpha = 1.0, double w = 72, double h = 56) {
    rf_begin(&ctx);
    GSHSCENE_List * t = rf_open(&ctx);
    rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, cw, ch), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.5, 1.0), 0, 0.0);
    rf_fill(&ctx, t, rf_rect(&ctx, 0.3 * cw, -0.2 * ch, 0.6 * cw, 0.5 * ch), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.8), 0, 0.0);
    GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
    const GSHSCENE_Resource * grad = rf_gradient(&ctx, rf_linear(0, 0, cw, ch, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
    rf_fill(&ctx, t, rf_rect(&ctx, 0.5 * cw, 0.55 * ch, 1.3 * cw, 0.9 * ch), nullptr, GSHGEOM_FILL_NONZERO, rf_gradient_paint(grad, 0.8), 0, 0.0);
    // a stroke that ends exactly at the cell's right edge, with a butt cap, and one that runs across it
    const double ends[4] = {0.1 * cw, 0.7 * ch, cw, 0.7 * ch};
    rf_stroke(&ctx, t, rf_polyline(&ctx, ends, 2, 0), rf_pen(&ctx, 0.15 * ch, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr,
        rf_solid(0.1, 0.1, 0.4, 1.0));
    const double across[6] = {-0.2 * cw, 0.2 * ch, 0.6 * cw, 0.35 * ch, 1.2 * cw, 0.1 * ch};
    rf_stroke(&ctx, t, rf_polyline(&ctx, across, 3, 0), rf_pen(&ctx, 0.12 * ch, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), nullptr,
        rf_solid(0.1, 0.5, 0.2, 0.9));
    rf_check(&ctx, gshscene_list_clip_push(t, rf_circle(&ctx, 0.35 * cw), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, cw, ch), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.9, 0.6), 0, 0.0);
    rf_check(&ctx, gshscene_list_clip_pop(t) == GSHSCENE_OK);
    rf_general(&ctx, t, 0, 0, 0.6, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
    rf_fill(&ctx, t, rf_rect(&ctx, 0.2 * cw, 0.2 * ch, 0.8 * cw, 0.8 * ch), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.1, 1.0), 0, 0.0);
    rf_pop(&ctx, t);
    if (kind == 1) {
      rf_general(&ctx, t, 1, 0, 1.0, GSHSCENE_COMPOSITE_MULTIPLY, nullptr);
      rf_fill(&ctx, t, rf_rect(&ctx, 0, 0.6 * ch, cw, ch), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.4, 0.8, 0.9, 1.0), 0, 0.0);
      rf_pop(&ctx, t);
    }
    if (kind == 2) {
      GSHSCENE_Paint f = rf_solid(0, 0, 0, 0.7);
      f.kind = GSHSCENE_PAINT_FOREGROUND;
      f.rgba[3] = 0.7;
      rf_fill(&ctx, t, rf_circle(&ctx, 0.2 * cw), nullptr, GSHGEOM_FILL_NONZERO, f, 0, 0.0);
    }
    if (kind == 3) {
      GSHSCENE_Color_Space none;
      std::memset(&none, 0, sizeof none);
      const GSHSCENE_Resource * img = rf_image(&ctx, 4, 3, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
      GSHSCENE_Paint ip = rf_image_paint(&ctx, img, rf_place(&ctx, cw / 4.0, ch / 3.0, 0.0, 0.0), GSHSCENE_IMAGE_EXTEND_PAD,
          GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, nullptr, 1.0);
      rf_fill(&ctx, t, rf_rect(&ctx, 0.4 * cw, 0.1 * ch, 1.1 * cw, 0.9 * ch), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
    }
    const GSHSCENE_Resource * tile = rf_picture_of(&ctx, t);
    pattern = rf_pattern(&ctx, tile, 0, 0, cw, ch, sx, sy, rx, ry, rf_affine(&ctx, a, b, c, d, tx, ty), nullptr);
    width = w;
    height = h;
    list = make_list(fg_alpha, 0.0, 0.0);
  }
  double width = 0, height = 0;
  /** Another list over the same pattern: the foreground it names and the item's translation (a whole-pixel one moves the picture, it does not change it). */
  GSHSCENE_List * make_list(double fg_alpha, double dx, double dy) {
    GSHSCENE_List * l = rf_open(&ctx);
    GSHSCENE_Color fg;
    std::memset(&fg, 0, sizeof fg);
    fg.rgba[0] = 0.1 + 0.4 * fg_alpha;
    fg.rgba[1] = 0.6;
    fg.rgba[2] = 0.2;
    fg.rgba[3] = 1.0;
    rf_check(&ctx, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
    rf_fill(&ctx, l, rf_rect(&ctx, 0, 0, width, height), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.95, 1.0), 0, 0.0);
    GSHGEOM_Map move = rf_move(dx, dy);
    rf_fill(&ctx, l, rf_rect(&ctx, 3, 2, width - 4 - dx, height - 3 - dy), &move, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pattern, 1.0), 0, 0.0);
    rf_close(&ctx, l);
    return l;
  }
  ~Scene() {
    rf_release(&ctx);
  }
  Scene(const Scene &) = delete;
  Scene & operator=(const Scene &) = delete;
};

struct Lattice {
  const char * name;
  int kind;
  double cw, ch, sx, sy, a, b, c, d, tx, ty;
  uint32_t rx, ry;
};

/** The lattices of the integer-period cases: abutting, gapped, overlapping, skewed, scaled, quarter turned, rotated by an integer rotation. */
const Lattice kLattices[] = {
    {"abut 8 at the origin", 0, 8, 8, 8, 8, 1, 0, 0, 1, 0.0, 0.0, 1, 1},
    {"abut 8 at 0.3 0.45", 0, 8, 8, 8, 8, 1, 0, 0, 1, 0.3, 0.45, 1, 1},
    {"abut 6 at 2.5 1.75", 0, 6, 6, 6, 6, 1, 0, 0, 1, 2.5, 1.75, 1, 1},
    {"gapped 8 in 12 and 10", 0, 8, 8, 12, 10, 1, 0, 0, 1, 1.25, 0.1, 1, 1},
    {"overlap 8 in 6 and 5", 0, 8, 8, 6, 5, 1, 0, 0, 1, 0.2, 0.7, 1, 1},
    {"overlap with a MULTIPLY group", 1, 8, 8, 6, 5, 1, 0, 0, 1, 0.2, 0.7, 1, 1},
    {"abut with a MULTIPLY group", 1, 8, 8, 8, 8, 1, 0, 0, 1, 0.5, 0.25, 1, 1},
    {"skewed (8, 3) and (0, 5)", 0, 8, 5, 1, 1, 8, 0, 3, 5, 0.3, 0.6, 1, 1},
    {"scaled by 2", 0, 4, 4, 4, 4, 2, 0, 0, 2, 0.7, 0.1, 1, 1},
    {"integer rotation (3, 4)", 0, 5, 5, 1, 1, 3, 4, -4, 3, 0.25, 0.5, 1, 1},
    {"quarter turn", 0, 7, 7, 1, 1, 0, 7, -7, 0, 0.5, 0.5, 1, 1},
    {"the foreground", 2, 8, 8, 8, 8, 1, 0, 0, 1, 0.3, 0.45, 1, 1},
    {"an image across the edge", 3, 8, 6, 8, 6, 1, 0, 0, 1, 0.3, 0.45, 1, 1},
    {"1 px", 0, 1, 1, 1, 1, 1, 0, 0, 1, 0.5, 0.5, 1, 1},
    {"mirrored (negative scale)", 0, 6, 6, 6, 6, -1, 0, 0, 1, 70.3, 0.45, 1, 1},
};

}  // namespace

TEST(RenderPatternPeriod, EveryIntegerLatticeIsTheGeneralTiersPictureBitForBitInEveryFormat) {
  for (const Lattice & l : kLattices) {
    for (GSHCPU_Format format : RF_FORMATS) {
      Scene s(l.kind, l.cw, l.ch, l.sx, l.sy, l.a, l.b, l.c, l.d, l.tx, l.ty, l.rx, l.ry);
      TSink on(kAll);
      TSink off(kNoPeriod);
      Out a = draw(on.sink, s.list, 72, 56, format);
      Out b = draw(off.sink, s.list, 72, 56, format);
      ASSERT_EQ(a.result, GSHSCENE_OK) << l.name;
      ASSERT_EQ(b.result, GSHSCENE_OK) << l.name;
      EXPECT_EQ(a.bytes, b.bytes) << l.name << " " << gshcpu_format_string(format);
      EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], b.summary.by_class[GSHSCENE_ENTRY_REFUSED]) << l.name;
      EXPECT_GE(on.counters.pattern_period, 1u) << l.name << ": the tier is taken";
      EXPECT_EQ(off.counters.pattern_period, 0u) << l.name << ": and is off when it is off";
      EXPECT_EQ(on.counters.period_aborts, 0u) << l.name;
      EXPECT_GE(on.counters.patterns, 1u);
      EXPECT_LT(on.counters.pattern_copies, off.counters.pattern_copies) << l.name << ": the window draws fewer copies than the lattice has";
    }
  }
}

TEST(RenderPatternPeriod, WhereTheTierDoesNotApplyItIsNotTakenAndTheBitsAreTheSame) {
  struct Negative {
    const char * name;
    Lattice lat;
    uint32_t w, h;
  };
  const Negative cases[] = {
      {"a step of 8.5 pixels", {"", 0, 8, 8, 8.5, 8, 1, 0, 0, 1, 0.3, 0.45, 1, 1}, 72, 56},
      {"the second step 8.25", {"", 0, 8, 8, 8, 8.25, 1, 0, 0, 1, 0.3, 0.45, 1, 1}, 72, 56},
      {"repeat_x 0", {"", 0, 8, 8, 8, 8, 1, 0, 0, 1, 20.3, 0.45, 0, 1}, 72, 56},
      {"repeat_y 0", {"", 0, 8, 8, 8, 8, 1, 0, 0, 1, 0.3, 20.45, 1, 0}, 72, 56},
      {"a skew that is not whole pixels", {"", 0, 8, 8, 8, 8, 1, 0, 0.33, 1, 0.3, 0.45, 1, 1}, 72, 56},
      {"the step vectors 1 ulp off whole pixels", {"", 0, 8, 8, 8.000000000000002, 8, 1, 0, 0, 1, 0.3, 0.45, 1, 1}, 72, 56},
      // |D| = 64 x 64 does not fit a 40 by 10 target
      {"the window p x s does not fit the rectangle", {"", 0, 64, 64, 64, 64, 1, 0, 0, 1, 0.3, 0.45, 1, 1}, 72, 10},
      // a window of 1000 x 1 fits a 1000-wide layer only; the layer here is 72 wide
      {"a period wider than the layer", {"", 0, 80, 4, 80, 4, 1, 0, 0, 1, 0.3, 0.45, 1, 1}, 72, 56},
  };
  for (const Negative & n : cases) {
    const Lattice & l = n.lat;
    Scene s(l.kind, l.cw, l.ch, l.sx, l.sy, l.a, l.b, l.c, l.d, l.tx, l.ty, l.rx, l.ry, 1.0, n.w, n.h);
    TSink on(kAll);
    TSink off(kNoPeriod);
    Out a = draw(on.sink, s.list, n.w, n.h);
    Out b = draw(off.sink, s.list, n.w, n.h);
    ASSERT_EQ(a.result, GSHSCENE_OK) << n.name;
    EXPECT_EQ(a.bytes, b.bytes) << n.name;
    EXPECT_EQ(on.counters.pattern_period, 0u) << n.name << ": the general tier";
  }
  // A tile that holds a pattern: the tier is not taken (the nested copies are counted over the outer copies drawn), and the bits are the same.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * inner = rf_open(&c);
  rf_fill(&c, inner, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.7, 0.2, 1.0), 0, 0.0);
  rf_fill(&c, inner, rf_rect(&c, 1, 1, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * small = rf_pattern(&c, rf_picture_of(&c, inner), 0, 0, 8, 8, 8, 8, 1, 1, rf_affine(&c, 0.5, 0, 0, 0.5, 0.25, 0.25), nullptr);
  GSHSCENE_List * outer_tile = rf_open(&c);
  rf_fill(&c, outer_tile, rf_rect(&c, 0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(small, 1.0), 0, 0.0);
  const GSHSCENE_Resource * outer = rf_pattern(&c, rf_picture_of(&c, outer_tile), 0, 0, 16, 16, 16, 16, 1, 1, rf_move(0.5, 0.5), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(outer, 1.0), 0, 0.0);
  rf_close(&c, l);
  TSink on(kAll);
  TSink off(kNoPeriod);
  Out a = draw(on.sink, l, 72, 56);
  Out b = draw(off.sink, l, 72, 56);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_GT(on.counters.patterns, on.counters.pattern_period) << "the outer pattern's pass is not the tier's: its tile holds a pattern";
  EXPECT_GE(on.counters.pattern_period, 1u) << "the inner passes have integer periods of their own";
  rf_release(&c);
}

TEST(RenderPatternPeriod, ACopyPastTheWindowIsTheCopyInIt) {
  // PC9: pixels far from the origin, behind a fraction, read from the one window: a lattice whose origin is 5000 pixels off.
  for (double t : {5000.3, -5000.3, 123456.75}) {
    Scene s(0, 8, 8, 8, 8, 1, 0, 0, 1, t, -t, 1, 1);
    TSink on(kAll);
    TSink off(kNoPeriod);
    Out a = draw(on.sink, s.list, 72, 56);
    Out b = draw(off.sink, s.list, 72, 56);
    ASSERT_EQ(a.result, GSHSCENE_OK) << t;
    EXPECT_EQ(a.bytes, b.bytes) << t;
    EXPECT_GE(on.counters.pattern_period, 1u) << t;
  }
}

TEST(RenderPatternPeriod, ADamagedRenderIsTheWholeRenderInsideTheDamageUnderBothTiers) {
  for (GSHCPU_Format format : RF_FORMATS) {
    Scene s(1, 8, 8, 6, 5, 1, 0, 0, 1, 0.2, 0.7);
    GSHSCENE_Rect damage{9, 11, 41, 37};
    TSink on(kAll);
    TSink off(kNoPeriod);
    Out whole = draw(on.sink, s.list, 72, 56, format);
    Out part = draw(on.sink, s.list, 72, 56, format, 0, &damage);
    Out part_off = draw(off.sink, s.list, 72, 56, format, 0, &damage);
    ASSERT_EQ(part.result, GSHSCENE_OK);
    EXPECT_EQ(part.bytes, part_off.bytes);
    const size_t bpp = whole.bytes.size() / (72u * 56u);
    for (int y = 11; y < 37; y++) {
      for (int x = 9; x < 41; x++) {
        const size_t at = ((size_t)y * 72u + (size_t)x) * bpp;
        EXPECT_EQ(whole.bytes.substr(at, bpp), part.bytes.substr(at, bpp)) << x << "," << y;
      }
    }
  }
}

TEST(RenderPatternPeriod, TheCopyLimitIsTheSameUnderBothTiersAndTheCountIsOfTheDefinedCopies) {
  // PC8, R2-D2: the refusal is a function of the list and the target; the period raster is not what is counted.  The use is the
  // rectangle [3, 12] by [2, 13] of 4-pixel cells at the origin: the cells that meet it, the touching ones too.
  Scene s(0, 4, 4, 4, 4, 1, 0, 0, 1, 0.0, 0.0, 1, 1, 1.0, 16, 16);
  GSHGEOM_Lattice lat{0, 0, 4, 4, 4, 4, 1, 1};
  GSHGEOM_Map map;
  gshgeom_map_translate(2, 0.0, 0.0, 0.0, &map);
  const int64_t rect[4] = {3, 2, 12, 13};
  uint64_t n = 0;
  ASSERT_EQ(gshgeom_lattice_each(&map, &lat, rect, UINT64_MAX >> 12, nullptr, nullptr, &n), GSHGEOM_OK);
  ASSERT_GT(n, 4u);
  for (uint64_t limit : {n - 1, n, n + 1, uint64_t(1000)}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_pattern_cells = limit;
    TSink on(kAll, &limits);
    TSink off(kNoPeriod, &limits);
    Out a = draw(on.sink, s.list, 16, 16);
    Out b = draw(off.sink, s.list, 16, 16);
    EXPECT_EQ(a.result, b.result) << limit;
    EXPECT_EQ(a.bytes, b.bytes) << limit;
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], b.summary.by_class[GSHSCENE_ENTRY_REFUSED]) << limit;
    // The tile holds strokes, which count for their points too (the next tests): the copies are a floor of the count, not the count.
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], limit < n ? 1u : a.summary.by_class[GSHSCENE_ENTRY_REFUSED]) << limit;
    EXPECT_EQ(on.counters.pattern_period, a.summary.by_class[GSHSCENE_ENTRY_REFUSED] ? 0u : 1u) << limit << ": a refused use draws no window";
  }
}

TEST(RenderPatternPeriod, ATileWithNoStrokeCountsTheCopiesOfTheLatticeAndNoMoreUnderBothTiers) {
  // PC8: with no stroke in the tile the count is the copies LT1 counts: n draws, n - 1 is refused, whichever tier draws.
  RF_Ctx ctx;
  rf_begin(&ctx);
  GSHSCENE_List * t = rf_open(&ctx);
  rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.5, 1.0), 0, 0.0);
  rf_fill(&ctx, t, rf_rect(&ctx, 1.2, -0.8, 3.4, 2.0), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.8), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_picture_of(&ctx, t);
  const GSHSCENE_Resource * pattern = rf_pattern(&ctx, tile, 0, 0, 4, 4, 4, 4, 1, 1, rf_affine(&ctx, 1, 0, 0, 1, 0.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&ctx);
  rf_fill(&ctx, l, rf_rect(&ctx, 3, 2, 12, 13), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pattern, 1.0), 0, 0.0);
  rf_close(&ctx, l);
  GSHGEOM_Lattice lat{0, 0, 4, 4, 4, 4, 1, 1};
  GSHGEOM_Map map;
  gshgeom_map_translate(2, 0.0, 0.0, 0.0, &map);
  const int64_t rect[4] = {3, 2, 15, 15};
  uint64_t n = 0;
  ASSERT_EQ(gshgeom_lattice_each(&map, &lat, rect, UINT64_MAX >> 12, nullptr, nullptr, &n), GSHGEOM_OK);
  ASSERT_GT(n, 4u);
  for (uint64_t limit : {n - 1, n, n + 1}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_pattern_cells = limit;
    TSink on(kAll, &limits);
    TSink off(kNoPeriod, &limits);
    Out a = draw(on.sink, l, 16, 16);
    Out b = draw(off.sink, l, 16, 16);
    EXPECT_EQ(a.bytes, b.bytes) << limit;
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], limit < n ? 1u : 0u) << limit;
    EXPECT_EQ(b.summary.by_class[GSHSCENE_ENTRY_REFUSED], limit < n ? 1u : 0u) << limit;
  }
  rf_release(&ctx);
}

TEST(RenderPatternPeriod, AStrokeOfTheTileCountsForEveryCopyByItsPointsAndTheRefusalIsTheSameUnderEveryTierAndCache) {
  // PC8: a copy of a stroke costs its outline's points (and a fill's or a clip's its path's), so the use counts one more copy for each of them past the first 16, for every copy of the pass.  The
  // count is a function of the list and the target: the period tier (which draws a window), a raster served from the cache (which draws
  // none) and the general tier refuse at the same limit, and the least limit that draws is past the copies the lattice counts.
  Scene s(0, 24, 24, 24, 24, 1, 0, 0, 1, 0.3, 0.45, 1, 1, 1.0, 72, 56);
  GSHGEOM_Lattice lat{0, 0, 24, 24, 24, 24, 1, 1};
  GSHGEOM_Map map;
  gshgeom_map_translate(2, 0.3, 0.45, 0.0, &map);
  const int64_t rect[4] = {3, 2, 68, 53};
  uint64_t n = 0;
  ASSERT_EQ(gshgeom_lattice_each(&map, &lat, rect, UINT64_MAX >> 12, nullptr, nullptr, &n), GSHGEOM_OK);
  ASSERT_GT(n, 1u);
  auto refused = [&](unsigned tiers, uint64_t limit, int renders, Out * last) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_pattern_cells = limit;
    TSink sink(tiers, &limits);
    Out o;
    for (int i = 0; i < renders; i++) {
      o = draw(sink.sink, s.list, 72, 56);
    }
    if (last) {
      *last = o;
    }
    return o.summary.by_class[GSHSCENE_ENTRY_REFUSED];
  };
  // the least limit the general tier draws at
  uint64_t lo = n, hi = n << 12;
  ASSERT_EQ(refused(kNoPeriod & kNoCache, hi, 1, nullptr), 0u);
  while (lo < hi) {
    const uint64_t mid = lo + (hi - lo) / 2;
    if (refused(kNoPeriod & kNoCache, mid, 1, nullptr) == 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  const uint64_t least = lo;
  EXPECT_GT(least, n) << "the strokes, fills and clips of the tile have more than 16 points each: more than the copies are counted";
  EXPECT_GE(least, n + n) << "and the count is of every copy";
  for (uint64_t limit : {least - 1, least, least + 1}) {
    Out general, period, again, no_cache;
    const uint64_t a = refused(kNoPeriod & kNoCache, limit, 1, &general);
    const uint64_t b = refused(kAll, limit, 1, &period);
    const uint64_t c = refused(kAll, limit, 2, &again); // the second render of one sink is served from the cache where the first drew
    const uint64_t d = refused(kNoCache, limit, 2, &no_cache);
    EXPECT_EQ(a, limit < least ? 1u : 0u) << limit;
    EXPECT_EQ(b, a) << limit << " (the period tier)";
    EXPECT_EQ(c, a) << limit << " (served from the cache on the second render)";
    EXPECT_EQ(d, a) << limit << " (the period tier without the cache)";
    EXPECT_EQ(period.bytes, general.bytes) << limit;
    EXPECT_EQ(again.bytes, general.bytes) << limit;
    EXPECT_EQ(no_cache.bytes, general.bytes) << limit;
  }
  // The cache does serve at the least limit that draws (each limit above was asked of a sink of its own: a sink's limits are fixed).
  GSHCPU_Limits roomy;
  gshcpu_limits_default(&roomy);
  roomy.max_pattern_cells = least;
  TSink kept(kAll, &roomy);
  draw(kept.sink, s.list, 72, 56);
  draw(kept.sink, s.list, 72, 56);
  EXPECT_GE(kept.counters.period_hits, 1u) << "the second render is served from the cache";
}

TEST(RenderPatternPeriod, APathOfManyPointsInASmallTileIsRefusedForWhatItCostsAndNotDrawnForEveryCopy) {
  // PC8: a tile of one stroke, one fill or one clip of a path of 1,500 vertices over a lattice of 60,000 copies is millions of copies by its cost, past the
  // default cap of 4 Mi: refused before a copy is drawn.  (Drawn, each is 60,000 copies of flattening, cutting and covering 1,500 points: 16, 9 and 18
  // seconds; a fuzzer found a list of the stroke's shape, nested, that did not finish in five minutes.)
  const char * const names[3] = {"a stroke", "a fill", "a clip and a fill under it"};
  for (int kind = 0; kind < 3; kind++) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHSCENE_List * t = rf_open(&ctx);
    std::vector<double> zig;
    for (int i = 0; i < 1500; i++) {
      zig.push_back(0.02 + 0.96 * i / 1499.0);
      zig.push_back(i % 2 ? 0.1 : 0.9);
    }
    if (kind == 0) {
      rf_stroke(&ctx, t, rf_polyline(&ctx, zig.data(), 1500, 0), rf_pen(&ctx, 0.05, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr,
          rf_solid(0.1, 0.1, 0.4, 1.0));
    } else if (kind == 1) {
      rf_fill(&ctx, t, rf_polyline(&ctx, zig.data(), 1500, 1), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.4, 1.0), 0, 0.0);
    } else {
      rf_check(&ctx, gshscene_list_clip_push(t, rf_polyline(&ctx, zig.data(), 1500, 1), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, 1, 1), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.4, 1.0), 0, 0.0);
      rf_check(&ctx, gshscene_list_clip_pop(t) == GSHSCENE_OK);
    }
    const GSHSCENE_Resource * tile = rf_picture_of(&ctx, t);
    const GSHSCENE_Resource * pattern = rf_pattern(&ctx, tile, 0, 0, 1, 1, 0.25, 0.25, 1, 1, rf_affine(&ctx, 1, 0, 0, 1, 0.0, 0.0), nullptr);
    GSHSCENE_List * l = rf_open(&ctx);
    rf_fill(&ctx, l, rf_rect(&ctx, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pattern, 1.0), 0, 0.0);
    rf_close(&ctx, l);
    TSink sink(kAll);
    Out o = draw(sink.sink, l, 72, 56);
    EXPECT_EQ(o.result, GSHSCENE_OK) << names[kind];
    ASSERT_EQ(o.entries.size(), 1u) << names[kind];
    EXPECT_EQ(o.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED) << names[kind];
    EXPECT_EQ(o.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT) << names[kind];
    rf_release(&ctx);
  }
}

TEST(RenderPatternPeriod, ALimitOfACopyIsMetUnderBothTiersAndTheTierGivesTheWindowUpWhereItCannotVouch) {
  // PC8, PC9: a cell-deposit cap that a whole copy can pass is met by the general tier on the copies it draws; the period tier, which
  // draws the copies of the window, cannot tell that no copy elsewhere passes it, and draws the use by the general tier.  The pixels, the
  // result and the report are the same for every cap, and over the sweep the tier is taken, given up and refused.
  Scene s(0, 24, 24, 24, 24, 1, 0, 0, 1, 0.3, 0.45, 1, 1, 1.0, 72, 56);
  uint64_t taken = 0, gave_up = 0, refused = 0;
  for (uint64_t cap : {uint64_t(4), uint64_t(40), uint64_t(100), uint64_t(110), uint64_t(120), uint64_t(140), uint64_t(160), uint64_t(200),
           uint64_t(260), uint64_t(320), uint64_t(400), uint64_t(520), uint64_t(700), uint64_t(1000), uint64_t(5000), uint64_t(1) << 20}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = cap;
    TSink on(kAll, &limits);
    TSink off(kNoPeriod, &limits);
    Out a = draw(on.sink, s.list, 72, 56);
    Out b = draw(off.sink, s.list, 72, 56);
    EXPECT_EQ(a.result, b.result) << cap;
    EXPECT_EQ(a.bytes, b.bytes) << cap;
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], b.summary.by_class[GSHSCENE_ENTRY_REFUSED]) << cap;
    EXPECT_EQ(a.entries.size(), b.entries.size()) << cap;
    taken += on.counters.pattern_period > 0;
    gave_up += on.counters.period_aborts > 0;
    refused += a.summary.by_class[GSHSCENE_ENTRY_REFUSED] > 0;
    EXPECT_TRUE(on.counters.pattern_period == 0 || on.counters.period_aborts == 0) << cap;
  }
  EXPECT_GE(taken, 1u) << "a cap that a whole copy cannot pass: the tier stands";
  EXPECT_GE(gave_up, 1u) << "a cap a whole copy can pass: the tier is given up for the general tier, which decides every copy";
  EXPECT_GE(refused, 1u) << "and a cap the general tier meets";
}

TEST(RenderPatternPeriod, ACachedPeriodIsTheMissBitForBitAndTheKeyIsWhatTheRasterContains) {
  Scene s(2, 8, 8, 8, 8, 1, 0, 0, 1, 0.25, 0.5); // a fraction that is a double exactly, so that adding whole pixels leaves its bits
  TSink cached(kAll);
  TSink plain(kNoCache);
  Out first = draw(cached.sink, s.list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 0u);
  EXPECT_EQ(cached.counters.period_misses, 1u);
  EXPECT_EQ(cached.counters.period_kept, 1u);
  Out second = draw(cached.sink, s.list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 1u) << "the second render finds the period";
  EXPECT_EQ(first.bytes, second.bytes) << "a hit is the miss";
  Out uncached = draw(plain.sink, s.list, 72, 56);
  EXPECT_EQ(uncached.bytes, first.bytes) << "the cache off draws the same pixels";
  EXPECT_EQ(plain.counters.period_hits + plain.counters.period_misses, 0u) << "and does not touch the table";
  EXPECT_EQ(held(plain.sink), 0u);
  const uint64_t with_period = held(cached.sink);
  EXPECT_GT(with_period, 0u);

  // The whole-pixel shift of the lattice is not in the key: the same pattern under an item map that moves it by whole pixels hits.
  GSHSCENE_List * moved_list = s.make_list(1.0, 5.0, 3.0);
  Out moved = draw(cached.sink, moved_list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 2u) << "a lattice moved by whole pixels finds the same raster";
  EXPECT_EQ(cached.counters.period_misses, 1u);
  Out moved_plain = draw(plain.sink, moved_list, 72, 56);
  EXPECT_EQ(moved.bytes, moved_plain.bytes);
  // ... and another fraction is another picture: a miss.
  GSHSCENE_List * other_list = s.make_list(1.0, 0.125, 0.0);
  Out o = draw(cached.sink, other_list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 2u);
  EXPECT_EQ(cached.counters.period_misses, 2u);
  EXPECT_NE(o.bytes, first.bytes);
  Out o_plain = draw(plain.sink, other_list, 72, 56);
  EXPECT_EQ(o.bytes, o_plain.bytes);
  // The foreground the tile reads is in the key: the same pattern under another foreground is a miss, and its pixels follow the foreground.
  GSHSCENE_List * fg2 = s.make_list(0.0, 0.0, 0.0);
  const uint64_t hits = cached.counters.period_hits;
  Out f2 = draw(cached.sink, fg2, 72, 56);
  Out f2_plain = draw(plain.sink, fg2, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, hits) << "another foreground, another key";
  EXPECT_EQ(f2.bytes, f2_plain.bytes);
  EXPECT_NE(f2.bytes, first.bytes);
  // ... and the first foreground again is a hit.
  Out again = draw(cached.sink, s.list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, hits + 1u);
  EXPECT_EQ(again.bytes, first.bytes);
}

/** A scene of two patterns in one store that differ in one thing only, drawn by lists of their own. */
struct Pair {
  RF_Ctx ctx;
  GSHSCENE_List * a = nullptr;
  GSHSCENE_List * b = nullptr;
  /** kind 0: cells of w by h; the second has the second tile; kind 1: the second is drawn under an item map that scales by 2. */
  Pair(int kind, double w, double h, double tx, double ty) {
    rf_begin(&ctx);
    const GSHSCENE_Resource * tiles[2];
    for (int k = 0; k < 2; k++) {
      GSHSCENE_List * t = rf_open(&ctx);
      rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, w, h), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.5, 1.0), 0, 0.0);
      rf_fill(&ctx, t, rf_rect(&ctx, 0.25 * w, 0.25 * h, (k && kind == 0 ? 0.9 : 0.5) * w, 0.5 * h), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.6, 0.9), 0, 0.0);
      tiles[k] = rf_picture_of(&ctx, t);
    }
    for (int k = 0; k < 2; k++) {
      const GSHSCENE_Resource * p = rf_pattern(&ctx, tiles[kind == 0 ? k : 0], 0, 0, w, h, w, h, 1, 1, rf_move(tx, ty), nullptr);
      GSHSCENE_List * l = rf_open(&ctx);
      GSHGEOM_Map scale;
      gshgeom_map_scale(2, 2.0, 2.0, 1.0, &scale);
      rf_fill(&ctx, l, rf_rect(&ctx, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.95, 1.0), 0, 0.0);
      rf_fill(&ctx, l, rf_rect(&ctx, 3, 2, 35, 27), kind == 1 && k ? &scale : nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(p, 1.0), 0, 0.0);
      rf_close(&ctx, l);
      (k ? b : a) = l;
    }
  }
  ~Pair() {
    rf_release(&ctx);
  }
  Pair(const Pair &) = delete;
  Pair & operator=(const Pair &) = delete;
};

TEST(RenderPatternPeriod, ANonSquarePeriodIsKeptAndServedInItsOwnOrder) {
  // PC10: the raster is indexed by the class, row after row of the window's width; a window of 8 by 4 is read back as it was written.
  Scene s(0, 8, 4, 8, 4, 1, 0, 0, 1, 0.25, 0.5);
  TSink cached(kAll);
  TSink plain(kNoCache);
  Out first = draw(cached.sink, s.list, 72, 56);
  Out second = draw(cached.sink, s.list, 72, 56);
  Out base = draw(plain.sink, s.list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 1u);
  EXPECT_EQ(first.bytes, base.bytes);
  EXPECT_EQ(second.bytes, base.bytes);
  // (Each scene has a store of its own, and ids are a store's, so each gets a sink of its own.)
  TSink cached2(kAll);
  Scene t(0, 4, 8, 4, 8, 1, 0, 0, 1, 0.25, 0.5);
  Out tf = draw(cached2.sink, t.list, 72, 56);
  Out ts = draw(cached2.sink, t.list, 72, 56);
  Out tb = draw(plain.sink, t.list, 72, 56);
  EXPECT_EQ(tf.bytes, tb.bytes);
  EXPECT_EQ(ts.bytes, tb.bytes);
  EXPECT_EQ(cached2.counters.period_hits, 1u);
  // A skewed lattice has a window whose width is not its height either.
  TSink cached3(kAll);
  Scene k(0, 5, 5, 1, 1, 8, 0, 3, 5, 0.25, 0.5);
  Out kf = draw(cached3.sink, k.list, 72, 56);
  Out ks = draw(cached3.sink, k.list, 72, 56);
  Out kb = draw(plain.sink, k.list, 72, 56);
  EXPECT_EQ(kf.bytes, kb.bytes);
  EXPECT_EQ(ks.bytes, kb.bytes);
  EXPECT_EQ(cached3.counters.period_hits, 1u);
}

TEST(RenderPatternPeriod, AnotherTileAnotherLinearPartAndAnotherFractionAreAnotherKey) {
  // PC10: the tile's id, the linear part and each fraction are in the key; one pattern under a map of another scale, or at another fraction in
  // y alone, or another tile of the same cell, never finds the first one's raster (the pixels follow the list, not the table).
  for (int kind = 0; kind < 2; kind++) {
    Pair pr(kind, 8, 8, 0.25, 0.5);
    TSink cached(kAll);
    TSink plain(kNoCache);
    draw(cached.sink, pr.a, 72, 56);
    Out b = draw(cached.sink, pr.b, 72, 56);
    Out bp = draw(plain.sink, pr.b, 72, 56);
    EXPECT_EQ(cached.counters.period_hits, 0u) << kind;
    EXPECT_EQ(b.bytes, bp.bytes) << kind;
  }
  Scene s(0, 8, 8, 8, 8, 1, 0, 0, 1, 0.25, 0.5);
  TSink cached(kAll);
  TSink plain(kNoCache);
  draw(cached.sink, s.list, 72, 56);
  GSHSCENE_List * moved = s.make_list(1.0, 0.0, 0.125);
  Out m = draw(cached.sink, moved, 72, 56);
  Out mp = draw(plain.sink, moved, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 0u) << "another fraction in y alone";
  EXPECT_EQ(m.bytes, mp.bytes);
}

TEST(RenderPatternPeriod, TheKeyOmitsTheForegroundWhereTheTileDoesNotReadIt) {
  Scene a(0, 8, 8, 8, 8, 1, 0, 0, 1, 0.3, 0.45, 1, 1, 1.0);
  TSink cached(kAll);
  Out x = draw(cached.sink, a.list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 0u);
  GSHSCENE_List * other = a.make_list(0.0, 0.0, 0.0);
  Out y = draw(cached.sink, other, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 1u) << "a tile that reads no foreground has one picture whatever the foreground is";
  EXPECT_EQ(x.bytes, y.bytes);
}

TEST(RenderPatternPeriod, EvictingAPatternDropsItsPeriodAndDestroyingTheSinkFreesTheTable) {
  Scene s(0, 8, 8, 8, 8, 1, 0, 0, 1, 0.3, 0.45);
  TSink cached(kAll);
  Out first = draw(cached.sink, s.list, 72, 56);
  const uint64_t before = held(cached.sink);
  ASSERT_GT(before, 0u);
  EXPECT_EQ(gshscene_sink_evict(cached.sink, gshscene_resource_id(s.pattern)), GSHSCENE_OK);
  const uint64_t period_bytes = 8u * 8u * 32u;
  EXPECT_LE(held(cached.sink) + period_bytes, before) << "the period raster is gone (the stroke outlines of the tile stay)";
  Out again = draw(cached.sink, s.list, 72, 56);
  EXPECT_EQ(cached.counters.period_hits, 0u) << "an evicted period is made again";
  EXPECT_EQ(cached.counters.period_misses, 2u);
  EXPECT_EQ(again.bytes, first.bytes);
  EXPECT_GE(cached.counters.cache_evictions, 1u);
  // Evicting the tile's id does the same.
}

TEST(RenderPatternPeriod, TheCacheIsBoundedAndARasterLargerThanTheBoundIsDrawnAndNotKept) {
  Scene s(0, 16, 16, 16, 16, 1, 0, 0, 1, 0.3, 0.45);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_cache_bytes = 1024; // a period of 16 x 16 pixels is 8 KiB
  TSink small(kAll, &limits);
  TSink plain(kNoCache);
  Out a = draw(small.sink, s.list, 72, 56);
  Out b = draw(plain.sink, s.list, 72, 56);
  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_EQ(small.counters.period_kept, 0u);
  EXPECT_LE(held(small.sink), 1024u);
  // A bound that holds one raster but not two keeps the newest, evicting the older.
  limits.max_cache_bytes = 16u * 16u * 32u + 4096u;
  TSink one(kAll, &limits);
  Scene t(0, 16, 16, 16, 16, 1, 0, 0, 1, 0.5, 0.25);
  draw(one.sink, s.list, 72, 56);
  draw(one.sink, t.list, 72, 56);
  EXPECT_EQ(one.counters.period_kept, 2u);
  EXPECT_GE(one.counters.cache_evictions, 1u);
  EXPECT_LE(held(one.sink), limits.max_cache_bytes);
}

TEST(RenderPatternPeriod, TheCallersMemoryDrawsTheTierWithoutTheTableOrTheAllocatorAndTheWorkspaceIsExact) {
  Scene s(1, 8, 8, 6, 5, 1, 0, 0, 1, 0.2, 0.7);
  TSink sink(kAll);
  Out plain = draw(sink.sink, s.list, 72, 56);
  const uint64_t hits = sink.counters.period_hits;
  const uint64_t misses = sink.counters.period_misses;
  RasterRef own(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 72, 56));
  GSHSCENE_Target t = target_of(own.raster);
  ReportRef report;
  GSHSCENE_Render_Request r = request_of(s.list, &t, report.report);
  r.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &r, &need), GSHSCENE_OK);
  std::vector<unsigned char> memory(need + 64);
  r.workspace = memory.data();
  r.workspace_bytes = need;
  ASSERT_EQ(gshscene_sink_render(sink.sink, &r), GSHSCENE_OK);
  EXPECT_EQ(own.bytes(), plain.bytes);
  EXPECT_EQ(sink.counters.period_hits, hits) << "no entry is read under CALLER_MEMORY";
  EXPECT_EQ(sink.counters.period_misses, misses) << "and none is written";
  EXPECT_GE(sink.counters.pattern_period, 2u) << "the tier is taken all the same";
  r.workspace_bytes = need - 1;
  RasterRef less(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 72, 56));
  GSHSCENE_Target tl = target_of(less.raster);
  r.target = &tl;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &r), GSHSCENE_ERR_LIMIT);
}

TEST(RenderPatternPeriod, ASurfaceRenderedByTwoSinksSharingNothingIsTheSame) {
  Scene s(1, 8, 8, 6, 5, 1, 0, 0, 1, 0.2, 0.7);
  TSink a(kAll);
  TSink b(kAll);
  Out x = draw(a.sink, s.list, 72, 56);
  Out y = draw(b.sink, s.list, 72, 56);
  Out z = draw(b.sink, s.list, 72, 56);
  EXPECT_EQ(x.bytes, y.bytes);
  EXPECT_EQ(y.bytes, z.bytes);
}

TEST(RenderPatternPeriod, ThePatternFixturesAreTheSameUnderBothTiersAndTheIntegerOnesTakeTheTier) {
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "pattern-", 8) != 0) {
      continue;
    }
    Fixture fx(i);
    for (GSHCPU_Format format : RF_FORMATS) {
      TSink on(kAll);
      TSink off(kNoPeriod);
      RasterRef ra(rf_raster(format, RF_WIDTH, RF_HEIGHT));
      RasterRef rb(rf_raster(format, RF_WIDTH, RF_HEIGHT));
      ReportRef rep;
      GSHSCENE_Target ta = target_of(ra.raster, &fx.ctx.target_map);
      GSHSCENE_Target tb = target_of(rb.raster, &fx.ctx.target_map);
      GSHSCENE_Render_Request qa = request_of(fx.list, &ta, rep.report);
      GSHSCENE_Render_Request qb = request_of(fx.list, &tb, rep.report);
      ASSERT_EQ(gshscene_sink_render(on.sink, &qa), GSHSCENE_OK) << RF_FIXTURES[i].name;
      ASSERT_EQ(gshscene_sink_render(off.sink, &qb), GSHSCENE_OK) << RF_FIXTURES[i].name;
      EXPECT_EQ(ra.bytes(), rb.bytes()) << RF_FIXTURES[i].name << " " << gshcpu_format_string(format);
      if (std::strncmp(RF_FIXTURES[i].name, "pattern-period", 14) == 0 || std::strcmp(RF_FIXTURES[i].name, "pattern-integer") == 0) {
        EXPECT_GE(on.counters.pattern_period, 1u) << RF_FIXTURES[i].name << ": the integer lattices take the tier";
      }
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
