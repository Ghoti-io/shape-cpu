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
 * A group's filter (shape-scene's filter.h, FL1 to FL20; render.h): the graph of primitives run over the group's layer.  The layer is
 * a canvas of whole device pixels of premultiplied RGBA `double`s in the group's blend space; the filter reads it as the source
 * graphic, runs each primitive in its own color space, clips each result to its subregion by the area each pixel has inside it
 * (FL8), and writes the last result back converted to the layer's space and clipped to the region (FL20).  Everything is in
 * `double`, in an order of operations the oracle (`tools/gen-filter-vectors.py`) restates in exact rationals, and the only
 * libraries are `math`'s (no libm).
 *
 * The executor holds a result only while a later primitive still reads it: the buffers it needs are a count the filter alone decides
 * (::gshcpu_filter_scratch_doubles), so that a render under CALLER_MEMORY knows the room before it begins.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/math/basic.h>
#include <ghoti.io/math/elementary.h>

#include <string.h>

#define PIX 4u

/* --- the plan: the last read of each result, and the slot each lives in ------------------------------------------------ */

typedef struct Plan {
  size_t count;
  size_t last[GSHSCENE_FILTER_MAX_PRIMITIVES]; /* the last primitive that reads result i (i itself for the final result) */
  size_t slot[GSHSCENE_FILTER_MAX_PRIMITIVES]; /* the buffer result i is held in */
  size_t slots;                                /* how many buffers at once */
} Plan;

/** The primitive an input names for primitive @p i; SIZE_MAX for the source graphic or alpha. */
static int input_result(const GSHSCENE_Filter_Input * in, size_t i, size_t * j) {
  if (in->kind == (uint32_t)GSHSCENE_FILTER_INPUT_RESULT) {
    *j = in->index;
    return 1;
  }
  if (in->kind == (uint32_t)GSHSCENE_FILTER_INPUT_PREVIOUS && i > 0) {
    *j = i - 1;
    return 1;
  }
  return 0;
}

static void plan_make(const GSHSCENE_Filter * f, Plan * plan) {
  plan->count = f->primitive_count;
  for (size_t i = 0; i < f->primitive_count; i++) {
    plan->last[i] = i;
  }
  for (size_t i = 0; i < f->primitive_count; i++) {
    const GSHSCENE_Filter_Primitive * p = &f->primitives[i];
    for (size_t k = 0; k < p->input_count; k++) {
      size_t j;
      if (input_result(&p->inputs[k], i, &j) && plan->last[j] < i) {
        plan->last[j] = i;
      }
    }
  }
  plan->last[f->primitive_count - 1u] = SIZE_MAX; /* the final result lives to the end */
  /* greedy interval colouring: a slot is free for result i when its holder's last read is before i */
  size_t holder_last[GSHSCENE_FILTER_MAX_PRIMITIVES];
  int used[GSHSCENE_FILTER_MAX_PRIMITIVES];
  memset(used, 0, sizeof used);
  size_t slots = 0;
  for (size_t i = 0; i < f->primitive_count; i++) {
    size_t pick = SIZE_MAX;
    for (size_t s = 0; s < slots; s++) {
      if (!used[s] || holder_last[s] < i) {
        pick = s;
        break;
      }
    }
    if (pick == SIZE_MAX) {
      pick = slots++;
    }
    used[pick] = 1;
    holder_last[pick] = plan->last[i];
    plan->slot[i] = pick;
  }
  plan->slots = slots;
}

/** The temporaries beside the result slots: two converted or alpha inputs. */
#define TEMPS 2u

size_t gshcpu_filter_scratch_doubles(const GSHSCENE_Filter * f, size_t width, size_t height) {
  Plan plan;
  plan_make(f, &plan);
  const size_t line = width > height ? width : height;
  size_t pixels = width * height;
  if (width != 0 && pixels / width != height) {
    return SIZE_MAX;
  }
  /* the slots and temporaries of four doubles a pixel, then 8 lines for the blur's and the weights' work */
  size_t buffers = plan.slots + TEMPS;
  if (pixels > SIZE_MAX / (PIX * buffers) || line > SIZE_MAX / 8u) {
    return SIZE_MAX;
  }
  return pixels * PIX * buffers + 8u * line;
}

/* --- geometry --------------------------------------------------------------------------------------------------------- */

typedef struct Aff {
  double a, b, c, d, e, f; /* x' = a x + b y + e, y' = c x + d y + f */
  int swap;                /* a quarter turn: the user x axis lands on the device y axis */
} Aff;

typedef struct Rect2 {
  double x0, y0, x1, y1;
} Rect2;

static int aff_make(const GSHGEOM_Map * map, Aff * out) {
  if (!map || map->dim != 2) {
    return 0;
  }
  out->a = map->m[0][0];
  out->b = map->m[0][1];
  out->c = map->m[1][0];
  out->d = map->m[1][1];
  out->e = map->t[0];
  out->f = map->t[1];
  out->swap = out->a == 0.0 && out->d == 0.0;
  const int plain = out->b == 0.0 && out->c == 0.0;
  const double det = out->a * out->d - out->b * out->c;
  return (plain || out->swap) && det != 0.0;
}

static void aff_point(const Aff * m, double x, double y, double * ox, double * oy) {
  *ox = m->a * x + m->b * y + m->e;
  *oy = m->c * x + m->d * y + m->f;
}

static void aff_inverse_point(const Aff * m, double x, double y, double * ox, double * oy) {
  const double det = m->a * m->d - m->b * m->c;
  const double dx = x - m->e;
  const double dy = y - m->f;
  *ox = (m->d * dx - m->b * dy) / det;
  *oy = (-m->c * dx + m->a * dy) / det;
}

static double dmin(double a, double b) {
  return a < b ? a : b;
}

static double dmax(double a, double b) {
  return a > b ? a : b;
}

static Rect2 rect_bound4(const double xs[4], const double ys[4]) {
  Rect2 r = {xs[0], ys[0], xs[0], ys[0]};
  for (int i = 1; i < 4; i++) {
    r.x0 = dmin(r.x0, xs[i]);
    r.y0 = dmin(r.y0, ys[i]);
    r.x1 = dmax(r.x1, xs[i]);
    r.y1 = dmax(r.y1, ys[i]);
  }
  return r;
}

static Rect2 rect_to_device(const Aff * m, Rect2 u) {
  double xs[4], ys[4];
  aff_point(m, u.x0, u.y0, &xs[0], &ys[0]);
  aff_point(m, u.x1, u.y0, &xs[1], &ys[1]);
  aff_point(m, u.x1, u.y1, &xs[2], &ys[2]);
  aff_point(m, u.x0, u.y1, &xs[3], &ys[3]);
  return rect_bound4(xs, ys);
}

static Rect2 rect_to_user(const Aff * m, Rect2 d) {
  double xs[4], ys[4];
  aff_inverse_point(m, d.x0, d.y0, &xs[0], &ys[0]);
  aff_inverse_point(m, d.x1, d.y0, &xs[1], &ys[1]);
  aff_inverse_point(m, d.x1, d.y1, &xs[2], &ys[2]);
  aff_inverse_point(m, d.x0, d.y1, &xs[3], &ys[3]);
  return rect_bound4(xs, ys);
}

/** A pair of user lengths along the user axes as lengths along the device axes (D-FL8). */
static void device_lengths(const Aff * m, double ux, double uy, double * dx, double * dy) {
  if (!m->swap) {
    *dx = gmath_fabs(m->a) * ux;
    *dy = gmath_fabs(m->d) * uy;
  } else {
    *dx = gmath_fabs(m->b) * uy;
    *dy = gmath_fabs(m->c) * ux;
  }
}

/** The area of pixel @p p (`[p, p + 1]` on its axis) inside `[lo, hi]`. */
static double overlap(double p, double lo, double hi) {
  const double a = dmax(p, lo);
  const double b = dmin(p + 1.0, hi);
  return b > a ? b - a : 0.0;
}

/** Weights of the canvas columns and rows for a device rectangle (FL8). */
static void weights(GSHCPU_Rect canvas, Rect2 d, double * wx, double * wy) {
  const size_t w = (size_t)(canvas.x1 - canvas.x0);
  const size_t h = (size_t)(canvas.y1 - canvas.y0);
  for (size_t i = 0; i < w; i++) {
    wx[i] = overlap(gshcpu_render_i2d(canvas.x0) + (double)i, d.x0, d.x1);
  }
  for (size_t j = 0; j < h; j++) {
    wy[j] = overlap(gshcpu_render_i2d(canvas.y0) + (double)j, d.y0, d.y1);
  }
}

