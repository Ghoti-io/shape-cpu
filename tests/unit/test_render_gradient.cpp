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
 * Gradients (color-paint story P2; shape-scene's G1 to G14, S17; S11 here): the
 * evaluation against the gradient oracle's committed vectors (shape-scene's
 * tests/gradient_vectors.h, installed), the sink's wiring of it (the sample
 * point, the composed chain, the conversion into the layer's space), the
 * notices and the sink's own limit.  The goldens, damage, caller memory, cancel
 * and allocation failure of the gradient fixtures are the fixture-wide tests'.
 */

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/gradient_vectors.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rh;

namespace {

GSHGEOM_Map map_of(const gradient_vectors::VMap & v) {
  const double m[4] = {v.m[0], v.m[1], v.m[2], v.m[3]};
  const double t[2] = {v.m[4], v.m[5]};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

/** A vector case's gradient, its stops solid in the target's space (the vectors' stops are resolved already). */
GSHSCENE_Gradient content_of(const gradient_vectors::VCase & c, std::vector<GSHSCENE_Gradient_Stop> & stops) {
  GSHSCENE_Gradient g{};
  g.kind = (uint32_t)c.kind;
  if (c.kind == GSHSCENE_GRADIENT_LINEAR) {
    g.p0[0] = c.geom[0];
    g.p0[1] = c.geom[1];
    g.p1[0] = c.geom[2];
    g.p1[1] = c.geom[3];
    g.d[0] = c.geom[4];
    g.d[1] = c.geom[5];
  } else if (c.kind == GSHSCENE_GRADIENT_RADIAL) {
    g.c0[0] = c.geom[0];
    g.c0[1] = c.geom[1];
    g.r0 = c.geom[2];
    g.c1[0] = c.geom[3];
    g.c1[1] = c.geom[4];
    g.r1 = c.geom[5];
  } else {
    g.center[0] = c.geom[0];
    g.center[1] = c.geom[1];
    g.a0 = c.geom[2];
    g.a1 = c.geom[3];
    g.angle_unit = (uint32_t)c.unit;
  }
  g.extend = (uint32_t)c.extend;
  g.premultiplied = (uint32_t)c.premultiplied;
  g.map = map_of(c.chain[c.chain_count - 1]);
  stops.clear();
  for (int i = 0; i < c.stop_count; i++) {
    GSHSCENE_Gradient_Stop s{};
    s.offset = c.stops[i].offset;
    s.color.kind = GSHSCENE_PAINT_SOLID;
    for (int k = 0; k < 4; k++) {
      s.color.rgba[k] = c.stops[i].rgba[k];
    }
    stops.push_back(s);
  }
  g.stop_count = stops.size();
  g.stops = stops.empty() ? nullptr : stops.data();
  return g;
}

/** The chain of a vector case composed (target first in the vector, the gradient's map its leaf). */
GSHGEOM_Map composed_of(const gradient_vectors::VCase & c) {
  std::vector<GSHGEOM_Map> chain;
  for (int i = 1; i < c.chain_count; i++) {
    chain.push_back(map_of(c.chain[i]));
  }
  GSHGEOM_Map target = map_of(c.chain[0]);
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_compose_chain(chain.data(), chain.size(), &target, &out), GSHGEOM_OK) << c.name;
  return out;
}

double rel(double got, double want) {
  double scale = std::fabs(want) > 1.0 ? std::fabs(want) : 1.0;
  return std::fabs(got - want) / scale;
}

GSHSCENE_Paint gradient_paint(const GSHSCENE_Resource * g, double alpha) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_GRADIENT;
  p.resource = g;
  p.rgba[3] = alpha;
  return p;
}

GSHSCENE_Paint solid_in(double r, double g, double b, double a, GSHSCENE_Color_Space space) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_SOLID;
  p.rgba[0] = r;
  p.rgba[1] = g;
  p.rgba[2] = b;
  p.rgba[3] = a;
  p.space = space;
  return p;
}

GSHSCENE_Color_Space space_of(GCOL_Primaries p, GCOL_Transfer t, double gamma = 0.0) {
  GSHSCENE_Color_Space s{};
  s.primaries = p;
  s.transfer = t;
  s.gamma = gamma;
  return s;
}

