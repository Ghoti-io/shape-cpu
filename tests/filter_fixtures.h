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
 * The filter fixture set: the scenes whose renders are the committed filter goldens (tests/corpus/filter.hash and
 * tests/corpus/filter-fast.hash), drawn through the sink on each of the four formats by the cross-target probe
 * (tools/corpus-hash.c) and the unit tests.  C, so that both build the same lists, and built on the pieces of
 * tests/render_fixtures.h (the context, the raster, the render).
 *
 * Every scene is a grid of cells of 12 by 10 on the 48 by 40 raster; a cell is one group around a small source graphic (a star and a
 * rectangle, a blob and a bar, or overlapping bars, all partly transparent so that the premultiplied arithmetic has work to do) whose
 * filter is the point of the scene.  Cells are independent: a cell's filter region is the cell (or a part of it, so that an edge mode
 * has an edge to act at), and a cell with an `AUTO` region reaches into its neighbors by what its primitives reach.  The scenes cover
 * every primitive kind and every mode of each:
 *
 * | Scene | What it holds |
 * | --- | --- |
 * | filter-basic | offset (whole, fractional, negative, past the region), flood (target, linear and Display P3 colors; opacities; subregions), merge of two to four inputs, subregions, `AUTO` regions |
 * | filter-blend-linear, filter-blend-srgb | each of the sixteen blend modes of the standard, in `linearRGB` and in `sRGB` |
 * | filter-composite | over, in, out, atop, xor in both spaces, and six arithmetic sets (clamped, with bias, a difference with an offset that leaves a color above its alpha) |
 * | filter-color-matrix | four general matrices (swap, alpha row, mix, out of range), saturate at 0, 0.5, 1 and 3, hue rotation at six angles (one negative), luminance to alpha, in both spaces |
 * | filter-transfer | every function (identity, table, discrete, linear, gamma) on every channel, tables of one to four entries, a gamma of exponent 0 |
 * | filter-blur | the true Gaussian below a deviation of 2 and the box approximation at and above it (an even and an odd box), anisotropic and a zero axis, and each edge mode on a region that cuts the shape |
 * | filter-morphology | erode and dilate at eight radii (zero, fractional, one axis, unequal) |
 * | filter-convolve | six kernels (box, sharpen, emboss, 1 by 3, 4 by 2, a negative-target Sobel) with every edge mode, with and without preserving alpha, a bias, a divisor |
 * | filter-tile-image-displace | tile of four subregions (two fractional), image under nearest, linear, area, cubic filtering, turned and skewed, and the displacement map over each pair of channels at both signs |
 * | filter-turbulence | turbulence and fractal noise, with and without stitching, at one to five octaves, eight base frequencies and seeds (one fractional), with and without a subregion |
 * | filter-lighting | diffuse and specular lighting under a distant, a point, a spot and a spot with a cone, two surface scales, constants and colors |
 * | filter-shadow | drop shadow at four offsets and four deviations, four colors (one in Display P3) and opacities |
 * | filter-graphs | chains of three to five primitives that use results by index, per-primitive color spaces, source alpha, a group opacity, `AUTO` and fractional regions |
 * | filter-map-rotate, filter-map-skew, filter-map-scale, filter-map-quarter, filter-map-mirror | twelve primitives under a target map that turns by 30 degrees, shears, scales unevenly, turns by a quarter and mirrors: the canonical-space route and the axis-aligned one |
 * | filter-picture | filtered groups inside pictures, instanced under a turn, a scale and a shear |
 * | filter-slot, filter-slot-rotated | twelve filters whose `IMAGE` primitives draw slots of the filter's picture (FL25): one slot under a translation, a fractional one, a turn, a scale with a shear; two and three slots merged; a slot fed to a blur and to a composite with the source graphic; a region that cuts the slot; an element with a group and an element with a filtered group inside; one slot drawn by two primitives; a group opacity; and the same under a turned target map |
 * | filter-backdrop, filter-backdrop-rotated | twelve backdrop-filter groups (FL22) over a drawn backdrop, under a clip with fractional edges, with opacity, a blend space of their own and unfiltered content over the result; and the same under a turned target map |
 */

#ifndef GHOTI_IO_GSHCPU_TEST_FILTER_FIXTURES_H
#define GHOTI_IO_GSHCPU_TEST_FILTER_FIXTURES_H

#include "render_fixtures.h"

#include <ghoti.io/shape-scene/filter.h>

#include <stdarg.h>
#include <stdlib.h>

#define FF_MAX_PRIMS 8u

/** What a primitive's input names, by short names. */
#define FF_PREV ((uint32_t)GSHSCENE_FILTER_INPUT_PREVIOUS)
#define FF_SRC ((uint32_t)GSHSCENE_FILTER_INPUT_SOURCE_GRAPHIC)
#define FF_ALPHA ((uint32_t)GSHSCENE_FILTER_INPUT_SOURCE_ALPHA)
#define FF_RES ((uint32_t)GSHSCENE_FILTER_INPUT_RESULT)

#define FF_LINEAR ((uint32_t)GSHSCENE_FILTER_SPACE_LINEAR_RGB)
#define FF_SRGB ((uint32_t)GSHSCENE_FILTER_SPACE_SRGB)

/** A primitive with the storage of its inputs. */
typedef struct FF_Prim {
  GSHSCENE_Filter_Primitive p;
  GSHSCENE_Filter_Input in[4];
} FF_Prim;

/** One cell's filter and group: what a maker fills. */
typedef struct FF_Spec {
  FF_Prim prims[FF_MAX_PRIMS];
  size_t n;
  int automatic;       /* the region is `AUTO` */
  double region[4];    /* x, y, width, height: the cell, until the maker says otherwise */
  uint32_t space;      /* the filter's color space */
  double opacity;      /* the group's */
  uint32_t variant;    /* the source graphic */
  uint32_t mode;       /* the group's filter mode */
  int content;         /* draw the cell's source graphic in the group (backdrop scenes: a mark of the group's own) */
  const GSHSCENE_Resource * picture; /* the filter's picture, whose instances the `IMAGE` primitives that use a slot draw (FL25); NULL for none */
} FF_Spec;

/** A maker fills @p s for cell @p i whose origin is (@p ox, @p oy). */
typedef void (*FF_Maker)(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s);

static inline void ff_begin(FF_Spec * s, unsigned i, double ox, double oy) {
  memset(s, 0, sizeof *s);
  s->region[0] = ox;
  s->region[1] = oy;
  s->region[2] = 12.0;
  s->region[3] = 10.0;
  s->space = FF_LINEAR;
  s->opacity = 1.0;
  s->variant = i % 3u;
  s->content = 1;
}

/** Add a primitive of @p kind with the inputs its kind wants (the previous result for one input; none; two placed by the caller). */
static inline FF_Prim * ff_add(FF_Spec * s, uint32_t kind) {
  FF_Prim * q = &s->prims[s->n++];
  memset(q, 0, sizeof *q);
  q->p.kind = kind;
  switch (kind) {
    case GSHSCENE_FILTER_FLOOD:
    case GSHSCENE_FILTER_TURBULENCE:
    case GSHSCENE_FILTER_IMAGE:
      q->p.input_count = 0;
      break;
    case GSHSCENE_FILTER_BLEND:
    case GSHSCENE_FILTER_COMPOSITE:
    case GSHSCENE_FILTER_DISPLACEMENT_MAP:
      q->p.input_count = 2;
      q->in[0].kind = FF_PREV;
      q->in[1].kind = FF_PREV;
      break;
    default:
      q->p.input_count = 1;
      q->in[0].kind = FF_PREV;
      break;
  }
  return q;
}

/** Name the inputs: @p n pairs of (kind, index), given as `kind0, index0, kind1, index1, ...` (all `uint32_t`). */
static inline void ff_ins(FF_Prim * q, size_t n, ...) {
  va_list ap;
  va_start(ap, n);
  q->p.input_count = n;
  for (size_t k = 0; k < n; k++) {
    q->in[k].kind = va_arg(ap, uint32_t);
    q->in[k].index = va_arg(ap, uint32_t);
  }
  va_end(ap);
}

static inline void ff_in1(FF_Prim * q, uint32_t kind, uint32_t index) {
  q->in[0].kind = kind;
  q->in[0].index = index;
}