/** Multiply @p buf by the weights of the rectangle (user space) @p u under @p m. */
static void clip_to(const Aff * m, GSHCPU_Rect canvas, Rect2 u, double * buf, double * wx, double * wy) {
  const size_t w = (size_t)(canvas.x1 - canvas.x0);
  const size_t h = (size_t)(canvas.y1 - canvas.y0);
  weights(canvas, rect_to_device(m, u), wx, wy);
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      const double k = wx[i] * wy[j];
      if (k != 1.0) {
        double * p = &buf[PIX * (j * w + i)];
        p[0] *= k;
        p[1] *= k;
        p[2] *= k;
        p[3] *= k;
      }
    }
  }
}

/* --- color ----------------------------------------------------------------------------------------------------------------- */

static GSHSCENE_Color_Space space_of(uint32_t filter_space) {
  GSHSCENE_Color_Space s;
  memset(&s, 0, sizeof s);
  s.primaries = GCOL_PRIMARIES_SRGB;
  s.transfer = filter_space == (uint32_t)GSHSCENE_FILTER_SPACE_SRGB ? GCOL_TRANSFER_SRGB : GCOL_TRANSFER_LINEAR;
  return s;
}

static double clamp01(double x) {
  return !(x > 0.0) ? 0.0 : (x > 1.0 ? 1.0 : x);
}

static void convert_buffer(const GSHCPU_Convert * c, const double * in, double * out, size_t pixels) {
  for (size_t p = 0; p < pixels; p++) {
    gshcpu_render_convert_premultiplied(c, &in[PIX * p], &out[PIX * p]);
  }
}

/* --- the primitives ---------------------------------------------------------------------------------------------------- */

/** Round half up to whole pixels (FL10). */
static long whole(double v) {
  return (long)gmath_floor(v + 0.5);
}

static void prim_offset(const double * in, double * out, size_t w, size_t h, long sx, long sy) {
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      const long si = (long)i - sx;
      const long sj = (long)j - sy;
      double * o = &out[PIX * (j * w + i)];
      if (si >= 0 && sj >= 0 && (size_t)si < w && (size_t)sj < h) {
        memcpy(o, &in[PIX * ((size_t)sj * w + (size_t)si)], PIX * sizeof(double));
      } else {
        o[0] = o[1] = o[2] = o[3] = 0.0;
      }
    }
  }
}

static void prim_over(const double * top, double * dst, size_t pixels) {
  for (size_t p = 0; p < pixels; p++) {
    const double a = top[PIX * p + 3];
    for (size_t c = 0; c < PIX; c++) {
      dst[PIX * p + c] = top[PIX * p + c] + dst[PIX * p + c] * (1.0 - a);
    }
  }
}

static void prim_composite(uint32_t op, const double k[4], const double * i1, const double * i2, double * out, size_t pixels) {
  for (size_t p = 0; p < pixels; p++) {
    const double * a = &i1[PIX * p];
    const double * b = &i2[PIX * p];
    double * o = &out[PIX * p];
    for (size_t c = 0; c < PIX; c++) {
      switch (op) {
        case GSHSCENE_FILTER_COMPOSITE_OVER:
          o[c] = a[c] + b[c] * (1.0 - a[3]);
          break;
        case GSHSCENE_FILTER_COMPOSITE_IN:
          o[c] = a[c] * b[3];
          break;
        case GSHSCENE_FILTER_COMPOSITE_OUT:
          o[c] = a[c] * (1.0 - b[3]);
          break;
        case GSHSCENE_FILTER_COMPOSITE_ATOP:
          o[c] = a[c] * b[3] + b[c] * (1.0 - a[3]);
          break;
        case GSHSCENE_FILTER_COMPOSITE_XOR:
          o[c] = a[c] * (1.0 - b[3]) + b[c] * (1.0 - a[3]);
          break;
        default: /* ARITHMETIC */
          o[c] = k[0] * a[c] * b[c] + k[1] * a[c] + k[2] * b[c] + k[3];
          break;
      }
    }
    if (op == (uint32_t)GSHSCENE_FILTER_COMPOSITE_ARITHMETIC) {
      o[3] = clamp01(o[3]);
      for (size_t c = 0; c < 3; c++) {
        o[c] = !(o[c] > 0.0) ? 0.0 : (o[c] > o[3] ? o[3] : o[c]);
      }
    }
  }
}

static void unpremultiply4(const double * p, double c[4]) {
  const double a = p[3];
  if (a == 0.0) {
    c[0] = c[1] = c[2] = c[3] = 0.0;
    return;
  }
  c[0] = p[0] / a;
  c[1] = p[1] / a;
  c[2] = p[2] / a;
  c[3] = a;
}

static void premultiply4(const double c[4], double * p) {
  p[0] = c[0] * c[3];
  p[1] = c[1] * c[3];
  p[2] = c[2] * c[3];
  p[3] = c[3];
}

static void prim_matrix(const GSHSCENE_Filter_Primitive * pr, const double * in, double * out, size_t pixels) {
  double m[20];
  memset(m, 0, sizeof m);
  switch (pr->mode) {
    case GSHSCENE_FILTER_MATRIX_MATRIX:
      memcpy(m, pr->values, sizeof m);
      break;
    case GSHSCENE_FILTER_MATRIX_SATURATE: {
      const double s = pr->values[0];
      const double r0[3] = {0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s};
      const double r1[3] = {0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s};
      const double r2[3] = {0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s};
      memcpy(&m[0], r0, sizeof r0);
      memcpy(&m[5], r1, sizeof r1);
      memcpy(&m[10], r2, sizeof r2);
      m[18] = 1.0;
      break;
    }
    case GSHSCENE_FILTER_MATRIX_HUE_ROTATE: {
      const double th = pr->values[0] * (3.14159265358979323846 / 180.0);
      const double c = gmath_cos(th);
      const double s = gmath_sin(th);
      const double r0[3] = {0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715, 0.072 - c * 0.072 + s * 0.928};
      const double r1[3] = {0.213 - c * 0.213 + s * 0.143, 0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283};
      const double r2[3] = {0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715, 0.072 + c * 0.928 + s * 0.072};
      memcpy(&m[0], r0, sizeof r0);
      memcpy(&m[5], r1, sizeof r1);
      memcpy(&m[10], r2, sizeof r2);
      m[18] = 1.0;
      break;
    }
    default: /* LUMINANCE_TO_ALPHA */
      m[15] = 0.2125;
      m[16] = 0.7154;
      m[17] = 0.0721;
      break;
  }
  for (size_t p = 0; p < pixels; p++) {
    double u[4], v[4];
    unpremultiply4(&in[PIX * p], u);
    for (int row = 0; row < 4; row++) {
      const double * r = &m[5 * row];
      v[row] = clamp01(r[0] * u[0] + r[1] * u[1] + r[2] * u[2] + r[3] * u[3] + r[4]);
    }
    premultiply4(v, &out[PIX * p]);
  }
}

static double transfer_value(const GSHSCENE_Filter_Transfer * t, double c) {
  switch (t->type) {
    case GSHSCENE_FILTER_TRANSFER_TABLE: {
      const size_t n = t->count;
      if (n == 1) {
        return t->table[0];
      }
      double k = gmath_floor(c * (double)(n - 1));
      if (k > (double)(n - 2)) {
        k = (double)(n - 2);
      }
      const size_t ki = (size_t)k;
      return t->table[ki] + (c - (double)ki / (double)(n - 1)) * (double)(n - 1) * (t->table[ki + 1] - t->table[ki]);
    }
    case GSHSCENE_FILTER_TRANSFER_DISCRETE: {
      const size_t n = t->count;
      double k = gmath_floor(c * (double)n);
      if (k > (double)(n - 1)) {
        k = (double)(n - 1);
      }
      return t->table[(size_t)k];
    }
    case GSHSCENE_FILTER_TRANSFER_LINEAR:
      return t->slope * c + t->intercept;
    case GSHSCENE_FILTER_TRANSFER_GAMMA:
      return t->amplitude * gmath_pow(c, t->exponent) + t->offset;
    default:
      return c;
  }
}

static void prim_transfer(const GSHSCENE_Filter_Primitive * pr, const double * in, double * out, size_t pixels) {
  for (size_t p = 0; p < pixels; p++) {
    double u[4], v[4];
    unpremultiply4(&in[PIX * p], u);
    for (int c = 0; c < 4; c++) {
      v[c] = clamp01(transfer_value(&pr->transfer[c], u[c]));
    }
    premultiply4(v, &out[PIX * p]);
  }
}

/* --- blur (D-FL9) ------------------------------------------------------------------------------------------------------ */

