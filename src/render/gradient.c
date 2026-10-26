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
 * A gradient's value at a device point (S11, render.h; shape-scene's G4 to G9 and
 * G14; color-paint story P2): the fill's chain composed by shape-geometry (AD-24)
 * and inverted once, the point taken to gradient space, its parameter `t` by
 * the kind's rule and its color by the color line and the extend.  Every value
 * is a `double`; the angle is `math`'s `gmath_atan2`, the root `gmath_sqrt`, the
 * floor `gmath_floor`.  The zero tests G7 and G8 depend on (a linear
 * denominator, a radial `a`) are made on exact sums of exact products, so a
 * value the doubles make nonzero is never divided as zero.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/math/basic.h>
#include <ghoti.io/math/elementary.h>

#include <string.h>

#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>
#define GRADIENT_AVX2 1
#endif

/** Two pi, the double nearest (twice the double nearest pi, which is exact). */
#define TWO_PI 6.283185307179586

/* --- exact sums of products (Dekker and Knuth, no fused multiply-add) ------------------------------------------- */

/** `a + b = s + e` exactly. */
static void two_sum(double a, double b, double * s, double * e) {
  double sum = a + b;
  double bb = sum - a;
  *e = (a - (sum - bb)) + (b - bb);
  *s = sum;
}

/** `a = hi + lo`, each half of 26 bits or fewer (|a| well below 2^996). */
static void split(double a, double * hi, double * lo) {
  double c = 134217729.0 * a; /* 2^27 + 1 */
  double h = c - (c - a);
  *hi = h;
  *lo = a - h;
}

/** `a b = p + e` exactly (no underflow: G14's bounds keep every product here far above the subnormals). */
static void two_product(double a, double b, double * p, double * e) {
  double ah;
  double al;
  double bh;
  double bl;
  double prod = a * b;
  split(a, &ah, &al);
  split(b, &bh, &bl);
  *e = ((ah * bh - prod) + ah * bl + al * bh) + al * bl;
  *p = prod;
}

/** The most terms an exact sum here takes. */
#define TERMS 24

/** An exact sum: a nonoverlapping expansion, smallest component first, zeros dropped (Shewchuk's grow). */
typedef struct Expansion {
  double e[TERMS];
  int n;
} Expansion;

static void grow(Expansion * x, double b) {
  double q = b;
  int k = 0;
  for (int i = 0; i < x->n; i++) {
    double h;
    two_sum(q, x->e[i], &q, &h);
    if (h != 0.0) {
      x->e[k++] = h;
    }
  }
  if (q != 0.0) {
    x->e[k++] = q;
  }
  x->n = k;
}

/** The exact product @p a @p b added. */
static void grow_product(Expansion * x, double a, double b) {
  double p;
  double e;
  two_product(a, b, &p, &e);
  grow(x, e);
  grow(x, p);
}

/** The sum as one double: zero exactly when the sum is, and of its sign (the largest component's). */
static double approximate(const Expansion * x) {
  double s = 0.0;
  for (int i = 0; i < x->n; i++) {
    s += x->e[i];
  }
  return s;
}

/** `(a - b)` as the exact pair `hi + lo`. */
static void two_diff(double a, double b, double * hi, double * lo) {
  two_sum(a, -b, hi, lo);
}

/** The exact square of the difference `a - b` added, times @p sign (1 or -1). */
static void grow_square_of_difference(Expansion * x, double a, double b, double sign) {
  double hi;
  double lo;
  two_diff(a, b, &hi, &lo);
  grow_product(x, sign * hi, hi);
  grow_product(x, sign * 2.0 * hi, lo);
  grow_product(x, sign * lo, lo);
}

/* --- the map ----------------------------------------------------------------------------------------------- */

static int is_finite(double x) {
  return x - x == 0.0;
}

/**
 * The inverse of @p f's 2x2 part, scaled by the power of two of its largest entry first (exact), and its translation:
 * `p = L^-1 (q - t)`.  @return 0 where the determinant is zero in doubles or an entry is not finite.
 */
static int invert(const GSHGEOM_Map * f, double inv[6]) {
  double m[4] = {f->m[0][0], f->m[0][1], f->m[1][0], f->m[1][1]};
  double largest = 0.0;
  for (int i = 0; i < 4; i++) {
    double a = m[i] < 0.0 ? -m[i] : m[i];
    if (!is_finite(m[i])) {
      return 0;
    }
    largest = a > largest ? a : largest;
  }
  if (largest == 0.0 || !is_finite(f->t[0]) || !is_finite(f->t[1])) {
    return 0;
  }
  int e = 0;
  (void)gmath_frexp(largest, &e);
  for (int i = 0; i < 4; i++) {
    m[i] = gmath_ldexp(m[i], -e);
  }
  Expansion det = {{0.0}, 0};
  grow_product(&det, m[0], m[3]);
  grow_product(&det, -m[1], m[2]);
  double d = approximate(&det);
  if (d == 0.0) {
    return 0;
  }
  inv[0] = gmath_ldexp(m[3] / d, -e);
  inv[1] = gmath_ldexp(-m[1] / d, -e);
  inv[2] = gmath_ldexp(-m[2] / d, -e);
  inv[3] = gmath_ldexp(m[0] / d, -e);
  inv[4] = f->t[0];
  inv[5] = f->t[1];
  const double from[4] = {m[3], m[1], m[2], m[0]};
  for (int i = 0; i < 4; i++) {
    if (!is_finite(inv[i]) || (inv[i] == 0.0 && from[i] != 0.0)) {
      return 0; /* an entry of the inverse past the double range, or lost below it */
    }
  }
  return 1;
}

