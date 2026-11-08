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
 * | operators | each of the thirteen operators of N4, a group per cell, each cell clipped |
 * | order | a depth group with a tie broken by layer, and an overlay group |
 * | blend-spaces | groups in linear sRGB, Display P3 and a gamma, nested, with paints in Adobe RGB, BT.2020 and P3 |
 * | foreground | foreground paints, the list's foreground in Display P3 |
 * | picture | a picture (a fill and a clipped group) instanced twice, rotated and scaled |
 * | glyphs | a glyph run of two outline glyphs (curves, a rational arc, a counter) |
 * | maps | a skewed fill, a rotated clip, and the target's map |
 * | font-fast | (story 14a) runs from the test provider's faces on the fast tier: unhinted at a scaled map and fractional origins, hinted, and a face without a program |
 * | font-general | (story 14a) the same runs on the general route: rotated, and at a size that is no integer ppem |
 * | gradient-linear | (color-paint story P2) linear gradients: pad, repeat with an isoline direction, reflect under a turned gradient map, a paint alpha |
 * | gradient-radial | the two-circle cone (the start circle outside the end), concentric repeat, a negative start radius under reflect, a strip |
 * | gradient-sweep | sweeps in degrees (pad), turns (repeat), radians (reflect) and with equal angles (pad's step) |
 * | gradient-paints | interpolation in linear sRGB premultiplied and in a P3 gamma, foreground and palette stops, equal offsets, a group in another blend space under a clip, a picture instanced turned, and two fills that draw nothing (degenerate, a flat map) |
 * | blend-separable | (color-paint story P3) each of the eleven separable blend modes, a group per cell over a partly transparent backdrop, with partial coverage, a clip, and opacity 0.8 on every other cell |
 * | blend-non-separable | the four non-separable modes over a saturated and a grey backdrop, in the target's sRGB-encoded space and inside a linear-light parent group (B5), and a blending group with a space of its own |
 * | strokes-joins | (strokes story S4) a zigzag stroked with each of the five joins (a miter limit that makes one miter a bevel), a closed curve, a tight corner |
 * | strokes-caps | open lines with each cap, dashes with each cap and dots, a path-scoped pattern across subpaths, degenerate subpaths drawn and omitted |
 * | strokes-widths | local width under a non-uniform scale (the elliptical pen), device width under a scale, hairlines, a wide stroke, a width under a quarter of a pixel |
 * | strokes-align | inside and outside alignment of a square, a star under both rules, a figure of a closed and an open subpath, dashed and with round joins |
 * | strokes-paints | linear, radial and sweep gradients, a foreground, a translucent and a premultiplied-space color on strokes, with a fill under each |
 * | strokes-groups | strokes in isolated groups (opacity, multiply, a clip), a fill and a stroke of one element, strokes of one path at whole-pixel translations and at a fraction |
 * | strokes-sparse | long hairlines and plain strokes, flat and slanted, far longer than the raster: the sparse composite is taken |
 * | strokes-nothing | strokes that draw nothing: width 0, a singular map, a singular product, a degenerate gradient |
 * | strokes-dash-overlap | (strokes story S4b) translucent dashes with square, butt and round caps whose gaps are shorter than the caps' reach, a hairline of square-cap dashes, and square-cap dashes round a circle |
 * | strokes-tight-joins | a zigzag whose segments are shorter than the stroke is wide, with each of the five joins, a device width and a non-uniform map: the inner side of every join overlaps |
 * | strokes-rings | a circle and a lens, the lens turned, stroked from one pixel to wider than the radius: the triangles of the inner side overlap |
 * | strokes-text | (strokes story S5) runs from the test provider's font, stroked on the general route: a hinted fill with the unhinted stroke over it and the reverse order, translucent glyphs that overlap (one union), a gradient across a run, inside alignment, dashes with round caps, an elliptical pen and a hairline |
 * | strokes-text-outlines | (strokes story S5) runs from outlines, with open subpaths: square-cap dashes, a path-scoped pattern across the glyphs, a wide stroke of a run under a rotation, and a run of one glyph beside a run of two |
 * | strokes-thin-aligned | aligned strokes thinner than two pixels (a square and a circle inside and outside, thin rectangles at fractional offsets, a star under even-odd, a closed subpath beside an open one): the aligned part and the fill's edge lie in every pixel |
 * | image-filters | (masks-images-patterns story I3) the three point filters magnified, the area filter minified at a fraction and at a half pixel, a unit scale at an integer translation (a copy) and at a half pixel, a map that stretches one way and shrinks the other, and an image turned 30 and 90 degrees |
 * | image-edges | the four edge modes on x against the four on y over one small image, drawn past its edges |
 * | image-formats | twelve layouts, alpha forms and sample types (8, 12, 16 bits and floats), and alpha-only images with a solid, a foreground and a palette tint |
 * | image-spaces | one image filtered in its own space and in linear light, wide-gamut and gamma images on an sRGB target, and a group in linear light holding images in a space of their own |
 * | image-compose | an image fill in a group (multiply, opacity) under a clip, a star filled with a repeated image, a stroke painted with an image, a picture holding an image fill instanced turned, and a 1 by 1 image repeated a thousand times per pixel |
 * | image-nothing | a singular map (a notice), a window of an image repeated, a zero alpha multiplier, and one drawn |
 * | mask-alpha | (masks-images-patterns story M2) alpha masks: the identity transfer over a star, translucent and gradient fills, a stroke and a clipped fill, a nested group, partial coverage at the edges |
 * | mask-luminance | luminance masks: each coefficient set over saturated colors, the picture rendered in linear light and the luminance taken in sRGB, a nonblack backdrop, gray, a color past [0, 1] |
 * | mask-transfers | an exponential, a table and a stitching transfer on alpha and luminance masks, and a transfer with a nonzero value at 0 |
 * | mask-outside | the value outside everything the picture draws: an empty picture, partial coverage, a nonzero transfer at 0, a nonblack backdrop |
 * | mask-acts-on | a mask acting on the opacity and on the shape under source-over, multiply, plus, dest-in, src, src-in, src-out and clear |
 * | mask-nested | a mask of a mask three deep, a masked group inside an instanced picture, a mask picture holding an instance |
 * | mask-frames | the placement of a mask: a scale, a translation, a turn, a shear, a swap of the axes, an instance under a map, a singular placement |
 * | mask-compose | a clip, a mask, an opacity and a blend mode on one group; a masked group in linear light with a mask rendered in Display P3; an unbounded operator on a small group |
 * | mask-not-a-clip | two abutting halves as an alpha mask (3/4 where they share a pixel) beside the exact union as a clip (whole) |
 * | blend-out-of-range | blend modes on sources out of range (negative, above 1, the recorded bound 2^16), dodge and burn past their poles, ClipColor with both steps, in a gamma-0.1 parent, nested blending groups, and a blend over the backdrop's transparent pixels |
 * | pattern-lattices | (masks-images-patterns story R2) cells that abut at a fraction, gapped under a skew (CSS `space`), scaled, and rotated |
 * | pattern-overlap | cells that overlap in x, in y and in both, one repeating on one axis only, and a tile whose content spills past its cell |
 * | pattern-content | a tile of a gradient, an image and the foreground across a cell edge, groups and a clip block, an uncolored pattern under a tint |
 * | pattern-strokes | strokes in a tile crossing the cell's edge, a stroke painted with a pattern, and a pattern inside a tile |
 * | pattern-integer | an axis-aligned lattice of whole-pixel steps |
 * | pattern-period | hatch tiles on lattices of whole-pixel steps, which the period tier draws from one window: abutting at a fraction, skewed (8, 3) and (0, 5), turned by an integer rotation, and gapped |
 * | pattern-period-blend | whole-pixel lattices whose tile blends: overlapping cells under a multiply group and a non-isolated group, an uncolored hatch, the foreground and a gradient |
 * | pattern-hatch | strokes that end at a cell's edge and run on into the next copy, horizontal, vertical and diagonal, with butt and square caps: the lines are unbroken |
 * | hatch-families | (masks-images-patterns story H1) a single 45 degree family, a translucent crosshatch (one union), dashes, dots under a round cap, a brick stagger, and a line along the region's edge |
 * | hatch-nesting | three nested squares hatched by the rule (nonzero and even-odd), by the outermost ring and by everything inside the outer loop |
 * | hatch-curved | a circle with an island, a hairline pen and thick pens with each cap, solid and dashed families |
 * | hatch-maps | a hatch under a rotation and a non-uniform scale (its own map and the item's), in a translucent group, and in a pattern's tile |
 * | union-overlap | (shape-exact-primitives story A3b) union clips whose members overlap: a star (its centre wound twice), a blob and a rectangle under the nonzero and even-odd rules, and a second union of tilted rectangles; the exact area of the union at every edge |
 * | union-groups | groups of overlapping members beside members that stand alone: a ring with a triangle across its hole, two crossing rectangles, a tripled contour that overlaps itself and a star apart |
 * | union-nested | a union under a rotation inside a clip block inside a translucent group, its members each under a map of their own, and a union of members that touch at a quarter pixel with two that overlap |
 * | union-abutting | rectangles that share edges at quarter pixels and a corner, a union of triangles that share a diagonal, and the same members as a union beside an overlapping one |
 * | antialias-fills | (shape-scene AA1 to AA6) fills that ask for no antialiasing beside ones that do not: a star under both rules, rectangles on whole pixels and on centers, two that share an edge in translucent paint, a blob in a clip and in an opacity group, a glyph, and a gradient, a pattern and a hatch drawn through the coverage |
 * | antialias-strokes | strokes that ask for no antialiasing: joins and caps, dashes, a hairline, a device width, an elliptical pen, a closed square in each alignment, a gradient, and a stroke of a glyph's outlines, beside one that does not |
 * | pattern-union | (shape-exact-primitives story CU21) union clips in a pattern's tile: members that cross the cell edges on an abutting lattice at a fraction, three members (a star under even-odd, a blob and a turned bar) on cells that overlap, a union inside a clip under a gapped skew, and a gradient under a union on a lattice of whole-pixel steps |
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
  const GSHSCENE_Resource * held[192];
  size_t held_count;
  GSHSCENE_List * lists[24];
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
  /* The thirteen of N4, pinned: the blend modes (B1) have fixtures of their own, and this golden stays what it was. */
  for (uint32_t op = 0; op <= GSHSCENE_COMPOSITE_PLUS; op++) {
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

/* --- gradients (color-paint story P2) ------------------------------------------------------------- */

static inline GSHSCENE_Paint rf_gradient_paint(const GSHSCENE_Resource * g, double alpha) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_GRADIENT;
  p.resource = g;
  p.rgba[3] = alpha;
  return p;
}

static inline GSHSCENE_Gradient_Stop rf_stop(double offset, GSHSCENE_Paint color) {
  GSHSCENE_Gradient_Stop s;
  memset(&s, 0, sizeof s);
  s.offset = offset;
  s.color = color;
  return s;
}

/** Intern @p g (its map the identity when its dimension is zero) with @p count stops. */
static inline const GSHSCENE_Resource * rf_gradient(RF_Ctx * c, GSHSCENE_Gradient g, const GSHSCENE_Gradient_Stop * stops,
    size_t count) {
  if (g.map.dim == 0) {
    gshgeom_map_identity(2, &g.map);
  }
  g.stops = stops;
  g.stop_count = count;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_gradient(c->store, &g, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

static inline GSHSCENE_Gradient rf_linear(double x0, double y0, double x1, double y1, double dx, double dy, uint32_t extend) {
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p0[0] = x0;
  g.p0[1] = y0;
  g.p1[0] = x1;
  g.p1[1] = y1;
  g.d[0] = dx;
  g.d[1] = dy;
  g.extend = extend;
  return g;
}

static inline GSHSCENE_Gradient rf_radial(double c0x, double c0y, double r0, double c1x, double c1y, double r1,
    uint32_t extend) {
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_RADIAL;
  g.c0[0] = c0x;
  g.c0[1] = c0y;
  g.r0 = r0;
  g.c1[0] = c1x;
  g.c1[1] = c1y;
  g.r1 = r1;
  g.extend = extend;
  return g;
}

static inline GSHSCENE_Gradient rf_sweep(double cx, double cy, uint32_t unit, double a0, double a1, uint32_t extend) {
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_SWEEP;
  g.center[0] = cx;
  g.center[1] = cy;
  g.angle_unit = unit;
  g.a0 = a0;
  g.a1 = a1;
  g.extend = extend;
  return g;
}

static inline GSHSCENE_List * rf_gradient_linear(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Gradient_Stop three[3] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)),
      rf_stop(0.4, rf_solid(0.1, 0.8, 0.2, 0.6)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.25, rf_solid(1.0, 0.9, 0.2, 1.0)),
      rf_stop(0.75, rf_solid(0.2, 0.1, 0.5, 0.8))};
  /* pad, from (6, 0) to (42, 0) */
  rf_fill(c, l, rf_rect(c, 2, 2, 46, 13), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_linear(6, 0, 42, 0, 0, 0, GSHSCENE_EXTEND_PAD), three, 3), 1.0), 0, 0.0);
  /* repeat, with an isoline direction that slants the bands */
  rf_fill(c, l, rf_rect(c, 2, 14, 46, 26), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_linear(10, 20, 22, 20, 1.0, 2.0, GSHSCENE_EXTEND_REPEAT), two, 2), 0.8), 0, 0.0);
  /* reflect, under a gradient map turned 30 degrees and scaled */
  GSHSCENE_Gradient r = rf_linear(0, 0, 9, 0, 0, 0, GSHSCENE_EXTEND_REFLECT);
  GSHGEOM_Map rot;
  GSHGEOM_Map scale;
  GSHGEOM_Map move;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &rot);
  gshgeom_map_scale(2, 1.5, 0.75, 1.0, &scale);
  gshgeom_map_translate(2, 24.0, 32.0, 0.0, &move);
  gshgeom_map_compose(&rot, &scale, &r.map);
  gshgeom_map_compose(&move, &r.map, &r.map);
  rf_fill(c, l, rf_star(c, 24.0, 32.0, 9.0), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, r, three, 3), 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_gradient_radial(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Gradient_Stop three[3] = {rf_stop(0.0, rf_solid(1.0, 1.0, 0.8, 1.0)),
      rf_stop(0.5, rf_solid(0.9, 0.3, 0.1, 0.9)), rf_stop(1.0, rf_solid(0.2, 0.0, 0.3, 0.7))};
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.1, 0.6, 0.9, 1.0)),
      rf_stop(1.0, rf_solid(0.9, 0.9, 0.9, 0.5))};
  /* a cone: the start circle lies outside the end circle */
  rf_fill(c, l, rf_rect(c, 1, 1, 24, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_radial(4, 4, 2, 15, 12, 7, GSHSCENE_EXTEND_PAD), three, 3), 1.0), 0, 0.0);
  /* concentric, repeat */
  rf_fill(c, l, rf_blob(c, 25.0, 1.0, 20.0), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_radial(35, 11, 0, 35, 11, 4.5, GSHSCENE_EXTEND_REPEAT), two, 2), 1.0), 0, 0.0);
  /* a negative start radius (COLRv1), reflect */
  rf_fill(c, l, rf_rect(c, 1, 21, 24, 39), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_radial(8, 30, -3, 16, 28, 6, GSHSCENE_EXTEND_REFLECT), three, 3), 0.9), 0, 0.0);
  /* a strip: equal radii, distinct centers */
  rf_fill(c, l, rf_rect(c, 25, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_radial(28, 26, 4, 40, 34, 4, GSHSCENE_EXTEND_PAD), two, 2), 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_gradient_sweep(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Gradient_Stop four[4] = {rf_stop(0.0, rf_solid(1.0, 0.0, 0.0, 1.0)),
      rf_stop(0.3, rf_solid(1.0, 1.0, 0.0, 1.0)), rf_stop(0.7, rf_solid(0.0, 0.6, 1.0, 0.8)),
      rf_stop(1.0, rf_solid(0.5, 0.0, 0.5, 1.0))};
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.0, 0.0, 0.0, 1.0)),
      rf_stop(1.0, rf_solid(1.0, 1.0, 1.0, 1.0))};
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_sweep(12, 10.5, GSHGEOM_ANGLE_DEGREES, 30, 300, GSHSCENE_EXTEND_PAD), four, 4), 1.0),
      0, 0.0);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_sweep(36, 10, GSHGEOM_ANGLE_TURNS, 0.125, 0.375, GSHSCENE_EXTEND_REPEAT), two, 2),
          1.0),
      0, 0.0);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_sweep(11.5, 30, GSHGEOM_ANGLE_RADIANS, -1.0, 2.0, GSHSCENE_EXTEND_REFLECT), four, 4),
          0.75),
      0, 0.0);
  rf_fill(c, l, rf_blob(c, 25.0, 21.0, 18.0), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_sweep(34, 30, GSHGEOM_ANGLE_DEGREES, 135, 135, GSHSCENE_EXTEND_PAD), two, 2), 1.0),
      0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_gradient_paints(RF_Ctx * c) {
  GSHSCENE_Color_Space lin = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  GSHSCENE_Color_Space p3g = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
  /* the palette of the list, and its foreground */
  GSHSCENE_Color colors[2];
  memset(colors, 0, sizeof colors);
  colors[0].rgba[0] = 0.2;
  colors[0].rgba[1] = 0.7;
  colors[0].rgba[2] = 0.3;
  colors[0].rgba[3] = 1.0;
  colors[1].rgba[0] = 0.95;
  colors[1].rgba[1] = 0.5;
  colors[1].rgba[2] = 0.05;
  colors[1].rgba[3] = 0.85;
  colors[1].space = rf_space(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_SRGB, 0.0);
  const GSHSCENE_Resource * palette = NULL;
  rf_check(c, gshscene_store_intern_palette(c->store, 2, colors, &palette) == GSHSCENE_OK);
  rf_hold(c, palette);
  GSHSCENE_Paint entry1;
  memset(&entry1, 0, sizeof entry1);
  entry1.kind = GSHSCENE_PAINT_PALETTE;
  entry1.entry = 1;
  entry1.rgba[3] = 1.0;
  GSHSCENE_Paint current;
  memset(&current, 0, sizeof current);
  current.kind = GSHSCENE_PAINT_FOREGROUND;
  current.rgba[3] = 0.7;
  /* a picture: one gradient fill, instanced turned */
  GSHSCENE_List * inner = rf_open(c);
  const GSHSCENE_Gradient_Stop pic[2] = {rf_stop(0.0, current), rf_stop(1.0, rf_solid_in(0.0, 0.2, 1.0, 1.0, lin))};
  GSHSCENE_Gradient pg = rf_linear(0, 0, 10, 10, 0, 0, GSHSCENE_EXTEND_REFLECT);
  pg.space = lin;
  rf_fill(c, inner, rf_rect(c, 0, 0, 10, 10), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(rf_gradient(c, pg, pic, 2), 1.0),
      0, 0.0);
  rf_close(c, inner);
  const GSHSCENE_Resource * picture = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, inner, &picture) == GSHSCENE_OK);
  rf_hold(c, picture);
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Color fg;
  memset(&fg, 0, sizeof fg);
  fg.rgba[0] = 0.85;
  fg.rgba[1] = 0.25;
  fg.rgba[2] = 0.6;
  fg.rgba[3] = 0.9;
  fg.space = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  rf_check(c, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
  rf_check(c, gshscene_list_set_palette(l, palette) == GSHSCENE_OK);
  /* linear sRGB, premultiplied, through a transparent stop; equal offsets make a hard edge */
  const GSHSCENE_Gradient_Stop hard[4] = {rf_stop(0.0, rf_solid_in(1.0, 0.1, 0.1, 1.0, lin)),
      rf_stop(0.5, rf_solid_in(0.0, 1.0, 0.0, 0.0, lin)), rf_stop(0.5, rf_solid_in(0.1, 0.1, 1.0, 1.0, lin)),
      rf_stop(1.0, entry1)};
  GSHSCENE_Gradient a = rf_linear(2, 0, 30, 0, 0, 0, GSHSCENE_EXTEND_PAD);
  a.space = lin;
  a.premultiplied = 1;
  rf_fill(c, l, rf_rect(c, 1, 1, 33, 12), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(rf_gradient(c, a, hard, 4), 1.0), 0,
      0.0);
  /* a P3 gamma interpolation of a palette, a foreground and an sRGB stop, in a linear group under a clip */
  const GSHSCENE_Gradient_Stop mix[3] = {rf_stop(-0.25, current), rf_stop(0.5, rf_solid_in(0.1, 0.9, 0.8, 1.0, rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0))),
      rf_stop(1.25, entry1)};
  GSHSCENE_Gradient b = rf_radial(20, 24, 1, 22, 26, 13, GSHSCENE_EXTEND_REPEAT);
  b.space = p3g;
  GSHSCENE_Group grp = rf_group(0.85, GSHSCENE_COMPOSITE_SRC_OVER);
  grp.blend_space = lin;
  rf_check(c, gshscene_list_clip_push(l, rf_star(c, 20.0, 25.0, 14.0), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_push(l, &grp, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 4, 12, 38, 39), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(rf_gradient(c, b, mix, 3), 0.9), 0,
      0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  /* the picture, turned */
  GSHGEOM_Map rot;
  GSHGEOM_Map move;
  GSHGEOM_Map placed;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 20.0, &rot);
  gshgeom_map_translate(2, 36.0, 2.0, 0.0, &move);
  gshgeom_map_compose(&move, &rot, &placed);
  rf_check(c, gshscene_list_instance(l, picture, &placed, 0, 0.0) == GSHSCENE_OK);
  /* two fills that draw nothing: a degenerate line, and a gradient under a flat map */
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(1, 0, 0, 1)), rf_stop(1.0, rf_solid(0, 0, 1, 1))};
  rf_fill(c, l, rf_rect(c, 36, 20, 46, 30), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_linear(40, 25, 40, 25, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0), 0, 0.0);
  GSHSCENE_Gradient flat = rf_linear(0, 0, 4, 0, 0, 0, GSHSCENE_EXTEND_PAD);
  const double m[4] = {1.0, 0.0, 2.0, 0.0};
  const double t[2] = {0.0, 0.0};
  gshgeom_map_from_matrix(2, m, t, &flat.map);
  rf_fill(c, l, rf_rect(c, 36, 31, 46, 39), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(rf_gradient(c, flat, two, 2), 1.0),
      0, 0.0);
  rf_close(c, l);
  return l;
}

/** The fixtures, in their committed order. */
typedef GSHSCENE_List * (*RF_Build)(RF_Ctx * c);

/* --- blend modes (color-paint story P3, B1 to B14) ----------------------------------------------------------------- */

/** One cell of a blend fixture: a clipped backdrop fill, then a group of @p op over it holding @p src. */
static inline void rf_blend_cell(RF_Ctx * c, GSHSCENE_List * l, uint32_t op, double x, double y, double opacity,
    GSHSCENE_Paint back, GSHSCENE_Paint src, const GSHSCENE_Color_Space * group_space) {
  GSHSCENE_Group g = rf_group(opacity, op);
  if (group_space) {
    g.blend_space = *group_space;
  }
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, x, y, x + 8.5, y + 12.25), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) ==
      GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, x, y + 1.5, x + 6.0, y + 9.0), NULL, GSHGEOM_FILL_NONZERO, back, 0, 0.0);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_blob(c, x + 2.25, y + 3.75, 6.5), NULL, GSHGEOM_FILL_NONZERO, src, 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
}

