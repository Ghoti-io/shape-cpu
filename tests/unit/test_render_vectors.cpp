/**
 * @file
 *
 * The gate of S11 (render.h; D7, D8): shape-cpu's arithmetic against the
 * rational oracles.
 *
 * - shape-scene's `tests/composite_vectors.h` (story 12, its oracle), twice:
 *   through the compositing primitives with each case's coverage as given, and
 *   through whole renders of a one-pixel target wherever the case's coverages
 *   are 0 or 1 (a full or an empty square), read before the write and after it.
 * - this library's `tests/render_vectors.h` (tools/gen-render-vectors.py): whole
 *   renders on every format, with 8-bit coverages and the D8 weights, the
 *   backdrop each format holds and the one rounding at the write.
 *
 * Before the write a value is within 1e-12 of the oracle, and equal to it where
 * the case is exact; after it, the stored value is the oracle's rounded the same
 * way, or the other side when the oracle's is within 1e-9 of the boundary.  The
 * worst error seen is printed, for the story's record.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/composite_vectors.h>

#include "../../src/render/render_internal.h"
#include "render_vectors.h"
#include "vector_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace vh;

namespace {


/* --- through the primitives, the coverage as the case gives it -------------------------------------- */

struct Frame {
  double layer[4];
  GSHSCENE_Color_Space space;
  std::vector<double> clips;
  double opacity;
  uint32_t composite;
  double outer;
};

double product(const std::vector<double> & v) {
  double p = 1.0;
  for (double x : v) {
    p *= x;
  }
  return p;
}

/** N6 to N10 at one pixel, with the library's primitives, the clips multiplied in double. */
void evaluate(const composite_vectors::VCase & c, double out[4]) {
  using namespace composite_vectors;
  GSHSCENE_Color_Space target = space_of(c.target.primaries, c.target.transfer, c.target.gamma);
  std::vector<Frame> stack(1);
  std::memcpy(stack[0].layer, c.backdrop, sizeof stack[0].layer);
  stack[0].space = target;
  for (int i = 0; i < c.item_count; i++) {
    const VItem & it = c.items[i];
    Frame & top = stack.back();
    if (it.kind == 1) {
      double rgb[3];
      double alpha;
      GSHSCENE_Color_Space space;
      if (it.paint == 2) {
        std::memcpy(rgb, c.fg, sizeof rgb);
        alpha = c.fg[3] * it.rgba[3];
        space = space_of(c.fg_space.primaries, c.fg_space.transfer, c.fg_space.gamma);
      } else if (it.paint == 3) {
        // N14: the entry's color, its alpha scaled; a missing entry is refused and draws nothing.
        if (it.entry >= c.palette_count) {
          continue;
        }
        const VColor & e = c.palette[it.entry];
        std::memcpy(rgb, e.rgba, sizeof rgb);
        alpha = e.rgba[3] * it.rgba[3];
        space = space_of(e.space.primaries, e.space.transfer, e.space.gamma);
      } else {
        std::memcpy(rgb, it.rgba, sizeof rgb);
        alpha = it.rgba[3];
        space = space_of(it.space.primaries, it.space.transfer, it.space.gamma);
      }
      if (gshscene_color_space_is_target(&space)) {
        space = target;
      }
      GSHCPU_Convert cv;
      gshcpu_render_convert_prepare(&cv, &space, &top.space);
      gshcpu_render_convert_straight(&cv, rgb, rgb);
      double s[4] = {rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha, alpha};
      gshcpu_render_apply(top.layer, s, it.coverage * product(top.clips), GSHSCENE_COMPOSITE_SRC_OVER);
    } else if (it.kind == 2) {
      top.clips.push_back(it.coverage);
    } else if (it.kind == 3) {
      top.clips.pop_back();
    } else if (it.kind == 4) {
      Frame f{};
      GSHSCENE_Color_Space blend = space_of(it.blend.primaries, it.blend.transfer, it.blend.gamma);
      f.space = gshscene_color_space_is_target(&blend) ? top.space : blend;
      f.opacity = it.opacity;
      f.composite = (uint32_t)it.composite;
      f.outer = product(top.clips);
      stack.push_back(f);
    } else if (it.kind == 5) {
      Frame done = stack.back();
      stack.pop_back();
      Frame & parent = stack.back();
      GSHCPU_Convert cv;
      gshcpu_render_convert_prepare(&cv, &done.space, &parent.space);
      double s[4];
      gshcpu_render_convert_premultiplied(&cv, done.layer, s);
      for (double & v : s) {
        v = done.opacity * v;
      }
      gshcpu_render_apply(parent.layer, s, done.outer, done.composite);
    }
  }
  std::memcpy(out, stack[0].layer, 4 * sizeof(double));
}

