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
 * Images (masks-images-patterns story I3; shape-scene's IP1 to IP18; IC1 to IC14 here): the sampling primitives against the
 * image oracle's committed vectors (shape-scene's tests/image_vectors.h, installed), the decode against the scene's one
 * reference, and the sink's wiring of them (the sample point, the composed chain, the filter chosen once for the fill, the
 * conversions, the tint, the multiplier) and its refusals.
 */

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/image_vectors.h>

#include "../../src/render/render_internal.h"
#include "../image_helpers.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rh;

namespace {

GSHSCENE_Color_Space space_from(const imagevec::Space & s) {
  GSHSCENE_Color_Space out{};
  out.primaries = (GCOL_Primaries)s.primaries;
  out.transfer = (GCOL_Transfer)s.transfer;
  out.gamma = s.gamma;
  return out;
}

GSHSCENE_Color_Space srgb() {
  GSHSCENE_Color_Space out{};
  out.primaries = GCOL_PRIMARIES_SRGB;
  out.transfer = GCOL_TRANSFER_SRGB;
  return out;
}

GSHSCENE_Color_Space linear_srgb() {
  GSHSCENE_Color_Space out{};
  out.primaries = GCOL_PRIMARIES_SRGB;
  out.transfer = GCOL_TRANSFER_LINEAR;
  return out;
}

bool is_zero(const GSHSCENE_Color_Space & s) {
  return s.primaries == 0 && s.transfer == 0 && s.gamma == 0.0;
}

GSHGEOM_Map matrix(double a, double b, double c, double d, double e, double f) {
  const double m[4] = {a, b, c, d};
  const double t[2] = {e, f};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

/** An image of the oracle's, interned. */
const GSHSCENE_Resource * vector_image(GSHSCENE_Store * store, const imagevec::Image & image) {
  ih::Fmt f = ih::fmt(image.layout, image.type != 0, image.bits, (GIMG_Alpha_Form)image.alpha_form, image.alpha_only != 0);
  std::vector<uint32_t> samples(image.samples, image.samples + (size_t)image.width * image.height * image.layout);
  return ih::intern_image(store, image.width, image.height, f, samples, space_from(image.space));
}

/** Whether @p got is within `tolerance * max(1, the largest |want|)` of @p want in every component. */
::testing::AssertionResult close(const double got[4], const double want[4], double tolerance) {
  double scale = 1.0;
  for (int k = 0; k < 4; k++) {
    scale = std::fmax(scale, std::fabs(want[k]));
  }
  for (int k = 0; k < 4; k++) {
    if (!(std::fabs(got[k] - want[k]) <= tolerance * scale)) {
      return ::testing::AssertionFailure() << "component " << k << ": " << got[k] << " against " << want[k] << " (bound "
                                           << tolerance * scale << ")";
    }
  }
  return ::testing::AssertionSuccess();
}

GSHSCENE_Paint image_paint_of(const GSHSCENE_Resource * ip, double alpha) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_IMAGE;
  p.resource = ip;
  p.rgba[3] = alpha;
  return p;
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

/** The pixel (x, y) of the freshly made w by h RGBAF32 target a render starts from. */
struct Backdrop {
  RasterRef raster;
  Backdrop(uint32_t w, uint32_t h) : raster(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, w, h)) {}
  void at(int64_t x, int64_t y, double d[4]) const {
    gshcpu_render_load(GSHCPU_FORMAT_RGBAF32_STRAIGHT,
        (const unsigned char *)gimg_raster_pixels_const(raster.raster) + (size_t)y * gimg_raster_stride_bytes(raster.raster),
        (size_t)x, d);
  }
};

} // namespace

/* --- IC1: what the sink claims -------------------------------------------------------------------------------- */

TEST(RenderImageCaps, EveryFormatAndEdgeAndTheFilterSpacesAreClaimed) {
  SinkRef sink;
  for (GSHCPU_Format format : RF_FORMATS) {
    RasterRef r(rf_raster(format, 4, 4));
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Sink_Caps caps{};
    ASSERT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
    EXPECT_EQ(caps.image_extends, 15u);
    EXPECT_EQ(caps.image_formats, (uint32_t)GSHSCENE_IMAGE_FORMAT_ALL);
    EXPECT_TRUE(caps.flags & GSHSCENE_CAP_IMAGE_FILTER_SPACES);
    EXPECT_TRUE(caps.image_filters & (1u << GSHSCENE_IMAGE_FILTER_NEAREST));
    EXPECT_TRUE(caps.image_filters & (1u << GSHSCENE_IMAGE_FILTER_LINEAR));
    EXPECT_TRUE(caps.image_filters & (1u << GSHSCENE_IMAGE_FILTER_CUBIC));
  }
}