static inline GSHSCENE_List * rf_blend_separable(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  for (uint32_t i = 0; i <= GSHSCENE_COMPOSITE_MULTIPLY - GSHSCENE_COMPOSITE_SCREEN; i++) {
    double x = 1.0 + 9.0 * (double)(i % 5);
    double y = 1.0 + 13.0 * (double)(i / 5);
    rf_blend_cell(c, l, GSHSCENE_COMPOSITE_SCREEN + i, x, y, i % 2 ? 0.8 : 1.0, rf_solid(0.2, 0.55, 0.9, 0.85),
        rf_solid(0.95, 0.45, 0.1, 0.7), NULL);
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_blend_non_separable(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Group linear = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  linear.blend_space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  const GSHSCENE_Color_Space p3 = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_LINEAR, 0.0);
  for (uint32_t i = 0; i < 4; i++) {
    uint32_t op = GSHSCENE_COMPOSITE_HUE + i;
    double x = 1.0 + 9.0 * (double)i;
    /* Row 1: in the target's space, over a saturated backdrop; row 2: over a grey one; row 3: inside a linear parent. */
    rf_blend_cell(c, l, op, x, 1.0, 1.0, rf_solid(0.9, 0.2, 0.3, 0.9), rf_solid(0.2, 0.4, 0.95, 0.8), NULL);
    rf_blend_cell(c, l, op, x, 14.0, 0.8, rf_solid(0.5, 0.5, 0.5, 1.0), rf_solid(0.1, 0.8, 0.3, 0.75), NULL);
    rf_check(c, gshscene_list_group_push(l, &linear, 0, 0.0) == GSHSCENE_OK);
    rf_blend_cell(c, l, op, x, 27.0, 1.0, rf_solid(0.9, 0.2, 0.3, 0.9), rf_solid(0.2, 0.4, 0.95, 0.8), i == 3 ? &p3 : NULL);
    rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_blend_out_of_range(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  static const uint32_t ops[5] = {GSHSCENE_COMPOSITE_COLOR_DODGE, GSHSCENE_COMPOSITE_COLOR_BURN,
      GSHSCENE_COMPOSITE_SOFT_LIGHT, GSHSCENE_COMPOSITE_LUMINOSITY, GSHSCENE_COMPOSITE_DIFFERENCE};
  for (uint32_t i = 0; i < 5; i++) {
    double x = 1.0 + 9.0 * (double)i;
    /* Above 1 and negative, the recorded bound, and dodge's pole approached from above (Cs = 1 + 2^-40). */
    rf_blend_cell(c, l, ops[i], x, 1.0, 1.0, rf_solid(0.6, 0.3, 0.8, 0.9), rf_solid(1.5, -0.25, 2.0, 0.75), NULL);
    rf_blend_cell(c, l, ops[i], x, 14.0, 1.0, rf_solid(-0.5, 1.25, 0.5, 1.0),
        rf_solid(65536.0, 1.0 + 0x1p-40, -65536.0, 0.5), NULL);
  }
  /* In a gamma-0.1 parent (its encode raises the out-of-range values toward the saturation), nested blending groups. */
  GSHSCENE_Group gamma = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  gamma.blend_space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0.1);
  GSHSCENE_Group outer = rf_group(0.9, GSHSCENE_COMPOSITE_MULTIPLY);
  rf_check(c, gshscene_list_group_push(l, &gamma, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 1.0, 27.5, 30.0, 39.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.25, 0.5, -0.5, 0.8), 0, 0.0);
  rf_check(c, gshscene_list_group_push(l, &outer, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_blob(c, 4.0, 28.0, 10.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.75, 1.5, 0.25, 0.9), 0, 0.0);
  rf_blend_cell(c, l, GSHSCENE_COMPOSITE_HARD_LIGHT, 14.0, 27.0, 1.0, rf_solid(0.3, -0.2, 0.9, 0.7),
      rf_solid(-1.0, 0.5, 3.0, 0.6), NULL);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  /* Over the backdrop alone (its transparent and partial pixels): no fill under the blend. */
  GSHSCENE_Group screen = rf_group(1.0, GSHSCENE_COMPOSITE_SCREEN);
  rf_check(c, gshscene_list_group_push(l, &screen, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 31.0, 27.5, 47.0, 39.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.4, 0.9, -0.3, 0.65), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

/* --- strokes (strokes story S4) -------------------------------------------------------------------------------- */

/** A style: every field named, the dashes copied by the store. */
static inline const GSHSCENE_Resource * rf_style(RF_Ctx * c, double width, uint32_t space, uint32_t join, double miter,
    uint32_t cap, uint32_t degenerate, const double * dashes, size_t dash_count, double offset, uint32_t scope,
    uint32_t align, GSHGEOM_Fill_Rule rule) {
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = width;
  st.width_space = space;
  st.join = join;
  st.miter_limit = miter;
  st.cap = cap;
  st.degenerate = degenerate;
  st.dash_count = dash_count;
  st.dashes = dashes;
  st.dash_offset = offset;
  st.dash_scope = scope;
  st.alignment = align;
  st.alignment_rule = rule;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_stroke_style(c->store, &st, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

/** A plain style: width, join, cap, local, centered. */
static inline const GSHSCENE_Resource * rf_pen(RF_Ctx * c, double width, uint32_t join, uint32_t cap) {
  return rf_style(c, width, GSHSCENE_STROKE_WIDTH_LOCAL, join, 4.0, cap, GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0,
      GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO);
}

static inline void rf_stroke(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * path, const GSHSCENE_Resource * style,
    const GSHGEOM_Map * map, GSHSCENE_Paint paint) {
  rf_check(c, gshscene_list_stroke(l, path, style, map, &paint, 0, 0.0) == GSHSCENE_OK);
}

/** A polyline through @p n points, closed or open. */
static inline const GSHSCENE_Resource * rf_polyline(RF_Ctx * c, const double * xy, size_t n, int closed) {
  GSHGEOM_Path * p = rf_path(c);
  for (size_t i = 0; i < n; i++) {
    i == 0 ? gshgeom_path_move_to(p, rf_pt(xy[2 * i], xy[2 * i + 1])) : gshgeom_path_line_to(p, rf_pt(xy[2 * i], xy[2 * i + 1]));
  }
  if (closed) {
    gshgeom_path_close(p);
  }
  return rf_intern(c, p);
}

static inline GSHGEOM_Map rf_move(double tx, double ty) {
  GSHGEOM_Map m;
  gshgeom_map_translate(2, tx, ty, 0.0, &m);
  return m;
}

static inline GSHSCENE_List * rf_strokes_joins(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  static const double zig[10] = {0.0, 8.0, 5.0, 1.0, 10.0, 8.0, 14.0, 2.0, 17.0, 7.0};
  const GSHSCENE_Resource * z = rf_polyline(c, zig, 5, 0);
  static const uint32_t joins[5] = {GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_JOIN_BEVEL,
      GSHSCENE_STROKE_JOIN_MITER_CLIP, GSHSCENE_STROKE_JOIN_ARCS};
  for (int i = 0; i < 5; i++) {
    GSHGEOM_Map m = rf_move(2.0 + 14.5 * (double)(i % 3) + 0.25 * i, 2.5 + 15.0 * (double)(i / 3) + 0.125 * i);
    GSHSCENE_Paint paint = rf_solid(0.1 + 0.2 * i, 0.5, 0.9 - 0.15 * i, 1.0);
    /* the zigzag's sharp corners exceed a miter limit of 2: a bevel under miter, a clipped miter under miter-clip and arcs */
    rf_stroke(c, l, z, rf_style(c, 2.5, GSHSCENE_STROKE_WIDTH_LOCAL, joins[i], 2.0, GSHSCENE_STROKE_CAP_BUTT,
                                   GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                   GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
        &m, paint);
  }
  /* a closed curve with round and miter joins at its corners, translucent */
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(0.0, 6.0));
  gshgeom_path_cubic_to(p, rf_pt(0.0, 0.0), rf_pt(8.0, 0.0), rf_pt(8.0, 6.0));
  gshgeom_path_line_to(p, rf_pt(4.0, 10.5));
  gshgeom_path_close(p);
  const GSHSCENE_Resource * blob = rf_intern(c, p);
  GSHGEOM_Map m = rf_move(32.5, 19.25);
  rf_stroke(c, l, blob, rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &m, rf_solid(0.8, 0.2, 0.1, 0.7));
  static const double tight[6] = {0.0, 0.0, 12.0, 1.5, 0.0, 3.0};
  GSHGEOM_Map m2 = rf_move(3.0, 30.0);
  rf_stroke(c, l, rf_polyline(c, tight, 3, 0), rf_pen(c, 1.5, GSHSCENE_STROKE_JOIN_MITER_CLIP, GSHSCENE_STROKE_CAP_SQUARE), &m2,
      rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_caps(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  static const double line[6] = {0.0, 0.0, 10.0, 1.5, 17.0, 0.5};
  const GSHSCENE_Resource * ln = rf_polyline(c, line, 3, 0);
  static const uint32_t caps[3] = {GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_CAP_ROUND, GSHSCENE_STROKE_CAP_SQUARE};
  static const double dash[3] = {3.0, 2.0, 1.0};
  static const double dots[2] = {0.0, 2.5};
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map a = rf_move(3.0, 3.0 + 4.0 * i);
    rf_stroke(c, l, ln, rf_pen(c, 2.5, GSHSCENE_STROKE_JOIN_ROUND, caps[i]), &a, rf_solid(0.1, 0.4, 0.8, 1.0));
    GSHGEOM_Map b = rf_move(26.0, 3.0 + 4.0 * i);
    rf_stroke(c, l, ln, rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, caps[i],
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, dash, 3, 0.75, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                    GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
        &b, rf_solid(0.9, 0.3, 0.1, 1.0));
  }
  /* dots: zero-length dashes under round and square caps (and butt: nothing) */
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map m = rf_move(3.0, 18.0 + 3.5 * i);
    rf_stroke(c, l, ln, rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, caps[i],
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, dots, 2, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                    GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
        &m, rf_solid(0.2, 0.7, 0.3, 1.0));
  }
  /* one pattern for the path across two subpaths, and degenerate subpaths drawn and omitted */
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(0.0, 0.0));
  gshgeom_path_line_to(p, rf_pt(9.0, 0.0));
  gshgeom_path_move_to(p, rf_pt(0.0, 4.0));
  gshgeom_path_line_to(p, rf_pt(9.0, 4.0));
  const GSHSCENE_Resource * two = rf_intern(c, p);
  GSHGEOM_Map m = rf_move(27.0, 18.0);
  rf_stroke(c, l, two, rf_style(c, 1.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                   GSHSCENE_STROKE_DEGENERATE_DRAW, dash, 3, 0.0, GSHSCENE_STROKE_DASH_PATH,
                                   GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &m, rf_solid(0.5, 0.2, 0.6, 1.0));
  GSHGEOM_Path * d = rf_path(c);
  gshgeom_path_move_to(d, rf_pt(2.0, 2.0));
  gshgeom_path_line_to(d, rf_pt(2.0, 2.0));
  gshgeom_path_move_to(d, rf_pt(8.0, 2.0));
  gshgeom_path_close(d);
  gshgeom_path_move_to(d, rf_pt(14.0, 2.0));
  gshgeom_path_line_to(d, rf_pt(14.0, 2.0));
  const GSHSCENE_Resource * dd = rf_intern(c, d);
  GSHGEOM_Map m1 = rf_move(3.0, 32.0);
  GSHGEOM_Map m2 = rf_move(3.0, 36.0);
  rf_stroke(c, l, dd, rf_style(c, 4.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_ROUND,
                                  GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                  GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &m1, rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_stroke(c, l, dd, rf_style(c, 4.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_SQUARE,
                                  GSHSCENE_STROKE_DEGENERATE_OMIT, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                  GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &m2, rf_solid(0.7, 0.1, 0.1, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_widths(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  static const double tri[8] = {0.0, 8.0, 5.0, 0.0, 10.0, 8.0, 0.0, 8.0};
  const GSHSCENE_Resource * t = rf_polyline(c, tri, 4, 1);
  GSHGEOM_Map scale;
  GSHGEOM_Map move;
  GSHGEOM_Map map;
  /* local width under a non-uniform scale: the pen is an ellipse */
  gshgeom_map_scale(2, 2.0, 0.75, 1.0, &scale);
  gshgeom_map_translate(2, 3.5, 4.25, 0.0, &move);
  gshgeom_map_compose(&move, &scale, &map);
  rf_stroke(c, l, t, rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                 GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                 GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &map, rf_solid(0.2, 0.3, 0.8, 1.0));
  /* device width under the same scale: a fixed 2 pixels around the mapped centerline */
  GSHGEOM_Map map2;
  GSHGEOM_Map move2 = rf_move(26.0, 4.25);
  gshgeom_map_compose(&move2, &scale, &map2);
  rf_stroke(c, l, t, rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                 GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                 GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &map2, rf_solid(0.8, 0.3, 0.2, 1.0));
  /* hairlines of both kinds, a wide stroke, a stroke under a quarter of a pixel */
  static const double zig[10] = {0.0, 6.0, 6.0, 0.0, 12.0, 6.0, 18.0, 0.0, 24.0, 6.0};
  const GSHSCENE_Resource * z = rf_polyline(c, zig, 5, 0);
  GSHGEOM_Map h1 = rf_move(3.0, 18.0);
  GSHGEOM_Map h2 = rf_move(3.5, 26.5);
  GSHGEOM_Map h3 = rf_move(3.25, 33.0);
  rf_stroke(c, l, z, rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                 GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                 GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &h1, rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_stroke(c, l, z, rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_ROUND,
                                 GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                 GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &h2, rf_solid(0.1, 0.5, 0.1, 1.0));
  rf_stroke(c, l, z, rf_pen(c, 0.2, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT), &h3, rf_solid(0.4, 0.1, 0.5, 1.0));
  GSHGEOM_Map wide = rf_move(31.0, 6.0);
  rf_stroke(c, l, t, rf_pen(c, 7.0, GSHSCENE_STROKE_JOIN_BEVEL, GSHSCENE_STROKE_CAP_BUTT), &wide, rf_solid(0.9, 0.7, 0.1, 0.6));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_align(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Resource * sq = rf_rect(c, 0.0, 0.0, 9.0, 9.0);
  const GSHSCENE_Resource * star = rf_star(c, 6.0, 6.0, 6.0);
  static const double dashes[2] = {2.0, 1.0};
  uint32_t aligns[3] = {GSHSCENE_STROKE_ALIGN_CENTER, GSHSCENE_STROKE_ALIGN_INSIDE, GSHSCENE_STROKE_ALIGN_OUTSIDE};
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map m = rf_move(3.0 + 15.0 * i, 3.0);
    rf_stroke(c, l, sq, rf_style(c, 3.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, aligns[i],
                                    GSHGEOM_FILL_NONZERO),
        &m, rf_solid(0.2 + 0.3 * i, 0.4, 0.8 - 0.3 * i, 0.9));
  }
  /* the star under both rules, inside and outside */
  for (int i = 0; i < 4; i++) {
    GSHGEOM_Map m = rf_move(0.5 + 12.0 * i, 16.25);
    rf_stroke(c, l, star, rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                      GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                      i % 2 ? GSHSCENE_STROKE_ALIGN_OUTSIDE : GSHSCENE_STROKE_ALIGN_INSIDE,
                                      i < 2 ? GSHGEOM_FILL_NONZERO : GSHGEOM_FILL_EVEN_ODD),
        &m, rf_solid(0.7, 0.2 + 0.1 * i, 0.3, 1.0));
  }
  /* a closed and an open subpath: the open one stays centered; dashed and aligned */
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(0.0, 0.0));
  gshgeom_path_line_to(p, rf_pt(10.0, 1.0));
  gshgeom_path_line_to(p, rf_pt(9.0, 10.0));
  gshgeom_path_line_to(p, rf_pt(1.0, 9.0));
  gshgeom_path_close(p);
  gshgeom_path_move_to(p, rf_pt(12.0, 2.0));
  gshgeom_path_line_to(p, rf_pt(20.0, 12.0));
  const GSHSCENE_Resource * mixed = rf_intern(c, p);
  GSHGEOM_Map m1 = rf_move(2.5, 30.0);
  GSHGEOM_Map m2 = rf_move(26.0, 28.5);
  rf_stroke(c, l, mixed, rf_style(c, 2.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_ROUND,
                                     GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                     GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
      &m1, rf_solid(0.1, 0.6, 0.5, 1.0));
  rf_stroke(c, l, mixed, rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                     GSHSCENE_STROKE_DEGENERATE_DRAW, dashes, 2, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                     GSHSCENE_STROKE_ALIGN_OUTSIDE, GSHGEOM_FILL_NONZERO),
      &m2, rf_solid(0.6, 0.3, 0.1, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_paints(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Gradient_Stop three[3] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)),
      rf_stop(0.5, rf_solid(0.1, 0.8, 0.2, 0.7)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
  static const double wave[12] = {0.0, 6.0, 4.0, 0.0, 8.0, 6.0, 12.0, 0.0, 16.0, 6.0, 20.0, 0.0};
  const GSHSCENE_Resource * w = rf_polyline(c, wave, 6, 0);
  const GSHSCENE_Resource * pen = rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND);
  /* each paint over a fill of its own band, so that a translucent stroke shows what is under it */
  rf_fill(c, l, rf_rect(c, 1, 1, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.9, 0.7, 1.0), 0, 0.0);
  GSHGEOM_Map m1 = rf_move(3.0, 3.0);
  rf_stroke(c, l, w, pen, &m1, rf_gradient_paint(rf_gradient(c, rf_linear(0, 0, 20, 0, 0, 0, GSHSCENE_EXTEND_PAD), three, 3), 1.0));
  GSHGEOM_Map m2 = rf_move(26.0, 3.0);
  rf_stroke(c, l, w, pen, &m2, rf_gradient_paint(rf_gradient(c, rf_radial(10, 3, 0, 10, 3, 12, GSHSCENE_EXTEND_REFLECT), three, 3), 0.9));
  GSHGEOM_Map m3 = rf_move(3.0, 14.0);
  /* a sweep, in a gradient map of the stroke's own item space: the paint is sampled in local space (SK6) */
  rf_stroke(c, l, w, pen, &m3,
      rf_gradient_paint(rf_gradient(c, rf_sweep(10, 3, GSHGEOM_ANGLE_DEGREES, 0.0, 360.0, GSHSCENE_EXTEND_PAD), three, 3), 1.0));
  GSHGEOM_Map m4 = rf_move(26.0, 14.0);
  GSHSCENE_Paint fg;
  memset(&fg, 0, sizeof fg);
  fg.kind = GSHSCENE_PAINT_FOREGROUND;
  fg.rgba[3] = 0.8;
  rf_stroke(c, l, w, pen, &m4, fg);
  GSHGEOM_Map m5 = rf_move(3.0, 26.0);
  rf_stroke(c, l, w, rf_pen(c, 6.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &m5, rf_solid(0.2, 0.4, 0.9, 0.5));
  GSHGEOM_Map m6 = rf_move(26.0, 26.0);
  rf_stroke(c, l, w, rf_pen(c, 6.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_SQUARE), &m6,
      rf_solid_in(0.9, 0.3, 0.2, 0.6, rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0)));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_groups(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Resource * star = rf_star(c, 8.0, 8.0, 8.0);
  const GSHSCENE_Resource * pen = rf_pen(c, 2.5, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND);
  /* a fill and then the stroke of one element (SK14) */
  GSHGEOM_Map a = rf_move(1.0, 1.0);
  rf_fill(c, l, star, &a, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.2, 1.0), 0, 0.0);
  rf_stroke(c, l, star, pen, &a, rf_solid(0.5, 0.1, 0.1, 1.0));
  /* an isolated group of opacity 0.6 holding two overlapping strokes: they composite as one layer */
  GSHSCENE_Group g = rf_group(0.6, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  GSHGEOM_Map b = rf_move(21.0, 1.0);
  GSHGEOM_Map b2 = rf_move(26.0, 4.0);
  rf_stroke(c, l, star, rf_pen(c, 4.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &b, rf_solid(0.1, 0.3, 0.9, 1.0));
  rf_stroke(c, l, star, rf_pen(c, 4.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &b2, rf_solid(0.9, 0.3, 0.1, 1.0));
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  /* a multiply group, clipped */
  GSHSCENE_Group mul = rf_group(1.0, GSHSCENE_COMPOSITE_MULTIPLY);
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 1.0, 20.0, 30.0, 39.0), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_push(l, &mul, 0, 0.0) == GSHSCENE_OK);
  GSHGEOM_Map c1 = rf_move(4.0, 22.0);
  GSHGEOM_Map c2 = rf_move(14.5, 24.25);
  rf_stroke(c, l, star, rf_pen(c, 5.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), &c1, rf_solid(0.9, 0.9, 0.2, 0.9));
  rf_stroke(c, l, star, rf_pen(c, 5.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), &c2, rf_solid(0.2, 0.9, 0.9, 0.9));
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  /* one path stroked at whole-pixel translations (one expansion) and at a fraction (another) */
  static const double ar[8] = {0.0, 0.0, 6.0, 0.0, 6.0, 6.0, 0.0, 6.0};
  const GSHSCENE_Resource * box = rf_polyline(c, ar, 4, 1);
  const GSHSCENE_Resource * thin = rf_pen(c, 1.5, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map m = rf_move(32.0 + 5.0 * (double)i, 22.25 + 4.0 * (double)i);
    rf_stroke(c, l, box, thin, &m, rf_solid(0.1, 0.1, 0.1, 1.0));
  }
  GSHGEOM_Map f = rf_move(36.5, 34.5);
  rf_stroke(c, l, box, thin, &f, rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_sparse(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  /* far longer than the raster, so that a rectangle of its own is large against the few deposits of its edges */
  static const double flat[4] = {-120.0, 7.5, 200.0, 7.5};
  static const double slant[4] = {-90.0, -80.0, 160.0, 130.0};
  static const double shallow[4] = {-100.0, 30.0, 160.0, 31.0};
  const GSHSCENE_Resource * f = rf_polyline(c, flat, 2, 0);
  const GSHSCENE_Resource * s = rf_polyline(c, slant, 2, 0);
  const GSHSCENE_Resource * h = rf_polyline(c, shallow, 2, 0);
  const GSHSCENE_Resource * hair = rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_ROUND, 4.0,
      GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
      GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO);
  const GSHSCENE_Resource * plain = rf_pen(c, 2.25, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_SQUARE);
  GSHGEOM_Map id = rf_move(0.0, 0.0);
  GSHGEOM_Map frac = rf_move(0.375, 0.625);
  rf_stroke(c, l, f, hair, &id, rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_stroke(c, l, f, plain, &frac, rf_solid(0.8, 0.1, 0.1, 0.8));
  rf_stroke(c, l, s, hair, &id, rf_solid(0.1, 0.5, 0.1, 1.0));
  rf_stroke(c, l, s, plain, &frac, rf_solid(0.1, 0.1, 0.8, 0.8));
  rf_stroke(c, l, h, hair, &frac, rf_solid(0.5, 0.1, 0.5, 1.0));
  /* the same strokes again: the second expansion of each comes from the cache */
  rf_stroke(c, l, f, plain, &frac, rf_solid(1.0, 1.0, 1.0, 0.4));
  rf_close(c, l);
  return l;
}

/* --- stroked text (strokes story S5) ------------------------------------------------------------------------ */

/** A run of the test glyphs @p ids at the positions @p xy (x, y pairs) from @p tp's face (fast tier bound). */
static inline const GSHSCENE_Resource * rf_text_run(RF_Ctx * c, GSHSCENE_Test_Provider * tp, double size, uint32_t mode,
    const uint32_t * ids, const double * xy, size_t n) {
  const GSHSCENE_Resource * face = rf_hold(c, gshscene_tp_face(c->store, tp, 1, tp == &rf_provider_plain ? 0x62u : 0x61u, 0));
  rf_check(c, face != NULL);
  GSHSCENE_Glyph glyphs[8];
  memset(glyphs, 0, sizeof glyphs);
  rf_check(c, n <= 8);
  for (size_t i = 0; i < n; i++) {
    glyphs[i].id = ids[i];
    glyphs[i].x = xy[2 * i];
    glyphs[i].y = xy[2 * i + 1];
  }
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = size;
  run.hint_mode = mode;
  run.glyph_count = n;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_glyph_run(c->store, &run, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

static inline GSHSCENE_List * rf_strokes_text(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.97, 0.95, 0.88, 1.0), 0, 0.0);
  /* a fill hinted at 10 ppem on a grid-aligned map, and over it the stroke of the same run, which is not hinted (ST3); and
   * the reverse order at 27 (ST7) */
  static const uint32_t abc[3] = {1, 2, 3};
  static const double abc_xy[6] = {1.5, 11.0, 9.25, 11.0, 18.5, 11.0};
  const GSHSCENE_Resource * pair = rf_text_run(c, &rf_provider_program, 10.0, GSHSCENE_HINT_FONT, abc, abc_xy, 3);
  const GSHSCENE_Resource * pen = rf_pen(c, 1.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT);
  rf_fill(c, l, pair, NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.7, 0.2, 1.0), 0, 0.0);
  rf_stroke(c, l, pair, pen, NULL, rf_solid(0.4, 0.05, 0.05, 1.0));
  GSHGEOM_Map right = rf_move(27.0, 0.0);
  rf_stroke(c, l, pair, rf_pen(c, 1.5, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &right, rf_solid(0.05, 0.1, 0.5, 1.0));
  rf_fill(c, l, pair, &right, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.7, 0.2, 0.8), 0, 0.0);
  /* glyphs that overlap, a translucent stroke: one coverage where they cross (ST2) */
  static const uint32_t rings[3] = {2, 2, 2};
  static const double rings_xy[6] = {2.0, 27.0, 5.0, 27.0, 8.0, 27.0};
  rf_stroke(c, l, rf_text_run(c, &rf_provider_program, 14.0, GSHSCENE_HINT_NONE, rings, rings_xy, 3),
      rf_pen(c, 1.5, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), NULL, rf_solid(0.1, 0.2, 0.8, 0.45));
  /* one gradient across the run, in the run's model space (ST6) */
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
  static const uint32_t gl[3] = {1, 3, 2};
  static const double gl_xy[6] = {26.0, 27.0, 31.5, 27.0, 36.0, 27.0};
  rf_stroke(c, l, rf_text_run(c, &rf_provider_program, 12.0, GSHSCENE_HINT_FONT_STRICT, gl, gl_xy, 3),
      rf_pen(c, 1.75, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), NULL,
      rf_gradient_paint(rf_gradient(c, rf_linear(26, 0, 46, 0, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0));
  /* inside alignment of a frame (its counter is a hole), dashes with round caps, an elliptical pen, a hairline */
  static const uint32_t frame[1] = {1};
  static const double frame_xy[2] = {1.0, 38.0};
  rf_stroke(c, l, rf_text_run(c, &rf_provider_program, 16.0, GSHSCENE_HINT_NONE, frame, frame_xy, 1),
      rf_style(c, 3.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
          GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_INSIDE,
          GSHGEOM_FILL_NONZERO),
      NULL, rf_solid(0.1, 0.5, 0.2, 0.9));
  static const double dashes[2] = {3.0, 1.5};
  static const uint32_t pair2[2] = {1, 3};
  static const double pair2_xy[4] = {13.0, 38.0, 19.0, 38.0};
  rf_stroke(c, l, rf_text_run(c, &rf_provider_program, 12.0, GSHSCENE_HINT_NONE, pair2, pair2_xy, 2),
      rf_style(c, 1.25, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_ROUND,
          GSHSCENE_STROKE_DEGENERATE_DRAW, dashes, 2, 0.5, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER,
          GSHGEOM_FILL_NONZERO),
      NULL, rf_solid(0.5, 0.1, 0.5, 1.0));
  GSHGEOM_Map wide;
  gshgeom_map_scale(2, 1.6, 0.8, 1.0, &wide);
  static const uint32_t two_glyphs[2] = {1, 2};
  static const double two_xy[4] = {0.0, 0.0, 6.0, 0.0};
  const GSHSCENE_Resource * small = rf_text_run(c, &rf_provider_plain, 10.0, GSHSCENE_HINT_NONE, two_glyphs, two_xy, 2);
  GSHGEOM_Map place = rf_move(29.0, 38.0);
  gshgeom_map_compose(&place, &wide, &place);
  rf_stroke(c, l, small, rf_pen(c, 1.5, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT), &place, rf_solid(0.05, 0.3, 0.35, 1.0));
  GSHGEOM_Map hair = rf_move(30.0, 24.0);
  rf_stroke(c, l, small, rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_ROUND, 4.0,
                            GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0,
                            GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &hair, rf_solid(0.0, 0.0, 0.0, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_text_outlines(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.92, 0.95, 0.97, 1.0), 0, 0.0);
  /* glyphs that are open polylines, a closed star and a blob; a glyph with an empty outline beside them */
  static const double zig[10] = {0.0, 8.0, 3.0, 0.0, 6.0, 8.0, 9.0, 0.0, 12.0, 8.0};
  static const double bar[4] = {0.0, 0.0, 0.0, 8.0};
  const GSHSCENE_Resource * z = rf_polyline(c, zig, 5, 0);
  const GSHSCENE_Resource * b = rf_polyline(c, bar, 2, 0);
  const GSHSCENE_Resource * star = rf_star(c, 5.0, 5.0, 5.0);
  const GSHSCENE_Resource * blob = rf_blob(c, 0.5, 0.5, 6.0);
  GSHSCENE_Glyph glyphs[5];
  memset(glyphs, 0, sizeof glyphs);
  glyphs[0].id = 1;
  glyphs[0].x = 2.0;
  glyphs[0].y = 4.0;
  glyphs[0].outline = z;
  glyphs[1].id = 2;
  glyphs[1].x = 17.0;
  glyphs[1].y = 4.0;
  glyphs[1].outline = b;
  glyphs[2].id = 3;
  glyphs[2].x = 21.0;
  glyphs[2].y = 3.0;
  glyphs[2].outline = star;
  glyphs[3].id = 4;
  glyphs[3].x = 31.0;
  glyphs[3].y = 3.5;
  glyphs[3].outline = rf_intern(c, rf_path(c)); /* a glyph with no outline: an empty path */
  glyphs[4].id = 5;
  glyphs[4].x = 36.0;
  glyphs[4].y = 4.0;
  glyphs[4].outline = blob;
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 5;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * text = NULL;
  rf_check(c, gshscene_store_intern_glyph_run(c->store, &run, &text) == GSHSCENE_OK);
  rf_hold(c, text);
  static const double dashes[2] = {3.0, 2.0};
  rf_stroke(c, l, text,
      rf_style(c, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_BEVEL, 4.0, GSHSCENE_STROKE_CAP_SQUARE,
          GSHSCENE_STROKE_DEGENERATE_DRAW, dashes, 2, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER,
          GSHGEOM_FILL_NONZERO),
      NULL, rf_solid(0.7, 0.1, 0.1, 0.85));
  GSHGEOM_Map down = rf_move(0.0, 12.0);
  rf_stroke(c, l, text,
      rf_style(c, 1.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_ROUND,
          GSHSCENE_STROKE_DEGENERATE_DRAW, dashes, 2, 1.0, GSHSCENE_STROKE_DASH_PATH, GSHSCENE_STROKE_ALIGN_CENTER,
          GSHGEOM_FILL_NONZERO),
      &down, rf_solid(0.1, 0.4, 0.7, 1.0));
  GSHGEOM_Map turn;
  GSHGEOM_Map spin;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 12.0, &turn);
  spin = rf_move(2.0, 21.0);
  gshgeom_map_compose(&spin, &turn, &spin);
  rf_stroke(c, l, text, rf_pen(c, 4.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &spin, rf_solid(0.2, 0.6, 0.3, 0.5));
  /* a run of one glyph, and the closed glyphs of the first run stroked inside */
  GSHSCENE_Glyph one[1];
  memset(one, 0, sizeof one);
  one[0].id = 9;
  one[0].x = 38.0;
  one[0].y = 26.0;
  one[0].outline = star;
  run.glyph_count = 1;
  run.glyphs = one;
  const GSHSCENE_Resource * lone = NULL;
  rf_check(c, gshscene_store_intern_glyph_run(c->store, &run, &lone) == GSHSCENE_OK);
  rf_hold(c, lone);
  rf_stroke(c, l, lone,
      rf_style(c, 1.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
          GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_OUTSIDE,
          GSHGEOM_FILL_NONZERO),
      NULL, rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_nothing(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Resource * box = rf_rect(c, 0.0, 0.0, 8.0, 8.0);
  GSHGEOM_Map m = rf_move(4.0, 4.0);
  rf_fill(c, l, rf_rect(c, 1, 1, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.5, 0.7, 0.9, 1.0), 0, 0.0);
  /* width 0 */
  rf_stroke(c, l, box, rf_pen(c, 0.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &m, rf_solid(0, 0, 0, 1));
  /* a flat map */
  double flat[4] = {0.0, 0.0, 0.0, 1.0};
  double zero[2] = {10.0, 10.0};
  GSHGEOM_Map fm;
  rf_check(c, gshgeom_map_from_matrix(2, flat, zero, &fm) == GSHGEOM_OK);
  rf_stroke(c, l, box, rf_pen(c, 2.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &fm, rf_solid(0, 0, 0, 1));
  /* each factor invertible, the product a singular map (SK9 as amended): a picture instanced under a tiny map, holding
   * a stroke under a tiny map of its own */
  double tinym[4] = {0x1p-600, 0.0, 0.0, 0x1p-600};
  double nothing[2] = {0.0, 0.0};
  GSHGEOM_Map tiny;
  rf_check(c, gshgeom_map_from_matrix(2, tinym, nothing, &tiny) == GSHGEOM_OK);
  GSHSCENE_List * inner = rf_open(c);
  rf_stroke(c, inner, box, rf_pen(c, 2.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &tiny, rf_solid(0, 0, 0, 1));
  rf_close(c, inner);
  const GSHSCENE_Resource * picture = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, inner, &picture) == GSHSCENE_OK);
  rf_hold(c, picture);
  rf_check(c, gshscene_list_instance(l, picture, &tiny, 0, 0.0) == GSHSCENE_OK);
  /* a degenerate gradient (p0 = p1) */
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(1, 0, 0, 1)), rf_stop(1.0, rf_solid(0, 0, 1, 1))};
  GSHGEOM_Map gm = rf_move(20.0, 20.0);
  rf_stroke(c, l, box, rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &gm,
      rf_gradient_paint(rf_gradient(c, rf_linear(3, 3, 3, 3, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0));
  rf_close(c, l);
  return l;
}

/** A map from a 2 by 2 matrix (row major) and a translation. */
static inline GSHGEOM_Map rf_affine(RF_Ctx * c, double a, double b, double cc, double d, double tx, double ty) {
  double mm[4] = {a, b, cc, d};
  double t[2] = {tx, ty};
  GSHGEOM_Map m;
  rf_check(c, gshgeom_map_from_matrix(2, mm, t, &m) == GSHGEOM_OK);
  return m;
}

/** A closed circle of radius @p r about the origin, as four cubics. */
static inline const GSHSCENE_Resource * rf_circle(RF_Ctx * c, double r) {
  const double k = 0.5522847498307936 * r;
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(r, 0.0));
  gshgeom_path_cubic_to(p, rf_pt(r, k), rf_pt(k, r), rf_pt(0.0, r));
  gshgeom_path_cubic_to(p, rf_pt(-k, r), rf_pt(-r, k), rf_pt(-r, 0.0));
  gshgeom_path_cubic_to(p, rf_pt(-r, -k), rf_pt(-k, -r), rf_pt(0.0, -r));
  gshgeom_path_cubic_to(p, rf_pt(k, -r), rf_pt(r, -k), rf_pt(r, 0.0));
  gshgeom_path_close(p);
  return rf_intern(c, p);
}

/** A lens (two cubics meeting at two points) 12 wide and 12 tall about the origin. */
static inline const GSHSCENE_Resource * rf_lens(RF_Ctx * c) {
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(0.0, -6.0));
  gshgeom_path_cubic_to(p, rf_pt(8.0, -6.0), rf_pt(8.0, 6.0), rf_pt(0.0, 6.0));
  gshgeom_path_cubic_to(p, rf_pt(-8.0, 6.0), rf_pt(-8.0, -6.0), rf_pt(0.0, -6.0));
  gshgeom_path_close(p);
  return rf_intern(c, p);
}

static inline GSHSCENE_List * rf_strokes_dash_overlap(RF_Ctx * c) {
  /* (strokes story S4b) pieces that overlap inside a pixel: the caps of dashes whose gaps are shorter than their reach, a
   * hairline of such dashes, and dashes along a curve, each translucent so that a credit of the overlap twice shows */
  GSHSCENE_List * l = rf_open(c);
  static const double line[4] = {0.0, 0.0, 21.0, 0.0};
  static const double diag[6] = {0.0, 0.0, 9.0, 7.0, 18.0, 0.5};
  static const double short_gap[2] = {3.0, 1.0};
  static const double tiny_gap[4] = {2.0, 0.5, 1.0, 0.25};
  static const uint32_t caps[3] = {GSHSCENE_STROKE_CAP_SQUARE, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_CAP_ROUND};
  const GSHSCENE_Resource * ln = rf_polyline(c, line, 2, 0);
  const GSHSCENE_Resource * dg = rf_polyline(c, diag, 3, 0);
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map a = rf_move(2.0, 3.0 + 4.5 * i);
    rf_stroke(c, l, ln, rf_style(c, 2.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, caps[i],
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, short_gap, 2, 0.5, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                    GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
        &a, rf_solid(0.9, 0.3, 0.1, 0.5));
    GSHGEOM_Map b = rf_move(26.0 + 0.25 * i, 2.5 + 8.0 * i);
    rf_stroke(c, l, dg, rf_style(c, 3.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, caps[i],
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, tiny_gap, 4, 0.25, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                    GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
        &b, rf_solid(0.1, 0.4, 0.8, 0.5));
  }
  /* a hairline of square-cap dashes, flat and slanted */
  GSHGEOM_Map h1 = rf_move(2.0, 18.5);
  GSHGEOM_Map h2 = rf_move(2.0, 21.0);
  rf_stroke(c, l, ln, rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_SQUARE,
                                  GSHSCENE_STROKE_DEGENERATE_DRAW, short_gap, 2, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                  GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &h1, rf_solid(0.1, 0.1, 0.1, 0.6));
  rf_stroke(c, l, dg, rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_SQUARE,
                                  GSHSCENE_STROKE_DEGENERATE_DRAW, tiny_gap, 4, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                  GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &h2, rf_solid(0.1, 0.1, 0.1, 0.6));
  /* square-cap dashes round a curve: each cap reaches into its neighbour's */
  GSHGEOM_Map r = rf_move(36.0, 31.0);
  rf_stroke(c, l, rf_circle(c, 7.0), rf_style(c, 3.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0,
                                              GSHSCENE_STROKE_CAP_SQUARE, GSHSCENE_STROKE_DEGENERATE_DRAW, short_gap, 2, 0.0,
                                              GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER,
                                              GSHGEOM_FILL_NONZERO),
      &r, rf_solid(0.5, 0.2, 0.7, 0.5));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_tight_joins(RF_Ctx * c) {
  /* (strokes story S4b) the inner side of joins whose segments are shorter than the stroke is wide, with each of the five
   * joins, under a local width, a device width and a non-uniform map */
  GSHSCENE_List * l = rf_open(c);
  static const double saw[14] = {0.0, 6.0, 3.0, 0.0, 5.0, 6.0, 8.0, 0.0, 10.0, 6.0, 13.0, 0.0, 15.0, 6.0};
  static const uint32_t joins[5] = {GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_JOIN_BEVEL,
      GSHSCENE_STROKE_JOIN_MITER_CLIP, GSHSCENE_STROKE_JOIN_ARCS};
  const GSHSCENE_Resource * zig = rf_polyline(c, saw, 7, 0);
  for (int i = 0; i < 5; i++) {
    GSHGEOM_Map m = rf_move(1.5 + 16.0 * (double)(i % 3) + 0.125 * i, 2.0 + 13.0 * (double)(i / 3) + 0.25 * i);
    rf_stroke(c, l, zig, rf_style(c, 3.5, GSHSCENE_STROKE_WIDTH_LOCAL, joins[i], 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                     GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                     GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
        &m, rf_solid(0.8 - 0.1 * i, 0.2 + 0.1 * i, 0.4, 0.55));
  }
  GSHGEOM_Map dev = rf_move(18.0, 28.5);
  rf_stroke(c, l, zig, rf_style(c, 3.5, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                   GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                   GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      &dev, rf_solid(0.1, 0.5, 0.2, 0.55));
  GSHGEOM_Map skew = rf_affine(c, 1.1, 0.0, 0.35, 0.55, 2.0, 30.0);
  rf_stroke(c, l, zig, rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_SQUARE), &skew, rf_solid(0.2, 0.2, 0.9, 0.55));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_rings(RF_Ctx * c) {
  /* (strokes story S4b) the inner side of a curve, where the triangles of the flattened outline lie over one another: a
   * circle and a lens at widths from one pixel to wider than their radius, the lens turned (0.9, 0.4) */
  GSHSCENE_List * l = rf_open(c);
  static const double widths[4] = {1.0, 2.5, 5.0, 9.0};
  const GSHSCENE_Resource * circle = rf_circle(c, 6.0);
  const GSHSCENE_Resource * lens = rf_lens(c);
  for (int i = 0; i < 4; i++) {
    GSHGEOM_Map m = rf_move(8.0 + 11.5 * (double)i + 0.25 * i, 9.0 + 0.125 * i);
    rf_stroke(c, l, circle, rf_pen(c, widths[i], GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT), &m,
        rf_solid(0.9, 0.4 - 0.1 * i, 0.1, 0.5));
    /* the rotation whose cosine and sine are in the ratio 0.9 : 0.4, as literals (no libm in a fixture) */
    GSHGEOM_Map t = rf_affine(c, 0.9138115486202572, -0.4061384660534476, 0.4061384660534476, 0.9138115486202572, 8.0 + 11.5 * (double)i, 27.0 + 0.25 * i);
    rf_stroke(c, l, lens, rf_pen(c, widths[i], i % 2 ? GSHSCENE_STROKE_JOIN_MITER : GSHSCENE_STROKE_JOIN_ROUND,
                                    GSHSCENE_STROKE_CAP_BUTT),
        &t, rf_solid(0.1, 0.3 + 0.1 * i, 0.9, 0.5));
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_strokes_thin_aligned(RF_Ctx * c) {
  /* (strokes story S4b, UC8) aligned strokes whose bands are thinner than two pixels, so that the aligned part and the fill's
   * edge lie in every pixel along the path: a square and a circle inside and outside, thin rectangles at fractional offsets
   * (an off-centre strip), a star under even-odd, and a closed subpath beside an open one whose body abuts it */
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Resource * sq = rf_rect(c, 0.0, 0.0, 10.0, 10.0);
  const GSHSCENE_Resource * circle = rf_circle(c, 5.5);
  const GSHSCENE_Resource * star = rf_star(c, 6.0, 6.0, 6.0);
  const GSHSCENE_Resource * strip = rf_rect(c, 0.0, 0.0, 0.6, 12.0);
  for (int i = 0; i < 2; i++) {
    uint32_t align = i ? GSHSCENE_STROKE_ALIGN_OUTSIDE : GSHSCENE_STROKE_ALIGN_INSIDE;
    GSHGEOM_Map m = rf_move(2.25 + 13.0 * i, 2.375);
    rf_stroke(c, l, sq, rf_style(c, 0.9, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, align,
                                    GSHGEOM_FILL_NONZERO),
        &m, rf_solid(0.2, 0.3 + 0.4 * i, 0.8, 0.7));
    GSHGEOM_Map mc = rf_move(33.5 + 7.0 * i, 8.25);
    rf_stroke(c, l, circle, rf_style(c, 0.7, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                        GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, align,
                                        GSHGEOM_FILL_NONZERO),
        &mc, rf_solid(0.8, 0.2 + 0.3 * i, 0.2, 0.7));
  }
  /* thin rectangles: the band lies on the edge of the fill, at an offset that is not a pixel's */
  for (int i = 0; i < 4; i++) {
    GSHGEOM_Map m = rf_move(3.3 + 3.75 * i, 17.0 + 0.125 * i);
    rf_stroke(c, l, strip, rf_style(c, 0.2 + 0.1 * i, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0,
                                       GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0,
                                       GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                       i % 2 ? GSHSCENE_STROKE_ALIGN_OUTSIDE : GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
        &m, rf_solid(0.1, 0.1, 0.1, 0.8));
  }
  for (int i = 0; i < 2; i++) {
    GSHGEOM_Map m = rf_move(19.5 + 14.0 * i, 22.75);
    rf_stroke(c, l, star, rf_style(c, 0.8, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                      GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                      i ? GSHSCENE_STROKE_ALIGN_OUTSIDE : GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_EVEN_ODD),
        &m, rf_solid(0.5, 0.2, 0.6, 0.7));
  }
  /* a closed subpath beside an open one: the open one's body abuts the closed one's aligned part */
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_move_to(p, rf_pt(0.0, 0.0));
  gshgeom_path_line_to(p, rf_pt(8.0, 0.0));
  gshgeom_path_line_to(p, rf_pt(8.0, 6.0));
  gshgeom_path_line_to(p, rf_pt(0.0, 6.0));
  gshgeom_path_close(p);
  gshgeom_path_move_to(p, rf_pt(8.0, 3.0));
  gshgeom_path_line_to(p, rf_pt(15.0, 3.0));
  const GSHSCENE_Resource * both = rf_intern(c, p);
  GSHGEOM_Map mb = rf_move(2.375, 31.25);
  rf_stroke(c, l, both, rf_style(c, 1.1, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                                    GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                                    GSHSCENE_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO),
      &mb, rf_solid(0.1, 0.5, 0.3, 0.8));
  rf_close(c, l);
  return l;
}

/* --- images (masks-images-patterns story I3) ---------------------------------------------------------- */

/**
 * An image of @p w by @p h generated in code: @p channels samples a texel (1 gray, 3 RGB, 4 RGBA) of @p bits (8, 12, 16, or 32 for
 * a float), alpha straight, premultiplied or none, alpha-only or not, in @p space.  The samples are a structured pattern (a
 * coarse checker under a ramp) so that a filter's work shows.
 */
static inline const GSHSCENE_Resource * rf_image(RF_Ctx * c, uint32_t w, uint32_t h, unsigned channels, unsigned bits,
    GIMG_Alpha_Form alpha, int alpha_only, GSHSCENE_Color_Space space) {
  GIMG_Pixel_Format format;
  memset(&format, 0, sizeof format);
  format.channel_model = channels == 1 ? GIMG_CHANNEL_GRAY : channels == 3 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
  format.channel_type = bits == 32 ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  format.layout = GIMG_LAYOUT_INTERLEAVED;
  format.channel_count = (uint8_t)channels;
  for (unsigned i = 0; i < channels; i++) {
    format.bits_per_channel[i] = (uint8_t)bits;
  }
  format.alignment = 1;
  format.alpha_form = (uint8_t)alpha;
  GIMG_Raster * raster = NULL;
  rf_check(c, gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) == GIMG_OK);
  if (!raster) {
    return NULL;
  }
  unsigned char * base = (unsigned char *)gimg_raster_pixels(raster);
  const size_t stride = gimg_raster_stride_bytes(raster);
  const size_t size = bits <= 8 ? 1 : bits <= 16 ? 2 : 4;
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      for (unsigned k = 0; k < channels; k++) {
        const int checker = ((x / 2u) + (y / 2u)) % 2u;
        const double ramp = (double)((x * 3u + y * 5u + k * 7u) % 16u) / 15.0;
        double v = checker ? 0.25 + 0.7 * ramp : 0.9 - 0.6 * ramp;
        if (k == 3 || alpha_only) {
          v = 0.3 + 0.7 * (double)((x * 5u + y * 3u) % 8u) / 7.0; /* alpha: never zero, so premultiplied stays a valid pair */
        } else if (alpha == GIMG_ALPHA_PREMULTIPLIED && channels == 4) {
          v *= 0.3 + 0.7 * (double)((x * 5u + y * 3u) % 8u) / 7.0;
        }
        unsigned char * at = base + (size_t)y * stride + ((size_t)x * channels + k) * size;
        if (bits == 32) {
          float f = (float)v;
          memcpy(at, &f, sizeof f);
        } else {
          const uint32_t top = (1u << bits) - 1u;
          uint32_t q = (uint32_t)(v * (double)top + 0.5);
          if (bits <= 8) {
            *at = (unsigned char)q;
          } else {
            uint16_t u = (uint16_t)q;
            memcpy(at, &u, sizeof u);
          }
        }
      }
    }
  }
  GSHSCENE_Image_Params params;
  memset(&params, 0, sizeof params);
  params.space = space;
  params.flags = alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0u;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_image(c->store, raster, &params, &r) == GSHSCENE_OK);
  gimg_raster_destroy(raster);
  return rf_hold(c, r);
}

/**
 * An image paint over the window @p window (`x0, y0, x1, y1` in texels; NULL: the whole image) with its map, extends, filters, filter
 * space and tint, and the paint of it with @p alpha.
 */
static inline GSHSCENE_Paint rf_image_paint_window(RF_Ctx * c, const GSHSCENE_Resource * image, const uint32_t * window,
    GSHGEOM_Map map, uint32_t ex, uint32_t ey, uint32_t magnify, uint32_t minify, GSHSCENE_Color_Space filter_space,
    const GSHSCENE_Paint * tint, double alpha) {
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  if (window) {
    memcpy(ip.window, window, sizeof ip.window);
  }
  ip.map = map;
  ip.extend_x = ex;
  ip.extend_y = ey;
  ip.magnify = magnify;
  ip.minify = minify;
  ip.filter_space = filter_space;
  ip.tint = tint;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_image_paint(c->store, &ip, &r) == GSHSCENE_OK);
  rf_hold(c, r);
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_IMAGE;
  p.resource = r;
  p.rgba[3] = alpha;
  return p;
}

/** An image paint over the whole image (the image's own filter space unless @p filter_space says one). */
static inline GSHSCENE_Paint rf_image_paint(RF_Ctx * c, const GSHSCENE_Resource * image, GSHGEOM_Map map, uint32_t ex, uint32_t ey,
    uint32_t magnify, uint32_t minify, GSHSCENE_Color_Space filter_space, const GSHSCENE_Paint * tint, double alpha) {
  return rf_image_paint_window(c, image, NULL, map, ex, ey, magnify, minify, filter_space, tint, alpha);
}

/** The map taking the unit of image space to a box of @p sx by @p sy at (@p tx, @p ty). */
static inline GSHGEOM_Map rf_place(RF_Ctx * c, double sx, double sy, double tx, double ty) {
  return rf_affine(c, sx, 0.0, 0.0, sy, tx, ty);
}

static inline GSHSCENE_List * rf_image_filters(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  const GSHSCENE_Resource * img = rf_image(c, 8, 6, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.85, 0.85, 0.9, 1.0), 0, 0.0);
  /* the three point filters magnified: each texel a block, a blend, a smoothed blend */
  rf_fill(c, l, rf_rect(c, 1, 1, 17, 13), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 2, 2, 1, 1), 0, 0, GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_NEAREST, none, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 17, 1, 33, 13), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 2, 2, 17, 1), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_NEAREST, none, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 33, 1, 47, 13), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1.75, 2, 33, 1), 0, 0, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_NEAREST, none, NULL, 1.0), 0, 0.0);
  /* minified by area, at a fraction: the exact mean of the covered texels; and at a half pixel offset */
  rf_fill(c, l, rf_rect(c, 1, 14, 9, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1.0, 1.0, 1.5, 14), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 10, 14, 14, 18), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 0.4, 0.3, 10, 14), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  /* a unit scale at an integer translation: a copy; at a half pixel: a mean of two texels */
  rf_fill(c, l, rf_rect(c, 16, 14, 24, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1.0, 1.0, 16, 14), 0, 0, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_NEAREST, none, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 26, 14, 34, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1.0, 1.0, 26.5, 14.5), 0, 0, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_LINEAR, none, NULL, 1.0), 0, 0.0);
  /* stretched one way and shrunk the other: it minifies (IP8), blocky along the stretched axis */
  rf_fill(c, l, rf_rect(c, 36, 14, 47, 20), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1.4, 0.8, 36, 14), 0, 0, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_NEAREST, none, NULL, 1.0), 0, 0.0);
  /* turned 30 degrees and quarter turned: the general tier, and the separable one */
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &turn);
  GSHGEOM_Map there = rf_move(10.0, 22.0);
  GSHGEOM_Map scale = rf_place(c, 2.2, 2.2, 0, 0);
  GSHGEOM_Map chain[2] = {turn, scale};
  GSHGEOM_Map composed;
  gshgeom_map_compose_chain(chain, 2, NULL, &composed);
  rf_fill(c, l, rf_rect(c, -4, -4, 22, 20), &there, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, composed, 3, 3, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 0.9), 0, 0.0);
  GSHGEOM_Map quarter;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &quarter);
  GSHGEOM_Map zoom[2] = {quarter, rf_place(c, 2.5, 2.5, 0, 0)};
  GSHGEOM_Map quarter_scaled;
  gshgeom_map_compose_chain(zoom, 2, NULL, &quarter_scaled);
  GSHGEOM_Map place_q = rf_move(40.0, 22.0);
  rf_fill(c, l, rf_rect(c, -8, -2, 8, 16), &place_q, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, quarter_scaled, 2, 1, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_image_edges(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  const GSHSCENE_Resource * img = rf_image(c, 4, 3, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.8, 1.0), 0, 0.0);
  /* every mode on x against every mode on y: the image placed in the middle of its cell, drawn past its edges */
  for (uint32_t ey = 0; ey < 4; ey++) {
    for (uint32_t ex = 0; ex < 4; ex++) {
      const double x = 1.0 + 11.75 * (double)ex;
      const double y = 1.0 + 9.75 * (double)ey;
      GSHSCENE_Paint p = rf_image_paint(c, img, rf_place(c, 2.0, 2.0, x + 3.0, y + 2.0), ex, ey,
          ex % 2 ? GSHSCENE_IMAGE_FILTER_LINEAR : GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0);
      rf_fill(c, l, rf_rect(c, x, y, x + 10.5, y + 8.5), NULL, GSHGEOM_FILL_NONZERO, p, 0, 0.0);
    }
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_image_formats(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  GSHSCENE_Color fg;
  memset(&fg, 0, sizeof fg);
  fg.rgba[0] = 0.1;
  fg.rgba[1] = 0.3;
  fg.rgba[2] = 0.8;
  fg.rgba[3] = 1.0;
  rf_check(c, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
  GSHSCENE_Color colors[1];
  memset(colors, 0, sizeof colors);
  colors[0].rgba[0] = 0.8;
  colors[0].rgba[1] = 0.2;
  colors[0].rgba[2] = 0.3;
  colors[0].rgba[3] = 1.0;
  const GSHSCENE_Resource * palette = NULL;
  rf_check(c, gshscene_store_intern_palette(c->store, 1, colors, &palette) == GSHSCENE_OK);
  rf_hold(c, palette);
  rf_check(c, gshscene_list_set_palette(l, palette) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.85, 1.0), 0, 0.0);
  struct {
    unsigned channels, bits;
    GIMG_Alpha_Form alpha;
  } formats[] = {
      {1, 8, GIMG_ALPHA_NONE}, {3, 8, GIMG_ALPHA_NONE}, {4, 8, GIMG_ALPHA_STRAIGHT}, {4, 8, GIMG_ALPHA_PREMULTIPLIED},
      {1, 12, GIMG_ALPHA_NONE}, {4, 12, GIMG_ALPHA_STRAIGHT}, {3, 16, GIMG_ALPHA_NONE}, {4, 16, GIMG_ALPHA_PREMULTIPLIED},
      {1, 32, GIMG_ALPHA_NONE}, {3, 32, GIMG_ALPHA_NONE}, {4, 32, GIMG_ALPHA_STRAIGHT}, {4, 32, GIMG_ALPHA_PREMULTIPLIED},
  };
  for (size_t i = 0; i < 12; i++) {
    const double x = 1.0 + 11.75 * (double)(i % 4);
    const double y = 1.0 + 9.0 * (double)(i / 4);
    const GSHSCENE_Resource * img = rf_image(c, 5, 4, formats[i].channels, formats[i].bits, formats[i].alpha, 0, none);
    rf_fill(c, l, rf_rect(c, x, y, x + 10.0, y + 8.0), NULL, GSHGEOM_FILL_NONZERO,
        rf_image_paint(c, img, rf_place(c, 2.0, 2.0, x, y), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0),
        0, 0.0);
  }
  /* the alpha-only image with a solid, a foreground and a palette tint (the mark of a stencil) */
  const GSHSCENE_Resource * mask = rf_image(c, 5, 4, 1, 8, GIMG_ALPHA_NONE, 1, none);
  GSHSCENE_Paint tint_solid = rf_solid_in(0.9, 0.4, 0.1, 0.9, rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0));
  GSHSCENE_Paint tint_fg;
  memset(&tint_fg, 0, sizeof tint_fg);
  tint_fg.kind = GSHSCENE_PAINT_FOREGROUND;
  tint_fg.rgba[3] = 0.8;
  GSHSCENE_Paint tint_palette;
  memset(&tint_palette, 0, sizeof tint_palette);
  tint_palette.kind = GSHSCENE_PAINT_PALETTE;
  tint_palette.entry = 0;
  tint_palette.rgba[3] = 1.0;
  const GSHSCENE_Paint * tints[3] = {&tint_solid, &tint_fg, &tint_palette};
  for (size_t i = 0; i < 3; i++) {
    const double x = 1.0 + 11.75 * (double)i;
    rf_fill(c, l, rf_rect(c, x, 29.0, x + 10.0, 37.0), NULL, GSHGEOM_FILL_NONZERO,
        rf_image_paint(c, mask, rf_place(c, 2.0, 2.0, x, 29.0), 1, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none,
            tints[i], 0.9),
        0, 0.0);
  }
  /* a 12-bit alpha-only image, a float one */
  const GSHSCENE_Resource * mask12 = rf_image(c, 5, 4, 1, 12, GIMG_ALPHA_NONE, 1, none);
  rf_fill(c, l, rf_rect(c, 36.0, 29.0, 46.0, 37.0), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, mask12, rf_place(c, 2.0, 2.0, 36.0, 29.0), 2, 0, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_AREA, none,
          &tint_fg, 1.0),
      0, 0.0);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_image_spaces(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  const GSHSCENE_Color_Space srgb = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0);
  const GSHSCENE_Color_Space lin = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  const GSHSCENE_Color_Space p3 = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  const GSHSCENE_Color_Space adobe = rf_space(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 2.2);
  const GSHSCENE_Resource * plain = rf_image(c, 6, 5, 4, 8, GIMG_ALPHA_STRAIGHT, 0, srgb);
  const GSHSCENE_Resource * wide = rf_image(c, 6, 5, 4, 16, GIMG_ALPHA_STRAIGHT, 0, p3);
  const GSHSCENE_Resource * gamma = rf_image(c, 6, 5, 3, 12, GIMG_ALPHA_NONE, 0, adobe);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.85, 0.9, 1.0), 0, 0.0);
  /* the same image filtered in its own space and in linear light */
  rf_fill(c, l, rf_rect(c, 1, 1, 17, 11), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, plain, rf_place(c, 2.6, 2.0, 1, 1), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 17, 1, 33, 11), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, plain, rf_place(c, 2.6, 2.0, 17, 1), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, lin, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 33, 1, 47, 11), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, plain, rf_place(c, 0.4, 0.4, 33, 1), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, lin, NULL, 1.0), 0, 0.0);
  /* wide-gamut and gamma images on an sRGB target */
  rf_fill(c, l, rf_rect(c, 1, 12, 17, 22), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, wide, rf_place(c, 2.6, 2.0, 1, 12), 0, 0, GSHSCENE_IMAGE_FILTER_CUBIC, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 17, 12, 33, 22), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, gamma, rf_place(c, 2.6, 2.0, 17, 12), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, p3, NULL, 0.8), 0, 0.0);
  /* a group in linear light holds an image of Display P3, filtered in a space of its own */
  GSHSCENE_Group g = rf_group(0.9, GSHSCENE_COMPOSITE_SRC_OVER);
  g.blend_space = lin;
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 1, 23, 17, 38), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, wide, rf_place(c, 2.6, 3.0, 1, 23), 1, 2, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, adobe, NULL, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 17, 23, 33, 38), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, plain, rf_place(c, 2.6, 3.0, 17, 23), 2, 1, GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_image_compose(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  const GSHSCENE_Resource * img = rf_image(c, 6, 5, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  const GSHSCENE_Resource * one = rf_image(c, 1, 1, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.6, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 2, 2, 22, 14), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.7, 1.0), 0, 0.0);
  /* a group of opacity 0.7 in multiply over it holding an image fill, a clip around part of it */
  GSHSCENE_Group g = rf_group(0.7, GSHSCENE_COMPOSITE_MULTIPLY);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_push(l, rf_blob(c, 3.0, 3.0, 8.0), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 2, 2, 24, 16), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 3.5, 3.0, 2, 2), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  /* a star filled with a repeating image, a stroke painted with one (sampled in the stroke's local space) */
  rf_fill(c, l, rf_star(c, 36.0, 10.0, 9.0), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 2.0, 2.0, 28, 2), 1, 2, GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 0.95),
      0, 0.0);
  static const double zig[12] = {0.0, 4.0, 5.0, 0.0, 10.0, 4.0, 15.0, 0.0, 20.0, 4.0, 25.0, 0.0};
  GSHGEOM_Map sm = rf_move(2.0, 20.0);
  rf_stroke(c, l, rf_polyline(c, zig, 6, 0), rf_pen(c, 4.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), &sm,
      rf_image_paint(c, img, rf_place(c, 4.2, 2.0, 0, 0), 1, 1, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0));
  /* a picture holding an image fill, instanced turned */
  GSHSCENE_List * inner = rf_open(c);
  rf_fill(c, inner, rf_rect(c, 0, 0, 9, 8), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1.5, 1.6, 0, 0), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  rf_close(c, inner);
  const GSHSCENE_Resource * pic = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, inner, &pic) == GSHSCENE_OK);
  rf_hold(c, pic);
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 40.0, &turn);
  GSHGEOM_Map into = rf_move(14.0, 28.0);
  GSHGEOM_Map two[2] = {into, turn};
  GSHGEOM_Map at;
  gshgeom_map_compose_chain(two, 2, NULL, &at);
  rf_check(c, gshscene_list_instance(l, pic, &at, 0, 0.0) == GSHSCENE_OK);
  /* a 1 by 1 image repeated: minified by area it is the texel, however many periods */
  rf_fill(c, l, rf_rect(c, 30, 24, 46, 38), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, one, rf_place(c, 0.001, 0.001, 30, 24), 1, 1, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0),
      0, 0.0);
  /* a damaged corner: a window of the image, the rest of it unseen */
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_image_nothing(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  const GSHSCENE_Resource * img = rf_image(c, 4, 4, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.9, 0.7, 1.0), 0, 0.0);
  /* a singular chain draws nothing and is noticed */
  GSHGEOM_Map flat = rf_affine(c, 1, 0, 0, 0, 0, 0);
  rf_fill(c, l, rf_rect(c, 2, 2, 20, 12), &flat, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 1, 1, 0, 0), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0), 0, 0.0);
  /* a window of the image: the texels of its middle, repeated, and not a texel beside it */
  const uint32_t middle[4] = {1, 1, 3, 3};
  rf_fill(c, l, rf_rect(c, 24, 2, 46, 12), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint_window(c, img, middle, rf_place(c, 3.0, 3.0, 24, 2), 1, 1, GSHSCENE_IMAGE_FILTER_NEAREST,
          GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 1.0),
      0, 0.0);
  /* a zero alpha multiplier is a legal paint and draws nothing visible */
  rf_fill(c, l, rf_rect(c, 2, 16, 20, 26), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 4, 4, 2, 16), 0, 0, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 0.0), 0, 0.0);
  /* and one that is drawn */
  rf_fill(c, l, rf_rect(c, 24, 16, 46, 26), NULL, GSHGEOM_FILL_NONZERO,
      rf_image_paint(c, img, rf_place(c, 5.5, 2.5, 24, 16), 3, 3, GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_AREA, none, NULL, 0.6),
      0, 0.0);
  rf_close(c, l);
  return l;
}


