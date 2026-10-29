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
#include <fstream>
#include <map>
#include <set>
#include <sstream>
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

/* The truncation the image code uses in place of a cast (the cast is an x87 instruction on i686, which the determinism
 * gate refuses): it is the cast, toward zero, below 2^40, and a clamp and a zero where the cast is undefined. */
TEST(RenderImageTruncate, ItIsTheCastTowardZeroBelowTheBoundAndAClampBeyond) {
  const double fixed[] = {0.0,  -0.0, 0.25, -0.25, 0.5,  -0.5, 0.999999999999,   -0.999999999999, 1.0,
                          -1.0, 1.5,  -1.5, 4294967295.5, -4294967296.5, 1048575.75, -1048576.25, 1099511627775.5,
                          -1099511627775.5, 8589934592.0, -8589934592.25};
  for (double w : fixed) {
    EXPECT_EQ(gshcpu_render_trunc_i64(w), (int64_t)w) << w;
  }
  uint64_t state = 0x9e3779b97f4a7c15ull;
  for (int i = 0; i < 200000; i++) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    const int shift = (int)((state >> 58) % 41u); /* magnitudes from 2^-1 .. 2^40 */
    double w = (double)(state >> 11) * 0x1p-53 * std::ldexp(1.0, shift);
    if ((state >> 7) & 1u) {
      w = -w;
    }
    if (w > -0x1p40 && w < 0x1p40) {
      ASSERT_EQ(gshcpu_render_trunc_i64(w), (int64_t)w) << w;
    }
  }
  EXPECT_EQ(gshcpu_render_trunc_i64(0x1p40), (int64_t)1 << 40);
  EXPECT_EQ(gshcpu_render_trunc_i64(-0x1p40), -((int64_t)1 << 40));
  EXPECT_EQ(gshcpu_render_trunc_i64(0x1.8p40), (int64_t)1 << 40) << "between the bound and twice it: clamped, not computed";
  EXPECT_EQ(gshcpu_render_trunc_i64(-0x1.8p40), -((int64_t)1 << 40));
  EXPECT_EQ(gshcpu_render_trunc_i64(1e300), (int64_t)1 << 40);
  EXPECT_EQ(gshcpu_render_trunc_i64(-1e300), -((int64_t)1 << 40));
  EXPECT_EQ(gshcpu_render_trunc_i64(INFINITY), (int64_t)1 << 40);
  EXPECT_EQ(gshcpu_render_trunc_i64(-INFINITY), -((int64_t)1 << 40));
  EXPECT_EQ(gshcpu_render_trunc_i64(NAN), 0);
}

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

namespace {

/** The area of a convex polygon clipped to the unit square, by Sutherland-Hodgman, coordinates relative to the cell. */
double naive_clipped_area(const std::vector<double> & polygon) {
  std::vector<double> p = polygon;
  for (int edge = 0; edge < 4 && !p.empty(); edge++) {
    std::vector<double> q;
    size_t n = p.size() / 2;
    for (size_t k = 0; k < n; k++) {
      double ax = p[2 * k], ay = p[2 * k + 1];
      double bx = p[2 * ((k + 1) % n)], by = p[2 * ((k + 1) % n) + 1];
      auto side = [edge](double x, double y) { return edge == 0 ? x : edge == 1 ? 1.0 - x : edge == 2 ? y : 1.0 - y; };
      double da = side(ax, ay), db = side(bx, by);
      if (da >= 0.0) {
        q.push_back(ax);
        q.push_back(ay);
      }
      if ((da >= 0.0) != (db >= 0.0)) {
        double t = da / (da - db);
        q.push_back(ax + t * (bx - ax));
        q.push_back(ay + t * (by - ay));
      }
    }
    p.swap(q);
  }
  double a = 0.0;
  size_t n = p.size() / 2;
  for (size_t k = 0; k < n; k++) {
    a += p[2 * k] * p[2 * ((k + 1) % n) + 1] - p[2 * ((k + 1) % n)] * p[2 * k + 1];
  }
  return std::fabs(a) / 2.0;
}

/** AREA by visiting every cell of the bounding box: the second engine the row-and-run algorithm is checked against. */
void naive_area(GSHCPU_Image & im, const double origin[2], const double ea[2], const double eb[2], double out[4]) {
  const double px[4] = {origin[0], origin[0] + ea[0], origin[0] + ea[0] + eb[0], origin[0] + eb[0]};
  const double py[4] = {origin[1], origin[1] + ea[1], origin[1] + ea[1] + eb[1], origin[1] + eb[1]};
  double minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
  for (int k = 1; k < 4; k++) {
    minx = std::fmin(minx, px[k]);
    maxx = std::fmax(maxx, px[k]);
    miny = std::fmin(miny, py[k]);
    maxy = std::fmax(maxy, py[k]);
  }
  double sum[4] = {0, 0, 0, 0};
  double comp[4] = {0, 0, 0, 0};
  for (long long cj = (long long)std::floor(miny); cj < (long long)std::ceil(maxy); cj++) {
    for (long long ci = (long long)std::floor(minx); ci < (long long)std::ceil(maxx); ci++) {
      std::vector<double> poly;
      for (int k = 0; k < 4; k++) {
        poly.push_back(px[k] - (double)ci);
        poly.push_back(py[k] - (double)cj);
      }
      double a = naive_clipped_area(poly);
      if (a <= 0.0) {
        continue;
      }
      double t[4];
      gshcpu_render_image_tap(&im, ci, cj, t);
      for (int k = 0; k < 4; k++) {
        double term = a * t[k] - comp[k];
        double next = sum[k] + term;
        comp[k] = (next - sum[k]) - term;
        sum[k] = next;
      }
    }
  }
  const double total = std::fabs(ea[0] * eb[1] - ea[1] * eb[0]);
  for (int k = 0; k < 4; k++) {
    out[k] = sum[k] / total;
  }
  if (im.convert_out) {
    gshcpu_render_convert_premultiplied(&im.out, out, out);
  }
}

} // namespace

