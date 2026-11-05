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
 * The period tier's key, its tile facts, its give-up and the table that holds its rasters (masks-images-patterns story R2; render.h, PC9 and
 * PC10), each told from a plausible wrong one.  A key field is shown by one pattern drawn twice through one sink with only that field changed:
 * the second picture must follow the field, which a stale hit would not.  The give-up is shown by dense sweeps of a limit, since a window that
 * cannot vouch for a copy matters only in the narrow band of limits between what the window's copies need and what a whole copy needs.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rh;

namespace {

struct TSink {
  GSHSCENE_Sink * sink = nullptr;
  GSHCPU_Render_Counters counters{};
  explicit TSink(unsigned tiers = GSHCPU_RENDER_TIERS_ALL, const GSHCPU_Limits * limits = nullptr) {
    EXPECT_EQ(gshcpu_render_sink_create_tiered(nullptr, nullptr, limits, tiers, &counters, nullptr, nullptr, &sink), GSHCPU_OK);
  }
  ~TSink() {
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
  TSink(const TSink &) = delete;
  TSink & operator=(const TSink &) = delete;
};

constexpr unsigned kAll = GSHCPU_RENDER_TIERS_ALL;
constexpr unsigned kNoPeriod = GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_PERIOD;
constexpr unsigned kNoCache = GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_CACHE;

struct Out {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::string bytes;
  size_t refused = 0;
  size_t entries = 0;
};

Out draw(GSHSCENE_Sink * sink, const GSHSCENE_List * list, uint32_t w, uint32_t h, const GSHSCENE_Color_Space * target_space = nullptr) {
  RasterRef raster(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, w, h));
  std::memset(gimg_raster_pixels(raster.raster), 0, gimg_raster_stride_bytes(raster.raster) * h);
  ReportRef report;
  GSHSCENE_Target t = target_of(raster.raster);
  if (target_space) {
    t.blend_space = *target_space;
  }
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  Out o;
  o.result = gshscene_sink_render(sink, &req);
  o.bytes = raster.bytes();
  GSHSCENE_Report_Summary s = report.summary();
  o.refused = s.by_class[GSHSCENE_ENTRY_REFUSED];
  o.entries = gshscene_report_count(report.report);
  return o;
}

GSHSCENE_Color_Space space(GCOL_Primaries p, GCOL_Transfer t, double gamma) {
  GSHSCENE_Color_Space s;
  std::memset(&s, 0, sizeof s);
  s.primaries = p;
  s.transfer = t;
  s.gamma = gamma;
  return s;
}

/** One pattern (of the tile `kind`) and any number of lists that fill with it, each its own foreground, palette, group space and item map. */
struct Family {
  RF_Ctx ctx;
  const GSHSCENE_Resource * pattern = nullptr;
  std::vector<const GSHSCENE_Resource *> palettes;
  /** kind 0: foreground fill; 1: a gradient whose stop is the foreground; 2: an alpha-only image with a foreground tint; 3: a palette entry;
   *  4: a stroke painted with the foreground; 5: the foreground inside an instanced picture; 6: a plain tile (no foreground). */
  explicit Family(int kind) {
    rf_begin(&ctx);
    GSHSCENE_Paint f = rf_solid(0, 0, 0, 0.8);
    f.kind = GSHSCENE_PAINT_FOREGROUND;
    f.rgba[3] = 0.8;
    GSHSCENE_Paint pal = rf_solid(0, 0, 0, 1.0);
    pal.kind = GSHSCENE_PAINT_PALETTE;
    pal.entry = 1;
    GSHSCENE_List * t = rf_open(&ctx);
    rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.7, 1.0), 0, 0.0);
    switch (kind) {
      case 0:
        rf_fill(&ctx, t, rf_rect(&ctx, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, f, 0, 0.0);
        break;
      case 1: {
        GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, f), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
        rf_fill(&ctx, t, rf_rect(&ctx, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO,
            rf_gradient_paint(rf_gradient(&ctx, rf_linear(1, 0, 7, 0, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2), 1.0), 0, 0.0);
        break;
      }
      case 2: {
        GSHSCENE_Color_Space none;
        std::memset(&none, 0, sizeof none);
        const GSHSCENE_Resource * img = rf_image(&ctx, 4, 3, 1, 8, GIMG_ALPHA_NONE, 1, none);
        GSHSCENE_Paint ip = rf_image_paint(&ctx, img, rf_place(&ctx, 1.0, 1.0, 1.5, 1.5), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
            GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_NEAREST, none, &f, 1.0);
        rf_fill(&ctx, t, rf_rect(&ctx, 1.5, 1.5, 5.5, 4.5), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
        break;
      }
      case 3:
        rf_fill(&ctx, t, rf_rect(&ctx, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, pal, 0, 0.0);
        break;
      case 4: {
        const double line[4] = {2.0, 4.0, 6.0, 4.0};
        rf_stroke(&ctx, t, rf_polyline(&ctx, line, 2, 0), rf_pen(&ctx, 2.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr, f);
        break;
      }
      case 5: {
        GSHSCENE_List * inner = rf_open(&ctx);
        rf_fill(&ctx, inner, rf_rect(&ctx, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, f, 0, 0.0);
        const GSHSCENE_Resource * pic = rf_picture_of(&ctx, inner);
        GSHGEOM_Map at = rf_move(0.0, 0.0);
        rf_check(&ctx, gshscene_list_instance(t, pic, &at, 0, 0.0) == GSHSCENE_OK);
        break;
      }
      default:
        rf_fill(&ctx, t, rf_rect(&ctx, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3, 0.8, 0.9), 0, 0.0);
        break;
    }
    pattern = rf_pattern(&ctx, rf_picture_of(&ctx, t), 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.25, 0.5), nullptr);
    for (int k = 0; k < 2; k++) {
      GSHSCENE_Color colors[2];
      std::memset(colors, 0, sizeof colors);
      colors[0].rgba[3] = 1.0;
      colors[1].rgba[0] = k ? 0.9 : 0.1;
      colors[1].rgba[1] = 0.4;
      colors[1].rgba[2] = k ? 0.1 : 0.8;
      colors[1].rgba[3] = 1.0;
      const GSHSCENE_Resource * p = nullptr;
      rf_check(&ctx, gshscene_store_intern_palette(ctx.store, 2, colors, &p) == GSHSCENE_OK);
      palettes.push_back(rf_hold(&ctx, p));
    }
  }
  ~Family() {
    rf_release(&ctx);
  }
  Family(const Family &) = delete;
  Family & operator=(const Family &) = delete;
  /** A list: foreground alpha and space, palette k, the pattern drawn in a group of blend space @p group (zero: none), under the item map @p map. */
  GSHSCENE_List * list(double fg_alpha, const GSHSCENE_Color_Space & fg_space, int palette, const GSHSCENE_Color_Space * group, const GSHGEOM_Map * map) {
    GSHSCENE_List * l = rf_open(&ctx);
    GSHSCENE_Color fg;
    std::memset(&fg, 0, sizeof fg);
    fg.rgba[0] = 0.2;
    fg.rgba[1] = 0.7;
    fg.rgba[2] = 0.3;
    fg.rgba[3] = fg_alpha;
    fg.space = fg_space;
    rf_check(&ctx, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
    rf_check(&ctx, gshscene_list_set_palette(l, palettes[(size_t)palette]) == GSHSCENE_OK);
    rf_fill(&ctx, l, rf_rect(&ctx, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.95, 1.0), 0, 0.0);
    if (group) {
      GSHSCENE_Group g = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
      g.blend_space = *group;
      rf_check(&ctx, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
    }
    rf_fill(&ctx, l, rf_rect(&ctx, 4, 3, 68, 51), map, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pattern, 1.0), 0, 0.0);
    if (group) {
      rf_check(&ctx, gshscene_list_group_pop(l) == GSHSCENE_OK);
    }
    rf_close(&ctx, l);
    return l;
  }
};

} // namespace

TEST(RenderPatternKeys, EveryFieldTheRasterDependsOnIsInTheKey) {
  // PC10: the same pattern drawn through one sink with only one thing changed between the lists: the second picture is what the cache off draws, so
  // the field is in the key.  The foreground (its alpha and its space), the palette, the group's space (the lattice layer's), the target's space and the
  // linear part, for a tile that reads the foreground by a fill, a gradient's stop, an alpha-only image's tint, a stroke and an instanced picture.
  const GSHSCENE_Color_Space srgb = space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0);
  const GSHSCENE_Color_Space p3 = space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  const GSHSCENE_Color_Space lin = space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  const GSHSCENE_Color_Space gam = space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.2);
  const GSHSCENE_Color_Space gam2 = space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 1.8);
  const GSHSCENE_Color_Space none = space(GCOL_PRIMARIES_UNKNOWN, GCOL_TRANSFER_UNKNOWN, 0.0);
  for (int kind = 0; kind <= 6; kind++) {
    Family fam(kind);
    GSHGEOM_Map scale2, skew, flip;
    gshgeom_map_scale(2, 1.0, 2.0, 1.0, &scale2);
    const double sk[4] = {1.0, 0.0, 1.0, 1.0}, t0[2] = {0, 0};
    gshgeom_map_from_matrix(2, sk, t0, &skew);
    const double fl[4] = {-1.0, 0.0, 0.0, 1.0}, t1[2] = {72, 0};
    gshgeom_map_from_matrix(2, fl, t1, &flip);
    struct Step {
      GSHSCENE_List * list;
      const GSHSCENE_Color_Space * target;
    };
    const Step steps[] = {
        {fam.list(0.8, none, 0, nullptr, nullptr), nullptr},   {fam.list(0.4, none, 0, nullptr, nullptr), nullptr},   // the foreground's alpha
        {fam.list(0.8, p3, 0, nullptr, nullptr), nullptr},     {fam.list(0.8, srgb, 0, nullptr, nullptr), nullptr},  // its space
        {fam.list(0.8, none, 1, nullptr, nullptr), nullptr},                                                         // the palette
        {fam.list(0.8, none, 0, &lin, nullptr), nullptr},      {fam.list(0.8, none, 0, &gam, nullptr), nullptr},     // the layer's space
        {fam.list(0.8, none, 0, &gam2, nullptr), nullptr},
        {fam.list(0.8, none, 0, nullptr, nullptr), &lin},      {fam.list(0.8, none, 0, nullptr, nullptr), &p3},      // the target's
        {fam.list(0.8, none, 0, nullptr, nullptr), &gam},      {fam.list(0.8, none, 0, nullptr, nullptr), &gam2},
        {fam.list(0.8, none, 0, nullptr, &scale2), nullptr},   {fam.list(0.8, none, 0, nullptr, &skew), nullptr},    // the linear part
        {fam.list(0.8, none, 0, nullptr, &flip), nullptr},
        // the target's space where the layer's is another (a group's), so that only the colors the tile leaves in the target's space are converted from it
        {fam.list(0.8, none, 0, &lin, nullptr), &gam},         {fam.list(0.8, none, 0, &lin, nullptr), &gam2},       {fam.list(0.8, none, 0, &lin, nullptr), &p3},
    };
    TSink cached(kAll);
    TSink plain(kNoCache);
    for (const Step & s : steps) {
      Out a = draw(cached.sink, s.list, 72, 56, s.target);
      Out b = draw(plain.sink, s.list, 72, 56, s.target);
      ASSERT_EQ(a.result, GSHSCENE_OK) << kind;
      EXPECT_EQ(a.bytes, b.bytes) << "kind " << kind << " step " << (&s - steps);
    }
    EXPECT_GE(cached.counters.period_misses, 3u) << "the key separates what it should";
  }
}

TEST(RenderPatternKeys, ATileThatHoldsAMaskOrAPatternIsNotDrawnFromAWindow) {
  // PC9: a mask's picture is placed once in the pass's frame and not once for each copy, and a nested pattern's count is of the outer copies
  // drawn, so neither tile takes the tier, whether the mark is in the tile or in a picture it instances; and every such render is the tier off's.
  for (int kind = 0; kind < 4; kind++) {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * pl = rf_open(&c);
    rf_fill(&c, pl, rf_rect(&c, 0, 0, 5, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 0.6), 0, 0.0);
    GSHSCENE_Mask m = rf_mask_of(rf_picture_of(&c, pl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
    const GSHSCENE_Resource * mask = rf_mask_intern(&c, &m);
    GSHSCENE_List * small = rf_open(&c);
    rf_fill(&c, small, rf_rect(&c, 0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3, 0.8, 1.0), 0, 0.0);
    const GSHSCENE_Resource * inner = rf_pattern(&c, rf_picture_of(&c, small), 0, 0, 4, 4, 4, 4, 1, 1, rf_move(0.5, 0.5), nullptr);
    GSHSCENE_List * t = rf_open(&c);
    rf_fill(&c, t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.6, 1.0), 0, 0.0);
    if (kind == 0) {
      rf_masked(&c, t, mask, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, nullptr, rf_rect(&c, 1, 1, 7, 7), rf_solid(0.8, 0.1, 0.2, 1.0));
    } else if (kind == 1) {
      rf_fill(&c, t, rf_rect(&c, 1, 1, 7, 7), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(inner, 1.0), 0, 0.0);
    } else {
      GSHSCENE_List * in2 = rf_open(&c);
      if (kind == 2) {
        rf_masked(&c, in2, mask, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, nullptr, rf_rect(&c, 1, 1, 7, 7), rf_solid(0.8, 0.1, 0.2, 1.0));
      } else {
        rf_fill(&c, in2, rf_rect(&c, 1, 1, 7, 7), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(inner, 1.0), 0, 0.0);
      }
      GSHGEOM_Map at = rf_move(0.0, 0.0);
      rf_check(&c, gshscene_list_instance(t, rf_picture_of(&c, in2), &at, 0, 0.0) == GSHSCENE_OK);
    }
    const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.25, 0.5), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink on(kAll), off(kNoPeriod);
    Out a = draw(on.sink, l, 72, 56);
    Out b = draw(off.sink, l, 72, 56);
    EXPECT_EQ(a.bytes, b.bytes) << kind;
    if (kind == 0 || kind == 2) {
      EXPECT_EQ(on.counters.pattern_period, 0u) << "a mask in the tile: the tier is not taken (kind " << kind << ")";
    } else {
      EXPECT_GT(on.counters.patterns, 6u) << "the outer pattern is drawn copy by copy, each with a pass of the inner one (kind " << kind << ")";
    }
    rf_release(&c);
  }
}

namespace {

/** The per-limit result of a sweep: whether the period tier was given up, whether it drew, and whether the use was refused, for each value from @p from. */
struct Profile {
  std::vector<int> value, aborted, taken, refused;
};

/** Where a sweep puts its pattern and how big the target it fills is. */
struct Geo {
  double cx = 0.0, cy = 0.0;     /* the cell's origin in the pattern's space */
  double tx = 0.3, ty = 0.45;    /* the pattern's translation */
  double step = 24.0;            /* the steps (the cell is 24 pixels) */
  uint32_t w = 72, h = 56;       /* the target, which the use fills */
};

/** One 24-pixel cell of @p unit content, filling a target with the tier on and off at a limit set by @p set; the two must agree in everything. */
template <typename Build, typename Set>
void sweep(Build build, Set set, int from, int to, int step, uint64_t * gave_up, uint64_t * taken, uint64_t * refused, const Geo & geo = Geo(),
    Profile * profile = nullptr) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * tile = build(&c);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, geo.cx, geo.cy, 24, 24, geo.step, geo.step, 1, 1, rf_move(geo.tx, geo.ty), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, geo.w, geo.h), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  for (int v = from; v <= to; v += step) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    set(&limits, v);
    TSink on(kAll, &limits), off(kNoPeriod, &limits);
    Out a = draw(on.sink, l, geo.w, geo.h);
    Out b = draw(off.sink, l, geo.w, geo.h);
    ASSERT_EQ(a.result, b.result) << v;
    ASSERT_EQ(a.bytes, b.bytes) << "limit " << v;
    ASSERT_EQ(a.refused, b.refused) << v;
    ASSERT_EQ(a.entries, b.entries) << v;
    *gave_up += on.counters.period_aborts > 0;
    *taken += on.counters.pattern_period > 0;
    *refused += a.refused > 0;
    if (profile) {
      profile->value.push_back(v);
      profile->aborted.push_back(on.counters.period_aborts > 0);
      profile->taken.push_back(on.counters.pattern_period > 0);
      profile->refused.push_back(a.refused > 0);
    }
  }
  rf_release(&c);
}

} // namespace