void gshcpu_render_gradient_unmap(const GSHCPU_Gradient * g, double qx, double qy, double * px, double * py) {
  double u = qx - g->inv[4];
  double v = qy - g->inv[5];
  *px = g->inv[0] * u + g->inv[1] * v;
  *py = g->inv[2] * u + g->inv[3] * v;
}

/* --- prepare ----------------------------------------------------------------------------------------------- */

void gshcpu_render_gradient_stops(GSHCPU_Stop * stops, size_t count, uint32_t premultiplied) {
  if (!premultiplied) {
    return; /* G4: the straight color is the interpolant */
  }
  for (size_t i = 0; i < count; i++) {
    /* G4: (a r, a g, a b, a). */
    double a = stops[i].rgba[3];
    for (int k = 0; k < 3; k++) {
      stops[i].rgba[k] = stops[i].rgba[k] * a;
    }
  }
}

static void row_f_template(GSHCPU_Gradient * g);

GSHCPU_Result gshcpu_render_gradient_prepare(GSHCPU_Gradient * out, const GSHSCENE_Gradient * g,
    const GSHCPU_Stop * stops, const GSHGEOM_Map * composed, double alpha) {
  memset(out, 0, sizeof *out);
  out->kind = g->kind;
  out->extend = g->extend;
  out->premultiplied = g->premultiplied;
  out->stops = stops;
  out->count = g->stop_count;
  out->alpha = alpha;
  if (!invert(composed, out->inv)) {
    return GSHCPU_ERR_LIMIT;
  }
  if (out->count) {
    out->o_first = stops[0].offset;
    out->o_last = stops[out->count - 1].offset;
    out->period = out->o_last - out->o_first;
    if (out->period > 0.0) {
      out->inv_period = 1.0 / out->period;
      out->scale = (double)GSHCPU_RENDER_GRADIENT_TABLE / out->period;
    }
  }
  switch (g->kind) {
    case GSHSCENE_GRADIENT_LINEAR: {
      /* G7: n_d = (-d_y, d_x), or p1 - p0 when d is zero; the denominator (p1 - p0) . n_d, exactly. */
      out->p[0] = g->p0[0];
      out->p[1] = g->p0[1];
      Expansion den = {{0.0}, 0};
      double dx_hi;
      double dx_lo;
      double dy_hi;
      double dy_lo;
      two_diff(g->p1[0], g->p0[0], &dx_hi, &dx_lo);
      two_diff(g->p1[1], g->p0[1], &dy_hi, &dy_lo);
      if (g->d[0] == 0.0 && g->d[1] == 0.0) {
        out->n[0] = g->p1[0] - g->p0[0];
        out->n[1] = g->p1[1] - g->p0[1];
        grow_square_of_difference(&den, g->p1[0], g->p0[0], 1.0);
        grow_square_of_difference(&den, g->p1[1], g->p0[1], 1.0);
      } else {
        out->n[0] = -g->d[1];
        out->n[1] = g->d[0];
        grow_product(&den, dx_hi, out->n[0]);
        grow_product(&den, dx_lo, out->n[0]);
        grow_product(&den, dy_hi, out->n[1]);
        grow_product(&den, dy_lo, out->n[1]);
      }
      out->den = approximate(&den);
      out->degenerate = out->den == 0.0;
      if (!out->degenerate) {
        out->dt_dx = (out->inv[0] * out->n[0] + out->inv[2] * out->n[1]) / out->den;
      }
      break;
    }
    case GSHSCENE_GRADIENT_RADIAL: {
      /* G8: a w^2 - 2 b w + c = 0 with a = |c1 - c0|^2 - (r1 - r0)^2, exactly. */
      out->p[0] = g->c0[0];
      out->p[1] = g->c0[1];
      out->r0 = g->r0;
      out->n[0] = g->c1[0] - g->c0[0];
      out->n[1] = g->c1[1] - g->c0[1];
      out->dr = g->r1 - g->r0;
      Expansion a = {{0.0}, 0};
      grow_square_of_difference(&a, g->c1[0], g->c0[0], 1.0);
      grow_square_of_difference(&a, g->c1[1], g->c0[1], 1.0);
      grow_square_of_difference(&a, g->r1, g->r0, -1.0);
      out->a = approximate(&a);
      out->degenerate = g->c0[0] == g->c1[0] && g->c0[1] == g->c1[1] && g->r0 == g->r1;
      break;
    }
    default: {
      /* G9: the angle's unit, one turn in it. */
      out->p[0] = g->center[0];
      out->p[1] = g->center[1];
      out->unit = g->angle_unit;
      out->a0 = g->a0;
      out->a1 = g->a1;
      switch (g->angle_unit) {
        case GSHGEOM_ANGLE_DEGREES:
          out->turn = 360.0;
          break;
        case GSHGEOM_ANGLE_GRADIANS:
          out->turn = 400.0;
          break;
        case GSHGEOM_ANGLE_TURNS:
          out->turn = 1.0;
          break;
        default:
          out->turn = TWO_PI;
          break;
      }
      out->degenerate = g->a0 == g->a1 && g->extend != GSHSCENE_EXTEND_PAD;
      break;
    }
  }
  /* G5: two or more stops at one offset under repeat or reflect draw nothing. */
  if (out->count >= 2 && out->period == 0.0 && g->extend != GSHSCENE_EXTEND_PAD) {
    out->degenerate = 1;
  }
  row_f_template(out);
  return GSHCPU_OK;
}