// IC7, IC13: AREA against the oracle's parallelograms: the exact mean, within the stated tolerance (the oracle's own bound where it
// cannot be exact, 1e-13 where it is dyadic: a sum of many cells may differ in the last digits).
TEST(RenderImageVectors, EveryAreaAgreesWithTheOracleWithinTheStatedTolerance) {
  Store s;
  std::vector<const GSHSCENE_Resource *> images;
  for (unsigned i = 0; i < imagevec::kImageCount; i++) {
    images.push_back(vector_image(s.store, imagevec::kImages[i]));
    ASSERT_NE(images.back(), nullptr) << imagevec::kImages[i].name;
    s.held.push_back(images.back());
  }
  size_t exact = 0;
  size_t exact_equal = 0;
  double worst = 0.0;
  for (unsigned i = 0; i < imagevec::kAreaCount; i++) {
    const imagevec::Area & a = imagevec::kAreas[i];
    GSHSCENE_Color_Space layer = space_from(a.layer);
    GSHSCENE_Color_Space filter_space = space_from(a.filter_space);
    if (is_zero(layer)) {
      layer = srgb();
    }
    GSHCPU_Image im;
    const uint32_t window[4] = {(uint32_t)a.window[0], (uint32_t)a.window[1], (uint32_t)a.window[2], (uint32_t)a.window[3]};
    ASSERT_EQ(gshcpu_render_image_open(&im, images[(size_t)a.image], window, a.extend_x, a.extend_y,
                  GSHSCENE_IMAGE_FILTER_AREA, &filter_space, &layer, &layer),
        GSHCPU_OK)
        << a.name << " (vector " << i << ")";
    double got[4];
    gshcpu_render_image_region(&im, a.origin, a.edge_a, a.edge_b, got);
    const double tolerance = a.tolerance == 0.0 ? 1e-13 : a.tolerance;
    EXPECT_TRUE(close(got, a.expected, tolerance)) << a.name << " (vector " << i << ")";
    const double cells = (std::fabs(a.edge_a[0]) + std::fabs(a.edge_b[0]) + 2.0) * (std::fabs(a.edge_a[1]) + std::fabs(a.edge_b[1]) + 2.0);
    if (cells <= 100000.0) {
      double second[4];
      naive_area(im, a.origin, a.edge_a, a.edge_b, second);
      EXPECT_TRUE(close(second, a.expected, tolerance)) << a.name << " (vector " << i << "): the every-cell engine";
    }
    double scale = 1.0;
    for (int k = 0; k < 4; k++) {
      scale = std::fmax(scale, std::fabs(a.expected[k]));
    }
    for (int k = 0; k < 4; k++) {
      worst = std::fmax(worst, std::fabs(got[k] - a.expected[k]) / (scale * tolerance));
    }
    if (a.tolerance == 0.0) {
      exact++;
      exact_equal += got[0] == a.expected[0] && got[1] == a.expected[1] && got[2] == a.expected[2] && got[3] == a.expected[3];
    }
  }
  std::printf("area vectors: %u regions, %zu exact (%zu bit for bit); worst error %.3g of its tolerance\n", imagevec::kAreaCount, exact,
      exact_equal, worst);
  EXPECT_GT(exact, 0u);
  EXPECT_EQ(exact, exact_equal) << "a region the oracle calls exact is the sink's bit for bit";
}

namespace {

struct Lcg {
  uint64_t state;
  uint32_t next() {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(state >> 33);
  }
  double unit() { return (double)next() / 2147483648.0; }
  double between(double lo, double hi) { return lo + (hi - lo) * unit(); }
};

} // namespace