static inline void ff_in2(FF_Prim * q, uint32_t k0, uint32_t i0, uint32_t k1, uint32_t i1) {
  q->in[0].kind = k0;
  q->in[0].index = i0;
  q->in[1].kind = k1;
  q->in[1].index = i1;
}

/** Give the primitive a subregion: bit 0 x, bit 1 y, bit 2 width, bit 3 height. */
static inline void ff_sub(FF_Prim * q, uint32_t mask, double x, double y, double w, double h) {
  q->p.subregion_mask = mask;
  q->p.subregion[0] = (mask & 1u) ? x : 0.0;
  q->p.subregion[1] = (mask & 2u) ? y : 0.0;
  q->p.subregion[2] = (mask & 4u) ? w : 0.0;
  q->p.subregion[3] = (mask & 8u) ? h : 0.0;
}

static inline void ff_color(FF_Prim * q, double r, double g, double b, double a) {
  q->p.color.rgba[0] = r;
  q->p.color.rgba[1] = g;
  q->p.color.rgba[2] = b;
  q->p.color.rgba[3] = a;
}

static inline void ff_color_in(FF_Prim * q, GCOL_Primaries primaries, GCOL_Transfer transfer) {
  q->p.color.space.primaries = primaries;
  q->p.color.space.transfer = transfer;
}

static inline FF_Prim * ff_blur(FF_Spec * s, double sx, double sy, uint32_t edge) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_GAUSSIAN_BLUR);
  q->p.sx = sx;
  q->p.sy = sy;
  q->p.edge_mode = edge;
  return q;
}

static inline FF_Prim * ff_offset(FF_Spec * s, double dx, double dy) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_OFFSET);
  q->p.dx = dx;
  q->p.dy = dy;
  return q;
}

static inline FF_Prim * ff_flood(FF_Spec * s, double r, double g, double b, double a, double opacity) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_FLOOD);
  ff_color(q, r, g, b, a);
  q->p.opacity = opacity;
  return q;
}

static inline FF_Prim * ff_matrix(FF_Spec * s, uint32_t type, const double * values, size_t count) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_COLOR_MATRIX);
  q->p.mode = type;
  q->p.value_count = count;
  q->p.values = count ? values : NULL;
  return q;
}

static inline FF_Prim * ff_composite(FF_Spec * s, uint32_t op) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_COMPOSITE);
  q->p.mode = op;
  return q;
}

static inline FF_Prim * ff_turbulence(FF_Spec * s, uint32_t noise, double fx, double fy, uint32_t octaves, double seed, uint32_t stitch) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_TURBULENCE);
  q->p.mode = noise;
  q->p.sx = fx;
  q->p.sy = fy;
  q->p.octaves = octaves;
  q->p.seed = seed;
  q->p.stitch_tiles = stitch;
  return q;
}

/** Intern the filter of @p s. */
static inline const GSHSCENE_Resource * ff_intern(RF_Ctx * c, const FF_Spec * s) {
  GSHSCENE_Filter_Primitive prims[FF_MAX_PRIMS];
  for (size_t i = 0; i < s->n; i++) {
    prims[i] = s->prims[i].p;
    prims[i].inputs = s->prims[i].p.input_count ? s->prims[i].in : NULL;
  }
  GSHSCENE_Filter f;
  memset(&f, 0, sizeof f);
  f.region_mode = s->automatic ? (uint32_t)GSHSCENE_FILTER_REGION_AUTO : (uint32_t)GSHSCENE_FILTER_REGION_EXPLICIT;
  if (!s->automatic) {
    for (int k = 0; k < 4; k++) {
      f.region[k] = s->region[k];
    }
  }
  f.color_space = s->space;
  f.primitive_count = s->n;
  f.primitives = prims;
  f.picture = s->picture;
  const GSHSCENE_Resource * r = NULL;
  rf_check(c, gshscene_store_intern_filter(c->store, &f, &r) == GSHSCENE_OK);
  return rf_hold(c, r);
}

/** The cell's source graphic: three variants, drawn partly transparent. */
static inline void ff_content(RF_Ctx * c, GSHSCENE_List * l, double ox, double oy, uint32_t variant) {
  if (variant == 0u) {
    rf_fill(c, l, rf_star(c, ox + 6.0, oy + 5.0, 4.6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.25, 0.1, 0.85), 0, 0.0);
    rf_fill(c, l, rf_rect(c, ox + 5.5, oy + 4.25, ox + 10.25, oy + 8.75), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.55, 0.9, 0.6), 0, 0.0);
  } else if (variant == 1u) {
    rf_fill(c, l, rf_blob(c, ox + 1.5, oy + 2.25, 2.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.8, 0.3, 1.0), 0, 0.0);
    rf_fill(c, l, rf_rect(c, ox + 7.25, oy + 1.5, ox + 10.5, oy + 9.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.8, 0.1, 0.5), 0, 0.0);
  } else {
    rf_fill(c, l, rf_rect(c, ox + 1.5, oy + 1.25, ox + 6.5, oy + 5.75), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.8, 0.1, 0.6, 1.0), 0, 0.0);
    rf_fill(c, l, rf_rect(c, ox + 4.75, oy + 4.5, ox + 10.75, oy + 8.5), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.3, 0.7, 0.7), 0, 0.0);
    rf_fill(c, l, rf_rect(c, ox + 8.25, oy + 0.75, ox + 9.25, oy + 9.25), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 1.0, 0.9), 0, 0.0);
  }
}

/** Open the group of one cell (the filter, the opacity and the mode of @p s) and draw its source graphic. */
static inline void ff_group(RF_Ctx * c, GSHSCENE_List * l, const FF_Spec * s, double ox, double oy) {
  GSHSCENE_Group g = rf_group(s->opacity, GSHSCENE_COMPOSITE_SRC_OVER);
  g.filter = ff_intern(c, s);
  g.filter_mode = s->mode;
  rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
  if (s->content) {
    ff_content(c, l, ox, oy, s->variant);
  }
  rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
}

/** The origin of cell @p i of a grid four across. */
static inline double ff_ox(unsigned i) {
  return 12.0 * (double)(i % 4u);
}

static inline double ff_oy(unsigned i) {
  return 10.0 * (double)(i / 4u);
}

/** A list of @p cells cells, each made by @p make. */
static inline GSHSCENE_List * ff_grid(RF_Ctx * c, FF_Maker make, unsigned cells) {
  GSHSCENE_List * l = rf_open(c);
  for (unsigned i = 0; i < cells; i++) {
    FF_Spec * s = (FF_Spec *)malloc(sizeof *s);
    if (!s) {
      c->failed = 1;
      break;
    }
    ff_begin(s, i, ff_ox(i), ff_oy(i));
    make(c, i, ff_ox(i), ff_oy(i), s);
    ff_group(c, l, s, ff_ox(i), ff_oy(i));
    free(s);
  }
  rf_close(c, l);
  return l;
}

static const uint32_t ff_spaces[2] = {FF_LINEAR, FF_SRGB};

/** A region inside the cell that cuts through the shape, so that an edge has something to do. */
static inline void ff_tight(FF_Spec * s, double ox, double oy) {
  s->region[0] = ox + 3.25;
  s->region[1] = oy + 2.5;
  s->region[2] = 6.5;
  s->region[3] = 5.25;
}

/* --- basic: offset, flood, merge, subregions ---------------------------------------------------- */

