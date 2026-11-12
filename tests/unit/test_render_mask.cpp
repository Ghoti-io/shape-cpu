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
 * Masks in the sink (masks-images-patterns story M2; MC1 to MC10 in render.h): the caps, a mask is not a clip, what a mask
 * acts on, the pop region, the frame, the memory, the limits, the tiers, and what a list with no mask pays.  The mask's value
 * itself, in every mode, transfer and space, is the vector gate's (test_render_mask_vectors.cpp).
 */

#include <gtest/gtest.h>

#include "../mask_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <thread>

using namespace mh;

namespace {

GSHSCENE_Paint white() {
  return rh::solid(1.0, 1.0, 1.0, 1.0);
}

GSHSCENE_Paint red() {
  return rh::solid(1.0, 0.0, 0.0, 1.0);
}

GSHSCENE_Sink_Caps caps_of_cpu() {
  SinkRef sink;
  RasterRef r(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 4, 4));
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Sink_Caps caps{};
  EXPECT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
  return caps;
}

} // namespace

/* --- MC1: what the sink claims ---------------------------------------------------------------------------- */

TEST(RenderMask, TheSinkClaimsTheFlagAndEveryFeatureTheWaistDefines) {
  GSHSCENE_Sink_Caps caps = caps_of_cpu();
  EXPECT_NE(caps.flags & GSHSCENE_CAP_GROUP_SOFT_MASK, 0u);
  EXPECT_EQ(caps.mask_native, (uint32_t)GSHSCENE_MASK_NATIVE_ALL);
  // And the two flags a masked group may also set (masks-images-patterns story M3, KC1).
  EXPECT_EQ(caps.flags & (GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT),
      (uint32_t)(GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT));
}

TEST(RenderMask, AMaskedGroupIsDrawnWithNoRefusalWhetherOrNotItIsAlsoNonIsolatedOrKnockout) {
  for (int variant = 0; variant < 3; variant++) {
    Scene s;
    const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 8, 8), white()));
    GSHSCENE_List * l = s.open();
    GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
    g.isolation = variant == 1 ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
    g.knockout = variant == 2 ? 1 : 0;
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Paint r = red();
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Draw d = draw(l, 8, 8);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u) << variant;
    EXPECT_EQ(d.px(3, 3), std::string("\xff\x00\x00\xff", 4)) << "drawn, in every combination: " << variant;
    Options strict;
    strict.flags = GSHSCENE_RENDER_STRICT;
    EXPECT_EQ(draw(l, 8, 8, strict).result, GSHSCENE_OK) << "strict refuses nothing: " << variant;
  }
}

/* --- MK8, MC3: a mask is not a clip ------------------------------------------------------------------------ */

// Two shapes abut at x = 3.5, half-covering the pixel they share.  As the pieces of an alpha mask they compose to 3/4 there; a
// clip of the union of the two is one shape, and its coverage in the pixel is 1.  The mask's value is the picture's alpha after
// it is rendered, not the area of a union, which is why a union clip is never lowered as a mask.
TEST(RenderMask, AMaskIsNotAClipTwoAbuttingHalvesMakeThreeQuartersWhereAnExactUnionIsWhole) {
  Scene s;
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Paint w = white();
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 3.5, 4), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(3.5, 0, 8, 4), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl));
  GSHSCENE_List * masked = s.masked_fill(mask, s.rect(0, 0, 8, 4), red());
  Draw m = draw(masked, 8, 4);
  ASSERT_EQ(m.result, GSHSCENE_OK);
  // The exact union: one clip of the whole, around the same fill.
  GSHSCENE_List * clipped = s.open();
  ASSERT_EQ(gshscene_list_clip_push(clipped, s.rect(0, 0, 8, 4), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint r = red();
  ASSERT_EQ(gshscene_list_fill(clipped, s.rect(0, 0, 8, 4), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(clipped), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(clipped), GSHSCENE_OK);
  Draw c = draw(clipped, 8, 4);
  ASSERT_EQ(c.result, GSHSCENE_OK);
  EXPECT_EQ(c.at8(3, 1, 3), 255u) << "the exact union: the shared pixel is whole";
  // Each half covers 128/255 of the pixel (D8's eight-bit coverage of half a pixel), composed source-over: a + (1 - a) a.
  const double a = 128.0 / 255.0;
  const double mask_alpha = a + (1.0 - a) * a;
  EXPECT_NEAR(m.at8(3, 1, 3), std::floor(mask_alpha * 255.0 + 0.5), 0.0) << "the mask: 3/4, not 1";
  EXPECT_EQ(m.at8(3, 1, 3), 192u);
  EXPECT_EQ(m.at8(3, 1, 0), 192u) << "premultiplied: the red is the alpha";
  EXPECT_EQ(m.at8(2, 1, 3), 255u) << "whole pixels either side are whole";
  EXPECT_EQ(m.at8(5, 1, 3), 255u);
}

/* --- helpers of the tests below -------------------------------------------------------------------------------------- */

namespace {

/** A scene with a star-shaped alpha mask of partial coverage and a table transfer, over two fills of partial coverage. */
struct StarScene {
  Scene s;
  GSHSCENE_List * list = nullptr;
  explicit StarScene(uint32_t composite = GSHSCENE_COMPOSITE_SRC_OVER, double opacity = 0.8, uint32_t acts_on = GSHSCENE_MASK_ACTS_ON_OPACITY) {
    GSHSCENE_Mask m{};
    m.picture = s.picture_of(s.polygon({16.5, 3.25, 28.75, 28.5, 4.25, 12.75, 29.0, 12.75, 4.0, 28.0}), white());
    m.mode = GSHSCENE_MASK_ALPHA;
    m.acts_on = acts_on;
    static const double table[3] = {0.1, 0.6, 1.0};
    m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
    m.transfer.sample_count = 3;
    m.transfer.samples = table;
    list = s.open();
    GSHSCENE_Paint back = rh::solid(0.9, 0.9, 0.5, 1.0);
    EXPECT_EQ(gshscene_list_fill(list, s.rect(0, 0, 32, 32), nullptr, GSHGEOM_FILL_NONZERO, &back, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group g = s.group(opacity, composite, s.mask(m));
    EXPECT_EQ(gshscene_list_group_push(list, &g, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Paint a = rh::solid(0.8, 0.1, 0.1, 0.9), b = rh::solid(0.1, 0.2, 0.9, 0.6);
    EXPECT_EQ(gshscene_list_fill(list, s.rect(2.5, 4.25, 22.75, 20.5), nullptr, GSHGEOM_FILL_NONZERO, &a, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(list, s.rect(10.25, 9.5, 30.5, 29.75), nullptr, GSHGEOM_FILL_NONZERO, &b, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(list), GSHSCENE_OK);
  }
};

/** A small linear congruential generator: the same windows everywhere. */
uint32_t next_random(uint32_t * state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

} // namespace

/* --- MC4: what the mask acts on --------------------------------------------------------------------------------------- */

// Under an isolated parent the opacity and the shape are the same weight for a bounded operator that is affine in the source
// (source-over, a blend mode), and differ for PLUS where its sum clamps and for every operator that is not bounded.  The
// vectors say exactly how; this says which.
TEST(RenderMask, TheMasksValueWeighsTheOpacityOrTheShapeAndTheyAgreeWhereTheOperatorIsAffine) {
  struct Op {
    uint32_t op;
    bool agrees;
  };
  const Op ops[] = {
      {GSHSCENE_COMPOSITE_SRC_OVER, true}, {GSHSCENE_COMPOSITE_MULTIPLY, true}, {GSHSCENE_COMPOSITE_SCREEN, true},
      {GSHSCENE_COMPOSITE_HUE, true}, {GSHSCENE_COMPOSITE_XOR, true}, {GSHSCENE_COMPOSITE_PLUS, false},
      {GSHSCENE_COMPOSITE_DEST_IN, false}, {GSHSCENE_COMPOSITE_SRC, false}, {GSHSCENE_COMPOSITE_SRC_IN, false},
      {GSHSCENE_COMPOSITE_SRC_OUT, false}, {GSHSCENE_COMPOSITE_CLEAR, false},
  };
  for (const Op & o : ops) {
    Draw by[2];
    for (uint32_t acts : {(uint32_t)GSHSCENE_MASK_ACTS_ON_OPACITY, (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE}) {
      Scene s;
      GSHSCENE_List * pl = s.open();
      GSHSCENE_Paint half = rh::solid(1, 1, 1, 0.5);
      ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &half, 0, 0.0), GSHSCENE_OK);
      const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl), acts);
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint backdrop = rh::solid(0.75, 0.0, 0.5, 1.0);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &backdrop, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group g = s.group(1.0, o.op, mask);
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Paint src = rh::solid(1.0, 0.25, 0.0, 1.0);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &src, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      Options opt;
      opt.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
      by[acts] = draw(l, 4, 4, opt);
      ASSERT_EQ(by[acts].result, GSHSCENE_OK);
    }
    double worst = 0.0;
    for (int ch = 0; ch < 4; ch++) {
      worst = std::max(worst, (double)std::fabs(by[0].atf(1, 1, ch) - by[1].atf(1, 1, ch)));
    }
    if (o.agrees) {
      EXPECT_LE(worst, 1e-6) << "operator " << o.op << ": the opacity and the shape weigh alike";
    } else {
      EXPECT_GT(worst, 1e-3) << "operator " << o.op << ": the shape weighs the whole operator, the opacity only the source";
    }
  }
}

// MC4 and MK7: PLUS is affine in the source only below its clamp.  With v = 1/2 and a group opacity of 1, the shape weighs the whole
// operator, (1 - v) B + v min(1, S + B), and the opacity the source, min(1, B + v S).  Where no component of S + B exceeds 1 they are
// the same number (B + v S); where one does they are not: the shape gives B + v (1 - B) there and the opacity min(1, B + v S).
TEST(RenderMask, PlusIsTheSameForShapeAndOpacityBelowItsClampAndNotAboveIt) {
  struct Case {
    const char * what;
    GSHSCENE_Paint backdrop, source;
    bool clamps;
    float opacity[4], shape[4]; /* premultiplied, at one pixel */
  };
  const Case cases[] = {
      // B = (0.15, 0, 0.15, 0.3), S = (0.4, 0.1, 0, 0.4): every sum is at most 1, so B + v S for both.
      {"below the clamp", rh::solid(0.5, 0.0, 0.5, 0.3), rh::solid(1.0, 0.25, 0.0, 0.4), false, {0.0f}, {0.0f}},
      // B = (0.75, 0, 0.5, 1), S = (1, 0.25, 0, 1): red and alpha clamp.  Opacity: (1, 0.125, 0.5, 1); shape: (0.875, 0.125, 0.5, 1).
      {"above the clamp", rh::solid(0.75, 0.0, 0.5, 1.0), rh::solid(1.0, 0.25, 0.0, 1.0), true, {1.0f, 0.125f, 0.5f, 1.0f}, {0.875f, 0.125f, 0.5f, 1.0f}},
  };
  for (const Case & c : cases) {
    Draw by[2];
    for (uint32_t acts : {(uint32_t)GSHSCENE_MASK_ACTS_ON_OPACITY, (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE}) {
      Scene s;
      GSHSCENE_List * pl = s.open();
      GSHSCENE_Paint half = rh::solid(1, 1, 1, 0.5);
      ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &half, 0, 0.0), GSHSCENE_OK);
      const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl), acts);
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint backdrop = c.backdrop;
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &backdrop, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_PLUS, mask);
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Paint src = c.source;
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &src, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      Options opt;
      opt.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
      by[acts] = draw(l, 4, 4, opt);
      ASSERT_EQ(by[acts].result, GSHSCENE_OK);
    }
    const Draw & o = by[GSHSCENE_MASK_ACTS_ON_OPACITY];
    const Draw & sh = by[GSHSCENE_MASK_ACTS_ON_SHAPE];
    if (!c.clamps) {
      // B + v S, from the premultiplied values: the sum of the premultiplied components of the backdrop and half the source's.
      const float want[4] = {0.15f + 0.5f * 0.4f, 0.0f + 0.5f * 0.1f, 0.15f + 0.0f, 0.3f + 0.5f * 0.4f};
      for (int ch = 0; ch < 4; ch++) {
        EXPECT_NEAR(o.atf(1, 1, ch), want[ch], 2e-6) << c.what << ", opacity, channel " << ch;
        EXPECT_NEAR(sh.atf(1, 1, ch), want[ch], 2e-6) << c.what << ", shape, channel " << ch;
      }
    } else {
      for (int ch = 0; ch < 4; ch++) {
        EXPECT_NEAR(o.atf(1, 1, ch), c.opacity[ch], 2e-6) << c.what << ", opacity, channel " << ch;
        EXPECT_NEAR(sh.atf(1, 1, ch), c.shape[ch], 2e-6) << c.what << ", shape, channel " << ch;
      }
      EXPECT_GT(std::fabs(o.atf(1, 1, 0) - sh.atf(1, 1, 0)), 0.1f) << "the clamp binds in red, and the two differ there";
    }
  }
}

// MC4: a mask of value 1 changes nothing, bit for bit: `o * 1 = o`, and `(m8 / 255) * 1 = m8 / 255`.
TEST(RenderMask, AMaskOfValueOneIsTheSameBitsAsNoMask) {
  for (uint32_t acts : {(uint32_t)GSHSCENE_MASK_ACTS_ON_OPACITY, (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE}) {
    for (uint32_t op : {(uint32_t)GSHSCENE_COMPOSITE_SRC_OVER, (uint32_t)GSHSCENE_COMPOSITE_MULTIPLY, (uint32_t)GSHSCENE_COMPOSITE_XOR,
                        (uint32_t)GSHSCENE_COMPOSITE_DEST_IN}) {
      Scene s;
      const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 32, 32), white()), acts);
      GSHSCENE_Paint a = rh::solid(0.8, 0.1, 0.3, 0.7);
      GSHSCENE_List * with = s.masked_fill(mask, s.rect(2.25, 3.5, 20.75, 22.5), a, 0.6, op);
      GSHSCENE_List * without = s.masked_fill(nullptr, s.rect(2.25, 3.5, 20.75, 22.5), a, 0.6, op);
      for (GSHCPU_Format f : RF_FORMATS) {
        Options opt;
        opt.format = f;
        opt.backdrop = true;
        Draw m = draw(with, 32, 32, opt);
        Draw p = draw(without, 32, 32, opt);
        ASSERT_EQ(m.result, GSHSCENE_OK);
        EXPECT_EQ(m.bytes, p.bytes) << "format " << f << " operator " << op << " acting on " << acts;
      }
    }
  }
}

