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
 * @file
 *
 * The filter engine (filter.c, shape-scene filter.h FL1 to FL20) against the oracle's vectors (`tools/gen-filter-vectors.py`, exact
 * rationals, no shared code): every case within 1e-12, the scratch the engine asks for being enough and exactly the plan's, a scratch
 * too small refused, and the primitives and maps this build does not draw refused by name.
 */

#include <gtest/gtest.h>

#include "../filter_vectors.h"

#include "../../src/render/render_internal.h"

#include <cmath>
#include <cstring>
#include <array>
#include <vector>

namespace {

using namespace filter_vectors;

struct Built {
  std::vector<GSHSCENE_Filter_Primitive> prims;
  std::vector<std::vector<GSHSCENE_Filter_Input>> inputs;
  std::vector<std::vector<double>> values;
  std::vector<std::vector<double>> tables;
  GSHSCENE_Filter f;
};

/** The case's filter as the library's structs; the vectors of the arrays stay put once pushed (reserved up front). */
void build(const FCase & c, Built * b) {
  std::memset(&b->f, 0, sizeof b->f);
  b->prims.resize((size_t)c.nprims);
  b->inputs.resize((size_t)c.nprims);
  b->values.resize((size_t)c.nprims);
  b->tables.resize((size_t)c.nprims * 4u);
  for (int i = 0; i < c.nprims; i++) {
    const FPrim & v = c.prims[i];
    GSHSCENE_Filter_Primitive & p = b->prims[(size_t)i];
    std::memset(&p, 0, sizeof p);
    p.kind = (uint32_t)v.kind;
    p.color_space = (uint32_t)v.space;
    for (int k = 0; k < v.n_in; k++) {
      b->inputs[(size_t)i].push_back({(uint32_t)v.in_kind[k], (uint32_t)v.in_idx[k]});
    }
    p.input_count = (size_t)v.n_in;
    p.inputs = v.n_in ? b->inputs[(size_t)i].data() : nullptr;
    p.subregion_mask = (uint32_t)v.sub_mask;
    for (int k = 0; k < 4; k++) {
      p.subregion[k] = v.sub[k];
    }
    p.dx = v.dx;
    p.dy = v.dy;
    for (int k = 0; k < 4; k++) {
      p.color.rgba[k] = v.color[k];
    }
    if (v.color_space == 1) {
      p.color.space.primaries = GCOL_PRIMARIES_SRGB;
      p.color.space.transfer = GCOL_TRANSFER_LINEAR;
    } else if (v.color_space == 2) {
      p.color.space.primaries = GCOL_PRIMARIES_SRGB;
      p.color.space.transfer = GCOL_TRANSFER_SRGB;
    }
    p.opacity = v.opacity;
    p.mode = (uint32_t)v.mode;
    for (int k = 0; k < 4; k++) {
      p.k[k] = v.k[k];
    }
    if (v.nvals) {
      b->values[(size_t)i].assign(v.vals, v.vals + v.nvals);
      p.value_count = (size_t)v.nvals;
      p.values = b->values[(size_t)i].data();
    }
    p.sx = v.sx;
    p.sy = v.sy;
    if (v.kind == GSHSCENE_FILTER_MORPHOLOGY) {
      p.sx = v.rx;
      p.sy = v.ry;
      p.mode = v.dilate ? GSHSCENE_FILTER_MORPHOLOGY_DILATE : GSHSCENE_FILTER_MORPHOLOGY_ERODE;
    }
    p.edge_mode = (uint32_t)v.edge;
    if (v.kind == GSHSCENE_FILTER_TURBULENCE) {
      p.octaves = (uint32_t)v.octaves;
      p.stitch_tiles = (uint32_t)v.stitch;
      p.mode = v.noise ? GSHSCENE_FILTER_NOISE_FRACTAL : GSHSCENE_FILTER_NOISE_TURBULENCE;
      p.seed = v.seed;
    }
    if (v.kind == GSHSCENE_FILTER_DIFFUSE_LIGHTING || v.kind == GSHSCENE_FILTER_SPECULAR_LIGHTING) {
      p.surface_scale = v.surface;
      p.constant = v.kconst;
      p.exponent = v.spec_exp;
      p.light.kind = (uint32_t)v.light_kind;
      p.light.azimuth = v.light[0];
      p.light.elevation = v.light[1];
      for (int k = 0; k < 3; k++) {
        p.light.position[k] = v.light[2 + k];
        p.light.points_at[k] = v.light[5 + k];
      }
      p.light.spot_exponent = v.light[8];
      p.light.has_cone = (uint32_t)v.has_cone;
      p.light.cone_angle = v.light[9];
    }
    if (v.kind == GSHSCENE_FILTER_CONVOLVE_MATRIX) {
      p.order_x = (uint32_t)v.order_x;
      p.order_y = (uint32_t)v.order_y;
      p.target_x = (uint32_t)v.target_x;
      p.target_y = (uint32_t)v.target_y;
      p.divisor = v.divisor;
      p.bias = v.bias;
      p.preserve_alpha = (uint32_t)v.preserve;
    }
    if (v.kind == GSHSCENE_FILTER_DISPLACEMENT_MAP) {
      p.scale = v.scale;
      p.x_channel = (uint32_t)v.xch;
      p.y_channel = (uint32_t)v.ych;
    }
    if (v.kind == GSHSCENE_FILTER_COMPONENT_TRANSFER) {
      for (int k = 0; k < 4; k++) {
        GSHSCENE_Filter_Transfer & t = p.transfer[k];
        t.type = (uint32_t)v.ttype[k];
        if (v.tcount[k]) {
          b->tables[(size_t)i * 4u + (size_t)k].assign(v.tvals[k], v.tvals[k] + v.tcount[k]);
          t.count = (size_t)v.tcount[k];
          t.table = b->tables[(size_t)i * 4u + (size_t)k].data();
        }
        t.slope = v.tparam[k][0];
        t.intercept = v.tparam[k][1];
        if (v.ttype[k] == GSHSCENE_FILTER_TRANSFER_GAMMA) {
          t.amplitude = v.tparam[k][0];
          t.exponent = v.tparam[k][1];
          t.offset = v.tparam[k][2];
          t.slope = t.intercept = 0.0;
        }
      }
    }
  }
  b->f.region_mode = c.region_mode ? GSHSCENE_FILTER_REGION_AUTO : GSHSCENE_FILTER_REGION_EXPLICIT;
  for (int k = 0; k < 4; k++) {
    b->f.region[k] = c.region[k];
  }
  b->f.color_space = (uint32_t)c.space;
  b->f.primitive_count = (size_t)c.nprims;
  b->f.primitives = b->prims.data();
}

GSHGEOM_Map map_of(const double m[6]) {
  GSHGEOM_Map map;
  gshgeom_map_identity(2, &map);
  map.m[0][0] = m[0];
  map.m[0][1] = m[1];
  map.m[1][0] = m[2];
  map.m[1][1] = m[3];
  map.t[0] = m[4];
  map.t[1] = m[5];
  return map;
}

GSHSCENE_Color_Space srgb() {
  GSHSCENE_Color_Space s;
  std::memset(&s, 0, sizeof s);
  s.primaries = GCOL_PRIMARIES_SRGB;
  s.transfer = GCOL_TRANSFER_SRGB;
  return s;
}

}  // namespace

