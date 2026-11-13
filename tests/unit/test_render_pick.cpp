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
 * Picking against the pixels (shape-scene's pick.h, PK10): random lists of curved fills, strokes (joins, caps, dashes, alignment),
 * runs of glyphs from outlines, grid-aligned clips, unions with inner clips and instances under rotating, scaling maps are drawn
 * with no antialiasing, and the item the pick names topmost at the center of every pixel is the one whose color is there.  A
 * pixel whose center is on an edge is the pick's boundary answer and is not compared; every other pixel is, so the test is
 * the statement that a pick says what is drawn.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace cuh;

namespace {

constexpr int kSize = 24;

struct Gen {
  explicit Gen(unsigned seed) : rng(seed) {}
  std::mt19937 rng;
  int pick(int lo, int hi) { return (int)(lo + rng() % (unsigned)(hi - lo + 1)); }
  double real(double lo, double hi) { return lo + (hi - lo) * ((double)(rng() % 100001) / 100000.0); }
};

/** The color index of the items made so far, which each fill and stroke takes: the red byte of the pixel it covers. */
struct Scenario {
  Scene s;
  Gen g;
  int next_color = 1;
  std::vector<GSHSCENE_Paint> paints;
  explicit Scenario(unsigned seed) : g(seed) {}

  GSHSCENE_Paint fresh() {
    GSHSCENE_Paint p = rh::solid((double)next_color / 255.0, 0.0, 0.0, 1.0);
    next_color++;
    return p;
  }

  /** A random path: a polygon, or a few lines, quadratics and cubics, closed or not. */
  const GSHSCENE_Resource * random_path(bool closed_only = false) {
    GSHGEOM_Path * p = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &p);
    auto pt = [&]() { return GSHGEOM_Point{g.real(-3, kSize + 3), g.real(-3, kSize + 3), 0}; };
    gshgeom_path_move_to(p, pt());
    const int segments = g.pick(2, 5);
    for (int i = 0; i < segments; i++) {
      switch (g.pick(0, 2)) {
        case 0:
          gshgeom_path_line_to(p, pt());
          break;
        case 1:
          gshgeom_path_quadratic_to(p, pt(), pt());
          break;
        default:
          gshgeom_path_cubic_to(p, pt(), pt(), pt());
          break;
      }
    }
    if (closed_only || g.pick(0, 2) > 0) {
      gshgeom_path_close(p);
    }
    return s.path(p);
  }

  const GSHSCENE_Resource * random_style(bool closed) {
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    static const double widths[] = {1.0, 2.5, 4.0, 7.0};
    st.width = widths[g.pick(0, 3)];
    st.join = (uint32_t)g.pick(0, 2);
    st.cap = (uint32_t)g.pick(0, 2);
    static const double dash_a[] = {3.0, 2.0};
    static const double dash_b[] = {5.0, 1.5, 1.0, 1.5};
    if (g.pick(0, 3) == 0) {
      st.dash_count = 2;
      st.dashes = dash_a;
    } else if (g.pick(0, 7) == 0) {
      st.dash_count = 4;
      st.dashes = dash_b;
      st.dash_offset = 1.0;
    }
    if (closed && g.pick(0, 3) == 0) {
      st.alignment = (uint32_t)g.pick(1, 2);
      st.alignment_rule = g.pick(0, 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
    }
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &r), GSHSCENE_OK);
    s.held.push_back(r);
    return r;
  }

  /** A run of two or three glyphs from outlines. */
  const GSHSCENE_Resource * random_run() {
    std::vector<GSHSCENE_Glyph> glyphs;
    const int n = g.pick(1, 3);
    for (int i = 0; i < n; i++) {
      GSHSCENE_Glyph gl;
      std::memset(&gl, 0, sizeof gl);
      gl.id = (uint32_t)(i + 1);
      gl.x = g.real(0, kSize / 2);
      gl.y = g.real(0, kSize / 2);
      gl.outline = random_path(true);
      glyphs.push_back(gl);
    }
    GSHSCENE_Glyph_Run run;
    std::memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    run.glyph_count = glyphs.size();
    run.glyphs = glyphs.data();
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
    s.held.push_back(r);
    return r;
  }

  /** A grid-aligned region: a rectangle or an L, with integer vertices, so that a clip's edge never splits a pixel. */
  const GSHSCENE_Resource * grid_region() {
    const int x0 = g.pick(0, kSize - 6);
    const int y0 = g.pick(0, kSize - 6);
    const int w = g.pick(3, kSize - x0);
    const int h = g.pick(3, kSize - y0);
    if (g.pick(0, 2) == 0) {
      const int cx = x0 + w / 2;
      const int cy = y0 + h / 2;
      return s.polygon({(double)x0, (double)y0, (double)(x0 + w), (double)y0, (double)(x0 + w), (double)cy, (double)cx, (double)cy,
          (double)cx, (double)(y0 + h), (double)x0, (double)(y0 + h)});
    }
    return s.rect(x0, y0, x0 + w, y0 + h);
  }

  GSHGEOM_Map random_map() {
    GSHGEOM_Map rot;
    gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, g.real(0, 360), &rot);
    GSHGEOM_Map sc;
    const double k = g.real(0.5, 1.4);
    gshgeom_map_scale(2, k, k * g.real(0.7, 1.3), 1.0, &sc);
    GSHGEOM_Map tr;
    gshgeom_map_translate(2, g.real(-4, 10), g.real(-4, 10), 0.0, &tr);
    GSHGEOM_Map m;
    gshgeom_map_compose(&sc, &rot, &m);
    gshgeom_map_compose(&tr, &m, &m);
    return m;
  }

  void fill(GSHSCENE_List * l, const GSHSCENE_Resource * r, const GSHGEOM_Map * map = nullptr) {
    GSHSCENE_Paint p = fresh();
    EXPECT_EQ(gshscene_list_fill_antialias(l, r, map, g.pick(0, 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, &p,
                  GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
        GSHSCENE_OK);
  }

  void stroke(GSHSCENE_List * l, const GSHGEOM_Map * map = nullptr) {
    const GSHSCENE_Resource * path = random_path(g.pick(0, 2) == 0);
    GSHSCENE_Paint p = fresh();
    const GSHGEOM_Path * geometry = gshscene_resource_path(path);
    // An aligned stroke needs a closed centerline: the style is made for what the path is (the last element of a closed path is a close).
    size_t n = gshgeom_path_element_count(geometry);
    GSHGEOM_Verb verb = GSHGEOM_VERB_MOVE;
    gshgeom_path_verb(geometry, n - 1, &verb);
    const GSHSCENE_Resource * style = random_style(verb == GSHGEOM_VERB_CLOSE);
    EXPECT_EQ(gshscene_list_stroke_antialias(l, path, style, map, &p, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
  }

  /** A picture of one to three fills, strokes and runs, drawn under its own map. */
  const GSHSCENE_Resource * picture() {
    GSHSCENE_List * l = s.open();
    const int n = g.pick(1, 3);
    for (int i = 0; i < n; i++) {
      switch (g.pick(0, 2)) {
        case 0:
          fill(l, random_path(true));
          break;
        case 1:
          stroke(l);
          break;
        default:
          fill(l, random_run());
          break;
      }
    }
    return s.picture(l);
  }

  /** A union of two or three grid-aligned members, one of them clipped by an inner clip. */
  const GSHSCENE_Resource * grid_union() {
    std::vector<GSHSCENE_Clip_Inner> inner(1);
    std::memset(&inner[0], 0, sizeof inner[0]);
    inner[0].resource = grid_region();
    std::vector<GSHSCENE_Clip_Member> members;
    const int n = g.pick(1, 3);
    for (int i = 0; i < n; i++) {
      GSHSCENE_Clip_Member m = member(grid_region(), g.pick(0, 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO);
      if (i == 0 && g.pick(0, 1)) {
        m.inner_count = 1;
        m.inner = inner.data();
      }
      members.push_back(m);
    }
    return make_union(s, members);
  }

  /** The list: items, clips (grid-aligned, and the unions), instances. */
  GSHSCENE_List * list() {
    GSHSCENE_List * l = s.open();
    std::vector<const GSHSCENE_Resource *> pictures;
    for (int i = 0; i < 3; i++) {
      pictures.push_back(picture());
    }
    int open_clips = 0;
    const int n = g.pick(4, 14);
    for (int i = 0; i < n; i++) {
      const int roll = g.pick(0, 9);
      if (roll <= 1) {
        fill(l, random_path(true));
      } else if (roll == 2) {
        fill(l, random_run());
      } else if (roll == 3 || roll == 4) {
        stroke(l);
      } else if (roll == 5 && open_clips < 3) {
        const GSHSCENE_Resource * region = g.pick(0, 2) == 0 ? grid_union() : grid_region();
        const GSHGEOM_Fill_Rule rule = g.pick(0, 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
        EXPECT_EQ(gshscene_list_clip_push(l, region, nullptr, rule, 0, 0.0), GSHSCENE_OK);
        open_clips++;
      } else if (roll == 6 && open_clips > 0) {
        EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
        open_clips--;
      } else {
        const GSHGEOM_Map m = random_map();
        EXPECT_EQ(gshscene_list_instance(l, pictures[(size_t)g.pick(0, 2)], &m, 0, 0.0), GSHSCENE_OK);
      }
    }
    while (open_clips-- > 0) {
      EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  }
};

/** The red byte an item of the chain was given. */
int color_of(const GSHSCENE_List * top, const size_t * chain, size_t count) {
  const GSHSCENE_List * list = top;
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = nullptr;
    if (gshscene_list_item(list, chain[i], &item) != GSHSCENE_OK) {
      return -1;
    }
    if (i + 1 == count) {
      return (int)std::lround(item->paint.rgba[0] * 255.0);
    }
    list = gshscene_resource_picture(item->resource);
  }
  return -1;
}

}  // namespace

TEST(RenderPick, EveryPixelCenterNamesTheItemThatIsDrawnThere) {
  size_t compared = 0;
  size_t drawn = 0;
  size_t boundary = 0;
  size_t chains = 0;
  size_t stroked = 0;
  for (unsigned seed = 1; seed <= 140; seed++) {
    Scenario sc(seed);
    GSHSCENE_List * list = sc.list();
    const Draw d = draw(list, kSize, kSize);
    ASSERT_EQ(d.result, GSHSCENE_OK) << "seed " << seed;
    ASSERT_EQ(d.count(GSHSCENE_ENTRY_REFUSED), 0u) << "seed " << seed;
    for (int y = 0; y < kSize; y++) {
      for (int x = 0; x < kSize; x++) {
        GSHSCENE_Pick_Params q;
        std::memset(&q, 0, sizeof q);
        q.point.x = 64 * x + 32;
        q.point.y = 64 * y + 32;
        GSHSCENE_Pick * pick = nullptr;
        ASSERT_EQ(gshscene_pick_list(nullptr, list, &q, &pick), GSHSCENE_OK);
        int expect = 0;  // the red byte, 0 for nothing drawn
        bool skip = false;
        for (size_t i = 0; i < gshscene_pick_count(pick) && !skip; i++) {
          GSHSCENE_Pick_Entry e;
          ASSERT_EQ(gshscene_pick_entry(pick, i, &e), GSHSCENE_OK);
          ASSERT_NE(e.state, GSHSCENE_PICK_UNDECIDED) << "seed " << seed << " at " << x << "," << y;
          if (e.state == GSHSCENE_PICK_BOUNDARY) {
            skip = true;  // a center on an edge: the pick says boundary, the sink's tie rule says the rest
            boundary++;
            break;
          }
          expect = color_of(list, e.chain, e.chain_count);
          chains += e.chain_count > 1;
          stroked += e.kind == GSHSCENE_ITEM_STROKE;
          break;
        }
        gshscene_pick_destroy(pick);
        if (skip) {
          continue;
        }
        const uint8_t alpha = d.at8((uint32_t)x, (uint32_t)y, 3);
        ASSERT_TRUE(alpha == 0 || alpha == 255) << "no antialiasing and grid-aligned clips leave whole pixels: seed " << seed << " at " << x
                                                << "," << y << " alpha " << (int)alpha;
        EXPECT_EQ(expect != 0, alpha == 255) << "seed " << seed << " at " << x << "," << y;
        if (expect != 0) {
          EXPECT_EQ((int)d.at8((uint32_t)x, (uint32_t)y, 0), expect) << "seed " << seed << " at " << x << "," << y;
          drawn++;
        }
        compared++;
      }
    }
  }
  EXPECT_GT(compared, 70000u);
  EXPECT_GT(drawn, 20000u) << "the comparison sees drawn pixels, not only empty ones";
  EXPECT_GT(chains, 2000u) << "and pixels drawn through an instance";
  EXPECT_GT(stroked, 2000u) << "and strokes";
  EXPECT_LT(boundary, compared / 20) << "edges are the exception";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