TEST(RenderVectors, ThePrimitivesMeetTheCompositeVectors) {
  using namespace composite_vectors;
  Worst worst;
  int exact = 0;
  for (const VCase & c : kCases) {
    double got[4];
    evaluate(c, got);
    expect_pre(got, c.want, c.exact, c.name, worst);
    exact += c.exact ? 1 : 0;
  }
  EXPECT_EQ(exact, kExactCaseCount);
  std::printf("composite_vectors through the primitives: %zu cases, %d exact; worst error %.3g (%s)\n",
      sizeof kCases / sizeof kCases[0], exact, worst.error, worst.where.c_str());
}

/* --- the blend modes' mixing function (color-paint story P3, B7 to B9) -------------------------------------------- */

TEST(RenderVectors, TheMixingFunctionMeetsTheTable) {
  // shape-scene's kMix: B(Cb, Cs) after B9's saturation at every branch, threshold, out-of-range value, pole and corner.
  // Bit for bit where every step is exact in doubles (STOP-2), else within the tolerance; the worst per family.
  using namespace composite_vectors;
  Worst polynomial, quotient, root, non_separable;
  int exact = 0;
  for (int n = 0; n < kMixCount; n++) {
    const VMix & m = kMix[n];
    double got[3];
    gshcpu_render_mix((uint32_t)m.mode, m.cb, m.cs, got);
    double got4[4] = {got[0], got[1], got[2], 0.0}, want4[4] = {m.want[0], m.want[1], m.want[2], 0.0};
    Worst & w = m.mode >= GSHSCENE_COMPOSITE_HUE ? non_separable
        : m.mode == GSHSCENE_COMPOSITE_SOFT_LIGHT ? root
        : (m.mode == GSHSCENE_COMPOSITE_COLOR_DODGE || m.mode == GSHSCENE_COMPOSITE_COLOR_BURN) ? quotient
                                                                                                 : polynomial;
    expect_pre(got4, want4, m.exact, "kMix[" + std::to_string(n) + "] " + gshscene_composite_string((GSHSCENE_Composite)m.mode),
        w);
    for (double v : got) {
      EXPECT_TRUE(std::isfinite(v) && std::fabs(v) <= 0x1p64) << n;
    }
    exact += m.exact ? 1 : 0;
  }
  EXPECT_EQ(exact, kMixExactCount);
  std::printf("kMix through shape-cpu: %d samples, %d exact; worst relative error: polynomial and min/max %.3g, dodge "
              "and burn %.3g, soft-light %.3g, non-separable %.3g\n", kMixCount, exact, polynomial.error, quotient.error,
      root.error, non_separable.error);
}

TEST(RenderVectors, ClipColorARoundingOffTheRayMeetsTheTable) {
  // shape-scene's kNearRay (B10): a triple a rounding off C = L mixed with itself, where the literal Lum(C) - n (or
  // x - Lum(C)) in doubles is 0 and the step 0 / 0 (a NaN this library's fuzzer found); within the tolerance.
  using namespace composite_vectors;
  ASSERT_GE(kNearRayCount, 20);
  Worst w;
  for (int n = 0; n < kNearRayCount; n++) {
    const VNearRay & r = kNearRay[n];
    double got[3];
    gshcpu_render_mix((uint32_t)r.mode, r.c, r.c, got);
    for (double v : got) {
      ASSERT_TRUE(std::isfinite(v)) << "kNearRay[" << n << "]";
    }
    double got4[4] = {got[0], got[1], got[2], 0.0}, want4[4] = {r.want[0], r.want[1], r.want[2], 0.0};
    expect_pre(got4, want4, false, "kNearRay[" + std::to_string(n) + "] " + gshscene_composite_string((GSHSCENE_Composite)r.mode), w);
  }
  std::printf("kNearRay through shape-cpu: %d samples; worst relative error %.3g\n", kNearRayCount, w.error);
}

/* --- through whole renders ----------------------------------------------------------------------------- */