/** A value of a line of @p n at index @p j, which may lie outside it: transparent black, the nearest end or the other end (D-FL9). */
static double edge_get(const double * in, size_t n, long j, uint32_t mode) {
  if (j >= 0 && (size_t)j < n) {
    return in[j];
  }
  switch (mode) {
    case GSHSCENE_FILTER_EDGE_DUPLICATE:
      return j < 0 ? in[0] : in[n - 1u];
    case GSHSCENE_FILTER_EDGE_WRAP: {
      long m = j % (long)n;
      return in[m < 0 ? m + (long)n : m];
    }
    default:
      return 0.0;
  }
}

/** `out[i] = sum_{t = 0}^{w - 1} in[i - off + t] / w`, the edge by @p mode. */
static void box_line(const double * in, double * out, size_t n, size_t w, size_t off, uint32_t mode) {
  double sum = 0.0;
  for (size_t t = 0; t < w; t++) {
    sum += edge_get(in, n, (long)t - (long)off, mode);
  }
  out[0] = sum / (double)w;
  for (size_t i = 1; i < n; i++) {
    sum += edge_get(in, n, (long)i - (long)off + (long)w - 1, mode);
    sum -= edge_get(in, n, (long)i - 1 - (long)off, mode);
    out[i] = sum / (double)w;
  }
}

/** The truncated Gaussian of deviation @p s (D-FL9), radius `ceil(3 s)`; @p ker has room for `2 r + 1` weights. */
static void gauss_line(const double * in, double * out, size_t n, double s, double * ker, size_t r, uint32_t mode) {
  double total = 0.0;
  for (size_t k = 0; k <= 2 * r; k++) {
    const double x = (double)k - (double)r;
    ker[k] = x == 0.0 ? 1.0 : gmath_exp(-(x * x) / (2.0 * s * s));
  }
  for (size_t k = 0; k <= 2 * r; k++) {
    total += ker[k];
  }
  for (size_t k = 0; k <= 2 * r; k++) {
    ker[k] /= total;
  }
  for (size_t i = 0; i < n; i++) {
    double acc = 0.0;
    for (size_t k = 0; k <= 2 * r; k++) {
      acc += ker[k] * edge_get(in, n, (long)i + (long)k - (long)r, mode);
    }
    out[i] = acc;
  }
}

/**
 * One axis of a blur over a line of @p n values (@p a and @p b are two work lines of at least @p n; the result is left in @p a).
 * Gaussian weights for `s < 2` need `2 ceil(3 s) + 1 <= 13` entries, held in @p ker.
 */
static void blur_line(double * a, double * b, size_t n, double s, double * ker, uint32_t mode) {
  if (s >= 2.0) {
    const double dd = gmath_floor(s * 3.0 * gmath_sqrt(2.0 * 3.14159265358979323846) / 4.0 + 0.5);
    const size_t d = (size_t)dd;
    if (d % 2 == 1) {
      box_line(a, b, n, d, (d - 1) / 2, mode);
      box_line(b, a, n, d, (d - 1) / 2, mode);
      box_line(a, b, n, d, (d - 1) / 2, mode);
      memcpy(a, b, n * sizeof(double));
    } else {
      box_line(a, b, n, d, d / 2, mode);
      box_line(b, a, n, d, d / 2 - 1, mode);
      box_line(a, b, n, d + 1, d / 2, mode);
      memcpy(a, b, n * sizeof(double));
    }
    return;
  }
  const size_t r = (size_t)gmath_ceil(3.0 * s);
  gauss_line(a, b, n, s, ker, r, mode);
  memcpy(a, b, n * sizeof(double));
}

/**
 * Blur @p buf in place: horizontal by @p sx then vertical by @p sy, device pixels, a deviation of 0 skipping its axis.  With the edge `NONE` every
 * line is blurred whole (the blur spreads past the input); with another edge only the pixels `[x0, x1) x [y0, y1)` (the input's subregion) are,
 * reading beyond them by the mode (D-FL9).
 */
static void prim_blur(double * buf, size_t w, size_t h, double sx, double sy, uint32_t mode, const size_t span[4], double * line_a,
    double * line_b) {
  double ker[16];
  const size_t x0 = mode == (uint32_t)GSHSCENE_FILTER_EDGE_NONE ? 0 : span[0];
  const size_t x1 = mode == (uint32_t)GSHSCENE_FILTER_EDGE_NONE ? w : span[2];
  const size_t y0 = mode == (uint32_t)GSHSCENE_FILTER_EDGE_NONE ? 0 : span[1];
  const size_t y1 = mode == (uint32_t)GSHSCENE_FILTER_EDGE_NONE ? h : span[3];
  if (x1 <= x0 || y1 <= y0) {
    return;
  }
  for (size_t c = 0; c < PIX; c++) {
    if (sx != 0.0) {
      for (size_t j = y0; j < y1; j++) {
        for (size_t i = x0; i < x1; i++) {
          line_a[i - x0] = buf[PIX * (j * w + i) + c];
        }
        blur_line(line_a, line_b, x1 - x0, sx, ker, mode);
        for (size_t i = x0; i < x1; i++) {
          buf[PIX * (j * w + i) + c] = line_a[i - x0];
        }
      }
    }
    if (sy != 0.0) {
      for (size_t i = x0; i < x1; i++) {
        for (size_t j = y0; j < y1; j++) {
          line_a[j - y0] = buf[PIX * (j * w + i) + c];
        }
        blur_line(line_a, line_b, y1 - y0, sy, ker, mode);
        for (size_t j = y0; j < y1; j++) {
          buf[PIX * (j * w + i) + c] = line_a[j - y0];
        }
      }
    }
  }
}

/* --- morphology, convolution, tile, displacement (stage 3) ------------------------------------------------------------ */

/** A radius or length in device pixels as whole pixels (round half up; FL17). */
static long whole_px(double v) {
  return (long)gmath_floor(v + 0.5);
}

/**
 * The minimum or maximum of `in[j - r .. j + r]` for each `j` of a line of @p n values, a window that leaves the line reading transparent black
 * (so an erosion is 0 there and a dilation does not see it): a sliding window by a monotone queue of indices, held in @p queue as doubles.
 */
static void morph_line(const double * in, double * out, size_t n, size_t r, int dilate, double * queue) {
  size_t head = 0, tail = 0; /* queue[head .. tail) */
  for (size_t i = 0; i < n + r; i++) {
    if (i < n) {
      while (tail > head) {
        const double last = in[(size_t)queue[tail - 1u]];
        if (dilate ? last <= in[i] : last >= in[i]) {
          tail--;
        } else {
          break;
        }
      }
      queue[tail++] = (double)i;
    }
    if (i >= r) {
      const size_t j = i - r;
      while ((size_t)queue[head] + r < j) {
        head++;
      }
      out[j] = (!dilate && (j < r || j + r >= n)) ? 0.0 : in[(size_t)queue[head]];
    }
  }
}

static void prim_morphology(double * buf, size_t w, size_t h, size_t rx, size_t ry, int dilate, double * line_a, double * line_b,
    double * queue) {
  for (size_t c = 0; c < PIX; c++) {
    if (rx > 0) {
      for (size_t j = 0; j < h; j++) {
        for (size_t i = 0; i < w; i++) {
          line_a[i] = buf[PIX * (j * w + i) + c];
        }
        morph_line(line_a, line_b, w, rx, dilate, queue);
        for (size_t i = 0; i < w; i++) {
          buf[PIX * (j * w + i) + c] = line_b[i];
        }
      }
    }
    if (ry > 0) {
      for (size_t i = 0; i < w; i++) {
        for (size_t j = 0; j < h; j++) {
          line_a[j] = buf[PIX * (j * w + i) + c];
        }
        morph_line(line_a, line_b, h, ry, dilate, queue);
        for (size_t j = 0; j < h; j++) {
          buf[PIX * (j * w + i) + c] = line_b[j];
        }
      }
    }
  }
}

/** The pixel (@p x, @p y) of @p in under the edge mode over the rectangle @p r (`x0 y0 x1 y1`); transparent black outside it for `NONE`. */
static void edge_pixel(const double * in, size_t w, const long r[4], long x, long y, uint32_t mode, double out[4]) {
  if (x < r[0] || x >= r[2] || y < r[1] || y >= r[3]) {
    if (mode == (uint32_t)GSHSCENE_FILTER_EDGE_DUPLICATE) {
      x = x < r[0] ? r[0] : (x >= r[2] ? r[2] - 1 : x);
      y = y < r[1] ? r[1] : (y >= r[3] ? r[3] - 1 : y);
    } else if (mode == (uint32_t)GSHSCENE_FILTER_EDGE_WRAP) {
      long mx = (x - r[0]) % (r[2] - r[0]);
      long my = (y - r[1]) % (r[3] - r[1]);
      x = r[0] + (mx < 0 ? mx + (r[2] - r[0]) : mx);
      y = r[1] + (my < 0 ? my + (r[3] - r[1]) : my);
    } else {
      out[0] = out[1] = out[2] = out[3] = 0.0;
      return;
    }
  }
  memcpy(out, &in[PIX * ((size_t)y * w + (size_t)x)], PIX * sizeof(double));
}

