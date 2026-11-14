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
 * A group's filter through the sink (shape-scene filter.h, render.h FC1 to FC3): the source moved by an offset, the order of the filter
 * and the group's opacity (FL20), a damaged render equal to the whole one inside the damage (FC2), what the planning pass refuses (a
 * primitive this build lacks, a rotated map), and the memory of the caller's workspace.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

const GSHSCENE_Filter_Input kPrev = {GSHSCENE_FILTER_INPUT_PREVIOUS, 0};

GSHSCENE_Filter_Primitive prim(uint32_t kind) {
  GSHSCENE_Filter_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = kind;
  if (kind != GSHSCENE_FILTER_FLOOD) {
    p.input_count = 1;
    p.inputs = &kPrev;
  }
  return p;
}

const GSHSCENE_Resource * intern_filter(rh::Store & s, std::vector<GSHSCENE_Filter_Primitive> prims, bool region_auto = true,
    const double region[4] = nullptr) {
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = region_auto ? GSHSCENE_FILTER_REGION_AUTO : GSHSCENE_FILTER_REGION_EXPLICIT;
  if (!region_auto) {
    for (int i = 0; i < 4; i++) {
      f.region[i] = region[i];
    }
  }
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = prims.size();
  f.primitives = prims.data();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_filter(s.store, &f, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

/** A premultiplied RGBA8 raster, filled with @p fill. */
GIMG_Raster * make_raster(uint32_t w, uint32_t h, unsigned char fill = 0) {
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  format.alpha_form = GIMG_ALPHA_PREMULTIPLIED;
  GIMG_Raster * r = nullptr;
  if (gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, nullptr, 0, &r) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * px = (unsigned char *)gimg_raster_pixels(r);
  for (uint32_t y = 0; y < h; y++) {
    std::memset(px + y * gimg_raster_stride_bytes(r), fill, (size_t)w * 4u);
  }
  return r;
}

std::string pixel(const GIMG_Raster * r, uint32_t x, uint32_t y) {
  const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(r);
  return std::string((const char *)px + y * gimg_raster_stride_bytes(r) + x * 4u, 4u);
}

std::string bytes_of(const GIMG_Raster * r) {
  std::string out;
  for (uint32_t y = 0; y < gimg_raster_height(r); y++) {
    for (uint32_t x = 0; x < gimg_raster_width(r); x++) {
      out += pixel(r, x, y);
    }
  }
  return out;
}

std::string px(unsigned r, unsigned g, unsigned b, unsigned a) {
  std::string s(4, 0);
  s[0] = (char)r;
  s[1] = (char)g;
  s[2] = (char)b;
  s[3] = (char)a;
  return s;
}

struct Rendered {
  GSHSCENE_Result result;
  std::vector<GSHSCENE_Report_Entry> entries;
};

Rendered render(GSHSCENE_List * list, GIMG_Raster * raster, const GSHGEOM_Map * map = nullptr, const GSHSCENE_Rect * damage = nullptr) {
  rh::SinkRef sink;
  rh::ReportRef report;
  GSHSCENE_Target target = rh::target_of(raster, map);
  target.damage = damage;
  GSHSCENE_Render_Request req = rh::request_of(list, &target, report.report);
  Rendered out;
  out.result = gshscene_sink_render(sink.sink, &req);
  out.entries = report.entries();
  return out;
}

/** A list of one group (with @p filter and @p opacity) around a red fill of the rectangle. */
GSHSCENE_List * group_of_rect(rh::Store & s, const GSHSCENE_Resource * filter, double opacity, double x0, double y0, double x1,
    double y1) {
  GSHSCENE_List * l = s.open();
  GSHSCENE_Group g;
  std::memset(&g, 0, sizeof g);
  g.opacity = opacity;
  g.filter = filter;
  EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint red = rh::solid(1, 0, 0, 1);
  EXPECT_EQ(gshscene_list_fill(l, s.rect(x0, y0, x1, y1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

}  // namespace

TEST(RenderFilterSink, AnOffsetMovesTheSourceByWholePixels) {
  rh::Store s;
  GSHSCENE_Filter_Primitive o = prim(GSHSCENE_FILTER_OFFSET);
  o.dx = 2;
  o.dy = 1;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {o}), 1.0, 1, 1, 3, 3);
  rh::RasterRef r(make_raster(8, 6));
  ASSERT_NE(r.raster, nullptr);
  Rendered got = render(l, r.raster);
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty());
  EXPECT_EQ(pixel(r.raster, 3, 2), px(255, 0, 0, 255));
  EXPECT_EQ(pixel(r.raster, 4, 3), px(255, 0, 0, 255));
  EXPECT_EQ(pixel(r.raster, 1, 1), px(0, 0, 0, 0)) << "the source is not left where it was";
  EXPECT_EQ(pixel(r.raster, 2, 1), px(0, 0, 0, 0));
  EXPECT_EQ(pixel(r.raster, 5, 3), px(0, 0, 0, 0));
}

TEST(RenderFilterSink, TheGroupsOpacityAppliesToTheFilteredResult) {
  // FL20: a flood of the region, then the group's opacity: 0.4 of white over nothing
  rh::Store s;
  GSHSCENE_Filter_Primitive f = prim(GSHSCENE_FILTER_FLOOD);
  f.color.rgba[0] = f.color.rgba[1] = f.color.rgba[2] = f.color.rgba[3] = 1.0;
  f.opacity = 1.0;
  const double region[4] = {1, 1, 2, 2};
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {f}, false, region), 0.4, 0, 0, 1, 1);
  rh::RasterRef r(make_raster(6, 6));
  ASSERT_EQ(render(l, r.raster).result, GSHSCENE_OK);
  EXPECT_EQ(pixel(r.raster, 1, 1), px(102, 102, 102, 102));
  EXPECT_EQ(pixel(r.raster, 2, 2), px(102, 102, 102, 102));
  EXPECT_EQ(pixel(r.raster, 0, 0), px(0, 0, 0, 0)) << "the source's own pixel is outside the region and the flood fills the region only";
  EXPECT_EQ(pixel(r.raster, 3, 3), px(0, 0, 0, 0));
}