// IC7: the row-and-run algorithm is the every-cell clip, for every edge mode on both axes, windows, formats and parallelograms,
// axis-aligned or not, tiny or covering many periods of the window.
TEST(RenderImageArea, TheRowsAndRunsAgreeWithAnEveryCellClip) {
  Lcg rng{12345};
  const ih::Fmt formats[] = {ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT), ih::fmt(3, true, 32, GIMG_ALPHA_NONE),
      ih::fmt(1, false, 8, GIMG_ALPHA_NONE, true), ih::fmt(4, false, 16, GIMG_ALPHA_PREMULTIPLIED)};
  size_t compared = 0;
  size_t folded = 0;
  for (int trial = 0; trial < 3000; trial++) {
    Store s;
    const ih::Fmt & f = formats[rng.next() % 4];
    const uint32_t w = 1 + rng.next() % 7;
    const uint32_t h = 1 + rng.next() % 7;
    std::vector<uint32_t> samples((size_t)w * h * f.channels);
    for (size_t i = 0; i < samples.size(); i++) {
      if (f.bits == 32) {
        samples[i] = ih::float_bits((float)(rng.next() % 200) / 64.0f - 1.0f);
      } else {
        samples[i] = rng.next() & ((1u << f.bits) - 1u);
      }
    }
    const GSHSCENE_Resource * image = ih::intern_image(s.store, w, h, f, samples);
    ASSERT_NE(image, nullptr);
    s.held.push_back(image);
    uint32_t window[4] = {0, 0, 0, 0};
    if (rng.next() % 3 == 0 && (w > 1 || h > 1)) {
      window[0] = rng.next() % w;
      window[1] = rng.next() % h;
      window[2] = window[0] + 1 + rng.next() % (w - window[0]);
      window[3] = window[1] + 1 + rng.next() % (h - window[1]);
    }
    const uint32_t ex = rng.next() % 4;
    const uint32_t ey = rng.next() % 4;
    const GSHSCENE_Color_Space own{};
    GSHSCENE_Color_Space layer = srgb();
    GSHCPU_Image im;
    ASSERT_EQ(gshcpu_render_image_open(&im, image, window, ex, ey, GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer), GSHCPU_OK);
    double origin[2] = {rng.between(-9.0, 14.0), rng.between(-9.0, 14.0)};
    double ea[2];
    double eb[2];
    const double span = (rng.next() % 4 == 0) ? 40.0 : 6.0;
    switch (rng.next() % 3) {
      case 0: // axis-aligned
        ea[0] = rng.between(0.05, span) * (rng.next() % 2 ? 1 : -1);
        ea[1] = 0.0;
        eb[0] = 0.0;
        eb[1] = rng.between(0.05, span) * (rng.next() % 2 ? 1 : -1);
        if (rng.next() % 2) {
          std::swap(ea[0], eb[0]);
          std::swap(ea[1], eb[1]);
        }
        break;
      default: // rotated, sheared
        ea[0] = rng.between(-span, span);
        ea[1] = rng.between(-span, span);
        eb[0] = rng.between(-span, span);
        eb[1] = rng.between(-span, span);
        break;
    }
    if (std::fabs(ea[0] * eb[1] - ea[1] * eb[0]) < 0.02) {
      continue;
    }
    double got[4];
    double want[4];
    // IC9: the bound of the map whose inverse has these edges is at least the visits of any parallelogram of them.
    im.inv[0] = ea[0];
    im.inv[2] = ea[1];
    im.inv[1] = eb[0];
    im.inv[3] = eb[1];
    uint64_t bound = 0;
    ASSERT_EQ(gshcpu_render_image_area_bound(&im, &bound), 1);
    im.visits = 0;
    gshcpu_render_image_region(&im, origin, ea, eb, got);
    const uint64_t visits = im.visits;
    ASSERT_LE(visits, bound) << "trial " << trial << ": the bound is no bound; extend " << ex << "," << ey << " window "
                             << im.x0 << "," << im.y0 << "," << im.x1 << "," << im.y1 << " ea " << ea[0] << "," << ea[1] << " eb "
                             << eb[0] << "," << eb[1] << " origin " << origin[0] << "," << origin[1];
    naive_area(im, origin, ea, eb, want);
    ASSERT_TRUE(close(got, want, 1e-11)) << "trial " << trial << " extend " << ex << "," << ey << " window " << window[0] << ","
                                         << window[1] << "," << window[2] << "," << window[3];
    compared++;
    folded += visits < (uint64_t)(std::fabs(ea[0]) + std::fabs(eb[0]) + 1) * (uint64_t)(std::fabs(ea[1]) + std::fabs(eb[1]) + 1) / 2;
  }
  EXPECT_GT(compared, 2500u);
  EXPECT_GT(folded, 100u) << "the runs spared cells in a good share of the large cases";
}

// IC7, IC9: whole periods are summed by the window's row, so a 1x1 image repeated a million times is one texel's value for the
// visits of the boundary, not of the million.
TEST(RenderImageArea, WholePeriodsAreSummedNotVisited) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  const GSHSCENE_Resource * one = ih::intern_image(s.store, 1, 1, f, {51});
  ASSERT_NE(one, nullptr);
  s.held.push_back(one);
  const uint32_t window[4] = {0, 0, 0, 0};
  const GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space layer = srgb();
  GSHCPU_Image im;
  ASSERT_EQ(gshcpu_render_image_open(&im, one, window, GSHSCENE_IMAGE_EXTEND_REPEAT, GSHSCENE_IMAGE_EXTEND_REPEAT,
                GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer),
      GSHCPU_OK);
  const double origin[2] = {-300.25, 17.5};
  const double ea[2] = {1000.0, 100.0};
  const double eb[2] = {-80.0, 1000.0}; // about a million cells
  double got[4];
  im.visits = 0;
  gshcpu_render_image_region(&im, origin, ea, eb, got);
  EXPECT_NEAR(got[0], 51.0 / 255.0, 1e-13);
  EXPECT_NEAR(got[3], 1.0, 1e-13);
  EXPECT_LT(im.visits, 40000u) << "boundary cells and the rows, not the million cells";
  // The same through a window and a reflect: a 5x3 image's reflected double period.
  std::vector<uint32_t> samples;
  for (uint32_t i = 0; i < 15; i++) {
    samples.push_back((i * 37u + 11u) % 256u);
  }
  const GSHSCENE_Resource * five = ih::intern_image(s.store, 5, 3, f, samples);
  ASSERT_NE(five, nullptr);
  s.held.push_back(five);
  ASSERT_EQ(gshcpu_render_image_open(&im, five, window, GSHSCENE_IMAGE_EXTEND_REFLECT, GSHSCENE_IMAGE_EXTEND_REPEAT,
                GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer),
      GSHCPU_OK);
  const double ob[2] = {-33.5, -20.25};
  const double ea2[2] = {61.0, 9.0};
  const double eb2[2] = {-7.0, 47.0};
  double want[4];
  gshcpu_render_image_region(&im, ob, ea2, eb2, got);
  naive_area(im, ob, ea2, eb2, want);
  EXPECT_TRUE(close(got, want, 1e-12));
}