/* --- MC2: the pop region ----------------------------------------------------------------------------------------------- */

TEST(RenderMask, TheMaskIsDrawnOverThePopRegionAndNoMore) {
  struct Case {
    const char * what;
    uint32_t op;
    bool clipped;
    bool damaged;
    uint64_t pixels;
  };
  // The group's fill is the whole pixels (10, 10) to (20, 20); the clip's reach is (12, 12) to (40, 40); the damage (0, 0) to (15, 15).
  const Case cases[] = {
      {"a bounded operator: the group's dirty rectangle", GSHSCENE_COMPOSITE_SRC_OVER, false, false, 100},
      {"a bounded operator under a clip: the rectangle met with the clip's reach", GSHSCENE_COMPOSITE_SRC_OVER, true, false, 64},
      {"a bounded operator under a damage", GSHSCENE_COMPOSITE_SRC_OVER, false, true, 25},
      {"an unbounded operator: the whole region", GSHSCENE_COMPOSITE_DEST_IN, false, false, 64u * 64u},
      {"an unbounded operator under a clip: the clip's reach", GSHSCENE_COMPOSITE_DEST_IN, true, false, 28u * 28u},
      {"an unbounded operator under a damage", GSHSCENE_COMPOSITE_DEST_IN, false, true, 15u * 15u},
  };
  for (const Case & c : cases) {
    Scene s;
    const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 64, 64), white()));
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint r = red();
    if (c.clipped) {
      ASSERT_EQ(gshscene_list_clip_push(l, s.rect(12, 12, 40, 40), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    }
    GSHSCENE_Group g = s.group(1.0, c.op, mask);
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(10, 10, 20, 20), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    if (c.clipped) {
      ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    const GSHSCENE_Rect damage = {0, 0, 15, 15};
    Options opt;
    opt.damage = c.damaged ? &damage : nullptr;
    Draw d = draw(l, 64, 64, opt);
    ASSERT_EQ(d.result, GSHSCENE_OK) << c.what;
    EXPECT_EQ(d.counters.masks, 1u) << c.what;
    EXPECT_EQ(d.counters.mask_pixels, c.pixels) << c.what;
    EXPECT_EQ(d.counters.mask_dirty, c.pixels) << c.what << ": the mask frame is drawn on those pixels and no others (a picture that covers the region)";
  }
}

// MC2: a pixel's value in the mask is independent of the window it was drawn in, so a render in a damage is the crop of the whole
// render, bit for bit, for random windows, over a mask whose coverage is partial and whose transfer is not the identity.
TEST(RenderMask, ARenderOverAWindowIsTheCropOfTheWholeRenderForRandomWindows) {
  for (uint32_t op : {(uint32_t)GSHSCENE_COMPOSITE_SRC_OVER, (uint32_t)GSHSCENE_COMPOSITE_DEST_IN, (uint32_t)GSHSCENE_COMPOSITE_MULTIPLY}) {
    for (uint32_t acts : {(uint32_t)GSHSCENE_MASK_ACTS_ON_OPACITY, (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE}) {
      StarScene sc(op, 0.8, acts);
      Options whole;
      whole.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
      whole.backdrop = true;
      Draw all = draw(sc.list, 32, 32, whole);
      ASSERT_EQ(all.result, GSHSCENE_OK);
      uint32_t state = 12345u + op * 31u + acts;
      for (int k = 0; k < 14; k++) {
        const int x0 = (int)(next_random(&state) % 30u), y0 = (int)(next_random(&state) % 30u);
        const int x1 = x0 + 1 + (int)(next_random(&state) % (uint32_t)(32 - x0)), y1 = y0 + 1 + (int)(next_random(&state) % (uint32_t)(32 - y0));
        const GSHSCENE_Rect damage = {x0, y0, x1, y1};
        Options opt = whole;
        opt.damage = &damage;
        Draw part = draw(sc.list, 32, 32, opt);
        ASSERT_EQ(part.result, GSHSCENE_OK);
        EXPECT_LE(part.counters.mask_pixels, (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0));
        for (int y = 0; y < 32; y++) {
          for (int x = 0; x < 32; x++) {
            const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (in) {
              ASSERT_EQ(part.px((uint32_t)x, (uint32_t)y), all.px((uint32_t)x, (uint32_t)y))
                  << "window " << x0 << "," << y0 << "," << x1 << "," << y1 << " at " << x << "," << y << " op " << op;
            }
          }
        }
      }
    }
  }
}

/* --- the order of the factors ------------------------------------------------------------------------------------------ */

// MC4 and D11: a clip, a mask, an opacity and a blend mode are a product and then the composite: the opacity may as well be
// in the mask's transfer (a table from 0 to o), within a rounding.
TEST(RenderMask, TheOpacityAndTheMaskAreOneProductWhicheverWayTheyAreWritten) {
  Scene s;
  GSHSCENE_Paint white_half = rh::solid(1, 1, 1, 0.5);
  const GSHSCENE_Resource * picture = s.picture_of(s.rect(0, 0, 16, 16), white_half);
  const GSHSCENE_Resource * plain = s.alpha_mask(picture);
  GSHSCENE_Mask scaled{};
  scaled.picture = picture;
  scaled.mode = GSHSCENE_MASK_ALPHA;
  static const double table[2] = {0.0, 0.6};
  scaled.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  scaled.transfer.sample_count = 2;
  scaled.transfer.samples = table;
  const GSHSCENE_Resource * with_opacity = s.mask(scaled);
  GSHSCENE_Paint src = rh::solid(0.2, 0.7, 0.4, 0.9);
  for (uint32_t op : {(uint32_t)GSHSCENE_COMPOSITE_SRC_OVER, (uint32_t)GSHSCENE_COMPOSITE_MULTIPLY}) {
    GSHSCENE_List * a = s.open();
    ASSERT_EQ(gshscene_list_clip_push(a, s.rect(2.5, 2, 14, 14), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group ga = s.group(0.6, op, plain);
    ASSERT_EQ(gshscene_list_group_push(a, &ga, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(a, s.rect(0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, &src, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(a), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(a), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(a), GSHSCENE_OK);
    GSHSCENE_List * b = s.open();
    ASSERT_EQ(gshscene_list_clip_push(b, s.rect(2.5, 2, 14, 14), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group gb = s.group(1.0, op, with_opacity);
    ASSERT_EQ(gshscene_list_group_push(b, &gb, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(b, s.rect(0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, &src, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(b), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(b), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(b), GSHSCENE_OK);
    Options opt;
    opt.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
    opt.backdrop = true;
    Draw da = draw(a, 16, 16, opt), db = draw(b, 16, 16, opt);
    ASSERT_EQ(da.result, GSHSCENE_OK);
    ASSERT_EQ(db.result, GSHSCENE_OK);
    for (uint32_t y = 0; y < 16; y++) {
      for (uint32_t x = 0; x < 16; x++) {
        for (int ch = 0; ch < 4; ch++) {
          EXPECT_NEAR(da.atf(x, y, ch), db.atf(x, y, ch), 2e-6) << x << "," << y << " channel " << ch << " op " << op;
        }
      }
    }
  }
}

/* --- MC6: a limit inside a mask is the whole render's ---------------------------------------------------------------- */

namespace {

/** What a render refused: each refusal's chain, reason and second, and the result. */
struct Refused {
  std::vector<std::vector<size_t>> rows;
  GSHSCENE_Result result = GSHSCENE_OK;
  bool operator==(const Refused & o) const {
    return rows == o.rows && result == o.result;
  }
};

Refused refused_of(const Draw & d) {
  Refused r;
  r.result = d.result;
  for (size_t i = 0; i < d.entries.size(); i++) {
    if (d.entries[i].entry_class == GSHSCENE_ENTRY_REFUSED) {
      std::vector<size_t> row{(size_t)d.entries[i].reason, d.entries[i].second};
      row.insert(row.end(), d.chains[i].begin(), d.chains[i].end());
      r.rows.push_back(row);
    }
  }
  return r;
}

} // namespace

// MC6: an item inside a mask picture meets the cell cap over its rectangle met with the whole render's region, never over the
// pop region or the damage.  A group of a few pixels with a mask whose picture is a large diamond: the least cap the whole
// render passes is the least cap a render in any window passes, and a window that holds none of the group (so that the pop
// region is empty) refuses what the whole render refuses.  Past it, one less, every render refuses the item with the chain
// [the group's push, the item], skips it (the mask lacks it: its value is the outside value 0) and goes on.
TEST(RenderMask, ACellCapInsideAMaskIsMetOverTheWholeRegionWhateverTheWindow) {
  Scene s;
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{32.0, 1.0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{62.0, 32.25, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{32.5, 62.0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{1.0, 31.75, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.path(p), white()));
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint r = red(), tiny = rh::solid(0, 0, 1, 1);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(60, 60, 62, 62), nullptr, GSHGEOM_FILL_NONZERO, &tiny, 0, 0.0), GSHSCENE_OK);  // 0
  GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
  ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);                                                      // 1
  ASSERT_EQ(gshscene_list_fill(l, s.rect(30, 30, 34, 34), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);   // 2
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);                                                                   // 3
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Rect windows[] = {{30, 30, 34, 34}, {31, 31, 33, 33}, {0, 0, 8, 8}, {40, 0, 64, 20}, {28, 28, 40, 40}};
  auto run = [&](uint64_t cap, const GSHSCENE_Rect * damage, uint32_t flags = 0) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = cap;
    Options opt;
    opt.limits = &limits;
    opt.damage = damage;
    opt.flags = flags;
    return draw(l, 64, 64, opt);
  };
  uint64_t lo = 1, hi = 1u << 20; // the least cap at which the whole render refuses nothing
  while (lo < hi) {
    const uint64_t mid = lo + (hi - lo) / 2;
    if (refused_of(run(mid, nullptr)).rows.empty()) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  const uint64_t T = lo;
  ASSERT_GT(T, 100u) << "the diamond is a large item";
  Refused passed = refused_of(run(T, nullptr));
  Refused over = refused_of(run(T - 1, nullptr));
  EXPECT_TRUE(passed.rows.empty());
  ASSERT_EQ(over.rows.size(), 1u) << "the one item of the mask's picture";
  EXPECT_EQ(over.rows[0][0], (size_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ((std::vector<size_t>(over.rows[0].begin() + 2, over.rows[0].end())), (std::vector<size_t>{1, 0})) << "[the group's push, the item]";
  Draw whole_over = run(T - 1, nullptr);
  EXPECT_EQ(whole_over.px(32, 32), std::string(4, '\0')) << "the mask lacks the item: its value is the outside value, 0, and nothing of the group is drawn";
  Draw whole_passed = run(T, nullptr);
  EXPECT_EQ(whole_passed.at8(32, 32, 3), 255u);
  for (const GSHSCENE_Rect & w : windows) {
    for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_CALLER_MEMORY}) {
      EXPECT_EQ(refused_of(run(T, &w, flags)), passed) << "damage " << w.x0 << "," << w.y0 << ": a cap met passes";
      EXPECT_EQ(refused_of(run(T - 1, &w, flags)), over)
          << "damage " << w.x0 << "," << w.y0 << ": one under the cap is refused, whether or not the window holds the group";
    }
  }
  // A strict render stops at the refusal, with the render of what came before.
  Draw strict = run(T - 1, nullptr, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(strict.failure, (std::vector<size_t>{1, 0}));
}

/* --- MC5: the memory is exact ---------------------------------------------------------------------------------------- */

namespace {

/** A mask of @p depth masks, one in the picture of the other, each picture holding a gradient fill and a stroke; the outer group over the whole. */
struct Deep {
  Scene s;
  GSHSCENE_List * list = nullptr;
  explicit Deep(int depth, bool decorated = true) {
    GSHSCENE_Gradient_Stop stops[2] = {{0.0, rh::solid(1, 1, 1, 1.0)}, {1.0, rh::solid(1, 1, 1, 0.5)}};
    GSHSCENE_Gradient grad{};
    grad.kind = GSHSCENE_GRADIENT_LINEAR;
    gshgeom_map_identity(2, &grad.map);
    grad.p1[0] = 16;
    grad.stop_count = 2;
    grad.stops = stops;
    const GSHSCENE_Resource * gradient = nullptr;
    EXPECT_EQ(gshscene_store_intern_gradient(s.store, &grad, &gradient), GSHSCENE_OK);
    s.held.push_back(gradient);
    GSHSCENE_Paint gp{};
    gp.kind = GSHSCENE_PAINT_GRADIENT;
    gp.resource = gradient;
    gp.rgba[3] = 1.0;
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    st.width = 2.0;
    st.alignment = GSHSCENE_STROKE_ALIGN_INSIDE; /* an aligned stroke needs the arrays the layout adds for one */
    const GSHSCENE_Resource * style = nullptr;
    EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
    s.held.push_back(style);
    const GSHSCENE_Resource * level = s.picture_of(s.rect(0, 0, 12, 12), white());
    for (int k = 1; k <= depth; k++) {
      GSHSCENE_List * up = s.open();
      GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, s.alpha_mask(level));
      EXPECT_EQ(gshscene_list_group_push(up, &g, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Paint w = white();
      if (decorated) {
        EXPECT_EQ(gshscene_list_clip_push(up, s.rect(0, 0, 13, 13), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
      }
      EXPECT_EQ(gshscene_list_fill(up, s.rect(0, 0, 14 + k, 14 + k), nullptr, GSHGEOM_FILL_NONZERO, decorated ? &gp : &w, 0, 0.0),
          GSHSCENE_OK);
      if (decorated) {
        EXPECT_EQ(gshscene_list_clip_pop(up), GSHSCENE_OK);
      }
      if (decorated) {
        EXPECT_EQ(gshscene_list_stroke(up, s.polygon({1, 1, 12, 3, 5, 12}), style, nullptr, &w, 0, 0.0), GSHSCENE_OK);
      }
      EXPECT_EQ(gshscene_list_group_pop(up), GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_fill(up, s.rect(0, 14 + k, 12, 15 + k), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK) << "a unit after the masked group: the outer walk goes on after the inner pass";
      level = s.picture(up);
    }
    list = s.masked_fill(s.alpha_mask(level), s.rect(0, 0, 24, 24), red());
  }
};

} // namespace

// MC2: a mask of a mask to the depth: the inner pops draw their own frames inside the outer pass.  Only what every mask
// covers is drawn.
TEST(RenderMask, AMaskOfAMaskOfAMaskDrawsOnlyWhatEveryLevelCovers) {
  Deep d(6, false);
  Draw out = draw(d.list, 24, 24);
  ASSERT_EQ(out.result, GSHSCENE_OK);
  EXPECT_EQ(out.counters.masks, 7u) << "six inner pops and the outer one";
  EXPECT_EQ(out.at8(11, 11, 3), 255u);
  EXPECT_EQ(out.at8(12, 11, 3), 0u);
  EXPECT_EQ(out.at8(11, 12, 3), 0u);
  EXPECT_EQ(out.at8(5, 20, 3), 255u) << "the strip the outermost picture draws after its masked group: the walk goes on after the inner pass";
}

// MC5: every masked fixture, a mask of a mask, and a mask picture holding gradients and strokes render from the caller's memory
// with no allocator call, the same pixels, the exact workspace; a byte less is a limit; nothing is written past it.
TEST(RenderMask, TheMemoryOfMasksIsExactAndNeverAllocated) {
  for (int depth : {0, 1, 2, 5, 9}) {
    for (bool decorated : {false, true}) {
      Deep d(depth, decorated);
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      {
        Options plain;
        plain.alloc = &alloc;
        plain.backdrop = true;
        Draw ref = draw(d.list, 24, 24, plain);
        ASSERT_EQ(ref.result, GSHSCENE_OK) << depth;
        SinkRef sink(nullptr, &alloc);
        ReportRef report(&alloc);
        RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 24, 24));
        GSHSCENE_Target target = target_of(r.raster);
        GSHSCENE_Render_Request req = request_of(d.list, &target, report.report);
        req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
        size_t need = 0;
        ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
        std::vector<unsigned char> workspace(need + 1 + 4096, 0xc5);
        req.workspace = workspace.data() + 1;
        req.workspace_bytes = need - 1;
        RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 24, 24));
        EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << "one byte short, depth " << depth;
        EXPECT_EQ(r.bytes(), fresh.bytes());
        req.workspace_bytes = need;
        size_t again = 0;
        long before = t.calls;
        ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &again), GSHSCENE_OK);
        EXPECT_EQ(again, need);
        EXPECT_EQ(t.calls, before) << "sizing in the caller's workspace allocated";
        for (int frame = 0; frame < 2; frame++) {
          RasterRef out(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 24, 24));
          GSHSCENE_Target ft = target_of(out.raster);
          req.target = &ft;
          const long calls = t.calls;
          ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << depth << (decorated ? " decorated" : "");
          if (frame > 0) {
            EXPECT_EQ(t.calls, calls) << "a render from the caller's memory allocated";
          }
          EXPECT_EQ(out.bytes(), ref.bytes) << "the same pixels as from the sink's own memory";
          for (size_t b = need + 1; b < workspace.size(); b++) {
            ASSERT_EQ(workspace[b], 0xc5) << "written past the caller's memory, at " << b - 1 << " (depth " << depth << ")";
          }
          ASSERT_EQ(workspace[0], 0xc5) << "written before the caller's memory";
          req.target = &target;
        }
      }
      EXPECT_TRUE(t.live.empty());
    }
  }
}

// MC5: the layout counts a mask frame as one more group, and the walk of the picture, level by level: each level of nesting adds
// a group's layer and its mask's (the region's pixels, 32 bytes each, twice) and a little for one more level of walk and frame
// records, never a third layer and never fewer than two.
TEST(RenderMask, EachLevelOfMaskNestingAddsTwoLayersAndAFewBytesToTheWorkspace) {
  std::vector<size_t> need;
  for (int depth = 1; depth <= 9; depth++) {
    Deep d(depth, false);
    SinkRef sink;
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 24, 24));
    GSHSCENE_Target target = target_of(r.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(d.list, &target, report.report);
    size_t bytes = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &bytes), GSHSCENE_OK);
    need.push_back(bytes);
  }
  const size_t layer = (size_t)24 * 24 * 32;
  for (size_t k = 1; k < need.size(); k++) {
    const size_t step = need[k] - need[k - 1];
    EXPECT_GE(step, 2 * layer) << "depth " << k + 1 << ": a group's layer and its mask's";
    EXPECT_LT(step, 2 * layer + 1024) << "depth " << k + 1 << ": and no third layer";
  }
}

// MC5: more distinct mask pictures than the layout's memo holds are counted again and not wrongly: the exact workspace still draws.
TEST(RenderMask, AListOfMoreDistinctMaskPicturesThanTheMemoHoldsIsStillExact) {
  Scene s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint r = red();
  for (int k = 0; k < 90; k++) {
    GSHSCENE_Paint w = white();
    GSHSCENE_List * inner = s.open();
    ASSERT_EQ(gshscene_list_fill(inner, s.rect(0, 0, 8.0 + 0.125 * (double)k, 8), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, s.alpha_mask(s.picture(inner)));
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, (double)(k % 8), 24, (double)(k % 8) + 1), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Options plain;
  Draw ref = draw(l, 24, 16, plain);
  Options caller;
  caller.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw out = draw(l, 24, 16, caller);
  ASSERT_EQ(ref.result, GSHSCENE_OK);
  ASSERT_EQ(out.result, GSHSCENE_OK);
  EXPECT_EQ(out.bytes, ref.bytes);
  EXPECT_EQ(out.counters.masks, 90u);
}

namespace {

/**
 * A list of @p pictures distinct mask pictures, each named by @p uses masked groups (one mask, so one picture each), the uses
 * coming round by round: every picture is named once, then every picture again, so a picture is named again only after all the
 * others were met (a memo that forgets what it held when it grew is seen by that, and not by two uses side by side).
 */
GSHSCENE_List * many_pictures(Scene & s, int pictures, int uses) {
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint r = red();
  std::vector<const GSHSCENE_Resource *> masks;
  for (int k = 0; k < pictures; k++) {
    GSHSCENE_Paint w = white();
    GSHSCENE_List * inner = s.open();
    EXPECT_EQ(gshscene_list_fill(inner, s.rect(0, 0, 8.0 + 0.0625 * (double)k, 8), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
    masks.push_back(s.alpha_mask(s.picture(inner)));
  }
  for (int u = 0; u < uses; u++) {
    for (int k = 0; k < pictures; k++) {
      GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, masks[(size_t)k]);
      EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_fill(l, s.rect(0, (double)((k + u) % 8), 24, (double)((k + u) % 8) + 1), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0),
          GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
  }
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

} // namespace

// MC5: the layout walks each mask picture once, however many pictures there are and however many masks name each: the walks are
// counted (`mask_pictures`), and one list of three pictures says how many layouts a draw makes.  Past the first sixty-four pictures
// the memo of the first version counted again where a picture was used a second time.  A render from the caller's memory finds the
// table's room in the caller's buffer, before anything else lives there; the target here is large enough that its layers hold it (a
// table takes up to about 640 bytes for each picture, and one layer takes 32 bytes for each pixel).
TEST(RenderMask, TheLayoutWalksEachMaskPictureOnceAtAnyNumberOfPictures) {
  for (bool caller_memory : {false, true}) {
    Options opt;
    if (caller_memory) {
      opt.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    }
    Scene base;
    Draw three = draw(many_pictures(base, 3, 2), 128, 96, opt);
    ASSERT_EQ(three.result, GSHSCENE_OK);
    ASSERT_GT(three.counters.mask_pictures, 0u);
    ASSERT_EQ(three.counters.mask_pictures % 3u, 0u);
    const uint64_t layouts = three.counters.mask_pictures / 3u; /* the layouts a draw makes, each walking the three once */
    for (int pictures : {64, 65, 130, 700}) {
      Scene s;
      Draw d = draw(many_pictures(s, pictures, 2), 128, 96, opt);
      ASSERT_EQ(d.result, GSHSCENE_OK);
      EXPECT_EQ(d.counters.masks, 2u * (uint64_t)pictures);
      EXPECT_EQ(d.counters.mask_pictures, layouts * (uint64_t)pictures)
          << pictures << " pictures, each named twice" << (caller_memory ? ", from the caller's memory" : "");
    }
  }
}

// ... and a render from the caller's memory takes the memo's room from its own buffer and calls the allocator for nothing, whatever the
// number of pictures: sizing in the buffer, then two renders.
TEST(RenderMask, TheLayoutsMemoInTheCallersWorkspaceNeverCallsTheAllocator) {
  Scene s;
  GSHSCENE_List * list = many_pictures(s, 300, 2);
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    Options plain;
    plain.alloc = &alloc;
    plain.backdrop = true;
    Draw ref = draw(list, 128, 96, plain);
    ASSERT_EQ(ref.result, GSHSCENE_OK);
    SinkRef sink(nullptr, &alloc);
    ReportRef report(&alloc);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 128, 96));
    GSHSCENE_Target target = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(list, &target, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t need = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
    std::vector<unsigned char> workspace(need, 0xc5);
    req.workspace = workspace.data();
    req.workspace_bytes = need;
    const long before = t.calls;
    size_t again = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &again), GSHSCENE_OK);
    EXPECT_EQ(again, need);
    EXPECT_EQ(t.calls, before) << "sizing in the caller's workspace called the allocator";
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK); /* the first render may make the sink's own one-off allocations */
    EXPECT_EQ(r.bytes(), ref.bytes);
    const long calls = t.calls;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    EXPECT_EQ(t.calls, calls) << "a render from the caller's workspace called the allocator";
    EXPECT_EQ(r.bytes(), ref.bytes);
  }
  EXPECT_TRUE(t.live.empty());
}

// ... the allocator's tables are all given back, whatever the number of pictures (the sink and the tracker both outlive the draw).
TEST(RenderMask, TheLayoutsMemoGivesBackEveryTableItTookFromTheAllocator) {
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    Scene s;
    Options plain;
    plain.alloc = &alloc;
    Draw d = draw(many_pictures(s, 300, 2), 128, 96, plain);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_EQ(d.counters.masks, 600u);
  }
  EXPECT_GT(t.calls, 0);
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks live after the sink and the report were destroyed";
}

// The memo itself (src/render/memo.c), in each of its three rooms: the allocator's, the scratch's and none.  A key it holds is found
// with its own value and the table never answers with another's; the allocator's room holds everything and gives it back, the
// scratch's holds what its bytes allow and is not written past, and with no room it holds the inline table's half and no more.
TEST(RenderMask, TheLayoutsMemoFindsEveryKeyItHoldsWithItsOwnValueInEachRoom) {
  struct Value {
    uint64_t a, b, c;
  };
  const auto key_of = [](int k) { return (uint64_t)k * UINT64_C(0x9E3779B97F4A7C15) + 7u; };
  const auto value_of = [](int k) { return Value{(uint64_t)k, ~(uint64_t)k, (uint64_t)k * 3u}; };
  const int keys = 5000;
  for (int room = 0; room < 5; room++) {
    // 0: the allocator's; 1: a scratch that holds all the tables; 2: a scratch that holds only a few; 3: none; 4: a scratch that holds
    // exactly one table (the first doubling, 64 slots of 80 bytes, with nothing left over: a table that fits exactly is taken).
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    const size_t slot_bytes = sizeof(GSHCPU_Memo_Slot);
    const size_t offset = room == 4 ? 0 : 17; /* an unaligned start: the memo aligns it (the exact fit starts aligned) */
    const size_t scratch_bytes = room == 1 ? (size_t)8 << 20 : room == 2 ? (size_t)100 << 10 : room == 4 ? 2 * GSHCPU_MEMO_INLINE * slot_bytes : 0;
    std::vector<unsigned char> store(scratch_bytes + 64, 0xc5);
    unsigned char * scratch = scratch_bytes ? store.data() + offset : nullptr;
    GSHCPU_Memo memo;
    gshcpu_memo_open(&memo, sizeof(Value), room == 0 ? &alloc : nullptr, scratch, scratch_bytes ? scratch_bytes - offset : 0);
    for (int k = 0; k < keys; k++) {
      const Value v = value_of(k);
      Value probe;
      ASSERT_FALSE(gshcpu_memo_find(&memo, key_of(k), &probe)) << "room " << room << ": a key not added is not found, " << k;
      gshcpu_memo_add(&memo, key_of(k), &v);
    }
    int held = 0, earliest_lost = -1;
    for (int k = 0; k < keys; k++) {
      Value got;
      if (gshcpu_memo_find(&memo, key_of(k), &got)) {
        held++;
        const Value want = value_of(k);
        ASSERT_TRUE(got.a == want.a && got.b == want.b && got.c == want.c) << "room " << room << ": the value of key " << k << " is another's";
      } else if (earliest_lost < 0) {
        earliest_lost = k;
      }
    }
    for (int k = keys; k < keys + 200; k++) {
      Value probe;
      ASSERT_FALSE(gshcpu_memo_find(&memo, key_of(k), &probe)) << "room " << room << ": a key never added is found";
    }
    if (room <= 1) {
      EXPECT_EQ(held, keys) << "room " << room << ": a room that holds all of them holds all of them";
    } else if (room == 2) {
      EXPECT_GT(held, 200) << "the scratch's tables hold what they can";
      EXPECT_LT(held, keys);
      EXPECT_GT(earliest_lost, 0) << "what was added first is what is held";
    } else if (room == 3) {
      EXPECT_EQ(held, (int)GSHCPU_MEMO_INLINE - 1) << "the inline table's, with a free slot kept";
    } else {
      EXPECT_EQ(held, 2 * (int)GSHCPU_MEMO_INLINE - 1) << "the one table that fits, with a free slot kept";
    }
    if (room == 0) {
      EXPECT_GT(t.calls, 0) << "tables came from the allocator";
    } else {
      EXPECT_EQ(t.calls, 0) << "and from nowhere else";
    }
    gshcpu_memo_close(&memo);
    EXPECT_TRUE(t.live.empty()) << "room " << room << ": the memo gave back what it took";
    for (int k = 0; k < keys; k++) {
      Value probe;
      ASSERT_FALSE(gshcpu_memo_find(&memo, key_of(k), &probe)) << "room " << room << ": a closed memo holds nothing, " << k;
    }
    gshcpu_memo_close(&memo); /* closing again gives back nothing twice */
    {
      const Value v = value_of(1);
      Value probe;
      gshcpu_memo_add(&memo, key_of(1), &v);
      ASSERT_TRUE(gshcpu_memo_find(&memo, key_of(1), &probe)) << "room " << room << ": a closed memo takes a key again";
      EXPECT_EQ(probe.b, ~(uint64_t)1);
      gshcpu_memo_close(&memo);
    }
    EXPECT_TRUE(t.live.empty());
    for (size_t b = 0; b < offset && scratch_bytes; b++) {
      ASSERT_EQ(store[b], 0xc5) << "written before the scratch, at " << b;
    }
    for (size_t b = scratch_bytes ? scratch_bytes : 0; b < store.size() && scratch_bytes; b++) {
      ASSERT_EQ(store[b], 0xc5) << "room " << room << ": written past the scratch, at " << b;
    }
  }
}

// ... and a picture inside a mask's picture is walked once too: some masks name one outer picture, which holds a masked group for
// each of a hundred distinct inner pictures, and the walks are the outer one and the hundred, the same whether one mask names it or
// three.
TEST(RenderMask, AMaskPictureInsideAMaskPictureIsWalkedOnceToo) {
  uint64_t walks[2] = {0, 0};
  for (int variant = 0; variant < 2; variant++) {
    const int uses = variant == 0 ? 1 : 3;
    Scene s;
    GSHSCENE_List * outer = s.open();
    GSHSCENE_Paint w = white();
    for (int k = 0; k < 100; k++) {
      GSHSCENE_List * inner = s.open();
      ASSERT_EQ(gshscene_list_fill(inner, s.rect(0, 0, 8.0 + 0.0625 * (double)k, 8), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, s.alpha_mask(s.picture(inner)));
      ASSERT_EQ(gshscene_list_group_push(outer, &g, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(outer, s.rect(0, 0, 24, 16), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(outer), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_close(outer), GSHSCENE_OK);
    const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(outer));
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint r = red();
    for (int u = 0; u < uses; u++) {
      GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 2.0 * u, 24, 2.0 * u + 1), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Draw d = draw(l, 24, 16);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    walks[variant] = d.counters.mask_pictures;
  }
  ASSERT_GT(walks[0], 0u);
  EXPECT_EQ(walks[0] % 101u, 0u) << "the outer picture and its hundred inner ones, once for each layout";
  EXPECT_EQ(walks[1], walks[0]) << "three masks name the outer picture and it is still walked once";
}

// MC5: the arena is sized by the items of the mask's picture too: a picture with one large path in it, drawn from the exact memory.
TEST(RenderMask, TheArenaIsSizedByTheItemsOfTheMasksPictureToo) {
  Scene s;
  std::vector<double> zigzag;
  for (int i = 0; i < 600; i++) {
    zigzag.push_back(0.5 + 23.0 * (double)i / 599.0);
    zigzag.push_back(i % 2 ? 2.0 : 22.0);
  }
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.polygon(zigzag), white()));
  GSHSCENE_List * l = s.masked_fill(mask, s.rect(0, 0, 24, 24), red());
  Draw ref = draw(l, 24, 24);
  Options caller;
  caller.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw out = draw(l, 24, 24, caller);
  ASSERT_EQ(ref.result, GSHSCENE_OK);
  EXPECT_EQ(out.result, GSHSCENE_OK) << "the exact memory holds the mask's largest item";
  EXPECT_EQ(out.bytes, ref.bytes);
  // The same list with a mask of a small picture needs less: the need depends on the picture's largest item.
  Scene t;
  GSHSCENE_List * small = t.masked_fill(t.alpha_mask(t.picture_of(t.rect(0, 0, 24, 24), white())), t.rect(0, 0, 24, 24), red());
  Draw little = draw(small, 24, 24);
  EXPECT_GT(ref.need, little.need + 600u * 16u);
}

// MC7: a masked pop counts its region twice: the pixels visited and the value found for each, so the worst unit of a render of a
// masked group over a region of 100 pixels is the pop's 200, past the push's 144 and the units of the mask's own.
TEST(RenderMask, AMaskedPopCountsItsRegionTwice) {
  Scene s;
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 10, 10), white()));
  Draw d = draw(s.masked_fill(mask, s.rect(0, 0, 10, 10), red()), 12, 12);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.summary.work_max, 200u);
}

/* --- MC6, MC7: refusals inside a mask, notices, cancel ---------------------------------------------------------------- */

// MK9, MC6: an item the plan refused inside the picture is reported with its chain [push, item] by the plan, skipped by the sink
// (the mask lacks it), and under strict the render does not begin.
TEST(RenderMask, ARefusedItemInsideAMaskPictureIsSkippedAndNamedByItsChain) {
  Scene s;
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Paint w = white();
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);  // 0
  const double box[4] = {1.0, 2.0, 9.0, 8.0};
  ASSERT_EQ(gshscene_list_placeholder(pl, GSHSCENE_PLACEHOLDER_SOURCE_COLR, GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED, 0, box, nullptr, 0, 0.0),
      GSHSCENE_OK);                                                                                                   // 1
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl));
  GSHSCENE_List * l = s.masked_fill(mask, s.rect(0, 0, 8, 8), red());
  Draw d = draw(l, 8, 8);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  ASSERT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_PLACEHOLDER), 1u);
  size_t at = 0;
  for (size_t i = 0; i < d.entries.size(); i++) {
    if (d.entries[i].reason == GSHSCENE_REASON_PLACEHOLDER) {
      at = i;
    }
  }
  EXPECT_EQ(d.chains[at], (std::vector<size_t>{0, 1}));
  EXPECT_EQ(d.at8(3, 3, 3), 255u) << "the rest of the picture is the mask: the group is drawn";
  Options strict;
  strict.flags = GSHSCENE_RENDER_STRICT;
  Draw sd = draw(l, 8, 8, strict);
  EXPECT_EQ(sd.result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(sd.px(3, 3), std::string(4, '\0')) << "strict fails before the target is touched";
}