TEST(RenderVectors, RendersMeetTheCompositeVectorsWhereTheCoverageIsWhole) {
  using namespace composite_vectors;
  Worst worst;
  int run = 0;
  for (const VCase & c : kCases) {
    bool whole = true;
    for (int i = 0; i < c.item_count; i++) {
      int k = c.items[i].kind;
      if ((k == 1 || k == 2) && c.items[i].coverage != 0.0 && c.items[i].coverage != 1.0) {
        whole = false;
      }
    }
    bool floats = true;
    for (double v : c.backdrop) {
      floats = floats && (double)(float)v == v;
    }
    if (!whole || !floats) {
      continue;
    }
    Bench b;
    GSHSCENE_Color fg{};
    std::memcpy(fg.rgba, c.fg, sizeof fg.rgba);
    fg.space = space_of(c.fg_space.primaries, c.fg_space.transfer, c.fg_space.gamma);
    ASSERT_EQ(gshscene_list_set_foreground(b.list, &fg), GSHSCENE_OK);
    if (c.palette_count >= 0) {
      std::vector<GSHSCENE_Color> entries((size_t)c.palette_count);
      for (int e = 0; e < c.palette_count; e++) {
        std::memcpy(entries[(size_t)e].rgba, c.palette[e].rgba, sizeof entries[(size_t)e].rgba);
        entries[(size_t)e].space = space_of(c.palette[e].space.primaries, c.palette[e].space.transfer, c.palette[e].space.gamma);
      }
      const GSHSCENE_Resource * palette = nullptr;
      ASSERT_EQ(gshscene_store_intern_palette(b.store, entries.size(), entries.data(), &palette), GSHSCENE_OK) << c.name;
      ASSERT_EQ(gshscene_list_set_palette(b.list, palette), GSHSCENE_OK);
      gshscene_resource_release(palette); /* the list holds its own */
    }
    for (int i = 0; i < c.item_count; i++) {
      const VItem & it = c.items[i];
      b.record(it.kind, it.paint, it.rgba, space_of(it.space.primaries, it.space.transfer, it.space.gamma), it.opacity,
          space_of(it.blend.primaries, it.blend.transfer, it.blend.gamma), it.composite, it.coverage == 1.0 ? 64 : 0,
          it.entry);
    }
    ASSERT_EQ(gshscene_list_close(b.list), GSHSCENE_OK);
    GIMG_Raster * raster = one_pixel(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    float px[4];
    for (int i = 0; i < 4; i++) {
      px[i] = (float)c.backdrop[i];
    }
    std::memcpy(gimg_raster_pixels(raster), px, sizeof px);
    Seen seen = render(b.list, raster, space_of(c.target.primaries, c.target.transfer, c.target.gamma));
    ASSERT_GE(seen.touched, 0) << c.name;
    if (seen.touched) {
      expect_pre(seen.root, c.want, c.exact, c.name, worst);
      std::memcpy(px, gimg_raster_pixels_const(raster), sizeof px);
      for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(float_store_ok(px[i], c.want[i], c.exact)) << c.name << " channel " << i << ": stored " << px[i];
      }
    } else {
      // Nothing touched it: the oracle's answer is the backdrop, and the pixel is not written.
      for (int i = 0; i < 4; i++) {
        EXPECT_EQ(c.want[i], c.backdrop[i]) << c.name;
      }
    }
    gimg_raster_destroy(raster);
    run++;
  }
  EXPECT_GE(run, 40) << "too few of the vectors reached a render";
  std::printf("composite_vectors through renders: %d cases; worst error %.3g (%s)\n", run, worst.error, worst.where.c_str());
}

