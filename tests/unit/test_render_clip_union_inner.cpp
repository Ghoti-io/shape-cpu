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
 * A union whose members carry inner clips (shape-scene's clip_union.h, CI1 to CI6) drawn by the general tier: the oracle's vectors byte for
 * byte on every route, the caller's memory exactly measured, the refusals of a sink without the tier, and a clipped member against the
 * same region made by other means.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace cuh;

namespace {

/** A path resource of @p count contours of kContours from @p first. */
const GSHSCENE_Resource * contours_path(Scene & s, size_t first, size_t count) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  for (size_t c = 0; c < count; c++) {
    const cv::Contour & ct = cv::kContours[first + c];
    for (size_t k = 0; k < ct.count; k++) {
      GSHGEOM_Point q{cv::kVerts[ct.first + k][0] / 64.0, cv::kVerts[ct.first + k][1] / 64.0, 0};
      if (k == 0) {
        gshgeom_path_move_to(p, q);
      } else {
        gshgeom_path_line_to(p, q);
      }
    }
    gshgeom_path_close(p);
  }
  return s.path(p);
}

/** The members of clipped vector case @p c, in @p s, with their inner clips (kept alive in @p keep: the intern copies them). */
std::vector<GSHSCENE_Clip_Member> clipped_members(Scene & s, const cv::ClippedCase & c, std::vector<std::vector<GSHSCENE_Clip_Inner>> & keep) {
  std::vector<GSHSCENE_Clip_Member> members;
  keep.assign(c.member_count, {});
  for (size_t i = 0; i < c.member_count; i++) {
    const cv::ClippedMember & m = cv::kClippedMembers[c.member_first + i];
    for (size_t k = 0; k < m.inner_count; k++) {
      const cv::Inner & in = cv::kInners[m.inner_first + k];
      GSHSCENE_Clip_Inner inner;
      std::memset(&inner, 0, sizeof inner);
      inner.resource = contours_path(s, in.contour_first, in.contour_count);
      inner.rule = in.even_odd ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
      keep[i].push_back(inner);
    }
    GSHSCENE_Clip_Member member;
    std::memset(&member, 0, sizeof member);
    member.resource = contours_path(s, m.contour_first, m.contour_count);
    member.rule = m.even_odd ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
    member.inner_count = keep[i].size();
    member.inner = keep[i].empty() ? nullptr : keep[i].data();
    members.push_back(member);
  }
  return members;
}

Options general_only() {
  Options o;
  o.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION;
  return o;
}

Options one_group() {
  Options o;
  o.tiers = (GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION) | GSHCPU_RENDER_FORCE_ONE_GROUP;
  return o;
}

GSHSCENE_Clip_Inner inner_of(const GSHSCENE_Resource * r, GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO) {
  GSHSCENE_Clip_Inner in;
  std::memset(&in, 0, sizeof in);
  in.resource = r;
  in.rule = rule;
  return in;
}

}  // namespace