// MK9, MC6: the notices of a gradient and a stroke in a mask picture are written by the plan once, whatever the render does, and
// the sink writes none: a singular placement makes both draw nothing.
TEST(RenderMask, TheNoticesOfAMaskPicturesItemsAreTheirPlansAndNotTheSinks) {
  Scene s;
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, rh::solid(1, 0, 0, 1.0)}, {1.0, rh::solid(0, 0, 1, 1.0)}};
  GSHSCENE_Gradient grad{};
  grad.kind = GSHSCENE_GRADIENT_LINEAR;
  gshgeom_map_identity(2, &grad.map);
  grad.p1[0] = 8;
  grad.stop_count = 2;
  grad.stops = stops;
  const GSHSCENE_Resource * gradient = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(s.store, &grad, &gradient), GSHSCENE_OK);
  s.held.push_back(gradient);
  GSHSCENE_Paint gp{};
  gp.kind = GSHSCENE_PAINT_GRADIENT;
  gp.resource = gradient;
  gp.rgba[3] = 1.0;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 2.0;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHSCENE_List * pl = s.open();
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &gp, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint w = white();
  ASSERT_EQ(gshscene_list_stroke(pl, s.polygon({1, 1, 7, 2, 4, 7}), style, nullptr, &w, 0, 0.0), GSHSCENE_OK);
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl));
  GSHGEOM_Map flat = scale(0.0, 1.0);
  GSHSCENE_List * l = s.masked_fill(mask, s.rect(0, 0, 8, 8), red(), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &flat);
  const GSHSCENE_Rect damage = {2, 2, 6, 6};
  for (const GSHSCENE_Rect * dm : {(const GSHSCENE_Rect *)nullptr, &damage}) {
    Options opt;
    opt.damage = dm;
    Draw d = draw(l, 8, 8, opt);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_NOTICE), 2u) << "the gradient's and the stroke's, once each, from the plan";
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
    for (const auto & c : d.chains) {
      ASSERT_EQ(c.size(), 2u);
      EXPECT_EQ(c[0], 0u);
    }
    EXPECT_EQ(d.at8(3, 3, 3), 0u) << "every item of the picture is degenerate: the mask is its outside value, 0";
  }
}