TEST(RenderFilter, EveryVectorIsTheOraclesWithinOneTrillionth) {
  size_t cases = 0;
  for (const FCase & c : kCases) {
    SCOPED_TRACE(c.name);
    Built b;
    build(c, &b);
    const size_t n = (size_t)c.w * (size_t)c.h;
    std::vector<double> layer(c.src, c.src + n * 4u);
    const size_t need = gshcpu_filter_scratch_doubles(&b.f, (size_t)c.w, (size_t)c.h);
    ASSERT_NE(need, SIZE_MAX);
    std::vector<double> scratch(need);
    const GSHGEOM_Map map = map_of(c.map);
    const GSHSCENE_Color_Space space = srgb();
    GSHCPU_Rect canvas = {0, 0, c.w, c.h};
    GSHCPU_Rect dirty{};
    ASSERT_EQ(gshcpu_filter_apply(&b.f, &map, &space, canvas, layer.data(), scratch.data(), scratch.size(), nullptr, nullptr, 0, &dirty), GSHCPU_OK);
    for (size_t q = 0; q < n * 4u; q++) {
      ASSERT_NEAR(layer[q], c.want[q], 1e-12) << "pixel " << q / 4u << " channel " << q % 4u;
    }
    cases++;
  }
  EXPECT_GE(cases, 59u);
}

