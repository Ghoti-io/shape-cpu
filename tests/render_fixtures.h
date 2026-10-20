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
 * The fixture set of story 13b: the lists whose renders are the committed
 * goldens (tests/corpus/render.hash), rendered by the unit tests and by the
 * cross-target probe (tools/corpus-hash.c), on a 48 by 40 raster of each format
 * the sink writes, over a fixed backdrop.  C, so that the probe and the tests
 * build the same lists.
 *
 * | Fixture | What it holds |
 * | --- | --- |
 * | fill-rules | one star with curved arms, filled nonzero and even-odd, side by side, over an opaque fill |
 * | clip-leaf | a clip around two overlapping fills: each is weighed by it (N9, D8) |
 * | clip-group | the same fills in a group inside the clip: weighed once |
 * | opacity-group | two overlapping opaque fills in a group of opacity 0.6 |
 * | operators | each of the thirteen operators, a group per cell, each cell clipped |
 * | order | a depth group with a tie broken by layer, and an overlay group |
 * | blend-spaces | groups in linear sRGB, Display P3 and a gamma, nested, with paints in Adobe RGB, BT.2020 and P3 |
 * | foreground | foreground paints, the list's foreground in Display P3 |
 * | picture | a picture (a fill and a clipped group) instanced twice, rotated and scaled |
 * | glyphs | a glyph run of two outline glyphs (curves, a rational arc, a counter) |
 * | maps | a skewed fill, a rotated clip, and the target's map |
 * | font-fast | (story 14a) runs from the test provider's faces on the fast tier: unhinted at a scaled map and fractional origins, hinted, and a face without a program |
 * | font-general | (story 14a) the same runs on the general route: rotated, and at a size that is no integer ppem |
 */

#ifndef GHOTI_IO_GSHCPU_TEST_RENDER_FIXTURES_H
#define GHOTI_IO_GSHCPU_TEST_RENDER_FIXTURES_H

#include <ghoti.io/image/raster.h>
#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>
#include <ghoti.io/shape-scene/testing/glyph_test_provider.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** The raster every fixture is drawn on. */
#define RF_WIDTH 48u
#define RF_HEIGHT 40u

/** What one fixture's lists hold on to; released by rf_release. */
typedef struct RF_Ctx {
  GSHSCENE_Store * store;
  const GSHSCENE_Resource * held[96];
  size_t held_count;
  GSHSCENE_List * lists[8];
  size_t list_count;
  int failed;
  GSHGEOM_Map target_map; /* the target's map the fixture asks for (the identity for most) */
} RF_Ctx;

static inline GSHGEOM_Point rf_pt(double x, double y) {
  GSHGEOM_Point p;
  p.x = x;
  p.y = y;
  p.z = 0.0;
  return p;
}

static inline void rf_check(RF_Ctx * c, int ok) {
  if (!ok) {
    c->failed = 1;
  }
}

static inline const GSHSCENE_Resource * rf_hold(RF_Ctx * c, const GSHSCENE_Resource * r) {
  if (!r || c->held_count >= sizeof c->held / sizeof c->held[0]) {
    c->failed = 1;
    return r;
  }
  c->held[c->held_count++] = r;
  return r;
}

static inline const GSHSCENE_Resource * rf_intern(RF_Ctx * c, GSHGEOM_Path * p) {
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_path(c->store, p, &r) == GSHSCENE_OK);
  gshgeom_path_destroy(p);
  return rf_hold(c, r);
}

static inline GSHGEOM_Path * rf_path(RF_Ctx * c) {
  GSHGEOM_Path * p = NULL;
  rf_check(c, gshgeom_path_create(NULL, NULL, 2, &p) == GSHGEOM_OK);
  return p;
}

static inline const GSHSCENE_Resource * rf_rect(RF_Ctx * c, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(x0, y0));
  gshgeom_path_line_to(p, rf_pt(x1, y0));
  gshgeom_path_line_to(p, rf_pt(x1, y1));
  gshgeom_path_line_to(p, rf_pt(x0, y1));
  gshgeom_path_close(p);
  return rf_intern(c, p);
}