TEST(RenderPatternKeys, ALimitIsTheSameUnderBothTiersAtEveryValueOfTheLimit) {
  // PC8, PC9: the window's copies are cut by the region and a whole copy is not, so a limit between what they need and what a whole copy needs
  // would be met by the general tier and not by a window that drew only its own copies; the give-up is what makes the two agree, at every value
  // of the limit, for a fill (the cell deposits), a stroke (the deposits of its parts), an image's work and a gradient's domain.
  uint64_t gave_up = 0, taken = 0, refused = 0;
  sweep([](RF_Ctx * c) {
    GSHSCENE_List * t = rf_open(c);
    rf_fill(c, t, rf_rect(c, 0, 0, 24, 24), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.5, 1.0), 0, 0.0);
    rf_fill(c, t, rf_rect(c, 3.5, 3.5, 20.5, 20.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.6, 0.8), 0, 0.0);
    return rf_picture_of(c, t);
  }, [](GSHCPU_Limits * l, int v) { l->max_deposits = (uint64_t)v; }, 1, 400, 1, &gave_up, &taken, &refused);
  EXPECT_GE(gave_up, 1u);
  EXPECT_GE(taken, 1u);
  EXPECT_GE(refused, 1u);
  uint64_t g2 = 0, t2 = 0, r2 = 0;
  sweep([](RF_Ctx * c) {
    GSHSCENE_List * t = rf_open(c);
    const double zig[12] = {-3.0, 6.0, 5.0, 17.5, 12.0, 4.0, 19.0, 21.0, 27.0, 9.0, 30.0, 14.0};
    rf_stroke(c, t, rf_polyline(c, zig, 6, 0), rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), nullptr, rf_solid(0.1, 0.2, 0.6, 0.9));
    const double sq[8] = {4.5, 4.5, 19.5, 4.5, 19.5, 19.5, 4.5, 19.5};
    rf_stroke(c, t, rf_polyline(c, sq, 4, 1),
        rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0, 0.0,
            GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
        nullptr, rf_solid(0.6, 0.1, 0.1, 0.8));
    return rf_picture_of(c, t);
  }, [](GSHCPU_Limits * l, int v) { l->max_deposits = (uint64_t)v; }, 1, 900, 1, &g2, &t2, &r2);
  EXPECT_GE(g2, 1u);
  EXPECT_GE(t2, 1u);
  EXPECT_GE(r2, 1u);
  // An area-filtered image: the work of one pixel, and of the item.
  for (int which = 0; which < 2; which++) {
    uint64_t g3 = 0, t3 = 0, r3 = 0;
    sweep([](RF_Ctx * c) {
      GSHSCENE_List * t = rf_open(c);
      GSHSCENE_Color_Space none;
      std::memset(&none, 0, sizeof none);
      const GSHSCENE_Resource * img = rf_image(c, 64, 48, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
      GSHSCENE_Paint ip = rf_image_paint(c, img, rf_place(c, 0.4, 0.4, 0.0, 0.0), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
          GSHSCENE_IMAGE_FILTER_AREA, GSHSCENE_IMAGE_FILTER_AREA, none, nullptr, 1.0);
      rf_fill(c, t, rf_rect(c, 0, 0, 24, 19), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
      return rf_picture_of(c, t);
    }, which == 0 ? [](GSHCPU_Limits * l, int v) { l->max_image_work = (uint64_t)v; } : [](GSHCPU_Limits * l, int v) { l->max_area_pixel_work = (uint64_t)v; },
        which == 0 ? 100 : 1, which == 0 ? 60000 : 90, which == 0 ? 97 : 1, &g3, &t3, &r3);
    std::printf("image which %d: gave_up %llu taken %llu refused %llu\n", which, (unsigned long long)g3, (unsigned long long)t3, (unsigned long long)r3);
    EXPECT_GE(t3 + g3, 1u) << which;
  }
  // A gradient whose map puts the cell's far corner past the sampling domain while a pixel is inside it: the scale is swept in steps of 2%.
  uint64_t g4 = 0, t4 = 0, r4 = 0;
  for (int k = 0; k < 400; k++) {
    const double scale = std::pow(2.0, -52.0 + k * 0.03);
    sweep([scale](RF_Ctx * c) {
      GSHSCENE_List * t = rf_open(c);
      GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
      GSHSCENE_Gradient g = rf_linear(0, 0, 1e15, 0, 0, 0, GSHSCENE_EXTEND_PAD);
      gshgeom_map_scale(2, scale, scale, 1.0, &g.map);
      const GSHSCENE_Resource * grad = rf_gradient(c, g, stops, 2);
      rf_fill(c, t, rf_rect(c, 0, 0, 24, 24), nullptr, GSHGEOM_FILL_NONZERO, rf_gradient_paint(grad, 1.0), 0, 0.0);
      return rf_picture_of(c, t);
    }, [](GSHCPU_Limits *, int) {}, 0, 0, 1, &g4, &t4, &r4);
  }
  std::printf("gradient: gave_up %llu taken %llu refused %llu\n", (unsigned long long)g4, (unsigned long long)t4, (unsigned long long)r4);
  EXPECT_GE(t4, 1u);
}

namespace {

GSHCPU_Period_Key key_of(uint64_t pattern, uint64_t tile) {
  GSHCPU_Period_Key k;
  std::memset(&k, 0, sizeof k);
  k.pattern_id = pattern;
  k.tile_id = tile;
  k.linear[0] = 0x3ff0000000000000ull;
  k.linear[3] = 0x3ff0000000000000ull;
  k.revisions[0] = 1;
  return k;
}

/** A key that outlives the call: one of a ring of eight. */
const GSHCPU_Period_Key * keyp(uint64_t pattern, uint64_t tile) {
  static GSHCPU_Period_Key ring[8];
  static int at = 0;
  GSHCPU_Period_Key & k = ring[at++ % 8];
  k = key_of(pattern, tile);
  return &k;
}

/** An allocator that overwrites what it frees, so that a read of a freed raster is seen. */
void * poison_malloc(void *, size_t n) {
  return std::malloc(n ? n : 1);
}
void * poison_calloc(void *, size_t k, size_t n) {
  return std::calloc(k ? k : 1, n ? n : 1);
}
void * poison_realloc(void *, void * p, size_t n) {
  return std::realloc(p, n ? n : 1);
}
long g_freed = 0;
void poison_free(void *, void * p) {
  g_freed++;
  std::free(p);
}

} // namespace

TEST(RenderPatternKeys, TheTableKeepsRastersByTheirKeysBoundAndRecencyAndNeverFreesAPinnedOne) {
  // PC10: the table's own rules, driven through its functions: a period key and a stroke key never meet the other kind's entries (a zero key matches
  // an entry of the other kind only if the kinds are not told apart), a hit is touched and pinned, a duplicate offered is found, an entry past the
  // bound is refused, the accounting is the raster's bytes and the entry, the least recently used goes first, and a pinned entry is neither evicted
  // nor freed.
  GSHGEOM_Allocator alloc = {nullptr, poison_malloc, poison_calloc, poison_realloc, poison_free};
  GSHCPU_Render_Counters counters{};
  GSHCPU_Stroke_Cache * c = nullptr;
  // Measure the entry: what one pixel costs.
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, 1u << 20, &counters, &c), GSHCPU_OK);
  double * r1 = gshcpu_period_cache_make(c, 1);
  ASSERT_NE(r1, nullptr);
  r1[0] = 1.0;
  void * e1 = nullptr;
  ASSERT_EQ(gshcpu_period_cache_store(c, keyp(7, 8), r1, 1, &e1), 1);
  gshcpu_stroke_cache_release(c, e1);
  const uint64_t one = gshcpu_stroke_cache_bytes(c);
  double * r2 = gshcpu_period_cache_make(c, 2);
  r2[0] = 2.0;
  void * e2 = nullptr;
  const GSHCPU_Period_Key k2 = key_of(9, 8);
  ASSERT_EQ(gshcpu_period_cache_store(c, &k2, r2, 2, &e2), 1);
  gshcpu_stroke_cache_release(c, e2);
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c) - one, one + 32u) << "one more pixel is 32 bytes of the accounting, four doubles";
  EXPECT_GT(one, 32u) << "the entry itself is accounted";
  EXPECT_EQ(counters.period_kept, 2u);
  // A duplicate offered is found, not stored twice.
  double * dup = gshcpu_period_cache_make(c, 1);
  void * ed = nullptr;
  const uint64_t before = gshcpu_stroke_cache_bytes(c);
  EXPECT_EQ(gshcpu_period_cache_store(c, keyp(7, 8), dup, 1, &ed), 2);
  gshcpu_stroke_cache_release(c, ed);
  gshcpu_period_cache_free(c, dup);
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c), before);
  // Evicting by the tile's id drops both, by the pattern's id one.
  gshcpu_stroke_cache_evict(c, 9);
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c), one);
  gshcpu_stroke_cache_evict(c, 8);
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c), 0u);
  gshcpu_stroke_cache_destroy(c);

  // The bound: an entry whose need is past it is not kept and `fits` says so; one whose need is exactly it is kept.
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, one - 1, &counters, &c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_period_cache_fits(c, 1), 0);
  double * big = gshcpu_period_cache_make(c, 1);
  void * eb = nullptr;
  EXPECT_EQ(gshcpu_period_cache_store(c, keyp(1, 2), big, 1, &eb), 0);
  gshcpu_period_cache_free(c, big);
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c), 0u);
  gshcpu_stroke_cache_destroy(c);
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, one, &counters, &c), GSHCPU_OK);
  EXPECT_EQ(gshcpu_period_cache_fits(c, 1), 1);
  EXPECT_EQ(gshcpu_period_cache_fits(c, 2), 0);
  double * fit = gshcpu_period_cache_make(c, 1);
  EXPECT_EQ(gshcpu_period_cache_store(c, keyp(1, 2), fit, 1, &eb), 1);
  gshcpu_stroke_cache_release(c, eb);
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c), one);
  gshcpu_stroke_cache_destroy(c);

  // Recency and pins: room for two; A and B stored, A read (touched), C stored: B goes, A stays.  A pinned entry is not evicted to make room.
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, 2 * one + 8, &counters, &c), GSHCPU_OK);
  const GSHCPU_Period_Key ka = key_of(11, 1), kb = key_of(12, 1), kc = key_of(13, 1);
  void * en = nullptr;
  for (const GSHCPU_Period_Key * k : {&ka, &kb}) {
    double * r = gshcpu_period_cache_make(c, 1);
    r[0] = 5.0;
    ASSERT_EQ(gshcpu_period_cache_store(c, k, r, 1, &en), 1);
    gshcpu_stroke_cache_release(c, en);
  }
  const double * raster = nullptr;
  ASSERT_EQ(gshcpu_period_cache_acquire(c, &ka, &en, &raster), 1);
  gshcpu_stroke_cache_release(c, en);
  double * rc = gshcpu_period_cache_make(c, 1);
  rc[0] = 6.0;
  ASSERT_EQ(gshcpu_period_cache_store(c, &kc, rc, 1, &en), 1);
  gshcpu_stroke_cache_release(c, en);
  EXPECT_EQ(gshcpu_period_cache_acquire(c, &kb, &en, &raster), 0) << "the least recently used went";
  ASSERT_EQ(gshcpu_period_cache_acquire(c, &ka, &en, &raster), 1) << "the one just read stayed";
  // A is pinned now: storing D needs room, and everything older than D that is not pinned is C only.
  const GSHCPU_Period_Key kd = key_of(14, 1);
  double * rd = gshcpu_period_cache_make(c, 1);
  void * ed2 = nullptr;
  EXPECT_EQ(gshcpu_period_cache_store(c, &kd, rd, 1, &ed2), 1);
  gshcpu_stroke_cache_release(c, ed2);
  EXPECT_EQ(gshcpu_period_cache_acquire(c, &kc, &ed2, &raster), 0) << "the unpinned one made room";
  // The pinned raster survives an eviction of its id, intact, and is freed by the release.
  const long freed_before = g_freed;
  gshcpu_stroke_cache_evict(c, 11);
  EXPECT_EQ(raster == nullptr, false);
  const double * pinned = nullptr;
  EXPECT_EQ(gshcpu_period_cache_acquire(c, &ka, &ed2, &pinned), 0) << "unlinked";
  EXPECT_EQ(g_freed, freed_before) << "but not freed while pinned";
  gshcpu_stroke_cache_release(c, en);
  EXPECT_GT(g_freed, freed_before) << "freed by the last release";
  gshcpu_stroke_cache_destroy(c);

  // A pinned entry is not evicted to make room (room for one).
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, one, &counters, &c), GSHCPU_OK);
  double * r_a = gshcpu_period_cache_make(c, 1);
  ASSERT_EQ(gshcpu_period_cache_store(c, &ka, r_a, 1, &en), 1); // pinned by the store
  double * r_b = gshcpu_period_cache_make(c, 1);
  void * e_b = nullptr;
  EXPECT_EQ(gshcpu_period_cache_store(c, &kb, r_b, 1, &e_b), 0) << "nothing unpinned to drop: not kept";
  gshcpu_period_cache_free(c, r_b);
  gshcpu_stroke_cache_release(c, en);
  ASSERT_EQ(gshcpu_period_cache_acquire(c, &ka, &en, &raster), 1);
  gshcpu_stroke_cache_release(c, en);
  gshcpu_stroke_cache_destroy(c);
}