namespace {

struct Limited {
  uint32_t refused = 0;
  uint32_t reason = 0;
  GSHSCENE_Result result = GSHSCENE_OK;
  std::string bytes;
};

/** Render @p list with @p limits on a w by h raster, through @p damage and with @p flags. */
Limited render_limited(const GSHCPU_Limits & limits, const GSHSCENE_List * list, uint32_t w, uint32_t h,
    const GSHSCENE_Rect * damage, uint32_t flags = 0) {
  Limited out;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &sink), GSHCPU_OK);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
  GSHSCENE_Target t = target_of(r.raster);
  t.damage = damage;
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  std::vector<unsigned char> workspace;
  if (flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    size_t need = 0;
    EXPECT_EQ(gshscene_sink_workspace_bytes(sink, &req, &need), GSHSCENE_OK);
    workspace.resize(need + 64);
    req.workspace = workspace.data();
    req.workspace_bytes = need;
  }
  out.result = gshscene_sink_render(sink, &req);
  for (const GSHSCENE_Report_Entry & e : report.entries()) {
    if (e.entry_class == GSHSCENE_ENTRY_REFUSED) {
      out.refused++;
      out.reason = (uint32_t)e.reason;
    }
  }
  out.bytes = r.bytes();
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  return out;
}

} // namespace