TEST(RenderFilter, AScratchTooSmallIsRefusedAndNothingIsWritten) {
  const FCase & c = kCases[0];
  Built b;
  build(c, &b);
  const size_t n = (size_t)c.w * (size_t)c.h;
  std::vector<double> layer(c.src, c.src + n * 4u);
  const size_t need = gshcpu_filter_scratch_doubles(&b.f, (size_t)c.w, (size_t)c.h);
  std::vector<double> scratch(need);
  const GSHGEOM_Map map = map_of(c.map);
  const GSHSCENE_Color_Space space = srgb();
  GSHCPU_Rect canvas = {0, 0, c.w, c.h};
  GSHCPU_Rect dirty{};
  EXPECT_EQ(gshcpu_filter_apply(&b.f, &map, &space, canvas, layer.data(), scratch.data(), need - 1u, nullptr, nullptr, 0, &dirty), GSHCPU_ERR_LIMIT);
  EXPECT_EQ(std::memcmp(layer.data(), c.src, n * 4u * sizeof(double)), 0);
}

TEST(RenderFilter, WhatThisBuildDoesNotDrawIsRefusedByName) {
  const FCase & c = kCases[0];
  Built b;
  build(c, &b);
  const size_t n = (size_t)c.w * (size_t)c.h;
  std::vector<double> layer(c.src, c.src + n * 4u);
  std::vector<double> scratch(gshcpu_filter_scratch_doubles(&b.f, (size_t)c.w, (size_t)c.h));
  const GSHSCENE_Color_Space space = srgb();
  GSHCPU_Rect canvas = {0, 0, c.w, c.h};
  GSHCPU_Rect dirty{};
  // a rotated map
  GSHGEOM_Map turned = map_of(c.map);
  turned.m[0][0] = 0.8;
  turned.m[0][1] = -0.6;
  turned.m[1][0] = 0.6;
  turned.m[1][1] = 0.8;
  EXPECT_EQ(gshcpu_filter_apply(&b.f, &turned, &space, canvas, layer.data(), scratch.data(), scratch.size(), nullptr, nullptr, 0, &dirty),
      GSHCPU_ERR_UNSUPPORTED);
  // a primitive of a later stage
  b.prims[0].kind = GSHSCENE_FILTER_KIND_COUNT;
  const GSHGEOM_Map map = map_of(c.map);
  EXPECT_EQ(gshcpu_filter_apply(&b.f, &map, &space, canvas, layer.data(), scratch.data(), scratch.size(), nullptr, nullptr, 0, &dirty),
      GSHCPU_ERR_UNSUPPORTED);
  b.prims[0].kind = GSHSCENE_FILTER_OFFSET;
  EXPECT_EQ(gshcpu_filter_apply(nullptr, &map, &space, canvas, layer.data(), scratch.data(), scratch.size(), nullptr, nullptr, 0, &dirty),
      GSHCPU_ERR_INVALID);
  EXPECT_EQ(std::memcmp(layer.data(), c.src, n * 4u * sizeof(double)), 0);
}

namespace {

/** A one-primitive filter whose `IMAGE` draws slot @p slot of its picture (the engine reads only the slot). */
struct SlotFilter {
  GSHSCENE_Filter_Primitive prim;
  GSHSCENE_Filter f;
  SlotFilter(uint32_t slot, uint32_t space, uint32_t mask = 0, const double * sub = nullptr) {
    std::memset(&prim, 0, sizeof prim);
    prim.kind = GSHSCENE_FILTER_IMAGE;
    prim.use_picture = 1;
    prim.slot = slot;
    prim.subregion_mask = mask;
    for (int k = 0; sub && k < 4; k++) {
      prim.subregion[k] = sub[k];
    }
    std::memset(&f, 0, sizeof f);
    f.region_mode = GSHSCENE_FILTER_REGION_AUTO;
    f.color_space = space;
    f.primitive_count = 1;
    f.primitives = &prim;
  }
};

}  // namespace

