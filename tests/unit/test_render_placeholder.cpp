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
 * A placeholder (shape-scene's list.h, PH1; the rules PH3 to PH6): nothing draws it and the planning pass reports it, so
 * this sink changes no pixel for it, counts it as a unit, needs no memory for it, and says the entry the contract says.
 * The conformance suite (test_render_conformance.cpp) holds the rules for every sink; this holds what only this sink
 * can show: the pixels, and the exact workspace.
 */

#include <gtest/gtest.h>

#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <vector>

using namespace rh;

namespace {

/** Two fills with a placeholder (refused, a box), a placeholder (approximated) and a clip block holding one at its end. */
GSHSCENE_List * with_placeholders(Store & s, bool placeholders) {
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  const double box[4] = {1.0, 1.0, 5.0, 5.0};
  GSHSCENE_List * l = s.open();
  EXPECT_EQ(gshscene_list_fill(l, s.rect(1, 1, 9, 7), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  if (placeholders) {
    EXPECT_EQ(gshscene_list_placeholder(l, GSHSCENE_PLACEHOLDER_SOURCE_COLR, GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED, 0, box,
                  nullptr, 0, 0.0),
        GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_clip_push(l, s.rect(3, 2, 14, 10), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 12), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  if (placeholders) {
    EXPECT_EQ(gshscene_list_placeholder(l, GSHSCENE_PLACEHOLDER_SOURCE_COLR, GSHSCENE_PLACEHOLDER_COLR_WALK_LIMIT, 1,
                  nullptr, nullptr, 0, 0.0),
        GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

}  // namespace

TEST(RenderPlaceholder, ItChangesNoPixelAndTheReportSaysWhyEveryPlaceholderWasNotDrawn) {
  Store s;
  GSHSCENE_List * bare = with_placeholders(s, false);
  GSHSCENE_List * marked = with_placeholders(s, true);
  SinkRef sink;
  RasterRef a(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
  RasterRef b(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
  ReportRef ra;
  ReportRef rb;
  GSHSCENE_Target ta = target_of(a.raster);
  GSHSCENE_Target tb = target_of(b.raster);
  GSHSCENE_Render_Request qa = request_of(bare, &ta, ra.report);
  GSHSCENE_Render_Request qb = request_of(marked, &tb, rb.report);
  ASSERT_EQ(gshscene_sink_render(sink.sink, &qa), GSHSCENE_OK);
  ASSERT_EQ(gshscene_sink_render(sink.sink, &qb), GSHSCENE_OK);
  EXPECT_EQ(a.bytes(), b.bytes()) << "PH6: nothing is drawn for a placeholder";
  EXPECT_EQ(ra.entries().size(), 0u);
  auto e = rb.entries();
  ASSERT_EQ(e.size(), 2u);
  EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(e[0].reason, GSHSCENE_REASON_PLACEHOLDER);
  ASSERT_EQ(e[0].chain_count, 1u);
  EXPECT_EQ(e[0].chain[0], 1u);
  EXPECT_EQ(e[0].second, (size_t)((GSHSCENE_PLACEHOLDER_SOURCE_COLR << 16) | GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED));
  EXPECT_EQ(e[1].entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(e[1].reason, GSHSCENE_REASON_PLACEHOLDER);
  ASSERT_EQ(e[1].chain_count, 1u);
  EXPECT_EQ(e[1].chain[0], 4u);
  EXPECT_EQ(e[1].second, (size_t)((GSHSCENE_PLACEHOLDER_SOURCE_COLR << 16) | GSHSCENE_PLACEHOLDER_COLR_WALK_LIMIT));
  GSHSCENE_Report_Summary sum = rb.summary();
  EXPECT_EQ(sum.units_total, gshscene_list_expansion(marked));
  EXPECT_EQ(sum.units_done, sum.units_total);
  EXPECT_EQ(sum.static_bytes, ra.summary().static_bytes) << "a placeholder holds nothing across frames";
  EXPECT_EQ(sum.work_max, ra.summary().work_max) << "a placeholder is no work between two cancel checks";
}

TEST(RenderPlaceholder, ItNeedsNoWorkspaceOfItsOwn) {
  Store s;
  GSHSCENE_List * bare = with_placeholders(s, false);
  GSHSCENE_List * marked = with_placeholders(s, true);
  SinkRef sink;
  size_t need[2] = {0, 0};
  GSHSCENE_List * lists[2] = {bare, marked};
  for (int i = 0; i < 2; i++) {
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(lists[i], &t, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need[i]), GSHSCENE_OK);
  }
  // The walk's scratch is a function of the nesting of the list, and a placeholder adds none: the same need.
  EXPECT_EQ(need[0], need[1]);
  // The exact workspace renders it; one byte less is the contract's ERR_LIMIT with the report entries untouched by it.
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(marked, &t, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  std::vector<unsigned char> workspace(need[1] + 16);
  req.workspace = workspace.data();
  req.workspace_bytes = need[1];
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(report.entries().size(), 2u);
}

TEST(RenderPlaceholder, StrictStopsBeforeAnyPixelIsDrawnAndAnApproximatedOneAloneNeverFails) {
  Store s;
  GSHSCENE_List * marked = with_placeholders(s, true);
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
  const std::string before = r.bytes();
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(marked, &t, report.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(r.bytes(), before) << "the target is untouched";
  // An approximated placeholder alone.
  GSHSCENE_List * only = s.open();
  EXPECT_EQ(gshscene_list_fill(only, s.rect(1, 1, 5, 5), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(only, GSHSCENE_PLACEHOLDER_SOURCE_COLR, GSHSCENE_PLACEHOLDER_COLR_NO_RECORD, 1, nullptr,
                nullptr, 0, 0.0),
      GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(only), GSHSCENE_OK);
  ReportRef report2;
  req = request_of(only, &t, report2.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(report2.summary().by_class[GSHSCENE_ENTRY_APPROXIMATED], 1u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