/* --- IC4: the decode every table and inlined read is gated equal to --------------------------------------------- */

TEST(RenderImageDecode, EverySampleOfEveryFormatDecodesAsTheSceneDecodesIt) {
  struct Case {
    ih::Fmt f;
    const char * name;
  };
  const Case cases[] = {
      {ih::fmt(1, false, 8, GIMG_ALPHA_NONE), "gray8"},
      {ih::fmt(3, false, 8, GIMG_ALPHA_NONE), "rgb8"},
      {ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT), "rgba8 straight"},
      {ih::fmt(4, false, 8, GIMG_ALPHA_PREMULTIPLIED), "rgba8 premultiplied"},
      {ih::fmt(1, false, 8, GIMG_ALPHA_NONE, true), "alpha8"},
      {ih::fmt(1, false, 12, GIMG_ALPHA_NONE), "gray12"},
      {ih::fmt(4, false, 12, GIMG_ALPHA_STRAIGHT), "rgba12 straight"},
      {ih::fmt(1, false, 16, GIMG_ALPHA_NONE), "gray16"},
      {ih::fmt(4, false, 16, GIMG_ALPHA_PREMULTIPLIED), "rgba16 premultiplied"},
      {ih::fmt(1, true, 32, GIMG_ALPHA_NONE), "grayf"},
      {ih::fmt(3, true, 32, GIMG_ALPHA_NONE), "rgbf"},
      {ih::fmt(4, true, 32, GIMG_ALPHA_STRAIGHT), "rgbaf straight"},
      {ih::fmt(4, true, 32, GIMG_ALPHA_PREMULTIPLIED), "rgbaf premultiplied"},
      {ih::fmt(1, true, 32, GIMG_ALPHA_NONE, true), "alphaf"},
  };
  const float specials[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 1e-40f, 1e30f, -1e30f, 1.8446744073709552e19f, 3.4e38f, -3.4e38f};
  for (const Case & c : cases) {
    SCOPED_TRACE(c.name);
    Store s;
    const uint32_t w = c.f.bits == 16 ? 256u : 64u;
    const uint32_t h = c.f.bits == 16 ? 256u * c.f.channels : c.f.bits == 12 ? 64u * c.f.channels : 4u * c.f.channels;
    std::vector<uint32_t> samples((size_t)w * h * c.f.channels);
    for (size_t i = 0; i < samples.size(); i++) {
      if (c.f.bits == 32) {
        samples[i] = ih::float_bits(i < 2000 ? (float)(((int64_t)i * 37 % 211) - 105) / 64.0f : specials[i % 11]);
        if ((c.f.alpha != GIMG_ALPHA_NONE || c.f.alpha_only) && i % c.f.channels + 1 == c.f.channels) {
          samples[i] = ih::float_bits((float)((i * 37) % 65) / 64.0f); // a float alpha lies in [0, 1] (IM3)
        }
      } else {
        samples[i] = (uint32_t)((i * 2654435761u) >> 7) & ((1u << c.f.bits) - 1u);
      }
      if (c.f.bits == 8 && i < 256) {
        samples[i] = (uint32_t)i;
      }
    }
    const GSHSCENE_Resource * image = ih::intern_image(s.store, w, h, c.f, samples);
    ASSERT_NE(image, nullptr) << "the store refused the image";
    s.held.push_back(image);
    const uint32_t window[4] = {0, 0, 0, 0};
    GSHCPU_Image im;
    GSHSCENE_Color_Space space = srgb();
    const GSHSCENE_Color_Space own{};
    ASSERT_EQ(gshcpu_render_image_open(&im, image, window, 0, 0, GSHSCENE_IMAGE_FILTER_NEAREST, &own, &space, &space), GSHCPU_OK);
    size_t checked = 0;
    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        double want[4];
        double got[4];
        ASSERT_EQ(gshscene_image_decode(image, x, y, want), GSHSCENE_OK);
        gshcpu_render_image_texel(&im, x, y, got);
        for (int k = 0; k < 4; k++) {
          ASSERT_EQ(memcmp(&got[k], &want[k], sizeof got[k]), 0) << "texel " << x << "," << y << " component " << k;
        }
        checked++;
      }
    }
    EXPECT_EQ(checked, (size_t)w * h);
  }
}

