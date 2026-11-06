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
 * The rules of the pattern code, one by one (masks-images-patterns story R2; render.h, PC3 to PC11): what a copy, a unit, a clip, a stroke,
 * the limits, the period tier's give-up, its key and the table that holds it must each do, told from a plausible wrong by a case built for it.
 * The checks against another way of drawing the same thing are the strongest: a tile whose content stays inside its cell is the same picture as
 * the picture drawn once for each copy as an instance (the lattice adds nothing but positions), and a stroke cut to its cell is the stroke under a
 * clip of the cell.  The plants of tools/check-planted.py (`rp-`) are what these tests answer to.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rh;

namespace {

struct TSink {
  GSHSCENE_Sink * sink = nullptr;
  GSHCPU_Render_Counters counters{};
  explicit TSink(unsigned tiers = GSHCPU_RENDER_TIERS_ALL, const GSHCPU_Limits * limits = nullptr, const GSHSCENE_Allocator * alloc = nullptr) {
    EXPECT_EQ(gshcpu_render_sink_create_tiered(alloc, nullptr, limits, tiers, &counters, nullptr, nullptr, &sink), GSHCPU_OK);
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
  GSHSCENE_Report_Summary summary{};
  size_t entries = 0;
};

Out draw(GSHSCENE_Sink * sink, const GSHSCENE_List * list, uint32_t w, uint32_t h, GSHCPU_Format format = GSHCPU_FORMAT_RGBA8_STRAIGHT,
    uint32_t flags = 0, GSHSCENE_Cancel * cancel = nullptr, const GSHSCENE_Color_Space * target_space = nullptr,
    const GSHSCENE_Rect * damage = nullptr) {
  RasterRef raster(rf_raster(format, w, h));
  unsigned char * px = (unsigned char *)gimg_raster_pixels(raster.raster);
  for (size_t i = 0; i < gimg_raster_stride_bytes(raster.raster) * h; i++) {
    px[i] = (unsigned char)(i * 7 + 3); // a backdrop that shows a pixel nothing wrote
  }
  if (format == GSHCPU_FORMAT_RGBAF32_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED) {
    std::memset(px, 0, gimg_raster_stride_bytes(raster.raster) * h);
  }
  ReportRef report;
  GSHSCENE_Target t = target_of(raster.raster);
  if (target_space) {
    t.blend_space = *target_space;
  }
  t.damage = damage;
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  req.cancel = cancel;
  Out o;
  o.result = gshscene_sink_render(sink, &req);
  o.bytes = raster.bytes();
  o.summary = report.summary();
  o.entries = gshscene_report_count(report.report);
  return o;
}

/** The largest difference of one premultiplied sample in 1/255 (a straight 8-bit color is compared premultiplied). */
double max_levels(const std::string & sa, const std::string & sb, GSHCPU_Format format) {
  const bool wide = format == GSHCPU_FORMAT_RGBAF32_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
  const bool straight = format == GSHCPU_FORMAT_RGBA8_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_STRAIGHT;
  if (sa.size() != sb.size()) {
    return 1e9;
  }
  const size_t sample = wide ? 4u : 1u;
  double worst = 0.0;
  for (size_t i = 0; i + 4 * sample <= sa.size(); i += 4 * sample) {
    double va[4], vb[4];
    for (int k = 0; k < 4; k++) {
      if (wide) {
        float x, y;
        std::memcpy(&x, sa.data() + i + 4u * (size_t)k, 4);
        std::memcpy(&y, sb.data() + i + 4u * (size_t)k, 4);
        va[k] = (double)x * 255.0;
        vb[k] = (double)y * 255.0;
      } else {
        va[k] = (double)(unsigned char)sa[i + (size_t)k];
        vb[k] = (double)(unsigned char)sb[i + (size_t)k];
      }
    }
    for (int k = 0; k < 3 && straight; k++) {
      va[k] = va[k] * va[3] / 255.0;
      vb[k] = vb[k] * vb[3] / 255.0;
    }
    for (int k = 0; k < 4; k++) {
      worst = std::max(worst, std::fabs(va[k] - vb[k]));
    }
  }
  return worst;
}

/** A tile of cell (0, 0, 8, 8) whose content stays 1.5 pixels inside it: variant @p v. */
const GSHSCENE_Resource * interior_tile(RF_Ctx * c, int v) {
  GSHSCENE_List * t = rf_open(c);
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 0.7))};
  switch (v) {
    case 0: // solid and linear gradient
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 4.0), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.8), 0, 0.0);
      rf_fill(c, t, rf_rect(c, 2.0, 4.5, 6.0, 6.5), nullptr, GSHGEOM_FILL_NONZERO,
          rf_gradient_paint(rf_gradient(c, rf_linear(2, 0, 6, 0, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2), 0.9), 0, 0.0);
      break;
    case 1: // a gradient that draws nothing outside its strip (G8): a cone of equal circles
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO,
          rf_gradient_paint(rf_gradient(c, rf_radial(2.5, 2.5, 1.0, 5.5, 5.0, 1.0, GSHSCENE_EXTEND_PAD), stops, 2), 1.0), 0, 0.0);
      rf_fill(c, t, rf_rect(c, 3.0, 5.0, 6.0, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.2, 0.6), 0, 0.0);
      break;
    case 2: { // a clip block, a group around it, and a group inside
      rf_general(c, t, 0, 0, 0.7, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 2.3), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      GSHGEOM_Map at = rf_move(4.0, 4.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK); // an empty block first
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 2.3), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.9, 1.0), 0, 0.0);
      rf_check(c, gshscene_list_clip_push(t, rf_rect(c, 3, 3, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.1, 0.9), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_pop(c, t);
      break;
    }
    case 3: { // two clips in force over a fill, and a second block after the first
      rf_check(c, gshscene_list_clip_push(t, rf_rect(c, 1.5, 1.5, 5.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_check(c, gshscene_list_clip_push(t, rf_rect(c, 2.5, 1.5, 6.5, 5.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.2, 0.5, 0.9), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 5.0, 5.0, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.1, 1.0), 0, 0.0);
      break;
    }
    case 4: { // a stroke inside the cell (centered, inside and outside alignment), a fill over it
      const double line[4] = {2.0, 2.5, 6.0, 2.5};
      rf_stroke(c, t, rf_polyline(c, line, 2, 0), rf_pen(c, 1.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr, rf_solid(0.1, 0.1, 0.5, 1.0));
      const double sq[8] = {2.5, 4.0, 5.5, 4.0, 5.5, 6.0, 2.5, 6.0};
      rf_stroke(c, t, rf_polyline(c, sq, 4, 1),
          rf_style(c, 0.8, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0,
              0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
          nullptr, rf_solid(0.6, 0.1, 0.1, 0.8));
      break;
    }
    case 6: { // a pattern in the tile (the inner lattice follows the copy)
      GSHSCENE_List * in = rf_open(c);
      rf_fill(c, in, rf_rect(c, 0.25, 0.25, 1.75, 1.75), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 1.0), 0, 0.0);
      const GSHSCENE_Resource * inner = rf_pattern(c, rf_picture_of(c, in), 0, 0, 2, 2, 2, 2, 1, 1, rf_move(0.5, 0.25), nullptr);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(inner, 1.0), 0, 0.0);
      break;
    }
    case 7: { // two blocks of clips one after the other, with different footprints, and a fill after each
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 1.2), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      GSHGEOM_Map at = rf_move(3.0, 3.0);
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 1.3), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.2, 1.0), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      GSHGEOM_Map at2 = rf_move(5.0, 5.0);
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 1.4), &at2, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.9, 1.0), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      break;
    }
    case 8: { // an instanced picture, under a map, that holds a clip and a fill
      GSHSCENE_List * in = rf_open(c);
      rf_check(c, gshscene_list_clip_push(in, rf_circle(c, 2.0), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, in, rf_rect(c, -2, -2, 2, 2), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.3, 0.6, 0.2, 0.9), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(in) == GSHSCENE_OK);
      GSHGEOM_Map at = rf_move(3.5, 4.5);
      rf_check(c, gshscene_list_instance(t, rf_picture_of(c, in), &at, 0, 0.0) == GSHSCENE_OK);
      break;
    }
    case 9: { // a stroke under an item map that moves it by whole pixels, and a wide one past the cell (cut)
      const double line[4] = {0.0, 0.0, 4.0, 1.0};
      GSHGEOM_Map at = rf_move(2.0, 3.0);
      rf_stroke(c, t, rf_polyline(c, line, 2, 0), rf_pen(c, 1.2, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_ROUND), &at, rf_solid(0.1, 0.5, 0.5, 1.0));
      break;
    }
    case 5: { // an image paint and the foreground
      GSHSCENE_Color_Space none;
      std::memset(&none, 0, sizeof none);
      const GSHSCENE_Resource * img = rf_image(c, 4, 3, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
      GSHSCENE_Paint ip = rf_image_paint(c, img, rf_place(c, 1.0, 1.0, 1.5, 1.5), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
          GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, nullptr, 1.0);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 5.5, 4.5), nullptr, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
      GSHSCENE_Paint f = rf_solid(0, 0, 0, 0.7);
      f.kind = GSHSCENE_PAINT_FOREGROUND;
      f.rgba[3] = 0.7;
      rf_fill(c, t, rf_rect(c, 3.0, 5.0, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, f, 0, 0.0);
      break;
    }
    case 10: { // a pattern in the tile whose steps (3) are not a divisor of the copies' (8): its lattice has to move with each copy by whole pixels
      GSHSCENE_List * in = rf_open(c);
      rf_fill(c, in, rf_rect(c, 0.25, 0.25, 1.25, 2.0), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 1.0), 0, 0.0);
      const GSHSCENE_Resource * inner = rf_pattern(c, rf_picture_of(c, in), 0, 0, 3, 3, 3, 3, 1, 1, rf_move(0.5, 0.25), nullptr);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(inner, 1.0), 0, 0.0);
      break;
    }
    case 11: { // two clip blocks in a row, each around a group: the second, inside the first's footprint but smaller, must not keep the first's coverage
      GSHGEOM_Map at = rf_move(4.0, 4.0);
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 2.9), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_general(c, t, 0, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.2, 1.0), 0, 0.0);
      rf_pop(c, t);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_check(c, gshscene_list_clip_push(t, rf_rect(c, 5.0, 5.0, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_general(c, t, 0, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.9, 1.0), 0, 0.0);
      rf_pop(c, t);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      break;
    }
    case 12: { // a union clip (CU21): three members, two of which overlap and one of which stands apart, around a solid and a gradient
      static const double tri[6] = {4.0, 3.5, 6.5, 6.25, 3.25, 6.0};
      GSHSCENE_Clip_Member m[3] = {rf_member(rf_rect(c, 1.5, 1.5, 4.5, 4.5), GSHGEOM_FILL_NONZERO, nullptr),
          rf_member(rf_rect(c, 3.0, 2.25, 6.5, 5.0), GSHGEOM_FILL_NONZERO, nullptr), rf_member(rf_polygon(c, tri, 3), GSHGEOM_FILL_NONZERO, nullptr)};
      rf_check(c, gshscene_list_clip_push(t, rf_union(c, m, 3), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.2, 0.3, 0.9), 0, 0.0);
      rf_fill(c, t, rf_rect(c, 2.0, 4.5, 6.0, 6.5), nullptr, GSHGEOM_FILL_NONZERO,
          rf_gradient_paint(rf_gradient(c, rf_linear(2, 0, 6, 0, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2), 0.9), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 5.0, 1.5, 6.5, 2.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.1, 1.0), 0, 0.0);
      break;
    }
    case 13: { // a union clip inside a clip, a group around it, and a second union after it: the coverages multiply, and a block leaves nothing behind
      GSHGEOM_Map at = rf_move(4.0, 4.0);
      rf_check(c, gshscene_list_clip_push(t, rf_circle(c, 2.6), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_general(c, t, 0, 0, 0.8, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
      GSHSCENE_Clip_Member a[2] = {rf_member(rf_rect(c, 1.5, 1.5, 4.25, 6.5), GSHGEOM_FILL_NONZERO, nullptr),
          rf_member(rf_rect(c, 3.5, 3.0, 6.5, 5.5), GSHGEOM_FILL_NONZERO, nullptr)};
      rf_check(c, gshscene_list_clip_push(t, rf_union(c, a, 2), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.9, 1.0), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      rf_pop(c, t);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      GSHSCENE_Clip_Member b[2] = {rf_member(rf_rect(c, 5.0, 5.0, 6.5, 6.5), GSHGEOM_FILL_NONZERO, nullptr),
          rf_member(rf_rect(c, 1.5, 5.5, 3.0, 6.5), GSHGEOM_FILL_NONZERO, nullptr)};
      rf_check(c, gshscene_list_clip_push(t, rf_union(c, b, 2), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.1, 0.9), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      break;
    }
    case 14: { // a union of curves and an even-odd star, one member under a rotation: the members' own rules and maps
      GSHGEOM_Map turn, move, at;
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 25.0, &turn);
      move = rf_move(4.0, 4.0);
      gshgeom_map_compose(&move, &turn, &at);
      GSHSCENE_Clip_Member m[3] = {rf_member(rf_star(c, 4.0, 4.0, 2.4), GSHGEOM_FILL_EVEN_ODD, nullptr),
          rf_member(rf_blob(c, 2.0, 2.25, 1.9), GSHGEOM_FILL_NONZERO, nullptr),
          rf_member(rf_rect(c, -1.25, -0.5, 1.25, 0.5), GSHGEOM_FILL_NONZERO, &at)};
      rf_check(c, gshscene_list_clip_push(t, rf_union(c, m, 3), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.2, 0.85), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      break;
    }
    case 15: { // one fill under a union clip of three members: nothing else in the tile overlaps it
      static const double tri[6] = {4.0, 3.5, 6.5, 6.25, 3.25, 6.0};
      GSHSCENE_Clip_Member m[3] = {rf_member(rf_rect(c, 1.5, 1.5, 4.5, 4.5), GSHGEOM_FILL_NONZERO, nullptr),
          rf_member(rf_rect(c, 3.0, 2.25, 6.5, 5.0), GSHGEOM_FILL_NONZERO, nullptr), rf_member(rf_polygon(c, tri, 3), GSHGEOM_FILL_NONZERO, nullptr)};
      rf_check(c, gshscene_list_clip_push(t, rf_union(c, m, 3), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(c, t, rf_rect(c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.2, 0.3, 0.9), 0, 0.0);
      rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      break;
    }
    default:
      break;
  }
  return rf_picture_of(c, t);
}

/** The pattern, and the list that fills the rectangle (4, 3, 68, 51) with it, against the list that draws one instance of the tile for each copy under a clip of that rectangle. */
struct Twin {
  RF_Ctx ctx;
  GSHSCENE_List * pattern = nullptr;
  GSHSCENE_List * explicit_copies = nullptr;
  Twin(int variant, double step_x, double step_y, double tx, double ty, int outer_clip = 0, double cx = 0.0, double cy = 0.0) {
    rf_begin(&ctx);
    GSHSCENE_Color fg;
    std::memset(&fg, 0, sizeof fg);
    fg.rgba[0] = 0.2;
    fg.rgba[1] = 0.7;
    fg.rgba[2] = 0.3;
    fg.rgba[3] = 1.0;
    const GSHSCENE_Resource * tile = interior_tile(&ctx, variant);
    const GSHSCENE_Resource * pat = rf_pattern(&ctx, tile, cx, cy, 8, 8, step_x, step_y, 1, 1, rf_move(tx, ty), nullptr);
    pattern = rf_open(&ctx);
    rf_check(&ctx, gshscene_list_set_foreground(pattern, &fg) == GSHSCENE_OK);
    rf_fill(&ctx, pattern, rf_rect(&ctx, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.9, 1.0), 0, 0.0);
    if (outer_clip) {
      GSHGEOM_Map at = rf_move(36.0, 28.0);
      rf_check(&ctx, gshscene_list_clip_push(pattern, rf_circle(&ctx, 22.0), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    }
    rf_fill(&ctx, pattern, rf_rect(&ctx, 4, 3, 68, 51), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    if (outer_clip) {
      rf_check(&ctx, gshscene_list_clip_pop(pattern) == GSHSCENE_OK);
    }
    rf_close(&ctx, pattern);
    explicit_copies = rf_open(&ctx);
    rf_check(&ctx, gshscene_list_set_foreground(explicit_copies, &fg) == GSHSCENE_OK);
    rf_fill(&ctx, explicit_copies, rf_rect(&ctx, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.9, 1.0), 0, 0.0);
    if (outer_clip) {
      GSHGEOM_Map at = rf_move(36.0, 28.0);
      rf_check(&ctx, gshscene_list_clip_push(explicit_copies, rf_circle(&ctx, 22.0), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    }
    rf_check(&ctx, gshscene_list_clip_push(explicit_copies, rf_rect(&ctx, 4, 3, 68, 51), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    for (int j = -3; j <= 16; j++) {
      for (int i = -3; i <= 16; i++) {
        const double x = tx + i * step_x, y = ty + j * step_y;
        if (x + 8 < 4 || x > 68 || y + 8 < 3 || y > 51) {
          continue;
        }
        GSHGEOM_Map at = rf_move(x, y);
        rf_check(&ctx, gshscene_list_instance(explicit_copies, tile, &at, 0, 0.0) == GSHSCENE_OK);
      }
    }
    rf_check(&ctx, gshscene_list_clip_pop(explicit_copies) == GSHSCENE_OK);
    if (outer_clip) {
      rf_check(&ctx, gshscene_list_clip_pop(explicit_copies) == GSHSCENE_OK);
    }
    rf_close(&ctx, explicit_copies);
  }
  ~Twin() {
    rf_release(&ctx);
  }
  Twin(const Twin &) = delete;
  Twin & operator=(const Twin &) = delete;
};

} // namespace

TEST(RenderPatternRules, ATileInsideItsCellIsThePictureDrawnOnceForEachCopy) {
  // PC3 to PC6: with nothing at a seam, the lattice adds positions and nothing else.  The tiles hold a gradient that draws nothing in places (G8), clips
  // (two in force at once, a block after a block, a clip inside a group), groups, strokes of every alignment and an image: every bookkeeping of a unit's
  // accumulator, its clips, its shifts and its weights is told from a wrong one by the pixels.
  const double lattices[4][4] = {{8, 8, 0.3, 0.45}, {10, 9, 0.7, 0.2}, {6, 6, 0.25, 0.5}, {8, 8, 0.0, 0.0}};
  for (int variant = 0; variant < 16; variant++) {
    for (const auto & l : lattices) {
      Twin tw(variant, l[0], l[1], l[2], l[3]);
      for (GSHCPU_Format format : {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED}) {
        TSink a(kAll), b(kAll);
        Out p = draw(a.sink, tw.pattern, 72, 56, format);
        Out e = draw(b.sink, tw.explicit_copies, 72, 56, format);
        ASSERT_EQ(p.result, GSHSCENE_OK);
        ASSERT_EQ(e.result, GSHSCENE_OK);
        EXPECT_LE(max_levels(p.bytes, e.bytes, format), format == GSHCPU_FORMAT_RGBA8_STRAIGHT ? 1.0 : 0.02)
            << "variant " << variant << " lattice " << l[0] << "," << l[1] << " at " << l[2] << "," << l[3] << " " << gshcpu_format_string(format);
      }
    }
  }
}

TEST(RenderPatternRules, ZeroCopiesLeaveTheTargetAsItWas) {
  // PC8: a use none of whose copies meets the region draws nothing and writes nothing: the backdrop's bytes are the target's.
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * tile = interior_tile(&c, 0);
  const GSHSCENE_Resource * far = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 0, 0, rf_move(500.0, 500.0), nullptr); // one copy, far off
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 4, 3, 40, 30), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(far, 1.0), 0, 0.0);
  rf_close(&c, l);
  GSHSCENE_List * empty = rf_open(&c);
  rf_close(&c, empty);
  for (GSHCPU_Format format : RF_FORMATS) {
    TSink a(kAll), b(kAll);
    Out p = draw(a.sink, l, 72, 56, format);
    Out e = draw(b.sink, empty, 72, 56, format);
    EXPECT_EQ(p.result, GSHSCENE_OK);
    EXPECT_EQ(p.bytes, e.bytes) << gshcpu_format_string(format);
  }
  rf_release(&c);
}

TEST(RenderPatternRules, NestedCopiesAreCountedAndSummedAtEveryLevelAndEachUseStartsAtZero) {
  // PC8: the copies of a use are counted by LT1 at every level of nesting and summed.  The outer use is the rectangle [0, 16]^2 of 8-pixel cells at the
  // origin: 4 x 4 cells meet it (the touching ones too), four of them (the cells [0, 16]) are drawn, and in each the inner use is the rectangle
  // [1, 7]^2 of 2-pixel cells: 4 x 4.  So 16 + 4 * 16 = 80 copies are counted, and 80 is the limit that passes and 79 the first that refuses.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * inner_tile = rf_open(&c);
  rf_fill(&c, inner_tile, rf_rect(&c, 0.25, 0.25, 1.75, 1.75), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * inner = rf_pattern(&c, rf_picture_of(&c, inner_tile), 0, 0, 2, 2, 2, 2, 1, 1, rf_move(0, 0), nullptr);
  GSHSCENE_List * outer_tile = rf_open(&c);
  rf_fill(&c, outer_tile, rf_rect(&c, 1, 1, 7, 7), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(inner, 1.0), 0, 0.0);
  const GSHSCENE_Resource * outer = rf_pattern(&c, rf_picture_of(&c, outer_tile), 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0, 0), nullptr);
  GSHSCENE_List * nested = rf_open(&c);
  rf_fill(&c, nested, rf_rect(&c, 0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(outer, 1.0), 0, 0.0);
  rf_close(&c, nested);
  for (uint64_t limit : {uint64_t(15), uint64_t(16), uint64_t(17), uint64_t(20), uint64_t(40), uint64_t(79), uint64_t(80), uint64_t(81), uint64_t(1000)}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_pattern_cells = limit;
    TSink s(kAll, &limits);
    Out o = draw(s.sink, nested, 16, 16);
    EXPECT_EQ(o.summary.by_class[GSHSCENE_ENTRY_REFUSED], limit < 80 ? 1u : 0u) << limit;
  }
  // Two uses in one list: each starts at zero, so a limit of exactly one use's count passes both.
  GSHSCENE_List * two = rf_open(&c);
  rf_fill(&c, two, rf_rect(&c, 0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(outer, 1.0), 0, 0.0);
  rf_fill(&c, two, rf_rect(&c, 0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(outer, 0.5), 0, 0.0);
  rf_close(&c, two);
  GSHCPU_Limits exact;
  gshcpu_limits_default(&exact);
  exact.max_pattern_cells = 80;
  TSink s2(kAll, &exact);
  Out o2 = draw(s2.sink, two, 16, 16);
  EXPECT_EQ(o2.summary.by_class[GSHSCENE_ENTRY_REFUSED], 0u);
  // A limit past 2^53 (and "no limit") is the same as a large one: the lattice enumeration's own range is not the sink's limit.
  for (uint64_t limit : {uint64_t(1) << 60, UINT64_MAX}) {
    GSHCPU_Limits big;
    gshcpu_limits_default(&big);
    big.max_pattern_cells = limit;
    TSink sb(kAll, &big);
    Out ob = draw(sb.sink, nested, 16, 16);
    EXPECT_EQ(ob.summary.by_class[GSHSCENE_ENTRY_REFUSED], 0u) << limit;
    EXPECT_EQ(ob.result, GSHSCENE_OK);
  }
  rf_release(&c);
}

TEST(RenderPatternRules, NoLimitMeansNoLimitForMoreThanAMillionCopies) {
  // PC8: UINT64_MAX is no cap: 2^20 + 1 copies of a one-row lattice are counted and drawn.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, rf_rect(&c, 0, 0, 1.0 / 64.0, 1), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * p = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 1.0 / 64.0, 1, 1.0 / 64.0, 1, 1, 0, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 16385, 1), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(p, 1.0), 0, 0.0);
  rf_close(&c, l);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_pattern_cells = UINT64_MAX;
  limits.max_dimension = 20000;
  TSink s(kAll, &limits);
  Out o = draw(s.sink, l, 16385, 1);
  EXPECT_EQ(o.result, GSHSCENE_OK);
  EXPECT_EQ(o.summary.by_class[GSHSCENE_ENTRY_REFUSED], 0u);
  rf_release(&c);
}

/** An allocator that sets @p cancel at its @p at-th allocation and counts every call. */
struct CancelAlloc {
  GSHSCENE_Cancel * cancel;
  long at;
  long calls = 0;
  long after = 0;
  bool fired = false;
};

namespace {
void * ca_malloc(void * ctx, size_t n) {
  CancelAlloc * a = (CancelAlloc *)ctx;
  a->calls++;
  if (a->fired) {
    a->after++;
  } else if (a->calls == a->at) {
    gshscene_cancel_set(a->cancel);
    a->fired = true;
  }
  return std::malloc(n ? n : 1);
}
void * ca_calloc(void * ctx, size_t k, size_t n) {
  CancelAlloc * a = (CancelAlloc *)ctx;
  a->calls++;
  if (a->fired) {
    a->after++;
  } else if (a->calls == a->at) {
    gshscene_cancel_set(a->cancel);
    a->fired = true;
  }
  return std::calloc(k ? k : 1, n ? n : 1);
}
void * ca_realloc(void * ctx, void * p, size_t n) {
  (void)ctx;
  return std::realloc(p, n ? n : 1);
}
void ca_free(void * ctx, void * p) {
  (void)ctx;
  std::free(p);
}
} // namespace

TEST(RenderPatternRules, ACancelInsideAPassStopsBeforeTheNextCopyAndTheWorkIsCounted) {
  // PC11: cancel is checked before every copy of every unit, so a token set in the middle of a pass stops it within a copy, not after the whole
  // lattice; and the pass's work (each unit's pixels, the layer's apply) is in the report's `work_max`.
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * tile = interior_tile(&c, 0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.3, 0.45), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 200, 150), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  {
    TSink plain(kNoPeriod);
    Out o = draw(plain.sink, l, 200, 150);
    ASSERT_EQ(o.result, GSHSCENE_OK);
    EXPECT_GE(o.summary.work_max, 200u * 150u) << "the pass's pixels are counted";
  }
  long full = 0;
  {
    CancelAlloc counting{nullptr, 1L << 40};
    GSHSCENE_Allocator al = {&counting, ca_malloc, ca_calloc, ca_realloc, ca_free};
    TSink s(kNoPeriod, nullptr, &al);
    draw(s.sink, l, 200, 150);
    full = counting.calls;
  }
  ASSERT_GT(full, 200);
  GSHSCENE_Cancel cancel;
  gshscene_cancel_init(&cancel);
  CancelAlloc ca{&cancel, full / 4};
  GSHSCENE_Allocator al = {&ca, ca_malloc, ca_calloc, ca_realloc, ca_free};
  TSink s(kNoPeriod, nullptr, &al);
  Out o = draw(s.sink, l, 200, 150, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, &cancel);
  EXPECT_EQ(o.result, GSHSCENE_ERR_CANCELLED);
  EXPECT_LT(ca.after, full / 40) << "the pass went on for " << ca.after << " allocations after the token was set, of " << full;
  rf_release(&c);
}

TEST(RenderPatternRules, AStrokeCutToItsCellIsTheStrokeUnderAClipOfTheCell) {
  // PC5: a stroke that runs past the cell edge (centered, inside and outside aligned, with round, square and butt caps and a join) in a gapped
  // lattice is the stroke under a clip of the cell, to the two engines' rounding: the body, the aligned part and the clip path are each cut.
  struct Case {
    uint32_t align, cap;
    double width;
  };
  const Case cases[] = {{GSHSCENE_STROKE_ALIGN_CENTER, GSHSCENE_STROKE_CAP_BUTT, 2.0}, {GSHSCENE_STROKE_ALIGN_CENTER, GSHSCENE_STROKE_CAP_ROUND, 2.5},
      {GSHSCENE_STROKE_ALIGN_INSIDE, GSHSCENE_STROKE_CAP_SQUARE, 1.5}, {GSHSCENE_STROKE_ALIGN_OUTSIDE, GSHSCENE_STROKE_CAP_BUTT, 1.5}};
  for (const Case & k : cases) {
    RF_Ctx c;
    rf_begin(&c);
    const GSHSCENE_Resource * path;
    {
      const double zig[10] = {-2.0, 3.0, 3.0, 6.5, 6.0, 1.0, 10.0, 7.0, 12.0, 4.0};
      path = rf_polyline(&c, zig, 5, k.align != GSHSCENE_STROKE_ALIGN_CENTER);
    }
    const GSHSCENE_Resource * style = rf_style(&c, k.width, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, k.cap, GSHSCENE_STROKE_DEGENERATE_DRAW,
        nullptr, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, k.align, GSHGEOM_FILL_NONZERO);
    GSHSCENE_List * t = rf_open(&c);
    rf_stroke(&c, t, path, style, nullptr, rf_solid(0.1, 0.2, 0.6, 0.8));
    const GSHSCENE_Resource * tile = rf_picture_of(&c, t);
    const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 12, 12, 1, 1, rf_move(0.25, 0.5), nullptr); // a gap: no copy continues another
    GSHSCENE_List * l1 = rf_open(&c);
    rf_fill(&c, l1, rf_rect(&c, 0, 0, 60, 48), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l1);
    GSHSCENE_List * l2 = rf_open(&c);
    for (int j = -1; j < 5; j++) {
      for (int i = -1; i < 6; i++) {
        const double x = 0.25 + 12.0 * i, y = 0.5 + 12.0 * j;
        GSHGEOM_Map at = rf_move(x, y);
        rf_check(&c, gshscene_list_clip_push(l2, rf_rect(&c, 0, 0, 8, 8), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
        rf_check(&c, gshscene_list_instance(l2, tile, &at, 0, 0.0) == GSHSCENE_OK);
        rf_check(&c, gshscene_list_clip_pop(l2) == GSHSCENE_OK);
      }
    }
    rf_close(&c, l2);
    for (GSHCPU_Format format : {GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED}) {
      TSink a(kAll), b(kAll);
      Out p = draw(a.sink, l1, 60, 48, format);
      Out e = draw(b.sink, l2, 60, 48, format);
      ASSERT_EQ(p.result, GSHSCENE_OK);
      ASSERT_EQ(e.result, GSHSCENE_OK);
      if (std::getenv("RULES_DEBUG") && format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED) {
        int shown = 0;
        for (size_t i = 0; i + 4 <= p.bytes.size() && shown < 12; i += 4) {
          if (std::memcmp(p.bytes.data() + i, e.bytes.data() + i, 4) != 0) {
            std::printf("px (%zu,%zu) pattern %02x%02x%02x%02x explicit %02x%02x%02x%02x\n", (i / 4) % 60, (i / 4) / 60, (unsigned char)p.bytes[i], (unsigned char)p.bytes[i + 1],
                (unsigned char)p.bytes[i + 2], (unsigned char)p.bytes[i + 3], (unsigned char)e.bytes[i], (unsigned char)e.bytes[i + 1], (unsigned char)e.bytes[i + 2], (unsigned char)e.bytes[i + 3]);
            shown++;
          }
        }
      }
      EXPECT_LE(max_levels(p.bytes, e.bytes, format), 40.0)
          << "align " << k.align << " cap " << k.cap << " " << gshcpu_format_string(format);
    }
    rf_release(&c);
  }
}


TEST(RenderPatternRules, AnOuterClipWeighsTheFillOnceAndTheCellOriginIsHonored) {
  // PC2: the clips in force around the use weigh the fill and never a copy (so the edge of an outer clip is its coverage once, not squared); and
  // the cell's own origin (x, y) places the cell: a tile whose content straddles a cell that is not at the origin is cut where the cell is.
  for (int variant : {0, 2, 3, 15}) { // 15: one fill under a union clip (two fills that overlap are a group, which the outer clip weighs once: not the explicit copies' item by item)
    Twin tw(variant, 8, 8, 0.3, 0.45, 1);
    TSink a(kAll), b(kAll);
    Out p = draw(a.sink, tw.pattern, 72, 56);
    Out e = draw(b.sink, tw.explicit_copies, 72, 56);
    ASSERT_EQ(p.result, GSHSCENE_OK);
    EXPECT_LE(max_levels(p.bytes, e.bytes, GSHCPU_FORMAT_RGBA8_STRAIGHT), 2.0) << variant;
  }
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, rf_rect(&c, 3, 2, 9, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.3, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_picture_of(&c, t);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 2.0, 1.0, 8, 8, 12, 12, 1, 1, rf_move(1.0, 2.0), nullptr);
  GSHSCENE_List * l1 = rf_open(&c);
  rf_fill(&c, l1, rf_rect(&c, 0, 0, 60, 48), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l1);
  GSHSCENE_List * l2 = rf_open(&c);
  for (int j = -1; j < 5; j++) {
    for (int i = -1; i < 6; i++) {
      GSHGEOM_Map at = rf_move(1.0 + 12.0 * i, 2.0 + 12.0 * j);
      rf_fill(&c, l2, rf_rect(&c, 3, 2, 9, 8), &at, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.3, 0.8, 1.0), 0, 0.0);
    }
  }
  rf_close(&c, l2);
  TSink a(kAll), b(kAll);
  Out p = draw(a.sink, l1, 60, 48);
  Out e = draw(b.sink, l2, 60, 48);
  // the cell [2, 10] x [1, 9] cuts the fill [3, 9] x [2, 8] not at all: the copies are the fills
  // the pass rewrites an uncovered pixel of the use through the layer (a straight pixel of the arbitrary backdrop may move by a level in the round trip), so
  // the bar is one level, as for the twin; a copy a cell off, or a cell cut where it is not, moves a pixel by far more
  EXPECT_LE(max_levels(p.bytes, e.bytes, GSHCPU_FORMAT_RGBA8_STRAIGHT), 1.0);
  rf_release(&c);
}

TEST(RenderPatternRules, AGroupAroundAClipBlockTakesTheUnionOfTheCopiesClips) {
  // PC4: a clip block that reaches the cell's edges, around a group, weighs the group at its pop by the saturating union of its copies' restricted
  // regions: where two cells share a pixel their coverages sum and are held at 255, so the seam is not a hole.  A tile that is only the cell's fill
  // is the reference: the same picture to a rounding.  (A clip block with no group is a mask of its own whose edge multiplies the cell's cut copy by copy,
  // which is the rule for a clip, not for a group, and is not compared.)
  for (double tx : {0.25, 0.3, 0.5, 0.77}) {
    RF_Ctx c;
    rf_begin(&c);
    const GSHSCENE_Resource * tiles[2];
    for (int k = 0; k < 2; k++) {
      GSHSCENE_List * t = rf_open(&c);
      if (k) {
        rf_check(&c, gshscene_list_clip_push(t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
        rf_general(&c, t, 0, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
      }
      rf_fill(&c, t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.5, 0.8, 1.0), 0, 0.0);
      if (k) {
        rf_pop(&c, t);
        rf_check(&c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
      }
      tiles[k] = rf_picture_of(&c, t);
    }
    Out outs[2];
    for (int k = 0; k < 2; k++) {
      const GSHSCENE_Resource * pat = rf_pattern(&c, tiles[k], 0, 0, 8, 8, 8, 8, 1, 1, rf_move(tx, 0.4), nullptr);
      GSHSCENE_List * l = rf_open(&c);
      rf_fill(&c, l, rf_rect(&c, 0, 0, 40, 32), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
      rf_close(&c, l);
      TSink s(kAll);
      outs[k] = draw(s.sink, l, 40, 32);
      ASSERT_EQ(outs[k].result, GSHSCENE_OK);
    }
    EXPECT_LE(max_levels(outs[0].bytes, outs[1].bytes, GSHCPU_FORMAT_RGBA8_STRAIGHT), 3.0) << tx;
    // and the seam is covered: every pixel of the use is opaque
    for (size_t i = 3; i < outs[1].bytes.size(); i += 4) {
      const size_t px = i / 4, x = px % 40, y = px / 40;
      if (x >= 1 && x < 39 && y >= 1 && y < 31) {
        ASSERT_EQ((unsigned char)outs[1].bytes[i], 255) << x << "," << y << " tx " << tx;
      }
    }
    rf_release(&c);
  }
}

TEST(RenderPatternRules, TheCopiesOfAPixelAddTheirAreasAndAWeighedCopyAddsItsWeighedByte) {
  // PC4, F3: the copies of a cell-filling fill add their exact areas, and the byte is made once from the sum.  The lattice sits at (19/64, 29/64)
  // of a pixel, so the pixel at a corner of four cells has the four areas 2 * 19 * 29, 2 * 19 * 35, 2 * 45 * 29 and 2 * 45 * 35 in 1/8192 pixel
  // (1102, 1330, 2610, 3150): bytes 34, 41, 81 and 98 with the remainders 14, 18, 18 and 14, which sum to the whole pixel, 8192, so the pixel is
  // opaque (the sum of the bytes is 254, one short of it, and the sum of the floors was the seam of the fold this replaced).  Under a clip block
  // the same cells weigh each copy by the clip cut to its cell, the same coverage: a byte weighed by a byte, (34 * 34 + 127) / 255 and the
  // others, 5, 7, 26 and 38, and a weighed copy adds that byte and not its area (the remainder is dropped), so the pixel is 76.
  // Variant 2: a group around the fill inside a clip block: the group is weighed at its pop by the clip block's union of the copies' regions,
  // whose areas add as well (PC4), so the corner is opaque there too.
  for (int variant = 0; variant < 3; variant++) {
    const int clipped = variant == 1;
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * t = rf_open(&c);
    if (variant) {
      rf_check(&c, gshscene_list_clip_push(t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    }
    if (variant == 2) {
      rf_general(&c, t, 0, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, nullptr);
    }
    rf_fill(&c, t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.5, 0.8, 1.0), 0, 0.0);
    if (variant == 2) {
      rf_pop(&c, t);
    }
    if (variant) {
      rf_check(&c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
    }
    const GSHSCENE_Resource * tile = rf_picture_of(&c, t);
    const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(19.0 / 64.0, 29.0 / 64.0), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 40, 32), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink s(kAll);
    Out out = draw(s.sink, l, 40, 32, GSHCPU_FORMAT_RGBAF32_STRAIGHT);
    ASSERT_EQ(out.result, GSHSCENE_OK);
    // On a float target over a zero backdrop, the alpha is the weight (shape) over 255.
    for (int x : {8, 16, 24, 32}) {
      float alpha;
      std::memcpy(&alpha, &out.bytes[(size_t)(4 * (8 * 40 + x) + 3) * sizeof(float)], sizeof alpha);
      EXPECT_NEAR(alpha * 255.0f, clipped ? 76.0f : 255.0f, 0.01f) << "variant " << variant << " corner " << x;
    }
    rf_release(&c);
  }
}

TEST(RenderPatternRules, AnEmptyTileStillPaysForTheLayerAndAUseOutsideTheExtentDrawsNothing) {
  // PC11, PC2: the pass's apply over the use's pixels is work even when the tile draws nothing; and a use none of whose pixels is in the extent is
  // skipped without a refusal, the target as it was.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  const GSHSCENE_Resource * empty = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.3, 0.45), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 200, 150), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(empty, 1.0), 0, 0.0);
  rf_close(&c, l);
  TSink s(kNoPeriod);
  Out o = draw(s.sink, l, 200, 150);
  EXPECT_EQ(o.result, GSHSCENE_OK);
  EXPECT_GE(o.summary.work_max, 200u * 150u);
  const GSHSCENE_Resource * tile = interior_tile(&c, 0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * out_of_extent = rf_open(&c);
  rf_fill(&c, out_of_extent, rf_rect(&c, 40, 30, 70, 50), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, out_of_extent);
  GSHSCENE_List * none = rf_open(&c);
  rf_close(&c, none);
  RasterRef ra(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 72, 56)), rb(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 72, 56));
  for (RasterRef * r : {&ra, &rb}) {
    unsigned char * px = (unsigned char *)gimg_raster_pixels(r->raster);
    for (size_t i = 0; i < gimg_raster_stride_bytes(r->raster) * 56; i++) {
      px[i] = (unsigned char)(i * 5 + 1);
    }
  }
  TSink sa(kAll), sb(kAll);
  ReportRef rep;
  GSHSCENE_Target ta = target_of(ra.raster);
  ta.extent.x0 = 0;
  ta.extent.y0 = 0;
  ta.extent.x1 = 30;
  ta.extent.y1 = 25;
  GSHSCENE_Render_Request qa = request_of(out_of_extent, &ta, rep.report);
  EXPECT_EQ(gshscene_sink_render(sa.sink, &qa), GSHSCENE_OK);
  EXPECT_EQ(rep.summary().by_class[GSHSCENE_ENTRY_REFUSED], 0u);
  GSHSCENE_Target tb = target_of(rb.raster);
  tb.extent = ta.extent;
  GSHSCENE_Render_Request qb = request_of(none, &tb, rep.report);
  EXPECT_EQ(gshscene_sink_render(sb.sink, &qb), GSHSCENE_OK);
  EXPECT_EQ(ra.bytes(), rb.bytes());
  rf_release(&c);
}

TEST(RenderPatternRules, AHugeTranslationOrCellIsTheSameRefusalUnderBothTiers) {
  // PC3, PC8: a lattice translated past 2^40 pixels has no canonical position for its copies (RESOURCE_LIMIT), and cells wider than the range the sink draws in, at
  // a step too large to take the tier, are refused for the coordinates they would need; both tiers say the same and draw nothing.
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * tile = interior_tile(&c, 0);
  const GSHSCENE_Resource * far = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(2199023255552.0 * 2.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 40, 30), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(far, 1.0), 0, 0.0);
  rf_close(&c, l);
  GSHSCENE_List * big = rf_open(&c);
  rf_fill(&c, big, rf_rect(&c, 0, 0, 33554432, 33554432), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 0.5), 0, 0.0);
  const GSHSCENE_Resource * wide = rf_pattern(&c, rf_picture_of(&c, big), 0, 0, 33554432, 33554432, 16777216, 16777216, 1, 1, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * l2 = rf_open(&c);
  rf_fill(&c, l2, rf_rect(&c, 0, 0, 40, 30), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(wide, 1.0), 0, 0.0);
  rf_close(&c, l2);
  for (GSHSCENE_List * list : {l, l2}) {
    TSink on(kAll), off(kNoPeriod);
    Out a = draw(on.sink, list, 40, 30);
    Out b = draw(off.sink, list, 40, 30);
    EXPECT_EQ(a.result, b.result);
    EXPECT_EQ(a.bytes, b.bytes);
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], b.summary.by_class[GSHSCENE_ENTRY_REFUSED]);
    EXPECT_GE(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u) << "a limit, reported";
  }
  rf_release(&c);
}

/** An allocator that counts what is live. */
struct LiveAlloc {
  long live = 0;
};

namespace {
void * la_malloc(void * ctx, size_t n) {
  ((LiveAlloc *)ctx)->live++;
  return std::malloc(n ? n : 1);
}
void * la_calloc(void * ctx, size_t k, size_t n) {
  ((LiveAlloc *)ctx)->live++;
  return std::calloc(k ? k : 1, n ? n : 1);
}
void * la_realloc(void * ctx, void * p, size_t n) {
  if (!p) {
    ((LiveAlloc *)ctx)->live++;
  }
  return std::realloc(p, n ? n : 1);
}
void la_free(void * ctx, void * p) {
  if (p) {
    ((LiveAlloc *)ctx)->live--;
  }
  std::free(p);
}
} // namespace

TEST(RenderPatternRules, ARasterReadFromTheTableIsReleasedSoThatAnEvictionFreesIt) {
  // PC10: a hit pins the entry for the copy and releases it; an entry whose pin were never released would survive its eviction and be a leak.
  LiveAlloc live;
  {
    GSHSCENE_Allocator al = {&live, la_malloc, la_calloc, la_realloc, la_free};
    TSink s(kAll, nullptr, &al);
    RF_Ctx c;
    rf_begin(&c);
    const GSHSCENE_Resource * tile = interior_tile(&c, 0);
    const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.25, 0.5), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    draw(s.sink, l, 72, 56);
    draw(s.sink, l, 72, 56);
    EXPECT_GE(s.counters.period_hits, 1u);
    EXPECT_EQ(gshscene_sink_evict(s.sink, gshscene_resource_id(pat)), GSHSCENE_OK);
    uint64_t held = 0;
    gshscene_sink_static_bytes(s.sink, &held);
    EXPECT_EQ(held, 0u);
    rf_release(&c);
  }
  EXPECT_EQ(live.live, 0) << "every byte the sink took is given back";
}

TEST(RenderPatternRules, AStrokeWhollyOutsideItsCellDrawsNothingAndIsNotCounted) {
  // PC5: a stroke that the cell's cut leaves nothing of is dropped before it is covered; it is not a stroke drawn (the counter) and it costs no
  // coverage.  The stroke inside the cell is the control that the counter counts.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * outside = rf_open(&c);
  const double far_line[4] = {20.0, 2.0, 30.0, 2.0};
  rf_stroke(&c, outside, rf_polyline(&c, far_line, 2, 0), rf_pen(&c, 1.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr, rf_solid(0.1, 0.1, 0.5, 1.0));
  GSHSCENE_List * inside = rf_open(&c);
  const double near_line[4] = {2.0, 2.5, 6.0, 2.5};
  rf_stroke(&c, inside, rf_polyline(&c, near_line, 2, 0), rf_pen(&c, 1.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr, rf_solid(0.1, 0.1, 0.5, 1.0));
  GSHSCENE_List * lists[2];
  const GSHSCENE_List * tiles[2] = {outside, inside};
  for (int k = 0; k < 2; k++) {
    const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, const_cast<GSHSCENE_List *>(tiles[k])), 0, 0, 8, 8, 12, 12, 1, 1, rf_move(0.25, 0.5), nullptr);
    lists[k] = rf_open(&c);
    rf_fill(&c, lists[k], rf_rect(&c, 0, 0, 48, 36), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, lists[k]);
  }
  GSHSCENE_List * none = rf_open(&c);
  rf_close(&c, none);
  for (unsigned tiers : {kAll, kNoPeriod, kNoCache}) {
    TSink a(tiers), b(tiers), z(tiers);
    Out o = draw(a.sink, lists[0], 48, 36, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    Out e = draw(z.sink, none, 48, 36, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    EXPECT_EQ(o.result, GSHSCENE_OK);
    EXPECT_EQ(o.bytes, e.bytes) << "tiers " << tiers;
    EXPECT_EQ(a.counters.strokes, 0u) << "tiers " << tiers;
    draw(b.sink, lists[1], 48, 36);
    EXPECT_GE(b.counters.strokes, 1u) << "tiers " << tiers;
  }
  rf_release(&c);
}

TEST(RenderPatternRules, TheUnitsOfAPassPayForTheLayerTheyApply) {
  // PC11: the apply of a unit's element to the lattice layer is work, over the pixels the copies reached.  A tile of one fill of the whole cell, drawn
  // by the general tier over a use of 64 by 64 pixels, has a unit that costs more than the use's own pixels: the pixels, the cells its copies
  // deposit and the points of its outlines.  (The use itself pays its pixels once, and work_max is the most any one unit paid.)
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 64, 64), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  TSink s(kNoPeriod);
  Out o = draw(s.sink, l, 64, 64);
  ASSERT_EQ(o.result, GSHSCENE_OK);
  EXPECT_GT(o.summary.work_max, 64u * 64u + 1000u);
  rf_release(&c);
}

TEST(RenderPatternRules, AStepThatNoCopyUsesIsNotAnOverlap) {
  // PC6: along an axis that does not repeat the step is never used, so a step smaller than the cell there does not make the copies overlap: the use is
  // drawn as the pass over all the copies, not copy by copy.  The two differ at a row the cells of the other axis share (a translucent whole-cell fill
  // at a fractional step: the mean of the two sources against one over the other), so the use with the unused step equals the one with the cell's.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, rf_rect(&c, 0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 0.5), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_picture_of(&c, t);
  GSHSCENE_List * lists[3];
  const double xs[3] = {4.0, 8.0, 2.0};
  for (int k = 0; k < 3; k++) {
    const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, xs[k], 8, 0, 1, rf_move(0.3, 0.5), nullptr);
    lists[k] = rf_open(&c);
    rf_fill(&c, lists[k], rf_rect(&c, 0, 0, 24, 40), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, lists[k]);
  }
  for (unsigned tiers : {kAll, kNoPeriod}) {
    TSink a(tiers), b(tiers), d(tiers);
    Out p4 = draw(a.sink, lists[0], 24, 40, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    Out p8 = draw(b.sink, lists[1], 24, 40, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    Out p2 = draw(d.sink, lists[2], 24, 40, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    ASSERT_EQ(p8.result, GSHSCENE_OK);
    EXPECT_EQ(p4.bytes, p8.bytes) << tiers;
    EXPECT_EQ(p2.bytes, p8.bytes) << tiers;
  }
  rf_release(&c);
}

TEST(RenderPatternRules, ACellsOriginIsItsOwnInEachAxis) {
  // PC2: the cell's x and y place it independently: the cell [5, 13] x [1, 9] cuts a fill that begins at x = 3 and not at y = 2 (a cell origin that took
  // its x from its y would cut the fill at x = 1, where it does not).  Each copy is the fill under a clip of the cell.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, rf_rect(&c, 3, 2, 9, 8), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.3, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_picture_of(&c, t);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 5.0, 1.0, 8, 8, 12, 12, 1, 1, rf_move(1.0, 2.0), nullptr);
  GSHSCENE_List * l1 = rf_open(&c);
  rf_fill(&c, l1, rf_rect(&c, 0, 0, 60, 48), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l1);
  GSHSCENE_List * l2 = rf_open(&c);
  for (int j = -1; j < 5; j++) {
    for (int i = -1; i < 6; i++) {
      GSHGEOM_Map at = rf_move(1.0 + 12.0 * i, 2.0 + 12.0 * j);
      rf_check(&c, gshscene_list_clip_push(l2, rf_rect(&c, 5, 1, 13, 9), &at, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(&c, l2, rf_rect(&c, 3, 2, 9, 8), &at, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.3, 0.8, 1.0), 0, 0.0);
      rf_check(&c, gshscene_list_clip_pop(l2) == GSHSCENE_OK);
    }
  }
  rf_close(&c, l2);
  for (unsigned tiers : {kAll, kNoPeriod}) {
    TSink a(tiers), b(tiers);
    Out p = draw(a.sink, l1, 60, 48);
    Out e = draw(b.sink, l2, 60, 48);
    ASSERT_EQ(p.result, GSHSCENE_OK);
    EXPECT_LE(max_levels(p.bytes, e.bytes, GSHCPU_FORMAT_RGBA8_STRAIGHT), 1.0) << tiers;
  }
  rf_release(&c);
}

TEST(RenderPatternRules, TheWindowMayBeExactlyTheUsesWidthOrHeight) {
  // PC9: the window of an integer lattice (8 by 8 here) is drawn when it fits the use's rectangle, which includes the rectangle that is exactly
  // its width, or exactly its height.
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * tile = interior_tile(&c, 0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.0, 0.0), nullptr);
  const double rects[2][2] = {{8, 16}, {16, 8}};
  for (const auto & r : rects) {
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, r[0], r[1]), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink s(kAll), off(kNoPeriod);
    Out a = draw(s.sink, l, 16, 16);
    Out b = draw(off.sink, l, 16, 16);
    EXPECT_EQ(s.counters.pattern_period, 1u) << r[0] << " by " << r[1];
    EXPECT_EQ(off.counters.pattern_period, 0u);
    EXPECT_EQ(a.bytes, b.bytes);
  }
  rf_release(&c);
}

TEST(RenderPatternRules, ACellPastTheRangeGivesTheWindowUpWhoseCopiesAreInRange) {
  // PC9: a copy whose whole cell lies past the range the scan converter takes cannot be vouched for by the window, which draws a small part of it, so the
  // pass gives the window up and the general tier decides every copy.  A cell 20 million pixels tall, at a step of 3000, has some 6,700 copies that
  // meet a use 16 pixels wide and 3000 tall, each of them drawn over a few pixels; the picture is the general tier's (here the copies far down the
  // cell are in the cell's frame past the range, which is the sink's limit under either tier), and the give-up is counted.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * fills = rf_open(&c);
  rf_fill(&c, fills, rf_rect(&c, 1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.8), 0, 0.0);
  GSHSCENE_List * strokes = rf_open(&c);
  const double line[4] = {2.0, 2.0, 6.0, 5.0};
  rf_stroke(&c, strokes, rf_polyline(&c, line, 2, 0), rf_pen(&c, 1.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr, rf_solid(0.1, 0.1, 0.5, 1.0));
  GSHSCENE_List * tiles[2] = {fills, strokes};
  for (GSHSCENE_List * tile_list : tiles) {
    const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, tile_list), 0, -20000000.0, 8, 20000008.0, 8, 3000, 1, 1, rf_move(0.0, 0.0), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 16, 3000), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink on(kAll), off(kNoPeriod);
    Out a = draw(on.sink, l, 16, 3000);
    Out b = draw(off.sink, l, 16, 3000);
    ASSERT_EQ(a.result, GSHSCENE_OK);
    ASSERT_EQ(b.result, GSHSCENE_OK);
    EXPECT_EQ(a.bytes, b.bytes);
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], b.summary.by_class[GSHSCENE_ENTRY_REFUSED]) << "a limit under both tiers, or under neither";
    EXPECT_EQ(on.counters.period_aborts, 1u);
    EXPECT_EQ(on.counters.pattern_period, 0u);
  }
  rf_release(&c);
}

TEST(RenderPatternRules, AGradientOverASmallFillWhoseCellLeavesTheDomainGivesTheWindowUp) {
  // PC9, G14: a gradient is sampled only where it is drawn, and a fill that is refused for leaving the sampling domain (2^128 in the gradient's own
  // coordinates) is refused where the whole render draws it.  The window draws the copies it meets, cut by the render's edges, and cannot tell that a
  // copy elsewhere is whole, so it vouches for the gradient only when the whole cell is in the domain; otherwise the pass gives the window up and the
  // general tier decides every copy.  A gradient's own map holds at most 2^64 (G14), so the scale that carries the cell past the domain is made by
  // the fill's map as well: the fill is written 2^k times larger and its map scales it back by 2^-k, and the gradient map scales by 2^-64, so the
  // gradient-space point of a pixel is 2^(64 + k) times its position in the cell.  At k = 58 the fill's corner (7.5 pixels) is at 2^125 and the cell's
  // far edge (64.5) at 2^128, past half the domain.  The same picture and the same report either way; the window is given up when the cell leaves
  // the domain whatever the fill does, stands when the cell does not, and a fill that is itself past the domain is the limit of both tiers.
  struct Row {
    const char * what;
    int k;              // the fill's scale, as a power of two
    double fill;        // the fill's far edge in the cell
    unsigned refused;   // the report's refusals
    unsigned gave_up;   // the window given up
  };
  const Row rows[] = {
      {"a small fill, a cell past the domain", 58, 7.5, 0, 1},
      {"a small fill, a cell inside the domain", 30, 7.5, 0, 0},
      {"a fill past the domain", 58, 64.0, 1, 0},
      {"a fill and a cell inside it", 40, 64.0, 0, 0},
  };
  for (const Row & row : rows) {
    RF_Ctx c;
    rf_begin(&c);
    const double up = std::ldexp(1.0, row.k);
    GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
    GSHSCENE_Gradient g = rf_linear(0, 0, 0x1p60, 0, 0, 0, GSHSCENE_EXTEND_PAD);
    rf_check(&c, gshgeom_map_scale(2, 0x1p-64, 0x1p-64, 1.0, &g.map) == GSHGEOM_OK);
    GSHGEOM_Map down;
    rf_check(&c, gshgeom_map_scale(2, 1.0 / up, 1.0 / up, 1.0, &down) == GSHGEOM_OK);
    GSHSCENE_List * t = rf_open(&c);
    rf_fill(&c, t, rf_rect(&c, 0.5 * up, 0.5 * up, row.fill * up, row.fill * up), &down, GSHGEOM_FILL_NONZERO,
        rf_gradient_paint(rf_gradient(&c, g, stops, 2), 0.9), 0, 0.0);
    const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 64, 64, 64, 64, 1, 1, rf_move(0.0, 0.0), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 200, 150), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    ASSERT_EQ(c.failed, 0) << row.what;
    TSink on(kAll), off(kNoPeriod);
    Out a = draw(on.sink, l, 200, 150);
    Out b = draw(off.sink, l, 200, 150);
    ASSERT_EQ(a.result, GSHSCENE_OK) << row.what;
    ASSERT_EQ(b.result, GSHSCENE_OK) << row.what;
    EXPECT_EQ(a.bytes, b.bytes) << row.what;
    EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], row.refused) << row.what;
    EXPECT_EQ(b.summary.by_class[GSHSCENE_ENTRY_REFUSED], row.refused) << row.what;
    EXPECT_EQ(on.counters.period_aborts, row.gave_up) << row.what;
    EXPECT_EQ(on.counters.pattern_period, (row.refused || row.gave_up) ? 0u : 1u) << row.what << ": the window draws when nothing stops it";
    rf_release(&c);
  }
}

TEST(RenderPatternRules, ALimitMetInsideATileIsMetWhateverTheDamageDraws) {
  // S4, PC8: a damage-limited render is a window onto the whole render and refuses exactly what the whole render refuses.  A fill or a stroke
  // in a tile that is painted with a gradient past the sampling domain (G14) is refused for the copies that are whole; the copies cut by the
  // render's edge are smaller and stand inside the domain.  A damage that meets only the cut copies draws only those, and used to find
  // nothing to refuse where the whole render refuses the use.  The scale puts a cell 64 pixels in either direction past the domain and one
  // that is a few pixels across inside it; the render is cut so that its corner copy is 3 pixels wide and tall (7 where the cells overlap, where it is the only copy
  // that holds the pixels past the tail of the one before), and the damage is that copy alone (inside its cell, so that no neighbor's cell touches it: a cell that only touches the damage is still drawn), or the copies of an edge, or a part inside.  The cells abut or overlap (PC6), where the tile is walked for one copy at
  // a time.
  struct Row {
    const char * what;
    int outer;          // the unit painted with the pattern: a fill of the render, or a stroke across it (whose rectangle the damage cuts)
    int stroke;         // the tile's unit: a fill, or a stroke of a line along the cell
    int k;              // the scale of the unit, as a power of two
    double edge;        // the unit's far edge in the cell
    double step;        // the lattice's step: 64 abuts, less overlaps
    uint32_t w, h;      // the render
    GSHSCENE_Rect corner;
    unsigned refused;   // the report's refusals of the whole render
    unsigned copies;    // where nothing is refused: the copies the render of the corner damage draws (0: not counted)
  };
  const GSHSCENE_Rect abut = {193, 129, 195, 131}, overlap = {125, 125, 127, 127};
  const Row rows[] = {
      {"a stroke painted with a pattern whose tile fills past the domain", 1, 0, 58, 64.0, 64, 195, 131, abut, 1, 0},
      {"a stroke painted with a pattern whose tile strokes past the domain", 1, 1, 58, 64.0, 64, 195, 131, abut, 1, 0},
      {"a stroke painted with a pattern of cells that overlap, filled", 1, 0, 58, 64.0, 60, 127, 127, overlap, 1, 0},
      {"a stroke painted with a pattern of cells that overlap, stroked", 1, 1, 58, 64.0, 60, 127, 127, overlap, 1, 0},
      {"a fill painted with a pattern whose tile fills past the domain", 0, 0, 58, 64.0, 64, 195, 131, abut, 1, 0},
      {"a fill painted with a pattern whose tile strokes past the domain", 0, 1, 58, 64.0, 64, 195, 131, abut, 1, 0},
      {"a fill painted with a pattern of cells that overlap", 0, 0, 58, 64.0, 60, 127, 127, overlap, 1, 0},
      {"a stroke painted with a pattern whose tile fills inside the domain", 1, 0, 58, 7.5, 64, 195, 131, abut, 0, 1},
      {"a fill painted with a pattern whose tile fills inside the domain", 0, 0, 58, 7.5, 64, 195, 131, abut, 0, 1},
      {"a stroke painted with a pattern of cells that overlap, inside the domain", 1, 0, 58, 7.5, 60, 127, 127, overlap, 0, 1},
  };
  for (const Row & row : rows) {
    RF_Ctx c;
    rf_begin(&c);
    const double up = std::ldexp(1.0, row.k);
    GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
    GSHSCENE_Gradient g = rf_linear(0, 0, 0x1p60, 0, 0, 0, GSHSCENE_EXTEND_PAD);
    rf_check(&c, gshgeom_map_scale(2, 0x1p-64, 0x1p-64, 1.0, &g.map) == GSHGEOM_OK);
    GSHGEOM_Map down;
    rf_check(&c, gshgeom_map_scale(2, 1.0 / up, 1.0 / up, 1.0, &down) == GSHGEOM_OK);
    GSHSCENE_List * t = rf_open(&c);
    const GSHSCENE_Paint paint = rf_gradient_paint(rf_gradient(&c, g, stops, 2), 0.9);
    if (row.stroke) {
      const double line[4] = {0.5 * up, 0.5 * up, row.edge * up, row.edge * up};
      rf_stroke(&c, t, rf_polyline(&c, line, 2, 0), rf_pen(&c, 1.5 * up, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &down, paint);
    } else {
      rf_fill(&c, t, rf_rect(&c, 0.5 * up, 0.5 * up, row.edge * up, row.edge * up), &down, GSHGEOM_FILL_NONZERO, paint, 0, 0.0);
    }
    const GSHSCENE_Resource * pat =
        rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 64, 64, row.step, row.step, 1, 1, rf_move(0.0, 0.0), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    if (row.outer) {
      // a butt-capped line along the render's middle, wide enough to cover it all
      const double across[4] = {-10.0, 0.5 * row.h, row.w + 10.0, 0.5 * row.h};
      rf_stroke(&c, l, rf_polyline(&c, across, 2, 0), rf_pen(&c, 4.0 * (row.w + row.h), GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr,
          rf_pattern_paint(pat, 1.0));
    } else {
      rf_fill(&c, l, rf_rect(&c, 0, 0, row.w, row.h), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    }
    rf_close(&c, l);
    ASSERT_EQ(c.failed, 0) << row.what;
    const GSHSCENE_Rect damages[] = {row.corner, {row.corner.x0, 0, (int32_t)row.w, (int32_t)row.h}, {0, 0, 8, (int32_t)row.h},
        {(int32_t)row.w / 2, (int32_t)row.h / 2, (int32_t)row.w / 2 + 10, (int32_t)row.h / 2 + 5}, {0, row.corner.y0, (int32_t)row.w, (int32_t)row.h}};
    for (unsigned tiers : {kAll, kNoPeriod}) {
      TSink whole_sink(tiers);
      Out whole = draw(whole_sink.sink, l, row.w, row.h);
      ASSERT_EQ(whole.result, GSHSCENE_OK) << row.what;
      EXPECT_EQ(whole.summary.by_class[GSHSCENE_ENTRY_REFUSED], row.refused) << row.what << ": the whole render";
      for (const GSHSCENE_Rect & damage : damages) {
        TSink sink(tiers);
        Out part = draw(sink.sink, l, row.w, row.h, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, nullptr, &damage);
        ASSERT_EQ(part.result, GSHSCENE_OK) << row.what;
        EXPECT_EQ(part.summary.by_class[GSHSCENE_ENTRY_REFUSED], row.refused)
            << row.what << ": damage " << damage.x0 << "," << damage.y0 << "," << damage.x1 << "," << damage.y1 << " refuses what the whole render does";
        EXPECT_EQ(part.entries, whole.entries) << row.what;
        if (row.copies && tiers == kNoPeriod && &damage == &damages[0]) {
          EXPECT_EQ(sink.counters.pattern_copies, (uint64_t)row.copies) << row.what << ": only the copy the damage reaches is drawn";
        }
        for (int64_t y = damage.y0; y < damage.y1; y++) {
          for (int64_t x = damage.x0; x < damage.x1; x++) {
            const size_t at_px = ((size_t)y * row.w + (size_t)x) * 4u;
            ASSERT_EQ(part.bytes.compare(at_px, 4, whole.bytes, at_px, 4), 0) << row.what << " at " << x << "," << y;
          }
        }
      }
    }
    rf_release(&c);
  }
}

TEST(RenderPatternRules, AUnitOfATileTheDamageDoesNotReachIsTestedAllTheSame) {
  // S4: a fill under a clip that keeps it in a strip of the cell the damage never meets has nothing to draw there, and the gradient it is
  // painted with is past the sampling domain in every whole copy: the unit has no rectangle in the damage, and the use is refused as the
  // whole render's is.
  RF_Ctx c;
  rf_begin(&c);
  const double up = std::ldexp(1.0, 58);
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
  GSHSCENE_Gradient g = rf_linear(0, 0, 0x1p60, 0, 0, 0, GSHSCENE_EXTEND_PAD);
  rf_check(&c, gshgeom_map_scale(2, 0x1p-64, 0x1p-64, 1.0, &g.map) == GSHGEOM_OK);
  GSHGEOM_Map down;
  rf_check(&c, gshgeom_map_scale(2, 1.0 / up, 1.0 / up, 1.0, &down) == GSHGEOM_OK);
  GSHSCENE_List * t = rf_open(&c);
  rf_check(&c, gshscene_list_clip_push(t, rf_rect(&c, 0, 0, 64, 4), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(&c, t, rf_rect(&c, 0.5 * up, 0.5 * up, 64.0 * up, 64.0 * up), &down, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(&c, g, stops, 2), 0.9), 0, 0.0);
  rf_check(&c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
  const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 64, 64, 64, 64, 1, 1, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 195, 131), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  ASSERT_EQ(c.failed, 0);
  TSink whole_sink(kNoPeriod);
  Out whole = draw(whole_sink.sink, l, 195, 131);
  ASSERT_EQ(whole.result, GSHSCENE_OK);
  EXPECT_EQ(whole.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u);
  for (const GSHSCENE_Rect & damage : {GSHSCENE_Rect{5, 20, 15, 30}, GSHSCENE_Rect{100, 100, 120, 120}}) {
    TSink sink(kNoPeriod);
    Out part = draw(sink.sink, l, 195, 131, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, nullptr, &damage);
    ASSERT_EQ(part.result, GSHSCENE_OK);
    EXPECT_EQ(part.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u) << damage.x0 << "," << damage.y0;
  }
  rf_release(&c);
}

TEST(RenderPatternRules, AUnionsDensestPixelInATileIsMetWhateverTheDamageDraws) {
  // S4, CG8: the pieces of the members of a union at one pixel are bounded by max_exact_fragments, and the pixels a copy meets are the whole
  // render's part of it.  Six members overlap in the middle of the cell, past the corner copy's three pixels: a whole copy is refused for
  // them at a cap of 3 and the corner copy never meets them; the damage is the corner copy and the use is refused as the whole render's is.
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_Clip_Member m[6];
  for (int i = 0; i < 6; i++) {
    m[i] = rf_member(rf_rect(&c, 30.0 + 0.5 * i, 30.0 + 0.25 * i, 50.0 + 0.5 * i, 50.0 + 0.25 * i), GSHGEOM_FILL_NONZERO, nullptr);
  }
  GSHSCENE_List * t = rf_open(&c);
  rf_check(&c, gshscene_list_clip_push(t, rf_union(&c, m, 6), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(&c, t, rf_rect(&c, 0, 0, 64, 64), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.8), 0, 0.0);
  rf_check(&c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
  const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 64, 64, 64, 64, 1, 1, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 131, 131), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  ASSERT_EQ(c.failed, 0);
  for (uint64_t cap : {uint64_t(3), uint64_t(1000)}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_exact_fragments = cap;
    TSink whole_sink(kNoPeriod, &limits);
    Out whole = draw(whole_sink.sink, l, 131, 131);
    ASSERT_EQ(whole.result, GSHSCENE_OK);
    const unsigned refused = cap == 3 ? 1u : 0u;
    EXPECT_EQ(whole.summary.by_class[GSHSCENE_ENTRY_REFUSED], refused) << "cap " << cap << ": the whole render";
    for (const GSHSCENE_Rect & damage : {GSHSCENE_Rect{129, 129, 131, 131}, GSHSCENE_Rect{129, 0, 131, 131}, GSHSCENE_Rect{0, 0, 3, 3}}) {
      TSink sink(kNoPeriod, &limits);
      Out part = draw(sink.sink, l, 131, 131, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, nullptr, &damage);
      ASSERT_EQ(part.result, GSHSCENE_OK);
      EXPECT_EQ(part.summary.by_class[GSHSCENE_ENTRY_REFUSED], refused) << "cap " << cap << ", damage " << damage.x0 << "," << damage.y0;
    }
  }
  rf_release(&c);
}

TEST(RenderPatternRules, ALimitMetInANestedPatternIsMetWhateverTheDamageDraws) {
  // S4: the copies of a nested pattern are those of every copy of the outer lattice that the whole render draws, not only of the copies the
  // damage reaches (design.md, PC8).  The inner tile holds a gradient past the sampling domain in a whole cell, or a union whose densest pixel
  // is past a cap, and the outer tile paints a fill with that pattern.  The damage is inside the corner copy of the outer lattice, whose
  // cell is cut to 3 by 3 pixels by the render's edge: no inner copy in it is whole, and the inner pattern is refused in every other copy of
  // the outer one.  A copy outside the damage has an empty rectangle and a whole part, and its units are tested with those.
  for (int kind = 0; kind < 2; kind++) {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * inner = rf_open(&c);
    if (kind == 0) {
      const double up = std::ldexp(1.0, 58);
      GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
      GSHSCENE_Gradient g = rf_linear(0, 0, 0x1p60, 0, 0, 0, GSHSCENE_EXTEND_PAD);
      rf_check(&c, gshgeom_map_scale(2, 0x1p-64, 0x1p-64, 1.0, &g.map) == GSHGEOM_OK);
      GSHGEOM_Map down;
      rf_check(&c, gshgeom_map_scale(2, 1.0 / up, 1.0 / up, 1.0, &down) == GSHGEOM_OK);
      rf_fill(&c, inner, rf_rect(&c, 0.5 * up, 0.5 * up, 64.0 * up, 64.0 * up), &down, GSHGEOM_FILL_NONZERO,
          rf_gradient_paint(rf_gradient(&c, g, stops, 2), 0.9), 0, 0.0);
    } else {
      GSHSCENE_Clip_Member m[6];
      for (int i = 0; i < 6; i++) {
        m[i] = rf_member(rf_rect(&c, 30.0 + 0.5 * i, 30.0 + 0.25 * i, 50.0 + 0.5 * i, 50.0 + 0.25 * i), GSHGEOM_FILL_NONZERO, nullptr);
      }
      rf_check(&c, gshscene_list_clip_push(inner, rf_union(&c, m, 6), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
      rf_fill(&c, inner, rf_rect(&c, 0, 0, 64, 64), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.8), 0, 0.0);
      rf_check(&c, gshscene_list_clip_pop(inner) == GSHSCENE_OK);
    }
    const GSHSCENE_Resource * inner_pat = rf_pattern(&c, rf_picture_of(&c, inner), 0, 0, 64, 64, 64, 64, 1, 1, rf_move(0.0, 0.0), nullptr);
    GSHSCENE_List * outer = rf_open(&c);
    rf_fill(&c, outer, rf_rect(&c, 0, 0, 64, 64), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(inner_pat, 1.0), 0, 0.0);
    const GSHSCENE_Resource * outer_pat = rf_pattern(&c, rf_picture_of(&c, outer), 0, 0, 64, 64, 64, 64, 1, 1, rf_move(0.0, 0.0), nullptr);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 195, 131), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(outer_pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    ASSERT_EQ(c.failed, 0) << kind;
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_exact_fragments = kind == 1 ? 3 : limits.max_exact_fragments;
    TSink whole_sink(kNoPeriod, &limits);
    Out whole = draw(whole_sink.sink, l, 195, 131);
    ASSERT_EQ(whole.result, GSHSCENE_OK) << kind;
    EXPECT_EQ(whole.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u) << kind << ": the whole render";
    for (const GSHSCENE_Rect & damage : {GSHSCENE_Rect{193, 129, 195, 131}, GSHSCENE_Rect{193, 0, 195, 131}, GSHSCENE_Rect{0, 0, 8, 8}}) {
      TSink sink(kNoPeriod, &limits);
      Out part = draw(sink.sink, l, 195, 131, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, nullptr, &damage);
      ASSERT_EQ(part.result, GSHSCENE_OK) << kind;
      EXPECT_EQ(part.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u) << kind << ", damage " << damage.x0 << "," << damage.y0;
    }
    rf_release(&c);
  }
}

TEST(RenderPatternRules, ADamageThatTheWindowFitsInRefusesWhatTheWholeRenderDoesOverCellsThatOverlap) {
  // The witness of an argued equivalence (PC9, S4).  The period window is taken only when it lies in the rectangle the damage leaves, and a
  // rectangle that holds the window of an overlapping lattice holds a whole copy of every class: the copy that refuses in the whole render
  // is in the rectangle too, so the pass that gives the window up and draws the rectangle's copies (the plain rectangle, or the whole
  // render's, as the pass does) finds the refusal either way.  Asserted here for the overlapping lattice of 60 with a cell of 64 whose tile
  // is past the domain, and a damage as small as the window (60 by 60) at each corner of a 127 by 127 render.
  RF_Ctx c;
  rf_begin(&c);
  const double up = std::ldexp(1.0, 58);
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
  GSHSCENE_Gradient g = rf_linear(0, 0, 0x1p60, 0, 0, 0, GSHSCENE_EXTEND_PAD);
  rf_check(&c, gshgeom_map_scale(2, 0x1p-64, 0x1p-64, 1.0, &g.map) == GSHGEOM_OK);
  GSHGEOM_Map down;
  rf_check(&c, gshgeom_map_scale(2, 1.0 / up, 1.0 / up, 1.0, &down) == GSHGEOM_OK);
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, rf_rect(&c, 0.5 * up, 0.5 * up, 64.0 * up, 64.0 * up), &down, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(&c, g, stops, 2), 0.9), 0, 0.0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), 0, 0, 64, 64, 60, 60, 1, 1, rf_move(0.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 127, 127), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  ASSERT_EQ(c.failed, 0);
  TSink whole_sink(kAll);
  Out whole = draw(whole_sink.sink, l, 127, 127);
  ASSERT_EQ(whole.result, GSHSCENE_OK);
  EXPECT_EQ(whole.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u);
  for (const GSHSCENE_Rect & damage : {GSHSCENE_Rect{0, 0, 60, 60}, GSHSCENE_Rect{67, 0, 127, 60}, GSHSCENE_Rect{0, 67, 60, 127}, GSHSCENE_Rect{67, 67, 127, 127}}) {
    TSink sink(kAll);
    Out part = draw(sink.sink, l, 127, 127, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, nullptr, &damage);
    ASSERT_EQ(part.result, GSHSCENE_OK);
    EXPECT_EQ(part.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u) << damage.x0 << "," << damage.y0;
  }
  rf_release(&c);
}

TEST(RenderPatternRules, ARegionPastTheRangeInACellsFrameIsALimitUnderBothTiers) {
  // PC3, PC8: a use of the plane in a cell 25 million pixels wide, translated by 20 million, is drawn over the region of the copy in the cell's own
  // frame, which lies 20 million pixels from its origin: past the range the scan converter takes.  That is the sink's resource limit, reported, and
  // nothing is drawn, under either tier.
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(c.store, &plane), GSHSCENE_OK);
  c.held[c.held_count++] = plane;
  GSHSCENE_List * t = rf_open(&c);
  rf_fill(&c, t, plane, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, rf_picture_of(&c, t), -25000000.0, 0, 25000000.0, 40, 25000000.0, 40, 1, 1, rf_move(20000000.0, 0.0), nullptr);
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 40, 30), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(&c, l);
  GSHSCENE_List * none = rf_open(&c);
  rf_close(&c, none);
  for (unsigned tiers : {kAll, kNoPeriod}) {
    TSink a(tiers), z(tiers);
    Out o = draw(a.sink, l, 40, 30);
    Out e = draw(z.sink, none, 40, 30);
    EXPECT_EQ(o.summary.by_class[GSHSCENE_ENTRY_REFUSED], 1u) << tiers;
    EXPECT_EQ(o.bytes, e.bytes) << tiers;
  }
  rf_release(&c);
}

TEST(RenderPatternRules, ACutStrokeKnowsItsPartsPointsExtentBoxAndBound) {
  // PC5: the stroke cut to a cell is a stroke of its own, whose parts are the cut ones: what each is made of (its points, the extent of its
  // coordinates, the pixels it touches shifted into the device, the deposits it can make) is the cut path's, not the outline's; a part with nothing
  // inside the cell is absent; and a stroke with no part left draws nothing.
  RF_Ctx c;
  rf_begin(&c);
  const double zig[10] = {-2.0, 3.0, 3.0, 6.5, 6.0, 1.0, 10.0, 7.0, 12.0, 4.0};
  GSHSCENE_List * l = rf_open(&c);
  rf_stroke(&c, l, rf_polyline(&c, zig, 5, 1),
      rf_style(&c, 1.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0, 0.0,
          GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
      nullptr, rf_solid(0.1, 0.2, 0.6, 0.8));
  rf_close(&c, l);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l, 0, &item), GSHSCENE_OK);
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  const GSHGEOM_Allocator * alloc = gshgeom_allocator_default();
  auto floor_px = [](int64_t v) { return v % 64 != 0 && v < 0 ? v / 64 - 1 : v / 64; };
  const GSHGEOM_Map at = rf_move(5.25, 7.5); // a whole-pixel shift of (5, 7) and the fraction in the outline
  GSHCPU_Stroke whole;
  ASSERT_EQ(gshcpu_render_stroke_open(nullptr, alloc, &limits, item, &at, &whole), GSHCPU_OK);
  ASSERT_TRUE(whole.drawn);
  EXPECT_EQ(whole.shift_x, 5);
  EXPECT_EQ(whole.shift_y, 7);
  GSHCPU_Stroke_Meta before = whole.meta;
  GSHCPU_Rect box_before[GSHCPU_STROKE_PARTS];
  for (int p = 0; p < GSHCPU_STROKE_PARTS; p++) {
    box_before[p] = whole.box[p];
  }
  // The cell [2, 6] x [2, 6] in the canonical frame is, in the device, that cell moved by the shift.
  GSHGEOM_Device_Point quad[4];
  const int xs[4] = {2, 6, 6, 2}, ys[4] = {2, 2, 6, 6};
  for (int k = 0; k < 4; k++) {
    quad[k].x = (xs[k] + 5) * 64;
    quad[k].y = (ys[k] + 7) * 64;
  }
  ASSERT_EQ(gshcpu_render_stroke_cut(&whole, quad, alloc, &limits), GSHCPU_OK);
  EXPECT_TRUE(whole.drawn);
  int parts_present = 0;
  for (int p = GSHCPU_STROKE_BODY; p <= GSHCPU_STROKE_ALIGNED; p++) {
    if (!whole.meta.present[p]) {
      EXPECT_EQ(whole.meta.points[p], 0u) << p;
      EXPECT_EQ(whole.meta.bound[p], 0u) << p;
      EXPECT_TRUE(whole.box[p].x0 == whole.box[p].x1 || whole.box[p].y0 == whole.box[p].y1) << p;
      continue;
    }
    parts_present++;
    ASSERT_TRUE(whole.cut_set[p]) << p;
    ASSERT_NE(whole.cut[p], nullptr) << p;
    const size_t n = gshgeom_flat_path_point_count(whole.cut[p]);
    const GSHGEOM_Device_Point * pts = gshgeom_flat_path_points(whole.cut[p]);
    ASSERT_GT(n, 0u);
    int64_t lo_x = pts[0].x, lo_y = pts[0].y, hi_x = pts[0].x, hi_y = pts[0].y;
    for (size_t i = 1; i < n; i++) {
      lo_x = std::min<int64_t>(lo_x, pts[i].x);
      lo_y = std::min<int64_t>(lo_y, pts[i].y);
      hi_x = std::max<int64_t>(hi_x, pts[i].x);
      hi_y = std::max<int64_t>(hi_y, pts[i].y);
    }
    EXPECT_EQ(whole.meta.points[p], n) << p;
    EXPECT_EQ(whole.meta.lo_x[p], lo_x) << p;
    EXPECT_EQ(whole.meta.lo_y[p], lo_y) << p;
    EXPECT_EQ(whole.meta.hi_x[p], hi_x) << p;
    EXPECT_EQ(whole.meta.hi_y[p], hi_y) << p;
    // the cut lies inside the cell (the cell's pixels are the canonical box's bound)
    EXPECT_GE(lo_x, 2 * 64) << p;
    EXPECT_LE(hi_x, 6 * 64) << p;
    EXPECT_GE(lo_y, 2 * 64) << p;
    EXPECT_LE(hi_y, 6 * 64) << p;
    GSHCPU_Rect canon;
    canon.x0 = floor_px(lo_x);
    canon.y0 = floor_px(lo_y);
    canon.x1 = -floor_px(-hi_x);
    canon.y1 = -floor_px(-hi_y);
    const GSHGEOM_Flat_Path * one = whole.cut[p];
    EXPECT_EQ(whole.meta.bound[p], gshcpu_render_cells_bound(&one, 1, canon, UINT64_MAX)) << p;
    EXPECT_EQ(whole.box[p].x0, canon.x0 + 5) << p;
    EXPECT_EQ(whole.box[p].y0, canon.y0 + 7) << p;
    EXPECT_EQ(whole.box[p].x1, canon.x1 + 5) << p;
    EXPECT_EQ(whole.box[p].y1, canon.y1 + 7) << p;
    // and it is not the outline's: the outline reaches past the cell in every direction
    EXPECT_LT(whole.meta.hi_x[p] - whole.meta.lo_x[p], before.hi_x[p] - before.lo_x[p]) << p;
    EXPECT_LT(whole.meta.points[p], 100000u);
    (void)box_before;
  }
  EXPECT_GE(parts_present, 1);
  gshcpu_render_stroke_close(nullptr, &whole);
  // A cell with none of the stroke in it leaves no part, and no stroke.
  GSHCPU_Stroke gone;
  ASSERT_EQ(gshcpu_render_stroke_open(nullptr, alloc, &limits, item, &at, &gone), GSHCPU_OK);
  for (int k = 0; k < 4; k++) {
    quad[k].x = (xs[k] + 205) * 64;
    quad[k].y = (ys[k] + 207) * 64;
  }
  ASSERT_EQ(gshcpu_render_stroke_cut(&gone, quad, alloc, &limits), GSHCPU_OK);
  EXPECT_FALSE(gone.drawn);
  EXPECT_FALSE(gone.meta.present[GSHCPU_STROKE_BODY]);
  EXPECT_FALSE(gone.meta.present[GSHCPU_STROKE_ALIGNED]);
  EXPECT_EQ(gone.meta.points[GSHCPU_STROKE_BODY], 0u);
  gshcpu_render_stroke_close(nullptr, &gone);
  // A translation past the range the scan converter takes is the sink's limit, at the opening.
  const GSHGEOM_Map far = rf_move(40000000.0, 0.0);
  GSHCPU_Stroke refused;
  EXPECT_EQ(gshcpu_render_stroke_open(nullptr, alloc, &limits, item, &far, &refused), GSHCPU_ERR_LIMIT);
  rf_release(&c);
}

TEST(RenderPatternRules, TheLinearPartAloneSeparatesTwoRastersOfOnePattern) {
  // PC10: the same pattern, at a fraction of zero, drawn through one sink under maps that differ only in their linear part (a mirror in x, a mirror in
  // y, a quarter turn, a transposition): the cell, the fraction and the spaces are the same, so only the key's linear part tells the rasters apart, and a
  // picture that took another's raster would be a mirrored or turned tile.  Each is what a sink with no cache draws.
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * tile = interior_tile(&c, 0);
  const GSHSCENE_Resource * pat = rf_pattern(&c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.0, 0.0), nullptr);
  struct M {
    double m[4];
    double t[2];
  };
  const M maps[] = {{{1, 0, 0, 1}, {0, 0}}, {{-1, 0, 0, 1}, {72, 0}}, {{1, 0, 0, -1}, {0, 56}}, {{0, -1, 1, 0}, {56, 0}}, {{0, 1, 1, 0}, {0, 0}},
      {{-1, 0, 0, -1}, {72, 56}}, {{1, 0, 0, 1}, {0, 0}}};
  std::vector<GSHSCENE_List *> lists;
  for (const M & m : maps) {
    GSHGEOM_Map map;
    gshgeom_map_from_matrix(2, m.m, m.t, &map);
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 0, 0, 72, 56), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.9, 1.0), 0, 0.0);
    rf_fill(&c, l, rf_rect(&c, 4, 3, 52, 51), &map, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&c, l);
    lists.push_back(l);
  }
  TSink cached(kAll), plain(kNoCache);
  for (size_t k = 0; k < lists.size(); k++) {
    Out a = draw(cached.sink, lists[k], 72, 56, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    Out b = draw(plain.sink, lists[k], 72, 56, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    ASSERT_EQ(a.result, GSHSCENE_OK);
    EXPECT_EQ(a.bytes, b.bytes) << "map " << k;
  }
  EXPECT_GE(cached.counters.period_misses, 5u) << "five linear parts, five rasters";
  EXPECT_GE(cached.counters.period_hits, 1u) << "and the last list is the first one again";
  rf_release(&c);
}

TEST(RenderPatternRules, ANestedPatternAndAnExtentThatDoesNotStartAtTheOriginKeepEachOthersPixels) {
  // PC3: a pattern inside a tile is drawn in the copy's own frame, whose pixels the pass keeps at the place they have in the device (the copy's whole-pixel
  // shift is part of where they are kept).  Over an extent that begins well inside the canvas, a frame that forgot the shift would address the pixels
  // before its layer, which belong to the layer of the pass around it.
  for (int variant : {6, 10, 12, 14}) {
    for (double step : {8.0, 11.0}) {
      Twin tw(variant, step, step, 0.3, 0.45);
      for (const auto & e : {std::array<int, 4>{16, 12, 72, 56}, std::array<int, 4>{30, 20, 70, 50}}) {
        Out o[2];
        const GSHSCENE_List * lists[2] = {tw.pattern, tw.explicit_copies};
        for (int k = 0; k < 2; k++) {
          TSink s(kNoPeriod);
          RasterRef raster(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 72, 56));
          std::memset(gimg_raster_pixels(raster.raster), 0, gimg_raster_stride_bytes(raster.raster) * 56);
          ReportRef report;
          GSHSCENE_Target t = target_of(raster.raster);
          t.extent.x0 = e[0];
          t.extent.y0 = e[1];
          t.extent.x1 = e[2];
          t.extent.y1 = e[3];
          GSHSCENE_Render_Request req = request_of(lists[k], &t, report.report);
          o[k].result = gshscene_sink_render(s.sink, &req);
          o[k].bytes = raster.bytes();
        }
        ASSERT_EQ(o[0].result, GSHSCENE_OK);
        ASSERT_EQ(o[1].result, GSHSCENE_OK);
        EXPECT_LE(max_levels(o[0].bytes, o[1].bytes, GSHCPU_FORMAT_RGBA8_STRAIGHT), 1.0) << "variant " << variant << " step " << step << " extent " << e[0] << "," << e[1];
      }
    }
  }
}

TEST(RenderPatternRules, AStrokeIsCutFromTheFormOfItsPartsThatHoldsNoCancellingEdges) {
  // PC5: the cut of a part is the cut of what the part covers: its reduced form (the same winding without the edges that cancel) where it has one,
  // else the part itself.  Strokes with sharp joins and a self-crossing path have a reduced form that is not the part; the cut equals the cut of it.
  struct Shape {
    std::vector<double> pts;
    int closed;
    uint32_t join, cap;
    double width;
  };
  const Shape shapes[] = {
      {{0.0, 8.0, 5.0, 1.0, 10.0, 8.0, 14.0, 2.0, 17.0, 7.0}, 0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND, 3.0},
      {{1.0, 1.0, 11.0, 9.0, 1.0, 9.0, 11.0, 1.0}, 1, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT, 2.5},
      {{1.0, 5.0, 12.0, 5.0, 2.0, 5.5, 13.0, 5.5}, 0, GSHSCENE_STROKE_JOIN_BEVEL, GSHSCENE_STROKE_CAP_SQUARE, 2.0},
  };
  int differing = 0;
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  const GSHGEOM_Allocator * alloc = gshgeom_allocator_default();
  for (const Shape & sh : shapes) {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * l = rf_open(&c);
    rf_stroke(&c, l, rf_polyline(&c, sh.pts.data(), sh.pts.size() / 2, sh.closed), rf_pen(&c, sh.width, sh.join, sh.cap), nullptr, rf_solid(0.1, 0.2, 0.6, 0.8));
    rf_close(&c, l);
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l, 0, &item), GSHSCENE_OK);
    const GSHGEOM_Map at = rf_move(0.25, 0.5);
    GSHCPU_Stroke st;
    ASSERT_EQ(gshcpu_render_stroke_open(nullptr, alloc, &limits, item, &at, &st), GSHCPU_OK);
    GSHGEOM_Device_Point quad[4];
    const int xs[4] = {3, 9, 9, 3}, ys[4] = {2, 2, 8, 8};
    for (int k = 0; k < 4; k++) {
      quad[k].x = xs[k] * 64;
      quad[k].y = ys[k] * 64;
    }
    ASSERT_EQ(gshcpu_render_stroke_cut(&st, quad, alloc, &limits), GSHCPU_OK);
    const GSHGEOM_Flat_Path * reduced = gshgeom_stroke_outline_body_reduced(st.outline);
    const GSHGEOM_Flat_Path * part = gshgeom_stroke_outline_body(st.outline);
    if (st.meta.present[GSHCPU_STROKE_BODY]) {
      const GSHGEOM_Flat_Path * source = reduced ? reduced : part;
      GSHGEOM_Flat_Path * want = nullptr;
      ASSERT_EQ(gshgeom_flat_path_clip_convex(source, quad, alloc, &limits, &want), GSHGEOM_OK);
      ASSERT_NE(st.cut[GSHCPU_STROKE_BODY], nullptr);
      ASSERT_EQ(gshgeom_flat_path_point_count(want), gshgeom_flat_path_point_count(st.cut[GSHCPU_STROKE_BODY]));
      EXPECT_EQ(std::memcmp(gshgeom_flat_path_points(want), gshgeom_flat_path_points(st.cut[GSHCPU_STROKE_BODY]),
                    gshgeom_flat_path_point_count(want) * sizeof(GSHGEOM_Device_Point)),
          0);
      gshgeom_flat_path_destroy(want);
      if (reduced && reduced != part) {
        differing++;
      }
    }
    gshcpu_render_stroke_close(nullptr, &st);
    rf_release(&c);
  }
  EXPECT_GE(differing, 1) << "a shape whose reduced form is not its part, or the case does not tell them apart";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