TEST(RenderClipUnionInner, EveryVectorIsTheOraclesByteForByteOnTheDefaultTiersAndWithTheGeneralTierForcedAndWithOneGroup) {
  size_t checked = 0;
  size_t exact_cells = 0;
  for (const cv::ClippedCase & c : cv::kClippedCases) {
    Scene s;
    std::vector<std::vector<GSHSCENE_Clip_Inner>> keep;
    const GSHSCENE_Resource * u = make_union(s, clipped_members(s, c, keep));
    GSHSCENE_List * l = clipped_fill(s, u, (uint32_t)c.width, (uint32_t)c.height);
    const Draw by_default = draw(l, (uint32_t)c.width, (uint32_t)c.height);
    const Draw forced = draw(l, (uint32_t)c.width, (uint32_t)c.height, general_only());
    const Draw one = draw(l, (uint32_t)c.width, (uint32_t)c.height, one_group());
    ASSERT_EQ(by_default.result, GSHSCENE_OK) << c.name;
    ASSERT_EQ(forced.result, GSHSCENE_OK) << c.name;
    ASSERT_EQ(one.result, GSHSCENE_OK) << c.name;
    EXPECT_EQ(by_default.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    EXPECT_EQ(forced.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    EXPECT_EQ(one.count(GSHSCENE_ENTRY_REFUSED), 0u) << c.name;
    EXPECT_EQ(by_default.counters.union_fast, 0u) << c.name << ": a clipped member is never the fast tier's (CI4)";
    EXPECT_EQ(by_default.counters.union_general, 1u) << c.name;
    exact_cells += by_default.counters.union_exact;
    for (int y = 0; y < c.height; y++) {
      for (int x = 0; x < c.width; x++) {
        const uint8_t want = cv::kClippedExpect[c.expect_first + (size_t)(y * c.width + x)];
        EXPECT_EQ(by_default.at8((uint32_t)x, (uint32_t)y, 3), want) << c.name << " default at (" << x << ", " << y << ")";
        EXPECT_EQ(forced.at8((uint32_t)x, (uint32_t)y, 3), want) << c.name << " forced at (" << x << ", " << y << ")";
        EXPECT_EQ(one.at8((uint32_t)x, (uint32_t)y, 3), want) << c.name << " one group at (" << x << ", " << y << ")";
      }
    }
    checked++;
  }
  EXPECT_EQ(checked, sizeof cv::kClippedCases / sizeof cv::kClippedCases[0]);
  EXPECT_GE(checked, 60u);
  EXPECT_GT(exact_cells, 100u) << "the exact step with members is on the answer's path";
}

TEST(RenderClipUnionInner, TheSinkClaimsTheCapabilityWithTheGeneralTierAndNotWithout) {
  SinkRef with;
  GSHSCENE_Sink_Caps caps;
  std::memset(&caps, 0, sizeof caps);
  GSHSCENE_Target t;
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 2, 2));
  t = target_of(r.raster);
  ASSERT_EQ(gshscene_sink_caps(with.sink, &t, &caps), GSHSCENE_OK);
  EXPECT_NE(caps.flags & GSHSCENE_CAP_CLIP_UNION_INNER, 0u);
  EXPECT_NE(caps.flags & GSHSCENE_CAP_CLIP_UNION_GENERAL, 0u);
}

TEST(RenderClipUnionInner, WithoutTheGeneralTierAUnionWithAClippedMemberIsRefusedAndNamedAndNothingIsDrawn) {
  Scene s;
  std::vector<GSHSCENE_Clip_Inner> inner = {inner_of(s.rect(1, 1, 3, 3))};
  GSHSCENE_Clip_Member m = member(s.rect(0, 0, 2.5, 2.5));
  m.inner_count = inner.size();
  m.inner = inner.data();
  const GSHSCENE_Resource * u = make_union(s, {m, member(s.rect(3.2, 0, 3.8, 1))});
  Options o;
  o.tiers = GSHCPU_RENDER_TIERS_ALL & ~(GSHCPU_RENDER_TIER_UNION | GSHCPU_RENDER_TIER_UNION_GENERAL);
  Draw d = draw(clipped_fill(s, u, 4, 3), 4, 3, o);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_CLIP_UNION), 1u) << "no union tier at all: refused as a union, before the inner clips are asked about";
  Draw g = draw(clipped_fill(s, u, 4, 3), 4, 3);
  ASSERT_EQ(g.result, GSHSCENE_OK);
  EXPECT_EQ(g.count(GSHSCENE_ENTRY_REFUSED), 0u);
}