static inline void ff_m_basic(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double odx[4] = {3.0, -2.5, 0.5, 15.0};
  static const double ody[4] = {2.0, 1.25, -0.5, 0.0};
  (void)c;
  FF_Prim * p;
  switch (i) {
    case 0: case 1: case 2: case 3:
      ff_offset(s, odx[i], ody[i]);
      if (i == 2u) {
        s->space = FF_SRGB;
      }
      break;
    case 4:
      ff_flood(s, 0.9, 0.4, 0.1, 0.8, 0.7);
      break;
    case 5:
      p = ff_flood(s, 0.5, 0.5, 0.5, 1.0, 1.0);
      ff_color_in(p, GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR);
      s->space = FF_SRGB;
      break;
    case 6:
      p = ff_flood(s, 0.2, 0.8, 0.4, 1.0, 0.5);
      ff_color_in(p, GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB);
      ff_sub(p, 15u, ox + 2.0, oy + 1.5, 8.0, 6.25);
      break;
    case 7:
      p = ff_flood(s, 0.8, 0.1, 0.5, 0.9, 1.0);
      ff_sub(p, 4u, 0.0, 0.0, 6.0, 0.0);
      break;
    case 8:
      ff_offset(s, 2.0, 1.0);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 2, FF_SRC, 0, FF_RES, 0);
      break;
    case 9:
      ff_flood(s, 0.1, 0.9, 0.5, 0.5, 1.0);
      p = ff_offset(s, 2.0, 2.0);
      ff_in1(p, FF_SRC, 0);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 3, FF_RES, 0, FF_ALPHA, 0, FF_RES, 1);
      break;
    case 10:
      ff_offset(s, 2.0, 0.0);
      ff_offset(s, 2.0, 0.0);
      ff_offset(s, 2.0, 0.0);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 4, FF_RES, 2, FF_RES, 1, FF_RES, 0, FF_SRC, 0);
      p->p.color_space = FF_SRGB;
      break;
    case 11:
      ff_offset(s, 1.5, 1.5);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 2, FF_PREV, 0, FF_SRC, 0);
      s->space = FF_SRGB;
      break;
    case 12:
      p = ff_offset(s, 1.0, 1.0);
      ff_sub(p, 3u, ox + 1.0, oy + 1.0, 0.0, 0.0);
      break;
    case 13:
      p = ff_offset(s, 2.0, 2.0);
      ff_in1(p, FF_ALPHA, 0);
      break;
    case 14:
      ff_offset(s, 3.0, 2.0);
      s->automatic = 1;
      break;
    default:
      p = ff_flood(s, 0.3, 0.6, 0.9, 0.3, 1.0);
      ff_sub(p, 8u, 0.0, 0.0, 0.0, 5.0);
      break;
  }
}

/* --- blend: the sixteen modes ------------------------------------------------------------------- */

static inline void ff_blend_cell(unsigned i, FF_Spec * s, uint32_t space) {
  s->space = space;
  ff_offset(s, 2.0, 1.5);
  FF_Prim * p = ff_add(s, GSHSCENE_FILTER_BLEND);
  p->p.mode = i == 0u ? (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER : (uint32_t)GSHSCENE_COMPOSITE_SCREEN + (i - 1u);
  ff_in2(p, FF_SRC, 0, FF_RES, 0);
}

static inline void ff_m_blend_linear(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  (void)c; (void)ox; (void)oy;
  ff_blend_cell(i, s, FF_LINEAR);
}

static inline void ff_m_blend_srgb(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  (void)c; (void)ox; (void)oy;
  ff_blend_cell(i, s, FF_SRGB);
}

/* --- composite: five operators in two spaces, six arithmetic sets --------------------------------- */

static inline void ff_m_composite(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double k[6][4] = {{0.0, 1.0, 0.0, 0.0}, {0.0, 0.5, 0.5, 0.0}, {0.2, 0.6, 0.6, -0.1},
      {1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, -1.0, 0.8}, {0.0, 1.0, 1.0, 0.0}};
  (void)c; (void)ox; (void)oy;
  ff_offset(s, 2.0, 1.5);
  FF_Prim * p;
  if (i < 10u) {
    p = ff_composite(s, i % 5u);
    s->space = ff_spaces[i / 5u];
  } else {
    p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_ARITHMETIC);
    for (int j = 0; j < 4; j++) {
      p->p.k[j] = k[i - 10u][j];
    }
    s->space = ff_spaces[i & 1u];
  }
  ff_in2(p, FF_SRC, 0, FF_RES, 0);
}

/* --- color matrix --------------------------------------------------------------------------------- */

static inline void ff_m_color_matrix(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double mat[4][20] = {
      {0, 1, 0, 0, 0.05, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0.1, 0, 0, 0, 1, 0},
      {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0.5, 0.5, 0, 0, 0.2},
      {0.9, 0.1, 0.0, 0.0, 0.05, 0.0, 0.8, 0.2, 0.0, 0.0, 0.1, 0.0, 0.7, 0.1, 0.1, 0.0, 0.0, 0.0, 0.9, 0.0},
      {2.0, -1.0, 0.0, 0.0, 0.0, 0.0, 2.0, -0.5, 0.0, 0.1, -0.5, 0.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, -0.2}};
  static const double sat[4] = {0.0, 0.5, 1.0, 3.0};
  static const double hue[6] = {0.0, 30.0, 90.0, 180.0, 270.0, -60.0};
  (void)c; (void)ox; (void)oy;
  s->space = ff_spaces[i & 1u];
  if (i < 4u) {
    ff_matrix(s, GSHSCENE_FILTER_MATRIX_MATRIX, mat[i], 20);
  } else if (i < 8u) {
    ff_matrix(s, GSHSCENE_FILTER_MATRIX_SATURATE, &sat[i - 4u], 1);
  } else if (i < 14u) {
    ff_matrix(s, GSHSCENE_FILTER_MATRIX_HUE_ROTATE, &hue[i - 8u], 1);
  } else {
    ff_matrix(s, GSHSCENE_FILTER_MATRIX_LUMINANCE_TO_ALPHA, NULL, 0);
  }
}

/* --- component transfer ----------------------------------------------------------------------------- */

static inline void ff_m_transfer(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double tbl[8] = {0.9, 0.1, 0.6, 0.3, 0.8, 0.2, 1.0, 0.0};
  static const double slope[4] = {1.5, 0.5, -1.0, 2.0};
  static const double icpt[4] = {-0.2, 0.25, 1.0, -0.5};
  static const double amp[4] = {1.0, 2.0, 0.5, 1.5};
  static const double expo[4] = {2.0, 0.5, 3.0, 0.0};
  static const double off[4] = {0.0, 0.1, -0.1, 0.2};
  (void)c; (void)ox; (void)oy;
  s->space = ff_spaces[i & 1u];
  FF_Prim * p = ff_add(s, GSHSCENE_FILTER_COMPONENT_TRANSFER);
  for (unsigned k = 0; k < 4u; k++) {
    GSHSCENE_Filter_Transfer * t = &p->p.transfer[k];
    const unsigned type = (i + 2u * k + i / 5u) % 5u;
    const unsigned j = (i + k) % 4u;
    t->type = type;
    if (type == GSHSCENE_FILTER_TRANSFER_TABLE || type == GSHSCENE_FILTER_TRANSFER_DISCRETE) {
      t->count = 1u + j;
      t->table = &tbl[j];
    } else if (type == GSHSCENE_FILTER_TRANSFER_LINEAR) {
      t->slope = slope[j];
      t->intercept = icpt[j];
    } else if (type == GSHSCENE_FILTER_TRANSFER_GAMMA) {
      t->amplitude = amp[j];
      t->exponent = expo[j];
      t->offset = off[j];
    }
  }
}

/* --- blur: the true Gaussian, the box approximation, the edge modes -------------------------------- */

static inline void ff_m_blur(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double sx[16] = {0.4, 1.0, 1.5, 0.0, 1.9, 2.0, 2.6, 3.0, 3.7, 2.2, 1.0, 1.0, 1.0, 2.6, 2.6, 2.6};
  static const double sy[16] = {0.4, 1.0, 0.5, 1.2, 1.9, 2.0, 2.6, 1.0, 3.7, 0.0, 1.0, 1.0, 1.0, 2.6, 2.6, 2.6};
  (void)c;
  s->space = ff_spaces[i & 1u];
  if (i >= 10u) {
    ff_tight(s, ox, oy);
  }
  ff_blur(s, sx[i], sy[i], i < 10u ? i % 3u : (i - 10u) % 3u);
}

/* --- morphology ------------------------------------------------------------------------------------- */

static inline void ff_m_morphology(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double rx[8] = {1.0, 2.0, 0.0, 3.0, 0.0, 0.4, 2.5, 1.0};
  static const double ry[8] = {1.0, 0.0, 2.0, 2.0, 0.0, 0.6, 1.5, 4.0};
  (void)c; (void)ox; (void)oy;
  s->space = ff_spaces[i & 1u];
  FF_Prim * p = ff_add(s, GSHSCENE_FILTER_MORPHOLOGY);
  p->p.mode = i < 8u ? (uint32_t)GSHSCENE_FILTER_MORPHOLOGY_ERODE : (uint32_t)GSHSCENE_FILTER_MORPHOLOGY_DILATE;
  p->p.sx = rx[i % 8u];
  p->p.sy = ry[i % 8u];
}