// IC9: each AREA limit passes exactly at its bound and refuses one past it; the bound is a function of the map and the window
// alone, so the refusal is the same with a damage and with the caller's memory, and nothing of a refused item is drawn.
TEST(RenderImageLimits, EachBoundPassesExactlyAndOneMoreIsRefused) {
  Store s;
  const ih::Fmt f = ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT);
  std::vector<uint32_t> samples;
  for (uint32_t i = 0; i < 16u * 16u * 4u; i++) {
    samples.push_back((i * 29u + 3u) % 256u);
  }
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 16, 16, f, samples);
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  ip.map = matrix(0.125, 0.0, 0.0, 0.125, 0.0, 0.0);
  ip.magnify = GSHSCENE_IMAGE_FILTER_AREA;
  ip.minify = GSHSCENE_IMAGE_FILTER_AREA;
  const GSHSCENE_Resource * paint = nullptr;
  ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
  s.held.push_back(paint);
  const uint32_t W = 40;
  const uint32_t H = 30;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint p = image_paint_of(paint, 1.0);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, W, H), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  // The bound, from the same primitives the sink uses.
  GSHCPU_Image im;
  const uint32_t window[4] = {0, 0, 0, 0};
  const GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space layer = srgb();
  ASSERT_EQ(gshcpu_render_image_open(&im, image, window, 0, 0, GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer), GSHCPU_OK);
  ASSERT_EQ(gshcpu_render_image_place(&im, &ip.map), 1);
  uint64_t pixel = 0;
  ASSERT_EQ(gshcpu_render_image_area_bound(&im, &pixel), 1);
  EXPECT_GT(pixel, 64u) << "an 8 by 8 preimage is at least its cells";
  const uint64_t pixels = (uint64_t)W * H;
  const GSHSCENE_Rect damage = {5, 4, 20, 18};
  for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_CALLER_MEMORY}) {
    for (const GSHSCENE_Rect * d : {(const GSHSCENE_Rect *)nullptr, &damage}) {
      SCOPED_TRACE(std::string(flags ? "caller memory" : "allocated") + (d ? ", damaged" : ", whole"));
      GSHCPU_Limits limits;
      gshcpu_limits_default(&limits);
      limits.max_area_pixel_work = pixel;
      limits.max_image_work = pixels * pixel;
      Limited ok = render_limited(limits, l, W, H, d, flags);
      EXPECT_EQ(ok.refused, 0u) << "exactly the bounds passes";
      limits.max_area_pixel_work = pixel - 1;
      Limited per_pixel = render_limited(limits, l, W, H, d, flags);
      EXPECT_EQ(per_pixel.refused, 1u) << "one past the per-pixel bound";
      EXPECT_EQ(per_pixel.reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
      limits.max_area_pixel_work = pixel;
      limits.max_image_work = pixels * pixel - 1;
      Limited per_item = render_limited(limits, l, W, H, d, flags);
      EXPECT_EQ(per_item.refused, 1u) << "one past the item's bound";
      EXPECT_EQ(per_item.reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
      EXPECT_EQ(per_item.bytes, per_pixel.bytes) << "a refused item draws nothing, whichever limit refused it";
      limits.max_area_pixel_work = UINT64_MAX;
      limits.max_image_work = UINT64_MAX;
      Limited uncapped = render_limited(limits, l, W, H, d, flags);
      EXPECT_EQ(uncapped.refused, 0u);
      EXPECT_EQ(uncapped.bytes, ok.bytes);
    }
  }
  // Strict stops at the refusal.
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_area_pixel_work = pixel - 1;
  EXPECT_EQ(render_limited(limits, l, W, H, nullptr, GSHSCENE_RENDER_STRICT).result, GSHSCENE_ERR_REFUSED);
}

// IC10: each tier is the general tier's pixels, and is taken: every fill of a corpus of maps, filters, edge modes and formats
// rendered with the image tiers on and off is the same bytes, and the counters show each tier taken (and none when forced off).
TEST(RenderImageTiers, EachTierIsBitEqualToTheGeneralTierAndIsTaken) {
  struct Shape {
    const char * name;
    GSHGEOM_Map map;
  };
  GSHGEOM_Map rot30;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &rot30), GSHGEOM_OK);
  GSHGEOM_Map rot90;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &rot90), GSHGEOM_OK);
  const Shape shapes[] = {
      {"integer translation", matrix(1, 0, 0, 1, 3, -2)},
      {"half pixel", matrix(1, 0, 0, 1, 0.5, 0.5)},
      {"scale 3", matrix(3, 0, 0, 3, 1.25, 0.5)},
      {"scale 0.4", matrix(0.4, 0, 0, 0.4, 2, 1)},
      {"anisotropic", matrix(5, 0, 0, 0.3, 0, 0)},
      {"mirror", matrix(-2, 0, 0, 2, 30, 0)},
      {"quarter turn", rot90},
      {"rotated 30 degrees", rot30},
      {"sheared", matrix(2, 0.7, 0, 2, 0, 0)},
      {"sheared the other way", matrix(2, 0, 0.7, 2, 0, 0)},
      {"unit shear at an integer translation", matrix(1, 0.5, 0, 1, 3, 2)},
      {"unit shear the other way", matrix(1, 0, 0.5, 1, 3, 2)},
      {"integer in x, a fraction in y", matrix(1, 0, 0, 1, 3, 0.5)},
  };
  const ih::Fmt formats[] = {ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT), ih::fmt(1, false, 8, GIMG_ALPHA_NONE),
      ih::fmt(3, true, 32, GIMG_ALPHA_NONE), ih::fmt(1, false, 8, GIMG_ALPHA_NONE, true)};
  GSHCPU_Render_Counters on_counts{};
  GSHCPU_Render_Counters off_counts{};
  size_t renders = 0;
  for (const ih::Fmt & f : formats) {
    for (const Shape & shape : shapes) {
      for (uint32_t filter = 0; filter < GSHSCENE_IMAGE_FILTER_COUNT; filter++) {
        for (uint32_t extend : {0u, 1u, 2u, 3u}) {
          Store s;
          const uint32_t w = 6;
          const uint32_t h = 5;
          std::vector<uint32_t> samples((size_t)w * h * f.channels);
          for (size_t i = 0; i < samples.size(); i++) {
            samples[i] = f.bits == 32 ? ih::float_bits((float)((i * 13) % 31) / 30.0f) : (uint32_t)((i * 47 + 5) % 256);
          }
          GSHSCENE_Color_Space p3{};
          p3.primaries = GCOL_PRIMARIES_DISPLAY_P3;
          p3.transfer = GCOL_TRANSFER_SRGB;
          const GSHSCENE_Resource * image = ih::intern_image(s.store, w, h, f, samples, f.alpha_only ? GSHSCENE_Color_Space{} : p3);
          ASSERT_NE(image, nullptr);
          s.held.push_back(image);
          GSHSCENE_Paint tint{};
          tint.kind = GSHSCENE_PAINT_SOLID;
          tint.rgba[0] = 0.9;
          tint.rgba[1] = 0.2;
          tint.rgba[2] = 0.4;
          tint.rgba[3] = 0.8;
          GSHSCENE_Image_Paint ip;
          gshscene_image_paint_init(&ip);
          ip.image = image;
          ip.map = shape.map;
          ip.extend_x = extend;
          ip.extend_y = (extend + 2) % 4;
          ip.magnify = filter;
          ip.minify = filter;
          if (f.alpha_only) {
            ip.tint = &tint;
          } else if (filter % 2) {
            ip.filter_space = linear_srgb();
          }
          const GSHSCENE_Resource * paint = nullptr;
          ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
          s.held.push_back(paint);
          GSHSCENE_List * l = s.open();
          GSHSCENE_Paint p = image_paint_of(paint, 0.9);
          ASSERT_EQ(gshscene_list_fill(l, s.rect(-100, -100, 100, 100), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
          ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
          std::string bytes[2];
          for (int mode = 0; mode < 2; mode++) {
            GSHCPU_Render_Counters counters{};
            GSHSCENE_Sink * sink = nullptr;
            const unsigned tiers = mode == 0 ? GSHCPU_RENDER_TIERS_ALL
                                             : GSHCPU_RENDER_TIERS_ALL & ~(GSHCPU_RENDER_TIER_IMAGE_COPY |
                                                   GSHCPU_RENDER_TIER_IMAGE_SEPARABLE | GSHCPU_RENDER_TIER_IMAGE_TABLE);
            ASSERT_EQ(gshcpu_render_sink_create_tiered(nullptr, nullptr, nullptr, tiers, &counters, nullptr, nullptr, &sink), GSHCPU_OK);
            RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, 36, 28));
            GSHSCENE_Target t = target_of(r.raster);
            ReportRef report;
            GSHSCENE_Render_Request req = request_of(l, &t, report.report);
            ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
            ASSERT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
            bytes[mode] = r.bytes();
            GSHCPU_Render_Counters & into = mode == 0 ? on_counts : off_counts;
            into.images += counters.images;
            into.image_copy += counters.image_copy;
            into.image_separable += counters.image_separable;
            into.image_table += counters.image_table;
            if (filter == GSHSCENE_IMAGE_FILTER_AREA) {
              EXPECT_EQ(counters.image_separable, 0u) << "AREA has no separable tier: " << shape.name;
            }
          }
          renders++;
          ASSERT_EQ(bytes[0], bytes[1]) << shape.name << " filter " << filter << " extend " << extend << " bits " << f.bits;
        }
      }
    }
  }
  EXPECT_GT(renders, 500u);
  EXPECT_GT(on_counts.image_copy, 0u) << "FT-1 is taken";
  EXPECT_GT(on_counts.image_separable, 0u) << "FT-2 is taken";
  EXPECT_GT(on_counts.image_table, 0u) << "FT-3 is taken";
  EXPECT_EQ(off_counts.image_copy + off_counts.image_separable + off_counts.image_table, 0u) << "forced off, none is";
  EXPECT_EQ(on_counts.images, off_counts.images);
  std::printf("image tiers: %zu renders, %llu fills; copy %llu, separable %llu, table %llu\n", renders,
      (unsigned long long)on_counts.images, (unsigned long long)on_counts.image_copy, (unsigned long long)on_counts.image_separable,
      (unsigned long long)on_counts.image_table);
}


