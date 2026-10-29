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
 * `AREA` (IC7, IC9; shape-scene's IP7): the exact mean of the extended image over the preimage of a pixel, a parallelogram.
 * The cells of the extended image that the parallelogram touches are visited a row of cells at a time.  In a row the cells
 * the parallelogram covers entirely form one run, whose texels are summed as a run (a repeating axis sums its whole periods
 * by the row's sum over the window, and a padded axis the cells past the window as a count of the edge texel); only the cells
 * its boundary crosses are clipped one by one.  The weights are areas, the sums are compensated and in a fixed order, the
 * mean is divided once at the end, and no operation is fused.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/math/basic.h>

#include <ghoti.io/cutil/safemath.h>

#include <string.h>

/** The largest side of a preimage in texels that is drawn at all: the sampling domain's width, `2^33`. */
#define WIDEST 8589934592.0

/* --- a compensated sum (Neumaier), in a fixed order -------------------------------------------------------------- */

typedef struct Sum {
  double s[4];
  double c[4];
} Sum;

static void sum_add(Sum * a, const double v[4]) {
  for (int k = 0; k < 4; k++) {
    const double t = a->s[k] + v[k];
    const double big = a->s[k] < 0.0 ? -a->s[k] : a->s[k];
    const double small = v[k] < 0.0 ? -v[k] : v[k];
    if (big >= small) {
      a->c[k] += (a->s[k] - t) + v[k];
    } else {
      a->c[k] += (v[k] - t) + a->s[k];
    }
    a->s[k] = t;
  }
}

/** @p v scaled by @p w, added. */
static void sum_add_scaled(Sum * a, double w, const double v[4]) {
  const double t[4] = {w * v[0], w * v[1], w * v[2], w * v[3]};
  sum_add(a, t);
}

static void sum_value(const Sum * a, double out[4]) {
  for (int k = 0; k < 4; k++) {
    out[k] = a->s[k] + a->c[k];
  }
}

/* --- the sum of a row of the window ------------------------------------------------------------------------------ */

/** The texels `[lo, hi)` of window row @p iy summed pairwise in a fixed tree of blocks of 1024 (IC7). */
static void tree_sum(const GSHCPU_Image * im, uint32_t iy, uint32_t lo, uint32_t hi, double out[4]) {
  const uint32_t block = 1024u;
  if (hi - lo <= block) {
    double s[4] = {0.0, 0.0, 0.0, 0.0};
    for (uint32_t a = lo; a < hi; a++) {
      double t[4];
      gshcpu_render_image_window_texel(im, a, iy, t);
      for (int k = 0; k < 4; k++) {
        s[k] = s[k] + t[k];
      }
    }
    memcpy(out, s, sizeof s);
    return;
  }
  const uint32_t blocks = (hi - lo + block - 1u) / block;
  const uint32_t mid = lo + (blocks / 2u) * block;
  double left[4];
  double right[4];
  tree_sum(im, iy, lo, mid, left);
  tree_sum(im, iy, mid, hi, right);
  for (int k = 0; k < 4; k++) {
    out[k] = left[k] + right[k];
  }
}

/** The sum of window row @p iy over the window, kept for the next pixel (the same doubles whether found or made). */
static void row_sum(GSHCPU_Image * im, int64_t iy, double out[4]) {
  GSHCPU_Image_Row_Sum * slot = &im->row_sums[(uint64_t)iy % GSHCPU_IMAGE_ROW_SUMS];
  if (!slot->valid || slot->row != iy) {
    tree_sum(im, (uint32_t)iy, im->x0, im->x1, slot->sum);
    slot->row = iy;
    slot->valid = 1;
  }
  memcpy(out, slot->sum, sizeof slot->sum);
  im->visits += (uint64_t)(im->x1 - im->x0); /* counted whether or not it was kept: the work of a pixel is the same either way */
}

/* --- the extended image's cells ---------------------------------------------------------------------------------- */

/** The texel the cell (@p i, @p ty) of the extended image holds, @p ty a row of the window; added with weight @p w. */
static void add_cell(GSHCPU_Image * im, Sum * acc, double w, int64_t i, int64_t ty) {
  const int64_t ix = gshcpu_render_image_edge(i, (int64_t)im->x0, (int64_t)im->x1, im->extend_x);
  im->visits++;
  if (ix < 0) {
    return;
  }
  double t[4];
  gshcpu_render_image_window_texel(im, (uint32_t)ix, (uint32_t)ty, t);
  sum_add_scaled(acc, w, t);
}

/**
 * The texels of the cells `[ia, ib)` of the row of the window @p ty (IP6), each weighing one, added to @p acc: the cells past a
 * padded window as a count of the edge texel, the whole periods of a repeating axis as that many sums of the row, the rest
 * one by one.
 */
static void add_run(GSHCPU_Image * im, Sum * acc, int64_t ia, int64_t ib, int64_t ty) {
  if (ib <= ia) {
    return;
  }
  const int64_t x0 = (int64_t)im->x0;
  const int64_t x1 = (int64_t)im->x1;
  const int64_t n = x1 - x0;
  switch (im->extend_x) {
    case GSHSCENE_IMAGE_EXTEND_PAD: {
      const int64_t left = ia < x0 ? (ib < x0 ? ib : x0) - ia : 0;
      const int64_t right = ib > x1 ? ib - (ia > x1 ? ia : x1) : 0;
      double t[4];
      if (left > 0) {
        gshcpu_render_image_window_texel(im, im->x0, (uint32_t)ty, t);
        sum_add_scaled(acc, gshcpu_render_i64_to_double(left), t);
        im->visits++;
      }
      for (int64_t i = ia > x0 ? ia : x0; i < (ib < x1 ? ib : x1); i++) {
        gshcpu_render_image_window_texel(im, (uint32_t)i, (uint32_t)ty, t);
        sum_add(acc, t);
        im->visits++;
      }
      if (right > 0) {
        gshcpu_render_image_window_texel(im, im->x1 - 1u, (uint32_t)ty, t);
        sum_add_scaled(acc, gshcpu_render_i64_to_double(right), t);
        im->visits++;
      }
      break;
    }
    case GSHSCENE_IMAGE_EXTEND_NONE: {
      double t[4];
      for (int64_t i = ia > x0 ? ia : x0; i < (ib < x1 ? ib : x1); i++) {
        gshcpu_render_image_window_texel(im, (uint32_t)i, (uint32_t)ty, t);
        sum_add(acc, t);
        im->visits++;
      }
      break;
    }
    default: {
      const int64_t period = im->extend_x == GSHSCENE_IMAGE_EXTEND_REPEAT ? n : 2 * n;
      const int64_t whole = (ib - ia) / period;
      int64_t from = ia;
      if (whole > 0) {
        double s[4];
        row_sum(im, ty, s);
        sum_add_scaled(acc, gshcpu_render_i64_to_double(whole) * (im->extend_x == GSHSCENE_IMAGE_EXTEND_REPEAT ? 1.0 : 2.0), s);
        from = ia + whole * period;
      }
      for (int64_t i = from; i < ib; i++) {
        add_cell(im, acc, 1.0, i, ty);
      }
      break;
    }
  }
}

/* --- the parallelogram ---------------------------------------------------------------------------------------- */

/** The area of the convex polygon @p p (@p n vertices) clipped to the unit square, coordinates relative to the cell. */
static double clipped_area(const double * vx, const double * vy, int64_t ci, int64_t cj) {
  double px[16];
  double py[16];
  int n = 4;
  for (int k = 0; k < 4; k++) {
    px[k] = vx[k] - gshcpu_render_i64_to_double(ci);
    py[k] = vy[k] - gshcpu_render_i64_to_double(cj);
  }
  for (int edge = 0; edge < 4 && n > 0; edge++) {
    double qx[16];
    double qy[16];
    int m = 0;
    for (int k = 0; k < n; k++) {
      const double ax = px[k];
      const double ay = py[k];
      const double bx = px[(k + 1) % n];
      const double by = py[(k + 1) % n];
      const double da = edge == 0 ? ax : edge == 1 ? 1.0 - ax : edge == 2 ? ay : 1.0 - ay;
      const double db = edge == 0 ? bx : edge == 1 ? 1.0 - bx : edge == 2 ? by : 1.0 - by;
      if (da >= 0.0) {
        qx[m] = ax;
        qy[m] = ay;
        m++;
      }
      if ((da >= 0.0) != (db >= 0.0)) {
        const double t = da / (da - db);
        qx[m] = ax + t * (bx - ax);
        qy[m] = ay + t * (by - ay);
        m++;
      }
    }
    memcpy(px, qx, (size_t)m * sizeof px[0]);
    memcpy(py, qy, (size_t)m * sizeof py[0]);
    n = m;
  }
  double a = 0.0;
  for (int k = 0; k < n; k++) {
    a += px[k] * py[(k + 1) % n] - px[(k + 1) % n] * py[k];
  }
  return (a < 0.0 ? -a : a) / 2.0;
}

/** The extent in x of the parallelogram's cross-section at height @p y (the vertices in order). */
static void cross_section(const double * vx, const double * vy, double y, double * lo, double * hi) {
  double l = 0.0;
  double r = 0.0;
  int found = 0;
  for (int k = 0; k < 4; k++) {
    const double ax = vx[k];
    const double ay = vy[k];
    const double bx = vx[(k + 1) % 4];
    const double by = vy[(k + 1) % 4];
    double xs[2];
    int count = 0;
    if (ay == by) {
      if (ay == y) {
        xs[count++] = ax;
        xs[count++] = bx;
      }
    } else if ((ay <= y && y <= by) || (by <= y && y <= ay)) {
      xs[count++] = ax + ((y - ay) / (by - ay)) * (bx - ax);
    }
    for (int i = 0; i < count; i++) {
      if (!found) {
        l = r = xs[i];
        found = 1;
      } else {
        l = xs[i] < l ? xs[i] : l;
        r = xs[i] > r ? xs[i] : r;
      }
    }
  }
  *lo = l;
  *hi = r;
}

/** The extent in x of the parallelogram within the strip `[j, j + 1]`, by clipping it to the strip. */
static void strip_extent(const double * vx, const double * vy, int64_t j, double * lo, double * hi) {
  double px[16];
  double py[16];
  int n = 4;
  memcpy(px, vx, 4 * sizeof px[0]);
  memcpy(py, vy, 4 * sizeof py[0]);
  for (int edge = 0; edge < 2 && n > 0; edge++) {
    double qx[16];
    double qy[16];
    int m = 0;
    for (int k = 0; k < n; k++) {
      const double ax = px[k];
      const double ay = py[k];
      const double bx = px[(k + 1) % n];
      const double by = py[(k + 1) % n];
      const double da = edge == 0 ? ay - gshcpu_render_i64_to_double(j) : gshcpu_render_i64_to_double(j + 1) - ay;
      const double db = edge == 0 ? by - gshcpu_render_i64_to_double(j) : gshcpu_render_i64_to_double(j + 1) - by;
      if (da >= 0.0) {
        qx[m] = ax;
        qy[m] = ay;
        m++;
      }
      if ((da >= 0.0) != (db >= 0.0)) {
        const double t = da / (da - db);
        qx[m] = ax + t * (bx - ax);
        qy[m] = ay + t * (by - ay);
        m++;
      }
    }
    memcpy(px, qx, (size_t)m * sizeof px[0]);
    memcpy(py, qy, (size_t)m * sizeof py[0]);
    n = m;
  }
  double l = n > 0 ? px[0] : 0.0;
  double r = l;
  for (int k = 1; k < n; k++) {
    l = px[k] < l ? px[k] : l;
    r = px[k] > r ? px[k] : r;
  }
  *lo = l;
  *hi = r;
}

static double dmin(double a, double b) {
  return a < b ? a : b;
}

static double dmax(double a, double b) {
  return a > b ? a : b;
}

/**
 * A parallelogram with edges along the axes: a cell's weight is the product of its overlap in x and in y, formed from the
 * same operands wherever the cell lies; a run of cells the parallelogram covers across is weighed by its row's overlap once.
 */
static void rectangle(GSHCPU_Image * im, const double * vx, const double * vy, Sum * acc) {
  const double xl = dmin(dmin(vx[0], vx[1]), dmin(vx[2], vx[3]));
  const double xr = dmax(dmax(vx[0], vx[1]), dmax(vx[2], vx[3]));
  const double yl = dmin(dmin(vy[0], vy[1]), dmin(vy[2], vy[3]));
  const double yr = dmax(dmax(vy[0], vy[1]), dmax(vy[2], vy[3]));
  const int64_t ci0 = gshcpu_render_trunc_i64(gmath_floor(xl));
  const int64_t ci1 = gshcpu_render_trunc_i64(gmath_floor(xr)) + (xr > gmath_floor(xr) ? 1 : 0); /* one past the last */
  const int64_t fa = gshcpu_render_trunc_i64(-gmath_floor(-xl));
  const int64_t fb = gshcpu_render_trunc_i64(gmath_floor(xr));
  const int have_run = fb > fa;
  const int64_t rj0 = gshcpu_render_trunc_i64(gmath_floor(yl));
  const int64_t rj1 = gshcpu_render_trunc_i64(gmath_floor(yr)) + (yr > gmath_floor(yr) ? 1 : 0);
  for (int64_t j = rj0; j < rj1; j++) {
    const double wy = dmin(gshcpu_render_i64_to_double(j + 1), yr) - dmax(gshcpu_render_i64_to_double(j), yl);
    if (!(wy > 0.0)) {
      continue;
    }
    const int64_t ty = gshcpu_render_image_edge(j, (int64_t)im->y0, (int64_t)im->y1, im->extend_y);
    im->visits++;
    for (int64_t i = ci0; i < ci1; i++) {
      if (have_run && i >= fa && i < fb) {
        // The run of whole cells: weighed by the row's overlap once.
        if (ty >= 0) {
          Sum run = {{0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}};
          add_run(im, &run, fa, fb, ty);
          double r[4];
          sum_value(&run, r);
          sum_add_scaled(acc, wy, r);
        }
        i = fb - 1;
        continue;
      }
      const double wx = dmin(gshcpu_render_i64_to_double(i + 1), xr) - dmax(gshcpu_render_i64_to_double(i), xl);
      if (!(wx > 0.0)) {
        continue;
      }
      const double w = wx * wy;
      if (ty >= 0) {
        add_cell(im, acc, w, i, ty);
      }
    }
  }
}

/** The general parallelogram: row by row, the cells it covers entirely as a run and the cells its boundary crosses clipped. */
static void general(GSHCPU_Image * im, const double * vx, const double * vy, Sum * acc) {
  const double ymin = dmin(dmin(vy[0], vy[1]), dmin(vy[2], vy[3]));
  const double ymax = dmax(dmax(vy[0], vy[1]), dmax(vy[2], vy[3]));
  const int64_t j0 = gshcpu_render_trunc_i64(gmath_floor(ymin));
  const int64_t j1 = gshcpu_render_trunc_i64(gmath_floor(ymax)) + (ymax > gmath_floor(ymax) ? 1 : 0);
  for (int64_t j = j0; j < j1; j++) {
    const int64_t ty = gshcpu_render_image_edge(j, (int64_t)im->y0, (int64_t)im->y1, im->extend_y);
    double xmin;
    double xmax;
    int64_t ra = 0;
    int64_t rb = 0; /* the cells covered entirely: [ra, rb) */
    // A row the parallelogram crosses from side to side with no vertex inside it: the cross-section is a trapezoid, so the cells
    // between its two ends at the row's bottom and top are covered entirely.  A row with a vertex in it is clipped cell by cell.
    int vertex_inside = 0;
    for (int k = 0; k < 4; k++) {
      vertex_inside = vertex_inside || (vy[k] > gshcpu_render_i64_to_double(j) && vy[k] < gshcpu_render_i64_to_double(j + 1));
    }
    if (!vertex_inside && ymin <= gshcpu_render_i64_to_double(j) && gshcpu_render_i64_to_double(j + 1) <= ymax) {
      double l0;
      double r0;
      double l1;
      double r1;
      cross_section(vx, vy, gshcpu_render_i64_to_double(j), &l0, &r0);
      cross_section(vx, vy, gshcpu_render_i64_to_double(j + 1), &l1, &r1);
      xmin = dmin(l0, l1);
      xmax = dmax(r0, r1);
      ra = gshcpu_render_trunc_i64(-gmath_floor(-dmax(l0, l1)));
      rb = gshcpu_render_trunc_i64(gmath_floor(dmin(r0, r1)));
    } else {
      strip_extent(vx, vy, j, &xmin, &xmax);
    }
    const int64_t i0 = gshcpu_render_trunc_i64(gmath_floor(xmin));
    const int64_t i1 = gshcpu_render_trunc_i64(gmath_floor(xmax)) + (xmax > gmath_floor(xmax) ? 1 : 0);
    const int have_run = rb > ra;
    for (int64_t i = i0; i < i1; i++) {
      if (have_run && i >= ra && i < rb) {
        if (ty >= 0) {
          add_run(im, acc, ra, rb, ty);
        }
        i = rb - 1;
        continue;
      }
      const double w = clipped_area(vx, vy, i, j);
      im->visits++;
      if (!(w > 0.0)) {
        continue;
      }
      if (ty >= 0) {
        add_cell(im, acc, w, i, ty);
      }
    }
  }
}

void gshcpu_render_image_region(GSHCPU_Image * im, const double origin[2], const double ea[2], const double eb[2],
    double out[4]) {
  const double vx[4] = {origin[0], origin[0] + ea[0], origin[0] + ea[0] + eb[0], origin[0] + eb[0]};
  const double vy[4] = {origin[1], origin[1] + ea[1], origin[1] + ea[1] + eb[1], origin[1] + eb[1]};
  double area = gshcpu_render_det2(ea[0], eb[1], ea[1], eb[0]);
  area = area < 0.0 ? -area : area;
  Sum acc = {{0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}};
  if ((ea[1] == 0.0 && eb[0] == 0.0) || (ea[0] == 0.0 && eb[1] == 0.0)) {
    rectangle(im, vx, vy, &acc);
  } else {
    general(im, vx, vy, &acc);
  }
  double mean[4];
  sum_value(&acc, mean);
  for (int k = 0; k < 4; k++) {
    mean[k] = mean[k] / area;
  }
  if (im->convert_out) {
    gshcpu_render_convert_premultiplied(&im->out, mean, mean);
  }
  memcpy(out, mean, sizeof mean);
}

/* --- the bound (IC9) ------------------------------------------------------------------------------------------- */

static uint64_t sat_mul(uint64_t a, uint64_t b) {
  uint64_t r;
  return gcu_safe_mul_u64(a, b, &r) ? r : UINT64_MAX;
}

static uint64_t sat_add(uint64_t a, uint64_t b) {
  uint64_t r;
  return gcu_safe_add_u64(a, b, &r) ? r : UINT64_MAX;
}

int gshcpu_render_image_area_bound(const GSHCPU_Image * im, uint64_t * out) {
  const double ex = (im->inv[0] < 0.0 ? -im->inv[0] : im->inv[0]) + (im->inv[1] < 0.0 ? -im->inv[1] : im->inv[1]);
  const double ey = (im->inv[2] < 0.0 ? -im->inv[2] : im->inv[2]) + (im->inv[3] < 0.0 ? -im->inv[3] : im->inv[3]);
  if (!(ex <= WIDEST && ey <= WIDEST)) {
    return 0; /* past the domain: the preimage of one pixel is wider than the domain is */
  }
  const uint64_t cx = (uint64_t)gshcpu_render_trunc_i64(gmath_ceil(ex)) + 2u; /* the columns of cells the preimage touches, at most */
  const uint64_t cy = (uint64_t)gshcpu_render_trunc_i64(gmath_ceil(ey)) + 2u;
  const uint64_t n = (uint64_t)(im->x1 - im->x0);
  const uint64_t run = cx < sat_mul(3u, n) ? cx : sat_mul(3u, n); /* a row's run: the window's cells, a whole period's sum, a period's rest */
  uint64_t b = sat_mul(cy, run);
  /* A cell clipped is counted once for the clip and once for the texel it is weighed with. */
  b = sat_add(b, sat_mul(4u, sat_add(cx, cy))); /* the cells the boundary crosses */
  b = sat_add(b, sat_mul(8u, cx)); /* the rows holding a vertex are clipped across */
  b = sat_add(b, sat_mul(cy, 2u)); /* a row's own bookkeeping, and a rectangle's partial cells */
  *out = sat_add(b, 8u);
  return 1;
}
