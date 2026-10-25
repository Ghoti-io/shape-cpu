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

TEST(RenderLayer8, PremultiplyingARowIsTheD8WeightOfEachChannel) {
  std::mt19937 g(99);
  for (size_t n : {0u, 1u, 7u, 8u, 9u, 64u, 101u}) {
    std::vector<uint8_t> src(4 * (n + 1)), dst(4 * (n + 1), 0xAB), want(4 * (n + 1), 0xAB);
    for (size_t i = 0; i < n; i++) {
      for (int k = 0; k < 4; k++) {
        src[4 * i + (size_t)k] = (uint8_t)(g() % 4 == 0 ? (g() % 2 ? 255 : 0) : g() % 256);
      }
      const uint32_t a = src[4 * i + 3];
      for (int k = 0; k < 3; k++) {
        want[4 * i + (size_t)k] = (uint8_t)gshcpu_render_weigh(src[4 * i + (size_t)k], a);
      }
      want[4 * i + 3] = (uint8_t)a;
    }
    gshcpu_direct8_premultiply_row(dst.data(), src.data(), n);
    EXPECT_EQ(dst, want) << "n " << n;
  }
}

TEST(RenderLayer8, AStraightStoreLeavesWhatTheDrawingDidNotChange) {
  // A target of straight pixels of every kind of alpha, loaded (premultiplied), left as they are, and four of them changed.
  const size_t n = 300;
  std::mt19937 g(5);
  std::vector<uint8_t> target(4 * n);
  for (size_t i = 0; i < n; i++) {
    for (int k = 0; k < 4; k++) {
      target[4 * i + (size_t)k] = (uint8_t)(g() % 256);
    }
    if (i % 3 == 0) {
      target[4 * i + 3] = 255;
    } else if (i % 3 == 1 && i % 2) {
      target[4 * i + 3] = 0;
    }
  }
  const std::vector<uint8_t> original = target;
  std::vector<uint8_t> layer(4 * n);
  gshcpu_direct8_premultiply_row(layer.data(), target.data(), n);
  std::vector<uint8_t> loaded = layer;
  gshcpu_direct8_store_straight_row(target.data(), layer.data(), n);
  EXPECT_EQ(target, original) << "untouched pixels come back as they were, rounding of the round trip or not";
  // Change four: an opaque one to another color, a transparent to half red, a partial to a new alpha, one to clear.
  size_t idx[4] = {3, 7, 4, 10};
  const uint8_t value[4][4] = {{10, 20, 30, 255}, {100, 0, 0, 200}, {50, 40, 30, 120}, {0, 0, 0, 0}};
  for (int j = 0; j < 4; j++) {
    std::memcpy(&layer[4 * idx[j]], value[j], 4);
  }
  gshcpu_direct8_store_straight_row(target.data(), layer.data(), n);
  for (size_t i = 0; i < n; i++) {
    bool changed = false;
    for (int j = 0; j < 4; j++) {
      changed = changed || idx[j] == i;
    }
    if (!changed) {
      EXPECT_EQ(std::memcmp(&target[4 * i], &original[4 * i], 4), 0) << "pixel " << i;
    }
  }
  EXPECT_EQ(target[4 * 3 + 0], 10);
  EXPECT_EQ(target[4 * 3 + 3], 255);
  EXPECT_EQ(target[4 * 7 + 3], 200);
  EXPECT_EQ(target[4 * 7 + 0], 128) << "100 of 200 is half: 127.5 rounds up";
  EXPECT_EQ(target[4 * 10 + 3], 0);
  EXPECT_EQ(target[4 * 10 + 0], 0);
  (void)loaded;
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

GSHSCENE_Gradient gradient_of(int kind, const GSHGEOM_Map & id) {
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
  return gs;
}

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
    GSHSCENE_Gradient gs = gradient_of(kind, id);
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

// The vector kernel and its scalar twin: the same indices, bit for bit, for every kind, whatever the row's start and length.
TEST(RenderLayer8, TheVectorRowsMakeTheIndicesOfTheirScalarTwins) {
  GSHCPU_Stop stops[3] = {};
  stops[1].offset = 0.5;
  stops[2].offset = 1.0;
  stops[2].rgba[3] = 1.0;
  GSHGEOM_Map id = map_of(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
  GSHGEOM_Map slanted = map_of(0.9, 0.3, -0.2, 1.1, 3.0, -7.0);
  std::mt19937 g(2024);
  size_t compared = 0;
  for (int kind = 0; kind < 4; kind++) {
    GSHSCENE_Gradient gs = gradient_of(kind, id);
    for (const GSHGEOM_Map * m : {&id, &slanted}) {
      GSHCPU_Gradient gr;
      ASSERT_EQ(gshcpu_render_gradient_prepare(&gr, &gs, stops, m, 1.0), GSHCPU_OK);
      for (int trial = 0; trial < 200; trial++) {
        const size_t count = 1 + g() % 130;
        const double qx = (double)(int)(g() % 400) - 100.0 + 0.5;
        const double qy = (double)(int)(g() % 300) - 60.0 + 0.5;
        std::vector<uint8_t> cov(count);
        for (auto & c : cov) {
          c = g() % 5 == 0 ? 0 : (uint8_t)(1 + g() % 255);
        }
        std::vector<uint32_t> vec(count), sca(count);
        gshcpu_render_gradient_vector(1);
        gshcpu_render_gradient_table_row(&gr, qx, qy, count, cov.data(), vec.data());
        gshcpu_render_gradient_vector(0);
        gshcpu_render_gradient_table_row(&gr, qx, qy, count, cov.data(), sca.data());
        gshcpu_render_gradient_vector(1);
        ASSERT_EQ(vec, sca) << "kind " << kind << " trial " << trial << " count " << count;
        compared += count;
      }
    }
  }
  EXPECT_GT(compared, 10000u);
}

// Every vector kernel of the 8-bit compositor and its scalar twin make the same bytes, so that a processor without AVX2 draws the
// picture one with it does.
TEST(RenderLayer8, TheVectorKernelsMakeTheBytesOfTheirScalarTwins) {
  std::mt19937 g(31337);
  auto both = [&](auto && run) {
    gshcpu_direct8_vector(1);
    auto a = run();
    gshcpu_direct8_vector(0);
    auto b = run();
    gshcpu_direct8_vector(1);
    return std::make_pair(a, b);
  };
  const uint32_t ops[] = {GSHSCENE_COMPOSITE_MULTIPLY, GSHSCENE_COMPOSITE_SCREEN, GSHSCENE_COMPOSITE_OVERLAY,
      GSHSCENE_COMPOSITE_HARD_LIGHT, GSHSCENE_COMPOSITE_DARKEN, GSHSCENE_COMPOSITE_LIGHTEN, GSHSCENE_COMPOSITE_DIFFERENCE,
      GSHSCENE_COMPOSITE_EXCLUSION};
  for (int trial = 0; trial < 300; trial++) {
    const size_t n = 1 + g() % 140;
    std::vector<uint8_t> dst(4 * n), src(4 * n), m(n);
    std::vector<uint32_t> srcw(n), index(n);
    std::vector<uint8_t> table(4 * 64);
    for (size_t i = 0; i < n; i++) {
      random_pixel(g, &dst[4 * i]);
      random_pixel(g, &src[4 * i]);
      m[i] = g() % 4 == 0 ? 0 : (g() % 3 == 0 ? 255 : (uint8_t)(g() % 256));
      index[i] = g() % 7 == 0 ? GSHCPU_RENDER_GRADIENT_NONE : g() % 64;
    }
    for (size_t k = 0; k < 64; k++) {
      random_pixel(g, &table[4 * k]);
    }
    std::memcpy(srcw.data(), src.data(), 4 * n);
    {
      auto r = both([&] {
        std::vector<uint8_t> d = dst;
        gshcpu_direct8_span_pixels(d.data(), srcw.data(), m.data(), 0, n);
        return d;
      });
      ASSERT_EQ(r.first, r.second) << "span_pixels, n " << n;
    }
    {
      auto r = both([&] {
        std::vector<uint8_t> d = dst;
        gshcpu_direct8_span_pixels(d.data(), srcw.data(), nullptr, 100u, n);
        return d;
      });
      ASSERT_EQ(r.first, r.second) << "span_pixels with one coverage, n " << n;
    }
    {
      auto r = both([&] {
        std::vector<uint8_t> d = dst;
        gshcpu_direct8_span_table(d.data(), table.data(), index.data(), m.data(), n);
        return d;
      });
      ASSERT_EQ(r.first, r.second) << "span_table, n " << n;
    }
    {
      auto r = both([&] {
        std::vector<uint8_t> mm = m;
        gshcpu_direct8_weigh(mm.data(), dst.data(), n);
        return mm;
      });
      ASSERT_EQ(r.first, r.second) << "weigh, n " << n;
    }
    {
      auto r = both([&] {
        std::vector<uint8_t> d(4 * n);
        gshcpu_direct8_premultiply_row(d.data(), dst.data(), n);
        return d;
      });
      ASSERT_EQ(r.first, r.second) << "premultiply, n " << n;
    }
    for (uint32_t op : ops) {
      for (float opacity : {1.0f, 0.22f, 0.75f}) {
        auto r = both([&] {
          std::vector<uint8_t> d = dst;
          gshcpu_direct8_blend_row(op, d.data(), src.data(), n, opacity);
          return d;
        });
        ASSERT_EQ(r.first, r.second) << "blend op " << op << " opacity " << opacity << " n " << n;
      }
    }
  }
}