// MC7: a cancel set before a masked group's pop (after the unit before it) stops the render there: the prefix is drawn, the group
// is not composited, no unit of its mask is gone through, and the declared work bound holds.
TEST(RenderMask, ACancelBeforeAMaskedPopLeavesThePrefixAndDrawsNoMask) {
  Scene s;
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{32.0, 1.0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{62.0, 32.25, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{32.5, 62.0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{1.0, 31.75, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * diamond = s.path(p);
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Paint w = white();
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);  // fine
  ASSERT_EQ(gshscene_list_fill(pl, diamond, nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);             // over the cap
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(8, 8, 12, 12), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl));
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint blue = rh::solid(0, 0, 1, 1);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 64, 4), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);  // 0
  GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
  ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);                                                      // 1
  GSHSCENE_Paint r = red();
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);     // 2
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);                                                                   // 3
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_deposits = 40;
  struct Token {
    GSHSCENE_Cancel cancel;
    size_t at;
  } token;
  gshscene_cancel_init(&token.cancel);
  token.at = 3; // after the fill inside the group: the pop is the next unit
  auto progress = [](void * ctx, const GSHSCENE_Job *, size_t processed) {
    Token * t = (Token *)ctx;
    if (processed == t->at) {
      gshscene_cancel_set(&t->cancel);
    }
  };
  Options opt;
  opt.limits = &limits;
  opt.cancel = &token.cancel;
  opt.progress = progress;
  opt.progress_ctx = &token;
  Draw cancelled = draw(l, 64, 64, opt);
  EXPECT_EQ(cancelled.result, GSHSCENE_ERR_CANCELLED);
  EXPECT_EQ(cancelled.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 0u) << "the mask's second unit was not gone through";
  EXPECT_EQ(cancelled.counters.masks, 0u) << "the group was not composited";
  EXPECT_EQ(cancelled.at8(8, 8, 3), 0u);
  EXPECT_EQ(cancelled.at8(8, 2, 3), 255u) << "the prefix of paint order is drawn";
  Options plain;
  plain.limits = &limits;
  Draw full = draw(l, 64, 64, plain);
  ASSERT_EQ(full.result, GSHSCENE_OK);
  EXPECT_EQ(full.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  // The declared bound holds for a masked pop over a large region (the pop's composite is the most pixels it visits twice).
  SinkRef sink(&limits);
  RasterRef big(blank(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 64, 64));
  GSHSCENE_Target target = target_of(big.raster);
  GSHSCENE_Sink_Caps caps{};
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &target, &caps), GSHSCENE_OK);
  EXPECT_LE(full.summary.work_max, caps.cancel_work_bound);
  EXPECT_GT(full.summary.work_max, 0u);
}