/** A five-pointed star with curved arms around (cx, cy): its centre is wound twice. */
static inline const GSHSCENE_Resource * rf_star(RF_Ctx * c, double cx, double cy, double r) {
  static const double ux[5] = {0.0, 0.9510565162951535, 0.5877852522924731, -0.5877852522924731, -0.9510565162951535};
  static const double uy[5] = {-1.0, -0.3090169943749474, 0.8090169943749475, 0.8090169943749475, -0.3090169943749474};
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(cx + r * ux[0], cy + r * uy[0]));
  for (int i = 1; i <= 5; i++) {
    int k = (i * 2) % 5;
    int prev = ((i - 1) * 2) % 5;
    double mx = cx + 0.3 * r * (ux[k] + ux[prev]);
    double my = cy + 0.3 * r * (uy[k] + uy[prev]);
    if (i % 2) {
      gshgeom_path_quadratic_to(p, rf_pt(mx, my), rf_pt(cx + r * ux[k], cy + r * uy[k]));
    } else {
      gshgeom_path_line_to(p, rf_pt(cx + r * ux[k], cy + r * uy[k]));
    }
  }
  gshgeom_path_close(p);
  return rf_intern(c, p);
}

/** A blob: cubics, and a rational quadratic arc (a quarter circle), so the device path subdivides one. */
static inline const GSHSCENE_Resource * rf_blob(RF_Ctx * c, double x, double y, double s) {
  static const double w[3] = {1.0, 0.7071067811865476, 1.0};
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(x, y + s));
  gshgeom_path_cubic_to(p, rf_pt(x, y + 0.3 * s), rf_pt(x + 0.4 * s, y), rf_pt(x + s, y));
  gshgeom_path_rational_quadratic_to(p, rf_pt(x + 2.0 * s, y), rf_pt(x + 2.0 * s, y + s), w);
  gshgeom_path_cubic_to(p, rf_pt(x + 1.6 * s, y + 1.9 * s), rf_pt(x + 0.5 * s, y + 1.2 * s), rf_pt(x, y + s));
  gshgeom_path_close(p);
  return rf_intern(c, p);
}

/** A glyph outline: an "o" (two contours, the inner one a counter wound the other way) or a "v". */
static inline const GSHSCENE_Resource * rf_glyph(RF_Ctx * c, int which) {
  GSHGEOM_Path * p = rf_path(c);
  if (which == 0) {
    static const double w[3] = {1.0, 0.7071067811865476, 1.0};
    /* Outer, clockwise in y-down: four rational quarter arcs. */
    gshgeom_path_move_to(p, rf_pt(6.0, 0.0));
    gshgeom_path_rational_quadratic_to(p, rf_pt(12.0, 0.0), rf_pt(12.0, 7.0), w);
    gshgeom_path_rational_quadratic_to(p, rf_pt(12.0, 14.0), rf_pt(6.0, 14.0), w);
    gshgeom_path_rational_quadratic_to(p, rf_pt(0.0, 14.0), rf_pt(0.0, 7.0), w);
    gshgeom_path_rational_quadratic_to(p, rf_pt(0.0, 0.0), rf_pt(6.0, 0.0), w);
    gshgeom_path_close(p);
    /* Inner, the other way: cubics. */
    gshgeom_path_move_to(p, rf_pt(6.0, 3.0));
    gshgeom_path_cubic_to(p, rf_pt(4.0, 3.0), rf_pt(3.0, 5.0), rf_pt(3.0, 7.0));
    gshgeom_path_cubic_to(p, rf_pt(3.0, 9.0), rf_pt(4.0, 11.0), rf_pt(6.0, 11.0));
    gshgeom_path_cubic_to(p, rf_pt(8.0, 11.0), rf_pt(9.0, 9.0), rf_pt(9.0, 7.0));
    gshgeom_path_cubic_to(p, rf_pt(9.0, 5.0), rf_pt(8.0, 3.0), rf_pt(6.0, 3.0));
    gshgeom_path_close(p);
  } else {
    gshgeom_path_move_to(p, rf_pt(0.0, 0.0));
    gshgeom_path_line_to(p, rf_pt(3.25, 0.0));
    gshgeom_path_quadratic_to(p, rf_pt(5.0, 6.0), rf_pt(6.5, 10.5));
    gshgeom_path_quadratic_to(p, rf_pt(8.0, 6.0), rf_pt(9.75, 0.0));
    gshgeom_path_line_to(p, rf_pt(13.0, 0.0));
    gshgeom_path_line_to(p, rf_pt(8.0, 14.0));
    gshgeom_path_line_to(p, rf_pt(5.0, 14.0));
    gshgeom_path_close(p);
  }
  return rf_intern(c, p);
}