TEST(RenderFilterSink, ADamagedRenderEqualsTheWholeRenderInsideTheDamageAndTouchesNothingOutsideIt) {
  rh::Store s;
  GSHSCENE_Filter_Primitive b = prim(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  b.sx = b.sy = 1.5;
  GSHSCENE_Filter_Primitive o = prim(GSHSCENE_FILTER_OFFSET);
  o.dx = 1;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {b, o}), 1.0, 2, 1, 6, 5);
  rh::RasterRef whole(make_raster(10, 8, 0x5a));
  ASSERT_EQ(render(l, whole.raster).result, GSHSCENE_OK);
  const GSHSCENE_Rect damages[3] = {{3, 2, 7, 6}, {0, 0, 4, 8}, {5, 0, 10, 3}};
  for (const GSHSCENE_Rect & d : damages) {
    rh::RasterRef part(make_raster(10, 8, 0x5a));
    ASSERT_EQ(render(l, part.raster, nullptr, &d).result, GSHSCENE_OK);
    for (int64_t y = 0; y < 8; y++) {
      for (int64_t x = 0; x < 10; x++) {
        const bool inside = x >= d.x0 && x < d.x1 && y >= d.y0 && y < d.y1;
        EXPECT_EQ(pixel(part.raster, (uint32_t)x, (uint32_t)y), inside ? pixel(whole.raster, (uint32_t)x, (uint32_t)y) : px(0x5a, 0x5a, 0x5a, 0x5a))
            << x << "," << y << " damage " << d.x0 << "," << d.y0 << " to " << d.x1 << "," << d.y1;
      }
    }
  }
}