const GSHSCENE_Resource * intern(Store & s, GSHSCENE_Gradient g, std::vector<GSHSCENE_Gradient_Stop> stops) {
  if (g.map.dim == 0) {
    gshgeom_map_identity(2, &g.map);
  }
  g.stops = stops.data();
  g.stop_count = stops.size();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_gradient(s.store, &g, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

GSHGEOM_Map matrix(double a, double b, double c, double d, double e, double f) {
  const double m[4] = {a, b, c, d};
  const double t[2] = {e, f};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

/** What the observer saw last: the root layer before its one rounding. */
struct Seen {
  GSHCPU_Rect region{};
  std::vector<double> root;
  std::vector<uint8_t> touched;
};

void observe(void * ctx, GSHCPU_Rect region, const double * root, const uint8_t * touched) {
  Seen * s = (Seen *)ctx;
  size_t n = (size_t)(region.x1 - region.x0) * (size_t)(region.y1 - region.y0);
  s->region = region;
  s->root.assign(root, root + 4 * n);
  s->touched.assign(touched, touched + n);
}

} // namespace

/* --- caps (S2, S17) ------------------------------------------------------------------------------------------ */

TEST(RenderGradientCaps, EveryKindIsDrawnInItsStatedSpace) {
  SinkRef sink;
  for (GSHCPU_Format format : RF_FORMATS) {
    RasterRef r(rf_raster(format, 4, 4));
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Sink_Caps caps{};
    ASSERT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
    EXPECT_EQ(caps.gradient_kinds, (1u << GSHSCENE_GRADIENT_LINEAR) | (1u << GSHSCENE_GRADIENT_RADIAL) |
        (1u << GSHSCENE_GRADIENT_SWEEP));
    EXPECT_TRUE(caps.flags & GSHSCENE_CAP_GRADIENT_SPACES);
  }
}

/* --- the oracle's vectors (G4 to G11) --------------------------------------------------------------------------- */

TEST(RenderGradientVectors, TheEvaluationAgreesWithTheOracleWithinTheStatedTolerance) {
  using gradient_vectors::kGradientCases;
  const size_t n = sizeof kGradientCases / sizeof kGradientCases[0];
  double worst[4] = {0, 0, 0, 0};  // by kind: color
  double worst_t[4] = {0, 0, 0, 0};
  size_t drawn = 0;
  size_t nothing = 0;
  size_t exact = 0;
  size_t exact_equal = 0;
  size_t table_checked = 0;
  size_t table_close = 0;
  Store s;
  for (size_t i = 0; i < n; i++) {
    const gradient_vectors::VCase & c = kGradientCases[i];
    std::vector<GSHSCENE_Gradient_Stop> stops;
    GSHSCENE_Gradient g = content_of(c, stops);
    // What draws nothing as a whole (G10, G11) is the plan's predicate, which the sink skips by: build the fill.
    {
      GSHSCENE_Gradient gi = g;
      gi.stops = stops.empty() ? nullptr : stops.data();
      const GSHSCENE_Resource * r = nullptr;
      ASSERT_EQ(gshscene_store_intern_gradient(s.store, &gi, &r), GSHSCENE_OK) << c.name;
      s.held.push_back(r);
      GSHSCENE_Item item{};
      item.kind = GSHSCENE_ITEM_FILL;
      item.map = map_of(c.chain[c.chain_count - 2]);
      item.paint = gradient_paint(r, c.alpha);
      std::vector<GSHGEOM_Map> instances;
      for (int k = 1; k < c.chain_count - 2; k++) {
        instances.push_back(map_of(c.chain[k]));
      }
      GSHGEOM_Map target = map_of(c.chain[0]);
      GSHSCENE_Reason notice = gshscene_gradient_notice(&item, instances.empty() ? nullptr : instances.data(),
          instances.size(), &target);
      if (!c.drawn && c.reason == 3) {
        EXPECT_EQ(notice, GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << c.name;
        nothing++;
        continue;
      }
      if (!c.drawn && c.reason == 2) {
        EXPECT_EQ(notice, GSHSCENE_REASON_GRADIENT_DEGENERATE) << c.name;
      } else {
        EXPECT_EQ(notice, (GSHSCENE_Reason)0) << c.name;
      }
    }
    std::vector<GSHCPU_Stop> line(stops.size());
    for (size_t k = 0; k < stops.size(); k++) {
      line[k].offset = stops[k].offset;
      for (int j = 0; j < 4; j++) {
        line[k].rgba[j] = stops[k].color.rgba[j];
      }
    }
    // G2: the line sorted stably by offset, as the store keeps it (the vectors give the stops as written).
    std::stable_sort(line.begin(), line.end(),
        [](const GSHCPU_Stop & x, const GSHCPU_Stop & y) { return x.offset < y.offset; });
    gshcpu_render_gradient_stops(line.data(), line.size(), g.premultiplied);
    GSHGEOM_Map composed = composed_of(c);
    GSHCPU_Gradient gr;
    ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, line.data(), &composed, c.alpha), GSHCPU_OK) << c.name;
    double out[4] = {0, 0, 0, 0};
    double px = 0;
    double py = 0;
    double t = 0;
    gshcpu_render_gradient_unmap(&gr, c.q[0], c.q[1], &px, &py);
    int got = gshcpu_render_gradient_param(&gr, px, py, &t);
    if (!c.drawn) {
      EXPECT_EQ(got, 0) << c.name << ": the oracle draws nothing here (reason " << c.reason << ")";
      EXPECT_EQ(gshcpu_render_gradient_at(&gr, c.q[0], c.q[1], out), 0) << c.name;
      nothing++;
      continue;
    }
    ASSERT_EQ(got, 1) << c.name;
    ASSERT_EQ(gshcpu_render_gradient_at(&gr, c.q[0], c.q[1], out), 1) << c.name;
    drawn++;
    bool step = c.kind == GSHSCENE_GRADIENT_SWEEP && c.geom[2] == c.geom[3]; // a0 = a1: pad's step, t is not a number
    if (!step) {
      double e = rel(t, c.t);
      worst_t[c.kind] = e > worst_t[c.kind] ? e : worst_t[c.kind];
      EXPECT_LE(e, GSHCPU_RENDER_GRADIENT_TOLERANCE) << c.name << " t " << t << " want " << c.t;
    }
    bool equal = true;
    for (int k = 0; k < 4; k++) {
      double e = rel(out[k], c.rgba[k]);
      worst[c.kind] = e > worst[c.kind] ? e : worst[c.kind];
      EXPECT_LE(e, GSHCPU_RENDER_GRADIENT_TOLERANCE) << c.name << " component " << k << ": " << out[k] << " want "
                                                     << c.rgba[k];
      equal = equal && out[k] == c.rgba[k];
    }
    if (c.exact) {
      exact++;
      exact_equal += equal ? 1 : 0;
    }
    // The fast fill coverage's table stands in for the evaluation to the spacing of its points.
    {
      std::vector<double> table(4u * (GSHCPU_RENDER_GRADIENT_TABLE + 1u));
      GSHCPU_Convert same;
      GSHSCENE_Color_Space space{};
      gshcpu_render_convert_prepare(&same, &space, &space);
      gshcpu_render_gradient_table(&gr, &same, table.data());
      const uint8_t covered = 255;
      uint32_t index = 0;
      gshcpu_render_gradient_table_row(&gr, c.q[0], c.q[1], 1, &covered, &index);
      ASSERT_NE(index, GSHCPU_RENDER_GRADIENT_NONE) << c.name;
      ASSERT_LE(index, GSHCPU_RENDER_GRADIENT_TABLE) << c.name;
      const double want[4] = {out[0] * out[3], out[1] * out[3], out[2] * out[3], out[3]};
      double worst_entry = 0;
      for (int k = 0; k < 4; k++) {
        worst_entry = std::max(worst_entry, std::fabs(table[4u * index + (size_t)k] - want[k]));
      }
      table_checked++;
      table_close += worst_entry <= 0.02 ? 1 : 0;
    }
  }
  EXPECT_GE(table_close * 100u, table_checked * 97u) << "the table matches the evaluation but at a step between its points";
  std::printf("gradient vectors: %zu points, %zu drawn, %zu nothing; worst color error linear %.3g radial %.3g sweep "
              "%.3g; worst t linear %.3g radial %.3g sweep %.3g (tolerance %g); %zu of %zu exact cases bit for bit\n",
      n, drawn, nothing, worst[1], worst[2], worst[3], worst_t[1], worst_t[2], worst_t[3],
      GSHCPU_RENDER_GRADIENT_TOLERANCE, exact_equal, exact);
  EXPECT_EQ(drawn + nothing, n);
  EXPECT_GT(drawn, 1000u);
}

/* --- the sink's wiring: sample point, chain, conversion ---------------------------------------------------------- */

// Every pixel of a fill covering the raster is G12's source at the pixel's center under the chain T M G composed by
// shape-geometry, interpolated in its space and converted into the target's, composited once over the backdrop:
// the observer's root equals that, computed with the same primitives, bit for bit.
TEST(RenderGradientSink, EachPixelIsTheSourceAtItsCenterUnderTheComposedChain) {
  GSHSCENE_Color_Space lin = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR);
  GSHSCENE_Color_Space p3 = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
  for (uint32_t kind = GSHSCENE_GRADIENT_LINEAR; kind < GSHSCENE_GRADIENT_KIND_COUNT; kind++) {
    for (uint32_t extend = 0; extend < GSHSCENE_EXTEND_COUNT; extend++) {
      Store s;
      GSHSCENE_Gradient g{};
      g.kind = kind;
      g.extend = extend;
      g.space = kind == GSHSCENE_GRADIENT_RADIAL ? p3 : lin;
      g.premultiplied = extend == GSHSCENE_EXTEND_REFLECT ? 1u : 0u;
      if (kind == GSHSCENE_GRADIENT_LINEAR) {
        g.p0[0] = 2;
        g.p1[0] = 7;
        g.p1[1] = 1;
      } else if (kind == GSHSCENE_GRADIENT_RADIAL) {
        g.c0[0] = 1;
        g.c0[1] = 2;
        g.r0 = 0.5;
        g.c1[0] = 12;
        g.c1[1] = 3;
        g.r1 = 2.5; // the start circle outside the end: a cone, with pixels outside it
      } else {
        g.center[0] = 5;
        g.center[1] = 4;
        g.angle_unit = GSHGEOM_ANGLE_DEGREES;
        g.a0 = 20;
        g.a1 = 200;
      }
      GSHGEOM_Map rot;
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 25.0, &rot);
      g.map = rot;
      const GSHSCENE_Resource * gradient = intern(s, g,
          {GSHSCENE_Gradient_Stop{0.0, solid_in(1.0, 0.2, 0.1, 1.0, lin)},
              GSHSCENE_Gradient_Stop{0.6, solid_in(0.1, 0.9, 0.3, 0.4, space_of(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_SRGB))},
              GSHSCENE_Gradient_Stop{1.0, solid_in(0.2, 0.1, 0.8, 0.9, lin)}});
      GSHSCENE_List * l = s.open();
      GSHGEOM_Map item = matrix(1.25, 0.0, 0.0, 0.75, -3.0, 2.0);
      GSHSCENE_Paint paint = gradient_paint(gradient, 0.8);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(-100, -100, 100, 100), &item, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0),
          GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      Seen seen;
      GSHSCENE_Sink * sink = nullptr;
      ASSERT_EQ(gshcpu_render_sink_create_observed(nullptr, nullptr, nullptr, observe, &seen, &sink), GSHCPU_OK);
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, 20, 14));
      GSHGEOM_Map tmap = matrix(1.5, 0.0, 0.0, 1.5, 1.0, 0.5);
      GSHSCENE_Color_Space target_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
      GSHSCENE_Target t = target_of(r.raster, &tmap);
      t.blend_space = target_space;
      ReportRef report;
      GSHSCENE_Render_Request req = request_of(l, &t, report.report);
      ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
      EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
      // The expectation, from the primitives.
      GSHGEOM_Map chain[2] = {item, rot};
      GSHGEOM_Map composed;
      ASSERT_EQ(gshgeom_map_compose_chain(chain, 2, &tmap, &composed), GSHGEOM_OK);
      GSHSCENE_Color fg;
      gshscene_color_init(&fg);
      GSHSCENE_Color_Space into;
      std::vector<GSHCPU_Stop> line(3);
      ASSERT_EQ(gshscene_gradient_resolve(gradient, &fg, nullptr, &target_space, &into, line.data(), 3), GSHSCENE_OK);
      GSHSCENE_Gradient stored;
      ASSERT_EQ(gshscene_resource_gradient(gradient, &stored), GSHSCENE_OK);
      gshcpu_render_gradient_stops(line.data(), 3, stored.premultiplied);
      GSHCPU_Gradient gr;
      ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &stored, line.data(), &composed, 0.8), GSHCPU_OK);
      GSHCPU_Convert conv;
      gshcpu_render_convert_prepare(&conv, &into, &target_space);
      RasterRef backdrop(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, 20, 14));
      size_t w = 20;
      int outside = 0;
      for (int64_t y = 0; y < 14; y++) {
        for (int64_t x = 0; x < 20; x++) {
          double d[4];
          gshcpu_render_load(GSHCPU_FORMAT_RGBAF32_STRAIGHT,
              (const unsigned char *)gimg_raster_pixels_const(backdrop.raster) + (size_t)y * gimg_raster_stride_bytes(backdrop.raster),
              (size_t)x, d);
          double c[4];
          if (gshcpu_render_gradient_at(&gr, (double)x + 0.5, (double)y + 0.5, c)) {
            gshcpu_render_convert_straight(&conv, c, c);
            double src[4] = {c[0] * c[3], c[1] * c[3], c[2] * c[3], c[3]};
            gshcpu_render_apply(d, src, 1.0, GSHSCENE_COMPOSITE_SRC_OVER);
          } else {
            outside++;
          }
          for (int k = 0; k < 4; k++) {
            ASSERT_EQ(seen.root[4 * ((size_t)y * w + (size_t)x) + (size_t)k], d[k])
                << "kind " << kind << " extend " << extend << " pixel " << x << "," << y << " component " << k;
          }
        }
      }
      if (kind == GSHSCENE_GRADIENT_RADIAL && extend == GSHSCENE_EXTEND_PAD) {
        EXPECT_GT(outside, 0) << "the cone leaves pixels outside it, which keep the backdrop";
      }
      EXPECT_EQ(report.summary().by_class[GSHSCENE_ENTRY_REFUSED], 0u);
    }
  }
}

