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

#if defined(GRADIENT_AVX2)
/** Whether this processor has AVX2, asked once (a race on it only repeats the question). */
static int gradient_have_avx2(void) {
  static int have = -1;
  int h = have;
  if (h < 0) {
    __builtin_cpu_init();
    h = __builtin_cpu_supports("avx2") ? 1 : 0;
    have = h;
  }
  return h;
}

/** Four table indices from four parameters on a line padded at both ends; NONE in the lanes of @p none. */
__attribute__((target("avx2"))) static inline __attribute__((always_inline)) void store_indices(const GSHCPU_Gradient * g, __m256d t,
    __m256d none, const uint8_t * cov, uint32_t * index) {
  const __m256d half = _mm256_set1_pd(0.5);
  const __m256d top = _mm256_set1_pd((double)GSHCPU_RENDER_GRADIENT_TABLE);
  __m256d u = _mm256_add_pd(_mm256_mul_pd(_mm256_sub_pd(t, _mm256_set1_pd(g->o_first)), _mm256_set1_pd(g->scale)), half);
  u = _mm256_max_pd(u, _mm256_setzero_pd()); /* below the line, and a NaN, is the first point */
  u = _mm256_min_pd(u, top);
  __m128i idx = _mm256_cvttpd_epi32(u);
  uint32_t c4;
  memcpy(&c4, cov, sizeof c4);
  const __m128i uncovered = _mm_cmpeq_epi32(_mm_cvtepu8_epi32(_mm_cvtsi32_si128((int)c4)), _mm_setzero_si128());
  const __m128i skip = _mm_or_si128(uncovered, _mm256_cvtpd_epi32(_mm256_and_pd(none, _mm256_set1_pd(1.0)))); /* 1 where none */
  const __m128i drop = _mm_or_si128(uncovered, _mm_cmpgt_epi32(skip, _mm_setzero_si128()));
  idx = _mm_blendv_epi8(idx, _mm_set1_epi32(-1), drop);
  _mm_storeu_si128((__m128i *)(void *)index, idx);
}

/**
 * table_row for a radial or a sweep gradient on a line padded at both ends, four pixels at a time: the same operations as
 * radial_param and angle_fast, the choices made with masks.  @return the pixels done, a multiple of four.
 */