/* --- masks (masks-images-patterns story M2) ------------------------------------------------------ */

/** The picture of the closed list @p inner, held. */
static inline const GSHSCENE_Resource * rf_picture_of(RF_Ctx * c, GSHSCENE_List * inner) {
  rf_close(c, inner);
  const GSHSCENE_Resource * picture = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, inner, &picture) == GSHSCENE_OK);
  return rf_hold(c, picture);
}

/** A picture of one fill of @p path in @p paint. */
static inline const GSHSCENE_Resource * rf_picture_fill(RF_Ctx * c, const GSHSCENE_Resource * path, GSHSCENE_Paint paint) {
  GSHSCENE_List * inner = rf_open(c);
  rf_fill(c, inner, path, NULL, GSHGEOM_FILL_NONZERO, paint, 0, 0.0);
  return rf_picture_of(c, inner);
}

/** A mask over @p picture in @p mode, acting on @p acts_on, with the identity transfer; the caller sets the rest and interns. */
static inline GSHSCENE_Mask rf_mask_of(const GSHSCENE_Resource * picture, uint32_t mode, uint32_t acts_on) {
  GSHSCENE_Mask m;
  memset(&m, 0, sizeof m);
  m.picture = picture;
  m.mode = mode;
  m.acts_on = acts_on;
  return m;
}