// One gradient in two fills is the same gradient in both (resolved once a render, S17), and its foreground and
// palette stops take the drawing list's parameters: a changed foreground changes the pixels.
TEST(RenderGradientSink, ParameterStopsResolveAgainstTheDrawingList) {
  Store s;
  GSHSCENE_Paint fgp{};
  fgp.kind = GSHSCENE_PAINT_FOREGROUND;
  fgp.rgba[3] = 1.0;
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 16;
  const GSHSCENE_Resource * gradient =
      intern(s, g, {GSHSCENE_Gradient_Stop{0.0, fgp}, GSHSCENE_Gradient_Stop{1.0, solid_in(1, 1, 1, 1, GSHSCENE_Color_Space{})}});
  std::string first;
  for (double red : {0.2, 0.9}) {
    GSHSCENE_List * l = s.open();
    GSHSCENE_Color fg;
    gshscene_color_init(&fg);
    fg.rgba[0] = red;
    ASSERT_EQ(gshscene_list_set_foreground(l, &fg), GSHSCENE_OK);
    GSHSCENE_Paint p = gradient_paint(gradient, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 4), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 4, 16, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    SinkRef sink;
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 8));
    GSHSCENE_Target t = target_of(r.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    for (uint32_t x = 0; x < 16; x++) {
      EXPECT_EQ(r.pixel(x, 1), r.pixel(x, 6)) << "the two fills of one gradient at column " << x;
    }
    if (first.empty()) {
      first = r.bytes();
    } else {
      EXPECT_NE(r.bytes(), first) << "the foreground stop follows the list's foreground";
    }
  }
}

