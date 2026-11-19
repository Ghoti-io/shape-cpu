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

#include <array>
#include <cmath>
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

Rendered render(GSHSCENE_List * list, GIMG_Raster * raster, const GSHGEOM_Map * map = nullptr, const GSHSCENE_Rect * damage = nullptr,
    const GSHCPU_Limits * limits = nullptr) {
  rh::SinkRef sink(limits);
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

namespace {

/** The centroid of the alpha of a raster (premultiplied RGBA8). */
void centroid(const GIMG_Raster * r, double * cx, double * cy, double * mass) {
  double sx = 0, sy = 0, m = 0;
  for (uint32_t y = 0; y < gimg_raster_height(r); y++) {
    for (uint32_t x = 0; x < gimg_raster_width(r); x++) {
      const double a = (unsigned char)pixel(r, x, y)[3];
      sx += a * (x + 0.5);
      sy += a * (y + 0.5);
      m += a;
    }
  }
  *cx = m > 0 ? sx / m : 0;
  *cy = m > 0 ? sy / m : 0;
  *mass = m;
}

}  // namespace

TEST(RenderFilterSink, AnImagePrimitiveDrawsTheImageUnderItsPlacement) {
  rh::Store s;
  GIMG_Raster * pic = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 2, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &pic), GIMG_OK);
  unsigned char * bytes = (unsigned char *)gimg_raster_pixels(pic);
  const unsigned char texels[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}};
  for (int t = 0; t < 4; t++) {
    std::memcpy(bytes + (t / 2) * gimg_raster_stride_bytes(pic) + (t % 2) * 4, texels[t], 4);
  }
  GSHSCENE_Image_Params params{};
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * image = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(s.store, pic, &params, &image), GSHSCENE_OK);
  gimg_raster_destroy(pic);
  s.held.push_back(image);
  GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_IMAGE);
  m.input_count = 0;
  m.inputs = nullptr;
  m.image = image;
  gshgeom_map_scale(2, 4.0, 4.0, 1.0, &m.placement); // each texel is 4 user units
  m.placement.t[0] = 2.0;
  m.placement.t[1] = 2.0;
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {m}), 1.0, 0, 0, 1, 1);
  rh::RasterRef r(make_raster(12, 12));
  ASSERT_EQ(render(l, r.raster).result, GSHSCENE_OK);
  EXPECT_EQ(pixel(r.raster, 3, 3), px(255, 0, 0, 255));
  EXPECT_EQ(pixel(r.raster, 7, 3), px(0, 255, 0, 255));
  EXPECT_EQ(pixel(r.raster, 3, 7), px(0, 0, 255, 255));
  EXPECT_EQ(pixel(r.raster, 7, 7), px(255, 255, 255, 255));
  EXPECT_EQ(pixel(r.raster, 1, 1), px(0, 0, 0, 0)) << "outside the image is empty";
}

TEST(RenderFilterSink, AnImagePrimitiveFollowsItsRenderingField) {
  rh::Store s;
  GIMG_Raster * pic = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 2, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &pic), GIMG_OK);
  unsigned char * bytes = (unsigned char *)gimg_raster_pixels(pic);
  const unsigned char texels[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}};
  for (int t = 0; t < 4; t++) {
    std::memcpy(bytes + (t / 2) * gimg_raster_stride_bytes(pic) + (t % 2) * 4, texels[t], 4);
  }
  GSHSCENE_Image_Params params{};
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * image = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(s.store, pic, &params, &image), GSHSCENE_OK);
  gimg_raster_destroy(pic);
  s.held.push_back(image);
  const uint32_t renderings[3] = {GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA};
  std::vector<std::array<unsigned char, 4>> at_edge, at_center;
  for (uint32_t rendering : renderings) {
    GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_IMAGE);
    m.input_count = 0;
    m.inputs = nullptr;
    m.image = image;
    m.rendering = rendering;
    gshgeom_map_scale(2, 4.0, 4.0, 1.0, &m.placement); // each texel is 4 user units
    m.placement.t[0] = 2.25; // the texel boundaries fall inside pixel 6, so a pixel's mean spans four texels
    m.placement.t[1] = 2.25;
    GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {m}), 1.0, 0, 0, 1, 1);
    rh::RasterRef r(make_raster(12, 12));
    ASSERT_EQ(render(l, r.raster).result, GSHSCENE_OK);
    const auto e = pixel(r.raster, 6, 6);  // the pixel the four texels meet in
    const auto c = pixel(r.raster, 3, 3);  // inside the first texel
    at_edge.push_back({(unsigned char)e.at(0), (unsigned char)e.at(1), (unsigned char)e.at(2), (unsigned char)e.at(3)});
    at_center.push_back({(unsigned char)c.at(0), (unsigned char)c.at(1), (unsigned char)c.at(2), (unsigned char)c.at(3)});
  }
  EXPECT_EQ(at_edge[0], (std::array<unsigned char, 4>{255, 255, 255, 255})) << "nearest reads the one texel, the white one";
  EXPECT_NE(at_edge[1], at_edge[0]) << "linear mixes the neighbors in";
  EXPECT_NE(at_edge[2], at_edge[0]) << "the area mean over the pixel spans four texels";
  EXPECT_NE(at_edge[1], at_edge[2]) << "and is not the bilinear value";
  EXPECT_LT(at_edge[2][0], 255);
  EXPECT_LT(at_edge[2][1], 255);
  EXPECT_EQ(at_center[0][0], 255);
}