static inline const GSHSCENE_Resource * rf_mask_intern(RF_Ctx * c, const GSHSCENE_Mask * m) {
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_mask(c->store, m, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

/** A masked group of @p opacity and @p composite holding one fill of @p path in @p paint, the mask placed by @p placement (or NULL). */
static inline void rf_masked(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * mask, double opacity, uint32_t composite,
    const GSHGEOM_Map * placement, const GSHSCENE_Resource * path, GSHSCENE_Paint paint) {
  GSHSCENE_Group g = rf_group(opacity, composite);
  g.soft_mask = mask;
  if (placement) {
    g.soft_mask_map = *placement;
  }
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, path, NULL, GSHGEOM_FILL_NONZERO, paint, 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
}

static inline GSHSCENE_List * rf_mask_alpha(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  /* A star as the mask: its edges are partial coverage, so the mask's value is a coverage and not 0 and 1. */
  const GSHSCENE_Resource * star = rf_picture_fill(c, rf_star(c, 8.0, 9.0, 7.5), rf_solid(1.0, 1.0, 1.0, 1.0));
  GSHSCENE_Mask m = rf_mask_of(star, GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at1 = rf_move(1.0, 1.0);
  rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at1, rf_rect(c, 1, 1, 17, 19), rf_solid(0.9, 0.1, 0.2, 1.0));
  /* A translucent fill and a gradient fading to nothing in the picture. */
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 1, 1.0)), rf_stop(1.0, rf_solid(1, 1, 1, 0.0))};
  const GSHSCENE_Resource * fade = rf_gradient(c, rf_linear(0, 0, 14, 0, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
  GSHSCENE_List * pl = rf_open(c);
  rf_fill(c, pl, rf_rect(c, 0, 0, 14, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 0.5), 0, 0.0);
  rf_fill(c, pl, rf_rect(c, 0, 8, 14, 18), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(fade, 1.0), 0, 0.0);
  m = rf_mask_of(rf_picture_of(c, pl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at2 = rf_move(18.0, 1.0);
  rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at2, rf_rect(c, 18, 1, 32, 19), rf_solid(0.1, 0.3, 0.9, 1.0));
  /* A stroke with round caps, and a fill inside a clip, as the mask. */
  GSHSCENE_List * sl = rf_open(c);
  static const double zig[8] = {1.0, 3.0, 6.0, 12.0, 10.0, 3.0, 14.0, 12.0};
  rf_stroke(c, sl, rf_polyline(c, zig, 4, 0), rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), NULL,
      rf_solid(1, 1, 1, 1.0));
  rf_check(c, gshscene_list_clip_push(sl, rf_blob(c, 2.0, 2.0, 4.0), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, sl, rf_rect(c, 0, 0, 16, 16), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 0.8), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(sl) == GSHSCENE_OK);
  m = rf_mask_of(rf_picture_of(c, sl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at3 = rf_move(33.0, 1.0);
  rf_masked(c, l, rf_mask_intern(c, &m), 0.9, GSHSCENE_COMPOSITE_SRC_OVER, &at3, rf_rect(c, 33, 1, 47, 19), rf_solid(0.2, 0.7, 0.3, 1.0));
  /* A nested group in the picture. */
  GSHSCENE_List * gl = rf_open(c);
  GSHSCENE_Group inner = rf_group(0.5, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_group_push(gl, &inner, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, gl, rf_rect(c, 0, 0, 12, 12), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  rf_fill(c, gl, rf_rect(c, 6, 6, 18, 18), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(gl) == GSHSCENE_OK);
  m = rf_mask_of(rf_picture_of(c, gl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at4 = rf_move(2.0, 21.0);
  rf_masked(c, l, rf_mask_intern(c, &m), 0.7, GSHSCENE_COMPOSITE_SRC_OVER, &at4, rf_rect(c, 2, 21, 22, 39), rf_solid(0.7, 0.2, 0.8, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_luminance(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  /* A picture of a saturated red, green and blue square and a mid gray: each coefficient set weighs them differently. */
  GSHSCENE_List * pl = rf_open(c);
  rf_fill(c, pl, rf_rect(c, 0, 0, 5, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 0, 0, 1.0), 0, 0.0);
  rf_fill(c, pl, rf_rect(c, 5, 0, 10, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0, 1, 0, 1.0), 0, 0.0);
  rf_fill(c, pl, rf_rect(c, 0, 8, 5, 16), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 1, 1.0), 0, 0.0);
  rf_fill(c, pl, rf_rect(c, 5, 8, 10, 16), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.5, 0.5, 0.5, 0.8), 0, 0.0);
  const GSHSCENE_Resource * picture = rf_picture_of(c, pl);
  for (uint32_t k = 0; k < GSHSCENE_MASK_COEFF_COUNT; k++) {
    GSHSCENE_Mask m = rf_mask_of(picture, GSHSCENE_MASK_LUMINANCE, GSHSCENE_MASK_ACTS_ON_OPACITY);
    m.coefficients = k;
    const double x = 1.0 + 11.5 * (double)k;
    GSHGEOM_Map at = rf_move(x, 1.0);
    rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at, rf_rect(c, x, 1, x + 10, 17), rf_solid(0.1, 0.1, 0.1, 1.0));
  }
  /* The picture rendered in linear light and its luminance taken in sRGB, over a mid gray backdrop, against the same in sRGB. */
  GSHSCENE_List * ol = rf_open(c);
  rf_fill(c, ol, rf_rect(c, 0, 0, 10, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.5), 0, 0.0);
  rf_fill(c, ol, rf_rect(c, 3, 2, 10, 14), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.9, 0.5), 0, 0.0);
  rf_fill(c, ol, rf_rect(c, 0, 10, 10, 16), NULL, GSHGEOM_FILL_NONZERO, rf_solid(-0.25, 1.5, 0.5, 1.0), 0, 0.0);
  const GSHSCENE_Resource * overlap = rf_picture_of(c, ol);
  for (int kind = 0; kind < 3; kind++) {
    GSHSCENE_Mask m = rf_mask_of(overlap, GSHSCENE_MASK_LUMINANCE, GSHSCENE_MASK_ACTS_ON_OPACITY);
    if (kind == 0) {
      m.render_space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
      m.space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0);
    } else if (kind == 1) {
      m.render_space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0);
      m.space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
    } else {
      m.backdrop[0] = 0.5;
      m.backdrop[1] = 0.5;
      m.backdrop[2] = 0.5;
      m.coefficients = GSHSCENE_MASK_COEFF_LUM;
      m.space = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0);
      m.backdrop_space = m.space;
    }
    const double x = 1.0 + 15.0 * (double)kind;
    GSHGEOM_Map at = rf_move(x, 21.0);
    rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at, rf_rect(c, x, 21, x + 10, 37), rf_solid(0.9, 0.9, 0.2, 1.0));
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_transfers(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 1, 0.0)), rf_stop(1.0, rf_solid(1, 1, 1, 1.0))};
  const GSHSCENE_Resource * ramp = rf_gradient(c, rf_linear(0, 0, 10, 0, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
  const GSHSCENE_Resource * picture = rf_picture_fill(c, rf_rect(c, 0, 0, 10, 14), rf_gradient_paint(ramp, 1.0));
  static const double table[4] = {0.0, 0.8, 0.2, 1.0};
  static const double fine[3] = {0.1, 0.5, 0.9};
  GSHSCENE_Transfer_Piece pieces[3];
  memset(pieces, 0, sizeof pieces);
  pieces[0].lo = 0.0;
  pieces[0].hi = 0.3;
  pieces[0].e0 = 0.0;
  pieces[0].e1 = 1.0;
  pieces[0].leaf_kind = GSHSCENE_TRANSFER_IDENTITY;
  pieces[1].lo = 0.3;
  pieces[1].hi = 0.7;
  pieces[1].e0 = 0.0;
  pieces[1].e1 = 1.0;
  pieces[1].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  pieces[1].c0 = 0.2;
  pieces[1].c1 = 0.9;
  pieces[1].n = 2.5;
  pieces[2].lo = 0.7;
  pieces[2].hi = 1.0;
  pieces[2].e0 = 1.0;
  pieces[2].e1 = 0.0;
  pieces[2].leaf_kind = GSHSCENE_TRANSFER_TABLE;
  pieces[2].sample_count = 3;
  pieces[2].samples = fine;
  for (int k = 0; k < 8; k++) {
    GSHSCENE_Mask m = rf_mask_of(picture, k % 2 ? GSHSCENE_MASK_LUMINANCE : GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
    switch (k / 2) {
      case 0:
        m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
        m.transfer.c0 = 0.0;
        m.transfer.c1 = 1.0;
        m.transfer.n = 2.0;
        break;
      case 1:
        m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
        m.transfer.sample_count = 4;
        m.transfer.samples = table;
        break;
      case 2:
        m.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
        m.transfer.piece_count = 3;
        m.transfer.pieces = pieces;
        break;
      default:
        m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL; /* a value at 0: the outside value is 0.3, everywhere the picture is not */
        m.transfer.c0 = 0.3;
        m.transfer.c1 = 0.9;
        m.transfer.n = 0.5;
        break;
    }
    const double x = 1.0 + 11.5 * (double)(k % 4);
    const double y = 1.0 + 19.0 * (double)(k / 4);
    GSHGEOM_Map at = rf_move(x + 0.5, y);
    rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at, rf_rect(c, x, y, x + 11, y + 15), rf_solid(0.2, 0.2, 0.9, 1.0));
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_outside(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.9, 0.8, 1.0), 0, 0.0);
  /* An empty picture: the mask is its outside value everywhere. */
  const GSHSCENE_Resource * empty = rf_picture_of(c, rf_open(c));
  static const double half[2] = {0.5, 0.5};
  for (int k = 0; k < 3; k++) {
    GSHSCENE_Mask m = rf_mask_of(empty, k == 2 ? GSHSCENE_MASK_LUMINANCE : GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
    if (k == 1) {
      m.transfer.kind = GSHSCENE_TRANSFER_TABLE; /* TR(0) = 0.5 */
      m.transfer.sample_count = 2;
      m.transfer.samples = half;
    } else if (k == 2) {
      m.backdrop[0] = 0.25;
      m.backdrop[1] = 0.5;
      m.backdrop[2] = 1.0;
    }
    const double x = 1.0 + 15.5 * (double)k;
    rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL, rf_rect(c, x, 1, x + 13, 12), rf_solid(0.1, 0.5, 0.2, 1.0));
  }
  /* A picture that covers part of the group: the mask is the picture inside it and the outside value elsewhere, the transfer's
   * nonzero value at 0 and a nonblack backdrop in a luminance mask. */
  const GSHSCENE_Resource * part = rf_picture_fill(c, rf_rect(c, 3, 3, 9, 9), rf_solid(0.2, 0.2, 0.2, 1.0));
  for (int k = 0; k < 3; k++) {
    GSHSCENE_Mask m = rf_mask_of(part, k == 0 ? GSHSCENE_MASK_ALPHA : GSHSCENE_MASK_LUMINANCE, GSHSCENE_MASK_ACTS_ON_OPACITY);
    if (k == 1) {
      m.backdrop[0] = m.backdrop[1] = m.backdrop[2] = 1.0;
    } else if (k == 2) {
      m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
      m.transfer.sample_count = 2;
      m.transfer.samples = half;
      m.backdrop[0] = 0.6;
    }
    const double x = 1.0 + 15.5 * (double)k;
    GSHGEOM_Map at = rf_move(x + 1.0, 15.0);
    rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at, rf_rect(c, x, 14, x + 13, 26), rf_solid(0.8, 0.1, 0.3, 1.0));
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_acts_on(RF_Ctx * c) {
  static const uint32_t ops[8] = {GSHSCENE_COMPOSITE_SRC_OVER, GSHSCENE_COMPOSITE_MULTIPLY, GSHSCENE_COMPOSITE_PLUS,
      GSHSCENE_COMPOSITE_DEST_IN, GSHSCENE_COMPOSITE_SRC, GSHSCENE_COMPOSITE_SRC_IN, GSHSCENE_COMPOSITE_SRC_OUT,
      GSHSCENE_COMPOSITE_CLEAR};
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.75, 0.1, 0.5, 0.9), 0, 0.0);
  /* The mask: half a pixel's worth of translucence and a triangle of full value. */
  GSHSCENE_List * pl = rf_open(c);
  rf_fill(c, pl, rf_rect(c, 0, 0, 11, 9), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 0.5), 0, 0.0);
  static const double tri[6] = {0.5, 0.5, 10.5, 0.5, 0.5, 8.5};
  rf_fill(c, pl, rf_polyline(c, tri, 3, 1), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  const GSHSCENE_Resource * picture = rf_picture_of(c, pl);
  for (int k = 0; k < 16; k++) {
    GSHSCENE_Mask m = rf_mask_of(picture, GSHSCENE_MASK_ALPHA, k < 8 ? GSHSCENE_MASK_ACTS_ON_OPACITY : GSHSCENE_MASK_ACTS_ON_SHAPE);
    const double x = 0.5 + 12.0 * (double)(k % 4);
    const double y = 0.5 + 10.0 * (double)(k / 4);
    /* Each cell is clipped, so that an unbounded operator acts inside its own cell only (N8). */
    rf_check(c, gshscene_list_clip_push(l, rf_rect(c, x, y, x + 11, y + 9), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    GSHGEOM_Map at = rf_move(x, y);
    rf_masked(c, l, rf_mask_intern(c, &m), 0.9, ops[k % 8], &at, rf_rect(c, x + 1, y + 1, x + 10, y + 8), rf_solid(0.2, 0.7, 0.9, 0.95));
    rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_nested(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  /* Three masks one inside the other: each level's picture is a group of a wider fill masked by the level below. */
  const GSHSCENE_Resource * level = rf_picture_fill(c, rf_star(c, 10.0, 10.5, 9.0), rf_solid(1, 1, 1, 1.0));
  for (int depth = 1; depth <= 3; depth++) {
    GSHSCENE_Mask m = rf_mask_of(level, GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
    GSHSCENE_List * up = rf_open(c);
    GSHSCENE_Group g = rf_group(1.0 - 0.1 * (double)depth, GSHSCENE_COMPOSITE_SRC_OVER);
    g.soft_mask = rf_mask_intern(c, &m);
    rf_check(c, gshscene_list_group_push(up, &g, 0, 0.0) == GSHSCENE_OK);
    rf_fill(c, up, rf_rect(c, 0, 0, 20, 20 - depth), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
    rf_check(c, gshscene_list_group_pop(up) == GSHSCENE_OK);
    level = rf_picture_of(c, up);
  }
  GSHSCENE_Mask outer = rf_mask_of(level, GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at = rf_move(1.0, 1.0);
  rf_masked(c, l, rf_mask_intern(c, &outer), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at, rf_rect(c, 1, 1, 21, 21), rf_solid(0.9, 0.4, 0.1, 1.0));
  /* A masked group inside an instanced picture, the instance turned. */
  GSHSCENE_Mask small = rf_mask_of(rf_picture_fill(c, rf_rect(c, 0, 0, 6, 12), rf_solid(1, 1, 1, 1.0)), GSHSCENE_MASK_ALPHA,
      GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHSCENE_List * inner = rf_open(c);
  rf_masked(c, inner, rf_mask_intern(c, &small), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL, rf_rect(c, 0, 0, 12, 12), rf_solid(0.1, 0.5, 0.9, 1.0));
  const GSHSCENE_Resource * pic = rf_picture_of(c, inner);
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 25.0, &turn);
  GSHGEOM_Map into = rf_move(34.0, 4.0);
  GSHGEOM_Map two[2] = {into, turn};
  GSHGEOM_Map placed;
  gshgeom_map_compose_chain(two, 2, NULL, &placed);
  rf_check(c, gshscene_list_instance(l, pic, &placed, 0, 0.0) == GSHSCENE_OK);
  /* A mask whose picture holds an instance of another picture. */
  GSHSCENE_List * host = rf_open(c);
  GSHGEOM_Map shift = rf_move(2.0, 2.0);
  rf_check(c, gshscene_list_instance(host, rf_picture_fill(c, rf_rect(c, 0, 0, 8, 6), rf_solid(1, 1, 1, 1.0)), &shift, 0, 0.0) == GSHSCENE_OK);
  GSHSCENE_Mask via = rf_mask_of(rf_picture_of(c, host), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at2 = rf_move(4.0, 26.0);
  rf_masked(c, l, rf_mask_intern(c, &via), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at2, rf_rect(c, 4, 26, 16, 36), rf_solid(0.3, 0.8, 0.4, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_frames(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.85, 0.9, 0.95, 1.0), 0, 0.0);
  /* One mask, a square with a notch, placed five ways by the group: a handle, so one mask serves them all. */
  GSHSCENE_List * pl = rf_open(c);
  rf_fill(c, pl, rf_rect(c, 0, 0, 8, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  static const double notch[6] = {2.0, 0.0, 6.0, 0.0, 4.0, 4.0};
  rf_fill(c, pl, rf_polyline(c, notch, 3, 1), NULL, GSHGEOM_FILL_EVEN_ODD, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  GSHSCENE_Mask m = rf_mask_of(rf_picture_of(c, pl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  const GSHSCENE_Resource * mask = rf_mask_intern(c, &m);
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 20.0, &turn);
  GSHGEOM_Map placements[5];
  GSHGEOM_Map chain[2] = {rf_move(0.0, 0.0), turn};
  /* Each cell is a fill 8 wide and 14 tall at (1 + 9 k, 1); the mask's 8 by 8 square is put over it five ways. */
  placements[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.75, 1.0, 1.0);
  placements[1] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, 10.0, 1.0);
  chain[0] = rf_move(21.0, 1.0);
  gshgeom_map_compose_chain(chain, 2, NULL, &placements[2]);
  placements[3] = rf_affine(c, 1.0, 0.5, 0.0, 1.75, 28.0, 1.0);
  placements[4] = rf_affine(c, 0.0, 1.0, 1.75, 0.0, 37.0, 1.0);
  for (int k = 0; k < 5; k++) {
    const double x = 1.0 + 9.0 * (double)k;
    rf_masked(c, l, mask, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &placements[k], rf_rect(c, x, 1, x + 8, 15), rf_solid(0.8 - 0.1 * (double)k, 0.2 * (double)k, 0.5, 0.95));
  }
  /* A singular placement: every item of the picture is degenerate, and the group is held to the mask's outside value, here 0.4. */
  GSHSCENE_Mask flat = rf_mask_of(m.picture, GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  flat.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  flat.transfer.c0 = 0.4;
  flat.transfer.c1 = 0.4;
  flat.transfer.n = 1.0;
  GSHGEOM_Map singular = rf_affine(c, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  rf_masked(c, l, rf_mask_intern(c, &flat), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &singular, rf_rect(c, 2, 18, 22, 28), rf_solid(0.1, 0.1, 0.8, 1.0));
  /* An instance of a picture holding a masked group, under a map: the chain of maps reaches the mask. */
  GSHSCENE_List * inner = rf_open(c);
  GSHGEOM_Map in_place = rf_move(1.0, 1.0);
  rf_masked(c, inner, mask, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &in_place, rf_rect(c, 0, 0, 10, 10), rf_solid(0.9, 0.5, 0.0, 1.0));
  const GSHSCENE_Resource * pic = rf_picture_of(c, inner);
  GSHGEOM_Map scale = rf_affine(c, 1.5, 0.0, 0.0, 1.5, 26.0, 18.0);
  rf_check(c, gshscene_list_instance(l, pic, &scale, 0, 0.0) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_compose(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space linear = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  const GSHSCENE_Color_Space p3 = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.4, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 4, 4, 30, 20), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.4, 0.8, 0.9), 0, 0.0);
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 1, 1.0)), rf_stop(1.0, rf_solid(0, 0, 0, 1.0))};
  const GSHSCENE_Resource * ramp = rf_gradient(c, rf_linear(0, 0, 0, 20, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
  GSHSCENE_Mask lum = rf_mask_of(rf_picture_fill(c, rf_rect(c, 0, 0, 24, 20), rf_gradient_paint(ramp, 1.0)), GSHSCENE_MASK_LUMINANCE,
      GSHSCENE_MASK_ACTS_ON_OPACITY);
  /* A clip, a mask, an opacity and a blend mode on one group: the product, then the composite. */
  rf_check(c, gshscene_list_clip_push(l, rf_blob(c, 3.0, 2.0, 10.0), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  GSHGEOM_Map at = rf_move(3.0, 3.0);
  GSHSCENE_Group g = rf_group(0.8, GSHSCENE_COMPOSITE_MULTIPLY);
  g.soft_mask = rf_mask_intern(c, &lum);
  g.soft_mask_map = at;
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 3, 3, 27, 23), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.2, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  /* A group that blends in linear light, with a mask rendered in Display P3 and its luminance taken in P3. */
  GSHSCENE_Mask wide = rf_mask_of(rf_picture_fill(c, rf_star(c, 8.0, 8.0, 7.5), rf_solid_in(0.9, 0.2, 0.1, 0.9, p3)),
      GSHSCENE_MASK_LUMINANCE, GSHSCENE_MASK_ACTS_ON_OPACITY);
  wide.render_space = p3;
  wide.coefficients = GSHSCENE_MASK_COEFF_SPACE_Y;
  GSHSCENE_Group lg = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  lg.blend_space = linear;
  lg.soft_mask = rf_mask_intern(c, &wide);
  GSHGEOM_Map at2 = rf_move(31.0, 2.0);
  lg.soft_mask_map = at2;
  rf_check(c, gshscene_list_group_push(l, &lg, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 31, 2, 47, 18), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.8, 0.3, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  /* An unbounded operator on a small group, masked: the mask is drawn over the whole of the clip's reach. */
  GSHSCENE_Mask half = rf_mask_of(rf_picture_fill(c, rf_rect(c, 0, 0, 7, 12), rf_solid(1, 1, 1, 0.6)), GSHSCENE_MASK_ALPHA,
      GSHSCENE_MASK_ACTS_ON_SHAPE);
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 4, 24, 22, 38), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  GSHGEOM_Map at3 = rf_move(8.0, 25.0);
  rf_masked(c, l, rf_mask_intern(c, &half), 1.0, GSHSCENE_COMPOSITE_DEST_IN, &at3, rf_rect(c, 10, 26, 14, 30), rf_solid(1, 1, 1, 1.0));
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  /* PDF's soft mask: a luminosity mask with a backdrop colour and a stitched transfer, on the shape. */
  GSHSCENE_Mask pdf = rf_mask_of(rf_picture_fill(c, rf_circle(c, 6.0), rf_solid(0.8, 0.8, 0.8, 1.0)), GSHSCENE_MASK_LUMINANCE,
      GSHSCENE_MASK_ACTS_ON_SHAPE);
  pdf.coefficients = GSHSCENE_MASK_COEFF_LUM;
  pdf.backdrop[0] = 0.2;
  pdf.backdrop[1] = 0.2;
  pdf.backdrop[2] = 0.2;
  pdf.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  pdf.transfer.c0 = 0.0;
  pdf.transfer.c1 = 1.0;
  pdf.transfer.n = 1.8;
  GSHGEOM_Map at4 = rf_move(36.0, 30.0);
  rf_masked(c, l, rf_mask_intern(c, &pdf), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at4, rf_rect(c, 26, 22, 46, 39), rf_solid(0.6, 0.1, 0.5, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_mask_not_a_clip(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.9, 1.0), 0, 0.0);
  /* Two shapes that abut at x = 7.5, half-covering the pixel they share, as the pieces of an alpha mask: 3/4 there. */
  GSHSCENE_List * pl = rf_open(c);
  rf_fill(c, pl, rf_rect(c, 0, 0, 7.5, 10), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  rf_fill(c, pl, rf_rect(c, 7.5, 0, 15, 10), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 1.0), 0, 0.0);
  GSHSCENE_Mask m = rf_mask_of(rf_picture_of(c, pl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHGEOM_Map at = rf_move(2.0, 2.0);
  rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &at, rf_rect(c, 2, 2, 17, 12), rf_solid(0.8, 0.0, 0.0, 1.0));
  /* The exact union of the same two shapes as one clip: whole where they meet. */
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 2, 16, 17, 26), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 2, 16, 17, 26), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.0, 0.0, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  /* The same two shapes at a quarter-pixel seam, rotated: the identity holds along an edge that is not on the grid. */
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 12.0, &turn);
  GSHGEOM_Map chain[2] = {rf_move(30.0, 3.0), turn};
  GSHGEOM_Map placed;
  gshgeom_map_compose_chain(chain, 2, NULL, &placed);
  rf_masked(c, l, rf_mask_intern(c, &m), 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &placed, rf_rect(c, 26, 0, 46, 22), rf_solid(0.0, 0.0, 0.8, 1.0));
  rf_close(c, l);
  return l;
}


/* --- non-isolated and knockout groups (masks-images-patterns story M3) ------------------------------------------ */

/** Push a group of @p opacity and @p composite that is non-isolated if @p isolation and knockout if @p knockout, in @p space (or the target's). */
static inline void rf_general(RF_Ctx * c, GSHSCENE_List * l, int isolation, int knockout, double opacity, uint32_t composite,
    const GSHSCENE_Color_Space * space) {
  GSHSCENE_Group g = rf_group(opacity, composite);
  g.isolation = isolation ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
  g.knockout = knockout ? 1u : 0u;
  if (space) {
    g.blend_space = *space;
  }
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
}

static inline void rf_pop(RF_Ctx * c, GSHSCENE_List * l) {
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
}

/** Two overlapping translucent discs and a star under a blend mode, drawn in a group of each kind, over the page. */
static inline GSHSCENE_List * rf_group_non_isolated(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.85, 0.8, 0.3, 1.0), 0, 0.0);
  for (int k = 0; k < 4; k++) {
    /* The same picture four times: isolated, non-isolated, each with a multiply group and with a plain one. */
    const int ni = k & 1;
    GSHGEOM_Map at = rf_move(1.0 + 12.0 * (double)k, 2.0);
    rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 1.0 + 12.0 * (double)k, 2.0, 12.0 + 12.0 * (double)k, 18.0), NULL,
                    GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    rf_general(c, l, ni, 0, k < 2 ? 1.0 : 0.7, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
    rf_fill(c, l, rf_circle(c, 5.0), &at, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.2, 0.8), 0, 0.0);
    /* a blend mode inside the group: a non-isolated group blends with the page, an isolated one with its own layer */
    GSHGEOM_Map at2 = rf_move(5.0 + 12.0 * (double)k, 7.0);
    rf_fill(c, l, rf_circle(c, 5.0), &at2, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.4, 0.9, 0.8), 0, 0.0);
    rf_pop(c, l);
    rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  }
  /* a blend mode as the fill's own group inside a non-isolated group: the page shows through to it */
  rf_general(c, l, 1, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_rect(c, 2, 22, 40, 36), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.2, 0.5), 0, 0.0);
  rf_general(c, l, 0, 0, 1.0, GSHSCENE_COMPOSITE_MULTIPLY, NULL);
  rf_fill(c, l, rf_star(c, 21.0, 29.0, 8.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.3, 0.7, 0.9), 0, 0.0);
  rf_pop(c, l);
  rf_general(c, l, 1, 0, 0.8, GSHSCENE_COMPOSITE_SCREEN, NULL);
  GSHGEOM_Map lamp = rf_move(30.0, 26.0);
  rf_fill(c, l, rf_circle(c, 6.0), &lamp, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.2, 0.7), 0, 0.0);
  rf_pop(c, l);
  rf_pop(c, l);
  rf_close(c, l);
  return l;
}

/** Overlapping translucent shapes in a knockout group and in a plain one, partial coverage at the edges, a clipped element. */
static inline GSHSCENE_List * rf_group_knockout(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.95, 1.0), 0, 0.0);
  for (int k = 0; k < 2; k++) {
    GSHGEOM_Map a = rf_move(2.0 + 22.0 * (double)k, 2.0);
    GSHGEOM_Map b = rf_move(8.0 + 22.0 * (double)k, 6.5);
    GSHGEOM_Map d = rf_move(5.0 + 22.0 * (double)k, 11.0);
    rf_general(c, l, k == 1, k == 0 || k == 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
    rf_fill(c, l, rf_circle(c, 7.0), &a, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.1, 0.7), 0, 0.0);
    rf_fill(c, l, rf_circle(c, 7.0), &b, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.8, 0.6), 0, 0.0);
    rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 3.0 + 22.0 * (double)k, 10.0, 15.5 + 22.0 * (double)k, 19.0), NULL,
                    GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    rf_fill(c, l, rf_star(c, 11.0 + 22.0 * (double)k, 13.0, 7.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.7, 0.2, 0.5), 0, 0.0);
    rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
    (void)d;
    rf_pop(c, l);
  }
  /* the plain group beside them: an isolated group of the same picture, and one at opacity 0.6 */
  rf_general(c, l, 0, 0, 0.6, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  GSHGEOM_Map p1 = rf_move(4.0, 24.0);
  GSHGEOM_Map p2 = rf_move(10.0, 28.5);
  rf_fill(c, l, rf_circle(c, 7.0), &p1, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.1, 0.7), 0, 0.0);
  rf_fill(c, l, rf_circle(c, 7.0), &p2, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.8, 0.6), 0, 0.0);
  rf_pop(c, l);
  /* a knockout group of abutting rectangles: each half-covers a pixel, so a quarter of the backdrop shows there (KC11) */
  rf_general(c, l, 0, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_rect(c, 26, 26, 33.5, 38), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.2, 1.0), 0, 0.0);
  rf_fill(c, l, rf_rect(c, 33.5, 26, 41, 38), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.2, 1.0), 0, 0.0);
  rf_pop(c, l);
  rf_close(c, l);
  return l;
}

/** A knockout group in a non-isolated group in a knockout group, an isolated child in a knockout parent, a non-isolated child in an isolated one. */
static inline GSHSCENE_List * rf_group_nested(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.9, 0.8, 1.0), 0, 0.0);
  GSHGEOM_Map a = rf_move(2.0, 2.0);
  GSHGEOM_Map b = rf_move(8.0, 6.0);
  GSHGEOM_Map d = rf_move(13.0, 4.0);
  rf_general(c, l, 0, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_circle(c, 8.0), &a, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.1, 0.6), 0, 0.0);
  rf_general(c, l, 1, 0, 0.9, GSHSCENE_COMPOSITE_MULTIPLY, NULL);
  rf_fill(c, l, rf_circle(c, 8.0), &b, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.5, 0.9, 0.7), 0, 0.0);
  rf_general(c, l, 0, 1, 0.8, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_circle(c, 8.0), &d, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.8, 0.3, 0.6), 0, 0.0);
  rf_fill(c, l, rf_star(c, 20.0, 12.0, 8.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.1, 0.5), 0, 0.0);
  rf_pop(c, l);
  rf_pop(c, l);
  rf_pop(c, l);
  /* an isolated child in a knockout parent, and a non-isolated child in an isolated parent and in a knockout parent */
  GSHGEOM_Map e = rf_move(2.0, 22.0);
  GSHGEOM_Map f = rf_move(9.0, 25.0);
  rf_general(c, l, 0, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_circle(c, 7.0), &e, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.1, 0.5, 0.8), 0, 0.0);
  rf_general(c, l, 0, 0, 0.7, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_circle(c, 7.0), &f, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.7, 0.8), 0, 0.0);
  rf_pop(c, l);
  rf_pop(c, l);
  GSHGEOM_Map g1 = rf_move(26.0, 22.0);
  GSHGEOM_Map g2 = rf_move(33.0, 25.0);
  rf_general(c, l, 0, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, l, rf_circle(c, 7.0), &g1, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.1, 0.5, 0.8), 0, 0.0);
  rf_general(c, l, 1, 0, 0.7, GSHSCENE_COMPOSITE_SCREEN, NULL);
  rf_fill(c, l, rf_circle(c, 7.0), &g2, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.7, 0.8), 0, 0.0);
  rf_pop(c, l);
  rf_pop(c, l);
  rf_close(c, l);
  return l;
}