// Each gradient keeps its own stops when another is resolved between two of its fills, and a clip weighs a gradient
// fill as it weighs a solid one (D8).
TEST(RenderGradientSink, GradientsKeepTheirStopsAndClipsWeighThem) {
  Store s;
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 16;
  const GSHSCENE_Resource * a = intern(s, g, {GSHSCENE_Gradient_Stop{0.0, solid_in(1, 0, 0, 1, GSHSCENE_Color_Space{})},
      GSHSCENE_Gradient_Stop{1.0, solid_in(1, 1, 0, 1, GSHSCENE_Color_Space{})}});
  const GSHSCENE_Resource * b = intern(s, g, {GSHSCENE_Gradient_Stop{0.0, solid_in(0, 0, 1, 1, GSHSCENE_Color_Space{})},
      GSHSCENE_Gradient_Stop{1.0, solid_in(0, 1, 1, 1, GSHSCENE_Color_Space{})}});
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint pa = gradient_paint(a, 1.0);
  GSHSCENE_Paint pb = gradient_paint(b, 1.0);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 2), nullptr, GSHGEOM_FILL_NONZERO, &pa, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 2, 16, 4), nullptr, GSHGEOM_FILL_NONZERO, &pb, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 4, 16, 6), nullptr, GSHGEOM_FILL_NONZERO, &pa, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l, s.rect(0, 0, 7.5, 10), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 6, 16, 8), nullptr, GSHGEOM_FILL_NONZERO, &pb, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 10));
  RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 10));
  GSHSCENE_Target t = target_of(r.raster);
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  for (uint32_t x = 0; x < 16; x++) {
    EXPECT_EQ(r.pixel(x, 0), r.pixel(x, 4)) << "a's fills, before and after b's, at column " << x;
    EXPECT_NE(r.pixel(x, 0), r.pixel(x, 2)) << "a and b differ at column " << x;
    if (x < 7) {
      EXPECT_EQ(r.pixel(x, 6), r.pixel(x, 2)) << "inside the clip, b as unclipped, column " << x;
    } else if (x == 7) {
      EXPECT_NE(r.pixel(x, 6), r.pixel(x, 2)) << "half inside the clip: weighed by it";
      EXPECT_NE(r.pixel(x, 6), fresh.pixel(x, 6)) << "half inside the clip: drawn";
    } else {
      EXPECT_EQ(r.pixel(x, 6), fresh.pixel(x, 6)) << "outside the clip, the backdrop, column " << x;
    }
  }
}

