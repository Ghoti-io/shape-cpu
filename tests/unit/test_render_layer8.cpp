/**
 * @file
 *
 * The byte compositors of the fast fill coverage on an 8-bit premultiplied target (direct.c, gradient.c): the integer
 * source-over of a row of sources against the arithmetic written out, the blend rows against the double operators, and the
 * gradient table's indices (the vector rows and the scalar ones) against the evaluation, over lengths that leave every kind of
 * tail.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"

#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

uint32_t div255(uint32_t x) {
  const uint32_t t = x + 128u;
  return (t + (t >> 8)) >> 8;
}

/** The arithmetic of the 8-bit compositor, written out for one pixel. */
void reference_over(uint8_t d[4], const uint8_t s[4], uint32_t m) {
  if (m == 0 || s[3] == 0) {
    return;
  }
  uint32_t w[4];
  for (int k = 0; k < 4; k++) {
    w[k] = m == 255u ? s[k] : div255(s[k] * m);
  }
  const uint32_t keep = 255u - w[3];
  for (int k = 0; k < 4; k++) {
    d[k] = (uint8_t)std::min(255u, div255(d[k] * keep) + w[k]);
  }
}

/** A premultiplied pixel: no channel past the alpha. */
void random_pixel(std::mt19937 & g, uint8_t p[4]) {
  const int kind = (int)(g() % 6);
  p[3] = kind == 0 ? 0 : kind == 1 ? 255 : (uint8_t)(g() % 256);
  for (int k = 0; k < 3; k++) {
    p[k] = p[3] == 0 ? 0 : (uint8_t)(g() % (p[3] + 1u));
  }
}

}  // namespace

TEST(RenderLayer8, ARowOfSourcesComposesAsTheArithmeticWrittenOut) {
  std::mt19937 g(12345);
  for (size_t n = 0; n <= 70; n++) {
    for (int variant = 0; variant < 3; variant++) {
      std::vector<uint8_t> dst(4 * (n + 1)), src(4 * (n + 1)), m(n + 1);
      for (size_t i = 0; i < n; i++) {
        random_pixel(g, &dst[4 * i]);
        random_pixel(g, &src[4 * i]);
        m[i] = variant == 0 ? 255 : (uint8_t)(g() % 256);
      }
      const uint32_t m_const = variant == 2 ? 1u + g() % 255u : 0u;
      std::vector<uint8_t> want = dst;
      for (size_t i = 0; i < n; i++) {
        reference_over(&want[4 * i], &src[4 * i], variant == 2 ? m_const : m[i]);
      }
      std::vector<uint32_t> srcw(n + 1);
      std::memcpy(srcw.data(), src.data(), 4 * n);
      gshcpu_direct8_span_pixels(dst.data(), srcw.data(), variant == 2 ? nullptr : m.data(), m_const, n);
      ASSERT_EQ(std::memcmp(dst.data(), want.data(), 4 * n), 0) << "n " << n << " variant " << variant;
      EXPECT_EQ(dst[4 * n], 0) << "nothing past the row is written";
    }
  }
}

TEST(RenderLayer8, ABlendRowStaysWithinALevelOrTwoOfTheDoubleOperator) {
  const uint32_t ops[] = {GSHSCENE_COMPOSITE_MULTIPLY, GSHSCENE_COMPOSITE_SCREEN, GSHSCENE_COMPOSITE_OVERLAY,
      GSHSCENE_COMPOSITE_HARD_LIGHT, GSHSCENE_COMPOSITE_DARKEN, GSHSCENE_COMPOSITE_LIGHTEN, GSHSCENE_COMPOSITE_DIFFERENCE,
      GSHSCENE_COMPOSITE_EXCLUSION};
  std::mt19937 g(777);
  for (uint32_t op : ops) {
    ASSERT_TRUE(gshcpu_direct8_blends(op));
    for (size_t n : {1u, 5u, 8u, 13u, 64u, 67u}) {
      for (float opacity : {1.0f, 0.5f}) {
        std::vector<uint8_t> dst(4 * n), src(4 * n);
        for (size_t i = 0; i < n; i++) {
          random_pixel(g, &dst[4 * i]);
          random_pixel(g, &src[4 * i]);
        }
        std::vector<uint8_t> got = dst;
        gshcpu_direct8_blend_row(op, got.data(), src.data(), n, opacity);
        int worst = 0;
        for (size_t i = 0; i < n; i++) {
          double d[4], s[4];
          for (int k = 0; k < 4; k++) {
            d[k] = dst[4 * i + (size_t)k] / 255.0;
            s[k] = opacity * (src[4 * i + (size_t)k] / 255.0);
          }
          if (s[3] == 0.0) {
            for (int k = 0; k < 4; k++) {
              EXPECT_EQ(got[4 * i + (size_t)k], dst[4 * i + (size_t)k]) << "no source alpha: left alone";
            }
            continue;
          }
          ASSERT_EQ(gshcpu_render_apply(d, s, 1.0, op), 1);
          for (int k = 0; k < 4; k++) {
            const int want = (int)std::lround(std::min(1.0, std::max(0.0, d[k])) * 255.0);
            worst = std::max(worst, std::abs(want - (int)got[4 * i + (size_t)k]));
          }
        }
        EXPECT_LE(worst, 2) << "op " << op << " n " << n << " opacity " << opacity;
      }
    }
  }
}