int gshcpu_render_gradient_in_domain(const GSHCPU_Gradient * g, GSHCPU_Rect r) {
  if (gshcpu_render_rect_empty(r)) {
    return 1;
  }
  /* The map is affine, so the farthest point of the rectangle's centers is a corner; half the domain leaves room for
   * the rounding of the points between. */
  const double bound = GSHSCENE_GRADIENT_DOMAIN * 0.5;
  const double xs[2] = {(double)(int32_t)r.x0 + 0.5, (double)(int32_t)r.x1 - 0.5};
  const double ys[2] = {(double)(int32_t)r.y0 + 0.5, (double)(int32_t)r.y1 - 0.5}; /* 32-bit: no x87 (AD-17) */
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      double px;
      double py;
      gshcpu_render_gradient_unmap(g, xs[i], ys[j], &px, &py);
      if (!(px >= -bound && px <= bound && py >= -bound && py <= bound)) {
        return 0;
      }
    }
  }
  return 1;
}

/* --- the parameter ------------------------------------------------------------------------------------------- */

/** G9: the angle of (dx, dy) in [0, one turn), +x toward +y, 0 at the center; the axes exactly. */
static double angle_of(const GSHCPU_Gradient * g, double dx, double dy) {
  if (dy == 0.0) {
    return dx < 0.0 ? 0.5 * g->turn : 0.0;
  }
  if (dx == 0.0) {
    return (dy > 0.0 ? 0.25 : 0.75) * g->turn;
  }
  double rad = gmath_atan2(dy, dx);
  if (rad < 0.0) {
    rad += TWO_PI;
  }
  return g->unit == GSHGEOM_ANGLE_RADIANS ? rad : rad / TWO_PI * g->turn;
}

/** G8: the parameter of the radial gradient @p g at the gradient-space point (@p px, @p py); 0 where nothing is drawn. */
static inline int radial_param(const GSHCPU_Gradient * g, double px, double py, double * t) {
    double x = px - g->p[0];
    double y = py - g->p[1];
    double b = x * g->n[0] + y * g->n[1] + g->r0 * g->dr;
    double c = x * x + y * y - g->r0 * g->r0;
    double w[2];
    int nw = 0;
    if (g->a == 0.0) {
      if (b == 0.0) {
        return 0; /* no w, or every w at a point: nothing (a set of measure zero) */
      }
      w[nw++] = c / (2.0 * b);
    } else {
      double disc = b * b - g->a * c;
      if (disc < 0.0) {
        return 0;
      }
      double root = gmath_sqrt(disc);
      double q = b < 0.0 ? b - root : b + root;
      if (q == 0.0) {
        w[nw++] = 0.0; /* b = 0 and c = 0: the double root 0 */
      } else {
        double u = q / g->a;
        double v = c / q;
        w[nw++] = u > v ? u : v;
        w[nw++] = u > v ? v : u;
      }
    }
    /* The largest w whose radius is positive. */
    for (int i = 0; i < nw; i++) {
      if (g->r0 + w[i] * g->dr > 0.0) {
        *t = w[i];
        return 1;
      }
    }
    return 0;
}

int gshcpu_render_gradient_param(const GSHCPU_Gradient * g, double px, double py, double * t) {
  if (g->degenerate) {
    return 0;
  }
  if (g->kind == GSHSCENE_GRADIENT_LINEAR) {
    *t = ((px - g->p[0]) * g->n[0] + (py - g->p[1]) * g->n[1]) / g->den;
    return 1;
  }
  if (g->kind == GSHSCENE_GRADIENT_RADIAL) {
    return radial_param(g, px, py, t);
  }
  double theta = angle_of(g, px - g->p[0], py - g->p[1]);
  if (g->a0 == g->a1) {
    /* G9: pad's step at a0 (repeat and reflect drew nothing above). */
    *t = theta < g->a0 ? -GSHCPU_RENDER_T_LIMIT : GSHCPU_RENDER_T_LIMIT;
    return 1;
  }
  *t = (theta - g->a0) / (g->a1 - g->a0);
  return 1;
}

/* --- the color ----------------------------------------------------------------------------------------------- */

static void copy4(double out[4], const double in[4]) {
  out[0] = in[0];
  out[1] = in[1];
  out[2] = in[2];
  out[3] = in[3];
}

/** G4 inside the line, at least two stops, `o_first < o_last`: @p t is clamped into the line. */
static void line_at(const GSHCPU_Gradient * g, double t, double out[4]) {
  const GSHCPU_Stop * s = g->stops;
  size_t n = g->count;
  if (!(t < g->o_last)) {
    copy4(out, s[n - 1].rgba);
    return;
  }
  if (t < g->o_first) {
    t = g->o_first;
  }
  /* The first stop whose offset is above t: among equal offsets the last given holds at that offset. */
  size_t lo = 1;
  size_t hi = n - 1;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (t < s[mid].offset) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  const GSHCPU_Stop * a = &s[lo - 1];
  const GSHCPU_Stop * b = &s[lo];
  double f = (t - a->offset) / (b->offset - a->offset);
  for (int k = 0; k < 4; k++) {
    out[k] = a->rgba[k] + f * (b->rgba[k] - a->rgba[k]);
  }
}