/* --- IC13, primitive stage: the point filters against the oracle's vectors ---------------------------------------- */

TEST(RenderImageVectors, EveryPointFilterAgreesWithTheOracleWithinTheStatedTolerance) {
  Store s;
  std::vector<const GSHSCENE_Resource *> images;
  for (unsigned i = 0; i < imagevec::kImageCount; i++) {
    images.push_back(vector_image(s.store, imagevec::kImages[i]));
    ASSERT_NE(images.back(), nullptr) << imagevec::kImages[i].name;
    s.held.push_back(images.back());
  }
  size_t checked = 0;
  size_t exact = 0;
  size_t exact_equal = 0;
  double worst = 0.0;
  for (unsigned i = 0; i < imagevec::kPointCount; i++) {
    const imagevec::Point & p = imagevec::kPoints[i];
    GSHSCENE_Color_Space layer = space_from(p.layer);
    GSHSCENE_Color_Space filter_space = space_from(p.filter_space);
    if (is_zero(layer)) {
      layer = srgb();
    }
    GSHCPU_Image im;
    const uint32_t window[4] = {(uint32_t)p.window[0], (uint32_t)p.window[1], (uint32_t)p.window[2], (uint32_t)p.window[3]};
    ASSERT_EQ(gshcpu_render_image_open(&im, images[(size_t)p.image], window, p.extend_x, p.extend_y, p.filter, &filter_space,
                  &layer, &layer),
        GSHCPU_OK)
        << p.name << " (vector " << i << ")";
    double got[4];
    gshcpu_render_image_point(&im, p.u, p.v, got);
    EXPECT_TRUE(close(got, p.expected, p.tolerance)) << p.name << " (vector " << i << ")";
    double scale = 1.0;
    for (int k = 0; k < 4; k++) {
      scale = std::fmax(scale, std::fabs(p.expected[k]));
    }
    for (int k = 0; k < 4 && p.tolerance > 0.0; k++) {
      worst = std::fmax(worst, std::fabs(got[k] - p.expected[k]) / (scale * p.tolerance));
    }
    if (p.tolerance == 0.0) {
      exact++;
      exact_equal += got[0] == p.expected[0] && got[1] == p.expected[1] && got[2] == p.expected[2] && got[3] == p.expected[3];
    }
    checked++;
  }
  EXPECT_EQ(checked, (size_t)imagevec::kPointCount);
  EXPECT_GT(exact, 0u);
  std::printf("image vectors: %zu points, %zu exact (%zu bit for bit); worst error %.3g of its stated tolerance\n", checked, exact, exact_equal, worst);
  EXPECT_EQ(exact, exact_equal) << "a case the oracle calls exact is the sink's bit for bit";
}

/* --- the sink's wiring: sample point, chain, filter, conversion, tint, multiplier -------------------------------- */

namespace {

struct Render {
  Seen seen;
  uint32_t refused = 0;
  uint32_t drawn_reason = 0;
};

/** Render @p l on a w by h RGBAF32 raster under @p tmap, the observer's root left in @p out. */
void render_observed(const GSHSCENE_List * l, uint32_t w, uint32_t h, const GSHGEOM_Map * tmap, Render & out,
    uint32_t flags = 0) {
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshcpu_render_sink_create_observed(nullptr, nullptr, nullptr, observe, &out.seen, &sink), GSHCPU_OK);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, w, h));
  GSHSCENE_Target t = target_of(r.raster, tmap);
  t.blend_space = srgb();
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  req.flags = flags;
  ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
  for (const GSHSCENE_Report_Entry & e : report.entries()) {
    if (e.entry_class == GSHSCENE_ENTRY_REFUSED) {
      out.refused++;
      out.drawn_reason = (uint32_t)e.reason;
    }
  }
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

} // namespace