TEST(RenderPatternKeys, AStrokeKeyAndAPeriodKeyOfZerosNeverMeetTheOtherKindsEntries) {
  // PC10: one table holds two kinds of entry; an all-zero key of either kind must not be found among the other's (their keys are zero in the
  // fields they do not use).
  GSHGEOM_Allocator alloc = {nullptr, poison_malloc, poison_calloc, poison_realloc, poison_free};
  GSHCPU_Render_Counters counters{};
  GSHCPU_Stroke_Cache * c = nullptr;
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, 1u << 20, &counters, &c), GSHCPU_OK);
  GSHCPU_Period_Key zero;
  std::memset(&zero, 0, sizeof zero);
  double * r = gshcpu_period_cache_make(c, 1);
  void * e = nullptr;
  ASSERT_EQ(gshcpu_period_cache_store(c, &zero, r, 1, &e), 1);
  gshcpu_stroke_cache_release(c, e);
  GSHCPU_Stroke_Key sk;
  std::memset(&sk, 0, sizeof sk);
  GSHGEOM_Stroke_Outline * outline = nullptr;
  GSHCPU_Stroke_Meta meta;
  EXPECT_EQ(gshcpu_stroke_cache_acquire(c, &sk, &e, &outline, &meta), 0) << "a stroke key of zeros finds no period raster";
  // A stroke entry, and a period key of zeros against it.
  GSHGEOM_Path * path = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &path), GSHGEOM_OK);
  GSHGEOM_Point a = {0, 0, 0}, b = {5, 0, 0};
  gshgeom_path_move_to(path, a);
  gshgeom_path_line_to(path, b);
  GSHGEOM_Stroke st;
  std::memset(&st, 0, sizeof st);
  gshgeom_stroke_init(&st);
  st.width = 1.0;
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  GSHGEOM_Stroke_Outline * out = nullptr;
  ASSERT_EQ(gshgeom_stroke_expand(path, &st, &id, nullptr, nullptr, &out), GSHGEOM_OK);
  gshgeom_path_destroy(path);
  std::memset(&meta, 0, sizeof meta);
  meta.bytes = 100;
  GSHCPU_Stroke_Key sk2;
  std::memset(&sk2, 0, sizeof sk2);
  sk2.path_id = 5;
  void * se = nullptr;
  GSHGEOM_Stroke_Outline * held = nullptr;
  ASSERT_EQ(gshcpu_stroke_cache_store(c, &sk2, out, &meta, &se, &held), 1);
  gshcpu_stroke_cache_release(c, se);
  gshcpu_stroke_cache_evict(c, 0); // the period entry's ids are 0: it goes; the stroke entry (ids 5 and 0) goes too
  EXPECT_EQ(gshcpu_stroke_cache_bytes(c), 0u);
  gshcpu_stroke_cache_destroy(c);
  // The other way: a stroke entry of key zeros, then a period key of zeros.
  ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, 1u << 20, &counters, &c), GSHCPU_OK);
  GSHGEOM_Stroke_Outline * out2 = nullptr;
  GSHGEOM_Path * path2 = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &path2), GSHGEOM_OK);
  gshgeom_path_move_to(path2, a);
  gshgeom_path_line_to(path2, b);
  ASSERT_EQ(gshgeom_stroke_expand(path2, &st, &id, nullptr, nullptr, &out2), GSHGEOM_OK);
  gshgeom_path_destroy(path2);
  ASSERT_EQ(gshcpu_stroke_cache_store(c, &sk, out2, &meta, &se, &held), 1);
  gshcpu_stroke_cache_release(c, se);
  const double * raster = nullptr;
  EXPECT_EQ(gshcpu_period_cache_acquire(c, &zero, &e, &raster), 0) << "a period key of zeros finds no stroke expansion";
  gshcpu_stroke_cache_destroy(c);
}