/* --- what draws nothing, and the sink's own limit ------------------------------------------------------------------ */

// G10 and G11: a degenerate gradient or a flat map draws nothing; the plan's notice says so; strict is OK.
TEST(RenderGradientSink, WhatDrawsNothingLeavesTheTargetAsItWas) {
  Store s;
  GSHSCENE_Paint red = solid_in(1, 0, 0, 1, GSHSCENE_Color_Space{});
  GSHSCENE_Paint blue = solid_in(0, 0, 1, 1, GSHSCENE_Color_Space{});
  std::vector<std::pair<GSHSCENE_Gradient, const char *>> cases;
  GSHSCENE_Gradient line{};
  line.kind = GSHSCENE_GRADIENT_LINEAR;
  line.p0[0] = line.p1[0] = 4;
  cases.push_back({line, "p0 = p1"});
  GSHSCENE_Gradient circles{};
  circles.kind = GSHSCENE_GRADIENT_RADIAL;
  circles.c0[0] = circles.c1[0] = 4;
  circles.r0 = circles.r1 = 3;
  cases.push_back({circles, "equal circles"});
  GSHSCENE_Gradient angles{};
  angles.kind = GSHSCENE_GRADIENT_SWEEP;
  angles.angle_unit = GSHGEOM_ANGLE_DEGREES;
  angles.a0 = angles.a1 = 45;
  angles.extend = GSHSCENE_EXTEND_REPEAT;
  cases.push_back({angles, "a0 = a1 under repeat"});
  GSHSCENE_Gradient flat{};
  flat.kind = GSHSCENE_GRADIENT_LINEAR;
  flat.p1[0] = 8;
  flat.map = matrix(1, 2, 2, 4, 0, 0);
  cases.push_back({flat, "a gradient map of rank one"});
  for (auto & c : cases) {
    const GSHSCENE_Resource * gradient = intern(s, c.first, {GSHSCENE_Gradient_Stop{0, red}, GSHSCENE_Gradient_Stop{1, blue}});
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint p = gradient_paint(gradient, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 12, 10), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    for (GSHCPU_Format format : RF_FORMATS) {
      SinkRef sink;
      RasterRef r(rf_raster(format, 12, 10));
      RasterRef fresh(rf_raster(format, 12, 10));
      GSHSCENE_Target t = target_of(r.raster);
      ReportRef report;
      GSHSCENE_Render_Request req = request_of(l, &t, report.report);
      req.flags = GSHSCENE_RENDER_STRICT;
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << c.second;
      EXPECT_EQ(r.bytes(), fresh.bytes()) << c.second;
      EXPECT_EQ(report.summary().by_class[GSHSCENE_ENTRY_NOTICE], 1u) << c.second;
    }
  }
}