static inline GSHSCENE_List * rf_open(RF_Ctx * c) {
  GSHSCENE_List * l = NULL;
  rf_check(c, gshscene_list_create(NULL, NULL, c->store, &l) == GSHSCENE_OK);
  if (l && c->list_count < sizeof c->lists / sizeof c->lists[0]) {
    c->lists[c->list_count++] = l;
  } else {
    c->failed = 1;
  }
  return l;
}

static inline GSHSCENE_Color_Space rf_space(GCOL_Primaries primaries, GCOL_Transfer transfer, double gamma) {
  GSHSCENE_Color_Space s;
  memset(&s, 0, sizeof s);
  s.primaries = primaries;
  s.transfer = transfer;
  s.gamma = gamma;
  return s;
}

static inline GSHSCENE_Paint rf_solid(double r, double g, double b, double a) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_SOLID;
  p.rgba[0] = r;
  p.rgba[1] = g;
  p.rgba[2] = b;
  p.rgba[3] = a;
  return p;
}

static inline GSHSCENE_Paint rf_solid_in(double r, double g, double b, double a, GSHSCENE_Color_Space space) {
  GSHSCENE_Paint p = rf_solid(r, g, b, a);
  p.space = space;
  return p;
}

static inline GSHSCENE_Group rf_group(double opacity, uint32_t composite) {
  GSHSCENE_Group g;
  memset(&g, 0, sizeof g);
  g.opacity = opacity;
  g.composite = composite;
  g.ordering = GSHSCENE_ORDER_FLAT;
  return g;
}

static inline void rf_fill(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * r, const GSHGEOM_Map * map,
    GSHGEOM_Fill_Rule rule, GSHSCENE_Paint paint, int32_t layer, double depth) {
  rf_check(c, gshscene_list_fill(l, r, map, rule, &paint, layer, depth) == GSHSCENE_OK);
}

static inline void rf_close(RF_Ctx * c, GSHSCENE_List * l) {
  rf_check(c, gshscene_list_close(l) == GSHSCENE_OK);
}

/* --- the fixtures ----------------------------------------------------------------------------- */