TEST(RenderLayer8, ClipWeighsAgreeWithTheIntegerRoundingOfD8) {
  std::mt19937 g(4242);
  for (size_t n : {0u, 1u, 31u, 32u, 33u, 64u, 100u, 257u}) {
    std::vector<uint8_t> m(n + 1), c(n + 1);
    for (size_t i = 0; i < n; i++) {
      m[i] = (uint8_t)(g() % 256);
      c[i] = (uint8_t)(g() % 256);
    }
    m[n] = 77;
    std::vector<uint8_t> want = m;
    for (size_t i = 0; i < n; i++) {
      want[i] = (uint8_t)gshcpu_render_weigh(m[i], c[i]);
    }
    gshcpu_direct8_weigh(m.data(), c.data(), n);
    EXPECT_EQ(m, want) << "n " << n;
  }
}

namespace {

GSHGEOM_Map map_of(double a, double b, double c, double d, double e, double f) {
  const double m[4] = {a, b, c, d};
  const double t[2] = {e, f};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

}  // namespace

// The table's index of every pixel of a row, whichever kind of gradient and however the row ends, is the point whose color is
// the evaluation's at that pixel (to the spacing of the points): the vector rows of the radial and the sweep and the scalar
// tail make the same choices.
TEST(RenderLayer8, TheTableRowAgreesWithTheEvaluationPixelByPixel) {
  GSHCPU_Stop stops[3] = {};
  stops[0].offset = 0.0;
  stops[0].rgba[0] = 1.0;
  stops[0].rgba[3] = 1.0;
  stops[1].offset = 0.4;
  stops[1].rgba[1] = 1.0;
  stops[1].rgba[3] = 1.0;
  stops[2].offset = 1.0;
  stops[2].rgba[2] = 1.0;
  stops[2].rgba[3] = 0.5;
  GSHCPU_Convert same;
  GSHSCENE_Color_Space space{};
  gshcpu_render_convert_prepare(&same, &space, &space);
  GSHGEOM_Map id = map_of(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
  GSHGEOM_Map slanted = map_of(1.3, 0.2, -0.1, 0.9, 5.0, -3.0);
  for (int kind = 0; kind < 4; kind++) {
    GSHSCENE_Gradient gs{};
    gs.extend = GSHSCENE_EXTEND_PAD;
    gs.stop_count = 3;
    switch (kind) {
      case 0:
        gs.kind = GSHSCENE_GRADIENT_LINEAR;
        gs.p0[0] = 20;
        gs.p1[0] = 90;
        gs.p1[1] = 30;
        break;
      case 1:
        gs.kind = GSHSCENE_GRADIENT_RADIAL;
        gs.c0[0] = 40;
        gs.c0[1] = 30;
        gs.r0 = 3;
        gs.c1[0] = 50;
        gs.c1[1] = 35;
        gs.r1 = 60;
        break;
      case 2:
        gs.kind = GSHSCENE_GRADIENT_RADIAL; /* two circles of one size, touching: a is zero */
        gs.c0[0] = 10;
        gs.r0 = 20;
        gs.c1[0] = 60;
        gs.r1 = 20;
        break;
      default:
        gs.kind = GSHSCENE_GRADIENT_SWEEP;
        gs.center[0] = 50;
        gs.center[1] = 40;
        gs.angle_unit = GSHGEOM_ANGLE_RADIANS;
        gs.a0 = 0.5;
        gs.a1 = 5.5;
        break;
    }
    gs.map = id;
    for (const GSHGEOM_Map * m : {&id, &slanted}) {
      GSHCPU_Gradient gr;
      ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &gs, stops, m, 1.0), GSHCPU_OK);
      std::vector<double> table(4u * (GSHCPU_RENDER_GRADIENT_TABLE + 1u));
      gshcpu_render_gradient_table(&gr, &same, table.data());
      size_t checked = 0;
      size_t close = 0;
      for (size_t count : {1u, 3u, 4u, 7u, 8u, 30u, 101u}) {
        std::vector<uint8_t> cov(count, 255);
        cov[count / 2] = 0;
        std::vector<uint32_t> index(count, 7u);
        const double y = 41.5;
        gshcpu_render_gradient_table_row(&gr, 3.5, y, count, cov.data(), index.data());
        for (size_t i = 0; i < count; i++) {
          double color[4];
          const bool drawn = gshcpu_render_gradient_at(&gr, 3.5 + (double)i, y, color) != 0;
          if (cov[i] == 0) {
            EXPECT_EQ(index[i], GSHCPU_RENDER_GRADIENT_NONE) << "an uncovered pixel gets none";
            continue;
          }
          if (!drawn) {
            EXPECT_EQ(index[i], GSHCPU_RENDER_GRADIENT_NONE) << "kind " << kind << " pixel " << i << ": drawn by neither";
            continue;
          }
          ASSERT_NE(index[i], GSHCPU_RENDER_GRADIENT_NONE) << "kind " << kind << " pixel " << i;
          ASSERT_LE(index[i], GSHCPU_RENDER_GRADIENT_TABLE);
          const double want[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};
          double worst = 0;
          for (int k = 0; k < 4; k++) {
            worst = std::max(worst, std::fabs(table[4u * index[i] + (size_t)k] - want[k]));
          }
          checked++;
          close += worst <= 0.02 ? 1 : 0;
        }
      }
      EXPECT_GE(close * 100u, checked * 98u) << "kind " << kind;
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