TEST(RenderFilterSink, AnAreaImagePrimitiveHoldsToTheSinksImageLimits) {
  rh::Store s;
  GIMG_Raster * pic = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 2, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &pic), GIMG_OK);
  std::memset(gimg_raster_pixels(pic), 255, 2 * gimg_raster_stride_bytes(pic));
  GSHSCENE_Image_Params params{};
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * image = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(s.store, pic, &params, &image), GSHSCENE_OK);
  gimg_raster_destroy(pic);
  s.held.push_back(image);
  GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_IMAGE);
  m.input_count = 0;
  m.inputs = nullptr;
  m.image = image;
  m.rendering = GSHSCENE_IMAGE_FILTER_AREA;
  gshgeom_map_scale(2, 4.0, 4.0, 1.0, &m.placement);
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {m}), 1.0, 0, 0, 1, 1);
  rh::RasterRef ok(make_raster(12, 12));
  EXPECT_EQ(render(l, ok.raster).result, GSHSCENE_OK) << "the defaults admit it";
  GSHCPU_Limits tight;
  gshcpu_limits_default(&tight);
  tight.max_image_work = 4; // a 12 by 12 canvas of AREA pixels visits far more than four
  rh::RasterRef refused(make_raster(12, 12));
  EXPECT_NE(render(l, refused.raster, nullptr, nullptr, &tight).result, GSHSCENE_OK) << "the sink's own limit binds the filter";
}

// The placement of a filter's image primitive is composed with the map of the group, translation included: an element drawn under a
// translating map is the element drawn under the identity, moved.
TEST(RenderFilterSink, AnImagePrimitiveMovesWithATranslatingMapOfTheGroup) {
  rh::Store s;
  GIMG_Raster * pic = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 1, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &pic), GIMG_OK);
  unsigned char * texels = (unsigned char *)gimg_raster_pixels(pic);
  const unsigned char colors[8] = {255, 0, 0, 255, 0, 0, 255, 255};
  std::memcpy(texels, colors, sizeof colors);
  GSHSCENE_Image_Params params{};
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * image = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(s.store, pic, &params, &image), GSHSCENE_OK);
  gimg_raster_destroy(pic);
  s.held.push_back(image);
  GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_IMAGE);
  m.input_count = 0;
  m.inputs = nullptr;
  m.image = image;
  m.rendering = GSHSCENE_IMAGE_FILTER_NEAREST;
  gshgeom_map_scale(2, 4.0, 4.0, 1.0, &m.placement);
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {m}), 1.0, 0, 0, 12, 8);
  rh::RasterRef plain(make_raster(24, 16));
  ASSERT_EQ(render(l, plain.raster).result, GSHSCENE_OK);
  // The picture is drawn: the first texel's four by four pixels are red and the second's blue.
  EXPECT_EQ(pixel(plain.raster, 1, 1), px(255, 0, 0, 255));
  EXPECT_EQ(pixel(plain.raster, 6, 1), px(0, 0, 255, 255));
  GSHGEOM_Map move;
  gshgeom_map_identity(2, &move);
  move.t[0] = 5;
  move.t[1] = 3;
  rh::RasterRef moved(make_raster(24, 16));
  ASSERT_EQ(render(l, moved.raster, &move).result, GSHSCENE_OK);
  for (uint32_t y = 3; y < 16; y++) {
    for (uint32_t x = 5; x < 24; x++) {
      ASSERT_EQ(pixel(moved.raster, x, y), pixel(plain.raster, x - 5, y - 3)) << "at " << x << ", " << y;
    }
  }
}