/** Parent, group and child in different blend spaces (sRGB, linear light, Display P3, a gamma space): L0 converted into the group's space and G back. */
static inline GSHSCENE_List * rf_group_spaces(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  const GSHSCENE_Color_Space lin = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0);
  const GSHSCENE_Color_Space p3 = rf_space(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0.0);
  const GSHSCENE_Color_Space gam = rf_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.2);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.6, 0.7, 0.9, 1.0), 0, 0.0);
  const GSHSCENE_Color_Space * spaces[4] = {&lin, &p3, &gam, &lin};
  for (int k = 0; k < 4; k++) {
    GSHGEOM_Map a = rf_move(2.0 + 11.0 * (double)k, 2.0);
    GSHGEOM_Map b = rf_move(5.0 + 11.0 * (double)k, 6.0);
    rf_general(c, l, 1, k >= 2, 0.9, k % 2 ? GSHSCENE_COMPOSITE_MULTIPLY : GSHSCENE_COMPOSITE_SRC_OVER, spaces[k]);
    rf_fill(c, l, rf_circle(c, 4.5), &a, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.1, 0.8), 0, 0.0);
    rf_general(c, l, 1, 0, 0.8, GSHSCENE_COMPOSITE_SCREEN, spaces[(k + 1) % 4]);
    rf_fill(c, l, rf_circle(c, 4.5), &b, GSHGEOM_FILL_NONZERO, rf_solid_in(0.1, 0.8, 0.3, 0.7, p3), 0, 0.0);
    rf_pop(c, l);
    rf_pop(c, l);
  }
  /* an isolated group in linear light holding a non-isolated one in a gamma space, over the page in sRGB */
  rf_general(c, l, 0, 0, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, &lin);
  rf_fill(c, l, rf_rect(c, 2, 20, 30, 38), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.2, 0.6), 0, 0.0);
  rf_general(c, l, 1, 1, 0.9, GSHSCENE_COMPOSITE_MULTIPLY, &gam);
  GSHGEOM_Map disc = rf_move(16.0, 29.0);
  rf_fill(c, l, rf_circle(c, 9.0), &disc, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.5, 0.9, 0.7), 0, 0.0);
  rf_pop(c, l);
  rf_pop(c, l);
  rf_close(c, l);
  return l;
}

