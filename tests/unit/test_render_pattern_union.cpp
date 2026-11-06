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
 * A union clip in a pattern's tile (shape-exact-primitives story CU21; render.h, PC3 and PC4 with CG1 to CG12): the general tier forms the
 * union in each copy, its members cut to the cell, and the copies' areas add.  The tile is the same picture as the content drawn directly
 * (test_render_pattern_rules.cpp holds that equality over many lattices and tiles) and agrees with shape-scene's oracle
 * (test_render_pattern_vectors.cpp); this file holds what neither can see: the tiers the pass takes, how the predicate is billed, the
 * limit, the caller's memory, a damage, and the cut.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace cuh;

namespace {

constexpr unsigned kAll = GSHCPU_RENDER_TIERS_ALL;

/** The members of the tile's union: @p overlap three that cross, else three that stand apart (provably disjoint boxes). */
std::vector<GSHSCENE_Clip_Member> members_of(Scene & s, bool overlap) {
  if (overlap) {
    return {member(s.rect(1.5, 1.5, 4.5, 4.5)), member(s.rect(3.0, 2.25, 6.5, 5.0)), member(s.polygon({4.0, 3.5, 6.5, 6.25, 3.25, 6.0}))};
  }
  return {member(s.rect(1.5, 1.5, 3.0, 3.0)), member(s.rect(4.0, 1.5, 6.0, 3.0)), member(s.rect(2.0, 4.5, 6.0, 6.5))};
}

/** The cell (0, 0, 8, 8)'s tile: a fill of the interior under the union of @p members, or (@p members empty) under nothing. */
const GSHSCENE_Resource * tile_of(Scene & s, const std::vector<GSHSCENE_Clip_Member> & members, double alpha = 0.9) {
  GSHSCENE_List * t = s.open();
  GSHSCENE_Paint p = rh::solid(0.8, 0.2, 0.3, alpha);
  if (!members.empty()) {
    EXPECT_EQ(gshscene_list_clip_push(t, make_union(s, members), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_fill(t, s.rect(1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  if (!members.empty()) {
    EXPECT_EQ(gshscene_list_clip_pop(t), GSHSCENE_OK);
  }
  return s.picture(t);
}

const GSHSCENE_Resource * pattern_of(Scene & s, const GSHSCENE_Resource * tile, double cell, double step, double tx, double ty) {
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = tile;
  pt.w = cell;
  pt.h = cell;
  pt.x_step = step;
  pt.y_step = step;
  EXPECT_EQ(gshgeom_map_translate(2, tx, ty, 0.0, &pt.map), GSHGEOM_OK);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_pattern(s.store, &pt, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

/** A list filling the rectangle (2, 2, w - 2, h - 2) with @p pattern. */
GSHSCENE_List * use_of(Scene & s, const GSHSCENE_Resource * pattern, double w, double h) {
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint pp{};
  pp.kind = GSHSCENE_PAINT_PATTERN;
  pp.resource = pattern;
  pp.rgba[3] = 1.0;
  EXPECT_EQ(gshscene_list_fill(l, s.rect(2, 2, w - 2, h - 2), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

constexpr uint32_t kW = 72;
constexpr uint32_t kH = 56;

} // namespace

TEST(RenderPatternUnion, ATileUnionIsFormedForEachCopyByTheGeneralTierAndThePeriodTierIsNotTaken) {
  // CU21: the union in a tile is the general tier's in each copy, whatever the predicate shows of its members, and a tile that holds one is
  // not drawn by the period window (its limits are those of the groups in a copy, which the window's copies do not vouch for).  The same
  // tile without the union clip is drawn by the window: the control that shows the lattice is eligible.
  Scene s;
  const GSHSCENE_Resource * crossing = tile_of(s, members_of(s, true));
  const GSHSCENE_Resource * apart = tile_of(s, members_of(s, false));
  const GSHSCENE_Resource * plain = tile_of(s, {});
  Draw control = draw(use_of(s, pattern_of(s, plain, 8, 8, 0.3, 0.45), kW, kH), kW, kH);
  ASSERT_EQ(control.result, GSHSCENE_OK);
  EXPECT_GT(control.counters.pattern_period, 0u) << "the lattice is eligible for the window";
  EXPECT_EQ(control.counters.union_general + control.counters.union_fast, 0u);

  Draw d = draw(use_of(s, pattern_of(s, crossing, 8, 8, 0.3, 0.45), kW, kH), kW, kH);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_EQ(d.counters.pattern_period, 0u) << "a tile with a union clip is not the window's";
  EXPECT_EQ(d.counters.period_aborts, 0u) << "it is not begun and given up: it is never eligible";
  EXPECT_GT(d.counters.patterns, 0u);
  EXPECT_GT(d.counters.union_general, 0u);
  EXPECT_GT(d.counters.union_groups, 0u) << "the crossing members are one group";
  EXPECT_GT(d.counters.union_exact, 0u) << "and its pixels are made exact";
  EXPECT_EQ(d.counters.union_fast, 0u);

  // Members the predicate shows disjoint are not the fast tier's in a tile: the tile forms every union by the general one.
  Draw a = draw(use_of(s, pattern_of(s, apart, 8, 8, 0.3, 0.45), kW, kH), kW, kH);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  EXPECT_EQ(a.counters.union_fast, 0u);
  EXPECT_GT(a.counters.union_general, 0u);
  EXPECT_EQ(a.counters.union_groups, 0u) << "three groups of one";
  EXPECT_GT(a.counters.union_sweeps, 0u);
  EXPECT_EQ(a.counters.pattern_period, 0u);
}

TEST(RenderPatternUnion, NoTierSwitchChangesATileUnion) {
  // The tier switches choose how fast a result is made and never which: the fast tier's off, the period window's off (it is not taken here),
  // the cache's off, and the exact step forced to take every union as one group, all draw the same bytes.
  for (bool overlap : {true, false}) {
    Scene s;
    GSHSCENE_List * l = use_of(s, pattern_of(s, tile_of(s, members_of(s, overlap)), 8, 8, 0.3, 0.45), kW, kH);
    Draw base = draw(l, kW, kH);
    ASSERT_EQ(base.result, GSHSCENE_OK);
    for (unsigned tiers : {kAll & ~(unsigned)GSHCPU_RENDER_TIER_UNION, kAll & ~(unsigned)GSHCPU_RENDER_TIER_PERIOD,
             kAll & ~(unsigned)GSHCPU_RENDER_TIER_CACHE, (unsigned)((kAll & ~GSHCPU_RENDER_TIER_UNION) | GSHCPU_RENDER_FORCE_ONE_GROUP)}) {
      Options o;
      o.tiers = tiers;
      Draw d = draw(l, kW, kH, o);
      ASSERT_EQ(d.result, GSHSCENE_OK) << tiers;
      EXPECT_EQ(d.bytes, base.bytes) << "overlap " << overlap << " tiers " << tiers;
    }
  }
}

TEST(RenderPatternUnion, ThePredicateIsBilledOncePerPushWhateverNumberOfCopiesAreWalked) {
  // CU19, CU21: the comparisons of a union push are billed once.  A tile of overlapping cells (PC6) is walked once for each copy, and a
  // tile inside the tile of overlapping cells once for each of those; the count must not depend on how many copies the render draws, or
  // `max_union_tests` would refuse a render for the size of its target.
  uint64_t once = 0;
  for (double step : {8.0, 6.0, 5.0}) {
    Scene s;
    GSHSCENE_List * l = use_of(s, pattern_of(s, tile_of(s, members_of(s, true)), 8, step, 0.3, 0.45), kW, kH);
    Draw d = draw(l, kW, kH);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    ASSERT_GT(d.counters.union_tests, 0u);
    if (step == 8.0) {
      once = d.counters.union_tests;
    }
    EXPECT_EQ(d.counters.union_tests, once) << "step " << step;
    // The limit that passes is exactly the count, and one below refuses the push: the same on every lattice.
    GSHCPU_Limits lim;
    gshcpu_limits_default(&lim);
    lim.max_union_tests = once;
    Options o;
    o.limits = &lim;
    EXPECT_EQ(draw(l, kW, kH, o).count(GSHSCENE_ENTRY_REFUSED), 0u) << "step " << step;
    lim.max_union_tests = once - 1u;
    EXPECT_EQ(draw(l, kW, kH, o).count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u) << "step " << step;
  }
  // A second use of the same lattice is a pass of its own and is billed again: the walks of the first do not suppress its first.
  {
    Scene s;
    const GSHSCENE_Resource * p = pattern_of(s, tile_of(s, members_of(s, true)), 8, 6, 0.3, 0.45);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint pp{};
    pp.kind = GSHSCENE_PAINT_PATTERN;
    pp.resource = p;
    pp.rgba[3] = 1.0;
    ASSERT_EQ(gshscene_list_fill(l, s.rect(2, 2, 40, 40), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(30, 10, 70, 50), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Draw d = draw(l, kW, kH);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_EQ(d.counters.union_tests, 2u * once) << "two uses, two passes";
  }
  // A tile in a tile is another pass for each copy of the outer one that paints it (PC8 counts its copies so), and each of those is billed once
  // whether or not the outer cells overlap.
  for (double outer_step : {16.0, 12.0}) {
    Scene s;
    const GSHSCENE_Resource * inner = pattern_of(s, tile_of(s, members_of(s, true)), 8, 8, 0.0, 0.0);
    GSHSCENE_List * t = s.open();
    GSHSCENE_Paint pp{};
    pp.kind = GSHSCENE_PAINT_PATTERN;
    pp.resource = inner;
    pp.rgba[3] = 1.0;
    ASSERT_EQ(gshscene_list_fill(t, s.rect(0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
    const GSHSCENE_Resource * outer = pattern_of(s, s.picture(t), 16, outer_step, 0.3, 0.45);
    Draw d = draw(use_of(s, outer, kW, kH), kW, kH);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    ASSERT_GT(d.counters.patterns, 1u);
    EXPECT_EQ(d.counters.union_tests, once * (d.counters.patterns - 1u)) << "outer step " << outer_step << ": one pass of the outer use, the rest the inner";
  }
}

TEST(RenderPatternUnion, ALimitOfTheUnionRefusesTheUseAndNeverDrawsTheTileWithoutIt) {
  // CG9, PC8: a union over a limit is the sink's resource limit for the use painted with the pattern; the tile is not drawn with the union
  // left out or with its clip ignored, and strict fails there.  The same render under the default limits draws.
  Scene s;
  GSHSCENE_List * l = use_of(s, pattern_of(s, tile_of(s, members_of(s, true)), 8, 8, 0.3, 0.45), kW, kH);
  Draw ok = draw(l, kW, kH);
  ASSERT_EQ(ok.result, GSHSCENE_OK);
  ASSERT_EQ(ok.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_TRUE(std::any_of(ok.bytes.begin(), ok.bytes.end(), [](char c) { return c != 0; }));
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_union_pieces = 1;
  Options o;
  o.limits = &lim;
  Draw d = draw(l, kW, kH, o);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
  EXPECT_TRUE(std::all_of(d.bytes.begin(), d.bytes.end(), [](char c) { return c == 0; })) << "the use is skipped: nothing is drawn";
  Options strict = o;
  strict.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(draw(l, kW, kH, strict).result, GSHSCENE_ERR_REFUSED);
  // Whatever the damage: the union of a copy that meets the damage is refused as the whole render's is.
  GSHSCENE_Rect damage{20, 14, 50, 40};
  Options damaged = o;
  damaged.damage = &damage;
  EXPECT_EQ(draw(l, kW, kH, damaged).count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT), 1u);
}

TEST(RenderPatternUnion, ADamagedRenderEqualsTheWholeOneOnItsPart) {
  // S4: the pixels a damage names are the pixels of the whole render, the union of every copy formed over its own rectangle.
  for (bool overlap : {true, false}) {
    Scene s;
    GSHSCENE_List * l = use_of(s, pattern_of(s, tile_of(s, members_of(s, overlap)), 8, 8, 0.3, 0.45), kW, kH);
    Draw whole = draw(l, kW, kH);
    ASSERT_EQ(whole.result, GSHSCENE_OK);
    for (const GSHSCENE_Rect & r : {GSHSCENE_Rect{20, 14, 50, 40}, GSHSCENE_Rect{9, 9, 10, 10}, GSHSCENE_Rect{31, 0, 33, 56}}) {
      GSHSCENE_Rect damage = r;
      Options o;
      o.damage = &damage;
      o.backdrop = false;
      Draw part = draw(l, kW, kH, o);
      ASSERT_EQ(part.result, GSHSCENE_OK);
      for (int64_t y = r.y0; y < r.y1; y++) {
        for (int64_t x = r.x0; x < r.x1; x++) {
          EXPECT_EQ(part.px((uint32_t)x, (uint32_t)y), whole.px((uint32_t)x, (uint32_t)y)) << x << "," << y << " overlap " << overlap;
        }
      }
    }
  }
}

TEST(RenderPatternUnion, ARenderInTheCallersMemoryIsTheSameForAUnionOfAnyMembers) {
  // CU20: the measure takes the general tier's memory for a union in a tile, whatever the predicate shows of its members (the tile never
  // uses the fast tier), so the block the query gives is enough and the render in it is the render without it.
  for (bool overlap : {true, false}) {
    for (bool nested : {false, true}) {
      Scene s;
      const GSHSCENE_Resource * tile = tile_of(s, members_of(s, overlap));
      const GSHSCENE_Resource * inner = pattern_of(s, tile, 8, 8, 0.3, 0.45);
      GSHSCENE_List * l = nullptr;
      if (nested) {
        GSHSCENE_List * t = s.open();
        GSHSCENE_Paint pp{};
        pp.kind = GSHSCENE_PAINT_PATTERN;
        pp.resource = inner;
        pp.rgba[3] = 1.0;
        EXPECT_EQ(gshscene_list_fill(t, s.rect(0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
        l = use_of(s, pattern_of(s, s.picture(t), 16, 12, 0.5, 0.25), kW, kH);
      } else {
        l = use_of(s, inner, kW, kH);
      }
      Draw plain = draw(l, kW, kH);
      ASSERT_EQ(plain.result, GSHSCENE_OK);
      Options cm;
      cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
      Draw in_memory = draw(l, kW, kH, cm);
      ASSERT_EQ(in_memory.result, GSHSCENE_OK) << "overlap " << overlap << " nested " << nested;
      EXPECT_EQ(in_memory.count(GSHSCENE_ENTRY_REFUSED), 0u);
      EXPECT_EQ(in_memory.bytes, plain.bytes);
      EXPECT_GT(in_memory.need, 0u);
    }
  }
}

TEST(RenderPatternUnion, TheMembersAreCutToTheCellAndTheCopiesAreaAddAtASeam) {
  // PC3, PC4, CK1: a union whose members reach far past the cell covers each copy's cell whole and nothing of the next, so at the pixel
  // a cell edge crosses the two copies' areas are half and half, each weighed by a coverage of a half (a weighed copy adds its weighed
  // byte, PC4): the seam is the sum of two products, about a half of the interior's value, where members that were not cut would weigh
  // each copy by a whole pixel and add to the whole.
  Scene s;
  GSHSCENE_List * t = s.open();
  GSHSCENE_Paint p = rh::solid(0.0, 0.0, 0.0, 1.0);
  ASSERT_EQ(gshscene_list_clip_push(t, make_union(s, {member(s.rect(-100, -100, 100, 0.5)), member(s.rect(-100, 0, 100, 100))}), nullptr,
                GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(t, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(t), GSHSCENE_OK);
  GSHSCENE_List * l = use_of(s, pattern_of(s, s.picture(t), 8, 8, 0.5, 0.0), kW, kH);
  Draw d = draw(l, kW, kH);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  // The cells' vertical edges are at x = 0.5 + 8k: the pixel columns 8, 16, ... hold two copies, half each; the others hold one.
  for (uint32_t y = 4; y < 50; y += 5) {
    EXPECT_EQ(d.at8(12, y, 3), 255u) << "a pixel inside one cell";
    for (uint32_t x : {16u, 24u, 32u}) {
      EXPECT_GE(d.at8(x, y, 3), 100u) << x << "," << y;
      EXPECT_LE(d.at8(x, y, 3), 160u) << "about a half: two half areas each weighed by a half";
    }
  }
}

TEST(RenderPatternUnion, ThePiecesAGroupKeepsAreCountedOverTheWholeCopyWhateverTheDamage) {
  // S4, CG9: the work of a union is over the whole render's rectangle of its copy, so the smallest `max_union_pieces` that draws is the same
  // for a damage that cuts every copy to a corner as for the whole render.
  Scene s;
  GSHSCENE_List * l = use_of(s, pattern_of(s, tile_of(s, members_of(s, true)), 8, 8, 0.3, 0.45), kW, kH);
  auto smallest = [&](const GSHSCENE_Rect * damage) {
    for (uint64_t limit = 1; limit < 400; limit++) {
      GSHCPU_Limits lim;
      gshcpu_limits_default(&lim);
      lim.max_union_pieces = limit;
      Options o;
      o.limits = &lim;
      o.damage = damage;
      if (draw(l, kW, kH, o).count(GSHSCENE_ENTRY_REFUSED) == 0) {
        return limit;
      }
    }
    return (uint64_t)0;
  };
  const uint64_t whole = smallest(nullptr);
  ASSERT_GT(whole, 1u);
  ASSERT_LT(whole, 400u);
  GSHSCENE_Rect corner{27, 21, 31, 25}; // a few pixels of one copy
  EXPECT_EQ(smallest(&corner), whole);
  GSHSCENE_Rect strip{0, 30, 72, 33};
  EXPECT_EQ(smallest(&strip), whole);
}

TEST(RenderPatternUnion, TheCopiesAreasOfAUnionAddAndTheByteOfTheSumIsMadeOnce) {
  // PC4, F3, CU21: a union clip that reaches the cell's edges, around a group, weighs the group at its pop by the sum of its copies' areas: the
  // lattice at (19/64, 29/64) of a pixel gives the pixel at a corner of four cells the areas 1102, 1330, 2610 and 3150 in 1/8192 pixel, which
  // sum to the whole pixel (the sum of the four bytes is 254, one short of it).  Without the group the copies weigh the fill by a byte each
  // and add the weighed bytes: 5 + 7 + 26 + 38 = 76 (the same as a clip of the cell).
  for (int variant = 0; variant < 2; variant++) {
    Scene s;
    GSHSCENE_List * t = s.open();
    GSHSCENE_Paint p = rh::solid(0.2, 0.5, 0.8, 1.0);
    EXPECT_EQ(gshscene_list_clip_push(t, make_union(s, {member(s.rect(0, 0, 4, 8)), member(s.rect(4, 0, 8, 8))}), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0),
        GSHSCENE_OK);
    if (variant == 1) {
      GSHSCENE_Group g = s.group(1.0);
      EXPECT_EQ(gshscene_list_group_push(t, &g, 0, 0.0), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_fill(t, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    if (variant == 1) {
      EXPECT_EQ(gshscene_list_group_pop(t), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_clip_pop(t), GSHSCENE_OK);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint pp{};
    pp.kind = GSHSCENE_PAINT_PATTERN;
    pp.resource = pattern_of(s, s.picture(t), 8, 8, 19.0 / 64.0, 29.0 / 64.0);
    pp.rgba[3] = 1.0;
    EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, 40, 32), nullptr, GSHGEOM_FILL_NONZERO, &pp, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Options o;
    o.format = GSHCPU_FORMAT_RGBAF32_STRAIGHT;
    Draw d = draw(l, 40, 32, o);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    for (uint32_t x : {8u, 16u, 24u, 32u}) {
      EXPECT_NEAR(d.atf(x, 8, 3) * 255.0f, variant == 0 ? 76.0f : 255.0f, 0.01f) << "variant " << variant << " corner " << x;
    }
    // A damage that begins inside a cell: the clip block's plane of the copies in it starts past the start of the copy's own region, and the
    // pixels the damage names are the whole render's.
    GSHSCENE_Rect damage{13, 3, 27, 20};
    o.damage = &damage;
    Draw part = draw(l, 40, 32, o);
    ASSERT_EQ(part.result, GSHSCENE_OK);
    for (uint32_t y = 3; y < 20; y++) {
      for (uint32_t x = 13; x < 27; x++) {
        EXPECT_EQ(part.px(x, y), d.px(x, y)) << "variant " << variant << " " << x << "," << y;
      }
    }
  }
}

TEST(RenderPatternUnion, AMemberThatOverlapsItselfBesideAnAbuttingOneIsTheSameInATileAndDrawnDirectly) {
  // The witness for the hold at the whole pixel in a tile: a pixel's plane can read more than itself (a member that overlaps itself reads its
  // winding area, held at the pixel, and an abutting group adds its own), and the tile takes it as the pixel.  The tile and the picture drawn at
  // each copy are the same bytes.
  Scene s;
  GSHGEOM_Path * path = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &path);
  for (int pass = 0; pass < 2; pass++) { // the same square twice in one path: winding 2 inside it
    gshgeom_path_move_to(path, GSHGEOM_Point{1.25, 1.25, 0});
    gshgeom_path_line_to(path, GSHGEOM_Point{3.6, 1.25, 0});
    gshgeom_path_line_to(path, GSHGEOM_Point{3.6, 3.7, 0});
    gshgeom_path_line_to(path, GSHGEOM_Point{1.25, 3.7, 0});
    gshgeom_path_close(path);
  }
  const GSHSCENE_Resource * doubled = s.path(path);
  GSHSCENE_List * t = s.open();
  GSHSCENE_Paint p = rh::solid(0.2, 0.5, 0.8, 1.0);
  ASSERT_EQ(gshscene_list_clip_push(t, make_union(s, {member(doubled), member(s.rect(3.6, 1.25, 6.0, 3.7)), member(s.rect(1.25, 3.7, 6.0, 6.0))}),
                nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(t, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(t), GSHSCENE_OK);
  const GSHSCENE_Resource * tile = s.picture(t);
  Draw tiled = draw(use_of(s, pattern_of(s, tile, 8, 8, 0.0, 0.0), 40, 32), 40, 32);
  GSHSCENE_List * direct = s.open();
  for (double y : {0.0, 8.0, 16.0, 24.0}) {
    for (double x : {0.0, 8.0, 16.0, 24.0, 32.0}) {
      GSHGEOM_Map at;
      ASSERT_EQ(gshgeom_map_translate(2, x, y, 0.0, &at), GSHGEOM_OK);
      ASSERT_EQ(gshscene_list_instance(direct, tile, &at, 0, 0.0), GSHSCENE_OK);
    }
  }
  ASSERT_EQ(gshscene_list_close(direct), GSHSCENE_OK);
  // The use fills (2, 2) to (38, 30): the instances cover the same pixels, cut to the use's rectangle by a clip.
  GSHSCENE_List * cut = s.open();
  ASSERT_EQ(gshscene_list_clip_push(cut, s.rect(2, 2, 38, 30), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  for (double y : {0.0, 8.0, 16.0, 24.0}) {
    for (double x : {0.0, 8.0, 16.0, 24.0, 32.0}) {
      GSHGEOM_Map at;
      ASSERT_EQ(gshgeom_map_translate(2, x, y, 0.0, &at), GSHGEOM_OK);
      ASSERT_EQ(gshscene_list_instance(cut, tile, &at, 0, 0.0), GSHSCENE_OK);
    }
  }
  ASSERT_EQ(gshscene_list_clip_pop(cut), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(cut), GSHSCENE_OK);
  Draw drawn = draw(cut, 40, 32);
  ASSERT_EQ(tiled.result, GSHSCENE_OK);
  ASSERT_EQ(drawn.result, GSHSCENE_OK);
  EXPECT_EQ(tiled.bytes, drawn.bytes);
  EXPECT_EQ(tiled.counters.union_groups, 0u) << "three members that only touch: three groups of one, whose areas add";
  EXPECT_GT(tiled.counters.union_sweeps, 0u);
}

TEST(RenderPatternUnion, AMaskedGroupInATileOverAUnionIsMeasuredAndDrawnInTheCallersMemory) {
  // CU20: a union in the picture of a mask that a tile's group takes is measured as a tile's: the general tier's memory, whatever the members' boxes.
  for (bool overlap : {true, false}) {
    Scene s;
    GSHSCENE_List * mp = s.open();
    GSHSCENE_Paint white_paint = rh::solid(1, 1, 1, 1);
    EXPECT_EQ(gshscene_list_clip_push(mp, make_union(s, members_of(s, overlap)), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(mp, s.rect(1.5, 1.5, 6.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, &white_paint, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_clip_pop(mp), GSHSCENE_OK);
    const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(mp));
    GSHSCENE_List * t = s.open();
    GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
    GSHSCENE_Paint p = rh::solid(0.8, 0.2, 0.3, 0.9);
    EXPECT_EQ(gshscene_list_group_push(t, &g, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(t, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(t), GSHSCENE_OK);
    GSHSCENE_List * l = use_of(s, pattern_of(s, s.picture(t), 8, 8, 0.3, 0.45), kW, kH);
    Draw plain = draw(l, kW, kH);
    Options cm;
    cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    Draw in_memory = draw(l, kW, kH, cm);
    ASSERT_EQ(plain.result, GSHSCENE_OK);
    ASSERT_EQ(in_memory.result, GSHSCENE_OK) << "overlap " << overlap;
    EXPECT_EQ(plain.bytes, in_memory.bytes);
    EXPECT_EQ(plain.count(GSHSCENE_ENTRY_REFUSED), 0u);
  }
}

TEST(RenderPatternUnion, AWideShortTargetsMeasureIsTheGeneralTiersWhateverTheMembersBoxesShow) {
  // CU20: the accumulator of the general tier is as wide as the target and many rows deep, where the fast tier's plane is two bytes a pixel; on a
  // target a few pixels tall the first is many times the second, so a measure that took a tile's disjoint union for the fast tier's would be short.
  // The tile holds the union directly (a) and in the picture of a mask that a group takes (b).
  for (int where = 0; where < 2; where++) {
    Scene s;
    GSHSCENE_List * t = s.open();
    GSHSCENE_Paint p = rh::solid(0.8, 0.2, 0.3, 0.9);
    const GSHSCENE_Resource * u = make_union(s, members_of(s, false));
    GSHSCENE_Group g{};
    if (where == 0) {
      EXPECT_EQ(gshscene_list_clip_push(t, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    } else {
      GSHSCENE_List * mp = s.open();
      GSHSCENE_Paint w = rh::solid(1, 1, 1, 1);
      EXPECT_EQ(gshscene_list_clip_push(mp, u, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_fill(mp, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &w, 0, 0.0), GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_clip_pop(mp), GSHSCENE_OK);
      g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, s.alpha_mask(s.picture(mp)));
      EXPECT_EQ(gshscene_list_group_push(t, &g, 0, 0.0), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_fill(t, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(where == 0 ? gshscene_list_clip_pop(t) : gshscene_list_group_pop(t), GSHSCENE_OK);
    GSHSCENE_List * l = use_of(s, pattern_of(s, s.picture(t), 8, 8, 0.3, 0.45), 4000, 6);
    Draw plain = draw(l, 4000, 6);
    Options cm;
    cm.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    Draw in_memory = draw(l, 4000, 6, cm);
    ASSERT_EQ(plain.result, GSHSCENE_OK);
    ASSERT_EQ(in_memory.result, GSHSCENE_OK) << "where " << where;
    EXPECT_EQ(plain.bytes, in_memory.bytes);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