TEST(RenderClipUnionInner, AClippedMemberIsTheIntersectionOfItsRegionsAndAUnionOfClippedMembersTheUnion) {
  Scene s;
  // A (0,0)-(2.5,2.5), B (1,1)-(3.5,3.5): the intersection is (1,1)-(2.5,2.5), a quarter-pixel aligned rectangle: whole pixels and halves.
  std::vector<GSHSCENE_Clip_Inner> inner = {inner_of(s.rect(1, 1, 3.5, 3.5))};
  GSHSCENE_Clip_Member m = member(s.rect(0, 0, 2.5, 2.5));
  m.inner_count = inner.size();
  m.inner = inner.data();
  Draw d = draw(clipped_fill(s, make_union(s, {m}), 4, 4), 4, 4);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
  const uint8_t want[4][4] = {{0, 0, 0, 0}, {0, 255, 128, 0}, {0, 128, 64, 0}, {0, 0, 0, 0}};
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      // 8192 units is 255 levels, half a pixel 4096 units: floor(4096 / 32) = 128; a quarter 2048: 64
      EXPECT_EQ(d.at8((uint32_t)x, (uint32_t)y, 3), want[y][x]) << x << "," << y;
    }
  }
  // the same region with the roles swapped (the member the inner clip and the other way): equal bytes
  std::vector<GSHSCENE_Clip_Inner> inner2 = {inner_of(s.rect(0, 0, 2.5, 2.5))};
  GSHSCENE_Clip_Member m2 = member(s.rect(1, 1, 3.5, 3.5));
  m2.inner_count = inner2.size();
  m2.inner = inner2.data();
  Draw swapped = draw(clipped_fill(s, make_union(s, {m2}), 4, 4), 4, 4);
  EXPECT_EQ(swapped.bytes, d.bytes);
}

TEST(RenderClipUnionInner, AClippedMemberWithTheWholeInnerClipEqualsThePlainMemberAndAnEmptyOneDrawsNothing) {
  Scene s;
  const GSHSCENE_Resource * shape = lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}});
  std::vector<GSHSCENE_Clip_Inner> whole = {inner_of(s.rect(-1, -1, 9, 9))};
  GSHSCENE_Clip_Member m = member(shape);
  m.inner_count = 1;
  m.inner = whole.data();
  Draw plain = draw(clipped_fill(s, make_union(s, {member(shape), member(s.rect(3.1, 0.1, 3.9, 0.9))}), 4, 3), 4, 3);
  Draw clipped = draw(clipped_fill(s, make_union(s, {m, member(s.rect(3.1, 0.1, 3.9, 0.9))}), 4, 3), 4, 3);
  ASSERT_EQ(plain.result, GSHSCENE_OK);
  ASSERT_EQ(clipped.result, GSHSCENE_OK);
  EXPECT_EQ(clipped.bytes, plain.bytes);
  std::vector<GSHSCENE_Clip_Inner> far = {inner_of(s.rect(50, 50, 60, 60))};
  GSHSCENE_Clip_Member gone = member(shape);
  gone.inner_count = 1;
  gone.inner = far.data();
  Draw empty = draw(clipped_fill(s, make_union(s, {gone}), 4, 3), 4, 3);
  ASSERT_EQ(empty.result, GSHSCENE_OK);
  for (uint32_t y = 0; y < 3; y++) {
    for (uint32_t x = 0; x < 4; x++) {
      EXPECT_EQ(empty.at8(x, y, 3), 0u);
    }
  }
}

TEST(RenderClipUnionInner, TheWorkspaceMeasuredForAClippedUnionIsEnoughAndOneByteLessIsNot) {
  Scene s;
  std::vector<GSHSCENE_Clip_Inner> inner = {inner_of(lattice_polygon(s, {{20, 100}, {190, 20}, {120, 125}})), inner_of(s.rect(0.2, 0.2, 2, 1.8))};
  GSHSCENE_Clip_Member m = member(lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}}));
  m.inner_count = inner.size();
  m.inner = inner.data();
  const GSHSCENE_Resource * u = make_union(s, {m, member(s.rect(1, 1, 3, 2.5))});
  GSHSCENE_List * l = clipped_fill(s, u, 4, 3);
  Options cm;
  cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw a = draw(l, 4, 3, cm);
  Draw b = draw(l, 4, 3);
  ASSERT_EQ(a.result, GSHSCENE_OK) << "the measure holds every part's geometry and the planes of the parts";
  EXPECT_EQ(a.bytes, b.bytes);
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 3));
  SinkRef sink;
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> mem(need);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  req.workspace = mem.data();
  req.workspace_bytes = need - 1u;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT);
  const std::string untouched = r.bytes();
  EXPECT_TRUE(std::all_of(untouched.begin(), untouched.end(), [](char c) { return c == 0; }));
}