void gshcpu_render_gradient_color(const GSHCPU_Gradient * g, double t, double out[4]) {
  double c[4] = {0.0, 0.0, 0.0, 0.0};
  if (!(t >= -GSHCPU_RENDER_T_LIMIT)) {
    t = -GSHCPU_RENDER_T_LIMIT; /* P2-4: past the double's reach, at that end (and never a NaN) */
  } else if (t > GSHCPU_RENDER_T_LIMIT) {
    t = GSHCPU_RENDER_T_LIMIT;
  }
  if (g->count == 1) {
    copy4(c, g->stops[0].rgba);
  } else if (g->count >= 2) {
    if (g->period == 0.0 || g->extend == GSHSCENE_EXTEND_PAD) {
      /* G5: pad, and the step of P = 0 (repeat and reflect at P = 0 drew nothing above). */
      if (t < g->o_first) {
        copy4(c, g->stops[0].rgba);
      } else {
        line_at(g, t, c);
      }
    } else {
      if (t < g->o_first || t > g->o_last) {
        double x = t - g->o_first;
        if (g->extend == GSHSCENE_EXTEND_REPEAT) {
          x = x - gmath_floor(x / g->period) * g->period;
        } else {
          double twice = 2.0 * g->period;
          double m = x - gmath_floor(x / twice) * twice;
          double d = m - g->period;
          x = g->period - (d < 0.0 ? -d : d);
        }
        t = g->o_first + x;
      }
      line_at(g, t, c);
    }
  }
  /* The straight color: un-premultiplied when the line was premultiplied (an alpha of 0 is transparent black). */
  if (g->premultiplied) {
    double a = c[3];
    for (int k = 0; k < 3; k++) {
      c[k] = a == 0.0 ? 0.0 : gshcpu_render_unpremultiply(c[k], a); /* N1: saturated */
    }
  }
  c[3] = c[3] * g->alpha;
  copy4(out, c);
}

int gshcpu_render_gradient_at(const GSHCPU_Gradient * g, double qx, double qy, double out[4]) {
  double px;
  double py;
  double t = 0.0;
  gshcpu_render_gradient_unmap(g, qx, qy, &px, &py);
  if (!gshcpu_render_gradient_param(g, px, py, &t)) {
    return 0;
  }
  gshcpu_render_gradient_color(g, t, out);
  return 1;
}

/* --- the table (the fast fill coverage) ----------------------------------------------------------------------- */

void gshcpu_render_gradient_table(const GSHCPU_Gradient * g, const GSHCPU_Convert * c, double * out) {
  const size_t n = GSHCPU_RENDER_GRADIENT_TABLE;
  for (size_t k = 0; k <= n; k++) {
    double t = g->period > 0.0 ? g->o_first + g->period * ((double)k / (double)n) : g->o_first;
    if (k == 0 && (g->period == 0.0 || g->extend == GSHSCENE_EXTEND_PAD)) {
      t = g->o_first - 1.0; /* before the line: the first stop, as a pad holds it */
    } else if (k == n) {
      t = g->o_last;
    }
    double color[4];
    gshcpu_render_gradient_color(g, t, color);
    gshcpu_render_convert_straight(c, color, color);
    out[4u * k + 0u] = color[0] * color[3];
    out[4u * k + 1u] = color[1] * color[3];
    out[4u * k + 2u] = color[2] * color[3];
    out[4u * k + 3u] = color[3];
  }
}

/** The floor of @p v, by a 32-bit conversion where the value is within its range. */
static inline double table_floor(double v) {
  if (v > -2147483000.0 && v < 2147483000.0) {
    const double r = (double)(int32_t)v; /* 32-bit: no x87 (AD-17) */
    return r > v ? r - 1.0 : r;
  }
  return gmath_floor(v);
}

/** The table index of the offset @p u on the line, in points from its first end (past either end: that end's). */
static inline uint32_t table_point(double u) {
  if (!(u > 0.0)) {
    return 0;
  }
  if (u >= (double)GSHCPU_RENDER_GRADIENT_TABLE) {
    return GSHCPU_RENDER_GRADIENT_TABLE;
  }
  return (uint32_t)(int32_t)u; /* 32-bit: no x87 (AD-17) */
}

/** The table index of the parameter @p t. */
static inline uint32_t table_index(const GSHCPU_Gradient * g, double t) {
  const uint32_t n = GSHCPU_RENDER_GRADIENT_TABLE;
  if (!(t >= -GSHCPU_RENDER_T_LIMIT)) {
    return 0;
  }
  if (t > GSHCPU_RENDER_T_LIMIT) {
    return n;
  }
  if (!(g->period > 0.0)) {
    return t < g->o_first ? 0u : n; /* a step: the first stop before the offset, the last from it */
  }
  if (g->extend != GSHSCENE_EXTEND_PAD && (t < g->o_first || t > g->o_last)) {
    double x = t - g->o_first;
    if (g->extend == GSHSCENE_EXTEND_REPEAT) {
      x = x - table_floor(x * g->inv_period) * g->period;
    } else {
      double twice = 2.0 * g->period;
      double m = x - table_floor(x * (0.5 * g->inv_period)) * twice;
      double d = m - g->period;
      x = g->period - (d < 0.0 ? -d : d);
    }
    t = g->o_first + x;
  }
  return table_point((t - g->o_first) * g->scale + 0.5);
}

