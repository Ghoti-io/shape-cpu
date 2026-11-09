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
 * The memory a render of a list whose pattern holds a pattern, and so on needs (render.h, "Memory"): what the sink asks for one render of a target of 16
 * pixels grows with the depth of the nesting as a polynomial.  A pass that runs while the pass above it holds its copy stands on the
 * memory of every pass above, once; counting the base twice at every level made the need double with each level (a nest of 17 patterns asked
 * for a gigabyte, and one of 24 for 127), though nothing but the depth had grown.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <vector>

using namespace cuh;

namespace {

/** A list that fills a 4 x 4 square with a pattern of @p depth levels: each tile is a picture that fills its cell with the pattern below it. */
GSHSCENE_List * nest_of(Scene & s, int depth, int clips) {
  const GSHSCENE_Resource * inner = nullptr;
  for (int d = 0; d < depth; d++) {
    GSHSCENE_List * t = s.open();
    for (int k = 0; k < clips; k++) {
      EXPECT_EQ(gshscene_list_clip_push(t, s.rect(0.5, 0.5, 7.5, 7.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    }
    GSHSCENE_Paint paint = rh::solid(0.8, 0.2, 0.3, 0.9);
    if (inner) {
      GSHSCENE_Pattern pt;
      gshscene_pattern_init(&pt);
      pt.tile = inner;
      pt.w = pt.h = pt.x_step = pt.y_step = 8;
      const GSHSCENE_Resource * r = nullptr;
      EXPECT_EQ(gshscene_store_intern_pattern(s.store, &pt, &r), GSHSCENE_OK);
      s.held.push_back(r);
      paint = GSHSCENE_Paint{};
      paint.kind = GSHSCENE_PAINT_PATTERN;
      paint.resource = r;
      paint.rgba[3] = 1.0;
      EXPECT_EQ(gshscene_list_fill(t, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    } else {
      EXPECT_EQ(gshscene_list_fill(t, s.rect(1, 1, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    }
    for (int k = 0; k < clips; k++) {
      EXPECT_EQ(gshscene_list_clip_pop(t), GSHSCENE_OK);
    }
    inner = s.picture(t);
  }
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = inner;
  pt.w = pt.h = pt.x_step = pt.y_step = 8;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_pattern(s.store, &pt, &r), GSHSCENE_OK);
  s.held.push_back(r);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint use{};
  use.kind = GSHSCENE_PAINT_PATTERN;
  use.resource = r;
  use.rgba[3] = 1.0;
  EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &use, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

size_t need_of(GSHSCENE_List * l, bool caller_memory) {
  SinkRef sink(nullptr, nullptr);
  ReportRef report(nullptr);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 4));
  GSHSCENE_Target target = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &target, report.report);
  req.flags = caller_memory ? GSHSCENE_RENDER_CALLER_MEMORY : 0;
  size_t need = 0;
  EXPECT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  return need;
}

} // namespace

TEST(RenderPatternNesting, TheWorkspaceDoesNotDoubleWithEachLevelOfTheNest) {
  // The fixed block holds a term in the depth squared (a pass level for each level, each with the chain's maps) and, with clips, one in
  // their product with the depth (the fourth power at most), so the need is a polynomial in the depth and no straight line; what it must never be is a doubling: a
  // level that adds more than a few percent of everything before it is the base counted twice.
  for (int clips : {0, 3}) {
    std::vector<size_t> need;
    for (int depth = 1; depth <= 24; depth++) {
      Scene s;
      need.push_back(need_of(nest_of(s, depth, clips), false));
    }
    for (size_t i = 15; i + 1 < need.size(); i++) {
      EXPECT_LT(need[i + 1] * 100u, need[i] * 130u) << "depth " << i + 2 << " against " << i + 1 << ", clips " << clips;
    }
    EXPECT_LT(need[23], clips ? 128u * 1024u * 1024u : 1024u * 1024u) << "a 4 x 4 target of 24 nested patterns, clips " << clips;
  }
}

TEST(RenderPatternNesting, TheNeedIsExactInTheCallersMemoryAndTheSameAsTheSinksOwn) {
  Scene s;
  GSHSCENE_List * l = nest_of(s, 6, 2);
  EXPECT_EQ(need_of(l, true), need_of(l, false)) << "the caller's workspace and the sink's allocator measure the same";
  Draw plain = draw(l, 4, 4);
  ASSERT_EQ(plain.result, GSHSCENE_OK);

  SinkRef sink(nullptr, nullptr);
  ReportRef report(nullptr);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 4));
  GSHSCENE_Target target = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &target, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> workspace(need + 64, 0xc5);
  req.workspace = workspace.data();
  req.workspace_bytes = need - 1;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << "a byte short";
  req.workspace_bytes = need;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << "the need itself";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