/* --- MC8: the tiers --------------------------------------------------------------------------------------------------- */

// Each tier is the general formula with a constant or an identity folded: with it off the bytes are the same, on every format
// and every mask fixture, and with it on its counter says it was taken somewhere in the corpus.
TEST(RenderMask, EachMaskTierIsBitEqualToTheGeneralTierAndTakenOnTheCorpus) {
  uint64_t outside = 0, identity = 0, masks = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "mask-", 5) != 0) {
      continue;
    }
    for (GSHCPU_Format f : RF_FORMATS) {
      Fixture fx(i);
      Options all;
      all.format = f;
      all.backdrop = true;
      all.map = &fx.ctx.target_map;
      Draw on = draw(fx.list, RF_WIDTH, RF_HEIGHT, all);
      Options general = all;
      general.tiers = GSHCPU_RENDER_TIERS_ALL & ~(unsigned)(GSHCPU_RENDER_TIER_MASK_OUTSIDE | GSHCPU_RENDER_TIER_MASK_IDENTITY);
      Draw off = draw(fx.list, RF_WIDTH, RF_HEIGHT, general);
      ASSERT_EQ(on.result, GSHSCENE_OK) << RF_FIXTURES[i].name;
      ASSERT_EQ(off.result, GSHSCENE_OK);
      EXPECT_EQ(on.bytes, off.bytes) << RF_FIXTURES[i].name << " on " << gshcpu_format_string(f);
      EXPECT_EQ(off.counters.mask_outside, 0u);
      EXPECT_EQ(off.counters.mask_identity, 0u);
      EXPECT_EQ(off.counters.masks, on.counters.masks);
      // Each alone.
      for (unsigned tier : {(unsigned)GSHCPU_RENDER_TIER_MASK_OUTSIDE, (unsigned)GSHCPU_RENDER_TIER_MASK_IDENTITY}) {
        Options one = all;
        one.tiers = GSHCPU_RENDER_TIERS_ALL & ~tier;
        Draw d = draw(fx.list, RF_WIDTH, RF_HEIGHT, one);
        EXPECT_EQ(d.bytes, on.bytes) << RF_FIXTURES[i].name << ": without tier " << tier;
      }
      if (f == GSHCPU_FORMAT_RGBA8_STRAIGHT) {
        outside += on.counters.mask_outside;
        identity += on.counters.mask_identity;
        masks += on.counters.masks;
      }
    }
  }
  EXPECT_GT(outside, 0u) << "FT-M1 is taken on the corpus";
  EXPECT_GT(identity, 0u) << "FT-M2 is taken on the corpus";
  EXPECT_GT(masks, 20u);
  std::printf("mask tiers on the corpus: %llu masked pops, FT-M1 in %llu, FT-M2 in %llu\n", (unsigned long long)masks,
      (unsigned long long)outside, (unsigned long long)identity);
}