// IC9 for the filter's image primitive (FL25's neighbour): the work of AREA over the canvas is held to `max_area_pixel_work` for one pixel and
// to `max_image_work` for all of them, each as a bound that is met exactly: the work that equals the limit is drawn, one past it is refused.
TEST(RenderFilterSink, TheAreaWorkOfAnImagePrimitiveIsHeldToBothLimitsExactly) {
  rh::Store s;
  GIMG_Raster * pic = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 2, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &pic), GIMG_OK);
  std::memset(gimg_raster_pixels(pic), 255, 2 * gimg_raster_stride_bytes(pic));
  GSHSCENE_Image_Params params{};
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * image = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(s.store, pic, &params, &image), GSHSCENE_OK);
  gimg_raster_destroy(pic);
  s.held.push_back(image);
  GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_IMAGE);
  m.input_count = 0;
  m.inputs = nullptr;
  m.image = image;
  m.rendering = GSHSCENE_IMAGE_FILTER_AREA;
  gshgeom_map_scale(2, 4.0, 4.0, 1.0, &m.placement);
  GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {m}), 1.0, 0, 0, 1, 1);
  // The bound of one pixel, from the same primitives the engine uses: the placement is the whole map here (the target's is the identity).
  GSHCPU_Image im;
  const uint32_t window[4] = {0, 0, 0, 0};
  GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space layer = {};
  layer.primaries = GCOL_PRIMARIES_SRGB;
  layer.transfer = GCOL_TRANSFER_SRGB;
  ASSERT_EQ(gshcpu_render_image_open(&im, image, window, 0, 0, GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer), GSHCPU_OK);
  ASSERT_EQ(gshcpu_render_image_place(&im, &m.placement), 1);
  uint64_t pixel = 0;
  ASSERT_EQ(gshcpu_render_image_area_bound(&im, &pixel), 1);
  ASSERT_GT(pixel, 0u);
  const uint64_t canvas = 12u * 12u;
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_area_pixel_work = pixel;
  limits.max_image_work = canvas * pixel;
  {
    rh::RasterRef r(make_raster(12, 12));
    EXPECT_EQ(render(l, r.raster, nullptr, nullptr, &limits).result, GSHSCENE_OK) << "work equal to both limits is drawn";
  }
  limits.max_area_pixel_work = pixel - 1;
  {
    rh::RasterRef r(make_raster(12, 12));
    EXPECT_NE(render(l, r.raster, nullptr, nullptr, &limits).result, GSHSCENE_OK) << "one past the bound of a pixel";
  }
  limits.max_area_pixel_work = pixel;
  limits.max_image_work = canvas * pixel - 1;
  {
    rh::RasterRef r(make_raster(12, 12));
    EXPECT_NE(render(l, r.raster, nullptr, nullptr, &limits).result, GSHSCENE_OK) << "one past the work of the whole canvas";
  }
}

TEST(RenderFilterSink, ARotatedMapIsFilteredInCanonicalSpaceSoTheFilterFollowsTheElement) {
  rh::Store s;
  GSHSCENE_Filter_Primitive o = prim(GSHSCENE_FILTER_OFFSET);
  o.dx = 10;
  GSHSCENE_List * moved = group_of_rect(s, intern_filter(s, {o}), 1.0, 4, 4, 14, 14);
  GSHSCENE_List * plain = group_of_rect(s, nullptr, 1.0, 4, 4, 14, 14);
  GSHGEOM_Map turn;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &turn), GSHGEOM_OK);
  turn.t[0] = 20;
  turn.t[1] = 2;
  rh::RasterRef a(make_raster(64, 64)), b(make_raster(64, 64));
  Rendered ra = render(plain, a.raster, &turn);
  Rendered rb = render(moved, b.raster, &turn);
  ASSERT_EQ(ra.result, GSHSCENE_OK);
  ASSERT_EQ(rb.result, GSHSCENE_OK);
  EXPECT_TRUE(rb.entries.empty()) << "a rotated map is drawn, not refused";
  double ax, ay, am, bx, by, bm;
  centroid(a.raster, &ax, &ay, &am);
  centroid(b.raster, &bx, &by, &bm);
  EXPECT_NEAR(bx - ax, 10.0 * std::cos(30.0 * M_PI / 180.0), 0.5) << "the offset runs along the element's x axis";
  EXPECT_NEAR(by - ay, 10.0 * std::sin(30.0 * M_PI / 180.0), 0.5);
  EXPECT_NEAR(bm / am, 1.0, 0.03) << "the resampling keeps the coverage";
  // a quarter turn is axis aligned and is drawn as before
  GSHGEOM_Map quarter;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &quarter), GSHGEOM_OK);
  rh::RasterRef q(make_raster(64, 64));
  Rendered ok = render(moved, q.raster, &quarter);
  ASSERT_EQ(ok.result, GSHSCENE_OK);
  EXPECT_TRUE(ok.entries.empty());
}