// IC5: a window that starts at the origin is a window, not the whole image (all four zero is the whole): the taps at its edges are
// the window's.
TEST(RenderImageEdges, AWindowAtTheOriginIsNotTheWholeImage) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  std::vector<uint32_t> samples;
  for (uint32_t i = 0; i < 20; i++) {
    samples.push_back(10 + 11 * i);
  }
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 5, 4, f, samples);
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  const GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space layer = srgb();
  const uint32_t window[4] = {0, 0, 3, 2};
  struct Case {
    uint32_t ex, ey;
    double u, v;
    uint32_t texel_x, texel_y;
  } cases[] = {
      {GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD, 4.5, 3.5, 2, 1},
      {GSHSCENE_IMAGE_EXTEND_REPEAT, GSHSCENE_IMAGE_EXTEND_REPEAT, 3.5, 2.5, 0, 0},
      {GSHSCENE_IMAGE_EXTEND_REFLECT, GSHSCENE_IMAGE_EXTEND_REFLECT, 3.5, 2.5, 2, 1},
  };
  for (const Case & c : cases) {
    GSHCPU_Image im;
    ASSERT_EQ(gshcpu_render_image_open(&im, image, window, c.ex, c.ey, GSHSCENE_IMAGE_FILTER_NEAREST, &own, &layer, &layer), GSHCPU_OK);
    double got[4];
    gshcpu_render_image_point(&im, c.u, c.v, got);
    double want[4];
    ASSERT_EQ(gshscene_image_decode(image, c.texel_x, c.texel_y, want), GSHSCENE_OK);
    EXPECT_TRUE(close(got, want, 0.0)) << "extend " << c.ex << "," << c.ey;
  }
}

// IC7: a repeating axis sums its whole periods by the window's row, and the sums of rows are kept for the pixels after: a table of 32
// rows holds none of a window 70 rows tall twice over, so a stale row would be served for another.
TEST(RenderImageArea, TheRowSumsAreForTheRowAndNotItsSlot) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  std::vector<uint32_t> samples;
  for (uint32_t i = 0; i < 3u * 70u; i++) {
    samples.push_back((i * 37u + 5u) % 256u);
  }
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 3, 70, f, samples);
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  const uint32_t window[4] = {0, 0, 0, 0};
  const GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space layer = srgb();
  for (uint32_t ey : {(uint32_t)GSHSCENE_IMAGE_EXTEND_PAD, (uint32_t)GSHSCENE_IMAGE_EXTEND_REPEAT}) {
    GSHCPU_Image im;
    ASSERT_EQ(gshcpu_render_image_open(&im, image, window, GSHSCENE_IMAGE_EXTEND_REPEAT, ey, GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer),
        GSHCPU_OK);
    const double origin[2] = {0.5, 0.25};
    const double shapes[2][4] = {{60.0, 0.0, 0.0, 140.0}, {60.0, 5.0, -3.0, 138.0}};
    for (const auto & e : shapes) {
      const double ea[2] = {e[0], e[1]};
      const double eb[2] = {e[2], e[3]};
      double got[4];
      double want[4];
      gshcpu_render_image_region(&im, origin, ea, eb, got);
      naive_area(im, origin, ea, eb, want);
      EXPECT_TRUE(close(got, want, 1e-12)) << "extend y " << ey << " edges " << e[1] << "," << e[2];
    }
  }
}