/* --- convolve matrix ---------------------------------------------------------------------------------- */

typedef struct FF_Kernel {
  uint32_t order_x, order_y, target_x, target_y;
  double divisor, bias;
  double v[9];
} FF_Kernel;

static inline void ff_convolve(FF_Spec * s, const FF_Kernel * k, uint32_t edge, uint32_t preserve) {
  FF_Prim * p = ff_add(s, GSHSCENE_FILTER_CONVOLVE_MATRIX);
  p->p.order_x = k->order_x;
  p->p.order_y = k->order_y;
  p->p.target_x = k->target_x;
  p->p.target_y = k->target_y;
  p->p.divisor = k->divisor;
  p->p.bias = k->bias;
  p->p.edge_mode = edge;
  p->p.preserve_alpha = preserve;
  p->p.value_count = (size_t)k->order_x * k->order_y;
  p->p.values = k->v;
}

static const FF_Kernel ff_kernels[6] = {
    {3, 3, 1, 1, 9.0, 0.0, {1, 1, 1, 1, 1, 1, 1, 1, 1}},
    {3, 3, 1, 1, 1.0, 0.0, {0, -1, 0, -1, 5, -1, 0, -1, 0}},
    {3, 3, 1, 1, 1.0, 0.25, {-2, -1, 0, -1, 1, 1, 0, 1, 2}},
    {1, 3, 0, 0, 4.0, 0.0, {1, 2, 1}},
    {4, 2, 2, 1, 36.0, 0.0, {1, 2, 3, 4, 5, 6, 7, 8}},
    {3, 3, 0, 2, 2.0, 0.5, {1, 0, -1, 2, 0, -2, 1, 0, -1}}};

static inline void ff_m_convolve(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  (void)c;
  s->space = ff_spaces[(i / 2u) & 1u];
  if ((i & 1u) == 0u) {
    ff_tight(s, ox, oy);
  }
  ff_convolve(s, &ff_kernels[(i / 3u) % 6u], i % 3u, i & 1u);
}

/* --- tile, image, displacement map -------------------------------------------------------------------- */