TEST(RenderPatternKeys, AKeyOfOneKindIsNotFoundAmongTheOtherKindsEntriesWhereTheirBucketsMeet) {
  // PC10: a stroke key is compared with the entries of its bucket, and a period key likewise; the two kinds hash differently, so an entry of the
  // other kind sits in the same bucket only by chance.  Where it does, the other kind's key (zero in the fields the entry's kind does not use) must
  // still not match: with 256 buckets, a couple of thousand entries of one kind meet the lookup of the other kind's zero key in a few of them.
  GSHGEOM_Allocator alloc = {nullptr, poison_malloc, poison_calloc, poison_realloc, poison_free};
  GSHCPU_Render_Counters counters{};
  // Period entries, and the stroke key of zeros.
  {
    GSHCPU_Stroke_Cache * c = nullptr;
    ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, 1u << 28, &counters, &c), GSHCPU_OK);
    GSHCPU_Stroke_Key sk;
    std::memset(&sk, 0, sizeof sk);
    for (uint64_t n = 1; n <= 3000; n++) {
      GSHCPU_Period_Key k;
      std::memset(&k, 0, sizeof k);
      k.pattern_id = n;
      double * r = gshcpu_period_cache_make(c, 1);
      ASSERT_NE(r, nullptr);
      void * e = nullptr;
      ASSERT_EQ(gshcpu_period_cache_store(c, &k, r, 1, &e), 1);
      gshcpu_stroke_cache_release(c, e);
      GSHGEOM_Stroke_Outline * outline = nullptr;
      GSHCPU_Stroke_Meta meta;
      void * found = nullptr;
      ASSERT_EQ(gshcpu_stroke_cache_acquire(c, &sk, &found, &outline, &meta), 0) << "after period entry " << n;
    }
    gshcpu_stroke_cache_destroy(c);
  }
  // Stroke entries, and the period key of zeros.
  {
    GSHCPU_Stroke_Cache * c = nullptr;
    ASSERT_EQ(gshcpu_stroke_cache_create(&alloc, 1u << 28, &counters, &c), GSHCPU_OK);
    GSHCPU_Period_Key zero;
    std::memset(&zero, 0, sizeof zero);
    GSHGEOM_Path * path = nullptr;
    ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &path), GSHGEOM_OK);
    GSHGEOM_Point a = {0, 0, 0}, b = {5, 0, 0};
    gshgeom_path_move_to(path, a);
    gshgeom_path_line_to(path, b);
    GSHGEOM_Stroke st;
    gshgeom_stroke_init(&st);
    st.width = 1.0;
    GSHGEOM_Map id;
    gshgeom_map_identity(2, &id);
    for (uint64_t n = 1; n <= 3000; n++) {
      GSHGEOM_Stroke_Outline * out = nullptr;
      ASSERT_EQ(gshgeom_stroke_expand(path, &st, &id, nullptr, nullptr, &out), GSHGEOM_OK);
      GSHCPU_Stroke_Meta meta;
      std::memset(&meta, 0, sizeof meta);
      meta.bytes = 100;
      GSHCPU_Stroke_Key sk;
      std::memset(&sk, 0, sizeof sk);
      sk.path_id = n;
      void * se = nullptr;
      GSHGEOM_Stroke_Outline * held = nullptr;
      ASSERT_EQ(gshcpu_stroke_cache_store(c, &sk, out, &meta, &se, &held), 1);
      gshcpu_stroke_cache_release(c, se);
      void * e = nullptr;
      const double * raster = nullptr;
      ASSERT_EQ(gshcpu_period_cache_acquire(c, &zero, &e, &raster), 0) << "after stroke entry " << n;
    }
    gshgeom_path_destroy(path);
    gshcpu_stroke_cache_destroy(c);
  }
}