// IC9: a preimage wider than the sampling domain has no bound; one as wide as the domain has.
TEST(RenderImageLimits, APreimageWiderThanTheDomainHasNoBound) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 2, 2, f, {1, 2, 3, 4});
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  const uint32_t window[4] = {0, 0, 0, 0};
  const GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space layer = srgb();
  GSHCPU_Image im;
  ASSERT_EQ(gshcpu_render_image_open(&im, image, window, 0, 0, GSHSCENE_IMAGE_FILTER_AREA, &own, &layer, &layer), GSHCPU_OK);
  uint64_t bound = 0;
  im.inv[0] = im.inv[3] = 0x1p32; // 2^32 + 0 on each axis
  im.inv[1] = im.inv[2] = 0.0;
  EXPECT_EQ(gshcpu_render_image_area_bound(&im, &bound), 1);
  im.inv[0] = im.inv[3] = 0x1p33;
  EXPECT_EQ(gshcpu_render_image_area_bound(&im, &bound), 1) << "as wide as the domain";
  im.inv[0] = 0x1p33;
  im.inv[3] = 0x1.0000000000001p33;
  EXPECT_EQ(gshcpu_render_image_area_bound(&im, &bound), 0) << "one step past it, in y";
  im.inv[0] = 0x1.0000000000001p33;
  im.inv[3] = 0x1p33;
  EXPECT_EQ(gshcpu_render_image_area_bound(&im, &bound), 0) << "one step past it, in x";
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

// IC8: a tint "in the target's space" is the target's space whatever space the group it is drawn in works in: the same stencil
// with the target's space written out (sRGB here) is the same picture.
TEST(RenderImageSink, ATintInTheTargetsSpaceIsTheTargetsInAGroupOfAnotherSpace) {
  decltype(Render().seen.root) roots[2];
  for (int written = 0; written < 2; written++) {
    Store s;
    const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE, true);
    const GSHSCENE_Resource * image = ih::intern_image(s.store, 2, 1, f, {255, 128});
    ASSERT_NE(image, nullptr);
    s.held.push_back(image);
    GSHSCENE_Paint tint{};
    tint.kind = GSHSCENE_PAINT_SOLID;
    tint.rgba[0] = 0.8;
    tint.rgba[1] = 0.4;
    tint.rgba[2] = 0.1;
    tint.rgba[3] = 1.0;
    if (written) {
      tint.space = srgb();
    }
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
    GSHSCENE_Group g = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
    g.blend_space = linear_srgb();
    GSHSCENE_Paint p = image_paint_of(paint, 1.0);
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 4, 2), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Render out;
    render_observed(l, 4, 2, nullptr, out);
    ASSERT_EQ(out.refused, 0u);
    roots[written] = out.seen.root;
  }
  ASSERT_FALSE(roots[0].empty());
  EXPECT_EQ(roots[0], roots[1]);
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

// IC3: the target's map is the chain's last: a target that scales and moves composes after the item and the paint.
TEST(RenderImageSink, TheTargetsMapIsTheChainsLast) {
  Store s;
  const ih::Fmt f = ih::fmt(4, false, 8, GIMG_ALPHA_STRAIGHT);
  std::vector<uint32_t> samples;
  for (uint32_t i = 0; i < 5u * 4u * 4u; i++) {
    samples.push_back((i * 53u + 17u) % 256u);
  }
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 5, 4, f, samples);
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  ip.map = matrix(4.0, 0.0, 0.0, 4.0, 2.0, 1.0);
  ip.magnify = GSHSCENE_IMAGE_FILTER_LINEAR;
  const GSHSCENE_Resource * paint = nullptr;
  ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
  s.held.push_back(paint);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint p = image_paint_of(paint, 1.0);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(-100, -100, 100, 100), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Render out;
  GSHGEOM_Map tmap = matrix(2.0, 0.0, 0.0, 2.0, 3.0, 1.0); // T G p = 2 (4 p + (2, 1)) + (3, 1) = 8 p + (7, 3)
  render_observed(l, 24, 20, &tmap, out);
  ASSERT_EQ(out.refused, 0u);
  const uint32_t window[4] = {0, 0, 0, 0};
  const GSHSCENE_Color_Space own{};
  GSHSCENE_Color_Space target = srgb();
  GSHCPU_Image im;
  ASSERT_EQ(gshcpu_render_image_open(&im, image, window, 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, &own, &target, &target), GSHCPU_OK);
  Backdrop backdrop(24, 20);
  for (int64_t y = 0; y < 20; y++) {
    for (int64_t x = 0; x < 24; x++) {
      double src[4];
      gshcpu_render_image_point(&im, ((double)x + 0.5 - 7.0) / 8.0, ((double)y + 0.5 - 3.0) / 8.0, src);
      double d[4];
      backdrop.at(x, y, d);
      gshcpu_render_apply(d, src, 1.0, GSHSCENE_COMPOSITE_SRC_OVER);
      for (int k = 0; k < 4; k++) {
        ASSERT_EQ(out.seen.root[4 * ((size_t)y * 24 + (size_t)x) + (size_t)k], d[k]) << x << "," << y << " component " << k;
      }
    }
  }
}

