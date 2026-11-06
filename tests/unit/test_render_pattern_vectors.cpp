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
 * The gate of the sink's pattern against the waist's oracle (masks-images-patterns story R2; render.h, PC1 to PC8): shape-scene's
 * installed `pattern_vectors.h` (exact rationals, shape-scene's `tools/pattern-oracle.py`) are drawn by the sink as the scenes they
 * describe, a pattern filling a rectangle over a transparent float target, and the layer before the write is compared with the
 * oracle's exact value at every pixel.
 *
 * The oracle takes a gradient or an image paint at the pixel's center in the frame of the copy, as the sink does, and weighs it by the
 * area a copy covers; the sink takes 8-bit coverages (render.h, D8, PC4), so the bound is the 8-bit rounding of a coverage (1/255 of
 * the pixel's value) plus the seam bound of CK4 (render.h, PC3).  A scene whose pixels are all covered whole by one copy agrees to
 * rounding.
 */

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/pattern_vectors.h>

#include "../../src/render/render_internal.h"
#include "../image_helpers.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace pv = pattern_vectors;

namespace {

GSHGEOM_Map map_of(const double m6[6]) {
  const double m[4] = {m6[0], m6[1], m6[2], m6[3]};
  const double t[2] = {m6[4], m6[5]};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

GSHSCENE_Paint paint_solid(const double rgba[4]) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_SOLID;
  for (int i = 0; i < 4; i++) {
    p.rgba[i] = rgba[i];
  }
  return p;
}

/** The scene's resources, built into one store. */
struct Scene {
  rh::Store st;
  GSHSCENE_Color foreground{};

  const GSHSCENE_Resource * polygon(const pv::VPoint * pts, int n) {
    GSHGEOM_Path * path = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &path);
    for (int i = 0; i < n; i++) {
      GSHGEOM_Point p{pts[i].x, pts[i].y, 0};
      if (i == 0) {
        gshgeom_path_move_to(path, p);
      } else {
        gshgeom_path_line_to(path, p);
      }
    }
    gshgeom_path_close(path);
    return st.path(path);
  }

  const GSHSCENE_Resource * pattern(const pv::VPattern & p) {
    GSHSCENE_List * l = st.open();
    record(l, p);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    const GSHSCENE_Resource * tile = nullptr;
    EXPECT_EQ(gshscene_store_intern_picture(st.store, l, &tile), GSHSCENE_OK);
    st.held.push_back(tile);
    GSHSCENE_Pattern pt;
    gshscene_pattern_init(&pt);
    pt.tile = tile;
    pt.x = p.cell[0];
    pt.y = p.cell[1];
    pt.w = p.cell[2];
    pt.h = p.cell[3];
    pt.x_step = p.step[0];
    pt.y_step = p.step[1];
    pt.repeat_x = (uint32_t)p.repeat[0];
    pt.repeat_y = (uint32_t)p.repeat[1];
    pt.map = map_of(p.map);
    if (p.has_tint) {
      pt.has_tint = 1;
      pt.tint = paint_solid(p.tint);
    }
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_pattern(st.store, &pt, &r), GSHSCENE_OK);
    st.held.push_back(r);
    return r;
  }

  void record(GSHSCENE_List * l, const pv::VPattern & p) {
    for (int k = 0; k < p.op_count; k++) {
      const pv::VOp & op = p.ops[k];
      switch (op.kind) {
        case 1: {
          const GSHSCENE_Resource * path = polygon(op.points, op.point_count);
          GSHSCENE_Paint paint{};
          if (op.paint == 1) {
            paint = paint_solid(op.rgba);
          } else if (op.paint == 2) {
            GSHSCENE_Gradient g{};
            g.kind = GSHSCENE_GRADIENT_LINEAR;
            g.p0[0] = op.p0[0];
            g.p0[1] = op.p0[1];
            g.p1[0] = op.p1[0];
            g.p1[1] = op.p1[1];
            gshgeom_map_identity(2, &g.map);
            g.extend = GSHSCENE_EXTEND_PAD;
            std::vector<GSHSCENE_Gradient_Stop> stops;
            for (int s = 0; s < op.stop_count; s++) {
              GSHSCENE_Gradient_Stop gs{};
              gs.offset = op.stops[s].offset;
              gs.color = paint_solid(op.stops[s].rgba);
              stops.push_back(gs);
            }
            g.stops = stops.data();
            g.stop_count = stops.size();
            const GSHSCENE_Resource * r = nullptr;
            EXPECT_EQ(gshscene_store_intern_gradient(st.store, &g, &r), GSHSCENE_OK);
            st.held.push_back(r);
            paint.kind = GSHSCENE_PAINT_GRADIENT;
            paint.resource = r;
            paint.rgba[3] = 1.0;
          } else if (op.paint == 3) {
            const pv::VImage & im = *op.image;
            std::vector<uint32_t> samples;
            for (int i = 0; i < im.width * im.height * 4; i++) {
              samples.push_back(im.rgba8[i]);
            }
            const GSHSCENE_Resource * image =
                ih::intern_image(st.store, (uint32_t)im.width, (uint32_t)im.height, ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT), samples);
            EXPECT_NE(image, nullptr);
            st.held.push_back(image);
            GSHSCENE_Image_Paint ip;
            gshscene_image_paint_init(&ip);
            ip.image = image;
            const double m[4] = {op.image_map[0], 0.0, 0.0, op.image_map[1]};
            const double t[2] = {op.image_map[2], op.image_map[3]};
            EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &ip.map), GSHGEOM_OK);
            ip.magnify = GSHSCENE_IMAGE_FILTER_NEAREST;
            ip.minify = GSHSCENE_IMAGE_FILTER_NEAREST;
            const GSHSCENE_Resource * r = nullptr;
            EXPECT_EQ(gshscene_store_intern_image_paint(st.store, &ip, &r), GSHSCENE_OK);
            st.held.push_back(r);
            paint.kind = GSHSCENE_PAINT_IMAGE;
            paint.resource = r;
            paint.rgba[3] = 1.0;
          } else if (op.paint == 4) {
            const GSHSCENE_Resource * nested = pattern(*op.nested);
            paint.kind = GSHSCENE_PAINT_PATTERN;
            paint.resource = nested;
            paint.rgba[3] = op.nested->alpha;
          } else {
            paint.kind = GSHSCENE_PAINT_FOREGROUND;
            paint.rgba[3] = op.rgba[3];
          }
          EXPECT_EQ(gshscene_list_fill(l, path, nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
          break;
        }
        case 2: {
          GSHSCENE_Group g{};
          g.opacity = op.opacity;
          g.composite = op.composite == 1 ? (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER : (uint32_t)GSHSCENE_COMPOSITE_MULTIPLY;
          g.isolation = GSHSCENE_ISOLATION_ISOLATED;
          EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
          break;
        }
        case 3:
          EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
          break;
        case 4:
          EXPECT_EQ(gshscene_list_clip_push(l, polygon(op.points, op.point_count), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
          break;
        case 6: { // a union clip (CU21): the members are the convex regions of the vector, each under the identity and the nonzero rule
          std::vector<GSHSCENE_Clip_Member> members;
          for (int m = 0; m < op.member_count; m++) {
            GSHSCENE_Clip_Member member{};
            member.resource = polygon(op.members[m].points, op.members[m].point_count);
            member.rule = GSHGEOM_FILL_NONZERO;
            gshgeom_map_identity(2, &member.map);
            members.push_back(member);
          }
          const GSHSCENE_Resource * u = nullptr;
          EXPECT_EQ(gshscene_store_intern_clip_union(st.store, members.data(), members.size(), &u), GSHSCENE_OK);
          st.held.push_back(u);
          EXPECT_EQ(gshscene_list_clip_push(l, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
          break;
        }
        default:
          EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
          break;
      }
    }
  }
};

struct Seen {
  GSHCPU_Rect region{};
  std::vector<double> root;
};

void observe(void * ctx, GSHCPU_Rect region, const double * root, const uint8_t *) {
  Seen * s = (Seen *)ctx;
  s->region = region;
  s->root.assign(root, root + 4u * (size_t)(region.x1 - region.x0) * (size_t)(region.y1 - region.y0));
}

/** Draw scene @p k; the premultiplied layer before the write over the width by height pixels. */
std::vector<double> draw(const pv::VScene & sc, int scale = 1) {
  Scene s;
  s.foreground.rgba[0] = sc.foreground[0];
  s.foreground.rgba[1] = sc.foreground[1];
  s.foreground.rgba[2] = sc.foreground[2];
  s.foreground.rgba[3] = sc.foreground[3];
  const GSHSCENE_Resource * pat = s.pattern(*sc.pattern);
  GSHSCENE_List * l = s.st.open();
  gshscene_list_set_foreground(l, &s.foreground);
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_PATTERN;
  p.resource = pat;
  p.rgba[3] = sc.pattern->alpha;
  EXPECT_EQ(gshscene_list_fill(l, s.st.rect(0, 0, sc.width, sc.height), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Seen seen;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_render_sink_create_observed(nullptr, nullptr, nullptr, observe, &seen, &sink), GSHCPU_OK);
  GIMG_Raster * raster = rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, (uint32_t)(sc.width * scale), (uint32_t)(sc.height * scale));
  unsigned char * px = (unsigned char *)gimg_raster_pixels(raster);
  std::fill(px, px + gimg_raster_stride_bytes(raster) * (size_t)(sc.height * scale), 0);
  GSHSCENE_Target target{};
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  {
    const double m[4] = {(double)scale, 0.0, 0.0, (double)scale};
    const double t[2] = {0.0, 0.0};
    EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &target.map), GSHGEOM_OK);
  }
  GSHSCENE_Report * report = nullptr;
  EXPECT_EQ(gshscene_report_create(nullptr, nullptr, &report), GSHSCENE_OK);
  GSHSCENE_Render_Request request{};
  request.list = l;
  request.target = &target;
  request.report = report;
  EXPECT_EQ(gshscene_sink_render(sink, &request), GSHSCENE_OK) << sc.name;
  GSHSCENE_Report_Summary sum{};
  gshscene_report_summary(report, &sum);
  EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_REFUSED], 0u) << sc.name;
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gimg_raster_destroy(raster);
  return seen.root;
}

} // namespace

