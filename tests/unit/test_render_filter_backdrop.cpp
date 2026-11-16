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
 * A group's backdrop filter through the sink (shape-scene filter.h, FL22, D-FL13, D-FL14; render.h FC6).  Each case is a differential
 * test: what the sink draws for a backdrop group equals what it draws for the ordinary filter of an equivalent isolated copy of the
 * backdrop (the same fills inside the same clip, drawn in a group that names the filter for its own content), computed by this test
 * with the group's clip, opacity and soft mask applied to it as FL20 says, then the group's own content composited over the result.
 * The backdrop is that of the enclosing isolated context only, a non-isolated or knockout parent refuses the group by name, and the
 * caps and the caller's memory are as for any filtered group.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

const GSHSCENE_Filter_Input kPrev = {GSHSCENE_FILTER_INPUT_PREVIOUS, 0};

GSHSCENE_Filter_Primitive prim(uint32_t kind) {
  GSHSCENE_Filter_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = kind;
  p.input_count = 1;
  p.inputs = &kPrev;
  return p;
}

/** A blur then an offset: the blur needs the pixels around the one it makes, the offset moves the result off the backdrop's own place. */
const GSHSCENE_Resource * blur_then_offset(rh::Store & s, double sigma = 1.5, double dx = 1.0) {
  GSHSCENE_Filter_Primitive b = prim(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  b.sx = b.sy = sigma;
  GSHSCENE_Filter_Primitive o = prim(GSHSCENE_FILTER_OFFSET);
  o.dx = dx;
  GSHSCENE_Filter_Primitive prims[2] = {b, o};
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = GSHSCENE_FILTER_REGION_AUTO;
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = 2;
  f.primitives = prims;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_filter(s.store, &f, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

GIMG_Raster * make_raster(uint32_t w, uint32_t h) {
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  format.alpha_form = GIMG_ALPHA_PREMULTIPLIED;
  GIMG_Raster * r = nullptr;
  if (gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, nullptr, 0, &r) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * px = (unsigned char *)gimg_raster_pixels(r);
  for (uint32_t y = 0; y < h; y++) {
    std::memset(px + y * gimg_raster_stride_bytes(r), 0, (size_t)w * 4u);
  }
  return r;
}

const unsigned kW = 16, kH = 12;

struct Pixels {
  std::vector<unsigned char> v;
  const unsigned char * at(unsigned x, unsigned y) const { return &v[4u * (y * kW + x)]; }
};

Pixels read(const GIMG_Raster * r) {
  Pixels out;
  out.v.resize(4u * kW * kH);
  const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(r);
  for (unsigned y = 0; y < kH; y++) {
    std::memcpy(&out.v[4u * y * kW], px + y * gimg_raster_stride_bytes(r), 4u * kW);
  }
  return out;
}

struct Rendered {
  GSHSCENE_Result result;
  std::vector<GSHSCENE_Report_Entry> entries;
  std::vector<std::vector<size_t>> chains;  // each entry's chain, copied: the report is gone when render returns
  Pixels pixels;
};

Rendered render(const GSHSCENE_List * list, const GSHGEOM_Map * map = nullptr, const GSHSCENE_Rect * damage = nullptr,
    uint32_t flags = 0, unsigned char fill = 0) {
  rh::SinkRef sink;
  rh::ReportRef report;
  rh::RasterRef r(make_raster(kW, kH));
  if (fill) {
    unsigned char * px = (unsigned char *)gimg_raster_pixels(r.raster);
    for (unsigned y = 0; y < kH; y++) {
      std::memset(px + y * gimg_raster_stride_bytes(r.raster), fill, 4u * kW);
    }
  }
  GSHSCENE_Target target = rh::target_of(r.raster, map);
  target.damage = damage;
  GSHSCENE_Render_Request req = rh::request_of(list, &target, report.report);
  req.flags = flags;
  Rendered out;
  out.result = gshscene_sink_render(sink.sink, &req);
  out.entries = report.entries();
  for (const GSHSCENE_Report_Entry & e : out.entries) {
    out.chains.emplace_back(e.chain, e.chain + e.chain_count);
  }
  out.pixels = read(r.raster);
  return out;
}

/** What a scene draws, in the one place the backdrop, the clip and the content are written, so the reference copies the backdrop exactly. */
struct Spec {
  const GSHSCENE_Resource * filter = nullptr;
  double opacity = 1.0;
  bool clip = false;                       // a rectangular clip around the group
  const GSHSCENE_Resource * mask = nullptr;
  bool content = false;                    // a group's own content: opaque green
  uint32_t isolation = GSHSCENE_ISOLATION_ISOLATED;  // the enclosing group's, when @p nest
  bool nest = false;                       // the whole scene (backdrop and group) inside an enclosing group
  bool outer_under = false;                // a fill beneath the enclosing group, to tell the isolated context from the target
  bool linear_blend = false;               // the group composites in linear light; the filter's source is still the backdrop in its own space
};

// fractional edges, so that the clip weighs the edge pixels (0.5 across, 0.75 down) and a clip taken as a rectangle is a different image
const double kClip[4] = {4.5, 2.25, 12.5, 9};

/** The backdrop: two overlapping fills, one half transparent, so a blur and a color mix have something to do. */
void backdrop(rh::Store & s, GSHSCENE_List * l, bool clipped) {
  GSHSCENE_Paint red = rh::solid(1, 0, 0, 1), yellow = rh::solid(1, 1, 0, 0.5);
  const GSHSCENE_Resource * clip_path = nullptr;
  if (clipped) {
    clip_path = s.rect(kClip[0], kClip[1], kClip[2], kClip[3]);
    EXPECT_EQ(gshscene_list_clip_push(l, clip_path, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_fill(l, s.rect(2, 2, 10, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(l, s.rect(6, 4, 14, 10), nullptr, GSHGEOM_FILL_NONZERO, &yellow, 0, 0.0), GSHSCENE_OK);
  if (clipped) {
    EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  }
}

GSHSCENE_Group group_of(const GSHSCENE_Resource * filter, uint32_t mode, double opacity, const GSHSCENE_Resource * mask) {
  GSHSCENE_Group g;
  std::memset(&g, 0, sizeof g);
  g.opacity = opacity;
  g.filter = filter;
  g.filter_mode = mode;
  g.soft_mask = mask;
  return g;
}

/** The list the sink is asked for: the backdrop, then the backdrop group (clipped when asked) with its content. */
GSHSCENE_List * actual(rh::Store & s, const Spec & sp) {
  GSHSCENE_List * l = s.open();
  GSHSCENE_Group outer = group_of(nullptr, 0, 1.0, nullptr);
  outer.isolation = sp.isolation;
  GSHSCENE_Paint blue = rh::solid(0, 0, 1, 1);
  if (sp.outer_under) {
    EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 12), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  }
  if (sp.nest) {
    EXPECT_EQ(gshscene_list_group_push(l, &outer, 0, 0.0), GSHSCENE_OK);
  }
  backdrop(s, l, false);
  if (sp.clip) {
    EXPECT_EQ(gshscene_list_clip_push(l, s.rect(kClip[0], kClip[1], kClip[2], kClip[3]), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  }
  GSHSCENE_Group g = group_of(sp.filter, GSHSCENE_FILTER_MODE_BACKDROP, sp.opacity, sp.mask);
  if (sp.linear_blend) {
    g.blend_space.primaries = GCOL_PRIMARIES_SRGB;
    g.blend_space.transfer = GCOL_TRANSFER_LINEAR;
  }
  EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
  if (sp.content) {
    GSHSCENE_Paint green = rh::solid(0, 1, 0, 1);
    EXPECT_EQ(gshscene_list_fill(l, s.rect(4, 3, 7, 6), nullptr, GSHGEOM_FILL_NONZERO, &green, 0, 0.0), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  if (sp.clip) {
    EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  }
  if (sp.nest) {
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

/** The backdrop alone (the same fills, no group). */
GSHSCENE_List * backdrop_only(rh::Store & s, const Spec & sp) {
  GSHSCENE_List * l = s.open();
  if (sp.outer_under) {
    GSHSCENE_Paint blue = rh::solid(0, 0, 1, 1);
    EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 12), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  }
  backdrop(s, l, false);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

/** The equivalent isolated copy of the backdrop, in the group that filters its own content: the backdrop drawn inside the clip. */
GSHSCENE_List * filtered_copy(rh::Store & s, const Spec & sp) {
  GSHSCENE_List * l = s.open();
  GSHSCENE_Group g = group_of(sp.filter, GSHSCENE_FILTER_MODE_OWN, 1.0, nullptr);
  EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
  backdrop(s, l, sp.clip);
  EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

/** The clip's coverage of the pixel: the area of the pixel inside the rectangle. */
double coverage(unsigned x, unsigned y) {
  const double cx = std::fmax(0.0, std::fmin(x + 1.0, kClip[2]) - std::fmax((double)x, kClip[0]));
  const double cy = std::fmax(0.0, std::fmin(y + 1.0, kClip[3]) - std::fmax((double)y, kClip[1]));
  return cx * cy;
}

/** The weight of the group over the pixel: its clip, its opacity, a soft mask's value (the mask is `mask_alpha` over `x < mask_x`). */
double weight(const Spec & sp, unsigned x, unsigned y, double mask_alpha, unsigned mask_x) {
  double w = sp.opacity;
  if (sp.clip) {
    w *= coverage(x, y);
  }
  if (sp.mask) {
    w *= x < mask_x ? mask_alpha : 0.0;
  }
  return w;
}

/** The expected pixels: the backdrop replaced by its filtered copy where the weight says, the content over it. */
Pixels expected(rh::Store & s, const Spec & sp, double mask_alpha = 0.0, unsigned mask_x = 0, const GSHGEOM_Map * map = nullptr) {
  Pixels base = render(backdrop_only(s, sp), map).pixels;
  Pixels filt = render(filtered_copy(s, sp), map).pixels;
  Pixels out = base;
  for (unsigned y = 0; y < kH; y++) {
    for (unsigned x = 0; x < kW; x++) {
      const double w = weight(sp, x, y, mask_alpha, mask_x);
      for (int c = 0; c < 4; c++) {
        const double p = base.at(x, y)[c], f = filt.at(x, y)[c];
        const double v = (w >= 1.0) ? f : p + w * (f - p);
        out.v[4u * (y * kW + x) + (unsigned)c] = (unsigned char)std::floor(v + 0.5);
      }
      if (sp.content && x >= 4 && x < 7 && y >= 3 && y < 6) {
        // the group's own content, unfiltered, composited over the replaced backdrop with the group's clip, mask and opacity
        static const double green[4] = {0, 255, 0, 255};
        for (int c = 0; c < 4; c++) {
          const double under = out.v[4u * (y * kW + x) + (unsigned)c];
          out.v[4u * (y * kW + x) + (unsigned)c] = (unsigned char)std::floor(under + w * (green[c] - under) + 0.5);
        }
      }
    }
  }
  return out;
}

/** The number of pixels whose bytes differ from @p b by more than @p tolerance; the first difference is described in @p where. */
size_t differing(const Pixels & a, const Pixels & b, int tolerance, std::string * where) {
  size_t n = 0;
  for (unsigned y = 0; y < kH; y++) {
    for (unsigned x = 0; x < kW; x++) {
      for (int c = 0; c < 4; c++) {
        const int d = (int)a.at(x, y)[c] - (int)b.at(x, y)[c];
        if (d > tolerance || d < -tolerance) {
          if (n == 0 && where) {
            *where = "(" + std::to_string(x) + "," + std::to_string(y) + ") channel " + std::to_string(c) + ": " +
                std::to_string((int)a.at(x, y)[c]) + " against " + std::to_string((int)b.at(x, y)[c]);
          }
          n++;
          break;
        }
      }
    }
  }
  return n;
}

/** The pixels of the backdrop's filtered copy differ from the backdrop itself in at least @p n pixels: the filter did something to test. */
void expect_filter_matters(const Pixels & a, const Pixels & b, size_t n) {
  EXPECT_GE(differing(a, b, 0, nullptr), n) << "the filter changes the backdrop here, so equality means something";
}

/** An alpha mask over a picture of one half-transparent fill of the left half of the raster: value 1/2 left of x = 8, 0 beyond. */
const GSHSCENE_Resource * half_mask(rh::Store & s) {
  GSHSCENE_List * ml = s.open();
  GSHSCENE_Paint half = rh::solid(1, 1, 1, 0.5);
  EXPECT_EQ(gshscene_list_fill(ml, s.rect(0, 0, 8, 12), nullptr, GSHGEOM_FILL_NONZERO, &half, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(ml), GSHSCENE_OK);
  const GSHSCENE_Resource * picture = nullptr;
  EXPECT_EQ(gshscene_store_intern_picture(s.store, ml, &picture), GSHSCENE_OK);
  s.held.push_back(picture);
  GSHSCENE_Mask content{};
  content.picture = picture;
  content.mode = GSHSCENE_MASK_ALPHA;
  const GSHSCENE_Resource * mask = nullptr;
  EXPECT_EQ(gshscene_store_intern_mask(s.store, &content, &mask), GSHSCENE_OK);
  s.held.push_back(mask);
  return mask;
}

}  // namespace

TEST(RenderFilterBackdrop, TheBackdropIsFilteredAndTheContentIsDrawnOverItUnfiltered) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.content = true;
  Rendered got = render(actual(s, sp));
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty());
  Pixels want = expected(s, sp);
  std::string where;
  EXPECT_EQ(differing(got.pixels, want, 0, &where), 0u) << where;
  expect_filter_matters(want, render(backdrop_only(s, sp)).pixels, 20);
  // the content is not filtered: its pixels are the pure green, whatever surrounds them
  EXPECT_EQ(got.pixels.at(5, 4)[1], 255);
  EXPECT_EQ(got.pixels.at(5, 4)[0], 0);
}

TEST(RenderFilterBackdrop, TheGroupsClipLimitsTheBackdropTheFilterSeesAndWhereItIsReplaced) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.clip = true;
  sp.content = true;
  Rendered got = render(actual(s, sp));
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty());
  std::string where;
  EXPECT_EQ(differing(got.pixels, expected(s, sp), 1, &where), 0u) << where;
  // outside the clip the backdrop is exactly what it was
  Pixels base = render(backdrop_only(s, sp)).pixels;
  for (unsigned y = 0; y < kH; y++) {
    for (unsigned x = 0; x < kW; x++) {
      if (coverage(x, y) == 0.0) {
        EXPECT_EQ(std::memcmp(got.pixels.at(x, y), base.at(x, y), 4), 0) << x << "," << y;
      }
    }
  }
}

TEST(RenderFilterBackdrop, TheOpacityWeighsTheFilteredResultAgainstTheBackdrop) {
  // FL20: opacity applies to the filtered result; the replacement is P + o (F - P), here with the clip
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.clip = true;
  sp.opacity = 0.4;
  Rendered got = render(actual(s, sp));
  ASSERT_EQ(got.result, GSHSCENE_OK);
  std::string where;
  EXPECT_LE(differing(got.pixels, expected(s, sp), 1, &where), 0u) << where;
  // not the opacity of the whole group over the result: a half-weighted pixel is neither the backdrop nor the filtered pixel
  Pixels base = render(backdrop_only(s, sp)).pixels;
  Spec full = sp;
  full.opacity = 1.0;
  Pixels filt = expected(s, full);
  EXPECT_NE(differing(got.pixels, base, 0, nullptr), 0u);
  EXPECT_NE(differing(got.pixels, filt, 0, nullptr), 0u);
}

TEST(RenderFilterBackdrop, ASoftMaskWeighsTheFilteredResultByItsValue) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.mask = half_mask(s);
  sp.content = true;
  Rendered got = render(actual(s, sp));
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty());
  std::string where;
  EXPECT_LE(differing(got.pixels, expected(s, sp, 0.5, 8), 1, &where), 0u) << where;
}

TEST(RenderFilterBackdrop, TheBackdropIsTheEnclosingIsolatedGroupsNotTheTargets) {
  // The fill under the enclosing group is not in the group's layer, so the filter does not see it and the replacement leaves it alone:
  // the layer is the filtered copy of the two fills, composited over the blue.
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.nest = true;
  sp.outer_under = true;
  Rendered got = render(actual(s, sp));
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty());
  // reference: [blue, isolated group { the filtered copy }]
  GSHSCENE_List * ref = s.open();
  GSHSCENE_Paint blue = rh::solid(0, 0, 1, 1);
  ASSERT_EQ(gshscene_list_fill(ref, s.rect(0, 0, 16, 12), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group iso = group_of(nullptr, 0, 1.0, nullptr);
  ASSERT_EQ(gshscene_list_group_push(ref, &iso, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group own = group_of(sp.filter, GSHSCENE_FILTER_MODE_OWN, 1.0, nullptr);
  ASSERT_EQ(gshscene_list_group_push(ref, &own, 0, 0.0), GSHSCENE_OK);
  backdrop(s, ref, false);
  ASSERT_EQ(gshscene_list_group_pop(ref), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(ref), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(ref), GSHSCENE_OK);
  std::string where;
  EXPECT_EQ(differing(got.pixels, render(ref).pixels, 0, &where), 0u) << where;
  // had the target's blue been blurred in, the corner pixel far from every fill would not be pure blue
  EXPECT_EQ(std::memcmp(got.pixels.at(0, 0), "\x00\x00\xff\xff", 4), 0);
  EXPECT_EQ(std::memcmp(got.pixels.at(0, 11), "\x00\x00\xff\xff", 4), 0);
}

TEST(RenderFilterBackdrop, AMaskedParentComposesWhatTheReplacementPutInItsLayer) {
  // The parent holds nothing but a backdrop group whose filter floods the region: the replacement is all the parent's layer has, and the
  // parent's pop (a half-transparent mask over its left half) must still find it there.
  rh::Store s;
  GSHSCENE_Filter_Primitive fl;
  std::memset(&fl, 0, sizeof fl);
  fl.kind = GSHSCENE_FILTER_FLOOD;
  fl.color.rgba[0] = fl.color.rgba[1] = fl.color.rgba[2] = fl.color.rgba[3] = 1.0;
  fl.opacity = 1.0;
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = GSHSCENE_FILTER_REGION_EXPLICIT;
  f.region[2] = kW;
  f.region[3] = kH;
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = 1;
  f.primitives = &fl;
  const GSHSCENE_Resource * flood = nullptr;
  ASSERT_EQ(gshscene_store_intern_filter(s.store, &f, &flood), GSHSCENE_OK);
  s.held.push_back(flood);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Group parent = group_of(nullptr, 0, 1.0, half_mask(s));
  ASSERT_EQ(gshscene_list_group_push(l, &parent, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group child = group_of(flood, GSHSCENE_FILTER_MODE_BACKDROP, 1.0, nullptr);
  ASSERT_EQ(gshscene_list_group_push(l, &child, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Rendered got = render(l);
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty());
  for (unsigned y = 0; y < kH; y += 5) {
    for (unsigned x = 0; x < kW; x++) {
      const int want = x < 8 ? 128 : 0;  // white at the mask's half
      for (int c = 0; c < 4; c++) {
        EXPECT_NEAR((int)got.pixels.at(x, y)[c], want, 1) << x << "," << y << " channel " << c;
      }
    }
  }
}

TEST(RenderFilterBackdrop, ANonIsolatedOrKnockoutParentRefusesTheGroupByName) {
  for (int variant = 0; variant < 3; variant++) {
    rh::Store s;
    Spec sp;
    sp.filter = blur_then_offset(s);
    sp.content = true;
    sp.nest = true;
    sp.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
    GSHSCENE_List * l = nullptr;
    if (variant == 0) {
      l = actual(s, sp);
    } else {
      // the same under a knockout group, and (variant 2) under an isolated group that has one for an ancestor
      l = s.open();
      GSHSCENE_Group ko = group_of(nullptr, 0, 1.0, nullptr);
      ko.knockout = 1;
      ASSERT_EQ(gshscene_list_group_push(l, &ko, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group inner = group_of(nullptr, 0, 1.0, nullptr);
      if (variant == 2) {
        ASSERT_EQ(gshscene_list_group_push(l, &inner, 0, 0.0), GSHSCENE_OK);
      }
      backdrop(s, l, false);
      GSHSCENE_Group g = group_of(sp.filter, GSHSCENE_FILTER_MODE_BACKDROP, 1.0, nullptr);
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Paint green = rh::solid(0, 1, 0, 1);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(4, 3, 7, 6), nullptr, GSHGEOM_FILL_NONZERO, &green, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      if (variant == 2) {
        ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      }
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    }
    Rendered got = render(l);
    ASSERT_EQ(got.result, GSHSCENE_OK) << variant;
    ASSERT_EQ(got.entries.size(), 1u) << variant;
    EXPECT_EQ(got.entries[0].reason, GSHSCENE_REASON_FILTER_BACKDROP) << variant;
    EXPECT_EQ(got.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED) << variant;
    ASSERT_EQ(got.chains[0].size(), 1u) << variant;
    EXPECT_EQ(got.chains[0][0], variant == 2 ? 4u : 3u) << variant << ": the entry names the group's push";
    // the refused group's block is skipped: no green, and the backdrop is drawn as it is, unfiltered
    EXPECT_NE(std::memcmp(got.pixels.at(5, 4), "\x00\xff\x00\xff", 4), 0) << variant;
    EXPECT_EQ(std::memcmp(got.pixels.at(3, 3), "\xff\x00\x00\xff", 4), 0) << variant << ": the red fill is drawn as it was";
    // and under strict the render stops
    Rendered strict = render(l, nullptr, nullptr, GSHSCENE_RENDER_STRICT);
    EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED) << variant;
  }
}

TEST(RenderFilterBackdrop, TheFilterRunsOverTheBackdropInItsOwnSpaceWhateverTheGroupCompositesIn) {
  // The backdrop is in the enclosing layer's space; the group's blend space is where its own content composites (N3), and does not
  // move the filter's source.  With no content the two groups draw the same.
  rh::Store s;
  Spec plain;
  plain.filter = blur_then_offset(s);
  Spec linear = plain;
  linear.linear_blend = true;
  Rendered a = render(actual(s, plain)), b = render(actual(s, linear));
  ASSERT_EQ(a.result, GSHSCENE_OK);
  ASSERT_EQ(b.result, GSHSCENE_OK);
  std::string where;
  EXPECT_EQ(differing(a.pixels, b.pixels, 0, &where), 0u) << where;
  expect_filter_matters(a.pixels, render(backdrop_only(s, plain)).pixels, 20);
}

TEST(RenderFilterBackdrop, ARotatedMapIsFilteredInCanonicalSpaceAsAnOrdinaryFilterIs) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s, 1.0, 2.0);
  GSHGEOM_Map turn;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 20.0, &turn), GSHGEOM_OK);
  turn.t[0] = 6;
  turn.t[1] = -1;
  Rendered got = render(actual(s, sp), &turn);
  ASSERT_EQ(got.result, GSHSCENE_OK);
  EXPECT_TRUE(got.entries.empty()) << "a rotated map is drawn, not refused";
  std::string where;
  EXPECT_EQ(differing(got.pixels, expected(s, sp, 0.0, 0, &turn), 0, &where), 0u) << where;
}

TEST(RenderFilterBackdrop, ADamagedRenderEqualsTheWholeRenderInsideTheDamageAndTouchesNothingOutsideIt) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.clip = true;
  sp.content = true;
  GSHSCENE_List * l = actual(s, sp);
  // the target's own pixels are part of the backdrop (here a gray); the whole render and the damaged ones start from the same target
  const Pixels whole = render(l, nullptr, nullptr, 0, 0x5a).pixels;
  const Pixels bare = render(backdrop_only(s, sp), nullptr, nullptr, 0, 0x5a).pixels;
  EXPECT_NE(differing(whole, bare, 0, nullptr), 0u) << "the filter reached the target's gray, so the damaged renders have something to match";
  const GSHSCENE_Rect damages[3] = {{3, 2, 9, 8}, {0, 0, 7, 12}, {8, 0, 16, 5}};
  for (const GSHSCENE_Rect & d : damages) {
    const Pixels part = render(l, nullptr, &d, 0, 0x5a).pixels;
    for (int64_t y = 0; y < kH; y++) {
      for (int64_t x = 0; x < kW; x++) {
        const bool in = x >= d.x0 && x < d.x1 && y >= d.y0 && y < d.y1;
        const unsigned char * want = in ? whole.at((unsigned)x, (unsigned)y) : (const unsigned char *)"\x5a\x5a\x5a\x5a";
        EXPECT_EQ(std::memcmp(part.at((unsigned)x, (unsigned)y), want, 4), 0)
            << x << "," << y << " damage " << d.x0 << "," << d.y0 << " to " << d.x1 << "," << d.y1;
      }
    }
  }
}

TEST(RenderFilterBackdrop, TheCallersMemoryIsExactAndTheRenderFromItIsTheSame) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  sp.clip = true;
  sp.mask = half_mask(s);
  sp.content = true;
  GSHSCENE_List * l = actual(s, sp);
  const Pixels plain = render(l).pixels;
  rh::SinkRef sink;
  rh::ReportRef report;
  rh::RasterRef r(make_raster(kW, kH));
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
  EXPECT_EQ(std::memcmp(read(r.raster).v.data(), plain.v.data(), plain.v.size()), 0) << "from the caller's memory the render is the same";
}

TEST(RenderFilterBackdrop, ATileThatHoldsABackdropGroupRefusesTheUseThatIsPaintedWithIt) {
  // D-FL14: a tile is drawn on a grid of its own, which the backdrop's canvas does not follow: the use is refused (the sink's limit, PC8),
  // never drawn with a backdrop that is not the tile's
  rh::Store s;
  GSHSCENE_List * tile = s.open();
  GSHSCENE_Paint red = rh::solid(1, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(tile, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group g = group_of(blur_then_offset(s), GSHSCENE_FILTER_MODE_BACKDROP, 1.0, nullptr);
  ASSERT_EQ(gshscene_list_group_push(tile, &g, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(tile), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(tile), GSHSCENE_OK);
  const GSHSCENE_Resource * picture = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, tile, &picture), GSHSCENE_OK);
  s.held.push_back(picture);
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = picture;
  pt.w = pt.h = pt.x_step = pt.y_step = 4.0;
  pt.repeat_x = pt.repeat_y = 1;
  const GSHSCENE_Resource * pattern = nullptr;
  ASSERT_EQ(gshscene_store_intern_pattern(s.store, &pt, &pattern), GSHSCENE_OK);
  s.held.push_back(pattern);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint painted;
  std::memset(&painted, 0, sizeof painted);
  painted.kind = GSHSCENE_PAINT_PATTERN;
  painted.resource = pattern;
  painted.rgba[3] = 1.0;
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 12), nullptr, GSHGEOM_FILL_NONZERO, &painted, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Rendered got = render(l);
  ASSERT_EQ(got.result, GSHSCENE_OK);
  ASSERT_EQ(got.entries.size(), 1u);
  EXPECT_EQ(got.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(got.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(std::memcmp(got.pixels.at(1, 1), "\0\0\0\0", 4), 0) << "the refused use draws nothing";
}

TEST(RenderFilterBackdrop, TheSinkClaimsTheBackdropAndTheGroupFilterFlag) {
  rh::Store s;
  Spec sp;
  sp.filter = blur_then_offset(s);
  rh::SinkRef sink;
  GSHSCENE_Sink_Caps caps;
  GSHSCENE_Target target;
  std::memset(&target, 0, sizeof target);
  rh::RasterRef r(make_raster(kW, kH));
  target = rh::target_of(r.raster);
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &target, &caps), GSHSCENE_OK);
  EXPECT_NE(caps.filter_native & GSHSCENE_FILTER_NATIVE_BACKDROP, 0u);
  EXPECT_NE(caps.flags & GSHSCENE_CAP_GROUP_FILTER, 0u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