// Every pixel of a fill covering the raster is the filter's value at the pixel's center under the chain T M G composed by
// shape-geometry (dyadic here, so its inverse is exact), taken to the layer's space, times the multiplier, composited once.
TEST(RenderImageSink, EachPixelIsTheFilteredValueAtItsCenterUnderTheComposedChain) {
  struct Filter {
    uint32_t magnify, minify;
  };
  const Filter filters[] = {{GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_NEAREST},
      {GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_NEAREST}, {GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_LINEAR}};
  for (const Filter & fl : filters) {
    for (uint32_t extend = 0; extend < GSHSCENE_IMAGE_EXTEND_COUNT; extend++) {
      Store s;
      const ih::Fmt f = ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT);
      std::vector<uint32_t> samples;
      for (uint32_t i = 0; i < 5u * 4u * 4u; i++) {
        samples.push_back((i * 53u + 17u) % 256u);
      }
      GSHSCENE_Color_Space display_p3{};
      display_p3.primaries = GCOL_PRIMARIES_DISPLAY_P3;
      display_p3.transfer = GCOL_TRANSFER_SRGB;
      const GSHSCENE_Resource * image = ih::intern_image(s.store, 5, 4, f, samples, display_p3);
      ASSERT_NE(image, nullptr);
      s.held.push_back(image);
      GSHSCENE_Image_Paint ip;
      gshscene_image_paint_init(&ip);
      ip.image = image;
      ip.map = matrix(4.0, 0.0, 0.0, 4.0, 2.0, 1.0);
      ip.extend_x = extend;
      ip.extend_y = (extend + 1) % GSHSCENE_IMAGE_EXTEND_COUNT;
      ip.magnify = fl.magnify;
      ip.minify = fl.minify;
      ip.filter_space = linear_srgb();
      const GSHSCENE_Resource * paint = nullptr;
      ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
      s.held.push_back(paint);
      GSHSCENE_List * l = s.open();
      GSHGEOM_Map item = matrix(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
      GSHSCENE_Paint p = image_paint_of(paint, 0.75);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(-100, -100, 100, 100), &item, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      Render out;
      GSHGEOM_Map tmap = matrix(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
      render_observed(l, 24, 20, &tmap, out);
      ASSERT_EQ(out.refused, 0u);
      // The expectation: the primitives under an inverse formed by hand (the composed map is a scale by 4 and a translation).
      const uint32_t window[4] = {0, 0, 0, 0};
      GSHCPU_Image im;
      GSHSCENE_Color_Space target = srgb();
      ASSERT_EQ(gshcpu_render_image_open(&im, image, window, ip.extend_x, ip.extend_y, fl.magnify, &ip.filter_space, &target,
                    &target),
          GSHCPU_OK);
      Backdrop backdrop(24, 20);
      for (int64_t y = 0; y < 20; y++) {
        for (int64_t x = 0; x < 24; x++) {
          double v[4];
          gshcpu_render_image_point(&im, ((double)x + 0.5 - 2.0) / 4.0, ((double)y + 0.5 - 1.0) / 4.0, v);
          double src[4] = {v[0] * 0.75, v[1] * 0.75, v[2] * 0.75, v[3] * 0.75};
          double d[4];
          backdrop.at(x, y, d);
          gshcpu_render_apply(d, src, 1.0, GSHSCENE_COMPOSITE_SRC_OVER);
          for (int k = 0; k < 4; k++) {
            ASSERT_EQ(out.seen.root[4 * ((size_t)y * 24 + (size_t)x) + (size_t)k], d[k])
                << "magnify " << fl.magnify << " extend " << extend << " pixel " << x << "," << y << " component " << k;
          }
        }
      }
    }
  }
}

// IC3: the filter is chosen once for the fill, by whether the composed map stretches every direction: a unit scale minifies.
TEST(RenderImageSink, TheFilterIsChosenOncePerFillByTheComposedMap) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  std::vector<uint32_t> samples;
  for (uint32_t i = 0; i < 16; i++) {
    samples.push_back(i * 16u);
  }
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 4, 4, f, samples);
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  for (double scale : {1.0, 2.0}) {
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image;
    ip.map = matrix(scale, 0, 0, scale, 0.5, 0.0);
    ip.magnify = GSHSCENE_IMAGE_FILTER_LINEAR;
    ip.minify = GSHSCENE_IMAGE_FILTER_NEAREST;
    const GSHSCENE_Resource * paint = nullptr;
    ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
    s.held.push_back(paint);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint p = image_paint_of(paint, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(-100, -100, 100, 100), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Render out;
    render_observed(l, 8, 4, nullptr, out);
    const uint32_t window[4] = {0, 0, 0, 0};
    GSHCPU_Image im;
    GSHSCENE_Color_Space t = srgb();
    ASSERT_EQ(gshcpu_render_image_open(&im, image, window, 0, 0, scale == 1.0 ? ip.minify : ip.magnify, &ip.filter_space, &t, &t),
        GSHCPU_OK);
    Backdrop backdrop(8, 4);
    for (int64_t y = 0; y < 4; y++) {
      for (int64_t x = 0; x < 8; x++) {
        double v[4];
        gshcpu_render_image_point(&im, ((double)x + 0.5 - 0.5) / scale, ((double)y + 0.5) / scale, v);
        double d[4];
        backdrop.at(x, y, d);
        gshcpu_render_apply(d, v, 1.0, GSHSCENE_COMPOSITE_SRC_OVER);
        EXPECT_EQ(out.seen.root[4 * ((size_t)y * 8 + (size_t)x)], d[0]) << "scale " << scale << " pixel " << x << "," << y;
      }
    }
  }
}