// S6, S4: a gradient fill whose whole-render rectangle maps outside G14's sampling domain is refused while drawing
// (RESOURCE_LIMIT), the same with a damage that leaves the item undrawn; just inside the domain it draws.
TEST(RenderGradientSink, AFillMappedOutsideTheDomainIsRefusedWhateverTheDamage) {
  Store s;
  GSHSCENE_Paint red = solid_in(1, 0, 0, 1, GSHSCENE_Color_Space{});
  GSHSCENE_Paint blue = solid_in(0, 0, 1, 1, GSHSCENE_Color_Space{});
  // G: gradient space to local, local = 2^-64 p + 2^64: a device point near the origin is p near -2^128.
  GSHSCENE_Gradient far{};
  far.kind = GSHSCENE_GRADIENT_LINEAR;
  far.p1[0] = 1;
  far.map = matrix(0x1p-64, 0, 0, 0x1p-64, 0x1p64, 0x1p64);
  GSHSCENE_Gradient near = far;
  near.map = matrix(0x1p-64, 0, 0, 0x1p-64, 0, 0); // p = 2^64 q: inside
  for (int which = 0; which < 2; which++) {
    const GSHSCENE_Resource * gradient =
        intern(s, which ? near : far, {GSHSCENE_Gradient_Stop{0, red}, GSHSCENE_Gradient_Stop{1, blue}});
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint p = gradient_paint(gradient, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 1, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(8, 8, 10, 10), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    GSHSCENE_Rect away{8, 8, 12, 12};
    for (const GSHSCENE_Rect * damage : {(const GSHSCENE_Rect *)nullptr, (const GSHSCENE_Rect *)&away}) {
      SinkRef sink;
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 12, 12));
      GSHSCENE_Target t = target_of(r.raster);
      t.damage = damage;
      ReportRef report;
      GSHSCENE_Render_Request req = request_of(l, &t, report.report);
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
      auto e = report.entries();
      if (which == 0) {
        ASSERT_EQ(e.size(), 1u) << (damage ? "with a damage" : "whole");
        EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_REFUSED);
        EXPECT_EQ(e[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
        EXPECT_EQ(e[0].chain[0], 0u);
        req.flags = GSHSCENE_RENDER_STRICT;
        ReportRef strict;
        req.report = strict.report;
        EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_REFUSED);
      } else {
        EXPECT_TRUE(e.empty()) << "inside the domain";
      }
    }
  }
}

// The inverse of the composed map: a map whose inverse passes the double range is the sink's own limit, never a NaN.
TEST(RenderGradientParts, AMapWhoseInverseIsNotFiniteIsRefusedByPrepare) {
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 1;
  GSHCPU_Stop stops[2] = {};
  stops[1].offset = 1;
  GSHCPU_Gradient gr;
  GSHGEOM_Map fine = matrix(0x1p-500, 0, 0, 0x1p-500, 0, 0);
  EXPECT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &fine, 1.0), GSHCPU_OK) << "a tiny scale inverts exactly";
  EXPECT_EQ(gr.inv[0], 0x1p500);
  GSHGEOM_Map lost = matrix(0x1p-1030, 0, 0, 0x1p-1030, 0, 0);
  EXPECT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &lost, 1.0), GSHCPU_ERR_LIMIT) << "2^1030 is no double";
  GSHGEOM_Map rank1 = matrix(1, 2, 2, 4, 0, 0);
  EXPECT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &rank1, 1.0), GSHCPU_ERR_LIMIT);
  GSHGEOM_Map sheared = matrix(1, 0x1p-60, 0x1p-60 * 3, 1, 5, 7);
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &sheared, 1.0), GSHCPU_OK);
  double px;
  double py;
  gshcpu_render_gradient_unmap(&gr, 6, 8, &px, &py);
  EXPECT_NEAR(px, 1.0 - 0x1p-60, 1e-30);
  EXPECT_NEAR(py, 1.0 - 0x1p-60 * 3, 1e-30);
}