namespace {

/** The first value of a profile at which @p which holds, or -1. */
int first_where(const Profile & pr, const std::vector<int> & which) {
  for (size_t i = 0; i < pr.value.size(); i++) {
    if (which[i]) {
      return pr.value[i];
    }
  }
  return -1;
}

std::vector<int> negated(const std::vector<int> & v) {
  std::vector<int> n(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    n[i] = !v[i];
  }
  return n;
}

} // namespace

TEST(RenderPatternKeys, TheWindowIsGivenUpExactlyWhereAWholeCellCouldMeetTheLimitTheGeneralTierMeets) {
  // PC9: for a tile that is one 24-pixel square at a fraction of (0.3, 0.45), the cell's cut outline is the square and its cells are bounded, from
  // its edges alone, by the rows each edge crosses and the columns it spans (cover.c's edge_bound: R + min(span + R, R * W)).  Its two vertical edges
  // cross 25 rows each (0.45 to 24.45) and span no column, 25 + min(0 + 25, 25 * 25) = 50 each, and its horizontal edges cross no row: the bound is
  // 100.  The general tier counts what the copies deposit, which here is 56.  The window is given up for every limit below the bound, and the
  // general tier decides, so for the limits from 56 to 99 the picture is drawn by it (the sweep checks the bytes); from 100 the window is drawn.
  // The tile is also placed 100 pixels away in the pattern's space, with the translation that brings it back: the cell the bound is measured over is
  // the cell's pixels in the frame of the copies, not the target's, and the profile is the same.
  for (int far = 0; far < 2; far++) {
    Profile pr;
    uint64_t g = 0, t = 0, r = 0;
    const double o = far ? 100.0 : 0.0;
    sweep([o](RF_Ctx * c) {
      GSHSCENE_List * tl = rf_open(c);
      rf_fill(c, tl, rf_rect(c, o, o, o + 24, o + 24), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.5, 1.0), 0, 0.0);
      return rf_picture_of(c, tl);
    }, [](GSHCPU_Limits * l, int v) { l->max_deposits = (uint64_t)v; }, 1, 200, 1, &g, &t, &r, Geo{o, o, far ? -99.7 : 0.3, far ? -99.55 : 0.45}, &pr);
    EXPECT_EQ(first_where(pr, negated(pr.refused)), 56) << "the general tier needs 56 deposits, far " << far;
    EXPECT_EQ(first_where(pr, pr.aborted), 56) << "given up from the first limit the general tier meets, far " << far;
    EXPECT_EQ(first_where(pr, pr.taken), 100) << "and taken at the bound, not before and not after, far " << far;
    EXPECT_EQ(g, 44u) << far;
  }
}