/** atan(x) for x in [0, 1] by an odd polynomial of the seventh degree in x: within 3e-7. */
static inline double atan_unit(double x) {
  const double x2 = x * x;
  return x * (0.9999966347006692 + x2 * (-0.3331830289944582 + x2 * (0.19813213509060276 + x2 * (-0.13247522771595696 +
      x2 * (0.0798112049555744 + x2 * (-0.033725938103610145 + x2 * 0.006842624897387503))))));
}

/** G9's angle (angle_of) with the arctangent of the polynomial: within 3e-7 of a turn's 2 pi. */
static inline double angle_fast(const GSHCPU_Gradient * g, double dx, double dy) {
  if (dy == 0.0) {
    return dx < 0.0 ? 0.5 * g->turn : 0.0;
  }
  if (dx == 0.0) {
    return (dy > 0.0 ? 0.25 : 0.75) * g->turn;
  }
  const double ax = dx < 0.0 ? -dx : dx;
  const double ay = dy < 0.0 ? -dy : dy;
  double rad = ay > ax ? 1.5707963267948966 - atan_unit(ax / ay) : atan_unit(ay / ax);
  if (dx < 0.0) {
    rad = 3.141592653589793 - rad;
  }
  if (dy < 0.0) {
    rad = TWO_PI - rad; /* the angle in [0, 2 pi) */
  }
  return g->unit == GSHGEOM_ANGLE_RADIANS ? rad : rad / TWO_PI * g->turn;
}

/* --- the rows of a table's indices, in single precision ----------------------------------------------------- */

/**
 * A gradient padded at both ends has its offsets on the line found in single precision, a row of pixels at a time: the same
 * operations, in the same order and with the same constants, in the scalar function (row_f_pixel) and in the AVX2 kernel
 * (row_f_avx2), so that a processor without AVX2 gives the bytes one with it does.  A float holds a row of pixels' offsets to
 * about a millionth, past what the table can tell apart; the first pixel's point is taken in doubles first.
 */
typedef GSHCPU_Row_F Row_F;

#define ROW_ATAN0 0.9999966347006692f
#define ROW_ATAN1 -0.3331830289944582f
#define ROW_ATAN2 0.19813213509060276f
#define ROW_ATAN3 -0.13247522771595696f
#define ROW_ATAN4 0.0798112049555744f
#define ROW_ATAN5 -0.033725938103610145f
#define ROW_ATAN6 0.006842624897387503f
#define ROW_HALF_PI 1.5707963267948966f
#define ROW_PI 3.141592653589793f
#define ROW_TWO_PI 6.283185307179586f
#define ROW_THREE_HALF_PI 4.71238898038469f

/** The constants of @p g's rows, in single precision (the per-row ones are set by the row). */
static void row_f_template(GSHCPU_Gradient * g) {
  Row_F * c = &g->rowf;
  memset(c, 0, sizeof *c);
  c->kind = g->kind;
  c->a_zero = g->a == 0.0;
  c->dx = (float)g->inv[0];
  c->dy = (float)g->inv[2];
  c->n0 = (float)g->n[0];
  c->n1 = (float)g->n[1];
  c->r0 = (float)g->r0;
  c->dr = (float)g->dr;
  c->ra = (float)g->a;
  c->r0dr = (float)(g->r0 * g->dr);
  c->r0sq = (float)(g->r0 * g->r0);
  c->ub = (float)(g->dt_dx * g->scale);
  c->turn = (float)(g->turn / TWO_PI);
  c->a0 = (float)g->a0;
  c->k = (float)(g->a1 != g->a0 ? 1.0 / (g->a1 - g->a0) : 0.0);
  c->first = (float)g->o_first;
  c->scale = (float)g->scale;
}

/** The operands' order of the vector instructions: a maximum or a minimum with a NaN is the second operand. */
static inline float row_max(float a, float b) {
  return a > b ? a : b;
}
static inline float row_min(float a, float b) {
  return a < b ? a : b;
}
static inline float row_abs(float v) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof bits);
  bits &= 0x7fffffffu;
  memcpy(&v, &bits, sizeof bits);
  return v;
}