static void prim_convolve(const GSHSCENE_Filter_Primitive * pr, const double * in, double * out, size_t w, size_t h, const long r[4]) {
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      double sum[4] = {0.0, 0.0, 0.0, 0.0};
      for (uint32_t ky = 0; ky < pr->order_y; ky++) {
        for (uint32_t kx = 0; kx < pr->order_x; kx++) {
          double p[4];
          edge_pixel(in, w, r, (long)i - (long)pr->target_x + (long)kx, (long)j - (long)pr->target_y + (long)ky, pr->edge_mode, p);
          if (pr->preserve_alpha) {
            double u[4];
            unpremultiply4(p, u);
            memcpy(p, u, sizeof p);
          }
          const double k = pr->values[(size_t)(pr->order_y - 1u - ky) * pr->order_x + (pr->order_x - 1u - kx)];
          for (int c = 0; c < 4; c++) {
            sum[c] += p[c] * k;
          }
        }
      }
      double * o = &out[PIX * (j * w + i)];
      if (pr->preserve_alpha) {
        const double a = in[PIX * (j * w + i) + 3];
        double u[4] = {clamp01(sum[0] / pr->divisor + pr->bias), clamp01(sum[1] / pr->divisor + pr->bias),
            clamp01(sum[2] / pr->divisor + pr->bias), a};
        premultiply4(u, o);
      } else {
        const double a = clamp01(sum[3] / pr->divisor + pr->bias);
        o[3] = a;
        for (int c = 0; c < 3; c++) {
          const double v = sum[c] / pr->divisor + pr->bias * a;
          o[c] = !(v > 0.0) ? 0.0 : (v > a ? a : v);
        }
      }
    }
  }
}

static void prim_tile(const double * in, double * out, size_t w, size_t h, const long r[4]) {
  const long rw = r[2] - r[0];
  const long rh = r[3] - r[1];
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      double * o = &out[PIX * (j * w + i)];
      if (rw <= 0 || rh <= 0) {
        o[0] = o[1] = o[2] = o[3] = 0.0;
        continue;
      }
      long mx = ((long)i - r[0]) % rw;
      long my = ((long)j - r[1]) % rh;
      mx = r[0] + (mx < 0 ? mx + rw : mx);
      my = r[1] + (my < 0 ? my + rh : my);
      memcpy(o, &in[PIX * ((size_t)my * w + (size_t)mx)], PIX * sizeof(double));
    }
  }
}

static void prim_displace(const GSHSCENE_Filter_Primitive * pr, const double * in, const double * map, double * out, size_t w, size_t h,
    double scale_x, double scale_y) {
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      double u[4];
      unpremultiply4(&map[PIX * (j * w + i)], u);
      const double xs = (double)i + scale_x * (u[pr->x_channel] - 0.5);
      const double ys = (double)j + scale_y * (u[pr->y_channel] - 0.5);
      const long sx = whole_px(xs);
      const long sy = whole_px(ys);
      double * o = &out[PIX * (j * w + i)];
      if (sx >= 0 && sy >= 0 && (size_t)sx < w && (size_t)sy < h) {
        memcpy(o, &in[PIX * ((size_t)sy * w + (size_t)sx)], PIX * sizeof(double));
      } else {
        o[0] = o[1] = o[2] = o[3] = 0.0;
      }
    }
  }
}

/* --- turbulence and lighting (stage 4) ---------------------------------------------------------------------------------- */

#define NOISE_B 0x100
#define NOISE_BM 0xff
#define NOISE_N 0x1000
#define RAND_M 2147483647L
#define RAND_A 16807L
#define RAND_Q 127773L
#define RAND_R 2836L

typedef struct Noise {
  int lattice[NOISE_B + NOISE_B + 2];
  double grad[4][NOISE_B + NOISE_B + 2][2];
} Noise;

typedef struct Stitch {
  int w, h, wrap_x, wrap_y;
} Stitch;

static long noise_random(long seed) {
  long r = RAND_A * (seed % RAND_Q) - RAND_R * (seed / RAND_Q);
  return r <= 0 ? r + RAND_M : r;
}

/** The reference implementation of the standard's turbulence: the lattice and the gradients from the seed. */
static void noise_init(Noise * nz, double seed_value) {
  long seed = (long)(seed_value < 0 ? gmath_ceil(seed_value - 0.5) : gmath_floor(seed_value + 0.5)); /* the seed is rounded (FL18) */
  if (seed <= 0) {
    seed = -(seed % (RAND_M - 1)) + 1;
  }
  if (seed > RAND_M - 1) {
    seed = RAND_M - 1;
  }
  for (int k = 0; k < 4; k++) {
    for (int i = 0; i < NOISE_B; i++) {
      nz->lattice[i] = i;
      for (int j = 0; j < 2; j++) {
        seed = noise_random(seed);
        nz->grad[k][i][j] = (double)((seed % (NOISE_B + NOISE_B)) - NOISE_B) / NOISE_B;
      }
      const double len = gmath_sqrt(nz->grad[k][i][0] * nz->grad[k][i][0] + nz->grad[k][i][1] * nz->grad[k][i][1]);
      if (len > 0.0) {
        nz->grad[k][i][0] /= len;
        nz->grad[k][i][1] /= len;
      }
    }
  }
  for (int i = NOISE_B - 1; i > 0; i--) {
    const int k = nz->lattice[i];
    seed = noise_random(seed);
    const int j = (int)(seed % NOISE_B);
    nz->lattice[i] = nz->lattice[j];
    nz->lattice[j] = k;
  }
  for (int i = 0; i < NOISE_B + 2; i++) {
    nz->lattice[NOISE_B + i] = nz->lattice[i];
    for (int k = 0; k < 4; k++) {
      nz->grad[k][NOISE_B + i][0] = nz->grad[k][i][0];
      nz->grad[k][NOISE_B + i][1] = nz->grad[k][i][1];
    }
  }
}

static double s_curve(double t) {
  return t * t * (3.0 - 2.0 * t);
}

static double lerp(double t, double a, double b) {
  return a + t * (b - a);
}

static double noise2(const Noise * nz, int ch, double vx, double vy, const Stitch * st) {
  double t = vx + NOISE_N;
  int bx0 = (int)t;
  int bx1 = bx0 + 1;
  const double rx0 = t - (double)(int)t, rx1 = rx0 - 1.0;
  t = vy + NOISE_N;
  int by0 = (int)t;
  int by1 = by0 + 1;
  const double ry0 = t - (double)(int)t, ry1 = ry0 - 1.0;
  if (st) {
    if (bx0 >= st->wrap_x) {
      bx0 -= st->w;
    }
    if (bx1 >= st->wrap_x) {
      bx1 -= st->w;
    }
    if (by0 >= st->wrap_y) {
      by0 -= st->h;
    }
    if (by1 >= st->wrap_y) {
      by1 -= st->h;
    }
  }
  bx0 &= NOISE_BM;
  bx1 &= NOISE_BM;
  by0 &= NOISE_BM;
  by1 &= NOISE_BM;
  const int i = nz->lattice[bx0], j = nz->lattice[bx1];
  const int b00 = nz->lattice[i + by0], b10 = nz->lattice[j + by0], b01 = nz->lattice[i + by1], b11 = nz->lattice[j + by1];
  const double sx = s_curve(rx0), sy = s_curve(ry0);
  const double (*g)[2] = nz->grad[ch];
  double u = rx0 * g[b00][0] + ry0 * g[b00][1];
  double v = rx1 * g[b10][0] + ry0 * g[b10][1];
  const double a = lerp(sx, u, v);
  u = rx0 * g[b01][0] + ry1 * g[b01][1];
  v = rx1 * g[b11][0] + ry1 * g[b11][1];
  const double b = lerp(sx, u, v);
  return lerp(sy, a, b);
}