__attribute__((target("avx2"))) static size_t table_row_avx2(const GSHCPU_Gradient * g, double px0, double py0, double dpx, double dpy,
    size_t count, const uint8_t * cov, uint32_t * index) {
  const __m256d lane = _mm256_set_pd(3.0, 2.0, 1.0, 0.0);
  const __m256d zero = _mm256_setzero_pd();
  const __m256d one = _mm256_set1_pd(1.0);
  size_t i = 0;
  for (; i + 4u <= count; i += 4) {
    const __m256d k = _mm256_add_pd(_mm256_set1_pd((double)(int32_t)i), lane);
    const __m256d x = _mm256_sub_pd(_mm256_add_pd(_mm256_set1_pd(px0), _mm256_mul_pd(_mm256_set1_pd(dpx), k)), _mm256_set1_pd(g->p[0]));
    const __m256d y = _mm256_sub_pd(_mm256_add_pd(_mm256_set1_pd(py0), _mm256_mul_pd(_mm256_set1_pd(dpy), k)), _mm256_set1_pd(g->p[1]));
    __m256d t;
    __m256d none;
    if (g->kind == GSHSCENE_GRADIENT_RADIAL) {
      const __m256d r0 = _mm256_set1_pd(g->r0);
      const __m256d dr = _mm256_set1_pd(g->dr);
      const __m256d b = _mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(x, _mm256_set1_pd(g->n[0])), _mm256_mul_pd(y, _mm256_set1_pd(g->n[1]))),
          _mm256_set1_pd(g->r0 * g->dr));
      const __m256d c = _mm256_sub_pd(_mm256_add_pd(_mm256_mul_pd(x, x), _mm256_mul_pd(y, y)), _mm256_set1_pd(g->r0 * g->r0));
      if (g->a == 0.0) {
        t = _mm256_div_pd(c, _mm256_mul_pd(_mm256_set1_pd(2.0), b));
        none = _mm256_or_pd(_mm256_cmp_pd(b, zero, _CMP_EQ_OQ),
            _mm256_cmp_pd(_mm256_add_pd(r0, _mm256_mul_pd(t, dr)), zero, _CMP_NGT_UQ));
      } else {
        const __m256d a = _mm256_set1_pd(g->a);
        const __m256d disc = _mm256_sub_pd(_mm256_mul_pd(b, b), _mm256_mul_pd(a, c));
        const __m256d bad = _mm256_cmp_pd(disc, zero, _CMP_LT_OQ);
        const __m256d root = _mm256_sqrt_pd(_mm256_max_pd(disc, zero));
        const __m256d q = _mm256_blendv_pd(_mm256_add_pd(b, root), _mm256_sub_pd(b, root), _mm256_cmp_pd(b, zero, _CMP_LT_OQ));
        const __m256d u = _mm256_div_pd(q, a);
        const __m256d v = _mm256_div_pd(c, q);
        const __m256d hi = _mm256_blendv_pd(v, u, _mm256_cmp_pd(u, v, _CMP_GT_OQ));
        const __m256d lo = _mm256_blendv_pd(u, v, _mm256_cmp_pd(u, v, _CMP_GT_OQ));
        const __m256d hi_ok = _mm256_cmp_pd(_mm256_add_pd(r0, _mm256_mul_pd(hi, dr)), zero, _CMP_GT_OQ);
        const __m256d lo_ok = _mm256_cmp_pd(_mm256_add_pd(r0, _mm256_mul_pd(lo, dr)), zero, _CMP_GT_OQ);
        const __m256d qz = _mm256_cmp_pd(q, zero, _CMP_EQ_OQ);
        __m256d tt = _mm256_blendv_pd(lo, hi, hi_ok);
        __m256d ok = _mm256_or_pd(hi_ok, lo_ok);
        tt = _mm256_blendv_pd(tt, zero, qz); /* the double root 0 */
        ok = _mm256_blendv_pd(ok, _mm256_cmp_pd(r0, zero, _CMP_GT_OQ), qz);
        t = tt;
        none = _mm256_or_pd(bad, _mm256_xor_pd(ok, _mm256_castsi256_pd(_mm256_set1_epi64x(-1))));
      }
    } else {
      /* G9: the angle in [0, one turn) from the polynomial's arctangent, the axes exactly. */
      const __m256d ax = _mm256_andnot_pd(_mm256_set1_pd(-0.0), x);
      const __m256d ay = _mm256_andnot_pd(_mm256_set1_pd(-0.0), y);
      const __m256d swap = _mm256_cmp_pd(ay, ax, _CMP_GT_OQ);
      const __m256d mx = _mm256_max_pd(ax, ay);
      const __m256d mn = _mm256_min_pd(ax, ay);
      const __m256d r = _mm256_div_pd(mn, mx);
      const __m256d r2 = _mm256_mul_pd(r, r);
      __m256d poly = _mm256_set1_pd(0.006842624897387503);
      poly = _mm256_add_pd(_mm256_mul_pd(poly, r2), _mm256_set1_pd(-0.033725938103610145));
      poly = _mm256_add_pd(_mm256_mul_pd(poly, r2), _mm256_set1_pd(0.0798112049555744));
      poly = _mm256_add_pd(_mm256_mul_pd(poly, r2), _mm256_set1_pd(-0.13247522771595696));
      poly = _mm256_add_pd(_mm256_mul_pd(poly, r2), _mm256_set1_pd(0.19813213509060276));
      poly = _mm256_add_pd(_mm256_mul_pd(poly, r2), _mm256_set1_pd(-0.3331830289944582));
      poly = _mm256_add_pd(_mm256_mul_pd(poly, r2), _mm256_set1_pd(0.9999966347006692));
      __m256d rad = _mm256_mul_pd(poly, r);
      rad = _mm256_blendv_pd(rad, _mm256_sub_pd(_mm256_set1_pd(1.5707963267948966), rad), swap);
      rad = _mm256_blendv_pd(rad, _mm256_sub_pd(_mm256_set1_pd(3.141592653589793), rad), _mm256_cmp_pd(x, zero, _CMP_LT_OQ));
      rad = _mm256_blendv_pd(rad, _mm256_sub_pd(_mm256_set1_pd(TWO_PI), rad), _mm256_cmp_pd(y, zero, _CMP_LT_OQ));
      const __m256d dy0 = _mm256_cmp_pd(y, zero, _CMP_EQ_OQ);
      const __m256d dx0 = _mm256_cmp_pd(x, zero, _CMP_EQ_OQ);
      rad = _mm256_blendv_pd(rad, _mm256_blendv_pd(_mm256_set1_pd(0.0), _mm256_set1_pd(3.141592653589793), _mm256_cmp_pd(x, zero, _CMP_LT_OQ)), dy0);
      rad = _mm256_blendv_pd(rad, _mm256_blendv_pd(_mm256_set1_pd(4.71238898038469), _mm256_set1_pd(1.5707963267948966),
                                      _mm256_cmp_pd(y, zero, _CMP_GT_OQ)), _mm256_andnot_pd(dy0, dx0));
      const __m256d theta = _mm256_mul_pd(rad, _mm256_set1_pd(g->turn / TWO_PI));
      t = _mm256_mul_pd(_mm256_sub_pd(theta, _mm256_set1_pd(g->a0)), _mm256_set1_pd(1.0 / (g->a1 - g->a0)));
      none = zero;
    }
    (void)one;
    store_indices(g, t, none, cov + i, index + i);
  }
  return i;
}
#endif

void gshcpu_render_gradient_table_row(const GSHCPU_Gradient * g, double qx0, double qy, size_t count, const uint8_t * cov,
    uint32_t * index) {
  if (g->degenerate) {
    for (size_t i = 0; i < count; i++) {
      index[i] = GSHCPU_RENDER_GRADIENT_NONE;
    }
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
    if (g->period > 0.0 && g->extend == GSHSCENE_EXTEND_PAD && t0 - t0 == 0.0 && dt - dt == 0.0) {
      /* The offset on the line is affine in the pixel too: no more than a multiply and an add each. */
      const double ua = (t0 - g->o_first) * g->scale + 0.5;
      const double ub = dt * g->scale;
      for (size_t i = 0; i < count; i++) {
        index[i] = cov[i] ? table_point(ua + ub * (double)(int32_t)i) : GSHCPU_RENDER_GRADIENT_NONE;
      }
      return;
    }
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
  size_t done = 0;
#if defined(GRADIENT_AVX2)
  if (count >= 4u && g->period > 0.0 && g->extend == GSHSCENE_EXTEND_PAD && (g->kind == GSHSCENE_GRADIENT_RADIAL || g->a0 != g->a1) &&
      gradient_have_avx2()) {
    done = table_row_avx2(g, px0, py0, dpx, dpy, count, cov, index);
  }
#endif
  cov += done;
  index += done;
  px0 += dpx * (double)(int32_t)done;
  py0 += dpy * (double)(int32_t)done;
  count -= done;
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