/* --- MC9: a list with no masked group pays nothing --------------------------------------------------------------------- */

// The memory a render needs, the work between two checks, the units and the frame bytes of every fixture from before masks, measured on the
// tree before the story: unchanged.  (The numbers are those of a 64-bit target: they are made of its pointers.)  The workspace of the twelve
// fixtures whose geometry reduces a stroke's seams (shape-geometry's cancel_seams) grew by a fraction of a percent when that step sorted its
// edges in room from the allocator, 12 bytes an edge more: the C library's qsort it replaced took its own buffer with malloc, outside the
// caller's memory.
TEST(RenderMask, AListWithNoMaskedGroupHasTheWorkspaceAndTheWorkItHadBeforeMasks) {
  if (sizeof(void *) != 8) {
    GTEST_SKIP() << "the committed sizes are those of a 64-bit target";
  }
  struct Row {
    const char * name;
    size_t workspace;
    uint64_t work_max;
    size_t processed, done, total;
    uint64_t frame_bytes;
  };
  static const Row before[] = {
    {"fill-rules", 98223, 1624, 3, 3, 3, 96863},
    {"clip-leaf", 102175, 717, 4, 4, 4, 98863},
    {"clip-group", 163871, 1920, 6, 6, 6, 160559},
    {"opacity-group", 159919, 1920, 4, 4, 4, 158559},
    {"operators", 161791, 1920, 78, 78, 78, 160559},
    {"order", 159791, 1920, 11, 11, 11, 158559},
    {"blend-spaces", 284223, 1964, 11, 11, 11, 281951},
    {"foreground", 99135, 717, 2, 2, 2, 96863},
    {"picture", 162031, 1920, 14, 14, 14, 160671},
    {"glyphs", 105663, 1201, 2, 2, 2, 96863},
    {"maps", 101135, 1178, 4, 4, 4, 98863},
    {"font-fast", 107247, 1066, 3, 3, 3, 96863},
    // font-general: 1156 deposits, one fewer than before F3: an edge is cut from its upper end whichever way it is traversed, which merges one
    // more crossing into the cell before it.
    {"font-general", 109823, 1156, 2, 2, 2, 96863},
    {"gradient-linear", 164335, 544, 3, 3, 3, 163103},
    {"gradient-radial", 166495, 832, 4, 4, 4, 163183},
    {"gradient-sweep", 165535, 559, 4, 4, 4, 163263},
    {"gradient-paints", 228799, 1920, 10, 10, 10, 227439},
    {"blend-separable", 162831, 1920, 66, 66, 66, 160559},
    {"blend-non-separable", 224527, 1920, 80, 80, 80, 222255},
    {"blend-out-of-range", 286223, 1920, 75, 75, 75, 283951},
    {"strokes-joins", 2893503, 2894, 7, 7, 7, 2855935},
    {"strokes-caps", 2977743, 3957, 12, 12, 12, 2855935},
    {"strokes-widths", 2880015, 2192, 6, 6, 6, 2855935},
    {"strokes-align", 8469231, 12883, 9, 9, 9, 8408831},
    {"strokes-paints", 2949759, 3054, 7, 7, 7, 2922223},
    {"strokes-groups", 2991439, 7521, 16, 16, 16, 2919631},
    {"strokes-sparse", 2875375, 2299, 6, 6, 6, 2855935},
    {"strokes-nothing", 2923039, 1790, 6, 6, 6, 2921807},
    {"strokes-dash-overlap", 3014447, 15949, 9, 9, 9, 2855935},
    {"strokes-tight-joins", 2885903, 4584, 7, 7, 7, 2855935},
    {"strokes-rings", 3000671, 4962, 8, 8, 8, 2855935},
    {"strokes-thin-aligned", 8466495, 9531, 11, 11, 11, 8408831},
    {"strokes-text", 9094623, 29759, 11, 11, 11, 8474591},
    {"strokes-text-outlines", 8735487, 23508, 5, 5, 5, 8408831},
    {"image-filters", 98095, 1964, 11, 11, 11, 96863},
    {"image-edges", 98095, 1964, 17, 17, 17, 96863},
    {"image-formats", 98095, 1964, 17, 17, 17, 96863},
    // image-spaces and image-compose: the work of an AREA fill with axis-aligned edges counts the rows its pixels cover across once per
    // period or once, not once each (the minified tile that took 883124 takes 3107), and no pixel of either render is different.
    {"image-spaces", 159791, 1964, 10, 10, 10, 158559},
    {"image-compose", 2948815, 3107, 12, 12, 12, 2919743},
    {"image-nothing", 98095, 1964, 5, 5, 5, 96863},
  };
  size_t checked = 0;
  for (const Row & row : before) {
    size_t index = RF_FIXTURE_COUNT;
    for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
      if (std::strcmp(RF_FIXTURES[i].name, row.name) == 0) {
        index = i;
      }
    }
    ASSERT_LT(index, RF_FIXTURE_COUNT) << row.name;
    Fixture fx(index);
    SinkRef sink;
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
    ReportRef report;
    GSHSCENE_Target target = target_of(r.raster, &fx.ctx.target_map);
    GSHSCENE_Render_Request req = request_of(fx.list, &target, report.report);
    size_t need = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
    EXPECT_EQ(need, row.workspace) << row.name;
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t in_place = 0;
    std::vector<unsigned char> workspace(need);
    req.workspace = workspace.data();
    req.workspace_bytes = need;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &in_place), GSHSCENE_OK);
    EXPECT_EQ(in_place, row.workspace) << row.name;
    req.flags = 0;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    GSHSCENE_Report_Summary sum = report.summary();
    EXPECT_EQ(sum.work_max, row.work_max) << row.name;
    EXPECT_EQ(sum.units_processed, row.processed) << row.name;
    EXPECT_EQ(sum.units_done, row.done) << row.name;
    EXPECT_EQ(sum.units_total, row.total) << row.name;
    EXPECT_EQ(sum.frame_bytes, row.frame_bytes) << row.name;
    checked++;
  }
  EXPECT_EQ(checked, 40u);
}

