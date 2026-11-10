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
 * A pattern whose tile may draw past its cell (shape-scene pattern.h, PT15), drawn by shape-cpu: the copies a pixel is looked for in are those
 * whose extent meets it, each copy is clipped to its extent and not to its cell, and the copies are composited in lattice order.  The references
 * are hand-derived and independent of the renderer: a pixel covered by k copies of a tile of constant alpha a has the alpha 1 - (1 - a)^k, and
 * for a turned lattice a pixel's center is mapped back to pattern space and the copies whose rectangle holds it are counted.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace cuh;

namespace {

const double kTileAlpha = 0.5;

struct Case {
  double cell_w, cell_h;   // the cell, at (0, 0)
  double step_x, step_y;
  double tile_x0, tile_y0, tile_x1, tile_y1;  // the tile's one rectangle
  uint32_t overflow;
  GSHGEOM_Map map;
  GSHSCENE_Antialias antialias = GSHSCENE_ANTIALIAS_AREA;
};

/** A list that fills the whole target with the pattern of @p c, the tile one rectangle of constant alpha. */
GSHSCENE_List * list_of(Scene & s, const Case & c, uint32_t w, uint32_t h) {
  GSHSCENE_List * t = s.open();
  GSHSCENE_Paint ink = rh::solid(0.9, 0.2, 0.1, kTileAlpha);
  EXPECT_EQ(gshscene_list_fill_antialias(t, s.rect(c.tile_x0, c.tile_y0, c.tile_x1, c.tile_y1), nullptr, GSHGEOM_FILL_NONZERO, &ink,
                c.antialias, 0, 0.0),
      GSHSCENE_OK);
  const GSHSCENE_Resource * tile = s.picture(t);
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = tile;
  pt.w = c.cell_w;
  pt.h = c.cell_h;
  pt.x_step = c.step_x;
  pt.y_step = c.step_y;
  pt.overflow = c.overflow;
  pt.map = c.map;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_pattern(s.store, &pt, &r), GSHSCENE_OK);
  s.held.push_back(r);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint use = rh::solid(0, 0, 0, 1.0);
  use.kind = GSHSCENE_PAINT_PATTERN;
  use.resource = r;
  EXPECT_EQ(gshscene_list_fill_antialias(l, s.rect(0, 0, (double)w, (double)h), nullptr, GSHGEOM_FILL_NONZERO, &use, c.antialias, 0, 0.0),
      GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

Case axis_case(uint32_t overflow) {
  Case c{};
  c.cell_w = c.cell_h = 4;
  c.step_x = c.step_y = 4;
  c.tile_x0 = c.tile_y0 = 0;
  c.tile_x1 = c.tile_y1 = 6;
  c.overflow = overflow;
  gshgeom_map_identity(2, &c.map);
  return c;
}

/** The byte of the alpha 1 - (1 - a)^k, as the renderer rounds (within one). */
int expect_alpha(int k) {
  return (int)std::lround(255.0 * (1.0 - std::pow(1.0 - kTileAlpha, k)));
}

}  // namespace

TEST(RenderPatternOverflow, TheCopiesThatSpillAreDrawnOverTheirNeighbours) {
  // Tile [0, 6]^2 in a 4 x 4 cell on steps of 4: copy (i, j) covers [4i, 4i + 6) x [4j, 4j + 6), so along an axis the pixels 0 and 1
  // are under copies -1 and 0, 2 and 3 under copy 0, 4 and 5 under copies 0 and 1, 6 and 7 under copy 1.  The copies are the product.
  const int along[8] = {2, 2, 1, 1, 2, 2, 1, 1};
  Scene s;
  Draw d = draw(list_of(s, axis_case(1), 8, 8), 8, 8);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  for (uint32_t y = 0; y < 8; y++) {
    for (uint32_t x = 0; x < 8; x++) {
      const int k = along[x] * along[y];
      EXPECT_NEAR((int)d.at8(x, y, 3), expect_alpha(k), 1) << x << "," << y << " under " << k << " copies";
    }
  }
}

TEST(RenderPatternOverflow, ThePatternThatClipsIsDrawnOnceEverywhere) {
  Scene s;
  Draw d = draw(list_of(s, axis_case(0), 8, 8), 8, 8);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  for (uint32_t y = 0; y < 8; y++) {
    for (uint32_t x = 0; x < 8; x++) {
      EXPECT_NEAR((int)d.at8(x, y, 3), expect_alpha(1), 1) << x << "," << y;
    }
  }
}

TEST(RenderPatternOverflow, ATileThatStaysInItsCellDrawsTheSameEitherWay) {
  Case c = axis_case(0);
  c.tile_x1 = c.tile_y1 = 3;
  Scene s0;
  Draw a = draw(list_of(s0, c, 12, 12), 12, 12);
  c.overflow = 1;
  Scene s1;
  Draw b = draw(list_of(s1, c, 12, 12), 12, 12);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  ASSERT_EQ(b.result, GSHSCENE_OK);
  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_NE(a.at8(3, 3, 3), a.at8(1, 1, 3)) << "the tile leaves a one pixel gap, so the render is not a flat color";
}

TEST(RenderPatternOverflow, AGapBetweenTheCellsIsBridgedByTheSpill) {
  // Cells of 4 on steps of 8, the tile [0, 6) wide: along x the pixels 0..5 of each 8 are drawn, and 6 and 7 are the gap.
  Case c = axis_case(1);
  c.step_x = c.step_y = 8;
  Scene s;
  Draw d = draw(list_of(s, c, 16, 16), 16, 16);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  for (uint32_t y = 0; y < 16; y++) {
    for (uint32_t x = 0; x < 16; x++) {
      const bool in = x % 8 < 6 && y % 8 < 6;
      EXPECT_EQ((int)d.at8(x, y, 3), in ? (int)std::lround(255.0 * kTileAlpha) : 0) << x << "," << y;
    }
  }
  Case clipped = c;
  clipped.overflow = 0;
  Scene s0;
  Draw e = draw(list_of(s0, clipped, 16, 16), 16, 16);
  EXPECT_EQ((int)e.at8(5, 5, 3), 0) << "clipped to the 4 x 4 cell, pixel 5 is in the gap";
  EXPECT_NE((int)d.at8(5, 5, 3), 0);
}

TEST(RenderPatternOverflow, ATurnedLatticeAgainstThePointReference) {
  // Each pixel's center, mapped back by the inverse of the pattern's map, is in the copies (i, j) whose rectangle [4i, 4i + 6) x [4j, 4j + 6)
  // holds it.  No pixel center is on an edge for this angle, so the crisp rule's tie never decides one.
  const double angle = 0.4;  // radians
  const double cs = std::cos(angle);
  const double sn = std::sin(angle);
  Case c = axis_case(1);
  c.antialias = GSHSCENE_ANTIALIAS_NONE;
  const double m[4] = {cs, -sn, sn, cs};
  const double t[2] = {1.3, 0.7};
  ASSERT_EQ(gshgeom_map_from_matrix(2, m, t, &c.map), GSHGEOM_OK);
  const uint32_t n = 24;
  Scene s;
  Draw d = draw(list_of(s, c, n, n), n, n);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  size_t drawn = 0, several = 0, skipped = 0;
  for (uint32_t y = 0; y < n; y++) {
    for (uint32_t x = 0; x < n; x++) {
      const double px = (double)x + 0.5 - t[0];
      const double py = (double)y + 0.5 - t[1];
      const double qx = cs * px + sn * py;   // the inverse of a rotation is its transpose
      const double qy = -sn * px + cs * py;
      int cx = 0, cy = 0;
      double nearest = 1e9;  // the distance from the center to the nearest edge of any copy, in pattern units
      for (int i = -4; i <= 8; i++) {
        cx += (qx >= 4.0 * i && qx < 4.0 * i + 6.0) ? 1 : 0;
        cy += (qy >= 4.0 * i && qy < 4.0 * i + 6.0) ? 1 : 0;
        for (double e : {4.0 * i, 4.0 * i + 6.0}) {
          nearest = std::fmin(nearest, std::fmin(std::fabs(qx - e), std::fabs(qy - e)));
        }
      }
      if (nearest < 0.02) {
        skipped++;  // the scan converter rounds a vertex to 1/64 of a pixel, so a center that close to an edge may be on either side
        continue;
      }
      const int k = cx * cy;
      drawn += k > 0;
      several += k > 1;
      EXPECT_NEAR((int)d.at8(x, y, 3), k ? expect_alpha(k) : 0, 1) << x << "," << y << " under " << k << " copies";
    }
  }
  EXPECT_GT(several, 40u) << "the reference does exercise the overlap";
  EXPECT_GT(drawn, 300u);
  EXPECT_LT(skipped, 40u) << "the pixels too near an edge to be judged by a point reference are a few percent";
}

TEST(RenderPatternOverflow, ASinkThatDrawsItReportsNothingAndAnUnknownExtentIsReportedAndDrawnClipped) {
  Scene s;
  Draw d = draw(list_of(s, axis_case(1), 8, 8), 8, 8);
  EXPECT_EQ(d.count(GSHSCENE_ENTRY_APPROXIMATED, GSHSCENE_REASON_PATTERN_OVERFLOW), 0u);

  // A tile of a device-width stroke has no known extent: the planning pass says so, and the pattern is drawn as clipped.
  Scene s2;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.0;
  st.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s2.store, &st, &style), GSHSCENE_OK);
  s2.held.push_back(style);
  GSHSCENE_List * t = s2.open();
  GSHSCENE_Paint ink = rh::solid(0.9, 0.2, 0.1, 1.0);
  ASSERT_EQ(gshscene_list_stroke(t, s2.rect(0, 0, 6, 6), style, nullptr, &ink, 0, 0.0), GSHSCENE_OK);
  const GSHSCENE_Resource * tile = s2.picture(t);
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = tile;
  pt.w = pt.h = pt.x_step = pt.y_step = 4;
  pt.overflow = 1;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_pattern(s2.store, &pt, &r), GSHSCENE_OK);
  s2.held.push_back(r);
  GSHSCENE_List * l = s2.open();
  GSHSCENE_Paint use = rh::solid(0, 0, 0, 1.0);
  use.kind = GSHSCENE_PAINT_PATTERN;
  use.resource = r;
  ASSERT_EQ(gshscene_list_fill(l, s2.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &use, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Draw e = draw(l, 8, 8);
  ASSERT_EQ(e.result, GSHSCENE_OK);
  EXPECT_EQ(e.count(GSHSCENE_ENTRY_APPROXIMATED, GSHSCENE_REASON_PATTERN_OVERFLOW), 1u);
  pt.overflow = 0;
  const GSHSCENE_Resource * r0 = nullptr;
  ASSERT_EQ(gshscene_store_intern_pattern(s2.store, &pt, &r0), GSHSCENE_OK);
  s2.held.push_back(r0);
  GSHSCENE_List * l0 = s2.open();
  use.resource = r0;
  ASSERT_EQ(gshscene_list_fill(l0, s2.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &use, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l0), GSHSCENE_OK);
  Draw f = draw(l0, 8, 8);
  EXPECT_EQ(e.bytes, f.bytes) << "the approximation is the clipped pattern, byte for byte";
}

TEST(RenderPatternOverflow, ACopyThatIsOutsideTheTargetButSpillsIntoItIsDrawn) {
  // A big spill: the tile [0, 20) in a cell of 4 on steps of 4 reaches the target from copies up to five cells before it.
  Case c = axis_case(1);
  c.tile_x1 = c.tile_y1 = 20;
  Scene s;
  Draw d = draw(list_of(s, c, 4, 4), 4, 4);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  // Pixel 0 along an axis is under copies -4 to 0 (copy i covers [4i, 4i + 20): i >= -4), five of them; likewise pixels 1 to 3.
  EXPECT_NEAR((int)d.at8(0, 0, 3), expect_alpha(25), 1);
  EXPECT_NEAR((int)d.at8(3, 3, 3), expect_alpha(25), 1);
}

TEST(RenderPatternOverflow, TheFastTiersDrawTheSpillAsTheGeneralOneDoes) {
  // Whole-pixel steps are what the period tier draws from one window; with the spill the copies overlap, and the window must hold them.  The cases
  // are the axis lattice and a skewed one, a stroke in the tile, and a target larger than a period; the bytes are the same with and without
  // the period tier (and with only the exact general tier), and the count of the tier's use says the period tier took part where it can.
  for (int variant = 0; variant < 3; variant++) {
    Case c = axis_case(1);
    if (variant == 1) {
      const double m[4] = {1.0, 0.0, 2.0, 1.0};  // a skew by whole pixels: the lattice stays integer
      const double t[2] = {0.0, 0.0};
      ASSERT_EQ(gshgeom_map_from_matrix(2, m, t, &c.map), GSHGEOM_OK);
    } else if (variant == 2) {
      c.step_x = 5;
      c.step_y = 3;
      c.tile_x1 = 9;
    }
    const uint32_t n = 40;
    Options all;
    Options none;
    none.tiers = GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_PERIOD;
    Scene s0;
    Draw a = draw(list_of(s0, c, n, n), n, n, all);
    Scene s1;
    Draw b = draw(list_of(s1, c, n, n), n, n, none);
    ASSERT_EQ(a.result, GSHSCENE_OK) << variant;
    ASSERT_EQ(b.result, GSHSCENE_OK) << variant;
    EXPECT_EQ(a.bytes, b.bytes) << "variant " << variant;
    EXPECT_EQ(a.summary.counted, b.summary.counted) << variant;
    EXPECT_GT(a.counters.pattern_period, 0u) << "variant " << variant << ": the period tier drew it";
    EXPECT_EQ(b.counters.pattern_period, 0u) << variant;
  }
}

TEST(RenderPatternOverflow, ASpillToTheLeftAndAboveIsDrawnToo) {
  // The tile [-2, 4)^2 in a 4 x 4 cell: copy i covers [4i - 2, 4i + 4), so along an axis pixels 0 and 1 are under copy 0 alone, 2 and 3 under
  // copies 0 and 1, 4 and 5 under copy 1, 6 and 7 under copies 1 and 2.
  const int along[8] = {1, 1, 2, 2, 1, 1, 2, 2};
  Case c = axis_case(1);
  c.tile_x0 = c.tile_y0 = -2;
  c.tile_x1 = c.tile_y1 = 4;
  Scene s;
  Draw d = draw(list_of(s, c, 8, 8), 8, 8);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  for (uint32_t y = 0; y < 8; y++) {
    for (uint32_t x = 0; x < 8; x++) {
      EXPECT_NEAR((int)d.at8(x, y, 3), expect_alpha(along[x] * along[y]), 1) << x << "," << y;
    }
  }
}

TEST(RenderPatternOverflow, ASpillAlongOneAxisOnlyIsDrawnOverItsNeighboursOnThatAxis) {
  const int along[8] = {2, 2, 1, 1, 2, 2, 1, 1};
  for (int axis = 0; axis < 2; axis++) {
    Case c = axis_case(1);
    (axis == 0 ? c.tile_x1 : c.tile_y1) = 6;
    (axis == 0 ? c.tile_y1 : c.tile_x1) = 4;
    Scene s;
    Draw d = draw(list_of(s, c, 8, 8), 8, 8);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    for (uint32_t y = 0; y < 8; y++) {
      for (uint32_t x = 0; x < 8; x++) {
        const int k = along[axis == 0 ? x : y];
        EXPECT_NEAR((int)d.at8(x, y, 3), expect_alpha(k), 1) << "axis " << axis << " at " << x << "," << y;
      }
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