/** The table index of the pixel @p k from the row's first, or ::GSHCPU_RENDER_GRADIENT_NONE where the gradient draws nothing there. */
static uint32_t row_f_pixel(const Row_F * c, float k) {
  float u;
  int none = 0;
  if (c->kind == GSHSCENE_GRADIENT_LINEAR) {
    u = c->ua0 + c->ub * k;
  } else {
    const float x = c->x0 + c->dx * k;
    const float y = c->y0 + c->dy * k;
    float t;
    if (c->kind == GSHSCENE_GRADIENT_RADIAL) {
      const float b = (x * c->n0 + y * c->n1) + c->r0dr;
      const float cc = (x * x + y * y) - c->r0sq;
      if (c->a_zero) {
        t = cc / (b + b);
        none = b == 0.0f || !(c->r0 + t * c->dr > 0.0f);
      } else {
        const float disc = b * b - c->ra * cc;
        const float root = (float)gmath_sqrt((double)row_max(disc, 0.0f)); /* a float's root, rounded twice, is its root rounded once */
        const float q = b < 0.0f ? b - root : b + root;
        const float uu = q / c->ra;
        const float vv = cc / q;
        const int gt = uu > vv;
        const float hi = gt ? uu : vv;
        const float lo = gt ? vv : uu;
        const int hi_ok = c->r0 + hi * c->dr > 0.0f;
        const int lo_ok = c->r0 + lo * c->dr > 0.0f;
        const int qz = q == 0.0f;
        t = qz ? 0.0f : (hi_ok ? hi : lo);
        const int ok = qz ? c->r0 > 0.0f : (hi_ok || lo_ok);
        none = disc < 0.0f || !ok;
      }
    } else {
      const float ax = row_abs(x);
      const float ay = row_abs(y);
      const float r = row_min(ax, ay) / row_max(ax, ay);
      const float r2 = r * r;
      float poly = ROW_ATAN6;
      poly = poly * r2 + ROW_ATAN5;
      poly = poly * r2 + ROW_ATAN4;
      poly = poly * r2 + ROW_ATAN3;
      poly = poly * r2 + ROW_ATAN2;
      poly = poly * r2 + ROW_ATAN1;
      poly = poly * r2 + ROW_ATAN0;
      float rad = poly * r;
      if (ay > ax) {
        rad = ROW_HALF_PI - rad;
      }
      const int xneg = x < 0.0f;
      if (xneg) {
        rad = ROW_PI - rad;
      }
      if (y < 0.0f) {
        rad = ROW_TWO_PI - rad;
      }
      const int dy0 = y == 0.0f;
      const int dx0 = x == 0.0f;
      if (dy0) {
        rad = xneg ? ROW_PI : 0.0f;
      }
      if (!dy0 && dx0) {
        rad = y > 0.0f ? ROW_HALF_PI : ROW_THREE_HALF_PI;
      }
      t = ((rad * c->turn) - c->a0) * c->k;
    }
    u = ((t - c->first) * c->scale) + 0.5f;
  }
  u = row_min(row_max(u, 0.0f), (float)GSHCPU_RENDER_GRADIENT_TABLE); /* below the line, and a NaN, is the first point */
  return none ? GSHCPU_RENDER_GRADIENT_NONE : (uint32_t)(int32_t)u;
}

#if defined(GRADIENT_AVX2)
/** Whether the vector kernel may be used: the processor has AVX2 (asked once; a race on it only repeats the question) and a test has not turned it off. */
static int gradient_vector_on = 1;

static int gradient_have_avx2(void) {
  return __builtin_cpu_supports("avx2") && gradient_vector_on; /* the answer is made before main: a read, no race */
}