TEST(RenderFilterSink, ATurbulencesNoiseIsTheStandardsFunctionOfThePointFromItsOriginAndAtItsFrequencyPerUnit) {
  // The noise at a pixel is the function of the pixel's position from the primitive's origin (dx, dy: where the user space's origin lies in
  // the group's space), so moving the origin by (5, 3) moves the picture by (5, 3); and a different frequency is a different picture.
  const double region[4] = {0, 0, 40, 30};
  auto noise = [&](double fx, double ox, double oy) {
    rh::Store s;
    GSHSCENE_Filter_Primitive t = prim(GSHSCENE_FILTER_TURBULENCE);
    t.input_count = 0;
    t.inputs = nullptr;
    t.sx = fx;
    t.sy = fx;
    t.octaves = 2;
    t.seed = 7;
    t.dx = ox;
    t.dy = oy;
    GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {t}, false, region), 1.0, 0, 0, 1, 1);
    rh::RasterRef r(make_raster(40, 30));
    Rendered got = render(l, r.raster);
    EXPECT_EQ(got.result, GSHSCENE_OK);
    return bytes_of(r.raster);
  };
  const std::string at_origin = noise(0.1, 0, 0);
  const std::string moved = noise(0.1, 5, 3);
  ASSERT_EQ(at_origin.size(), (size_t)40 * 30 * 4);
  size_t compared = 0;
  for (uint32_t y = 8; y < 24; y++) {
    for (uint32_t x = 10; x < 30; x++) {
      EXPECT_EQ(moved.substr(((size_t)y * 40 + x) * 4, 4), at_origin.substr(((size_t)(y - 3) * 40 + (x - 5)) * 4, 4)) << x << "," << y;
      compared++;
    }
  }
  EXPECT_EQ(compared, 16u * 20u);
  EXPECT_NE(at_origin, moved) << "the origin moves the picture";
  EXPECT_NE(at_origin, noise(0.2, 0, 0)) << "and the frequency changes it";
}