// IC9: either axis past the sampling domain refuses the fill: a map that is fine in x and far out in y, and the other way.
TEST(RenderImageLimits, EitherAxisPastTheDomainRefusesTheFill) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 2, 2, f, {10, 20, 30, 40});
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  const GSHGEOM_Map maps[2] = {matrix(1.0, 0.0, 0.0, 0x1p-30, 0.0, 0.0), matrix(0x1p-30, 0.0, 0.0, 1.0, 0.0, 0.0)};
  for (const GSHGEOM_Map & m : maps) {
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image;
    ip.map = m;
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
  }
}

// IC9: the domain is met over what the whole render draws, so a damage that lies inside it refuses what the whole render refuses.
TEST(RenderImageLimits, ADamagedRenderRefusesWhatTheWholeRefusesPastTheDomain) {
  Store s;
  const ih::Fmt f = ih::fmt(1, false, 8, GIMG_ALPHA_NONE);
  const GSHSCENE_Resource * image = ih::intern_image(s.store, 2, 2, f, {10, 20, 30, 40});
  ASSERT_NE(image, nullptr);
  s.held.push_back(image);
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  ip.map = matrix(0x1p-30, 0, 0, 0x1p-30, 0, 0);
  const GSHSCENE_Resource * paint = nullptr;
  ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
  s.held.push_back(paint);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint p = image_paint_of(paint, 1.0);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 24, 20), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  const GSHSCENE_Rect corner = {0, 0, 1, 1}; // a pixel whose own preimage is far inside the domain
  Limited whole = render_limited(limits, l, 24, 20, nullptr);
  Limited damaged = render_limited(limits, l, 24, 20, &corner);
  EXPECT_EQ(whole.refused, 1u);
  EXPECT_EQ(damaged.refused, 1u);
}

// IC9, IC10: a list that holds an image is composited in double layers on every target, under the fast coverages too: each image fixture
// is its committed fast golden on the four formats.
TEST(RenderImageFast, EveryImageFixtureUnderTheFastCoveragesIsItsGolden) {
  std::map<std::pair<std::string, std::string>, uint64_t> want;
  std::ifstream in("tests/corpus/render-fast.hash");
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string tag, fixture, format, hex;
    if (words >> tag >> fixture >> format >> hex && tag == "render") {
      want[{fixture, format}] = std::stoull(hex, nullptr, 16);
    }
  }
  GSHCPU_Limits fast;
  gshcpu_limits_default(&fast);
  fast.fill_coverage = GSHCPU_FILL_COVERAGE_FAST;
  fast.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
  size_t checked = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::string(RF_FIXTURES[i].name).rfind("image-", 0) != 0) {
      continue;
    }
    for (GSHCPU_Format format : RF_FORMATS) {
      uint64_t h = 0;
      ASSERT_EQ(rf_render_with(i, format, &fast, &h, nullptr), GSHSCENE_OK) << RF_FIXTURES[i].name;
      auto it = want.find({RF_FIXTURES[i].name, gshcpu_format_string(format)});
      ASSERT_NE(it, want.end()) << RF_FIXTURES[i].name;
      EXPECT_EQ(h, it->second) << RF_FIXTURES[i].name << " on " << gshcpu_format_string(format);
      checked++;
    }
  }
  EXPECT_EQ(checked, 24u);
}

// IC1: a stroke painted with an image is not a solid stroke: under the fast stroke coverage on an 8-bit premultiplied target, where a list
// of solid strokes alone is composed in integers straight into the target, it takes the image's colors and the exact coverage's bytes.
TEST(RenderImageSink, AStrokePaintedWithAnImageIsNotComposedAsASolidOne) {
  std::string bytes[2];
  for (int fast = 0; fast < 2; fast++) {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * l = rf_open(&c);
    const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
    const GSHSCENE_Resource * img = rf_image(&c, 6, 2, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
    static const double line[4] = {2.0, 8.0, 40.0, 8.0};
    GSHGEOM_Map at = rf_move(0.0, 0.0);
    rf_stroke(&c, l, rf_polyline(&c, line, 2, 0), rf_pen(&c, 6.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &at,
        rf_image_paint(&c, img, rf_place(&c, 6.5, 3.0, 0, 5), 1, 0, GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_NEAREST, none, NULL, 1.0));
    rf_close(&c, l);
    ASSERT_EQ(c.failed, 0);
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.stroke_coverage = fast ? GSHCPU_STROKE_COVERAGE_FAST : GSHCPU_STROKE_COVERAGE_EXACT;
    SinkRef sink(&limits);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target t = target_of(r.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    bytes[fast] = r.bytes();
    std::set<std::string> colors;
    for (uint32_t x = 4; x < 38; x++) {
      colors.insert(r.pixel(x, 8));
    }
    EXPECT_GT(colors.size(), 3u) << "the stroke shows the image's texels, not one color";
    rf_release(&c);
  }
  EXPECT_EQ(bytes[0], bytes[1]);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