static double turbulence_sum(const Noise * nz, int ch, double px, double py, double fx, double fy, uint32_t octaves, int fractal, int stitch,
    Rect2 tile) {
  Stitch st;
  Stitch * stp = NULL;
  const double tw = tile.x1 - tile.x0, th = tile.y1 - tile.y0;
  if (stitch) {
    if (fx != 0.0) {
      const double lo = gmath_floor(tw * fx) / tw, hi = gmath_ceil(tw * fx) / tw;
      fx = (lo > 0.0 && fx / lo < hi / fx) ? lo : hi;
    }
    if (fy != 0.0) {
      const double lo = gmath_floor(th * fy) / th, hi = gmath_ceil(th * fy) / th;
      fy = (lo > 0.0 && fy / lo < hi / fy) ? lo : hi;
    }
    st.w = (int)(tw * fx + 0.5);
    st.h = (int)(th * fy + 0.5);
    st.wrap_x = (int)(tile.x0 * fx + NOISE_N + st.w);
    st.wrap_y = (int)(tile.y0 * fy + NOISE_N + st.h);
    stp = &st;
  }
  double sum = 0.0, ratio = 1.0;
  double vx = px * fx, vy = py * fy;
  for (uint32_t o = 0; o < octaves; o++) {
    const double nv = noise2(nz, ch, vx, vy, stp);
    sum += (fractal ? nv : gmath_fabs(nv)) / ratio;
    vx *= 2.0;
    vy *= 2.0;
    ratio *= 2.0;
    if (stp) {
      stp->w *= 2;
      stp->wrap_x = 2 * stp->wrap_x - NOISE_N;
      stp->h *= 2;
      stp->wrap_y = 2 * stp->wrap_y - NOISE_N;
    }
  }
  return sum;
}

static void prim_turbulence(const GSHSCENE_Filter_Primitive * pr, const Aff * m, GSHCPU_Rect canvas, size_t w, size_t h, Rect2 tile, double * out) {
  Noise * nz = NULL;
  Noise local;
  nz = &local;
  noise_init(nz, pr->seed);
  const int fractal = pr->mode == (uint32_t)GSHSCENE_FILTER_NOISE_FRACTAL;
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      double ux, uy;
      aff_inverse_point(m, gshcpu_render_i2d(canvas.x0) + (double)i, gshcpu_render_i2d(canvas.y0) + (double)j, &ux, &uy);
      double c[4];
      for (int ch = 0; ch < 4; ch++) {
        double v = turbulence_sum(nz, ch, ux, uy, pr->sx, pr->sy, pr->octaves, fractal, (int)pr->stitch_tiles, tile);
        v = fractal ? (v + 1.0) / 2.0 : v;
        c[ch] = clamp01(v);
      }
      premultiply4(c, &out[PIX * (j * w + i)]);
    }
  }
}

static double height_at(const double * in, size_t w, const long r[4], long x, long y) {
  (void)r;
  return in[PIX * ((size_t)y * w + (size_t)x) + 3];
}

/** The surface normal's x and y (before the surface scale) at (@p x, @p y) of the image rectangle @p r by the standard's table of kernels. */
static void surface_normal(const double * in, size_t w, const long r[4], long x, long y, double ss, double * nx, double * ny) {
  const int left = x == r[0], right = x == r[2] - 1, top = y == r[1], bottom = y == r[3] - 1;
#define I(dx, dy) height_at(in, w, r, x + (dx), y + (dy))
  double fx, fy, ax, ay;
  if (r[2] - r[0] < 2 || r[3] - r[1] < 2) {
    *nx = *ny = 0.0;
    return;
  }
  if (top && left) {
    fx = 2.0 / 3.0; fy = 2.0 / 3.0;
    ax = (2 * I(1, 0) + I(1, 1)) - (2 * I(0, 0) + I(0, 1));
    ay = (2 * I(0, 1) + I(1, 1)) - (2 * I(0, 0) + I(1, 0));
  } else if (top && right) {
    fx = 2.0 / 3.0; fy = 2.0 / 3.0;
    ax = (2 * I(0, 0) + I(0, 1)) - (2 * I(-1, 0) + I(-1, 1));
    ay = (I(-1, 1) + 2 * I(0, 1)) - (I(-1, 0) + 2 * I(0, 0));
  } else if (top) {
    fx = 1.0 / 3.0; fy = 1.0 / 2.0;
    ax = (2 * I(1, 0) + I(1, 1)) - (2 * I(-1, 0) + I(-1, 1));
    ay = (I(-1, 1) + 2 * I(0, 1) + I(1, 1)) - (I(-1, 0) + 2 * I(0, 0) + I(1, 0));
  } else if (bottom && left) {
    fx = 2.0 / 3.0; fy = 2.0 / 3.0;
    ax = (I(1, -1) + 2 * I(1, 0)) - (I(0, -1) + 2 * I(0, 0));
    ay = (2 * I(0, 0) + I(1, 0)) - (2 * I(0, -1) + I(1, -1));
  } else if (bottom && right) {
    fx = 2.0 / 3.0; fy = 2.0 / 3.0;
    ax = (I(0, -1) + 2 * I(0, 0)) - (I(-1, -1) + 2 * I(-1, 0));
    ay = (I(-1, 0) + 2 * I(0, 0)) - (I(-1, -1) + 2 * I(0, -1));
  } else if (bottom) {
    fx = 1.0 / 3.0; fy = 1.0 / 2.0;
    ax = (I(1, -1) + 2 * I(1, 0)) - (I(-1, -1) + 2 * I(-1, 0));
    ay = (I(-1, 0) + 2 * I(0, 0) + I(1, 0)) - (I(-1, -1) + 2 * I(0, -1) + I(1, -1));
  } else if (left) {
    fx = 1.0 / 2.0; fy = 1.0 / 3.0;
    ax = (I(1, -1) + 2 * I(1, 0) + I(1, 1)) - (I(0, -1) + 2 * I(0, 0) + I(0, 1));
    ay = (2 * I(0, 1) + I(1, 1)) - (2 * I(0, -1) + I(1, -1));
  } else if (right) {
    fx = 1.0 / 2.0; fy = 1.0 / 3.0;
    ax = (I(0, -1) + 2 * I(0, 0) + I(0, 1)) - (I(-1, -1) + 2 * I(-1, 0) + I(-1, 1));
    ay = (I(-1, 1) + 2 * I(0, 1)) - (I(-1, -1) + 2 * I(0, -1));
  } else {
    fx = 1.0 / 4.0; fy = 1.0 / 4.0;
    ax = (I(1, -1) + 2 * I(1, 0) + I(1, 1)) - (I(-1, -1) + 2 * I(-1, 0) + I(-1, 1));
    ay = (I(-1, 1) + 2 * I(0, 1) + I(1, 1)) - (I(-1, -1) + 2 * I(0, -1) + I(1, -1));
  }
#undef I
  *nx = -ss * fx * ax;
  *ny = -ss * fy * ay;
}

static void prim_lighting(const GSHSCENE_Filter_Primitive * pr, const Aff * m, GSHCPU_Rect canvas, const double * in, double * out, size_t w, size_t h,
    const long r[4], const double color[3]) {
  const int specular = pr->kind == GSHSCENE_FILTER_SPECULAR_LIGHTING;
  const GSHSCENE_Filter_Light * lt = &pr->light;
  /* a light's position in the filter's pixels: x and y through the map, z by the root of the product of the axis scales */
  double lx = 0, ly = 0, lz = 0, px = 0, py = 0, pz = 0;
  if (lt->kind != (uint32_t)GSHSCENE_FILTER_LIGHT_DISTANT) {
    aff_point(m, lt->position[0], lt->position[1], &lx, &ly);
    double sx, sy;
    device_lengths(m, 1.0, 1.0, &sx, &sy);
    const double zs = gmath_sqrt(sx * sy);
    lz = lt->position[2] * zs;
    aff_point(m, lt->points_at[0], lt->points_at[1], &px, &py);
    pz = lt->points_at[2] * zs;
  }
  double dl[3] = {0, 0, 1};
  if (lt->kind == (uint32_t)GSHSCENE_FILTER_LIGHT_DISTANT) {
    const double az = lt->azimuth * (3.14159265358979323846 / 180.0), el = lt->elevation * (3.14159265358979323846 / 180.0);
    dl[0] = gmath_cos(az) * gmath_cos(el);
    dl[1] = gmath_sin(az) * gmath_cos(el);
    dl[2] = gmath_sin(el);
  }
  double sdir[3] = {0, 0, 0};
  if (lt->kind == (uint32_t)GSHSCENE_FILTER_LIGHT_SPOT) {
    sdir[0] = px - lx;
    sdir[1] = py - ly;
    sdir[2] = pz - lz;
    const double sl = gmath_sqrt(sdir[0] * sdir[0] + sdir[1] * sdir[1] + sdir[2] * sdir[2]);
    if (sl > 0.0) {
      sdir[0] /= sl;
      sdir[1] /= sl;
      sdir[2] /= sl;
    }
  }
  for (size_t j = 0; j < h; j++) {
    for (size_t i = 0; i < w; i++) {
      double * o = &out[PIX * (j * w + i)];
      o[0] = o[1] = o[2] = o[3] = 0.0;
      const long x = (long)i, y = (long)j;
      if (x < r[0] || x >= r[2] || y < r[1] || y >= r[3]) {
        continue;
      }
      double nx, ny;
      surface_normal(in, w, r, x, y, pr->surface_scale, &nx, &ny);
      const double nl = gmath_sqrt(nx * nx + ny * ny + 1.0);
      const double n[3] = {nx / nl, ny / nl, 1.0 / nl};
      double l[3];
      double lc[3] = {color[0], color[1], color[2]};
      if (lt->kind == (uint32_t)GSHSCENE_FILTER_LIGHT_DISTANT) {
        memcpy(l, dl, sizeof l);
      } else {
        const double z = pr->surface_scale * height_at(in, w, r, x, y);
        l[0] = lx - (gshcpu_render_i2d(canvas.x0) + (double)x);
        l[1] = ly - (gshcpu_render_i2d(canvas.y0) + (double)y);
        l[2] = lz - z;
        const double ll = gmath_sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        if (ll > 0.0) {
          l[0] /= ll;
          l[1] /= ll;
          l[2] /= ll;
        }
        if (lt->kind == (uint32_t)GSHSCENE_FILTER_LIGHT_SPOT) {
          const double sdot = -(l[0] * sdir[0] + l[1] * sdir[1] + l[2] * sdir[2]);
          double f = 0.0;
          if (sdot > 0.0 && !(lt->has_cone && sdot < gmath_cos(gmath_fabs(lt->cone_angle) * (3.14159265358979323846 / 180.0)))) {
            f = gmath_pow(sdot, lt->spot_exponent);
          }
          for (int c = 0; c < 3; c++) {
            lc[c] *= f;
          }
        }
      }
      if (!specular) {
        const double ndl = n[0] * l[0] + n[1] * l[1] + n[2] * l[2];
        const double f = pr->constant * (ndl > 0.0 ? ndl : 0.0);
        for (int c = 0; c < 3; c++) {
          o[c] = clamp01(f * lc[c]);
        }
        o[3] = 1.0;
      } else {
        double hv[3] = {l[0], l[1], l[2] + 1.0};
        const double hl = gmath_sqrt(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]);
        double f = 0.0;
        if (hl > 0.0) {
          const double ndh = (n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2]) / hl;
          f = pr->constant * (ndh > 0.0 ? gmath_pow(ndh, pr->exponent) : 0.0);
        }
        double a = 0.0;
        for (int c = 0; c < 3; c++) {
          o[c] = clamp01(f * lc[c]);
          a = o[c] > a ? o[c] : a;
        }
        o[3] = a;
      }
    }
  }
}