static inline const GSHSCENE_Resource * ff_tile_image(RF_Ctx * c, unsigned which) {
  const GSHSCENE_Color_Space none = rf_space((GCOL_Primaries)0, (GCOL_Transfer)0, 0.0);
  if (which == 0u) {
    return rf_image(c, 4, 3, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  }
  if (which == 1u) {
    return rf_image(c, 16, 12, 4, 8, GIMG_ALPHA_STRAIGHT, 0, none);
  }
  return rf_image(c, 4, 3, 4, 32, GIMG_ALPHA_PREMULTIPLIED, 0, none);
}

static inline void ff_m_tile_image_displace(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double tile[4][4] = {{2.0, 2.0, 4.0, 4.0}, {3.5, 1.0, 3.0, 3.5}, {1.25, 2.75, 5.0, 4.0}, {1.0, 1.0, 6.0, 6.0}};
  static const uint32_t xch[7] = {0, 1, 3, 2, 3, 0, 2};
  static const uint32_t ych[7] = {1, 0, 2, 3, 3, 0, 1};
  static const double scale[7] = {4.0, -4.0, 6.0, 2.5, 8.0, -1.5, 10.0};
  FF_Prim * p;
  if (i < 4u) {
    p = ff_offset(s, 0.0, 0.0);
    ff_in1(p, FF_SRC, 0);
    ff_sub(p, 15u, ox + tile[i][0], oy + tile[i][1], tile[i][2], tile[i][3]);
    ff_add(s, GSHSCENE_FILTER_TILE);
    s->space = ff_spaces[i & 1u];
  } else if (i < 9u) {
    static const uint32_t filters[5] = {GSHSCENE_IMAGE_FILTER_NEAREST, GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_AREA,
        GSHSCENE_IMAGE_FILTER_LINEAR, GSHSCENE_IMAGE_FILTER_CUBIC};
    const unsigned k = i - 4u;
    p = ff_add(s, GSHSCENE_FILTER_IMAGE);
    p->p.image = ff_tile_image(c, k == 2u ? 1u : k == 4u ? 2u : 0u);
    p->p.rendering = filters[k];
    if (k < 2u) {
      p->p.placement = rf_place(c, 2.5, 2.5, ox + 1.0, oy + 1.0);
    } else if (k == 2u) {
      p->p.placement = rf_place(c, 0.6, 0.6, ox + 1.0, oy + 1.0);
    } else if (k == 3u) {
      p->p.placement = rf_affine(c, 1.75, -1.0, 1.0, 1.75, ox + 4.0, oy + 0.5);
    } else {
      p->p.placement = rf_affine(c, 2.2, 0.6, 0.0, 2.0, ox + 1.0, oy + 1.0);
    }
    s->space = ff_spaces[k & 1u];
  } else {
    const unsigned k = i - 9u;
    ff_turbulence(s, GSHSCENE_FILTER_NOISE_TURBULENCE, 0.15, 0.2, 2, 3.0, 0);
    p = ff_add(s, GSHSCENE_FILTER_DISPLACEMENT_MAP);
    ff_in2(p, FF_RES, 0, FF_SRC, 0);
    p->p.scale = scale[k];
    p->p.x_channel = xch[k];
    p->p.y_channel = ych[k];
    s->space = ff_spaces[k & 1u];
  }
}

/* --- turbulence ------------------------------------------------------------------------------------------- */

static inline void ff_m_turbulence(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double fx[8] = {0.05, 0.1, 0.2, 0.3, 0.15, 0.08, 0.25, 0.12};
  static const double fy[8] = {0.12, 0.05, 0.3, 0.2, 0.08, 0.15, 0.1, 0.25};
  static const double seed[8] = {0.0, 1.0, 2.5, 7.0, -3.0, 100.0, 11.0, 0.4};
  static const uint32_t oct[4] = {1, 2, 4, 5};
  (void)c;
  s->space = ff_spaces[(i / 3u) & 1u];
  FF_Prim * p = ff_turbulence(s, i & 1u, fx[i % 8u], fy[(i + 3u) % 8u], oct[i >> 2], seed[(i * 3u) % 8u], (i >> 1) & 1u);
  if (i >= 8u) {
    ff_sub(p, 15u, ox + 1.5, oy + 1.0, 9.25, 7.5);
  }
}

/* --- lighting ------------------------------------------------------------------------------------------------ */

static inline void ff_m_lighting(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  (void)c;
  const unsigned kind = i & 3u;
  const unsigned variant = (i >> 2) & 1u;
  const int specular = (i & 8u) != 0u;
  s->space = ff_spaces[variant];
  FF_Prim * p = ff_add(s, specular ? (uint32_t)GSHSCENE_FILTER_SPECULAR_LIGHTING : (uint32_t)GSHSCENE_FILTER_DIFFUSE_LIGHTING);
  if (variant == 0u) {
    ff_color(p, 1.0, 1.0, 1.0, 1.0);
    p->p.surface_scale = 2.0;
    p->p.constant = 1.0;
    p->p.exponent = specular ? 8.0 : 0.0;
  } else {
    ff_color(p, 1.0, 0.85, 0.6, 1.0);
    p->p.surface_scale = 4.5;
    p->p.constant = 1.3;
    p->p.exponent = specular ? 35.0 : 0.0;
  }
  if (kind == 0u) {
    p->p.light.kind = GSHSCENE_FILTER_LIGHT_DISTANT;
    p->p.light.azimuth = variant ? 200.0 : 45.0;
    p->p.light.elevation = variant ? 60.0 : 35.0;
  } else if (kind == 1u) {
    p->p.light.kind = GSHSCENE_FILTER_LIGHT_POINT;
    p->p.light.position[0] = ox + (variant ? 9.0 : 3.0);
    p->p.light.position[1] = oy + (variant ? 8.0 : 2.0);
    p->p.light.position[2] = variant ? 3.0 : 6.0;
  } else {
    p->p.light.kind = GSHSCENE_FILTER_LIGHT_SPOT;
    p->p.light.position[0] = ox + 2.0;
    p->p.light.position[1] = oy + 1.0;
    p->p.light.position[2] = 8.0;
    p->p.light.points_at[0] = ox + 8.0;
    p->p.light.points_at[1] = oy + 7.0;
    p->p.light.points_at[2] = 0.0;
    p->p.light.spot_exponent = variant ? 3.0 : 1.0;
    if (kind == 3u) {
      p->p.light.has_cone = 1;
      p->p.light.cone_angle = variant ? 60.0 : 30.0;
    }
  }
}

/* --- drop shadow ------------------------------------------------------------------------------------------------ */

static inline void ff_m_shadow(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double dx[4] = {2.0, -1.5, 0.0, 4.0};
  static const double dy[4] = {2.0, 3.0, 0.0, -2.0};
  static const double sx[4] = {0.0, 1.0, 2.6, 1.5};
  static const double sy[4] = {0.0, 1.0, 2.6, 0.5};
  static const double col[4][4] = {{0.0, 0.0, 0.0, 1.0}, {0.2, 0.1, 0.8, 1.0}, {0.0, 0.0, 0.0, 0.8}, {0.9, 0.2, 0.2, 1.0}};
  static const double opa[4] = {0.6, 1.0, 1.0, 0.5};
  (void)c; (void)ox; (void)oy;
  const unsigned k = (i + i / 4u) % 4u;
  s->space = ff_spaces[k & 1u];
  FF_Prim * p = ff_add(s, GSHSCENE_FILTER_DROP_SHADOW);
  p->p.dx = dx[i % 4u];
  p->p.dy = dy[i % 4u];
  p->p.sx = sx[i / 4u];
  p->p.sy = sy[i / 4u];
  ff_color(p, col[k][0], col[k][1], col[k][2], col[k][3]);
  if (k == 3u) {
    ff_color_in(p, GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB);
  }
  p->p.opacity = opa[k];
}

/* --- graphs ------------------------------------------------------------------------------------------------------- */

static inline void ff_m_graphs(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double sat2[1] = {2.0};
  static const double sat02[1] = {0.2};
  static const double hue60[1] = {60.0};
  static const double a_table[3] = {0.0, 0.7, 1.0};
  static const double sharpen[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
  FF_Prim * p;
  (void)c;
  switch (i) {
    case 0:
      ff_blur(s, 1.2, 1.2, 0);
      ff_offset(s, 2.0, 1.0);
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_SATURATE, sat2, 1);
      break;
    case 1:
      p = ff_blur(s, 1.2, 1.2, 0);
      ff_in1(p, FF_ALPHA, 0);
      p = ff_offset(s, 2.0, 2.0);
      ff_in1(p, FF_RES, 0);
      ff_flood(s, 0.0, 0.0, 0.0, 1.0, 0.6);
      p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_IN);
      ff_in2(p, FF_RES, 2, FF_RES, 1);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 2, FF_RES, 3, FF_SRC, 0);
      break;
    case 2:
      s->space = FF_SRGB;
      ff_blur(s, 1.0, 1.0, 0);
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_HUE_ROTATE, hue60, 1);
      break;
    case 3:
      p = ff_blur(s, 1.0, 1.0, 0);
      p->p.color_space = FF_SRGB;
      ff_offset(s, 1.0, 1.0);
      break;
    case 4:
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_SATURATE, sat02, 1);
      ff_blur(s, 1.5, 1.5, 0);
      p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_ARITHMETIC);
      p->p.k[1] = 0.5;
      p->p.k[2] = 0.5;
      ff_in2(p, FF_PREV, 0, FF_SRC, 0);
      break;
    case 5:
      p = ff_add(s, GSHSCENE_FILTER_MORPHOLOGY);
      p->p.mode = GSHSCENE_FILTER_MORPHOLOGY_DILATE;
      p->p.sx = 1.0;
      p->p.sy = 1.0;
      ff_blur(s, 1.0, 1.0, 0);
      p = ff_add(s, GSHSCENE_FILTER_BLEND);
      p->p.mode = GSHSCENE_COMPOSITE_MULTIPLY;
      ff_in2(p, FF_PREV, 0, FF_SRC, 0);
      break;
    case 6:
      ff_turbulence(s, GSHSCENE_FILTER_NOISE_FRACTAL, 0.2, 0.2, 2, 5.0, 0);
      p = ff_add(s, GSHSCENE_FILTER_DISPLACEMENT_MAP);
      ff_in2(p, FF_SRC, 0, FF_RES, 0);
      p->p.scale = 5.0;
      p->p.x_channel = GSHSCENE_FILTER_CHANNEL_R;
      p->p.y_channel = GSHSCENE_FILTER_CHANNEL_G;
      ff_blur(s, 0.8, 0.8, 0);
      break;
    case 7:
      p = ff_blur(s, 1.5, 1.5, 0);
      ff_in1(p, FF_ALPHA, 0);
      p = ff_add(s, GSHSCENE_FILTER_SPECULAR_LIGHTING);
      ff_color(p, 1.0, 1.0, 1.0, 1.0);
      p->p.surface_scale = 3.0;
      p->p.constant = 1.0;
      p->p.exponent = 12.0;
      p->p.light.kind = GSHSCENE_FILTER_LIGHT_DISTANT;
      p->p.light.azimuth = 60.0;
      p->p.light.elevation = 40.0;
      p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_ARITHMETIC);
      p->p.k[1] = 1.0;
      p->p.k[2] = 1.0;
      ff_in2(p, FF_RES, 1, FF_SRC, 0);
      break;
    case 8:
      p = ff_offset(s, 1.0, 1.0);
      ff_in1(p, FF_SRC, 0);
      ff_sub(p, 15u, ox + 2.0, oy + 2.0, 4.0, 4.0);
      ff_add(s, GSHSCENE_FILTER_TILE);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 2, FF_PREV, 0, FF_SRC, 0);
      break;
    case 9:
      p = ff_add(s, GSHSCENE_FILTER_COMPONENT_TRANSFER);
      p->p.transfer[3].type = GSHSCENE_FILTER_TRANSFER_TABLE;
      p->p.transfer[3].count = 3;
      p->p.transfer[3].table = a_table;
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 2, FF_SRC, 0, FF_PREV, 0);
      break;
    case 10:
      s->opacity = 0.5;
      ff_blur(s, 1.2, 1.2, 0);
      break;
    case 11:
      s->automatic = 1;
      ff_blur(s, 2.2, 2.2, 0);
      ff_offset(s, 3.0, 2.0);
      break;
    case 12:
      s->region[0] = ox + 0.5;
      s->region[1] = oy + 0.25;
      s->region[2] = 11.0;
      s->region[3] = 9.5;
      ff_blur(s, 1.8, 1.8, GSHSCENE_FILTER_EDGE_DUPLICATE);
      break;
    case 13:
      ff_flood(s, 0.2, 0.9, 0.3, 0.4, 1.0);
      p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_OVER);
      ff_in2(p, FF_SRC, 0, FF_RES, 0);
      break;
    case 14:
      p = ff_add(s, GSHSCENE_FILTER_DROP_SHADOW);
      p->p.dx = 2.0;
      p->p.dy = 2.0;
      p->p.sx = 1.0;
      p->p.sy = 1.0;
      ff_color(p, 0.0, 0.0, 0.0, 1.0);
      p->p.opacity = 0.8;
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_LUMINANCE_TO_ALPHA, NULL, 0);
      break;
    default:
      p = ff_add(s, GSHSCENE_FILTER_MORPHOLOGY);
      p->p.mode = GSHSCENE_FILTER_MORPHOLOGY_ERODE;
      p->p.sx = 1.0;
      p->p.sy = 1.0;
      p = ff_add(s, GSHSCENE_FILTER_CONVOLVE_MATRIX);
      p->p.order_x = 3;
      p->p.order_y = 3;
      p->p.target_x = 1;
      p->p.target_y = 1;
      p->p.divisor = 1.0;
      p->p.edge_mode = GSHSCENE_FILTER_EDGE_DUPLICATE;
      p->p.value_count = 9;
      p->p.values = sharpen;
      p = ff_add(s, GSHSCENE_FILTER_DIFFUSE_LIGHTING);
      ff_color(p, 1.0, 1.0, 1.0, 1.0);
      p->p.surface_scale = 2.5;
      p->p.constant = 1.1;
      p->p.light.kind = GSHSCENE_FILTER_LIGHT_POINT;
      p->p.light.position[0] = ox + 5.0;
      p->p.light.position[1] = oy + 4.0;
      p->p.light.position[2] = 5.0;
      break;
  }
}

/* --- twelve primitives, drawn under a target map -------------------------------------------------------------- */