TEST(RenderClipUnionInner, ALimitOnTheKeptPiecesRefusesTheUnionAndNothingIsDrawnWithAMemberLeftOut) {
  Scene s;
  std::vector<GSHSCENE_Clip_Inner> inner = {inner_of(lattice_polygon(s, {{20, 100}, {190, 20}, {120, 125}}))};
  GSHSCENE_Clip_Member m = member(lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}}));
  m.inner_count = 1;
  m.inner = inner.data();
  GSHSCENE_List * l = clipped_fill(s, make_union(s, {m, member(s.rect(0.2, 0.2, 2, 1.8))}), 4, 3);
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_union_pieces = 1;
  Options o;
  o.limits = &lim;
  Draw d = draw(l, 4, 3, o);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 1u) << "the pieces the sweeps keep are the sink's resource limit";
}

TEST(RenderClipUnionInner, TheMeasureHoldsTheGeometryOfTheLargestPartAndAnInnerClipMayBeIt) {
  Scene s;
  // an inner clip with far more points than anything else in the union: the memory of the render is what its geometry takes
  std::vector<std::pair<int, int>> star;
  for (int i = 0; i < 90; i++) {
    star.push_back({128 + (int)(100 * std::cos(i * 0.9)), 128 + (int)(100 * std::sin(i * 0.9))});
  }
  std::vector<GSHSCENE_Clip_Inner> inner = {inner_of(lattice_polygon(s, star), GSHGEOM_FILL_EVEN_ODD)};
  GSHSCENE_Clip_Member m = member(s.rect(0.1, 0.1, 3.9, 3.9));
  m.inner_count = 1;
  m.inner = inner.data();
  GSHSCENE_List * l = clipped_fill(s, make_union(s, {m, member(s.rect(3, 0, 3.9, 1))}), 4, 4);
  Draw reference = draw(l, 4, 4);
  ASSERT_EQ(reference.result, GSHSCENE_OK);
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  SinkRef sink(nullptr, &alloc);
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 4));
  ReportRef report;
  GSHSCENE_Target target = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &target, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> workspace(need);
  req.workspace = workspace.data();
  req.workspace_bytes = need;
  const long before = t.calls;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << "the memory measured holds the inner clip geometry";
  EXPECT_EQ(t.calls, before);
  EXPECT_EQ(r.bytes(), reference.bytes);
}

TEST(RenderClipUnionInner, ACallerMemoryRenderOfAClippedUnionMakesNoAllocationAndTheSameBytes) {
  Scene s;
  std::vector<GSHSCENE_Clip_Inner> inner = {inner_of(lattice_polygon(s, {{20, 100}, {190, 20}, {120, 125}})), inner_of(s.rect(0.2, 0.2, 2, 1.8))};
  GSHSCENE_Clip_Member m = member(lattice_polygon(s, {{10, 10}, {200, 50}, {40, 120}}));
  m.inner_count = inner.size();
  m.inner = inner.data();
  GSHSCENE_List * l = clipped_fill(s, make_union(s, {m, member(s.rect(1, 1, 3, 2.5))}), 4, 3);
  Draw reference = draw(l, 4, 3);
  ASSERT_EQ(reference.result, GSHSCENE_OK);
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  SinkRef sink(nullptr, &alloc);
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 3));
  ReportRef report;
  GSHSCENE_Target target = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &target, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> workspace(need);
  req.workspace = workspace.data();
  req.workspace_bytes = need;
  const long before = t.calls;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(t.calls, before) << "S9: the exact workspace is all the render takes";
  EXPECT_EQ(r.bytes(), reference.bytes);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