/* --- the executor ----------------------------------------------------------------------------------------------------- */

typedef struct Env {
  const GSHSCENE_Filter * f;
  Aff m;
  GSHCPU_Rect canvas;
  size_t w, h, pixels;
  GSHSCENE_Color_Space layer_space;
  double * layer;
  double * slots; /* plan.slots buffers */
  double * temp[TEMPS];
  double * wx;
  double * wy;
  double * line_a;
  double * line_b;
  Plan plan;
  Rect2 region_u;
  Rect2 sub[GSHSCENE_FILTER_MAX_PRIMITIVES];
  GSHSCENE_Color_Space space[GSHSCENE_FILTER_MAX_PRIMITIVES];
} Env;

/** The buffer of result @p j. */
static double * result_buf(Env * e, size_t j) {
  return &e->slots[e->plan.slot[j] * PIX * e->pixels];
}

static int prim_space_is(const GSHSCENE_Color_Space * a, const GSHSCENE_Color_Space * b) {
  return a->primaries == b->primaries && a->transfer == b->transfer;
}

/**
 * Input @p k of primitive @p i as a buffer in that primitive's space: the layer, a result or a temporary (@p tn the temporary to use if one
 * is needed), and the input's subregion in @p sub (user space).
 */
static const double * fetch(Env * e, size_t i, size_t k, size_t tn, Rect2 * sub) {
  const GSHSCENE_Filter_Primitive * pr = &e->f->primitives[i];
  const GSHSCENE_Filter_Input * in = &pr->inputs[k];
  const GSHSCENE_Color_Space * want = &e->space[i];
  size_t j;
  if (in->kind == (uint32_t)GSHSCENE_FILTER_INPUT_SOURCE_ALPHA) {
    double * t = e->temp[tn];
    for (size_t p = 0; p < e->pixels; p++) {
      t[PIX * p] = t[PIX * p + 1] = t[PIX * p + 2] = 0.0;
      t[PIX * p + 3] = e->layer[PIX * p + 3];
    }
    *sub = e->region_u;
    return t;
  }
  const double * src;
  const GSHSCENE_Color_Space * have;
  if (input_result(in, i, &j)) {
    src = result_buf(e, j);
    have = &e->space[j];
    *sub = e->sub[j];
  } else {
    src = e->layer;
    have = &e->layer_space;
    *sub = e->region_u;
  }
  if (prim_space_is(have, want)) {
    return src;
  }
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, have, want);
  double * t = e->temp[tn];
  convert_buffer(&c, src, t, e->pixels);
  return t;
}

static Rect2 default_subregion(const Rect2 * subs, size_t n, Rect2 region) {
  if (n == 0) {
    return region;
  }
  Rect2 r = subs[0];
  for (size_t k = 1; k < n; k++) {
    r.x0 = dmin(r.x0, subs[k].x0);
    r.y0 = dmin(r.y0, subs[k].y0);
    r.x1 = dmax(r.x1, subs[k].x1);
    r.y1 = dmax(r.y1, subs[k].y1);
  }
  return r;
}

/** The premultiplied color of a flood or a shadow in space @p space, times @p opacity. */
static void flood_color(const Env * e, const GSHSCENE_Filter_Primitive * pr, const GSHSCENE_Color_Space * space, double out[4]) {
  const GSHSCENE_Color_Space from = gshscene_color_space_is_target(&pr->color.space) ? e->layer_space : pr->color.space;
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &from, space);
  double straight[3] = {pr->color.rgba[0], pr->color.rgba[1], pr->color.rgba[2]};
  gshcpu_render_convert_straight(&c, straight, straight);
  const double a = pr->color.rgba[3];
  out[0] = straight[0] * a * pr->opacity;
  out[1] = straight[1] * a * pr->opacity;
  out[2] = straight[2] * a * pr->opacity;
  out[3] = a * pr->opacity;
}

/**
 * A subregion (user space) as whole canvas pixels `x0 y0 x1 y1` relative to the canvas: outward (`round` 0) or to the nearest pixel edge
 * (`round` 1, the tile's), met with the canvas.
 */
static void pixel_rect(const Env * e, Rect2 u, int round, long out[4]) {
  const Rect2 d = rect_to_device(&e->m, u);
  const double o[4] = {d.x0 - gshcpu_render_i2d(e->canvas.x0), d.y0 - gshcpu_render_i2d(e->canvas.y0), d.x1 - gshcpu_render_i2d(e->canvas.x0), d.y1 - gshcpu_render_i2d(e->canvas.y0)};
  for (int k = 0; k < 4; k++) {
    double v = round ? gmath_floor(o[k] + 0.5) : (k < 2 ? gmath_floor(o[k]) : gmath_ceil(o[k]));
    const double lim = (double)(k % 2 == 0 ? e->w : e->h);
    v = v < 0.0 ? 0.0 : (v > lim ? lim : v);
    out[k] = (long)v;
  }
}

static void pixel_span(const Env * e, Rect2 u, size_t out[4]) {
  long r[4];
  pixel_rect(e, u, 0, r);
  for (int k = 0; k < 4; k++) {
    out[k] = (size_t)r[k];
  }
}

/** The primitive's own x, y, width and height (user space) over the default @p base (FL4). */
static Rect2 apply_subregion(const GSHSCENE_Filter_Primitive * pr, Rect2 base) {
  Rect2 sub = base;
  if (pr->subregion_mask & 1u) {
    sub.x0 = pr->subregion[0];
  }
  if (pr->subregion_mask & 2u) {
    sub.y0 = pr->subregion[1];
  }
  if (pr->subregion_mask & 4u) {
    sub.x1 = sub.x0 + pr->subregion[2];
  }
  if (pr->subregion_mask & 8u) {
    sub.y1 = sub.y0 + pr->subregion[3];
  }
  return sub;
}