/** row_f_pixel for the eight pixels from @p i: the same operations, the choices made with masks. */
__attribute__((target("avx2"), always_inline)) static inline __m256i row_f_block(const Row_F * c, size_t i) {
  const __m256 lane = _mm256_set_ps(7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.0f);
  const __m256 zero = _mm256_setzero_ps();
  const __m256 ones = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
  const __m256 sign = _mm256_set1_ps(-0.0f);
  const __m256 half_pi = _mm256_set1_ps(ROW_HALF_PI);
  const __m256 pi = _mm256_set1_ps(ROW_PI);
  {
    const __m256 k = _mm256_add_ps(_mm256_set1_ps((float)(int32_t)i), lane);
    __m256 u;
    __m256 none = zero;
    if (c->kind == GSHSCENE_GRADIENT_LINEAR) {
      u = _mm256_add_ps(_mm256_set1_ps(c->ua0), _mm256_mul_ps(_mm256_set1_ps(c->ub), k));
    } else {
      const __m256 x = _mm256_add_ps(_mm256_set1_ps(c->x0), _mm256_mul_ps(_mm256_set1_ps(c->dx), k));
      const __m256 y = _mm256_add_ps(_mm256_set1_ps(c->y0), _mm256_mul_ps(_mm256_set1_ps(c->dy), k));
      __m256 t;
      if (c->kind == GSHSCENE_GRADIENT_RADIAL) {
        const __m256 r0 = _mm256_set1_ps(c->r0);
        const __m256 dr = _mm256_set1_ps(c->dr);
        const __m256 b = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(x, _mm256_set1_ps(c->n0)), _mm256_mul_ps(y, _mm256_set1_ps(c->n1))),
            _mm256_set1_ps(c->r0dr));
        const __m256 cc = _mm256_sub_ps(_mm256_add_ps(_mm256_mul_ps(x, x), _mm256_mul_ps(y, y)), _mm256_set1_ps(c->r0sq));
        if (c->a_zero) {
          t = _mm256_div_ps(cc, _mm256_add_ps(b, b));
          none = _mm256_or_ps(_mm256_cmp_ps(b, zero, _CMP_EQ_OQ),
              _mm256_cmp_ps(_mm256_add_ps(r0, _mm256_mul_ps(t, dr)), zero, _CMP_NGT_UQ));
        } else {
          const __m256 ra = _mm256_set1_ps(c->ra);
          const __m256 disc = _mm256_sub_ps(_mm256_mul_ps(b, b), _mm256_mul_ps(ra, cc));
          const __m256 root = _mm256_sqrt_ps(_mm256_max_ps(disc, zero));
          const __m256 q = _mm256_blendv_ps(_mm256_add_ps(b, root), _mm256_sub_ps(b, root), _mm256_cmp_ps(b, zero, _CMP_LT_OQ));
          const __m256 uu = _mm256_div_ps(q, ra);
          const __m256 vv = _mm256_div_ps(cc, q);
          const __m256 gt = _mm256_cmp_ps(uu, vv, _CMP_GT_OQ);
          const __m256 hi = _mm256_blendv_ps(vv, uu, gt);
          const __m256 lo = _mm256_blendv_ps(uu, vv, gt);
          const __m256 hi_ok = _mm256_cmp_ps(_mm256_add_ps(r0, _mm256_mul_ps(hi, dr)), zero, _CMP_GT_OQ);
          const __m256 lo_ok = _mm256_cmp_ps(_mm256_add_ps(r0, _mm256_mul_ps(lo, dr)), zero, _CMP_GT_OQ);
          const __m256 qz = _mm256_cmp_ps(q, zero, _CMP_EQ_OQ);
          t = _mm256_blendv_ps(_mm256_blendv_ps(lo, hi, hi_ok), zero, qz);
          const __m256 ok = _mm256_blendv_ps(_mm256_or_ps(hi_ok, lo_ok), _mm256_cmp_ps(r0, zero, _CMP_GT_OQ), qz);
          none = _mm256_or_ps(_mm256_cmp_ps(disc, zero, _CMP_LT_OQ), _mm256_xor_ps(ok, ones));
        }
      } else {
        const __m256 ax = _mm256_andnot_ps(sign, x);
        const __m256 ay = _mm256_andnot_ps(sign, y);
        const __m256 r = _mm256_div_ps(_mm256_min_ps(ax, ay), _mm256_max_ps(ax, ay));
        const __m256 r2 = _mm256_mul_ps(r, r);
        __m256 poly = _mm256_set1_ps(ROW_ATAN6);
        poly = _mm256_add_ps(_mm256_mul_ps(poly, r2), _mm256_set1_ps(ROW_ATAN5));
        poly = _mm256_add_ps(_mm256_mul_ps(poly, r2), _mm256_set1_ps(ROW_ATAN4));
        poly = _mm256_add_ps(_mm256_mul_ps(poly, r2), _mm256_set1_ps(ROW_ATAN3));
        poly = _mm256_add_ps(_mm256_mul_ps(poly, r2), _mm256_set1_ps(ROW_ATAN2));
        poly = _mm256_add_ps(_mm256_mul_ps(poly, r2), _mm256_set1_ps(ROW_ATAN1));
        poly = _mm256_add_ps(_mm256_mul_ps(poly, r2), _mm256_set1_ps(ROW_ATAN0));
        __m256 rad = _mm256_mul_ps(poly, r);
        rad = _mm256_blendv_ps(rad, _mm256_sub_ps(half_pi, rad), _mm256_cmp_ps(ay, ax, _CMP_GT_OQ));
        const __m256 xneg = _mm256_cmp_ps(x, zero, _CMP_LT_OQ);
        rad = _mm256_blendv_ps(rad, _mm256_sub_ps(pi, rad), xneg);
        rad = _mm256_blendv_ps(rad, _mm256_sub_ps(_mm256_set1_ps(ROW_TWO_PI), rad), _mm256_cmp_ps(y, zero, _CMP_LT_OQ));
        const __m256 dy0 = _mm256_cmp_ps(y, zero, _CMP_EQ_OQ);
        const __m256 dx0 = _mm256_cmp_ps(x, zero, _CMP_EQ_OQ);
        rad = _mm256_blendv_ps(rad, _mm256_blendv_ps(zero, pi, xneg), dy0);
        rad = _mm256_blendv_ps(rad, _mm256_blendv_ps(_mm256_set1_ps(ROW_THREE_HALF_PI), half_pi, _mm256_cmp_ps(y, zero, _CMP_GT_OQ)),
            _mm256_andnot_ps(dy0, dx0));
        t = _mm256_mul_ps(_mm256_sub_ps(_mm256_mul_ps(rad, _mm256_set1_ps(c->turn)), _mm256_set1_ps(c->a0)), _mm256_set1_ps(c->k));
      }
      u = _mm256_add_ps(_mm256_mul_ps(_mm256_sub_ps(t, _mm256_set1_ps(c->first)), _mm256_set1_ps(c->scale)), _mm256_set1_ps(0.5f));
    }
    u = _mm256_min_ps(_mm256_max_ps(u, zero), _mm256_set1_ps((float)GSHCPU_RENDER_GRADIENT_TABLE));
    __m256i idx = _mm256_cvttps_epi32(u);
    return _mm256_blendv_epi8(idx, _mm256_set1_epi32(-1), _mm256_castps_si256(none));
  }
}

/** row_f_block over the whole blocks of eight pixels in @p count.  @return the pixels done, a multiple of eight. */
__attribute__((target("avx2"))) static size_t row_f_avx2(const Row_F * c, size_t count, uint32_t * index) {
  size_t i = 0;
  for (; i + 8u <= count; i += 8) {
    _mm256_storeu_si256((__m256i *)(void *)(index + i), row_f_block(c, i));
  }
  return i;
}

/** The last pixels, fewer than eight, from @p from to @p count: a block whose lanes past the end are not kept. */
__attribute__((target("avx2"))) static void row_f_avx2_tail(const Row_F * c, size_t from, size_t count, uint32_t * index) {
  const __m256i lane = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
  const __m256i valid = _mm256_cmpgt_epi32(_mm256_set1_epi32((int)(count - from)), lane);
  _mm256_maskstore_epi32((int *)(void *)(index + from), valid, row_f_block(c, from));
}
#endif