static inline void ff_m_mix(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double sharpen[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
  static const double hue90[1] = {90.0};
  FF_Prim * p;
  (void)c;
  s->space = ff_spaces[(i / 2u) & 1u];
  switch (i) {
    case 0:
      ff_blur(s, 1.4, 1.4, 0);
      break;
    case 1:
      ff_blur(s, 3.0, 3.0, 0);
      break;
    case 2:
      ff_offset(s, 3.0, 1.5);
      break;
    case 3:
      p = ff_add(s, GSHSCENE_FILTER_DROP_SHADOW);
      p->p.dx = 2.0;
      p->p.dy = 2.0;
      p->p.sx = 1.2;
      p->p.sy = 1.2;
      ff_color(p, 0.0, 0.0, 0.0, 1.0);
      p->p.opacity = 0.7;
      break;
    case 4:
      p = ff_add(s, GSHSCENE_FILTER_DIFFUSE_LIGHTING);
      ff_color(p, 1.0, 1.0, 1.0, 1.0);
      p->p.surface_scale = 3.0;
      p->p.constant = 1.0;
      p->p.light.kind = GSHSCENE_FILTER_LIGHT_POINT;
      p->p.light.position[0] = ox + 4.0;
      p->p.light.position[1] = oy + 3.0;
      p->p.light.position[2] = 5.0;
      break;
    case 5:
      ff_turbulence(s, GSHSCENE_FILTER_NOISE_FRACTAL, 0.15, 0.15, 2, 4.0, 0);
      break;
    case 6:
      p = ff_add(s, GSHSCENE_FILTER_MORPHOLOGY);
      p->p.mode = GSHSCENE_FILTER_MORPHOLOGY_DILATE;
      p->p.sx = 1.0;
      p->p.sy = 1.0;
      break;
    case 7:
      p = ff_add(s, GSHSCENE_FILTER_CONVOLVE_MATRIX);
      p->p.order_x = 3;
      p->p.order_y = 3;
      p->p.target_x = 1;
      p->p.target_y = 1;
      p->p.divisor = 1.0;
      p->p.edge_mode = GSHSCENE_FILTER_EDGE_DUPLICATE;
      p->p.value_count = 9;
      p->p.values = sharpen;
      break;
    case 8:
      ff_turbulence(s, GSHSCENE_FILTER_NOISE_TURBULENCE, 0.2, 0.2, 1, 1.0, 0);
      p = ff_add(s, GSHSCENE_FILTER_DISPLACEMENT_MAP);
      ff_in2(p, FF_SRC, 0, FF_RES, 0);
      p->p.scale = 4.0;
      p->p.x_channel = GSHSCENE_FILTER_CHANNEL_R;
      p->p.y_channel = GSHSCENE_FILTER_CHANNEL_G;
      break;
    case 9:
      p = ff_add(s, GSHSCENE_FILTER_COMPONENT_TRANSFER);
      p->p.transfer[0].type = GSHSCENE_FILTER_TRANSFER_GAMMA;
      p->p.transfer[0].amplitude = 1.5;
      p->p.transfer[0].exponent = 0.6;
      p->p.transfer[2].type = GSHSCENE_FILTER_TRANSFER_LINEAR;
      p->p.transfer[2].slope = 1.4;
      p->p.transfer[2].intercept = -0.1;
      break;
    case 10:
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_HUE_ROTATE, hue90, 1);
      break;
    default:
      ff_offset(s, 2.0, 2.0);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 2, FF_PREV, 0, FF_SRC, 0);
      break;
  }
}

/* The scenes of the grid makers: a list function for each, in the form of RF_Fixture. */

#define FF_SCENE(name, maker, cells) \
  static inline GSHSCENE_List * name(RF_Ctx * c) { return ff_grid(c, maker, cells); }

FF_SCENE(ff_basic, ff_m_basic, 16)
FF_SCENE(ff_blend_linear, ff_m_blend_linear, 16)
FF_SCENE(ff_blend_srgb, ff_m_blend_srgb, 16)
FF_SCENE(ff_composite_scene, ff_m_composite, 16)
FF_SCENE(ff_color_matrix, ff_m_color_matrix, 16)
FF_SCENE(ff_transfer, ff_m_transfer, 16)
FF_SCENE(ff_blur_scene, ff_m_blur, 16)
FF_SCENE(ff_morphology, ff_m_morphology, 16)
FF_SCENE(ff_convolve_scene, ff_m_convolve, 16)
FF_SCENE(ff_tile_image_displace, ff_m_tile_image_displace, 16)
FF_SCENE(ff_turbulence_scene, ff_m_turbulence, 16)
FF_SCENE(ff_lighting, ff_m_lighting, 16)
FF_SCENE(ff_shadow, ff_m_shadow, 16)
FF_SCENE(ff_graphs, ff_m_graphs, 16)

/* The five maps: the cells fill the user rectangle 48 by 30, the target map takes it onto the raster. */

static inline GSHSCENE_List * ff_mapped(RF_Ctx * c, double a, double b, double cc, double d, double tx, double ty) {
  c->target_map = rf_affine(c, a, b, cc, d, tx, ty);
  return ff_grid(c, ff_m_mix, 12);
}

static inline GSHSCENE_List * ff_map_rotate(RF_Ctx * c) {
  /* a turn of 30 degrees (cos, sin to 16 digits) and a scale of 0.7 */
  return ff_mapped(c, 0.7 * 0.8660254037844387, -0.7 * 0.5, 0.7 * 0.5, 0.7 * 0.8660254037844387, 12.0, 2.0);
}

static inline GSHSCENE_List * ff_map_skew(RF_Ctx * c) {
  return ff_mapped(c, 0.7, 0.3, 0.12, 0.7, 2.0, 4.0);
}

static inline GSHSCENE_List * ff_map_scale(RF_Ctx * c) {
  return ff_mapped(c, 0.9, 0.0, 0.0, 1.2, 0.0, 2.0);
}

static inline GSHSCENE_List * ff_map_quarter(RF_Ctx * c) {
  /* a quarter turn: x' = -0.8 y + 26, y' = 0.8 x + 1 */
  return ff_mapped(c, 0.0, -0.8, 0.8, 0.0, 26.0, 1.0);
}

static inline GSHSCENE_List * ff_map_mirror(RF_Ctx * c) {
  return ff_mapped(c, -0.9, 0.0, 0.0, 0.9, 44.0, 2.0);
}

/* --- filtered groups inside pictures ------------------------------------------------------------------------------ */

static inline const GSHSCENE_Resource * ff_picture_of(RF_Ctx * c, unsigned kind) {
  GSHSCENE_List * inner = rf_open(c);
  FF_Spec * s = (FF_Spec *)malloc(sizeof *s);
  if (!s) {
    c->failed = 1;
    return NULL;
  }
  ff_begin(s, kind, 0.0, 0.0);
  if (kind == 0u) {
    ff_blur(s, 1.0, 1.0, 0);
    ff_offset(s, 2.0, 1.0);
  } else {
    s->automatic = 1;
    FF_Prim * p = ff_add(s, GSHSCENE_FILTER_DROP_SHADOW);
    p->p.dx = 2.0;
    p->p.dy = 2.0;
    p->p.sx = 1.2;
    p->p.sy = 1.2;
    ff_color(p, 0.0, 0.0, 0.0, 1.0);
    p->p.opacity = 0.7;
  }
  ff_group(c, inner, s, 0.0, 0.0);
  free(s);
  rf_close(c, inner);
  const GSHSCENE_Resource * picture = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, inner, &picture) == GSHSCENE_OK);
  return rf_hold(c, picture);
}

static inline GSHSCENE_List * ff_picture(RF_Ctx * c) {
  const GSHSCENE_Resource * blurred = ff_picture_of(c, 0);
  const GSHSCENE_Resource * shadowed = ff_picture_of(c, 1);
  GSHSCENE_List * l = rf_open(c);
  const GSHGEOM_Map a = rf_affine(c, 1.0, 0.0, 0.0, 1.0, 2.0, 2.0);
  const GSHGEOM_Map b = rf_affine(c, 0.9063077870366499, -0.42261826174069944, 0.42261826174069944, 0.9063077870366499, 30.0, 2.0);
  const GSHGEOM_Map d = rf_affine(c, 1.6, 0.0, 0.0, 1.2, 4.0, 16.0);
  const GSHGEOM_Map e = rf_affine(c, 1.0, 0.5, 0.0, 1.0, 26.0, 20.0);
  if (blurred && shadowed) {
    rf_check(c, gshscene_list_instance(l, blurred, &a, 0, 0.0) == GSHSCENE_OK);
    rf_check(c, gshscene_list_instance(l, shadowed, &b, 0, 0.0) == GSHSCENE_OK);
    rf_check(c, gshscene_list_instance(l, blurred, &d, 0, 0.0) == GSHSCENE_OK);
    rf_check(c, gshscene_list_instance(l, shadowed, &e, 0, 0.0) == GSHSCENE_OK);
  }
  rf_close(c, l);
  return l;
}