/* --- MK6: the frame ------------------------------------------------------------------------------------------------------ */

// The mask's picture is drawn under the chain of the maps of the instances around the group, then the group's placement, and
// the target's map is the last of the chain.  Whole pixels throughout, so that every comparison is of bytes.
TEST(RenderMask, TheMaskIsDrawnUnderThePlacementTheInstancesAndTheTargetsMap) {
  auto build = [](Scene & s, double unit, const GSHGEOM_Map * placement, const GSHGEOM_Map * instance) {
    const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 4 * unit, 4 * unit), white()));
    GSHSCENE_List * l = s.masked_fill(mask, s.rect(0, 0, 10 * unit, 10 * unit), red(), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, placement);
    if (!instance) {
      return l;
    }
    GSHSCENE_List * outer = s.open();
    EXPECT_EQ(gshscene_list_instance(outer, s.picture(l), instance, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(outer), GSHSCENE_OK);
    return outer;
  };
  {
    // (a) the placement moves the mask, not the group.
    Scene s;
    GSHGEOM_Map q = translate(5.0, 3.0);
    Draw d = draw(build(s, 1.0, &q, nullptr), 16, 16);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    for (uint32_t y = 0; y < 16; y++) {
      for (uint32_t x = 0; x < 16; x++) {
        const bool in = x >= 5 && x < 9 && y >= 3 && y < 7;
        EXPECT_EQ(d.at8(x, y, 3), in ? 255u : 0u) << x << "," << y;
      }
    }
  }
  {
    // (b) the group's own map (an instance) moves the mask with the group: the instance at (6, 2) under a placement of (1, 1).
    Scene s;
    GSHGEOM_Map q = translate(1.0, 1.0);
    GSHGEOM_Map at = translate(6.0, 2.0);
    Draw d = draw(build(s, 1.0, &q, &at), 20, 16);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    for (uint32_t y = 0; y < 16; y++) {
      for (uint32_t x = 0; x < 20; x++) {
        const bool in = x >= 7 && x < 11 && y >= 3 && y < 7;
        EXPECT_EQ(d.at8(x, y, 3), in ? 255u : 0u) << x << "," << y;
      }
    }
  }
  {
    // (c) the target's map scales the picture and the placement alike: the render equals the one of the list drawn at that scale.
    Scene a, b;
    GSHGEOM_Map q = translate(3.0, 3.0);
    GSHGEOM_Map q2 = translate(6.0, 6.0);
    GSHGEOM_Map doubling = scale(2.0, 2.0);
    Options mapped;
    mapped.map = &doubling;
    Draw m = draw(build(a, 1.0, &q, nullptr), 24, 24, mapped);
    Draw e = draw(build(b, 2.0, &q2, nullptr), 24, 24);
    ASSERT_EQ(m.result, GSHSCENE_OK);
    EXPECT_EQ(m.bytes, e.bytes);
    EXPECT_EQ(m.at8(7, 7, 3), 255u);
    EXPECT_EQ(m.at8(14, 14, 3), 0u);
  }
  {
    // (d) a singular placement makes every item of the picture degenerate: the mask is its outside value, and nothing else is drawn.
    Scene s;
    GSHSCENE_Mask m{};
    m.picture = s.picture_of(s.rect(0, 0, 4, 4), white());
    m.mode = GSHSCENE_MASK_ALPHA;
    GSHGEOM_Map flat = scale(0.0, 1.0);
    GSHSCENE_List * none = s.masked_fill(s.mask(m), s.rect(0, 0, 8, 8), red(), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &flat);
    Draw nothing = draw(none, 8, 8);
    ASSERT_EQ(nothing.result, GSHSCENE_OK);
    EXPECT_EQ(nothing.count(GSHSCENE_ENTRY_NOTICE), 0u) << "a fill with a singular map is silent (a notice is for a paint that needs the inverse)";
    EXPECT_EQ(nothing.bytes, std::string(8 * 8 * 4, '\0'));
    m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
    m.transfer.c0 = 0.5;
    m.transfer.c1 = 0.5;
    m.transfer.n = 1.0;
    GSHSCENE_List * constant = s.masked_fill(s.mask(m), s.rect(0, 0, 8, 8), red(), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &flat);
    Draw half = draw(constant, 8, 8);
    ASSERT_EQ(half.result, GSHSCENE_OK);
    for (uint32_t y = 0; y < 8; y++) {
      for (uint32_t x = 0; x < 8; x++) {
        EXPECT_EQ(half.at8(x, y, 3), 128u) << "the constant, everywhere";
      }
    }
    EXPECT_EQ(half.counters.mask_outside, 1u) << "FT-M1: the pop took the outside value for every pixel";
  }
}

// MC2: outside the damage nothing is touched, and a damage that holds none of the group (so that its pop region is empty)
// draws nothing; the mask is not drawn for it either.
TEST(RenderMask, ADamageThatMissesTheGroupTouchesNothingAndDrawsNoMask) {
  StarScene sc;
  const GSHSCENE_Rect corner = {28, 28, 32, 32};
  Options opt;
  opt.damage = &corner;
  Draw d = draw(sc.list, 32, 32, opt);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  const GSHSCENE_Rect group_only = {0, 0, 2, 2};
  opt.damage = &group_only;
  Draw far = draw(sc.list, 32, 32, opt);
  ASSERT_EQ(far.result, GSHSCENE_OK);
  EXPECT_EQ(far.counters.mask_pixels, 0u) << "no pixel of the group is in the window: the mask is drawn over nothing";
  for (uint32_t y = 0; y < 32; y++) {
    for (uint32_t x = 0; x < 32; x++) {
      const bool in = x >= 28 && x < 32 && y >= 28 && y < 32;
      if (!in) {
        EXPECT_EQ(d.px(x, y), std::string(4, '\0')) << "outside the damage nothing is touched: " << x << "," << y;
      }
    }
  }
}

/* --- MC10: no state ----------------------------------------------------------------------------------------------------- */

// A render keeps nothing about a mask after its pop: renders of masked lists on distinct surfaces, from several threads through
// one sink, give the bytes a lone render gives, and the sink holds no byte for them.
TEST(RenderMask, RendersOnDistinctSurfacesFromManyThreadsDoNotInteractThroughMasks) {
  Deep deep(3, false); // (strokes in a picture are cached as any stroke is, SC7: the sink then holds their expansions)
  StarScene star;
  Options lone;
  lone.backdrop = true;
  const Draw want_deep = draw(deep.list, 24, 24, lone);
  const Draw want_star = draw(star.list, 32, 32, lone);
  ASSERT_EQ(want_deep.result, GSHSCENE_OK);
  ASSERT_EQ(want_star.result, GSHSCENE_OK);
  SinkRef sink;
  uint64_t kept = 1;
  EXPECT_EQ(gshscene_sink_static_bytes(sink.sink, &kept), GSHSCENE_OK);
  EXPECT_EQ(kept, 0u) << "nothing is kept before a render";
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 6; t++) {
    threads.emplace_back([&, t]() {
      for (int k = 0; k < 6; k++) {
        const bool first = (t + k) % 2 == 0;
        const GSHSCENE_List * list = first ? deep.list : star.list;
        const uint32_t size = first ? 24 : 32;
        RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, size, size));
        ReportRef report;
        GSHSCENE_Target target = target_of(r.raster);
        GSHSCENE_Render_Request req = request_of(list, &target, report.report);
        if (gshscene_sink_render(sink.sink, &req) != GSHSCENE_OK || r.bytes() != (first ? want_deep.bytes : want_star.bytes)) {
          wrong++;
        }
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  EXPECT_EQ(wrong.load(), 0);
  EXPECT_EQ(gshscene_sink_static_bytes(sink.sink, &kept), GSHSCENE_OK);
  EXPECT_EQ(kept, 0u) << "a render keeps nothing about a mask: no cache, nothing held by the sink";
}

// MC5: the arena is sized by a mask picture's items at every depth: the largest item is in the picture of a mask inside a mask.
TEST(RenderMask, TheArenaIsSizedByTheItemsOfAMaskInsideAMaskToo) {
  Scene s;
  std::vector<double> zigzag;
  for (int i = 0; i < 600; i++) {
    zigzag.push_back(0.5 + 23.0 * (double)i / 599.0);
    zigzag.push_back(i % 2 ? 2.0 : 22.0);
  }
  const GSHSCENE_Resource * inner = s.alpha_mask(s.picture_of(s.polygon(zigzag), white()));
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, inner);
  ASSERT_EQ(gshscene_list_group_push(pl, &g, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint w = white();
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 24, 24), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(pl), GSHSCENE_OK);
  GSHSCENE_List * l = s.masked_fill(s.alpha_mask(s.picture(pl)), s.rect(0, 0, 24, 24), red());
  Draw ref = draw(l, 24, 24);
  Options caller;
  caller.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw out = draw(l, 24, 24, caller);
  ASSERT_EQ(ref.result, GSHSCENE_OK);
  EXPECT_EQ(out.result, GSHSCENE_OK) << "the exact memory holds the largest item of the mask inside the mask";
  EXPECT_EQ(out.bytes, ref.bytes);
}