TEST(RenderPatternKeys, TheStrokeGuardSumsTheDepositsOfEveryCutPartAndTakesTheWindowAtTheirSum) {
  // PC9: a stroke in a tile is vouched for when the deposits its cut parts can make, summed over the parts (body and aligned part), are within the limit.
  // For one stroke of an inside alignment in a 24-pixel cell (an aligned part, no body) the sum is that of the stroke cut to the cell at the fraction the
  // copies have; the window is taken from exactly that limit.  A center-aligned stroke (a body, no aligned part) is the same with the other term.
  RF_Ctx c;
  rf_begin(&c);
  const double sq[10] = {3.5, 2.5, 20.5, 6.5, 18.5, 21.5, 11.5, 16.5, 4.5, 20.5};
  auto build = [&sq](RF_Ctx * k) {
    GSHSCENE_List * tl = rf_open(k);
    rf_stroke(k, tl, rf_polyline(k, sq, 5, 1),
        rf_style(k, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0, 0.0,
            GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
        nullptr, rf_solid(0.6, 0.1, 0.1, 0.8));
    return tl;
  };
  GSHSCENE_List * once = build(&c);
  rf_close(&c, once);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(once, 0, &item), GSHSCENE_OK);
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  const GSHGEOM_Allocator * alloc = gshgeom_allocator_default();
  const GSHGEOM_Map canon = rf_move(0.3, 0.45);
  GSHCPU_Stroke st;
  ASSERT_EQ(gshcpu_render_stroke_open(nullptr, alloc, &limits, item, &canon, &st), GSHCPU_OK);
  ASSERT_EQ(st.shift_x, 0);
  GSHGEOM_Device_Point quad[4];
  const double cx[4] = {0, 24, 24, 0}, cy[4] = {0, 0, 24, 24};
  for (int k = 0; k < 4; k++) {
    quad[k].x = (int32_t)std::llround((cx[k] + 0.3) * 64.0);
    quad[k].y = (int32_t)std::llround((cy[k] + 0.45) * 64.0);
  }
  ASSERT_EQ(gshcpu_render_stroke_cut(&st, quad, alloc, &limits), GSHCPU_OK);
  ASSERT_TRUE(st.meta.present[GSHCPU_STROKE_ALIGNED]) << "an aligned stroke is its aligned part (and the clip path), not a body";
  const int expected = (int)(st.meta.bound[GSHCPU_STROKE_BODY] + st.meta.bound[GSHCPU_STROKE_ALIGNED]);
  EXPECT_GT(st.meta.bound[GSHCPU_STROKE_ALIGNED], 0u);
  gshcpu_render_stroke_close(nullptr, &st);
  rf_release(&c);
  Profile pr;
  uint64_t g = 0, t = 0, r = 0;
  sweep([&build](RF_Ctx * k) { return rf_picture_of(k, build(k)); }, [](GSHCPU_Limits * l, int v) { l->max_deposits = (uint64_t)v; }, 1, expected + 50, 1, &g, &t, &r, Geo(), &pr);
  EXPECT_EQ(first_where(pr, pr.taken), expected);
  EXPECT_EQ(g, (uint64_t)(expected - first_where(pr, pr.aborted))) << "given up at every limit below the sum from the first at which a copy is drawn at all";
}