/* --- slots of the filter's picture (FL25) ------------------------------------------------------------------------------- */

/** An element a slot's instance draws: kinds 0 to 2 are the cell's source graphics at the origin, 3 is the third of them in a group at half
 * opacity, 4 and 5 are the pictures of filtered groups (a blur then an offset; a drop shadow). */
static inline const GSHSCENE_Resource * ff_element(RF_Ctx * c, unsigned kind) {
  if (kind == 4u) {
    return ff_picture_of(c, 0);
  }
  if (kind == 5u) {
    return ff_picture_of(c, 1);
  }
  GSHSCENE_List * l = rf_open(c);
  if (kind == 3u) {
    GSHSCENE_Group g = rf_group(0.5, GSHSCENE_COMPOSITE_SRC_OVER);
    rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
    ff_content(c, l, 0.0, 0.0, 2u);
    rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  } else {
    ff_content(c, l, 0.0, 0.0, kind);
  }
  rf_close(c, l);
  const GSHSCENE_Resource * element = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, l, &element) == GSHSCENE_OK);
  return rf_hold(c, element);
}

/** Give @p s a picture of @p n instances, the elements @p kinds under the maps @p maps, in that order: slot i is instance i. */
static inline void ff_slots(RF_Ctx * c, FF_Spec * s, size_t n, const unsigned * kinds, const GSHGEOM_Map * maps) {
  GSHSCENE_List * l = rf_open(c);
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Resource * element = ff_element(c, kinds[i]);
    if (!element) {
      c->failed = 1;
      return;
    }
    rf_check(c, gshscene_list_instance(l, element, &maps[i], 0, 0.0) == GSHSCENE_OK);
  }
  rf_close(c, l);
  const GSHSCENE_Resource * picture = NULL;
  rf_check(c, gshscene_store_intern_picture(c->store, l, &picture) == GSHSCENE_OK);
  s->picture = rf_hold(c, picture);
}

/** A primitive that draws slot @p slot of the filter's picture. */
static inline FF_Prim * ff_slot(FF_Spec * s, uint32_t slot) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_IMAGE);
  q->p.use_picture = 1;
  q->p.slot = slot;
  return q;
}

/** A merge of the results of primitives 0 to @p n - 1 (two or three). */
static inline FF_Prim * ff_merge_results(FF_Spec * s, uint32_t n) {
  FF_Prim * q = ff_add(s, GSHSCENE_FILTER_MERGE);
  if (n == 2u) {
    ff_ins(q, 2, FF_RES, 0u, FF_RES, 1u);
  } else {
    ff_ins(q, 3, FF_RES, 0u, FF_RES, 1u, FF_RES, 2u);
  }
  return q;
}

static inline void ff_m_slot(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  const unsigned one[1] = {0u};
  GSHGEOM_Map m[3];
  FF_Prim * p;
  switch (i) {
    case 0: /* one slot at the cell's origin */
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox, oy);
      ff_slots(c, s, 1, one, m);
      ff_slot(s, 0);
      break;
    case 1: { /* a fractional translation, and a different element */
      const unsigned k[1] = {1u};
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox + 2.375, oy + 1.625);
      ff_slots(c, s, 1, k, m);
      ff_slot(s, 0);
      break;
    }
    case 2: /* a turn of 30 degrees about the cell's origin, moved into the cell */
      m[0] = rf_affine(c, 0.8660254037844387, 0.5, -0.5, 0.8660254037844387, ox + 3.0, oy + 0.5);
      ff_slots(c, s, 1, one, m);
      ff_slot(s, 0);
      break;
    case 3: { /* an uneven scale with a shear */
      const unsigned k[1] = {2u};
      m[0] = rf_affine(c, 0.75, 0.0, 0.4, 0.9, ox + 0.5, oy + 0.25);
      ff_slots(c, s, 1, k, m);
      ff_slot(s, 0);
      break;
    }
    case 4: { /* two slots, merged */
      const unsigned k[2] = {0u, 3u};
      m[0] = rf_affine(c, 0.6, 0.0, 0.0, 0.6, ox, oy);
      m[1] = rf_affine(c, 0.6, 0.0, 0.0, 0.6, ox + 5.5, oy + 4.0);
      ff_slots(c, s, 2, k, m);
      ff_slot(s, 0);
      ff_slot(s, 1);
      ff_merge_results(s, 2u);
      break;
    }
    case 5: { /* three slots, merged, in sRGB */
      const unsigned k[3] = {1u, 2u, 0u};
      m[0] = rf_affine(c, 0.5, 0.0, 0.0, 0.5, ox, oy);
      m[1] = rf_affine(c, 0.5, 0.0, 0.0, 0.5, ox + 6.0, oy);
      m[2] = rf_affine(c, 0.5, 0.0, 0.0, 0.5, ox + 3.0, oy + 5.0);
      ff_slots(c, s, 3, k, m);
      ff_slot(s, 0);
      ff_slot(s, 1);
      ff_slot(s, 2);
      ff_merge_results(s, 3u);
      s->space = FF_SRGB;
      break;
    }
    case 6: /* a slot fed to a blur */
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox + 1.0, oy + 0.5);
      ff_slots(c, s, 1, one, m);
      ff_slot(s, 0);
      ff_blur(s, 1.5, 1.5, 0);
      break;
    case 7: { /* a slot composited with the source graphic */
      const unsigned k[1] = {1u};
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox + 2.0, oy);
      ff_slots(c, s, 1, k, m);
      ff_slot(s, 0);
      p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_IN);
      ff_in2(p, FF_SRC, 0, FF_RES, 0);
      break;
    }
    case 8: /* a region that cuts the slot */
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox, oy);
      ff_slots(c, s, 1, one, m);
      ff_tight(s, ox, oy);
      ff_slot(s, 0);
      break;
    case 9: { /* an element that holds a filtered group of its own */
      const unsigned k[1] = {4u};
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox + 1.0, oy + 1.0);
      ff_slots(c, s, 1, k, m);
      ff_slot(s, 0);
      break;
    }
    case 10: { /* one slot drawn by two primitives, a flood between them */
      const unsigned k[1] = {5u};
      m[0] = rf_affine(c, 0.8, 0.0, 0.0, 0.8, ox + 1.0, oy + 0.5);
      ff_slots(c, s, 1, k, m);
      ff_slot(s, 0);
      ff_flood(s, 0.2, 0.4, 0.9, 0.5, 1.0);
      ff_slot(s, 0);
      p = ff_add(s, GSHSCENE_FILTER_MERGE);
      ff_ins(p, 3, FF_RES, 0u, FF_RES, 1u, FF_RES, 2u);
      break;
    }
    default: { /* a group opacity, and a cell with no content of its own */
      const unsigned k[1] = {3u};
      m[0] = rf_affine(c, 1.0, 0.0, 0.0, 1.0, ox + 0.5, oy);
      ff_slots(c, s, 1, k, m);
      ff_slot(s, 0);
      s->opacity = 0.6;
      s->content = 0;
      break;
    }
  }
}

static inline GSHSCENE_List * ff_slot_scene(RF_Ctx * c) {
  return ff_grid(c, ff_m_slot, 12u);
}

static inline GSHSCENE_List * ff_slot_rotated(RF_Ctx * c) {
  /* the same twelve under a turn of 20 degrees and a scale of 0.8: the slots' frame is the filter's canvas, drawn in canonical space */
  c->target_map = rf_affine(c, 0.8 * 0.9396926207859084, -0.8 * 0.3420201433256687, 0.8 * 0.3420201433256687, 0.8 * 0.9396926207859084, 9.0, 1.0);
  return ff_slot_scene(c);
}