// IC8 and IP9: an alpha-only image takes its color from the tint, premultiplied by m a_t a'.
TEST(RenderImageSink, AStencilIsThePaintOfItsTintTimesTheFilteredAlpha) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE, true);
  std::vector<uint32_t> samples = {0, 85, 170, 255, 255, 170, 85, 0};
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 4, 2, f, samples);
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  GSHSCENE_Paint tint{};
  tint.kind = GSHSCENE_PAINT_SOLID;
  tint.rgba[0] = 0.8;
  tint.rgba[1] = 0.4;
  tint.rgba[2] = 0.1;
  tint.rgba[3] = 0.5;
  tint.space = linear_srgb();
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  ip.map = matrix(2, 0, 0, 2, 0, 0);
  ip.magnify = GSHSCENE_IMAGE_FILTER_NEAREST;
  ip.tint = &tint;
  const GSHSCENE_Resource * paint = nullptr;
  ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
  s.held.push_back(paint);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint p = image_paint_of(paint, 0.5);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 4), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Render out;
  render_observed(l, 8, 4, nullptr, out);
  ASSERT_EQ(out.refused, 0u);
  double rgb[3] = {0.8, 0.4, 0.1};
  GSHCPU_Convert c;
  GSHSCENE_Color_Space from = linear_srgb();
  GSHSCENE_Color_Space to = srgb();
  gshcpu_render_convert_prepare(&c, &from, &to);
  gshcpu_render_convert_straight(&c, rgb, rgb);
  Backdrop backdrop(8, 4);
  for (int64_t y = 0; y < 4; y++) {
    for (int64_t x = 0; x < 8; x++) {
      const double a = (double)samples[(size_t)(y / 2) * 4 + (size_t)(x / 2)] / 255.0;
      const double k = (0.5 * 0.5) * a;
      const double src[4] = {rgb[0] * k, rgb[1] * k, rgb[2] * k, k};
      double want[4];
      backdrop.at(x, y, want);
      gshcpu_render_apply(want, src, 1.0, GSHSCENE_COMPOSITE_SRC_OVER);
      for (int j = 0; j < 4; j++) {
        EXPECT_EQ(out.seen.root[4 * ((size_t)y * 8 + (size_t)x) + (size_t)j], want[j]) << x << "," << y << " component " << j;
      }
    }
  }
}

// IC9, IP12: a rectangle that maps outside the sampling domain is the sink's resource limit; a singular map is a notice.
TEST(RenderImageSink, APastDomainFillIsRefusedAndASingularMapDrawsNothing) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 2, 2, f, {10, 20, 30, 40});
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  {
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image;
    ip.map = matrix(0x1p-30, 0, 0, 0x1p-30, 0, 0); // the raster's 24 pixels are 24 * 2^30 texels
    const GSHSCENE_Resource * paint = nullptr;
    ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
    s.held.push_back(paint);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint p = image_paint_of(paint, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 24, 20), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Render out;
    render_observed(l, 24, 20, nullptr, out);
    EXPECT_EQ(out.refused, 1u);
    EXPECT_EQ(out.drawn_reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
    for (size_t i = 0; i < out.seen.touched.size(); i++) {
      EXPECT_EQ(out.seen.touched[i], 0) << "nothing of the refused fill is drawn";
    }
    // The same under strict stops the render with the finished prefix.
    Render strict;
    GSHSCENE_Sink * sink = nullptr;
    ASSERT_EQ(gshcpu_sink_create(nullptr, nullptr, nullptr, &sink), GSHCPU_OK);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, 24, 20));
    GSHSCENE_Target t = target_of(r.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
  {
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image;
    GSHSCENE_List * l = s.open();
    const GSHSCENE_Resource * paint = nullptr;
    ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
    s.held.push_back(paint);
    GSHSCENE_Paint p = image_paint_of(paint, 1.0);
    GSHGEOM_Map singular = matrix(1, 0, 0, 0, 0, 0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), &singular, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Render out;
    render_observed(l, 8, 8, nullptr, out);
    for (size_t i = 0; i < out.seen.touched.size(); i++) {
      EXPECT_EQ(out.seen.touched[i], 0);
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