TEST(RenderFilterSink, ATurbulencesSeedIsTruncatedTowardZeroBeforeTheGeneratorGetsIt) {
  // Filter Effects 1: "the seed number ... must first be truncated, i.e. rounded to the closest integer value towards zero".
  const double region[4] = {0, 0, 24, 16};
  auto noise = [&](double seed) {
    rh::Store s;
    GSHSCENE_Filter_Primitive t = prim(GSHSCENE_FILTER_TURBULENCE);
    t.input_count = 0;
    t.inputs = nullptr;
    t.sx = 0.1;
    t.sy = 0.1;
    t.octaves = 2;
    t.seed = seed;
    GSHSCENE_List * l = group_of_rect(s, intern_filter(s, {t}, false, region), 1.0, 0, 0, 1, 1);
    rh::RasterRef r(make_raster(24, 16));
    EXPECT_EQ(render(l, r.raster).result, GSHSCENE_OK);
    return bytes_of(r.raster);
  };
  EXPECT_EQ(noise(2.5), noise(2.0)) << "2.5 is 2, not 3";
  EXPECT_NE(noise(2.5), noise(3.0));
  EXPECT_EQ(noise(2.999), noise(2.0));
  EXPECT_EQ(noise(-2.5), noise(-2.0)) << "toward zero: -2.5 is -2, not -3";
  EXPECT_NE(noise(-2.5), noise(-3.0));
  EXPECT_NE(noise(-2.5), noise(2.0)) << "and a negative seed is not its magnitude";
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

namespace {

/** A picture of one filled rectangle, in the paint @p paint. */
const GSHSCENE_Resource * rect_picture(rh::Store & s, double x0, double y0, double x1, double y1, const GSHSCENE_Paint & paint,
    double group_opacity = 1.0) {
  GSHSCENE_List * l = s.open();
  if (group_opacity != 1.0) {
    GSHSCENE_Group g;
    std::memset(&g, 0, sizeof g);
    g.opacity = group_opacity;
    EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_fill(l, s.rect(x0, y0, x1, y1), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
  if (group_opacity != 1.0) {
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Resource * pic = nullptr;
  EXPECT_EQ(gshscene_store_intern_picture(s.store, l, &pic), GSHSCENE_OK);
  s.held.push_back(pic);
  return pic;
}

/**
 * A filter whose primitives each draw one slot of its picture (FL25), the slots being the instances of @p elements under their maps,
 * and whose last primitive merges them all when there is more than one.
 */
const GSHSCENE_Resource * slot_filter(rh::Store & s, const std::vector<std::pair<const GSHSCENE_Resource *, GSHGEOM_Map>> & elements) {
  GSHSCENE_List * l = s.open();
  for (const auto & e : elements) {
    EXPECT_EQ(gshscene_list_instance(l, e.first, &e.second, 0, 0.0), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Resource * slots = nullptr;
  EXPECT_EQ(gshscene_store_intern_picture(s.store, l, &slots), GSHSCENE_OK);
  s.held.push_back(slots);
  std::vector<GSHSCENE_Filter_Primitive> prims;
  static std::vector<std::vector<GSHSCENE_Filter_Input>> inputs;  // kept for the intern call below
  for (size_t i = 0; i < elements.size(); i++) {
    GSHSCENE_Filter_Primitive p;
    std::memset(&p, 0, sizeof p);
    p.kind = GSHSCENE_FILTER_IMAGE;
    p.use_picture = 1;
    p.slot = (uint32_t)i;
    prims.push_back(p);
  }
  inputs.clear();
  inputs.emplace_back();
  if (elements.size() > 1) {
    for (uint32_t i = 0; i < elements.size(); i++) {
      inputs[0].push_back({GSHSCENE_FILTER_INPUT_RESULT, i});
    }
    GSHSCENE_Filter_Primitive m;
    std::memset(&m, 0, sizeof m);
    m.kind = GSHSCENE_FILTER_MERGE;
    m.input_count = inputs[0].size();
    m.inputs = inputs[0].data();
    prims.push_back(m);
  }
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = GSHSCENE_FILTER_REGION_AUTO;
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = prims.size();
  f.primitives = prims.data();
  f.picture = slots;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_filter(s.store, &f, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

GSHGEOM_Map moved(double dx, double dy) {
  GSHGEOM_Map m;
  gshgeom_map_translate(2, dx, dy, 0.0, &m);
  return m;
}

}  // namespace

TEST(RenderFilterSink, ASlotOfTheFiltersPictureIsTheFiltersResultUnderItsInstancesMap) {
  rh::Store s;
  GSHSCENE_Paint green = rh::solid(0, 1, 0, 1);
  const GSHSCENE_Resource * element = rect_picture(s, 2, 2, 4, 4, green);
  // the group draws a red rectangle in the corner, which the primitive does not read
  GSHSCENE_List * l = group_of_rect(s, slot_filter(s, {{element, moved(3, 1)}}), 1.0, 0, 0, 1, 1);
  rh::RasterRef r(make_raster(12, 8));
  Rendered done = render(l, r.raster);
  ASSERT_EQ(done.result, GSHSCENE_OK);
  EXPECT_TRUE(done.entries.empty());
  EXPECT_EQ(pixel(r.raster, 5, 3), px(0, 255, 0, 255)) << "the element, moved by its instance's map";
  EXPECT_EQ(pixel(r.raster, 8, 3), px(0, 0, 0, 0));
  EXPECT_EQ(pixel(r.raster, 2, 2), px(0, 0, 0, 0)) << "not where the picture alone puts it";
  EXPECT_EQ(pixel(r.raster, 0, 0), px(0, 0, 0, 0)) << "the group's own content is not the result";
}

TEST(RenderFilterSink, TwoSlotsAreTwoPicturesAndAGroupInsideOneIsDrawnInTheSlotsFrame) {
  rh::Store s;
  GSHSCENE_Paint blue = rh::solid(0, 0, 1, 1);
  GSHSCENE_Paint red = rh::solid(1, 0, 0, 1);
  const GSHSCENE_Resource * a = rect_picture(s, 0, 0, 3, 3, blue);
  const GSHSCENE_Resource * b = rect_picture(s, 0, 0, 3, 3, red, 0.5);  // a group inside the slot's picture
  GSHSCENE_List * l = group_of_rect(s, slot_filter(s, {{a, moved(1, 1)}, {b, moved(6, 1)}}), 1.0, 0, 0, 1, 1);
  rh::RasterRef r(make_raster(12, 6));
  Rendered done = render(l, r.raster);
  ASSERT_EQ(done.result, GSHSCENE_OK);
  EXPECT_EQ(pixel(r.raster, 2, 2), px(0, 0, 255, 255));
  const std::string half = pixel(r.raster, 7, 2);
  EXPECT_NEAR((unsigned char)half[0], 128, 2) << "the half-opaque group inside the second slot";
  EXPECT_NEAR((unsigned char)half[3], 128, 2);
  EXPECT_EQ(pixel(r.raster, 4, 2), px(0, 0, 0, 0)) << "between them nothing";
}

TEST(RenderFilterSink, ARenderWithASlotInADamagedWindowIsTheWholeRenderInsideIt) {
  rh::Store s;
  GSHSCENE_Paint green = rh::solid(0, 1, 0, 1);
  const GSHSCENE_Resource * element = rect_picture(s, 1, 1, 7, 6, green);
  GSHSCENE_List * l = group_of_rect(s, slot_filter(s, {{element, moved(0, 0)}}), 1.0, 0, 0, 1, 1);
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
            << x << "," << y;
      }
    }
  }
}

TEST(RenderFilterSink, ASlotUnderARotatedMapFollowsTheElementThroughCanonicalSpace) {
  rh::Store s;
  GSHSCENE_Paint green = rh::solid(0, 1, 0, 1);
  const GSHSCENE_Resource * element = rect_picture(s, 0, 0, 10, 10, green);
  GSHSCENE_List * with_slot = group_of_rect(s, slot_filter(s, {{element, moved(4, 4)}}), 1.0, 0, 0, 1, 1);
  GSHSCENE_List * plain = s.open();
  GSHSCENE_Group g;
  std::memset(&g, 0, sizeof g);
  g.opacity = 1.0;
  ASSERT_EQ(gshscene_list_group_push(plain, &g, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(plain, s.rect(4, 4, 14, 14), nullptr, GSHGEOM_FILL_NONZERO, &green, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(plain), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(plain), GSHSCENE_OK);
  GSHGEOM_Map turn;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &turn), GSHGEOM_OK);
  turn.t[0] = 20;
  turn.t[1] = 2;
  rh::RasterRef a(make_raster(64, 64)), b(make_raster(64, 64));
  Rendered ra = render(plain, a.raster, &turn);
  Rendered rb = render(with_slot, b.raster, &turn);
  ASSERT_EQ(ra.result, GSHSCENE_OK);
  ASSERT_EQ(rb.result, GSHSCENE_OK);
  EXPECT_TRUE(rb.entries.empty());
  double ax, ay, am, bx, by, bm;
  centroid(a.raster, &ax, &ay, &am);
  centroid(b.raster, &bx, &by, &bm);
  EXPECT_GT(am, 0.0);
  EXPECT_NEAR(bx, ax, 0.6) << "the same square, drawn through the canonical canvas";
  EXPECT_NEAR(by, ay, 0.6);
  EXPECT_NEAR(bm / am, 1.0, 0.05);
}

TEST(RenderFilterSink, TheCallersMemoryForASlotsFilterIsExactAndTheRenderFromItIsTheSame) {
  rh::Store s;
  GSHSCENE_Paint green = rh::solid(0, 1, 0, 1);
  GSHSCENE_Paint blue = rh::solid(0, 0, 1, 1);
  const GSHSCENE_Resource * a = rect_picture(s, 0, 0, 3, 3, green);
  const GSHSCENE_Resource * b = rect_picture(s, 0, 0, 3, 3, blue, 0.5);
  GSHSCENE_List * l = group_of_rect(s, slot_filter(s, {{a, moved(1, 1)}, {b, moved(4, 3)}}), 1.0, 0, 0, 1, 1);
  rh::RasterRef plain(make_raster(9, 9));
  ASSERT_EQ(render(l, plain.raster).result, GSHSCENE_OK);
  rh::SinkRef sink;
  rh::ReportRef report;
  rh::RasterRef r(make_raster(9, 9));
  GSHSCENE_Target target = rh::target_of(r.raster);
  GSHSCENE_Render_Request req = rh::request_of(l, &target, report.report);
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  std::vector<unsigned char> workspace(need + 4096, 0xc5);
  req.workspace = workspace.data();
  req.workspace_bytes = need - 1;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << "one byte short";
  req.workspace_bytes = need;
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(bytes_of(r.raster), bytes_of(plain.raster));
  EXPECT_EQ(pixel(r.raster, 2, 2), px(0, 255, 0, 255));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