/* --- backdrop-filter groups (FL22) ----------------------------------------------------------------------------------- */

static inline void ff_m_backdrop(RF_Ctx * c, unsigned i, double ox, double oy, FF_Spec * s) {
  static const double hue90[1] = {90.0};
  static const double grey[1] = {0.0};
  static const double a_table[3] = {1.0, 0.5, 0.0};
  FF_Prim * p;
  (void)c; (void)ox; (void)oy;
  s->mode = GSHSCENE_FILTER_MODE_BACKDROP;
  s->automatic = 1;
  s->space = ff_spaces[(i >> 1) & 1u];
  s->content = i != 10u;
  switch (i) {
    case 0:
      ff_blur(s, 1.2, 1.2, 0);
      break;
    case 1:
      ff_blur(s, 3.0, 3.0, 0);
      break;
    case 2:
      ff_offset(s, 2.5, 1.0);
      break;
    case 3:
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_HUE_ROTATE, hue90, 1);
      break;
    case 4:
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_SATURATE, grey, 1);
      break;
    case 5:
      p = ff_add(s, GSHSCENE_FILTER_MORPHOLOGY);
      p->p.mode = GSHSCENE_FILTER_MORPHOLOGY_DILATE;
      p->p.sx = 1.0;
      p->p.sy = 1.0;
      break;
    case 6:
      s->automatic = 0;
      s->region[0] = ox + 1.0;
      s->region[1] = oy + 1.0;
      s->region[2] = 10.0;
      s->region[3] = 8.0;
      ff_blur(s, 1.0, 1.0, GSHSCENE_FILTER_EDGE_WRAP);
      break;
    case 7:
      ff_flood(s, 0.9, 0.2, 0.2, 0.5, 1.0);
      p = ff_composite(s, GSHSCENE_FILTER_COMPOSITE_ATOP);
      ff_in2(p, FF_SRC, 0, FF_RES, 0);
      break;
    case 8:
      s->opacity = 0.6;
      ff_blur(s, 1.2, 1.2, 0);
      break;
    case 9:
      ff_matrix(s, GSHSCENE_FILTER_MATRIX_HUE_ROTATE, hue90, 1);
      break;
    case 10:
      p = ff_add(s, GSHSCENE_FILTER_COMPONENT_TRANSFER);
      p->p.transfer[0].type = GSHSCENE_FILTER_TRANSFER_TABLE;
      p->p.transfer[0].count = 3;
      p->p.transfer[0].table = a_table;
      break;
    default:
      ff_turbulence(s, GSHSCENE_FILTER_NOISE_TURBULENCE, 0.1, 0.1, 1, 2.0, 0);
      p = ff_add(s, GSHSCENE_FILTER_DISPLACEMENT_MAP);
      ff_in2(p, FF_SRC, 0, FF_RES, 0);
      p->p.scale = 5.0;
      p->p.x_channel = GSHSCENE_FILTER_CHANNEL_R;
      p->p.y_channel = GSHSCENE_FILTER_CHANNEL_G;
      break;
  }
}

/** The drawn backdrop: bars, a star and a blob across the whole raster, with edges inside every cell. */
static inline void ff_backdrop_art(RF_Ctx * c, GSHSCENE_List * l) {
  rf_fill(c, l, rf_rect(c, 0.0, 0.0, 48.0, 40.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.9, 0.9, 0.8, 1.0), 0, 0.0);
  for (unsigned k = 0; k < 6u; k++) {
    const double x = 3.0 + 8.0 * (double)k;
    rf_fill(c, l, rf_rect(c, x, 0.0, x + 3.5, 40.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.3 + 0.1 * (double)k, 0.7, 0.8), 0, 0.0);
  }
  rf_fill(c, l, rf_star(c, 24.0, 20.0, 17.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.85, 0.2, 0.15, 0.75), 0, 0.0);
  rf_fill(c, l, rf_blob(c, 4.0, 22.0, 9.0), NULL, GSHGEOM_FILL_NONZERO, rf_solid(0.1, 0.6, 0.3, 0.6), 0, 0.0);
}

static inline GSHSCENE_List * ff_backdrop_scene(RF_Ctx * c) {
  GSHSCENE_List * l = rf_open(c);
  ff_backdrop_art(c, l);
  for (unsigned i = 0; i < 12u; i++) {
    const double ox = ff_ox(i);
    const double oy = ff_oy(i);
    FF_Spec * s = (FF_Spec *)malloc(sizeof *s);
    if (!s) {
      c->failed = 1;
      break;
    }
    ff_begin(s, i, ox, oy);
    ff_m_backdrop(c, i, ox, oy, s);
    rf_check(c, gshscene_list_clip_push(l, rf_rect(c, ox + 1.25, oy + 1.5, ox + 10.75, oy + 8.25), NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    GSHSCENE_Group g = rf_group(s->opacity, GSHSCENE_COMPOSITE_SRC_OVER);
    g.filter = ff_intern(c, s);
    g.filter_mode = s->mode;
    if (i == 9u) {
      g.blend_space.primaries = GCOL_PRIMARIES_SRGB;
      g.blend_space.transfer = GCOL_TRANSFER_LINEAR;
    }
    rf_check(c, gshscene_list_group_push(l, &g, 0, 0.0) == GSHSCENE_OK);
    if (s->content) {
      rf_fill(c, l, rf_star(c, ox + 6.0, oy + 5.0, 2.6), NULL, GSHGEOM_FILL_NONZERO, rf_solid(1.0, 1.0, 0.2, 0.9), 0, 0.0);
    }
    rf_check(c, gshscene_list_group_pop(l) == GSHSCENE_OK);
    rf_check(c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
    free(s);
  }
  rf_close(c, l);
  return l;
}

static inline GSHSCENE_List * ff_backdrop(RF_Ctx * c) {
  return ff_backdrop_scene(c);
}

static inline GSHSCENE_List * ff_backdrop_rotated(RF_Ctx * c) {
  /* a turn of 20 degrees and a scale of 0.8: the filter's canvas is the turned render, drawn in canonical space */
  c->target_map = rf_affine(c, 0.8 * 0.9396926207859084, -0.8 * 0.3420201433256687, 0.8 * 0.3420201433256687, 0.8 * 0.9396926207859084, 9.0, 1.0);
  return ff_backdrop_scene(c);
}

/* --- the set ------------------------------------------------------------------------------------------------------------ */

typedef struct FF_Fixture {
  const char * name;
  GSHSCENE_List * (*build)(RF_Ctx *);
} FF_Fixture;

static const FF_Fixture FF_FIXTURES[] = {
    {"filter-basic", ff_basic},
    {"filter-blend-linear", ff_blend_linear},
    {"filter-blend-srgb", ff_blend_srgb},
    {"filter-composite", ff_composite_scene},
    {"filter-color-matrix", ff_color_matrix},
    {"filter-transfer", ff_transfer},
    {"filter-blur", ff_blur_scene},
    {"filter-morphology", ff_morphology},
    {"filter-convolve", ff_convolve_scene},
    {"filter-tile-image-displace", ff_tile_image_displace},
    {"filter-turbulence", ff_turbulence_scene},
    {"filter-lighting", ff_lighting},
    {"filter-shadow", ff_shadow},
    {"filter-graphs", ff_graphs},
    {"filter-map-rotate", ff_map_rotate},
    {"filter-map-skew", ff_map_skew},
    {"filter-map-scale", ff_map_scale},
    {"filter-map-quarter", ff_map_quarter},
    {"filter-map-mirror", ff_map_mirror},
    {"filter-picture", ff_picture},
    {"filter-slot", ff_slot_scene},
    {"filter-slot-rotated", ff_slot_rotated},
    {"filter-backdrop", ff_backdrop},
    {"filter-backdrop-rotated", ff_backdrop_rotated},
};

#define FF_FIXTURE_COUNT (sizeof FF_FIXTURES / sizeof FF_FIXTURES[0])

/** Render filter fixture @p index on a fresh raster of @p format (the sink's own limits, or @p limits) and give the render's hash. */
static inline int ff_render_with(size_t index, GSHCPU_Format format, const GSHCPU_Limits * limits, uint64_t * hash, GIMG_Raster ** keep) {
  return rf_render_built(FF_FIXTURES[index].build, format, limits, hash, keep);
}

#endif /* GHOTI_IO_GSHCPU_TEST_FILTER_FIXTURES_H */