// MC7: the work of the mask's units is the work of the group's pop: a mask picture holding a stroke of a long polyline (whose
// expansion alone is the worst unit) gives the render a work between two checks at least what that stroke is alone.
TEST(RenderMask, TheWorkOfTheMasksUnitsCountsInTheWorkBetweenTwoChecks) {
  Scene s;
  std::vector<double> zigzag;
  for (int i = 0; i < 600; i++) {
    zigzag.push_back(0.5 + 23.0 * (double)i / 599.0);
    zigzag.push_back(i % 2 ? 2.0 : 22.0);
  }
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.5;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  const GSHSCENE_Resource * line = s.polygon(zigzag);
  GSHSCENE_Paint w = white();
  GSHSCENE_List * alone = s.open();
  ASSERT_EQ(gshscene_list_stroke(alone, line, style, nullptr, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(alone), GSHSCENE_OK);
  GSHSCENE_List * pl = s.open();
  ASSERT_EQ(gshscene_list_stroke(pl, line, style, nullptr, &w, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_List * masked = s.masked_fill(s.alpha_mask(s.picture(pl)), s.rect(0, 0, 24, 24), red());
  Draw one = draw(alone, 24, 24);
  Draw two = draw(masked, 24, 24);
  ASSERT_EQ(one.result, GSHSCENE_OK);
  ASSERT_EQ(two.result, GSHSCENE_OK);
  EXPECT_GT(one.summary.work_max, 4u * 24u * 24u) << "the stroke is the worst unit";
  EXPECT_GE(two.summary.work_max, one.summary.work_max) << "and its work in the mask is counted";
}

// MC6, MC2: a masked group that draws nothing in the whole render has no mask drawn for it, and the group before it at the same
// depth leaves nothing of its own behind.
TEST(RenderMask, AMaskedGroupThatDrawsNothingDrawsNoMask) {
  Scene s;
  const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 16, 16), white()));
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint r = red();
  GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
  for (double x : {2.0, 400.0}) { /* the second group's fill is far off the 16 by 16 target */
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(x, 2, x + 8, 10), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Draw d = draw(l, 16, 16);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.masks, 1u) << "the group that draws nothing has no mask drawn for it";
  EXPECT_EQ(d.counters.mask_pixels, 8u * 8u);
  EXPECT_EQ(d.summary.units_done, d.summary.units_processed) << "and its pop closes the group: every unit is in the target";
  EXPECT_EQ(d.summary.units_done, 6u);
}

// MC6, MC2: a masked group inside a mask's picture is drawn when the whole render would draw it, which is decided by the whole
// render's pop region of the outer group (the pixels the outer mask is drawn over), not by the whole region.  The outer group is
// a few pixels at (30, 30); its mask holds a clipped corner (0, 0) to (8, 8), with an unbounded group in it whose own mask holds
// a diamond past the cell cap.  The whole render draws the outer mask over (30, 30) to (34, 34), so the clip leaves the inner
// group nothing to compose and its mask is never drawn: nothing is refused, whole or damaged.
TEST(RenderMask, AMaskedGroupInsideAMaskIsDrawnOnlyWhereTheWholeRenderDrawsIt) {
  Scene s;
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{32.0, 1.0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{62.0, 32.25, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{32.5, 62.0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{1.0, 31.75, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * inner_mask = s.alpha_mask(s.picture_of(s.path(p), white()));
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Paint w = white();
  ASSERT_EQ(gshscene_list_clip_push(pl, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group inner = s.group(1.0, GSHSCENE_COMPOSITE_DEST_IN, inner_mask);
  ASSERT_EQ(gshscene_list_group_push(pl, &inner, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(pl), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(pl), GSHSCENE_OK);
  const GSHSCENE_Resource * outer_mask = s.alpha_mask(s.picture(pl));
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint r = red();
  GSHSCENE_Group outer = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, outer_mask);
  ASSERT_EQ(gshscene_list_group_push(l, &outer, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(30, 30, 34, 34), nullptr, GSHGEOM_FILL_NONZERO, &r, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_deposits = 40;
  const GSHSCENE_Rect windows[] = {{0, 0, 64, 64}, {30, 30, 34, 34}, {0, 0, 8, 8}, {20, 20, 40, 40}};
  for (const GSHSCENE_Rect & win : windows) {
    Options opt;
    opt.limits = &limits;
    opt.damage = &win;
    Draw d = draw(l, 64, 64, opt);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u) << "the inner mask is not drawn, so nothing in it is refused (window " << win.x0 << "," << win.y0 << ")";
    EXPECT_EQ(d.counters.masks, 1u) << "only the outer mask, drawn whenever the whole render would draw it (over an empty window where the damage holds none of the group)";
  }
}

namespace {

/** An allocator that sets a cancel token on its @c at th call: a cancel that lands where the test chooses, inside the mask pass. */
struct CancelAt {
  long calls = 0;
  long at = -1;
  GSHSCENE_Cancel * token = nullptr;
  void count() {
    if (calls++ == at) {
      gshscene_cancel_set(token);
    }
  }
};

void * ca_malloc(void * ctx, size_t n) {
  ((CancelAt *)ctx)->count();
  return std::malloc(n ? n : 1);
}

void * ca_calloc(void * ctx, size_t k, size_t n) {
  ((CancelAt *)ctx)->count();
  return std::calloc(k ? k : 1, n ? n : 1);
}

void * ca_realloc(void * ctx, void * old, size_t n) {
  ((CancelAt *)ctx)->count();
  return std::realloc(old, n ? n : 1);
}

void ca_free(void *, void * p) {
  std::free(p);
}

} // namespace

// MC7: cancel is checked between the mask's units.  The mask's picture holds a stroke of a long polyline (a unit whose work is
// the most of any) and then a fill the sink refuses (more points than its cap).  A token set while the stroke is drawn stops
// the pass at the check after it: the second unit is never gone through (the refusal it would report is absent), the group is
// not composited, and the work counted is the stroke's: that landing exists, for some call of the allocator, and without the
// check there is none.
TEST(RenderMask, ACancelInsideTheMaskPassStopsBetweenItsUnits) {
  Scene s;
  std::vector<double> zigzag, huge;
  for (int i = 0; i < 300; i++) {
    zigzag.push_back(0.5 + 23.0 * (double)i / 299.0);
    zigzag.push_back(i % 2 ? 2.0 : 22.0);
  }
  for (int i = 0; i < 6000; i++) {
    huge.push_back(12.0 + 11.0 * std::cos(0.001 * (double)i));
    huge.push_back(12.0 + 11.0 * std::sin(0.001 * (double)i * 7.0));
  }
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.5;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHSCENE_Paint w = white();
  GSHSCENE_List * alone = s.open();
  const GSHSCENE_Resource * line = s.polygon(zigzag);
  ASSERT_EQ(gshscene_list_stroke(alone, line, style, nullptr, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(alone), GSHSCENE_OK);
  GSHSCENE_List * pl = s.open();
  ASSERT_EQ(gshscene_list_stroke(pl, line, style, nullptr, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(pl, s.polygon(huge), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_List * l = s.masked_fill(s.alpha_mask(s.picture(pl)), s.rect(0, 0, 24, 24), red());
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 4000;
  Options base;
  base.limits = &limits;
  Draw stroke_alone = draw(alone, 24, 24, base);
  ASSERT_EQ(stroke_alone.result, GSHSCENE_OK);
  Draw full = draw(l, 24, 24, base);
  ASSERT_EQ(full.result, GSHSCENE_OK);
  ASSERT_EQ(full.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u) << "the second unit of the mask is refused";
  int landed = 0;
  for (long k = 0; k < 400; k++) {
    GSHSCENE_Cancel cancel;
    gshscene_cancel_init(&cancel);
    CancelAt at;
    at.at = k;
    at.token = &cancel;
    GSHSCENE_Allocator alloc;
    alloc.ctx = &at;
    alloc.malloc_fn = ca_malloc;
    alloc.calloc_fn = ca_calloc;
    alloc.realloc_fn = ca_realloc;
    alloc.free_fn = ca_free;
    Options opt = base;
    opt.alloc = &alloc;
    opt.cancel = &cancel;
    Draw d = draw(l, 24, 24, opt);
    if (d.result == GSHSCENE_ERR_CANCELLED && d.count(GSHSCENE_ENTRY_REFUSED) == 0 && d.counters.masks == 0 &&
        d.summary.work_max >= stroke_alone.summary.work_max) {
      landed++;
    }
  }
  EXPECT_GT(landed, 0) << "a cancel inside the first unit of the mask stops the pass before its second unit";
}

// MC5: the arena is sized with the chain of maps of the mask's items: a picture instanced under a large scale inside a mask's
// picture flattens to many more points than the same curve at unit scale, and the need counts them.
TEST(RenderMask, TheArenaIsSizedUnderTheMapsOfAnInstanceInsideTheMasksPicture) {
  Scene s;
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  const double k = 0.5522847498307936 * 2.0;
  gshgeom_path_move_to(p, GSHGEOM_Point{2.0, 0.0, 0});
  gshgeom_path_cubic_to(p, GSHGEOM_Point{2.0, k, 0}, GSHGEOM_Point{k, 2.0, 0}, GSHGEOM_Point{0.0, 2.0, 0});
  gshgeom_path_cubic_to(p, GSHGEOM_Point{-k, 2.0, 0}, GSHGEOM_Point{-2.0, k, 0}, GSHGEOM_Point{-2.0, 0.0, 0});
  gshgeom_path_cubic_to(p, GSHGEOM_Point{-2.0, -k, 0}, GSHGEOM_Point{-k, -2.0, 0}, GSHGEOM_Point{0.0, -2.0, 0});
  gshgeom_path_cubic_to(p, GSHGEOM_Point{k, -2.0, 0}, GSHGEOM_Point{2.0, -k, 0}, GSHGEOM_Point{2.0, 0.0, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * circle = s.picture_of(s.path(p), white());
  GSHSCENE_List * pl = s.open();
  GSHGEOM_Map big = scale(5.0, 5.0);
  GSHGEOM_Map into = translate(12.0, 12.0);
  GSHGEOM_Map placed;
  GSHGEOM_Map two[2] = {into, big};
  ASSERT_EQ(gshgeom_map_compose_chain(two, 2, nullptr, &placed), GSHGEOM_OK);
  ASSERT_EQ(gshscene_list_instance(pl, circle, &placed, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_List * l = s.masked_fill(s.alpha_mask(s.picture(pl)), s.rect(0, 0, 24, 24), red());
  Draw ref = draw(l, 24, 24);
  Options caller;
  caller.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw out = draw(l, 24, 24, caller);
  ASSERT_EQ(ref.result, GSHSCENE_OK);
  EXPECT_EQ(out.result, GSHSCENE_OK) << "the exact memory holds the circle flattened at the instance's scale";
  EXPECT_EQ(out.bytes, ref.bytes);
  EXPECT_GT(ref.at8(12, 12, 3), 0u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