static inline GSHSCENE_List * rf_fill_rules(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 2, 2, 46, 38), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.7, 1.0), 0, 0.0);
  const GSHSCENE_Resource * star = rf_star(c, 13.0, 20.0, 11.5);
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 22.25, 0.5, 0.0, &move);
  rf_fill(c, l, star, NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.2, 1.0), 0, 0.0);
  rf_fill(c, l, star, &move, GSHGEOM_FILL_EVEN_ODD, rf_solid(0.1, 0.2, 0.9, 0.7), 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_clip_leaf(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_check(c, gshscene_list_clip_push(l, rf_blob(c, 4.3, 6.1, 19.7), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 3.5, 3.25, 30.0, 28.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 0.5, 0.0, 0.75), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 14.25, 12.0, 45.0, 37.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.0, 0.4, 1.0, 0.5), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_clip_group(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Group g = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_clip_push(l, rf_blob(c, 4.3, 6.1, 19.7), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 3.5, 3.25, 30.0, 28.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 0.5, 0.0, 0.75), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 14.25, 12.0, 45.0, 37.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.0, 0.4, 1.0, 0.5), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_opacity_group(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Group g = rf_group(0.6, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_star(c, 16.0, 18.0, 14.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.7, 0.3, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 18.5, 9.75, 44.0, 33.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.6, 0.0, 0.6, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_operators(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  for (uint32_t op = 0; op < GSHSCENE_COMPOSITE_COUNT; op++) {
    double x = 1.0 + 9.0 * (double)(op % 5);
    double y = 1.0 + 13.0 * (double)(op / 5);
    GSHSCENE_Group g = rf_group(op % 2 ? 0.8 : 1.0, op);
    /* Each cell is clipped, so that an unbounded operator acts inside its own cell only (N8). */
    rf_check(c, gshscene_list_clip_push(l, rf_rect(c, x, y, x + 8.5, y + 12.25), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) ==
        GSHSCENE_OK);
    rf_fill(c, l, rf_rect(c, x, y + 1.5, x + 6.0, y + 9.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.3, 0.9, 0.8), 0, 0.0);
    rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
    rf_fill(c, l, rf_rect(c, x + 2.5, y + 4.0, x + 8.5, y + 12.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.1, 0.6),
        0, 0.0);
    rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
    rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_order(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Group depth = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  depth.ordering = GSHSCENE_ORDER_DEPTH;
  GSHSCENE_Group overlay = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  overlay.ordering = GSHSCENE_ORDER_OVERLAY;
  rf_check(c, gshscene_list_group_push(l, &depth, 0, 0.0) == GSHSCENE_OK);
  /* Declared near to far; painted far first.  The last two tie at depth 2, broken by layer. */
  rf_fill(c, l, rf_rect(c, 2, 2, 16, 16), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 0, 0, 0.8), 0, 1.0);
  rf_fill(c, l, rf_rect(c, 8, 8, 22, 22), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0, 1, 0, 0.8), 0, 3.0);
  rf_fill(c, l, rf_rect(c, 5, 12, 19, 26), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 1, 0.8), 1, 2.0);
  rf_fill(c, l, rf_rect(c, 11, 5, 25, 19), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 0, 0.8), 0, 2.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_push(l, &overlay, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 26, 20, 40, 34), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0, 1, 1, 0.9), 2, 0.0);
  rf_fill(c, l, rf_rect(c, 30, 24, 44, 38), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 0, 1, 0.9), -1, 0.0);
  rf_fill(c, l, rf_rect(c, 33, 16, 46, 29), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.5, 0.5, 0.5, 0.9), 2, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_blend_spaces(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Group linear = rf_group(0.9, GSHSCENE_COMPOSITE_SRC_OVER);
  linear.blend_space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  GSHSCENE_Group p3 = rf_group(0.75, GSHSCENE_COMPOSITE_SRC_OVER);
  p3.blend_space = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  GSHSCENE_Group gamma = rf_group(1.0, GSHSCENE_COMPOSITE_PLUS);
  gamma.blend_space = rf_space(GCOL_PRIMARIES_BT2020, GCOL_TRANSFER_GAMMA, 2.2);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.25, 0.5, 0.75, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_push(l, &linear, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 3, 3, 30, 22), NULL, GSHGEOM_FILL_NONZERO,
      rf_solid_in(0.9, 0.1, 0.1, 0.7, rf_space(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 2.19921875)), 0, 0.0);
  rf_check(c, gshscene_list_group_push(l, &p3, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_blob(c, 10.5, 9.25, 12.0), NULL, GSHGEOM_FILL_NONZERO,
      rf_solid_in(0.1, 0.8, 0.2, 0.9, rf_space(GCOL_PRIMARIES_BT2020, GCOL_TRANSFER_LINEAR, 0.0)), 0, 0.0);
  rf_check(c, gshscene_list_group_push(l, &gamma, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 20, 15, 45, 37), NULL, GSHGEOM_FILL_NONZERO,
      rf_solid_in(0.2, 0.3, 1.1, 0.5, rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0)), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 25, 5, 40, 30), NULL, GSHGEOM_FILL_NONZERO, rf_solid(-0.1, 0.6, 0.4, 0.4), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_foreground(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Color fg;
  memset(&fg, 0, sizeof fg);
  fg.rgba[0] = 0.85;
  fg.rgba[1] = 0.25;
  fg.rgba[2] = 0.1;
  fg.rgba[3] = 0.9;
  fg.space = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  rf_check(c, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
  GSHSCENE_Paint current;
  memset(&current, 0, sizeof current);
  current.kind = GSHSCENE_PAINT_FOREGROUND;
  current.rgba[3] = 1.0;
  rf_fill(c, l, rf_star(c, 14.0, 20.0, 12.0), NULL, GSHGEOM_FILL_NONZERO, current, 0, 0.0);
  current.rgba[3] = 0.5;
  rf_fill(c, l, rf_blob(c, 22.0, 8.0, 12.0), NULL, GSHGEOM_FILL_EVEN_ODD, current, 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_picture(RF_Ctx * c) {
  GSHSCENE_List * inner = rf_open(c);
  GSHSCENE_Group g = rf_group(0.7, GSHSCENE_COMPOSITE_XOR);
  rf_fill(c, inner, rf_rect(c, 0, 0, 10, 10), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.6, 0.1, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_push(inner, rf_star(c, 8.0, 8.0, 8.0), NULL, GSHGEOM_FILL_EVEN_ODD, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_push(inner, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, inner, rf_rect(c, 4, 4, 16, 16), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.8, 0.9), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(inner) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(inner) == GSHSCENE_OK);
  rf_close(c, inner);
  const GSHSCENE_Resource * picture = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, inner, &picture) == GSHSCENE_OK);
  rf_hold(c, picture);
  GSHSCENE_List * l = rf_open(c);
  GSHGEOM_Map a;
  GSHGEOM_Map b;
  GSHGEOM_Map rot;
  GSHGEOM_Map scale;
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 2.5, 3.25, 0.0, &a);
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &rot);
  gshgeom_map_scale(2, 1.25, 0.8, 1.0, &scale);
  gshgeom_map_translate(2, 30.0, 12.0, 0.0, &move);
  gshgeom_map_compose(&rot, &scale, &b);
  gshgeom_map_compose(&move, &b, &b);
  rf_check(c, gshscene_list_instance(l, picture, &a, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_instance(l, picture, &b, 0, 0.0) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_glyphs(RF_Ctx * c) {
  GSHSCENE_Glyph glyphs[2];
  memset(glyphs, 0, sizeof glyphs);
  glyphs[0].id = 82;
  glyphs[0].x = 0.0;
  glyphs[0].y = 0.0;
  glyphs[0].outline = rf_glyph(c, 0);
  glyphs[1].id = 89;
  glyphs[1].x = 13.5;
  glyphs[1].y = 0.0;
  glyphs[1].outline = rf_glyph(c, 1);
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_glyph_run(c->store, &run, &r) == GSHSCENE_OK);
  rf_hold(c, r);
  GSHSCENE_List * l = rf_open(c);
  GSHGEOM_Map place;
  GSHGEOM_Map big;
  gshgeom_map_translate(2, 3.3, 4.7, 0.0, &place);
  rf_fill(c, l, r, &place, GSHGEOM_FILL_NONZERO, rf_solid(0.05, 0.05, 0.1, 1.0), 0, 0.0);
  gshgeom_map_scale(2, 1.5, 1.5, 1.0, &big);
  gshgeom_map_translate(2, 4.0, 20.25, 0.0, &place);
  gshgeom_map_compose(&place, &big, &big);
  rf_fill(c, l, r, &big, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.1, 0.3, 0.85), 0, 0.0);
  rf_close(c, l);
  return l;
}

/* --- runs from a font (story 14a): the test provider's faces ---------------------------------------------- */

/** A 2048-unit face with a program, and a 1000-unit one without; for the life of the process. */
static GSHSCENE_Test_Provider rf_provider_program = {2048u, GSHSCENE_FACE_HINTING, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
static GSHSCENE_Test_Provider rf_provider_plain = {1000u, 0, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, {0}, {0}};

/** A run of the test glyphs 1, 2, 3, 6 and 7 of @p tp's face (fast tier bound), 1.3 em apart from (x, y). */
static inline const GSHSCENE_Resource * rf_font_run(RF_Ctx * c, GSHSCENE_Test_Provider * tp, int16_t coord, double size,
    uint32_t mode, double x, double y) {
  const GSHSCENE_Resource * face = rf_hold(c, gshscene_tp_face(c->store, tp, 1, tp == &rf_provider_plain ? 0x62u : 0x61u,
      coord));
  rf_check(c, face != NULL);
  static const uint32_t ids[5] = {1, 2, 3, 6, 7};
  GSHSCENE_Glyph glyphs[5];
  memset(glyphs, 0, sizeof glyphs);
  for (size_t i = 0; i < 5; i++) {
    glyphs[i].id = ids[i];
    glyphs[i].x = x + (double)i * 0.95 * size + (double)i / 7.0;
    glyphs[i].y = y + (double)(i % 2) / 3.0;
  }
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = size;
  run.hint_mode = mode;
  run.glyph_count = 5;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_glyph_run(c->store, &run, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

static inline GSHSCENE_List * rf_font_fast(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHGEOM_Map map;
  GSHGEOM_Map half;
  gshgeom_map_scale(2, 0.5, 0.5, 1.0, &half);
  gshgeom_map_translate(2, 0.3125, 0.0, 0.0, &map);
  gshgeom_map_compose(&map, &half, &map);
  /* Unhinted at 9 ppem (18 at a scale of one half), an instance of the axis. */
  rf_fill(c, l, rf_font_run(c, &rf_provider_program, 6000, 18.0, GSHSCENE_HINT_NONE, 1.0, 20.0), &map,
      GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.2, 1.0), 0, 0.0);
  /* Hinted at 9 ppem; and a face without a program, unhinted with its entry. */
  gshgeom_map_translate(2, 0.6, 20.0, 0.0, &map);
  rf_fill(c, l, rf_font_run(c, &rf_provider_program, 0, 9.0, GSHSCENE_HINT_FONT, 0.0, 0.0), &map, GSHGEOM_FILL_NONZERO,
      rf_solid(0.8, 0.2, 0.1, 0.9), 0, 0.0);
  rf_fill(c, l, rf_font_run(c, &rf_provider_plain, 0, 8.0, GSHSCENE_HINT_FONT, 2.0, 37.5), NULL, GSHGEOM_FILL_EVEN_ODD,
      rf_solid(0.1, 0.6, 0.3, 0.8), 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_font_general(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHGEOM_Map map;
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, -20.0, &turn);
  gshgeom_map_translate(2, 2.0, 18.0, 0.0, &map);
  gshgeom_map_compose(&map, &turn, &map);
  rf_fill(c, l, rf_font_run(c, &rf_provider_program, -8000, 9.0, GSHSCENE_HINT_FONT, 0.0, 0.0), &map,
      GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.7, 1.0), 0, 0.0);
  rf_fill(c, l, rf_font_run(c, &rf_provider_program, 0, 40.0 / 3.0, GSHSCENE_HINT_FONT, 1.0, 36.0), NULL,
      GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.5, 0.0, 0.7), 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_maps(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const double skew[4] = {1.0, 0.45, 0.1, 0.9};
  const double shift[2] = {1.5, 2.0};
  GSHGEOM_Map sk;
  GSHGEOM_Map rot;
  gshgeom_map_from_matrix(2, skew, shift, &sk);
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, -17.5, &rot);
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 6, 4, 30, 26), &rot, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_star(c, 14.0, 14.0, 13.0), &sk, GSHGEOM_FILL_EVEN_ODD, rf_solid(0.3, 0.6, 0.9, 0.95), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  rf_fill(c, l, rf_blob(c, 2.0, 2.0, 8.0), &sk, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.1, 0.6), 0, 0.0);
  rf_close(c, l);
  /* The target's map: list space is scaled into the raster (AD-24, composed last). */
  gshgeom_map_scale(2, 1.4, 1.3, 1.0, &c->target_map);
  return l;
}

/** The fixtures, in their committed order. */
typedef GSHSCENE_List * (*RF_Build)(RF_Ctx * c);

typedef struct RF_Fixture {
  const char * name;
  RF_Build build;
} RF_Fixture;

static const RF_Fixture RF_FIXTURES[] = {
    {"fill-rules", rf_fill_rules},
    {"clip-leaf", rf_clip_leaf},
    {"clip-group", rf_clip_group},
    {"opacity-group", rf_opacity_group},
    {"operators", rf_operators},
    {"order", rf_order},
    {"blend-spaces", rf_blend_spaces},
    {"foreground", rf_foreground},
    {"picture", rf_picture},
    {"glyphs", rf_glyphs},
    {"maps", rf_maps},
    {"font-fast", rf_font_fast},
    {"font-general", rf_font_general},
};

#define RF_FIXTURE_COUNT (sizeof RF_FIXTURES / sizeof RF_FIXTURES[0])

/** Start a context: a store of its own. */
static inline void rf_begin(RF_Ctx * c) {
  memset(c, 0, sizeof *c);
  rf_check(c, gshscene_store_create(NULL, NULL, &c->store) == GSHSCENE_OK);
  gshgeom_map_identity(2, &c->target_map);
}

/** Release every list and resource, then the store. */
static inline void rf_release(RF_Ctx * c) {
  for (size_t i = 0; i < c->list_count; i++) {
    gshscene_list_destroy(c->lists[i]);
  }
  for (size_t i = 0; i < c->held_count; i++) {
    gshscene_resource_release(c->held[i]);
  }
  gshscene_store_destroy(c->store);
  memset(c, 0, sizeof *c);
}

/* --- the backdrop and the raster -------------------------------------------------------------- */

/** The backdrop, straight 8-bit: varied colors, opaque, transparent and partial alpha. */
static inline void rf_backdrop8(uint32_t x, uint32_t y, unsigned char out[4]) {
  out[0] = (unsigned char)((37u * x + 11u * y) & 255u);
  out[1] = (unsigned char)((13u * x + 59u * y + 7u) & 255u);
  out[2] = (unsigned char)((x * y * 7u + 3u) & 255u);
  if ((x + y) % 5u == 0u) {
    out[3] = 255;
  } else if (x % 7u == 0u) {
    out[3] = 0;
  } else {
    out[3] = (unsigned char)(64u + ((x * 29u + y * 17u) & 127u));
  }
}

/** The pixel format of @p format. */
static inline GIMG_Pixel_Format rf_pixel_format(GSHCPU_Format format) {
  GIMG_Pixel_Format f;
  memset(&f, 0, sizeof f);
  f.channel_model = GIMG_CHANNEL_RGBA;
  f.layout = GIMG_LAYOUT_INTERLEAVED;
  f.channel_count = 4;
  int wide = format == GSHCPU_FORMAT_RGBAF32_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
  f.channel_type = wide ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  for (int i = 0; i < 4; i++) {
    f.bits_per_channel[i] = (uint8_t)(wide ? 32 : 8);
  }
  f.alpha_form = (uint8_t)(format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED
          ? GIMG_ALPHA_PREMULTIPLIED
          : GIMG_ALPHA_STRAIGHT);
  return f;
}

/**
 * A raster of @p format holding the backdrop: the 8-bit straight values as they are, premultiplied in integers
 * (`(c * a + 127) / 255`), or as floats (`v / 255`, and their product for premultiplied).
 */
static inline GIMG_Raster * rf_raster(GSHCPU_Format format, uint32_t w, uint32_t h) {
  GIMG_Pixel_Format f = rf_pixel_format(format);
  GIMG_Raster * r = NULL;
  if (gimg_raster_create(w, h, &f, GIMG_RASTER_OWNED, NULL, 0, &r) != GIMG_OK) {
    return NULL;
  }
  unsigned char * px = (unsigned char *)gimg_raster_pixels(r);
  size_t stride = gimg_raster_stride_bytes(r);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      unsigned char s[4];
      rf_backdrop8(x, y, s);
      if (format == GSHCPU_FORMAT_RGBA8_STRAIGHT) {
        memcpy(px + y * stride + x * 4u, s, 4);
      } else if (format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED) {
        unsigned char p[4];
        for (int i = 0; i < 3; i++) {
          p[i] = (unsigned char)(((unsigned)s[i] * s[3] + 127u) / 255u);
        }
        p[3] = s[3];
        memcpy(px + y * stride + x * 4u, p, 4);
      } else {
        float v[4];
        double a = (double)s[3] / 255.0;
        for (int i = 0; i < 4; i++) {
          double c = (double)s[i] / 255.0;
          v[i] = (float)(format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED && i < 3 ? c * a : c);
        }
        memcpy(px + y * stride + x * 16u, v, sizeof v);
      }
    }
  }
  return r;
}

/** The four formats, in their committed order. */
static const GSHCPU_Format RF_FORMATS[4] = {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED,
    GSHCPU_FORMAT_RGBAF32_STRAIGHT, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED};

/**
 * Render fixture @p index on a fresh raster of @p format with a fresh sink and give the render's hash.
 *
 * @return The render's result (shape-scene's), or -1 when the fixture could not be built.
 */
static inline int rf_render(size_t index, GSHCPU_Format format, uint64_t * hash, GIMG_Raster ** keep) {
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * list = RF_FIXTURES[index].build(&c);
  GIMG_Raster * raster = rf_raster(format, RF_WIDTH, RF_HEIGHT);
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  int result = -1;
  if (!c.failed && list && raster && gshcpu_sink_create(NULL, NULL, NULL, &sink) == GSHCPU_OK &&
      gshscene_report_create(NULL, NULL, &report) == GSHSCENE_OK) {
    GSHSCENE_Target target;
    memset(&target, 0, sizeof target);
    target.kind = GSHSCENE_TARGET_RASTER;
    target.surface = raster;
    target.map = c.target_map;
    GSHSCENE_Render_Request request;
    memset(&request, 0, sizeof request);
    request.list = list;
    request.target = &target;
    request.report = report;
    result = (int)gshscene_sink_render(sink, &request);
    if (gshcpu_render_hash(raster, NULL, hash) != GSHCPU_OK) {
      result = -1;
    }
  }
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  rf_release(&c);
  if (keep) {
    *keep = raster;
  } else {
    gimg_raster_destroy(raster);
  }
  return result;
}

#endif /* GHOTI_IO_GSHCPU_TEST_RENDER_FIXTURES_H */