/** feImage: the image under its placement map (the image's space to the user space), nearest texel at each pixel center, empty outside (FL17). */
static void prim_image(const Env * e, const GSHSCENE_Filter_Primitive * pr, const GSHSCENE_Color_Space * space, double * out) {
  GSHSCENE_Image_Info info;
  const int ok = gshscene_resource_image(pr->image, &info) == GSHSCENE_OK;
  GSHGEOM_Map inv;
  GSHGEOM_Map placement = pr->placement;
  if (placement.dim != 2) {
    gshgeom_map_identity(2, &placement);
  }
  const double det = placement.m[0][0] * placement.m[1][1] - placement.m[0][1] * placement.m[1][0];
  const int placed = ok && det != 0.0;
  if (placed) { /* the inverse of the placement: the linear part by the adjugate, the translation carried through it */
    inv.m[0][0] = placement.m[1][1] / det;
    inv.m[0][1] = -placement.m[0][1] / det;
    inv.m[1][0] = -placement.m[1][0] / det;
    inv.m[1][1] = placement.m[0][0] / det;
    inv.t[0] = -(inv.m[0][0] * placement.t[0] + inv.m[0][1] * placement.t[1]);
    inv.t[1] = -(inv.m[1][0] * placement.t[0] + inv.m[1][1] * placement.t[1]);
  }
  GSHCPU_Convert cv;
  const GSHSCENE_Color_Space from = ok && !gshscene_color_space_is_target(&info.space) ? info.space : e->layer_space;
  gshcpu_render_convert_prepare(&cv, &from, space);
  for (size_t j = 0; j < e->h; j++) {
    for (size_t i = 0; i < e->w; i++) {
      double * o = &out[PIX * (j * e->w + i)];
      o[0] = o[1] = o[2] = o[3] = 0.0;
      if (!placed) {
        continue;
      }
      double ux, uy;
      aff_inverse_point(&e->m, gshcpu_render_i2d(e->canvas.x0) + (double)i + 0.5, gshcpu_render_i2d(e->canvas.y0) + (double)j + 0.5, &ux, &uy);
      const double ix = inv.m[0][0] * ux + inv.m[0][1] * uy + inv.t[0];
      const double iy = inv.m[1][0] * ux + inv.m[1][1] * uy + inv.t[1];
      if (!(ix >= 0.0 && iy >= 0.0 && ix < (double)info.width && iy < (double)info.height)) {
        continue;
      }
      double px[4];
      if (gshscene_image_decode(pr->image, (uint32_t)ix, (uint32_t)iy, px) == GSHSCENE_OK) {
        gshcpu_render_convert_premultiplied(&cv, px, o);
      }
    }
  }
}

static GSHCPU_Result run_primitive(Env * e, size_t i) {
  const GSHSCENE_Filter_Primitive * pr = &e->f->primitives[i];
  double * out = result_buf(e, i);
  Rect2 sub_in[2];
  size_t n_in = pr->input_count;
  const double * in0 = NULL;
  const double * in1 = NULL;
  switch (pr->kind) {
    case GSHSCENE_FILTER_IMAGE:
      prim_image(e, pr, &e->space[i], out);
      break;
    case GSHSCENE_FILTER_TURBULENCE: {
      Rect2 tile = apply_subregion(pr, e->region_u);
      prim_turbulence(pr, &e->m, e->canvas, e->w, e->h, tile, out);
      break;
    }
    case GSHSCENE_FILTER_FLOOD: {
      double c[4];
      flood_color(e, pr, &e->space[i], c);
      for (size_t p = 0; p < e->pixels; p++) {
        memcpy(&out[PIX * p], c, sizeof c);
      }
      break;
    }
    case GSHSCENE_FILTER_MERGE: {
      /* first input into the output, each later one over it; every input is read through a temporary if it must be converted */
      Rect2 s0;
      const double * a = fetch(e, i, 0, 0, &s0);
      memcpy(out, a, e->pixels * PIX * sizeof(double));
      sub_in[0] = s0;
      Rect2 un = s0;
      for (size_t k = 1; k < n_in; k++) {
        Rect2 sk;
        const double * b = fetch(e, i, k, 0, &sk);
        prim_over(b, out, e->pixels);
        un.x0 = dmin(un.x0, sk.x0);
        un.y0 = dmin(un.y0, sk.y0);
        un.x1 = dmax(un.x1, sk.x1);
        un.y1 = dmax(un.y1, sk.y1);
      }
      sub_in[0] = un;
      n_in = 1; /* the union is the one input subregion */
      break;
    }
    default:
      break;
  }
  if (pr->kind != GSHSCENE_FILTER_FLOOD && pr->kind != GSHSCENE_FILTER_MERGE && pr->kind != GSHSCENE_FILTER_TURBULENCE && pr->kind != GSHSCENE_FILTER_IMAGE) {
    in0 = fetch(e, i, 0, 0, &sub_in[0]);
    if (pr->input_count > 1) {
      in1 = fetch(e, i, 1, 1, &sub_in[1]);
    }
    switch (pr->kind) {
      case GSHSCENE_FILTER_OFFSET: {
        double ddx = e->m.a * pr->dx + e->m.b * pr->dy;
        double ddy = e->m.c * pr->dx + e->m.d * pr->dy;
        prim_offset(in0, out, e->w, e->h, whole(ddx), whole(ddy));
        break;
      }
      case GSHSCENE_FILTER_BLEND: {
        for (size_t p = 0; p < e->pixels; p++) {
          double d[4];
          memcpy(d, &in1[PIX * p], sizeof d);
          if (!gshcpu_render_apply(d, &in0[PIX * p], 1.0, pr->mode)) {
            return GSHCPU_ERR_INTERNAL;
          }
          memcpy(&out[PIX * p], d, sizeof d);
        }
        break;
      }
      case GSHSCENE_FILTER_COMPOSITE:
        prim_composite(pr->mode, pr->k, in0, in1, out, e->pixels);
        break;
      case GSHSCENE_FILTER_COLOR_MATRIX:
        prim_matrix(pr, in0, out, e->pixels);
        break;
      case GSHSCENE_FILTER_COMPONENT_TRANSFER:
        prim_transfer(pr, in0, out, e->pixels);
        break;
      case GSHSCENE_FILTER_GAUSSIAN_BLUR: {
        double dx, dy;
        device_lengths(&e->m, pr->sx, pr->sy, &dx, &dy);
        memcpy(out, in0, e->pixels * PIX * sizeof(double));
        size_t span[4];
        pixel_span(e, sub_in[0], span);
        prim_blur(out, e->w, e->h, dx, dy, pr->edge_mode, span, e->line_a, e->line_b);
        break;
      }
      case GSHSCENE_FILTER_MORPHOLOGY: {
        double dx, dy;
        device_lengths(&e->m, pr->sx, pr->sy, &dx, &dy);
        long rx = whole_px(dx);
        long ry = whole_px(dy);
        memcpy(out, in0, e->pixels * PIX * sizeof(double));
        rx = rx < 0 ? 0 : (rx > (long)e->w ? (long)e->w : rx); /* past the canvas a window is the whole line: the result is as for the canvas' size */
        ry = ry < 0 ? 0 : (ry > (long)e->h ? (long)e->h : ry);
        if (rx > 0 || ry > 0) { /* a radius that rounds to nothing disables the effect */
          prim_morphology(out, e->w, e->h, (size_t)rx, (size_t)ry, pr->mode == (uint32_t)GSHSCENE_FILTER_MORPHOLOGY_DILATE, e->line_a, e->line_b, e->wx);
        }
        break;
      }
      case GSHSCENE_FILTER_CONVOLVE_MATRIX: {
        long r[4];
        pixel_rect(e, sub_in[0], 0, r);
        prim_convolve(pr, in0, out, e->w, e->h, r);
        break;
      }
      case GSHSCENE_FILTER_TILE: {
        long r[4];
        pixel_rect(e, sub_in[0], 1, r);
        prim_tile(in0, out, e->w, e->h, r);
        break;
      }
      case GSHSCENE_FILTER_DISPLACEMENT_MAP: {
        /* the scale is in user units along each axis, as a length (D-FL8); the map's color is the second input's, in this space */
        double dx, dy;
        device_lengths(&e->m, pr->scale, pr->scale, &dx, &dy);
        prim_displace(pr, in0, in1, out, e->w, e->h, dx, dy);
        break;
      }
      case GSHSCENE_FILTER_DIFFUSE_LIGHTING:
      case GSHSCENE_FILTER_SPECULAR_LIGHTING: {
        long r[4];
        pixel_rect(e, sub_in[0], 0, r);
        const GSHSCENE_Color_Space from = gshscene_color_space_is_target(&pr->color.space) ? e->layer_space : pr->color.space;
        GSHCPU_Convert cv;
        gshcpu_render_convert_prepare(&cv, &from, &e->space[i]);
        double lc[3] = {pr->color.rgba[0], pr->color.rgba[1], pr->color.rgba[2]};
        gshcpu_render_convert_straight(&cv, lc, lc);
        prim_lighting(pr, &e->m, e->canvas, in0, out, e->w, e->h, r, lc);
        break;
      }
      case GSHSCENE_FILTER_DROP_SHADOW: {
        double dx, dy;
        device_lengths(&e->m, pr->sx, pr->sy, &dx, &dy);
        double * shadow = e->temp[1]; /* the one input may have used the first temporary */
        for (size_t p = 0; p < e->pixels; p++) {
          shadow[PIX * p] = shadow[PIX * p + 1] = shadow[PIX * p + 2] = 0.0;
          shadow[PIX * p + 3] = in0[PIX * p + 3];
        }
        prim_blur(shadow, e->w, e->h, dx, dy, (uint32_t)GSHSCENE_FILTER_EDGE_NONE, NULL, e->line_a, e->line_b);
        /* the offset of the blurred alpha, then the color, then the source over it: composed in one pass into the output */
        const double ddx = e->m.a * pr->dx + e->m.b * pr->dy;
        const double ddy = e->m.c * pr->dx + e->m.d * pr->dy;
        const long sx = whole(ddx);
        const long sy = whole(ddy);
        double c[4];
        flood_color(e, pr, &e->space[i], c);
        for (size_t j = 0; j < e->h; j++) {
          for (size_t q = 0; q < e->w; q++) {
            const long si = (long)q - sx;
            const long sj = (long)j - sy;
            double a = 0.0;
            if (si >= 0 && sj >= 0 && (size_t)si < e->w && (size_t)sj < e->h) {
              a = shadow[PIX * ((size_t)sj * e->w + (size_t)si) + 3];
            }
            const double * s = &in0[PIX * (j * e->w + q)];
            double * o = &out[PIX * (j * e->w + q)];
            for (int k = 0; k < 4; k++) {
              o[k] = s[k] + c[k] * a * (1.0 - s[3]);
            }
          }
        }
        break;
      }
      default:
        return GSHCPU_ERR_UNSUPPORTED;
    }
  }
  /* FL4: the subregion, the default and the primitive's own coordinates; the result is clipped to it */
  Rect2 sub = default_subregion(sub_in, (pr->kind == GSHSCENE_FILTER_FLOOD || pr->kind == GSHSCENE_FILTER_TILE || pr->kind == GSHSCENE_FILTER_TURBULENCE || pr->kind == GSHSCENE_FILTER_IMAGE) ? 0 : (n_in > 2 ? 2 : n_in), e->region_u); /* feTile's default is the filter region (FL4) */
  sub = apply_subregion(pr, sub);
  /* A subregion outside the filter region is empty there: intermediate results live in the region only (so a feTile of such an
     input tiles nothing, as the references have it). */
  if (sub.x0 < e->region_u.x0) { sub.x0 = e->region_u.x0; }
  if (sub.y0 < e->region_u.y0) { sub.y0 = e->region_u.y0; }
  if (sub.x1 > e->region_u.x1) { sub.x1 = e->region_u.x1; }
  if (sub.y1 > e->region_u.y1) { sub.y1 = e->region_u.y1; }
  if (sub.x1 < sub.x0) { sub.x1 = sub.x0; }
  if (sub.y1 < sub.y0) { sub.y1 = sub.y0; }
  e->sub[i] = sub;
  clip_to(&e->m, e->canvas, sub, out, e->wx, e->wy);
  return GSHCPU_OK;
}