/**
 * The bound at which the sink agrees with the oracle on a scene, at every pixel and channel: the 8-bit rounding of a coverage and the
 * seam bound of CK4, which is 2e-2 over the 360-case sweep of the vectors.  It is the same for every scene, those whose tile holds a
 * gradient or an image included: the oracle takes such a paint at the pixel's center in the copy's own frame, as the sink does (G12,
 * the image paint's sampling), and weighs it by the area.
 */
constexpr double kBound = 2e-2;

double bound_of(const std::string &) {
  return kBound;
}

TEST(RenderPatternVectors, EveryScene) {
  const char * only = std::getenv("PATTERN_SCENE");
  if (only && !*only) {
    only = nullptr;
  }
  double worst_all = 0.0;
  for (int k = 0; k < pv::kSceneCount; k++) {
    const pv::VScene & sc = pv::kScenes[k];
    if (only && std::string(only) != sc.name) {
      continue;
    }
    std::vector<double> got = draw(sc);
    ASSERT_EQ(got.size(), (size_t)sc.width * (size_t)sc.height * 4u) << sc.name;
    double worst = 0.0;
    size_t at = 0;
    for (size_t i = 0; i < got.size(); i++) {
      double e = std::fabs(got[i] - sc.pixels[i]);
      if (e > worst) {
        worst = e;
        at = i;
      }
    }
    std::printf("%-34s %dx%d worst %.3e at pixel %zu channel %zu (got %.6f want %.6f)\n", sc.name, sc.width, sc.height, worst, at / 4,
        at % 4, got[at], sc.pixels[at]);
    worst_all = std::max(worst_all, worst);
    EXPECT_LE(worst, bound_of(sc.name)) << sc.name;
    if (std::getenv("PATTERN_DUMP") && *std::getenv("PATTERN_DUMP")) {
      for (int y = 0; y < sc.height; y++) {
        for (int x = 0; x < sc.width; x++) {
          const size_t i = 4u * ((size_t)y * (size_t)sc.width + (size_t)x);
          std::printf("%5.3f/%5.3f ", got[i + 3], sc.pixels[i + 3]);
        }
        std::printf("\n");
      }
    }
  }
  std::printf("worst over all scenes: %.3e\n", worst_all);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