TEST(RenderVectors, RendersMeetTheRenderVectorsOnEveryFormat) {
  using namespace render_vectors;
  Worst worst;
  int exact = 0;
  int near = 0;
  for (const RCase & c : kCases) {
    for (const RFormat & f : c.formats) {
      Bench b;
      GSHSCENE_Color fg{};
      std::memcpy(fg.rgba, c.fg, sizeof fg.rgba);
      fg.space = space_of(c.fg_space.primaries, c.fg_space.transfer, c.fg_space.gamma);
      ASSERT_EQ(gshscene_list_set_foreground(b.list, &fg), GSHSCENE_OK);
      for (int i = 0; i < c.item_count; i++) {
        const RItem & it = c.items[i];
        b.record(it.kind, it.paint, it.rgba, space_of(it.space.primaries, it.space.transfer, it.space.gamma), it.opacity,
            space_of(it.blend.primaries, it.blend.transfer, it.blend.gamma), it.composite, it.width);
      }
      ASSERT_EQ(gshscene_list_close(b.list), GSHSCENE_OK);
      GSHCPU_Format format = (GSHCPU_Format)f.format;
      GIMG_Raster * raster = one_pixel(format);
      bool wide = gshcpu_render_pixel_bytes(format) == 16u;
      unsigned char * px = (unsigned char *)gimg_raster_pixels(raster);
      for (int i = 0; i < 4; i++) {
        if (wide) {
          std::memcpy(px + 4 * i, &f.backdrop[i], 4);
        } else {
          px[i] = (unsigned char)f.backdrop[i];
        }
      }
      std::string where = std::string(c.name) + " on " + gshcpu_format_string(format);
      Seen seen = render(b.list, raster, space_of(c.target.primaries, c.target.transfer, c.target.gamma));
      EXPECT_EQ(seen.touched, f.touched ? 1 : 0) << where;
      expect_pre(seen.root, f.pre, f.exact, where, worst);
      exact += f.exact ? 1 : 0;
      for (int i = 0; i < 4; i++) {
        uint32_t got;
        if (wide) {
          std::memcpy(&got, px + 4 * i, 4);
        } else {
          got = px[i];
        }
        near += f.want[i] != f.alt[i] ? 1 : 0;
        // An exact value is rounded as the oracle rounds it, a value exactly on a boundary half up: the other side is
        // allowed only to a value the double arithmetic has moved (D7).
        EXPECT_TRUE(got == f.want[i] || (!f.exact && got == f.alt[i]))
            << where << " channel " << i << ": stored " << got << ", want " << f.want[i] << " (or " << f.alt[i] << ")";
      }
      gimg_raster_destroy(raster);
    }
  }
  EXPECT_EQ(exact, kExactCount);
  std::printf("render_vectors: %d cases on 4 formats, %d exact, %d stored values within 1e-9 of a boundary; worst error %.3g "
              "(%s)\n", kCaseCount, exact, near, worst.error, worst.where.c_str());
}

/* --- the spelling of the render hash and dump ------------------------------------------------------------- */

TEST(RenderVectors, TheHashAndDumpSpellingIsTheGenerators) {
  using namespace render_vectors;
  for (const RHash & h : kHashes) {
    GSHCPU_Format format = (GSHCPU_Format)h.format;
    GIMG_Raster * raster = nullptr;
    GIMG_Pixel_Format pf{};
    pf.channel_model = GIMG_CHANNEL_RGBA;
    pf.layout = GIMG_LAYOUT_INTERLEAVED;
    pf.channel_count = 4;
    bool wide = gshcpu_render_pixel_bytes(format) == 16u;
    pf.channel_type = wide ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
    for (int i = 0; i < 4; i++) {
      pf.bits_per_channel[i] = wide ? 32 : 8;
    }
    pf.alpha_form = format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED
        ? GIMG_ALPHA_PREMULTIPLIED
        : GIMG_ALPHA_STRAIGHT;
    ASSERT_EQ(gimg_raster_create(h.width, h.height, &pf, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
    unsigned char * px = (unsigned char *)gimg_raster_pixels(raster);
    size_t stride = gimg_raster_stride_bytes(raster);
    for (uint32_t y = 0; y < h.height; y++) {
      for (uint32_t x = 0; x < h.width; x++) {
        const uint32_t * v = h.pixels + 4 * (y * h.width + x);
        for (int i = 0; i < 4; i++) {
          if (wide) {
            std::memcpy(px + y * stride + x * 16 + 4 * i, &v[i], 4);
          } else {
            px[y * stride + x * 4 + i] = (unsigned char)v[i];
          }
        }
      }
    }
    GSHSCENE_Rect extent{h.extent[0], h.extent[1], h.extent[2], h.extent[3]};
    uint64_t got = 0;
    ASSERT_EQ(gshcpu_render_hash(raster, &extent, &got), GSHCPU_OK);
    EXPECT_EQ(got, h.hash) << gshcpu_format_string(format);
    FILE * out = std::tmpfile();
    ASSERT_NE(out, nullptr);
    ASSERT_EQ(gshcpu_render_dump(raster, &extent, out), GSHCPU_OK);
    std::rewind(out);
    std::string text;
    for (int ch; (ch = std::fgetc(out)) != EOF;) {
      text.push_back((char)ch);
    }
    std::fclose(out);
    EXPECT_EQ(text, h.dump);
    gimg_raster_destroy(raster);
  }
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