/** A child's pop under each operator (bounded and unbounded, PDF's and the Porter-Duff family) into a non-isolated parent and a knockout one. */
static inline GSHSCENE_List * rf_group_operators(RF_Ctx * c) {
  static const uint32_t ops[8] = {GSHSCENE_COMPOSITE_SRC_OVER, GSHSCENE_COMPOSITE_DEST_OVER, GSHSCENE_COMPOSITE_PLUS,
      GSHSCENE_COMPOSITE_XOR, GSHSCENE_COMPOSITE_DEST_IN, GSHSCENE_COMPOSITE_SRC, GSHSCENE_COMPOSITE_SRC_ATOP, GSHSCENE_COMPOSITE_DIFFERENCE};
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.85, 0.7, 1.0), 0, 0.0);
  for (int k = 0; k < 8; k++) {
    const int col = k % 4;
    const int row = k / 4;
    const double x = 1.0 + 12.0 * (double)col;
    const double y = 1.0 + 19.0 * (double)row;
    rf_check(c, gshscene_list_clip_push(l, rf_rect(c, x, y, x + 11.0, y + 18.0), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    rf_general(c, l, 1, row == 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
    GSHGEOM_Map a = rf_move(x + 1.0, y + 1.0);
    GSHGEOM_Map b = rf_move(x + 4.0, y + 6.0);
    rf_fill(c, l, rf_rect(c, 0, 0, 9, 7), &a, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.2, 0.2, 0.6), 0, 0.0);
    rf_general(c, l, 0, 0, 0.8, ops[k], NULL);
    rf_fill(c, l, rf_circle(c, 4.0), &b, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3, 0.9, 0.7), 0, 0.0);
    rf_pop(c, l);
    rf_pop(c, l);
    rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  }
  rf_close(c, l);
  return l;
}

/** A stroke, a gradient fill, a glyph, an instance's items and a child group, each one element of a knockout group. */
static inline GSHSCENE_List * rf_group_elements(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.95, 0.85, 1.0), 0, 0.0);
  GSHSCENE_List * pl = rf_open(c);
  rf_fill(c, pl, rf_circle(c, 5.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.2, 0.2, 0.7), 0, 0.0);
  GSHGEOM_Map shifted = rf_move(4.0, 3.0);
  rf_fill(c, pl, rf_circle(c, 5.0), &shifted, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.8, 0.7), 0, 0.0);
  const GSHSCENE_Resource * pic = rf_picture_of(c, pl);
  const GSHSCENE_Resource * star = rf_star(c, 8.0, 8.0, 8.0);
  const GSHSCENE_Resource * pen = rf_pen(c, 3.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND);
  for (int k = 0; k < 2; k++) {
    const double dy = 20.0 * (double)k;
    rf_general(c, l, k, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
    GSHGEOM_Map s1 = rf_move(2.0, 1.0 + dy);
    rf_fill(c, l, star, &s1, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.2, 0.8), 0, 0.0);
    GSHGEOM_Map s2 = rf_move(5.0, 3.0 + dy);
    rf_stroke(c, l, star, pen, &s2, rf_solid(0.1, 0.4, 0.6, 0.6));
    const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 0.8)), rf_stop(1.0, rf_solid(0.1, 0.1, 0.9, 0.8))};
    rf_fill(c, l, rf_rect(c, 20, 2.0 + dy, 32, 16.0 + dy), NULL, GSHGEOM_FILL_NONZERO,
        rf_gradient_paint(rf_gradient(c, rf_linear(20, 0, 32, 0, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0), 0, 0.0);
    GSHGEOM_Map at = rf_move(26.0, 6.0 + dy);
    rf_check(c, gshscene_list_instance(l, pic, &at, 0, 0.0) == GSHSCENE_OK);
    rf_general(c, l, 0, 0, 0.5, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
    GSHGEOM_Map far = rf_move(40.0, 8.0 + dy);
    rf_fill(c, l, rf_circle(c, 5.0), &far, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.7, 0.2, 0.9), 0, 0.0);
    rf_pop(c, l);
    rf_pop(c, l);
  }
  rf_close(c, l);
  return l;
}

/** A masked non-isolated group, a masked knockout group, and a mask whose picture holds a knockout group. */
static inline GSHSCENE_List * rf_group_masked(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.85, 0.6, 1.0), 0, 0.0);
  for (int k = 0; k < 3; k++) {
    GSHSCENE_Mask m = rf_mask_of(rf_picture_fill(c, rf_circle(c, 7.0), rf_solid(1, 1, 1, 0.8)), k == 1 ? GSHSCENE_MASK_LUMINANCE : GSHSCENE_MASK_ALPHA,
        k == 2 ? GSHSCENE_MASK_ACTS_ON_SHAPE : GSHSCENE_MASK_ACTS_ON_OPACITY);
    GSHSCENE_Group g = rf_group(0.9, k == 1 ? GSHSCENE_COMPOSITE_MULTIPLY : GSHSCENE_COMPOSITE_SRC_OVER);
    g.isolation = k != 1 ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
    g.knockout = k >= 1;
    g.soft_mask = rf_mask_intern(c, &m);
    GSHGEOM_Map place = rf_move(2.0 + 15.0 * (double)k, 2.0);
    g.soft_mask_map = place;
    rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
    GSHGEOM_Map a = rf_move(4.0 + 15.0 * (double)k, 4.0);
    GSHGEOM_Map b = rf_move(8.0 + 15.0 * (double)k, 8.0);
    rf_fill(c, l, rf_rect(c, 0, 0, 12, 12), &a, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.2, 0.7), 0, 0.0);
    rf_fill(c, l, rf_rect(c, 0, 0, 12, 12), &b, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.2, 0.8, 0.6), 0, 0.0);
    rf_pop(c, l);
  }
  /* a mask whose picture is a knockout group of two shapes */
  GSHSCENE_List * pl = rf_open(c);
  rf_general(c, pl, 0, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, pl, rf_circle(c, 8.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 0.9), 0, 0.0);
  GSHGEOM_Map second = rf_move(7.0, 3.0);
  rf_fill(c, pl, rf_circle(c, 8.0), &second, GSHGEOM_FILL_NONZERO, rf_solid(1, 1, 1, 0.4), 0, 0.0);
  rf_pop(c, pl);
  GSHSCENE_Mask km = rf_mask_of(rf_picture_of(c, pl), GSHSCENE_MASK_ALPHA, GSHSCENE_MASK_ACTS_ON_OPACITY);
  GSHSCENE_Group g = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  g.soft_mask = rf_mask_intern(c, &km);
  GSHGEOM_Map place = rf_move(4.0, 20.0);
  g.soft_mask_map = place;
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 2, 20, 38, 38), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.2, 1.0), 0, 0.0);
  rf_pop(c, l);
  rf_close(c, l);
  return l;
}

/* --- patterns (masks-images-patterns story R2) -------------------------------------------------------------- */

/** Intern a pattern of @p tile over the cell (@p x, @p y, @p w, @p h) at steps (@p sx, @p sy), repeating where @p rx and @p ry say, under @p map. */
static inline const GSHSCENE_Resource * rf_pattern(RF_Ctx * c, const GSHSCENE_Resource * tile, double x, double y, double w, double h,
    double sx, double sy, uint32_t rx, uint32_t ry, GSHGEOM_Map map, const GSHSCENE_Paint * tint) {
  GSHSCENE_Pattern p;
  gshscene_pattern_init(&p);
  p.tile = tile;
  p.x = x;
  p.y = y;
  p.w = w;
  p.h = h;
  p.x_step = sx;
  p.y_step = sy;
  p.repeat_x = rx;
  p.repeat_y = ry;
  p.map = map;
  if (tint) {
    p.has_tint = 1;
    p.tint = *tint;
  }
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_pattern(c->store, &p, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

static inline GSHSCENE_Paint rf_pattern_paint(const GSHSCENE_Resource * pattern, double alpha) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_PATTERN;
  p.resource = pattern;
  p.rgba[3] = alpha;
  return p;
}

/** A tile of a cell-filling background, a diagonal and a disc: a picture of the cell (0, 0, 6, 6). */
static inline const GSHSCENE_Resource * rf_tile_basic(RF_Ctx * c) {
  GSHSCENE_List * t = rf_open(c);
  rf_fill(c, t, rf_rect(c, 0, 0, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.85, 0.5, 1.0), 0, 0.0);
  const double tri[6] = {0.5, 0.5, 5.5, 1.5, 2.5, 5.5};
  rf_fill(c, t, rf_polyline(c, tri, 3, 1), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.15, 0.3, 0.7, 0.9), 0, 0.0);
  GSHGEOM_Map at = rf_move(4.5, 4.5);
  rf_fill(c, t, rf_circle(c, 1.7), &at, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.2, 0.8), 0, 0.0);
  return rf_picture_of(c, t);
}

/** The same tile with no background: its alpha is its shapes', which is all an uncolored pattern reads (PT9). */
static inline const GSHSCENE_Resource * rf_tile_open(RF_Ctx * c) {
  GSHSCENE_List * t = rf_open(c);
  const double tri[6] = {0.5, 0.5, 5.5, 1.5, 2.5, 5.5};
  rf_fill(c, t, rf_polyline(c, tri, 3, 1), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.15, 0.3, 0.7, 0.9), 0, 0.0);
  GSHGEOM_Map at = rf_move(4.5, 4.5);
  rf_fill(c, t, rf_circle(c, 1.7), &at, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.2, 0.8), 0, 0.0);
  return rf_picture_of(c, t);
}