GSHCPU_Result gshcpu_filter_apply(const GSHSCENE_Filter * f, const GSHGEOM_Map * map, const GSHSCENE_Color_Space * layer_space,
    GSHCPU_Rect canvas, double * layer, double * scratch, size_t scratch_doubles, GSHCPU_Rect * dirty) {
  if (!f || !map || !layer_space || !layer || !scratch || f->primitive_count == 0) {
    return GSHCPU_ERR_INVALID;
  }
  for (size_t i = 0; i < f->primitive_count; i++) {
    switch (f->primitives[i].kind) {
      case GSHSCENE_FILTER_OFFSET:
      case GSHSCENE_FILTER_FLOOD:
      case GSHSCENE_FILTER_MERGE:
      case GSHSCENE_FILTER_BLEND:
      case GSHSCENE_FILTER_COMPOSITE:
      case GSHSCENE_FILTER_COLOR_MATRIX:
      case GSHSCENE_FILTER_COMPONENT_TRANSFER:
      case GSHSCENE_FILTER_GAUSSIAN_BLUR:
      case GSHSCENE_FILTER_DROP_SHADOW:
      case GSHSCENE_FILTER_MORPHOLOGY:
      case GSHSCENE_FILTER_CONVOLVE_MATRIX:
      case GSHSCENE_FILTER_TILE:
      case GSHSCENE_FILTER_DISPLACEMENT_MAP:
      case GSHSCENE_FILTER_TURBULENCE:
      case GSHSCENE_FILTER_IMAGE:
      case GSHSCENE_FILTER_DIFFUSE_LIGHTING:
      case GSHSCENE_FILTER_SPECULAR_LIGHTING:
        break;
      default:
        return GSHCPU_ERR_UNSUPPORTED;
    }
  }
  Env e;
  memset(&e, 0, sizeof e);
  if (!aff_make(map, &e.m)) {
    return GSHCPU_ERR_UNSUPPORTED; /* a map with a rotation or a shear: canonical space (D-FL8), not built */
  }
  if (gshcpu_render_rect_empty(canvas)) {
    *dirty = canvas;
    return GSHCPU_OK;
  }
  e.f = f;
  e.canvas = canvas;
  e.w = (size_t)(canvas.x1 - canvas.x0);
  e.h = (size_t)(canvas.y1 - canvas.y0);
  e.pixels = e.w * e.h;
  e.layer_space = *layer_space;
  e.layer = layer;
  plan_make(f, &e.plan);
  const size_t need = gshcpu_filter_scratch_doubles(f, e.w, e.h);
  if (need == SIZE_MAX || need > scratch_doubles) {
    return GSHCPU_ERR_LIMIT;
  }
  const size_t line = e.w > e.h ? e.w : e.h;
  e.slots = scratch;
  e.temp[0] = scratch + e.plan.slots * PIX * e.pixels;
  e.temp[1] = e.temp[0] + PIX * e.pixels;
  double * rest = e.temp[1] + PIX * e.pixels;
  e.wx = rest;
  e.wy = rest + line;
  e.line_a = rest + 2u * line;
  e.line_b = rest + 3u * line;
  /* the region (FL1), in user space and in the device */
  Rect2 canvas_d = {gshcpu_render_i2d(canvas.x0), gshcpu_render_i2d(canvas.y0), gshcpu_render_i2d(canvas.x1), gshcpu_render_i2d(canvas.y1)};
  if (f->region_mode == (uint32_t)GSHSCENE_FILTER_REGION_AUTO) {
    e.region_u = rect_to_user(&e.m, canvas_d);
  } else {
    e.region_u.x0 = f->region[0];
    e.region_u.y0 = f->region[1];
    e.region_u.x1 = f->region[0] + f->region[2];
    e.region_u.y1 = f->region[1] + f->region[3];
  }
  const GSHSCENE_Color_Space filter_space = space_of(f->color_space);
  for (size_t i = 0; i < f->primitive_count; i++) {
    e.space[i] = f->primitives[i].color_space == (uint32_t)GSHSCENE_FILTER_SPACE_INHERIT ? filter_space
                                                                                         : space_of(f->primitives[i].color_space);
  }
  /* the source graphic is the layer cropped to the region (FL9) */
  clip_to(&e.m, canvas, e.region_u, layer, e.wx, e.wy);
  GSHCPU_Result r = GSHCPU_OK;
  for (size_t i = 0; r == GSHCPU_OK && i < f->primitive_count; i++) {
    r = run_primitive(&e, i);
  }
  if (r != GSHCPU_OK) {
    return r;
  }
  /* FL20: the last result in the layer's space, clipped to the region */
  const size_t last = f->primitive_count - 1u;
  const double * result = result_buf(&e, last);
  if (prim_space_is(&e.space[last], &e.layer_space)) {
    memcpy(layer, result, e.pixels * PIX * sizeof(double));
  } else {
    GSHCPU_Convert c;
    gshcpu_render_convert_prepare(&c, &e.space[last], &e.layer_space);
    convert_buffer(&c, result, layer, e.pixels);
  }
  clip_to(&e.m, canvas, e.region_u, layer, e.wx, e.wy);
  Rect2 rd = rect_to_device(&e.m, e.region_u);
  GSHCPU_Rect d;
  d.x0 = gshcpu_render_d2i(gmath_floor(rd.x0));
  d.y0 = gshcpu_render_d2i(gmath_floor(rd.y0));
  d.x1 = gshcpu_render_d2i(gmath_ceil(rd.x1));
  d.y1 = gshcpu_render_d2i(gmath_ceil(rd.y1));
  *dirty = gshcpu_render_rect_meet(d, canvas);
  return GSHCPU_OK;
}