void gshcpu_render_gradient_vector(int enable) {
#if defined(GRADIENT_AVX2)
  gradient_vector_on = enable;
#else
  (void)enable;
#endif
}

/** The indices of @p count pixels by the single-precision rows (the vector kernel's eight at a time, the scalar function for the rest), NONE where @p cov is 0. */
static void table_row_f(const Row_F * c, size_t count, const uint8_t * cov, uint32_t * index) {
  size_t done = 0;
#if defined(GRADIENT_AVX2)
  if (gradient_have_avx2()) {
    done = row_f_avx2(c, count, index);
    if (done < count) {
      row_f_avx2_tail(c, done, count, index);
      done = count;
    }
  }
#endif
  for (size_t i = done; i < count; i++) {
    index[i] = row_f_pixel(c, (float)(int32_t)i);
  }
  for (size_t i = 0; i < count; i++) {
    if (!cov[i]) {
      index[i] = GSHCPU_RENDER_GRADIENT_NONE; /* (a pixel not covered is not asked about: the vector kernel did it for the pixels it computed) */
    }
  }
}

void gshcpu_render_gradient_table_row(const GSHCPU_Gradient * g, double qx0, double qy, size_t count, const uint8_t * cov,
    uint32_t * index) {
  if (g->degenerate) {
    for (size_t i = 0; i < count; i++) {
      index[i] = GSHCPU_RENDER_GRADIENT_NONE;
    }
    return;
  }
  const int padded = g->period > 0.0 && g->extend == GSHSCENE_EXTEND_PAD;
  if (padded && g->kind == GSHSCENE_GRADIENT_LINEAR && g->dt_dx - g->dt_dx == 0.0) {
    /* The offset on the line is affine in the pixel: its value at the first center, and a step that is the gradient's. */
    double px0, py0, t0 = 0.0;
    gshcpu_render_gradient_unmap(g, qx0, qy, &px0, &py0);
    (void)gshcpu_render_gradient_param(g, px0, py0, &t0);
    if (t0 - t0 == 0.0) {
      Row_F c = g->rowf;
      c.ua0 = (float)((t0 - g->o_first) * g->scale + 0.5);
      table_row_f(&c, count, cov, index);
      return;
    }
  }
  if (padded && g->kind != GSHSCENE_GRADIENT_LINEAR && (g->kind == GSHSCENE_GRADIENT_RADIAL || g->a0 != g->a1)) {
    /* The gradient-space point is affine in the pixel's x: its value at the first center, and a step that is the map's. */
    double px0, py0;
    gshcpu_render_gradient_unmap(g, qx0, qy, &px0, &py0);
    Row_F c = g->rowf;
    c.x0 = (float)(px0 - g->p[0]);
    c.y0 = (float)(py0 - g->p[1]);
    table_row_f(&c, count, cov, index);
    return;
  }
  if (g->kind == GSHSCENE_GRADIENT_LINEAR) {
    /* The parameter is affine in the pixel's x: its value at the first center and its step. */
    double px0, py0, px1, py1, t0 = 0.0, t1 = 0.0;
    gshcpu_render_gradient_unmap(g, qx0, qy, &px0, &py0);
    gshcpu_render_gradient_unmap(g, qx0 + 1.0, qy, &px1, &py1);
    (void)gshcpu_render_gradient_param(g, px0, py0, &t0);
    (void)gshcpu_render_gradient_param(g, px1, py1, &t1);
    const double dt = t1 - t0;
    for (size_t i = 0; i < count; i++) {
      index[i] = cov[i] ? table_index(g, t0 + dt * (double)(int32_t)i) : GSHCPU_RENDER_GRADIENT_NONE;
    }
    return;
  }
  /* The gradient-space point is affine in the pixel's x: its value at the first center and its step. */
  double px0, py0, px1, py1;
  gshcpu_render_gradient_unmap(g, qx0, qy, &px0, &py0);
  gshcpu_render_gradient_unmap(g, qx0 + 1.0, qy, &px1, &py1);
  const double dpx = px1 - px0;
  const double dpy = py1 - py0;
  if (g->kind == GSHSCENE_GRADIENT_RADIAL) {
    for (size_t i = 0; i < count; i++) {
      double t;
      index[i] = cov[i] && radial_param(g, px0 + dpx * (double)(int32_t)i, py0 + dpy * (double)(int32_t)i, &t) ? table_index(g, t)
                                                                                                               : GSHCPU_RENDER_GRADIENT_NONE;
    }
    return;
  }
  for (size_t i = 0; i < count; i++) {
    if (!cov[i]) {
      index[i] = GSHCPU_RENDER_GRADIENT_NONE;
      continue;
    }
    const double theta = angle_fast(g, px0 + dpx * (double)(int32_t)i - g->p[0], py0 + dpy * (double)(int32_t)i - g->p[1]);
    double t;
    if (g->a0 == g->a1) {
      t = theta < g->a0 ? -GSHCPU_RENDER_T_LIMIT : GSHCPU_RENDER_T_LIMIT;
    } else {
      t = (theta - g->a0) / (g->a1 - g->a0);
    }
    index[i] = table_index(g, t);
  }
}