/** Cells that abut, at a fraction, under a scale and a quarter turn's worth of skew; and the same lattice gapped, as CSS `space`. */
static inline GSHSCENE_List * rf_pattern_lattices(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.95, 1.0), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_tile_basic(c);
  const GSHSCENE_Resource * abut = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.3, 0.45), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(abut, 1.0), 0, 0.0);
  const GSHSCENE_Resource * gap = rf_pattern(c, tile, 0, 0, 6, 6, 8.25, 7.5, 1, 1, rf_affine(c, 1.0, 0.0, 0.35, 1.0, 0.1, 0.2), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(gap, 1.0), 0, 0.0);
  const GSHSCENE_Resource * scaled = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 1, 1, rf_affine(c, 1.7, 0.0, 0.0, 0.85, 0.5, 0.25), NULL);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(scaled, 0.8), 0, 0.0);
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 17.0, &turn);
  const GSHSCENE_Resource * rot = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 1, 1, turn, NULL);
  GSHGEOM_Map place = rf_move(24.0, 21.0);
  rf_fill(c, l, rf_rect(c, 0, 0, 22, 18), &place, GSHGEOM_FILL_NONZERO, rf_pattern_paint(rot, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** Overlapping cells (PDF's steps smaller than the cell), one axis only, and a tile that spills over its cell. */
static inline GSHSCENE_List * rf_pattern_overlap(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 1.0, 1.0), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_tile_basic(c);
  const GSHSCENE_Resource * ox = rf_pattern(c, tile, 0, 0, 6, 6, 4.5, 6, 1, 1, rf_move(0.25, 0.5), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(ox, 1.0), 0, 0.0);
  const GSHSCENE_Resource * oxy = rf_pattern(c, tile, 0, 0, 6, 6, 4.0, 4.5, 1, 1, rf_move(0.0, 0.0), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(oxy, 0.9), 0, 0.0);
  const GSHSCENE_Resource * once = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 0, 1, rf_move(2.5, 0.0), NULL);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(once, 1.0), 0, 0.0);
  GSHSCENE_List * spill = rf_open(c);
  GSHGEOM_Map big = rf_move(3.0, 3.0);
  rf_fill(c, spill, rf_circle(c, 5.0), &big, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.6, 0.3, 0.7), 0, 0.0);
  const GSHSCENE_Resource * sp = rf_pattern(c, rf_picture_of(c, spill), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.0, 0.0), NULL);
  rf_fill(c, l, rf_rect(c, 25, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(sp, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** What a tile can hold: a gradient, an image, the foreground, a palette entry, a clip block, groups (opacity, knockout, non-isolated). */
static inline GSHSCENE_List * rf_pattern_content(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Color fg;
  memset(&fg, 0, sizeof fg);
  fg.rgba[0] = 0.1;
  fg.rgba[1] = 0.5;
  fg.rgba[2] = 0.3;
  fg.rgba[3] = 1.0;
  rf_check(c, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.9, 1.0), 0, 0.0);
  /* A gradient, an image and the foreground in one tile, each across a cell edge. */
  GSHSCENE_Gradient_Stop stops[3] = {rf_stop(0.0, rf_solid(1, 0, 0, 1)), rf_stop(0.5, rf_solid(0, 1, 0, 0.5)), rf_stop(1.0, rf_solid(0, 0, 1, 1))};
  const GSHSCENE_Resource * grad = rf_gradient(c, rf_linear(0, 0, 8, 8, 0, 0, GSHSCENE_EXTEND_PAD), stops, 3);
  GSHSCENE_Color_Space none;
  memset(&none, 0, sizeof none);
  const GSHSCENE_Resource * img = rf_image(c, 4, 3, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  GSHSCENE_Paint ip = rf_image_paint(c, img, rf_place(c, 2.0, 2.0, 1.0, 1.0), GSHSCENE_IMAGE_EXTEND_PAD, GSHSCENE_IMAGE_EXTEND_PAD,
      GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_LINEAR, none, NULL, 1.0);
  GSHSCENE_List * t = rf_open(c);
  rf_fill(c, t, rf_rect(c, 0, 0, 8, 8), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(grad, 1.0), 0, 0.0);
  rf_fill(c, t, rf_rect(c, 3, 3, 8, 8), NULL, GSHGEOM_FILL_NONZERO, ip, 0, 0.0);
  GSHSCENE_Paint f = rf_solid(0, 0, 0, 0.6);
  f.kind = GSHSCENE_PAINT_FOREGROUND;
  f.rgba[3] = 0.6;
  GSHGEOM_Map disc_at = rf_move(5.0, 5.0);
  rf_fill(c, t, rf_circle(c, 2.0), &disc_at, GSHGEOM_FILL_NONZERO, f, 0, 0.0);
  const GSHSCENE_Resource * tile = rf_picture_of(c, t);
  const GSHSCENE_Resource * p1 = rf_pattern(c, tile, 0, 0, 8, 8, 8, 8, 1, 1, rf_move(0.4, 0.3), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(p1, 1.0), 0, 0.0);
  /* A clip block and groups. */
  GSHSCENE_List * g = rf_open(c);
  rf_fill(c, g, rf_rect(c, 0, 0, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.2, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_push(g, rf_circle(c, 3.5), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, g, rf_rect(c, 0, 0, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3, 0.8, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(g) == GSHSCENE_OK);
  rf_general(c, g, 0, 0, 0.6, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, g, rf_rect(c, 1, 1, 5, 5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.1, 0.1, 1.0), 0, 0.0);
  rf_pop(c, g);
  rf_general(c, g, 0, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, g, rf_rect(c, 2, 0, 4, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.1, 0.5), 0, 0.0);
  rf_fill(c, g, rf_rect(c, 0, 2, 6, 4), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.9, 0.5), 0, 0.0);
  rf_pop(c, g);
  rf_general(c, g, 1, 0, 1.0, GSHSCENE_COMPOSITE_MULTIPLY, NULL);
  rf_fill(c, g, rf_rect(c, 0, 4, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.4, 0.8, 0.9, 1.0), 0, 0.0);
  rf_pop(c, g);
  const GSHSCENE_Resource * p2 = rf_pattern(c, rf_picture_of(c, g), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.15, 0.6), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(p2, 1.0), 0, 0.0);
  /* Uncolored: a tint over the tile's colors. */
  GSHSCENE_Paint tint = rf_solid(0.7, 0.1, 0.5, 0.9);
  const GSHSCENE_Resource * p3 = rf_pattern(c, rf_tile_open(c), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(1.2, 0.2), &tint);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(p3, 0.5), 0, 0.0);
  GSHSCENE_Paint fgtint = rf_solid(0, 0, 0, 1.0);
  fgtint.kind = GSHSCENE_PAINT_FOREGROUND;
  const GSHSCENE_Resource * p4 = rf_pattern(c, rf_tile_open(c), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.6, 1.2), &fgtint);
  rf_fill(c, l, rf_rect(c, 25, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(p4, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** Strokes in a tile (crossing the cell's edge), a stroke painted with a pattern, and a pattern inside a tile. */
static inline GSHSCENE_List * rf_pattern_strokes(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 0.97, 1.0), 0, 0.0);
  GSHSCENE_List * t = rf_open(c);
  const double line[6] = {-1.0, 2.0, 3.0, 3.5, 7.0, 1.0};
  rf_stroke(c, t, rf_polyline(c, line, 3, 0), rf_pen(c, 1.3, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), NULL,
      rf_solid(0.1, 0.2, 0.5, 1.0));
  rf_fill(c, t, rf_rect(c, 1, 4, 5, 5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.7, 0.2, 0.1, 0.8), 0, 0.0);
  const GSHSCENE_Resource * sq = rf_pattern(c, rf_picture_of(c, t), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.35, 0.2), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(sq, 1.0), 0, 0.0);
  /* A stroke painted with a pattern. */
  const GSHSCENE_Resource * tile = rf_tile_basic(c);
  const GSHSCENE_Resource * pp = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.0, 0.5), NULL);
  const double zig[10] = {26.0, 17.0, 32.0, 3.0, 38.0, 17.0, 43.0, 4.0, 46.0, 15.0};
  rf_stroke(c, l, rf_polyline(c, zig, 5, 0), rf_pen(c, 5.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), NULL,
      rf_pattern_paint(pp, 1.0));
  /* A pattern inside a tile. */
  GSHSCENE_List * inner = rf_open(c);
  rf_fill(c, inner, rf_rect(c, 0, 0, 12, 12), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.95, 1.0, 1.0), 0, 0.0);
  const GSHSCENE_Resource * small = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 1, 1, rf_affine(c, 0.5, 0.0, 0.0, 0.5, 0.25, 0.5), NULL);
  rf_fill(c, inner, rf_rect(c, 1, 1, 9, 11), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(small, 1.0), 0, 0.0);
  const GSHSCENE_Resource * outer = rf_pattern(c, rf_picture_of(c, inner), 0, 0, 12, 12, 12, 12, 1, 1, rf_move(0.5, 0.25), NULL);
  rf_fill(c, l, rf_rect(c, 1, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(outer, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** Lattices whose steps are whole pixels: an axis-aligned one, a skewed one (steps (8, 3) and (0, 5)), and one rotated a quarter turn. */
static inline GSHSCENE_List * rf_pattern_integer(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.97, 0.97, 0.97, 1.0), 0, 0.0);
  const GSHSCENE_Resource * tile = rf_tile_basic(c);
  const GSHSCENE_Resource * a = rf_pattern(c, tile, 0, 0, 6, 6, 8, 8, 1, 1, rf_move(2.0, 1.0), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(a, 1.0), 0, 0.0);
  const GSHSCENE_Resource * s = rf_pattern(c, tile, 0, 0, 6, 6, 1, 1, 1, 1, rf_affine(c, 8.0, 3.0, 0.0, 5.0, 0.0, 0.0), NULL);
  (void)s;
  const GSHSCENE_Resource * k = rf_pattern(c, tile, 0, 0, 6, 6, 1, 1, 1, 1, rf_affine(c, 8.0, 0.0, 3.0, 5.0, 1.0, 1.0), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(k, 1.0), 0, 0.0);
  GSHGEOM_Map quarter;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &quarter);
  const GSHSCENE_Resource * q = rf_pattern(c, tile, 0, 0, 6, 6, 6, 6, 1, 1, quarter, NULL);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(q, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/**
 * A hatch tile of the cell (0, 0, w, h): a pale background, a stroke from the left edge to the right edge at a third of the height
 * (butt caps: it ends exactly at the cell's edges, so the copies' strokes join into a line), a diagonal stroke that crosses the cell's
 * edges (round caps) and a translucent corner rectangle.  Proportional to the cell, so one tile serves cells of 1 (under a map that
 * scales) and of 6.
 */
static inline const GSHSCENE_Resource * rf_tile_hatch(RF_Ctx * c, double w, double h) {
  GSHSCENE_List * t = rf_open(c);
  rf_fill(c, t, rf_rect(c, 0, 0, w, h), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.97, 0.93, 0.8, 1.0), 0, 0.0);
  const double line[4] = {0.0, h / 3.0, w, h / 3.0};
  rf_stroke(c, t, rf_polyline(c, line, 2, 0), rf_pen(c, h / 6.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), NULL,
      rf_solid(0.1, 0.2, 0.5, 1.0));
  const double diag[4] = {-0.1 * w, 1.1 * h, 1.1 * w, -0.1 * h};
  rf_stroke(c, t, rf_polyline(c, diag, 2, 0), rf_pen(c, h / 8.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND), NULL,
      rf_solid(0.7, 0.2, 0.1, 0.85));
  rf_fill(c, t, rf_rect(c, 0.6 * w, 0.6 * h, w, h), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.3, 0.6), 0, 0.0);
  return rf_picture_of(c, t);
}

/** Four lattices of whole-pixel steps, which the period tier draws from one window: abutting at a fraction, skewed (8, 3) and (0, 5), turned by an integer rotation, and gapped. */
static inline GSHSCENE_List * rf_pattern_period(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.92, 0.92, 0.96, 1.0), 0, 0.0);
  const GSHSCENE_Resource * six = rf_tile_hatch(c, 6, 6);
  const GSHSCENE_Resource * abut = rf_pattern(c, six, 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.3, 0.45), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(abut, 1.0), 0, 0.0);
  const GSHSCENE_Resource * unit = rf_tile_hatch(c, 1, 1);
  const GSHSCENE_Resource * skew = rf_pattern(c, unit, 0, 0, 1, 1, 1, 1, 1, 1, rf_affine(c, 8.0, 0.0, 3.0, 5.0, 0.4, 0.2), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(skew, 1.0), 0, 0.0);
  const GSHSCENE_Resource * turn = rf_pattern(c, unit, 0, 0, 1, 1, 1, 1, 1, 1, rf_affine(c, 3.0, 4.0, -4.0, 3.0, 0.25, 0.5), NULL);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(turn, 0.9), 0, 0.0);
  const GSHSCENE_Resource * four = rf_tile_hatch(c, 4, 4);
  const GSHSCENE_Resource * gap = rf_pattern(c, four, 0, 0, 4, 4, 7, 6, 1, 1, rf_move(1.5, 0.5), NULL);
  rf_fill(c, l, rf_rect(c, 25, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(gap, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** Whole-pixel lattices with a tile that blends: overlapping cells under a multiply group and a non-isolated group, an uncolored tile, and the foreground and a gradient. */
static inline GSHSCENE_List * rf_pattern_period_blend(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  GSHSCENE_Color fg;
  memset(&fg, 0, sizeof fg);
  fg.rgba[0] = 0.55;
  fg.rgba[1] = 0.15;
  fg.rgba[2] = 0.35;
  fg.rgba[3] = 1.0;
  rf_check(c, gshscene_list_set_foreground(l, &fg) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.9, 0.85, 1.0), 0, 0.0);
  GSHSCENE_List * m = rf_open(c);
  rf_fill(c, m, rf_rect(c, 0, 0, 8, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.9, 0.5, 0.8), 0, 0.0);
  rf_general(c, m, 0, 0, 1.0, GSHSCENE_COMPOSITE_MULTIPLY, NULL);
  rf_fill(c, m, rf_rect(c, 2, 2, 8, 8), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.3, 0.7, 0.9, 1.0), 0, 0.0);
  rf_pop(c, m);
  const GSHSCENE_Resource * multiply = rf_pattern(c, rf_picture_of(c, m), 0, 0, 8, 8, 6, 5, 1, 1, rf_move(0.2, 0.7), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(multiply, 1.0), 0, 0.0);
  GSHSCENE_List * n = rf_open(c);
  rf_fill(c, n, rf_rect(c, 0, 0, 7, 7), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.3, 0.2, 0.7), 0, 0.0);
  rf_general(c, n, 1, 0, 0.8, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, n, rf_rect(c, 1, 1, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3, 0.9, 0.8), 0, 0.0);
  rf_pop(c, n);
  rf_general(c, n, 0, 1, 1.0, GSHSCENE_COMPOSITE_SRC_OVER, NULL);
  rf_fill(c, n, rf_rect(c, 3, 0, 5, 7), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.1, 0.5), 0, 0.0);
  rf_fill(c, n, rf_rect(c, 0, 3, 7, 5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.9, 0.5), 0, 0.0);
  rf_pop(c, n);
  const GSHSCENE_Resource * general = rf_pattern(c, rf_picture_of(c, n), 0, 0, 7, 7, 6, 6, 1, 1, rf_move(0.5, 0.1), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(general, 1.0), 0, 0.0);
  GSHSCENE_Paint tint = rf_solid(0.1, 0.45, 0.6, 0.9);
  const GSHSCENE_Resource * open = rf_pattern(c, rf_tile_hatch(c, 5, 5), 0, 0, 5, 5, 5, 5, 1, 1, rf_move(0.6, 0.35), &tint);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(open, 0.8), 0, 0.0);
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 0.9, 0, 1)), rf_stop(1.0, rf_solid(0, 0.3, 0.9, 1))};
  const GSHSCENE_Resource * grad = rf_gradient(c, rf_linear(0, 0, 6, 6, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
  GSHSCENE_List * g = rf_open(c);
  rf_fill(c, g, rf_rect(c, 0, 0, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_gradient_paint(grad, 1.0), 0, 0.0);
  GSHSCENE_Paint f = rf_solid(0, 0, 0, 0.6);
  f.kind = GSHSCENE_PAINT_FOREGROUND;
  f.rgba[3] = 0.6;
  GSHGEOM_Map at = rf_move(3.0, 3.0);
  rf_fill(c, g, rf_circle(c, 2.2), &at, GSHGEOM_FILL_NONZERO, f, 0, 0.0);
  const GSHSCENE_Resource * graded = rf_pattern(c, rf_picture_of(c, g), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(1.35, 0.8), NULL);
  rf_fill(c, l, rf_rect(c, 25, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(graded, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** Hatch lines: strokes that end at the cell's edge and run on into the next copy, horizontal, vertical and diagonal, with butt and square caps, so that the lines are unbroken. */
static inline GSHSCENE_List * rf_pattern_hatch(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 0.98, 1.0), 0, 0.0);
  const int kinds = 4;
  for (int k = 0; k < kinds; k++) {
    GSHSCENE_List * t = rf_open(c);
    const double w = 4.0;
    const double horizontal[4] = {0.0, 1.5, w, 1.5};
    const double vertical[4] = {2.5, 0.0, 2.5, w};
    const double slant[4] = {0.0, w, w, 0.0};
    const double *line = k == 1 ? vertical : (k == 3 ? slant : horizontal);
    rf_stroke(c, t, rf_polyline(c, line, 2, 0),
        rf_pen(c, k == 2 ? 1.25 : 0.8, GSHSCENE_STROKE_JOIN_MITER, k == 3 ? GSHSCENE_STROKE_CAP_SQUARE : GSHSCENE_STROKE_CAP_BUTT), NULL,
        rf_solid(0.05, 0.1, 0.4, k == 2 ? 0.7 : 1.0));
    const GSHSCENE_Resource * pat = rf_pattern(c, rf_picture_of(c, t), 0, 0, w, w, w, w, 1, 1, rf_move(0.0 + 0.35 * (double)(k % 2), 0.2 * (double)k), NULL);
    rf_fill(c, l, rf_rect(c, 1 + 24 * (k % 2), 1 + 20 * (k / 2), 23 + 24 * (k % 2), 19 + 20 * (k / 2)), NULL, GSHGEOM_FILL_NONZERO,
        rf_pattern_paint(pat, 1.0), 0, 0.0);
  }
  rf_close(c, l);
  return l;
}


/* --- hatches (masks-images-patterns story H1) ----------------------------------------------------------------------- */

/** Intern a hatch of @p n families over the pen @p pen, with the ink @p ink. */
static inline const GSHSCENE_Resource * rf_hatch(RF_Ctx * c, const GSHSCENE_Resource * pen, uint32_t nesting,
    const GSHSCENE_Hatch_Family * f, size_t n, GSHGEOM_Map map, GSHSCENE_Paint ink) {
  GSHSCENE_Hatch h;
  gshscene_hatch_init(&h);
  h.style = pen;
  h.nesting = nesting;
  h.family_count = n;
  h.families = f;
  h.map = map;
  h.ink = ink;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_hatch(c->store, &h, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

static inline GSHSCENE_Paint rf_hatch_paint(const GSHSCENE_Resource * hatch, double alpha) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_HATCH;
  p.resource = hatch;
  p.rgba[3] = alpha;
  return p;
}

static inline const GSHSCENE_Resource * rf_ring_path(RF_Ctx * c, double x0, double y0, double x1, double y1, int levels) {
  GSHGEOM_Path * p = rf_path(c);
  double d = 0.0;
  for (int i = 0; i < levels; i++) {
    const int flip = i == 1 ? 1 : 0;
    const double a = x0 + d, b = y0 + d, e = x1 - d, f = y1 - d;
    gshgeom_path_move_to(p, rf_pt(flip ? a : a, b));
    if (flip) {
      gshgeom_path_line_to(p, rf_pt(a, f));
      gshgeom_path_line_to(p, rf_pt(e, f));
      gshgeom_path_line_to(p, rf_pt(e, b));
    } else {
      gshgeom_path_line_to(p, rf_pt(e, b));
      gshgeom_path_line_to(p, rf_pt(e, f));
      gshgeom_path_line_to(p, rf_pt(a, f));
    }
    gshgeom_path_close(p);
    d += 3.0;
  }
  return rf_intern(c, p);
}

/** One hatch panel: the hatch painted over @p region at (@p x, @p y). */
static inline void rf_hatch_panel(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * region, double x, double y,
    const GSHSCENE_Resource * hatch, double alpha, GSHGEOM_Fill_Rule rule) {
  GSHGEOM_Map at = rf_move(x, y);
  rf_fill(c, l, region, &at, rule, rf_hatch_paint(hatch, alpha), 0, 0.0);
}

/** Six panels of families: a single 45 degree family, a translucent crosshatch (one union), dashes, dots, a brick stagger, a line along the edge. */
static inline GSHSCENE_List * rf_hatch_families(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 0.97, 1.0), 0, 0.0);
  const GSHSCENE_Resource * box = rf_rect(c, 0.0, 0.0, 14.0, 11.0);
  const GSHSCENE_Resource * pen = rf_pen(c, 0.7, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  const GSHSCENE_Resource * round = rf_pen(c, 1.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_ROUND);
  const GSHSCENE_Resource * square = rf_pen(c, 0.9, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_SQUARE);
  static const double dash[2] = {3.0, -1.5};
  static const double dots[2] = {0.0, -2.0};
  static const double brick[2] = {3.5, -1.0};
  GSHSCENE_Hatch_Family f1[1] = {{45.0, 0.0, 0.0, 0.0, 2.0, 0, NULL}};
  GSHSCENE_Hatch_Family f2[2] = {{45.0, 0.0, 0.0, 0.0, 2.5, 0, NULL}, {135.0, 0.0, 0.0, 0.0, 2.5, 0, NULL}};
  GSHSCENE_Hatch_Family f3[1] = {{0.0, 0.0, 0.5, 0.0, 2.0, 2, dash}};
  GSHSCENE_Hatch_Family f4[1] = {{0.0, 0.0, 0.5, 0.0, 2.0, 2, dots}};
  GSHSCENE_Hatch_Family f5[1] = {{0.0, 0.0, 0.5, 2.25, 2.0, 2, brick}};
  GSHSCENE_Hatch_Family f6[1] = {{0.0, 0.0, 0.0, 0.0, 5.5, 0, NULL}};
  GSHGEOM_Map id = rf_move(0.0, 0.0);
  const GSHSCENE_Resource * h1 = rf_hatch(c, pen, 0, f1, 1, id, rf_solid(0.1, 0.2, 0.6, 1.0));
  const GSHSCENE_Resource * h2 = rf_hatch(c, pen, 0, f2, 2, id, rf_solid(0.7, 0.1, 0.1, 1.0));
  const GSHSCENE_Resource * h3 = rf_hatch(c, square, 0, f3, 1, id, rf_solid(0.1, 0.5, 0.2, 1.0));
  const GSHSCENE_Resource * h4 = rf_hatch(c, round, 0, f4, 1, id, rf_solid(0.4, 0.1, 0.5, 1.0));
  const GSHSCENE_Resource * h5 = rf_hatch(c, pen, 0, f5, 1, id, rf_solid(0.1, 0.1, 0.1, 1.0));
  const GSHSCENE_Resource * h6 = rf_hatch(c, pen, 0, f6, 1, id, rf_solid(0.8, 0.4, 0.0, 1.0));
  const GSHSCENE_Resource * hs[6] = {h1, h2, h3, h4, h5, h6};
  for (int i = 0; i < 6; i++) {
    rf_hatch_panel(c, l, box, 2.0 + 15.5 * (double)(i % 3), 3.0 + 18.0 * (double)(i / 3), hs[i], i == 1 ? 0.55 : 1.0, GSHGEOM_FILL_NONZERO);
  }
  rf_close(c, l);
  return l;
}

/** The nesting styles over three nested squares: the rule nonzero and even-odd, the outermost ring and everything inside the outer loop. */
static inline GSHSCENE_List * rf_hatch_nesting(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.98, 0.98, 1.0, 1.0), 0, 0.0);
  const GSHSCENE_Resource * rings = rf_ring_path(c, 0.0, 0.0, 17.0, 17.0, 3);
  const GSHSCENE_Resource * pen = rf_pen(c, 0.6, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHSCENE_Hatch_Family f[1] = {{0.0, 0.0, 0.25, 0.0, 1.5, 0, NULL}};
  GSHGEOM_Map id = rf_move(0.0, 0.0);
  for (int i = 0; i < 4; i++) {
    const GSHSCENE_Resource * h = rf_hatch(c, pen, (uint32_t)(i < 2 ? 0 : i - 1), f, 1, id, rf_solid(0.1 + 0.2 * i, 0.2, 0.6 - 0.1 * i, 1.0));
    rf_hatch_panel(c, l, rings, 4.0 + 22.0 * (double)(i % 2), 2.0 + 20.0 * (double)(i / 2), h, 1.0,
        i == 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO);
  }
  rf_close(c, l);
  return l;
}

/** A circle with an island, hatched with a hairline and with thick pens of each cap, and a dashed family at the boundary. */
static inline GSHSCENE_List * rf_hatch_curved(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 0.99, 0.95, 1.0), 0, 0.0);
  GSHGEOM_Path * p = rf_path(c);
  gshgeom_path_add_ellipse(p, 7.0, 7.0, 6.5, 6.5, 0, 0);
  gshgeom_path_add_ellipse(p, 7.0, 7.0, 2.5, 2.5, 1, 0);
  const GSHSCENE_Resource * disc = rf_intern(c, p);
  static const double dash[2] = {2.5, -1.0};
  GSHSCENE_Hatch_Family solid_fam[1] = {{60.0, 0.0, 0.0, 0.0, 1.75, 0, NULL}};
  GSHSCENE_Hatch_Family dashed_fam[1] = {{30.0, 0.0, 0.0, 0.0, 1.75, 2, dash}};
  GSHGEOM_Map id = rf_move(0.0, 0.0);
  const GSHSCENE_Resource * pens[6];
  pens[0] = rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
      GSHSCENE_STROKE_DEGENERATE_DRAW, NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO);
  pens[1] = rf_pen(c, 1.1, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  pens[2] = rf_pen(c, 1.1, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_ROUND);
  pens[3] = rf_pen(c, 1.1, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_SQUARE);
  pens[4] = rf_pen(c, 0.9, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_ROUND);
  pens[5] = rf_pen(c, 0.9, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  for (int i = 0; i < 6; i++) {
    const GSHSCENE_Resource * h = rf_hatch(c, pens[i], 0, i >= 4 ? dashed_fam : solid_fam, 1, id, rf_solid(0.1, 0.3 + 0.1 * (double)i, 0.5, 1.0));
    rf_hatch_panel(c, l, disc, 1.0 + 15.5 * (double)(i % 3), 1.0 + 19.0 * (double)(i / 3), h, 1.0, GSHGEOM_FILL_NONZERO);
  }
  rf_close(c, l);
  return l;
}

/** A hatch under a rotation and a non-uniform scale, in an opaque and a translucent group, and in a pattern's tile. */
static inline GSHSCENE_List * rf_hatch_maps(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.97, 1.0, 0.97, 1.0), 0, 0.0);
  const GSHSCENE_Resource * box = rf_rect(c, 0.0, 0.0, 12.0, 9.0);
  const GSHSCENE_Resource * pen = rf_pen(c, 0.5, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_SQUARE);
  static const double dash[2] = {2.0, -1.0};
  GSHSCENE_Hatch_Family f[2] = {{0.0, 0.0, 0.0, 0.0, 2.25, 2, dash}, {90.0, 0.0, 0.0, 0.0, 3.0, 0, NULL}};
  GSHGEOM_Map hm;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &hm);
  const GSHSCENE_Resource * turned = rf_hatch(c, pen, 0, f, 2, hm, rf_solid(0.1, 0.2, 0.6, 1.0));
  /* the hatch map stretches hatch space; the item's map turns the region */
  GSHGEOM_Map stretch;
  gshgeom_map_scale(2, 1.0, 0.6, 1.0, &stretch);
  const GSHSCENE_Resource * stretched = rf_hatch(c, pen, 0, f, 2, stretch, rf_solid(0.6, 0.1, 0.1, 1.0));
  GSHGEOM_Map a, b, rot, mv;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 15.0, &rot);
  mv = rf_move(8.0, 4.0);
  gshgeom_map_compose(&mv, &rot, &a);
  rf_fill(c, l, box, &a, GSHGEOM_FILL_NONZERO, rf_hatch_paint(turned, 1.0), 0, 0.0);
  gshgeom_map_scale(2, 1.6, 0.8, 1.0, &b);
  GSHGEOM_Map mv2 = rf_move(26.0, 3.0);
  GSHGEOM_Map b2;
  gshgeom_map_compose(&mv2, &b, &b2);
  rf_fill(c, l, box, &b2, GSHGEOM_FILL_NONZERO, rf_hatch_paint(stretched, 1.0), 0, 0.0);
  /* a translucent group of two overlapping hatched squares */
  GSHSCENE_Group half = rf_group(0.6, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_group_push(l, &half, 0, 0.0) == GSHSCENE_OK);
  GSHGEOM_Map p1 = rf_move(3.0, 21.0), p2 = rf_move(9.0, 25.0);
  rf_fill(c, l, box, &p1, GSHGEOM_FILL_NONZERO, rf_hatch_paint(turned, 1.0), 0, 0.0);
  rf_fill(c, l, box, &p2, GSHGEOM_FILL_NONZERO, rf_hatch_paint(stretched, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  /* a pattern whose tile is a hatch fill: the tile repeats, and the lines are cut at its cell */
  GSHSCENE_List * t = rf_open(c);
  GSHSCENE_Hatch_Family tf[1] = {{45.0, 0.0, 0.0, 0.0, 2.75, 0, NULL}};
  const GSHSCENE_Resource * thatch = rf_hatch(c, rf_pen(c, 0.6, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), 0, tf, 1,
      rf_move(0.0, 0.0), rf_solid(0.2, 0.2, 0.2, 1.0));
  rf_fill(c, t, rf_rect(c, 0, 0, 5, 5), NULL, GSHGEOM_FILL_NONZERO, rf_hatch_paint(thatch, 1.0), 0, 0.0);
  const GSHSCENE_Resource * pat = rf_pattern(c, rf_picture_of(c, t), 0, 0, 5, 5, 6, 6, 1, 1, rf_move(0.0, 0.0), NULL);
  rf_fill(c, l, rf_rect(c, 28, 22, 46, 38), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/** The closed polygon through the @p n points of @p xy (x, y pairs). */
static inline const GSHSCENE_Resource * rf_polygon(RF_Ctx * c, const double * xy, size_t n) {
  GSHGEOM_Path * p = rf_path(c);
  for (size_t i = 0; i < n; i++) {
    if (i == 0) {
      gshgeom_path_move_to(p, rf_pt(xy[0], xy[1]));
    } else {
      gshgeom_path_line_to(p, rf_pt(xy[2 * i], xy[2 * i + 1]));
    }
  }
  gshgeom_path_close(p);
  return rf_intern(c, p);
}

/** A clip-union member: @p r under @p rule, and under @p map when it is not NULL (the identity otherwise). */
static inline GSHSCENE_Clip_Member rf_member(const GSHSCENE_Resource * r, GSHGEOM_Fill_Rule rule, const GSHGEOM_Map * map) {
  GSHSCENE_Clip_Member m;
  memset(&m, 0, sizeof m);
  m.resource = r;
  m.rule = rule;
  if (map) {
    m.map = *map;
  }
  return m;
}

/** The union of the @p n members, held by @p c. */
static inline const GSHSCENE_Resource * rf_union(RF_Ctx * c, const GSHSCENE_Clip_Member * members, size_t n) {
  const GSHSCENE_Resource * u = NULL;
  rf_check(c, gshscene_store_intern_clip_union(c->store, members, n, &u) == GSHSCENE_OK);
  return rf_hold(c, u);
}

/** The list @p l clips a fill of the rectangle (x0, y0) to (x1, y1) in @p paint to the union @p u under @p map. */
static inline void rf_clipped_fill(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * u, const GSHGEOM_Map * map, double x0, double y0,
    double x1, double y1, GSHSCENE_Paint paint) {
  rf_check(c, gshscene_list_clip_push(l, u, map, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, l, rf_rect(c, x0, y0, x1, y1), NULL, GSHGEOM_FILL_NONZERO, paint, 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
}

static inline GSHSCENE_List * rf_union_overlap(RF_Ctx * c) {
  static const double lit1[] = {27.25, 4.25, 45.5, 4.25, 45.5, 22.5, 27.25, 22.5, 31.5, 8.5, 41.25, 8.5, 41.25, 18.25, 31.5, 18.25};
  static const double lit2[] = {29.75, 16.5, 43.0, 6.25, 38.5, 21.25};
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.97, 0.96, 0.9, 1.0), 0, 0.0);
  /* A star whose centre is wound twice, a blob with curves, and a rectangle that cuts both: one group of three. */
  GSHSCENE_Clip_Member a[3] = {rf_member(rf_star(c, 13.0, 13.0, 10.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_blob(c, 14.25, 4.5, 5.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 6.5, 14.25, 22.75, 20.5), GSHGEOM_FILL_EVEN_ODD, NULL)};
  rf_clipped_fill(c, l, rf_union(c, a, 3), NULL, 0, 0, 24, 24, rf_solid(0.75, 0.1, 0.1, 0.9));
  /* An even-odd polygon that doubles back on itself, and two triangles that cross it. */
  static const double tri[6] = {30.5, 31.25, 44.75, 28.5, 36.25, 38.5};
  const GSHSCENE_Resource * ring = rf_polygon(c, lit1, 8);
  GSHSCENE_Clip_Member b[3] = {rf_member(ring, GSHGEOM_FILL_EVEN_ODD, NULL),
      rf_member(rf_polygon(c, lit2, 3), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_polygon(c, tri, 3), GSHGEOM_FILL_NONZERO, NULL)};
  rf_clipped_fill(c, l, rf_union(c, b, 3), NULL, 24, 0, 48, 26, rf_solid(0.1, 0.3, 0.8, 0.8));
  /* Tilted rectangles whose edges cross at points that are not on the lattice. */
  GSHGEOM_Map t1, t2, m1, m2;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 21.0, &t1);
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, -34.0, &t2);
  GSHGEOM_Map mv1 = rf_move(8.0, 25.0), mv2 = rf_move(19.0, 28.0);
  gshgeom_map_compose(&mv1, &t1, &m1);
  gshgeom_map_compose(&mv2, &t2, &m2);
  const GSHSCENE_Resource * bar = rf_rect(c, 0.0, 0.0, 14.0, 3.75);
  GSHSCENE_Clip_Member d[2] = {rf_member(bar, GSHGEOM_FILL_NONZERO, &m1), rf_member(bar, GSHGEOM_FILL_NONZERO, &m2)};
  rf_clipped_fill(c, l, rf_union(c, d, 2), NULL, 0, 24, 40, 40, rf_solid(0.1, 0.55, 0.2, 1.0));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_union_groups(RF_Ctx * c) {
  static const double lit3[] = {10.0, 12.25, 22.5, 9.0, 16.25, 22.0};
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.92, 0.95, 1.0, 1.0), 0, 0.0);
  /* A tripled contour: it overlaps itself in every pixel its edges cross, and stands apart from the others (its own number, AD-12's). */
  GSHGEOM_Path * p = rf_path(c);
  for (int k = 0; k < 3; k++) {
    gshgeom_path_move_to(p, rf_pt(35.25, 3.25));
    gshgeom_path_line_to(p, rf_pt(44.75, 3.25));
    gshgeom_path_line_to(p, rf_pt(44.75, 12.5));
    gshgeom_path_line_to(p, rf_pt(35.25, 12.5));
    gshgeom_path_close(p);
  }
  const GSHSCENE_Resource * thrice = rf_intern(c, p);
  const double ringxy[16] = {3.5, 3.5, 18.5, 3.5, 18.5, 18.5, 3.5, 18.5, 7.25, 7.25, 14.75, 7.25, 14.75, 14.5, 7.25, 14.5};
  GSHSCENE_Clip_Member m[6] = {rf_member(rf_polygon(c, ringxy, 8), GSHGEOM_FILL_EVEN_ODD, NULL),
      rf_member(rf_polygon(c, lit3, 3), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 24.25, 5.25, 31.75, 9.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 27.5, 3.0, 29.75, 12.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(thrice, GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_star(c, 40.0, 26.0, 8.25), GSHGEOM_FILL_NONZERO, NULL)};
  rf_clipped_fill(c, l, rf_union(c, m, 6), NULL, 0, 0, 48, 40, rf_solid(0.15, 0.2, 0.7, 0.9));
  /* The same union under a map that scales and moves it, drawn again over the first with another color. */
  GSHGEOM_Map sc, mv, both;
  gshgeom_map_scale(2, 0.8, 0.6, 1.0, &sc);
  mv = rf_move(1.5, 20.5);
  gshgeom_map_compose(&mv, &sc, &both);
  rf_clipped_fill(c, l, rf_union(c, m, 6), &both, 0, 20, 48, 40, rf_solid(0.9, 0.5, 0.0, 0.75));
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_union_nested(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 1.0, 1.0), 0, 0.0);
  GSHSCENE_Group g = rf_group(0.7, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 2.25, 2.5, 45.5, 37.75), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  /* Members that each turn about the point they stand at, the item's map turning all of them. */
  const GSHSCENE_Resource * bar = rf_rect(c, -9.0, -2.0, 9.0, 2.0);
  GSHGEOM_Map r0, r1, r2, p0, p1, p2, m0, m1, m2;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 0.0, &r0);
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 60.0, &r1);
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 120.0, &r2);
  p0 = rf_move(14.0, 14.0);
  p1 = rf_move(14.0, 14.0);
  p2 = rf_move(14.0, 14.0);
  gshgeom_map_compose(&p0, &r0, &m0);
  gshgeom_map_compose(&p1, &r1, &m1);
  gshgeom_map_compose(&p2, &r2, &m2);
  GSHSCENE_Clip_Member spokes[3] = {rf_member(bar, GSHGEOM_FILL_NONZERO, &m0), rf_member(bar, GSHGEOM_FILL_NONZERO, &m1),
      rf_member(bar, GSHGEOM_FILL_NONZERO, &m2)};
  GSHGEOM_Map turn, move, placed;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 9.0, &turn);
  move = rf_move(4.0, 3.0);
  gshgeom_map_compose(&move, &turn, &placed);
  rf_clipped_fill(c, l, rf_union(c, spokes, 3), &placed, 0, 0, 48, 40, rf_solid(0.8, 0.1, 0.4, 1.0));
  /* Two members that touch at a quarter pixel and one that overlaps both. */
  GSHSCENE_Clip_Member quarter[3] = {rf_member(rf_rect(c, 26.25, 5.0, 32.25, 15.0), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 32.25, 7.5, 40.5, 17.5), GSHGEOM_FILL_EVEN_ODD, NULL),
      rf_member(rf_rect(c, 30.5, 12.5, 36.75, 24.25), GSHGEOM_FILL_NONZERO, NULL)};
  rf_clipped_fill(c, l, rf_union(c, quarter, 3), NULL, 0, 0, 48, 40, rf_solid(0.1, 0.5, 0.6, 1.0));
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * rf_union_abutting(RF_Ctx * c) {
  static const double lit4[] = {28.0, 3.0, 44.0, 3.0, 28.0, 19.0};
  static const double lit5[] = {44.0, 3.0, 44.0, 19.0, 28.0, 19.0};
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.95, 1.0), 0, 0.0);
  /* Three rectangles that share edges at quarter pixels, and a fourth that shares a corner: disjoint, the fast tier's. */
  GSHSCENE_Clip_Member row[4] = {rf_member(rf_rect(c, 2.0, 2.0, 7.25, 8.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 7.25, 2.0, 13.75, 8.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 13.75, 2.0, 20.5, 8.5), GSHGEOM_FILL_EVEN_ODD, NULL),
      rf_member(rf_rect(c, 20.5, 8.5, 26.0, 14.0), GSHGEOM_FILL_NONZERO, NULL)};
  rf_clipped_fill(c, l, rf_union(c, row, 4), NULL, 0, 0, 48, 20, rf_solid(0.2, 0.2, 0.2, 1.0));
  /* The two triangles of a square share its diagonal: their boxes overlap, so the general tier draws them, and the diagonal is no seam. */
  GSHSCENE_Clip_Member diag[2] = {rf_member(rf_polygon(c, lit4, 3), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_polygon(c, lit5, 3), GSHGEOM_FILL_NONZERO, NULL)};
  rf_clipped_fill(c, l, rf_union(c, diag, 2), NULL, 0, 0, 48, 20, rf_solid(0.7, 0.2, 0.2, 1.0));
  /* The rectangles of the first union again, with a bar across all of them: one group now. */
  GSHSCENE_Clip_Member across[5] = {row[0], row[1], row[2], row[3], rf_member(rf_rect(c, 1.0, 5.0, 24.0, 6.75), GSHGEOM_FILL_NONZERO, NULL)};
  rf_clipped_fill(c, l, rf_union(c, across, 5), NULL, 0, 20, 48, 40, rf_solid(0.2, 0.2, 0.2, 1.0));
  rf_close(c, l);
  return l;
}

