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
    ASSERT_EQ(gshcpu_filter_apply(&b.f, &map, &space, canvas, layer.data(), scratch.data(), scratch.size(), &dirty), GSHCPU_OK);
    for (size_t q = 0; q < n * 4u; q++) {
      ASSERT_NEAR(layer[q], c.want[q], 1e-12) << "pixel " << q / 4u << " channel " << q % 4u;
    }
    cases++;
  }
  EXPECT_GE(cases, 30u);
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
  EXPECT_EQ(gshcpu_filter_apply(&b.f, &map, &space, canvas, layer.data(), scratch.data(), need - 1u, &dirty), GSHCPU_ERR_LIMIT);
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
  EXPECT_EQ(gshcpu_filter_apply(&b.f, &turned, &space, canvas, layer.data(), scratch.data(), scratch.size(), &dirty),
      GSHCPU_ERR_UNSUPPORTED);
  // a primitive of a later stage
  b.prims[0].kind = GSHSCENE_FILTER_MORPHOLOGY;
  const GSHGEOM_Map map = map_of(c.map);
  EXPECT_EQ(gshcpu_filter_apply(&b.f, &map, &space, canvas, layer.data(), scratch.data(), scratch.size(), &dirty),
      GSHCPU_ERR_UNSUPPORTED);
  b.prims[0].kind = GSHSCENE_FILTER_OFFSET;
  EXPECT_EQ(gshcpu_filter_apply(nullptr, &map, &space, canvas, layer.data(), scratch.data(), scratch.size(), &dirty),
      GSHCPU_ERR_INVALID);
  EXPECT_EQ(std::memcmp(layer.data(), c.src, n * 4u * sizeof(double)), 0);
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