TEST(RenderPatternKeys, ALatticeWhoseCopiesOverlapIsDrawnWholeAfterTheWindowIsGivenUp) {
  // PC6, PC9: a lattice of steps smaller than the cell is drawn copy by copy over the use's rectangle; the general tier that follows a give-up draws
  // that rectangle, not the window, whatever the tile holds (a plain fill, or a clip block around a group and a fill after it).
  for (int nest = 0; nest < 2; nest++) {
    Profile pr;
    uint64_t g = 0, t = 0, r = 0;
    sweep([nest](RF_Ctx * c) {
      GSHSCENE_List * tl = rf_open(c);
      if (nest) {
        rf_check(c, gshscene_list_clip_push(tl, rf_rect(c, 1, 1, 23, 23), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
        rf_general(c, tl, 0, 0, 0.9, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
      }
      rf_fill(c, tl, rf_rect(c, 0, 0, 24, 24), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.5, 1.0), 0, 0.0);
      rf_fill(c, tl, rf_rect(c, 3.5, 3.5, 20.5, 20.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.6, 0.8), 0, 0.0);
      if (nest) {
        rf_pop(c, tl);
        rf_check(c, gshscene_list_clip_pop(tl) == GSHSCENE_OK);
        rf_fill(c, tl, rf_rect(c, 9.5, 9.5, 14.5, 14.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.1, 0.1, 1.0), 0, 0.0);
      }
      return rf_picture_of(c, tl);
    }, [](GSHCPU_Limits * l, int v) { l->max_deposits = (uint64_t)v; }, 1, 400, 1, &g, &t, &r, Geo{0.0, 0.0, 0.3, 0.45, 20.0}, &pr);
    EXPECT_GE(g, 1u) << nest;
    EXPECT_GE(t, 1u) << nest;
    EXPECT_GE(r, 1u) << nest;
  }
}

TEST(RenderPatternKeys, AnImagesWorkOverAWholeCellGivesTheWindowUpWhereTheCopiesCutByTheTargetAreAffordable) {
  // PC9: a 24-pixel cell at steps of 12 over a 20 by 20 target: every copy is cut by the target, so what the general tier counts (the work of the
  // pixels drawn) is less than what a whole cell would cost; for the limits between the two the window is given up and the general tier draws, and
  // for the limits above the whole cell's the window is drawn.  The area filter's work of one pixel is bounded in the same way.
  const Geo geo{0.0, 0.0, 0.3, 0.45, 12.0, 20, 20};
  uint64_t g_work = 0, t_work = 0, r_work = 0;
  Profile work;
  auto area_tile = [](RF_Ctx * c) {
    GSHSCENE_List * t = rf_open(c);
    GSHSCENE_Color_Space none;
    std::memset(&none, 0, sizeof none);
    const GSHSCENE_Resource * img = rf_image(c, 64, 48, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
    GSHSCENE_Paint ip = rf_image_paint(c, img, rf_place(c, 0.4, 0.4, 0.0, 0.0), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
        GSHSCENE_IMAGE_FILTER_AREA, GSHSCENE_IMAGE_FILTER_AREA, none, nullptr, 1.0);
    rf_fill(c, t, rf_rect(c, 0, 0, 24, 19), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
    return rf_picture_of(c, t);
  };
  sweep(area_tile, [](GSHCPU_Limits * l, int v) { l->max_image_work = (uint64_t)v; }, 100, 60000, 97, &g_work, &t_work, &r_work, geo, &work);
  EXPECT_GE(g_work, 1u);
  EXPECT_GE(r_work, 1u);
  int given_up_and_drawn = 0;
  for (size_t i = 0; i < work.value.size(); i++) {
    given_up_and_drawn += work.aborted[i] && !work.refused[i];
  }
  EXPECT_GE(given_up_and_drawn, 1) << "limits at which the window is given up and the general tier draws";
  // The other filters have no work to bound: the window is taken at a limit the area filter would refuse.
  uint64_t g_lin = 0, t_lin = 0, r_lin = 0;
  sweep([](RF_Ctx * c) {
    GSHSCENE_List * t = rf_open(c);
    GSHSCENE_Color_Space none;
    std::memset(&none, 0, sizeof none);
    const GSHSCENE_Resource * img = rf_image(c, 64, 48, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
    GSHSCENE_Paint ip = rf_image_paint(c, img, rf_place(c, 0.4, 0.4, 0.0, 0.0), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
        GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_LINEAR, none, nullptr, 1.0);
    rf_fill(c, t, rf_rect(c, 0, 0, 24, 19), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
    return rf_picture_of(c, t);
  }, [](GSHCPU_Limits * l, int v) { l->max_image_work = (uint64_t)v; }, 100, 60000, 997, &g_lin, &t_lin, &r_lin, geo);
  EXPECT_EQ(g_lin, 0u) << "a linear filter has no work to bound: the window is never given up for it";
  EXPECT_GE(t_lin, 1u);
}

TEST(RenderPatternKeys, ACellWhoseCornersAreOutsideAnImagesDomainGivesTheWindowUpWhereTheImageIsDrawnInside) {
  // PC9: an image is sampled within 2^32 image units of its origin (an image is one unit across).  An image placed in a box 1e-5 pixel wide, painted over
  // a fill of 24 by 24 pixels (2.4e6 units) in the corner of a cell 200,000 pixels wide (the cell's far corners are 2e10 units out), is wholly in its
  // domain where it is drawn and not where the cell reaches.  The window cannot vouch for a copy whose cell is not wholly in the domain, so it is given
  // up and the general tier, which tests only what each copy draws, draws the use: the same bytes, one give-up.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  GSHSCENE_Color_Space none;
  std::memset(&none, 0, sizeof none);
  const GSHSCENE_Resource * img = rf_image(&c, 64, 48, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  GSHSCENE_Paint ip = rf_image_paint(&c, img, rf_place(&c, 1e-5, 1e-5, 0.0, 0.0), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
      GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_NEAREST, none, nullptr, 1.0);
  rf_fill(&c, t, rf_rect(&c, 0, 0, 24, 24), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 200000, 24, 12, 12, 1, 1, rf_move(0.3, 0.45), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 20, 20), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  TSink on(kAll), off(kNoPeriod);
  Out a = draw(on.sink, l, 20, 20);
  Out b = draw(off.sink, l, 20, 20);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_EQ(a.refused, b.refused);
  EXPECT_EQ(a.refused, 0u) << "what is drawn is inside the domain";
  EXPECT_EQ(on.counters.period_aborts, 1u);
  EXPECT_EQ(on.counters.pattern_period, 0u);
  rf_release(&c);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