/** A tile of a union clip (CU21): the cell (0, 0, 6, 6), a fill of the cell under the union of @p n members, and a fill outside the clip. */
static inline const GSHSCENE_Resource * rf_tile_union(RF_Ctx * c, const GSHSCENE_Clip_Member * members, size_t n, GSHSCENE_Paint under) {
  GSHSCENE_List * t = rf_open(c);
  rf_fill(c, t, rf_rect(c, 0, 0, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.85, 0.9, 0.8, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_push(t, rf_union(c, members, n), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, t, rf_rect(c, -1, -1, 7, 7), NULL, GSHGEOM_FILL_NONZERO, under, 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(t) == GSHSCENE_OK);
  return rf_picture_of(c, t);
}

static inline GSHSCENE_List * rf_pattern_union(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 0, 0, 48, 40), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.93, 0.9, 1.0), 0, 0.0);
  /* Members that cross the cell edges: each copy's union is cut to its cell, and the copies' areas add at a seam. */
  GSHSCENE_Clip_Member spill[3] = {rf_member(rf_rect(c, -0.75, 0.75, 3.5, 2.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 2.25, -1.0, 6.75, 3.25), GSHGEOM_FILL_NONZERO, NULL), rf_member(rf_rect(c, 1.0, 3.5, 5.5, 5.25), GSHGEOM_FILL_NONZERO, NULL)};
  const GSHSCENE_Resource * crossing = rf_pattern(c, rf_tile_union(c, spill, 3, rf_solid(0.75, 0.15, 0.2, 0.9)), 0, 0, 6, 6, 6, 6, 1, 1,
      rf_move(0.3, 0.45), NULL);
  rf_fill(c, l, rf_rect(c, 1, 1, 23, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(crossing, 1.0), 0, 0.0);
  /* A star under even-odd, a blob and a turned bar, on cells that overlap (PC6). */
  GSHGEOM_Map turn, to, at;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &turn);
  to = rf_move(3.0, 3.0);
  gshgeom_map_compose(&to, &turn, &at);
  GSHSCENE_Clip_Member curved[3] = {rf_member(rf_star(c, 3.0, 3.0, 2.8), GSHGEOM_FILL_EVEN_ODD, NULL),
      rf_member(rf_blob(c, 0.75, 0.75, 1.6), GSHGEOM_FILL_NONZERO, NULL), rf_member(rf_rect(c, -2.5, -0.4, 2.5, 0.4), GSHGEOM_FILL_NONZERO, &at)};
  const GSHSCENE_Resource * overlapping = rf_pattern(c, rf_tile_union(c, curved, 3, rf_solid(0.1, 0.35, 0.8, 0.85)), 0, 0, 6, 6, 4.5, 5.0, 1, 1,
      rf_move(0.0, 0.25), NULL);
  rf_fill(c, l, rf_rect(c, 25, 1, 47, 19), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(overlapping, 1.0), 0, 0.0);
  /* A union inside a clip, on a gapped lattice under a skew. */
  GSHSCENE_Clip_Member pair[2] = {rf_member(rf_rect(c, 0.5, 0.5, 4.0, 3.5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 2.5, 2.5, 5.5, 5.5), GSHGEOM_FILL_NONZERO, NULL)};
  GSHSCENE_List * inner = rf_open(c);
  GSHGEOM_Map mid = rf_move(3.0, 3.0);
  rf_check(c, gshscene_list_clip_push(inner, rf_circle(c, 3.4), &mid, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_push(inner, rf_union(c, pair, 2), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill(c, inner, rf_rect(c, 0, 0, 6, 6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.6, 0.3, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(inner) == GSHSCENE_OK);
  rf_check(c, gshscene_list_clip_pop(inner) == GSHSCENE_OK);
  const GSHSCENE_Resource * gapped = rf_pattern(c, rf_picture_of(c, inner), 0, 0, 6, 6, 8.25, 7.5, 1, 1, rf_affine(c, 1.0, 0.0, 0.35, 1.0, 0.1, 0.2), NULL);
  rf_fill(c, l, rf_rect(c, 1, 21, 23, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(gapped, 1.0), 0, 0.0);
  /* A gradient under a union on whole-pixel steps. */
  GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 0.9, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
  const GSHSCENE_Resource * grad = rf_gradient(c, rf_linear(0, 0, 6, 6, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
  GSHSCENE_Clip_Member trio[3] = {rf_member(rf_rect(c, 0, 0, 3, 3), GSHGEOM_FILL_NONZERO, NULL), rf_member(rf_rect(c, 2, 2, 6, 5), GSHGEOM_FILL_NONZERO, NULL),
      rf_member(rf_rect(c, 4.5, 0.5, 6, 2), GSHGEOM_FILL_NONZERO, NULL)};
  const GSHSCENE_Resource * whole = rf_pattern(c, rf_tile_union(c, trio, 3, rf_gradient_paint(grad, 1.0)), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(2.0, 1.0), NULL);
  rf_fill(c, l, rf_rect(c, 25, 21, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(whole, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

typedef struct RF_Fixture {
  const char * name;
  RF_Build build;
} RF_Fixture;

/* --- coverage by the centers of the pixels (shape-scene's list.h, AA1 to AA6) ------------------------------------------------------ */

/** A fill that asks for no antialiasing. */
static inline void rf_fill_crisp(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * r, const GSHGEOM_Map * map,
    GSHGEOM_Fill_Rule rule, GSHSCENE_Paint paint, int32_t layer, double depth) {
  rf_check(c, gshscene_list_fill_antialias(l, r, map, rule, &paint, GSHSCENE_ANTIALIAS_NONE, layer, depth) == GSHSCENE_OK);
}

/** A stroke that asks for no antialiasing. */
static inline void rf_stroke_crisp(RF_Ctx * c, GSHSCENE_List * l, const GSHSCENE_Resource * path, const GSHSCENE_Resource * style,
    const GSHGEOM_Map * map, GSHSCENE_Paint paint) {
  rf_check(c, gshscene_list_stroke_antialias(l, path, style, map, &paint, GSHSCENE_ANTIALIAS_NONE, 0, 0.0) == GSHSCENE_OK);
}

/**
 * Fills that ask for no antialiasing beside ones that do not: the star of `fill-rules` under both rules, rectangles on whole pixels and
 * on the centers of pixels (the left and top edges in, the right and bottom out), two that share an edge at half pixels in translucent
 * paint (no pixel twice, none missed), a blob in a clip and in an opacity group, a glyph run, and a gradient, a pattern and a hatch drawn
 * through the coverage.
 */
static inline GSHSCENE_List * rf_antialias_fills(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 1, 1, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.92, 0.9, 0.8, 1.0), 0, 0.0);
  const GSHSCENE_Resource * star = rf_star(c, 9.0, 9.0, 7.5);
  GSHGEOM_Map move = rf_move(15.5, 0.25);
  rf_fill_crisp(c, l, star, NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.2, 1.0), 0, 0.0);
  rf_fill_crisp(c, l, star, &move, GSHGEOM_FILL_EVEN_ODD, rf_solid(0.1, 0.2, 0.9, 0.7), 0, 0.0);
  GSHGEOM_Map beside = rf_move(32.0, 0.5);
  rf_fill(c, l, star, &beside, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.5, 0.2, 1.0), 0, 0.0);
  rf_fill_crisp(c, l, rf_rect(c, 2, 18, 8, 24), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.0, 0.0, 0.0, 1.0), 0, 0.0);
  rf_fill_crisp(c, l, rf_rect(c, 9.5, 18.5, 15.5, 24.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.0, 0.0, 0.0, 1.0), 0, 0.0);
  rf_fill_crisp(c, l, rf_rect(c, 17.5, 18.5, 21.5, 24.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.1, 0.5), 0, 0.0);
  rf_fill_crisp(c, l, rf_rect(c, 21.5, 18.5, 25.5, 24.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.2, 0.1, 0.5), 0, 0.0);
  rf_fill_crisp(c, l, rf_glyph(c, 0), &move, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.1, 0.1, 1.0), 0, 0.0);
  /* a crisp blob in a clip, and in an opacity group */
  rf_check(c, gshscene_list_clip_push(l, rf_rect(c, 28, 17, 40, 31), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
  rf_fill_crisp(c, l, rf_blob(c, 27.0, 17.5, 6.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.2, 0.8, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
  GSHSCENE_Group g = rf_group(0.6, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  rf_fill_crisp(c, l, rf_blob(c, 3.0, 28.0, 5.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.6, 0.2, 1.0), 0, 0.0);
  rf_fill_crisp(c, l, rf_blob(c, 7.5, 30.5, 5.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.5, 0.1, 1.0), 0, 0.0);
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  /* a gradient, a pattern and a hatch through the coverage */
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
  rf_fill_crisp(c, l, rf_star(c, 40.0, 24.0, 6.5), NULL, GSHGEOM_FILL_NONZERO,
      rf_gradient_paint(rf_gradient(c, rf_linear(34, 0, 46, 0, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0), 0, 0.0);
  const GSHSCENE_Resource * pat = rf_pattern(c, rf_tile_basic(c), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.3, 0.45), NULL);
  rf_fill_crisp(c, l, rf_blob(c, 17.0, 29.5, 5.5), NULL, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
  const GSHSCENE_Resource * pen = rf_pen(c, 0.7, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHSCENE_Hatch_Family fam[1] = {{45.0, 0.0, 0.0, 0.0, 2.0, 0, NULL}};
  const GSHSCENE_Resource * hatch = rf_hatch(c, pen, 0, fam, 1, rf_move(0.0, 0.0), rf_solid(0.1, 0.2, 0.6, 1.0));
  GSHGEOM_Map hat_at = rf_move(31.0, 31.5);
  rf_fill_crisp(c, l, rf_rect(c, 0.0, 0.0, 14.0, 7.0), &hat_at, GSHGEOM_FILL_NONZERO, rf_hatch_paint(hatch, 1.0), 0, 0.0);
  rf_close(c, l);
  return l;
}

/**
 * Strokes that ask for no antialiasing: each join and cap, dashes, a hairline and a device width, an elliptical pen, a closed shape in each
 * alignment, a stroke painted with a gradient, and a stroke of a run, beside a stroke that does not.
 */
static inline GSHSCENE_List * rf_antialias_strokes(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  rf_fill(c, l, rf_rect(c, 1, 1, 47, 39), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.95, 0.95, 0.9, 1.0), 0, 0.0);
  static const double zig[] = {3.0, 9.0, 8.0, 3.0, 13.0, 9.0, 18.0, 3.0};
  const uint32_t joins[3] = {GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_JOIN_BEVEL};
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map at = rf_move(0.0, 8.0 * (double)i);
    rf_stroke_crisp(c, l, rf_polyline(c, zig, 4, 0), rf_pen(c, 2.25, joins[i], i == 1 ? GSHSCENE_STROKE_CAP_ROUND : GSHSCENE_STROKE_CAP_SQUARE),
        &at, rf_solid(0.1, 0.1, 0.5, 1.0));
  }
  GSHGEOM_Map plain = rf_move(0.0, 24.0);
  rf_stroke(c, l, rf_polyline(c, zig, 4, 0), rf_pen(c, 2.25, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_SQUARE), &plain,
      rf_solid(0.1, 0.1, 0.5, 1.0));
  /* dashes, a hairline and a device width */
  static const double dash[2] = {3.0, 2.0};
  static const double line[] = {21.0, 4.5, 45.0, 4.5};
  rf_stroke_crisp(c, l, rf_polyline(c, line, 2, 0),
      rf_style(c, 1.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW,
          dash, 2, 0.5, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      NULL, rf_solid(0.5, 0.1, 0.1, 1.0));
  static const double line2[] = {21.0, 9.25, 45.0, 11.75};
  rf_stroke_crisp(c, l, rf_polyline(c, line2, 2, 0),
      rf_style(c, 1.0, GSHSCENE_STROKE_WIDTH_DEVICE, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW,
          NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO),
      NULL, rf_solid(0.1, 0.4, 0.1, 1.0));
  GSHGEOM_Map squash = rf_affine(c, 1.0, 0.0, 0.0, 0.4, 0.0, 14.0);
  rf_stroke_crisp(c, l, rf_circle(c, 5.0), rf_pen(c, 2.0, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT), &squash, rf_solid(0.4, 0.1, 0.5, 1.0));
  /* a closed square in each alignment */
  static const double box[] = {2.0, 2.0, 12.0, 2.0, 12.0, 11.0, 2.0, 11.0};
  const uint32_t aligns[3] = {GSHSCENE_STROKE_ALIGN_CENTER, GSHSCENE_STROKE_ALIGN_INSIDE, GSHSCENE_STROKE_ALIGN_OUTSIDE};
  for (int i = 0; i < 3; i++) {
    GSHGEOM_Map at = rf_move(3.0 + 15.0 * (double)i, 27.0);
    rf_stroke_crisp(c, l, rf_polyline(c, box, 4, 1),
        rf_style(c, 2.5, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW,
            NULL, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, aligns[i], GSHGEOM_FILL_NONZERO),
        &at, rf_solid(0.1 + 0.3 * (double)i, 0.3, 0.6, 0.8));
  }
  /* a gradient across a crisp stroke, and a crisp stroke of a run's outlines */
  const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
  rf_stroke_crisp(c, l, rf_star(c, 40.0, 19.0, 5.5), rf_pen(c, 1.25, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT), NULL,
      rf_gradient_paint(rf_gradient(c, rf_linear(34, 0, 46, 0, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0));
  GSHGEOM_Map glyph_at = rf_move(25.0, 22.0);
  rf_stroke_crisp(c, l, rf_glyph(c, 0), rf_pen(c, 1.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &glyph_at, rf_solid(0.1, 0.1, 0.1, 1.0));
  rf_close(c, l);
  return l;
}

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
    {"gradient-linear", rf_gradient_linear},
    {"gradient-radial", rf_gradient_radial},
    {"gradient-sweep", rf_gradient_sweep},
    {"gradient-paints", rf_gradient_paints},
    {"blend-separable", rf_blend_separable},
    {"blend-non-separable", rf_blend_non_separable},
    {"blend-out-of-range", rf_blend_out_of_range},
    {"strokes-joins", rf_strokes_joins},
    {"strokes-caps", rf_strokes_caps},
    {"strokes-widths", rf_strokes_widths},
    {"strokes-align", rf_strokes_align},
    {"strokes-paints", rf_strokes_paints},
    {"strokes-groups", rf_strokes_groups},
    {"strokes-sparse", rf_strokes_sparse},
    {"strokes-nothing", rf_strokes_nothing},
    {"strokes-dash-overlap", rf_strokes_dash_overlap},
    {"strokes-tight-joins", rf_strokes_tight_joins},
    {"strokes-rings", rf_strokes_rings},
    {"strokes-thin-aligned", rf_strokes_thin_aligned},
    {"strokes-text", rf_strokes_text},
    {"strokes-text-outlines", rf_strokes_text_outlines},
    {"image-filters", rf_image_filters},
    {"image-edges", rf_image_edges},
    {"image-formats", rf_image_formats},
    {"image-spaces", rf_image_spaces},
    {"image-compose", rf_image_compose},
    {"image-nothing", rf_image_nothing},
    {"mask-alpha", rf_mask_alpha},
    {"mask-luminance", rf_mask_luminance},
    {"mask-transfers", rf_mask_transfers},
    {"mask-outside", rf_mask_outside},
    {"mask-acts-on", rf_mask_acts_on},
    {"mask-nested", rf_mask_nested},
    {"mask-frames", rf_mask_frames},
    {"mask-compose", rf_mask_compose},
    {"mask-not-a-clip", rf_mask_not_a_clip},
    {"group-non-isolated", rf_group_non_isolated},
    {"group-knockout", rf_group_knockout},
    {"group-nested", rf_group_nested},
    {"group-spaces", rf_group_spaces},
    {"group-operators", rf_group_operators},
    {"group-elements", rf_group_elements},
    {"group-masked", rf_group_masked},
    {"pattern-lattices", rf_pattern_lattices},
    {"pattern-overlap", rf_pattern_overlap},
    {"pattern-content", rf_pattern_content},
    {"pattern-strokes", rf_pattern_strokes},
    {"pattern-integer", rf_pattern_integer},
    {"pattern-period", rf_pattern_period},
    {"pattern-period-blend", rf_pattern_period_blend},
    {"pattern-hatch", rf_pattern_hatch},
    {"hatch-families", rf_hatch_families},
    {"hatch-nesting", rf_hatch_nesting},
    {"hatch-curved", rf_hatch_curved},
    {"hatch-maps", rf_hatch_maps},
    {"union-overlap", rf_union_overlap},
    {"union-groups", rf_union_groups},
    {"union-nested", rf_union_nested},
    {"union-abutting", rf_union_abutting},
    {"pattern-union", rf_pattern_union},
    {"antialias-fills", rf_antialias_fills},
    {"antialias-strokes", rf_antialias_strokes},
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
 * Render fixture @p index on a fresh raster of @p format with a fresh sink (of @p limits, or the defaults) and give the render's hash.
 *
 * @return The render's result (shape-scene's), or -1 when the fixture could not be built.
 */
static inline int rf_render_with(size_t index, GSHCPU_Format format, const GSHCPU_Limits * limits, uint64_t * hash,
    GIMG_Raster ** keep) {
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * list = RF_FIXTURES[index].build(&c);
  GIMG_Raster * raster = rf_raster(format, RF_WIDTH, RF_HEIGHT);
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  int result = -1;
  if (!c.failed && list && raster && gshcpu_sink_create(NULL, NULL, limits, &sink) == GSHCPU_OK &&
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

/** rf_render with the sink's default limits. */
static inline int rf_render(size_t index, GSHCPU_Format format, uint64_t * hash, GIMG_Raster ** keep) {
  return rf_render_with(index, format, NULL, hash, keep);
}

#endif /* GHOTI_IO_GSHCPU_TEST_RENDER_FIXTURES_H */