// G7, G8: the zero tests are made on the exact values, so a denominator or an `a` that the doubles make nonzero is
// never taken as zero, and one they make zero is.
TEST(RenderGradientParts, TheZeroTestsAreExact) {
  GSHCPU_Stop stops[2] = {};
  stops[1].offset = 1;
  stops[1].rgba[3] = 1;
  GSHGEOM_Map id = matrix(1, 0, 0, 1, 0, 0);
  GSHCPU_Gradient gr;
  // A cone whose circles touch inside (|c1 - c0| = |r1 - r0|): a is exactly zero, and one ulp more radius is not.
  GSHSCENE_Gradient tangent{};
  tangent.kind = GSHSCENE_GRADIENT_RADIAL;
  tangent.c1[0] = 0.1;
  tangent.r1 = 0.1;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &tangent, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_EQ(gr.a, 0.0) << "0.1^2 - 0.1^2 is zero whatever the rounding";
  GSHSCENE_Gradient near = tangent;
  near.r1 = std::nextafter(0.1, 1.0);
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &near, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_LT(gr.a, 0.0) << "one ulp more radius";
  EXPECT_GT(gr.a, -1e-16);
  // (p1 - p0) . (-d_y, d_x) of decimals whose doubles are not parallel: the exact value is nonzero, and so is the
  // denominator taken from it, though the rounded products cancel.
  GSHSCENE_Gradient lin{};
  lin.kind = GSHSCENE_GRADIENT_LINEAR;
  lin.p0[0] = 0.1;
  lin.p1[0] = 0.30000000000000004;
  lin.p1[1] = 0.2;
  lin.d[0] = 0.1;
  lin.d[1] = 0.1;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &lin, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_NE(gr.den, 0.0);
  EXPECT_FALSE(gr.degenerate);
  // A difference no double holds: p1 - p0 = (1 + 2^-60, 1) against d = (1, 1) is exactly -2^-60, which the rounded
  // differences make zero; and the same in y.
  GSHSCENE_Gradient lx{};
  lx.kind = GSHSCENE_GRADIENT_LINEAR;
  lx.p0[0] = -0x1p-60;
  lx.p1[0] = 1.0;
  lx.p1[1] = 1.0;
  lx.d[0] = 1.0;
  lx.d[1] = 1.0;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &lx, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_EQ(gr.den, -0x1p-60);
  GSHSCENE_Gradient ly = lx;
  ly.p0[0] = 0.0;
  ly.p0[1] = -0x1p-60;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &ly, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_EQ(gr.den, 0x1p-60);
  // With d zero the denominator is |p1 - p0|^2 of the exact difference.
  GSHSCENE_Gradient sq{};
  sq.kind = GSHSCENE_GRADIENT_LINEAR;
  sq.p0[0] = -0x1p-60;
  sq.p1[0] = 1.0;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &sq, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_EQ(gr.den, 1.0 + 0x1p-59);
  // G8's a of a center difference no double holds: |(1 + 2^-60, 0)|^2 - 1 = 2^-59 + 2^-120, not zero.
  GSHSCENE_Gradient ra{};
  ra.kind = GSHSCENE_GRADIENT_RADIAL;
  ra.c0[0] = -0x1p-60;
  ra.c1[0] = 1.0;
  ra.r1 = 1.0;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &ra, stops, &id, 1.0), GSHCPU_OK);
  EXPECT_EQ(gr.a, 0x1p-59);
}

// G7: an isoline direction along an axis is the perpendicular of that axis, not p1 - p0's.
TEST(RenderGradientParts, AnAxisIsolineIsReadAsGiven) {
  GSHCPU_Stop stops[2] = {};
  stops[1].offset = 1;
  GSHGEOM_Map id = matrix(1, 0, 0, 1, 0, 0);
  for (int axis = 0; axis < 2; axis++) {
    GSHSCENE_Gradient g{};
    g.kind = GSHSCENE_GRADIENT_LINEAR;
    g.p1[0] = 4.0;
    g.p1[1] = 2.0;
    g.d[axis] = 1.0;
    GSHCPU_Gradient gr;
    ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &id, 1.0), GSHCPU_OK);
    double t = 0;
    ASSERT_EQ(gshcpu_render_gradient_param(&gr, 2.0, 7.0, &t), 1);
    // d = (0, 1): isolines are vertical, t = x / 4; d = (1, 0): horizontal, t = y / 2.
    EXPECT_EQ(t, axis == 1 ? 0.5 : 3.5) << "axis " << axis;
  }
}