TEST(RenderFilterSink, ABlurSpreadsTheSourceSymmetricallyAndKeepsItsTotal) {
  rh::Store s;
  GSHSCENE_Filter_Primitive b = prim(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  b.sx = b.sy = 1.0;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {b}), 1.0, 4, 4, 5, 5);
  rh::RasterRef r(make_raster(9, 9));
  ASSERT_EQ(render(l, r.raster).result, GSHSCENE_OK);
  const std::string c = pixel(r.raster, 4, 4);
  EXPECT_GT((unsigned char)c[3], 0u);
  EXPECT_EQ(pixel(r.raster, 3, 4), pixel(r.raster, 5, 4));
  EXPECT_EQ(pixel(r.raster, 4, 3), pixel(r.raster, 4, 5));
  EXPECT_EQ(pixel(r.raster, 3, 3), pixel(r.raster, 5, 5));
  EXPECT_GT((unsigned char)pixel(r.raster, 3, 4)[3], 0u) << "the red reaches its neighbors";
  EXPECT_EQ(pixel(r.raster, 0, 0), px(0, 0, 0, 0));
}

TEST(RenderFilterSink, APrimitiveThisBuildLacksIsRefusedByNameAndNothingOfTheGroupIsDrawn) {
  rh::Store s;
  GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_TURBULENCE);
  m.input_count = 0;
  m.inputs = nullptr;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {m}), 1.0, 1, 1, 3, 3);
  rh::RasterRef r(make_raster(6, 6, 0x33));
  Rendered got = render(l, r.raster);
  ASSERT_EQ(got.result, GSHSCENE_OK);
  ASSERT_EQ(got.entries.size(), 1u);
  EXPECT_EQ(got.entries[0].reason, GSHSCENE_REASON_FILTER_PRIMITIVE);
  EXPECT_EQ(got.entries[0].second, (size_t)(1u << GSHSCENE_FILTER_TURBULENCE));
  EXPECT_EQ(bytes_of(r.raster), std::string(6 * 6 * 4, '\x33'));
}

TEST(RenderFilterSink, ARotatedMapIsRefusedAsCanonicalSpace) {
  rh::Store s;
  GSHSCENE_Filter_Primitive b = prim(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  b.sx = b.sy = 1;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {b}), 1.0, 1, 1, 3, 3);
  rh::RasterRef r(make_raster(6, 6, 0x33));
  GSHGEOM_Map turn;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &turn), GSHGEOM_OK);
  Rendered got = render(l, r.raster, &turn);
  ASSERT_EQ(got.result, GSHSCENE_OK);
  ASSERT_EQ(got.entries.size(), 1u);
  EXPECT_EQ(got.entries[0].reason, GSHSCENE_REASON_FILTER_MAP);
  EXPECT_EQ(bytes_of(r.raster), std::string(6 * 6 * 4, '\x33'));
  // a quarter turn is axis aligned and is drawn
  GSHGEOM_Map quarter;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &quarter), GSHGEOM_OK);
  rh::RasterRef q(make_raster(6, 6, 0x33));
  Rendered ok = render(l, q.raster, &quarter);
  ASSERT_EQ(ok.result, GSHSCENE_OK);
  EXPECT_TRUE(ok.entries.empty());
}

TEST(RenderFilterSink, TheCallersMemoryIsExactAndTheRenderFromItIsTheSame) {
  rh::Store s;
  GSHSCENE_Filter_Primitive b = prim(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  b.sx = b.sy = 2;
  GSHSCENE_Filter_Primitive o = prim(GSHSCENE_FILTER_OFFSET);
  o.dx = 1;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {b, o}), 1.0, 2, 2, 5, 5);
  rh::RasterRef plain(make_raster(9, 9));
  ASSERT_EQ(render(l, plain.raster).result, GSHSCENE_OK);
  rh::SinkRef sink;
  rh::ReportRef report;
  rh::RasterRef r(make_raster(9, 9));
  GSHSCENE_Target target = rh::target_of(r.raster);
  GSHSCENE_Render_Request req = rh::request_of(l, &target, report.report);
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  EXPECT_GT(need, (size_t)9 * 9 * 4 * sizeof(double) * 3) << "the layers, the root and the filter's buffers";
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  std::vector<unsigned char> workspace(need + 4096, 0xc5);
  req.workspace = workspace.data();
  req.workspace_bytes = need - 1;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << "one byte short";
  req.workspace_bytes = need;
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(bytes_of(r.raster), bytes_of(plain.raster)) << "from the caller's memory the render is the same";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