TEST(RenderFilter, ASlotOfThePictureIsTheResultAndTheSourceGraphicIsNotRead) {
  const int w = 4, h = 3;
  const size_t n = (size_t)w * h;
  std::vector<double> layer(n * 4u, 0.0), slot(n * 4u, 0.0), want;
  for (size_t p = 0; p < n; p++) {
    layer[4 * p] = 0.5;
    layer[4 * p + 3] = 0.5;  // a half-red source, which the primitive does not read
    slot[4 * p + 1] = (double)(p % 3) * 0.25;  // premultiplied green with alpha the same
    slot[4 * p + 3] = (double)(p % 3) * 0.25;
  }
  want = slot;
  const GSHGEOM_Map map = map_of(std::array<double, 6>{1, 0, 0, 1, 0, 0}.data());
  const GSHSCENE_Color_Space space = srgb();
  GSHCPU_Rect canvas = {0, 0, w, h}, dirty{};
  for (uint32_t filter_space : {(uint32_t)GSHSCENE_FILTER_SPACE_SRGB, (uint32_t)GSHSCENE_FILTER_SPACE_LINEAR_RGB}) {
    SlotFilter sf(1, filter_space);  // the slot index is the sink's business, the engine is given the drawn layer
    std::vector<double> l = layer;
    std::vector<double> scratch(gshcpu_filter_scratch_doubles(&sf.f, (size_t)w, (size_t)h));
    const double * slots[1] = {slot.data()};
    ASSERT_EQ(gshcpu_filter_apply(&sf.f, &map, &space, canvas, l.data(), scratch.data(), scratch.size(), nullptr, slots, 1, &dirty), GSHCPU_OK);
    for (size_t q = 0; q < n * 4u; q++) {
      EXPECT_NEAR(l[q], want[q], 1e-9) << "space " << filter_space << " value " << q;  // the round trip through the primitive's space
    }
  }
}

TEST(RenderFilter, ASlotIsClippedToTheSubregionAndAMissingSlotIsRefused) {
  const int w = 4, h = 2;
  const size_t n = (size_t)w * h;
  std::vector<double> slot(n * 4u, 1.0);
  const GSHGEOM_Map map = map_of(std::array<double, 6>{1, 0, 0, 1, 0, 0}.data());
  const GSHSCENE_Color_Space space = srgb();
  GSHCPU_Rect canvas = {0, 0, w, h}, dirty{};
  const double sub[4] = {1, 0, 2, 2};  // x = 1, width 2
  SlotFilter sf(0, GSHSCENE_FILTER_SPACE_SRGB, 1u | 4u, sub);
  std::vector<double> l(n * 4u, 0.0);
  std::vector<double> scratch(gshcpu_filter_scratch_doubles(&sf.f, (size_t)w, (size_t)h));
  const double * slots[1] = {slot.data()};
  ASSERT_EQ(gshcpu_filter_apply(&sf.f, &map, &space, canvas, l.data(), scratch.data(), scratch.size(), nullptr, slots, 1, &dirty), GSHCPU_OK);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const double want = (x == 1 || x == 2) ? 1.0 : 0.0;
      EXPECT_EQ(l[4 * ((size_t)y * w + x) + 3], want) << x << "," << y;
    }
  }
  // no slots, too few entries, and an entry that was not drawn
  std::vector<double> l2(n * 4u, 0.25);
  EXPECT_EQ(gshcpu_filter_apply(&sf.f, &map, &space, canvas, l2.data(), scratch.data(), scratch.size(), nullptr, nullptr, 0, &dirty), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_filter_apply(&sf.f, &map, &space, canvas, l2.data(), scratch.data(), scratch.size(), nullptr, slots, 0, &dirty), GSHCPU_ERR_INVALID);  // fewer entries than the primitive's index
  const double * none[1] = {nullptr};
  EXPECT_EQ(gshcpu_filter_apply(&sf.f, &map, &space, canvas, l2.data(), scratch.data(), scratch.size(), nullptr, none, 1, &dirty), GSHCPU_ERR_INVALID);
}

TEST(RenderFilter, TheScratchIsAFunctionOfTheGraphAndTheCanvasOnly) {
  // a chain holds one result and its successor; a wide graph holds more
  const FCase & chain = kCases[0];
  Built b;
  build(chain, &b);
  const size_t one = gshcpu_filter_scratch_doubles(&b.f, 8, 6);
  EXPECT_EQ(one, (size_t)8 * 6 * 4 * (1 + 2) + 8 * 8) << "one slot, two temporaries, eight lines";
  EXPECT_EQ(gshcpu_filter_scratch_doubles(&b.f, 16, 6), (size_t)16 * 6 * 4 * (1 + 2) + 8 * 16);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