// G4, G5: among stops at one offset the last given holds at that offset, inside the line and at its start.
TEST(RenderGradientParts, AtAnOffsetOfEqualStopsTheLastGivenHolds) {
  GSHCPU_Stop stops[5] = {};
  const double offsets[5] = {0.0, 0.0, 0.5, 0.5, 1.0};
  for (int i = 0; i < 5; i++) {
    stops[i].offset = offsets[i];
    stops[i].rgba[i < 4 ? i : 0] = 1.0;
    stops[i].rgba[3] = 1.0;
  }
  GSHGEOM_Map id = matrix(1, 0, 0, 1, 0, 0);
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 1.0;
  g.stop_count = 5;
  GSHCPU_Gradient gr;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &id, 1.0), GSHCPU_OK);
  double c[4];
  gshcpu_render_gradient_color(&gr, 0.5, c);
  EXPECT_EQ(c[3], 1.0);
  EXPECT_EQ(c[2], 0.0) << "the stop given third is past";
  EXPECT_EQ(c[0], 0.0);
  EXPECT_EQ(stops[3].rgba[3], 1.0);
  EXPECT_EQ(c[1] + c[2] + c[0], 0.0) << "the fourth stop (alpha only) holds at 0.5";
  gshcpu_render_gradient_color(&gr, 0.0, c);
  EXPECT_EQ(c[1], 1.0) << "the second stop holds at 0";
  gshcpu_render_gradient_color(&gr, -1.0, c);
  EXPECT_EQ(c[0], 1.0) << "pad below is the first stop";
}

// G8: the larger root whose radius is not positive is passed over for the smaller.
TEST(RenderGradientParts, TheLargestRootWithAPositiveRadiusIsTaken) {
  GSHCPU_Stop stops[2] = {};
  stops[1].offset = 1;
  GSHGEOM_Map id = matrix(1, 0, 0, 1, 0, 0);
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_RADIAL;
  g.r0 = 3.0;
  g.r1 = 1.0; // r(w) = 3 - 2w: at |p| = 4, w = 3.5 has r = -4 and w = -0.5 has r = 4
  GSHCPU_Gradient gr;
  ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &id, 1.0), GSHCPU_OK);
  double t = 0;
  ASSERT_EQ(gshcpu_render_gradient_param(&gr, 4.0, 0.0, &t), 1);
  EXPECT_EQ(t, -0.5);
}

// P2-4: a parameter past the double's reach is taken at that end, and no NaN reaches a color.
TEST(RenderGradientParts, AParameterPastTheRangeIsTakenAtItsEnd) {
  GSHCPU_Stop stops[3] = {};
  stops[0].rgba[0] = 1;
  stops[0].rgba[3] = 1;
  stops[1].offset = 0.5;
  stops[1].rgba[1] = 1;
  stops[1].rgba[3] = 1;
  stops[2].offset = 1;
  stops[2].rgba[2] = 1;
  stops[2].rgba[3] = 1;
  GSHGEOM_Map id = matrix(1, 0, 0, 1, 0, 0);
  for (uint32_t extend = 0; extend < GSHSCENE_EXTEND_COUNT; extend++) {
    GSHSCENE_Gradient g{};
    g.kind = GSHSCENE_GRADIENT_LINEAR;
    g.p1[0] = 1;
    g.extend = extend;
    g.stop_count = 3;
    GSHCPU_Gradient gr;
    ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &g, stops, &id, 1.0), GSHCPU_OK);
    for (double t : {(double)INFINITY, -(double)INFINITY, (double)NAN, 0x1p1000, -0x1p1000, GSHCPU_RENDER_T_LIMIT,
             -GSHCPU_RENDER_T_LIMIT}) {
      double c[4];
      gshcpu_render_gradient_color(&gr, t, c);
      for (int k = 0; k < 4; k++) {
        EXPECT_TRUE(std::isfinite(c[k])) << "extend " << extend << " t " << t;
      }
    }
    double hi[4];
    double lim[4];
    double lo[4];
    double low_lim[4];
    gshcpu_render_gradient_color(&gr, INFINITY, hi);
    gshcpu_render_gradient_color(&gr, GSHCPU_RENDER_T_LIMIT, lim);
    gshcpu_render_gradient_color(&gr, -INFINITY, lo);
    gshcpu_render_gradient_color(&gr, -GSHCPU_RENDER_T_LIMIT, low_lim);
    for (int k = 0; k < 4; k++) {
      EXPECT_EQ(hi[k], lim[k]) << "extend " << extend;
      EXPECT_EQ(lo[k], low_lim[k]) << "extend " << extend;
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
