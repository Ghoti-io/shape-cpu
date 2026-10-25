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
 * The lean cover (the fast stroke coverage): the coverage of one stroke part's outline under the nonzero rule, summed in floats
 * as the area and cover of each cell an edge crosses, by rows of sparse cells.  An edge adds to the cells it crosses and to
 * nothing else, the cells a row holds are kept in a list and sorted, and the sweep of a row visits them and the runs between
 * them, so the work follows the outline and never the rectangle it lies in.  Where pieces of the outline overlap the
 * coverages add and the magnitude is held at one, the way the rasterizers this is compared with draw them; the exact union
 * of the default coverage is not made.
 *
 * The scratch is the sink's accumulator, a band of rows at a time, and is left all zero, so the next user finds it as it
 * expects.  The sums are floats, `+ - * /`, so that every target that rounds as IEEE says gives the
 * same bits (no contraction: the build says so).
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** The scratch of a band of @p rows rows of @p width cells, in @p memory: the row flags, the sums and the bits of the cells noted. */
typedef struct Lean_Band {
  uint32_t * n;   /* 1 for a row that has a cell noted */
  float * a;      /* the sums of each row: width + 2 */
  uint64_t * bit; /* which cells of a row are noted, a bit each: words per row */
  size_t stride;  /* width + 2 */
  size_t words;   /* the 64-bit words of the bits of a row */
} Lean_Band;

size_t gshcpu_lean_bytes(size_t width, size_t rows) {
  size_t stride = width + 2u;
  size_t words = (stride + 63u) / 64u;
  return rows * 4u + 8u + rows * stride * sizeof(float) + 8u + rows * words * sizeof(uint64_t) + 24u;
}

static void lean_band(Lean_Band * b, void * memory, size_t width, size_t rows) {
  unsigned char * p = (unsigned char *)memory;
  b->stride = width + 2u;
  b->words = (b->stride + 63u) / 64u;
  b->n = (uint32_t *)(void *)p;
  p += (rows * 4u + 7u) & ~(size_t)7u;
  b->a = (float *)(void *)p;
  p += (rows * b->stride * sizeof(float) + 7u) & ~(size_t)7u;
  b->bit = (uint64_t *)(void *)p;
}

/** Add @p v to cell @p x of row @p y, noting the cell when it is new. */
static inline void lean_add(Lean_Band * b, int y, int x, float v) {
  b->n[y] = 1;
  b->bit[(size_t)y * b->words + ((size_t)x >> 6)] |= (uint64_t)1 << (x & 63);
  b->a[(size_t)y * b->stride + (size_t)x] += v;
}

/**
 * The part of one row of an edge: from (@p xa, @p ya) down to (@p xb, @p yb) in the band's frame (row @p y, 0 at its top and x at the
 * window's left), directed down when @p dir is 1 and up when it is -1.  What is left of the window is a cover at its first column;
 * what is right of it is nothing.
 */
static void lean_row(Lean_Band * b, int y, int width, double xa, double xb, float d) {
  if (xa > xb) {
    double t = xa;
    xa = xb;
    xb = t;
  }
  const double span = xb - xa;
  if (xb <= 0.0) {
    lean_add(b, y, 0, d);
    return;
  }
  if (xa >= (double)width) {
    return;
  }
  if (xa < 0.0 || xb > (double)width) {
    /* The edge leaves the window inside this row: the share of its height outside is a cover at the window's left or nothing. */
    const double lo = xa < 0.0 ? 0.0 : xa;
    const double hi = xb > (double)width ? (double)width : xb;
    if (xa < 0.0) {
      lean_add(b, y, 0, (float)((double)d * (lo - xa) / span));
    }
    d = (float)((double)d * (hi - lo) / span);
    xa = lo;
    xb = hi;
  }
  /* xa and xb are not negative here, so a cast is their floor, and one more when it falls short is the ceiling of xb */
  const int xai = (int)xa;
  const double xaf = (double)xai;
  const int xbt = (int)xb;
  const int xbi = xbt + ((double)xbt < xb);
  const double xbc = (double)xbi;
  if (xbi <= xai + 1) {
    const float xmf = (float)(0.5 * (xa + xb) - xaf);
    lean_add(b, y, xai, d - d * xmf);
    lean_add(b, y, xai + 1, d * xmf);
    return;
  }
  const float s = (float)(1.0 / (xb - xa));
  const float x0f = (float)(xa - xaf);
  const float a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
  const float x1f = (float)(xb - xbc + 1.0);
  const float am = 0.5f * s * x1f * x1f;
  lean_add(b, y, xai, d * a0);
  if (xbi == xai + 2) {
    lean_add(b, y, xai + 1, d * (1.0f - a0 - am));
  } else {
    const float a1 = s * (1.5f - x0f);
    lean_add(b, y, xai + 1, d * (a1 - a0));
    for (int xi = xai + 2; xi < xbi - 1; xi++) {
      lean_add(b, y, xi, d * s);
    }
    const float a2 = a1 + (float)(xbi - xai - 3) * s;
    lean_add(b, y, xbi - 1, d * (1.0f - a2 - am));
  }
  lean_add(b, y, xbi, d * am);
}

/** One edge into the band whose first row is @p top (rows @p rows), in the window's frame. */
static void lean_edge(Lean_Band * b, int top, int rows, int width, double x0, double y0, double x1, double y1) {
  if (y0 == y1) {
    return;
  }
  float dir = 1.0f;
  if (y0 > y1) {
    double t = x0;
    x0 = x1;
    x1 = t;
    t = y0;
    y0 = y1;
    y1 = t;
    dir = -1.0f;
  }
  const double lo = (double)top;
  const double hi = (double)(top + rows);
  if (y1 <= lo || y0 >= hi) {
    return;
  }
  const double dxdy = (x1 - x0) / (y1 - y0);
  double x = x0;
  if (y0 < lo) {
    x += (lo - y0) * dxdy;
    y0 = lo;
  }
  if (y1 > hi) {
    y1 = hi;
  }
  /* y0 is not below the band's top, which is not negative, so a cast is its floor */
  const int first = (int)y0;
  const int last = (int)y1 + ((double)(int)y1 < y1);
  for (int y = first; y < last; y++) {
    const double ya = y > y0 ? (double)y : y0;
    const double yb = (double)(y + 1) < y1 ? (double)(y + 1) : y1;
    const double dy = yb - ya;
    const double xn = x + dxdy * dy;
    lean_row(b, y - top, width, x, xn, (float)dy * dir);
    x = xn;
  }
}

/** The coverage of an accumulated sum, 0 to 255. */
static inline uint8_t lean_level(float acc) {
  float c = acc < 0.0f ? -acc : acc;
  if (c > 1.0f) {
    c = 1.0f;
  }
  return (uint8_t)(c * 255.0f + 0.5f);
}

/** The next noted cell at or after @p x in a row's bits (@p words of them), or -1; the bit is cleared. */
static inline int lean_next(uint64_t * bit, size_t words, int x) {
  size_t w = (size_t)x >> 6;
  if (w >= words) {
    return -1;
  }
  uint64_t v = bit[w] & (~(uint64_t)0 << (x & 63));
  while (v == 0) {
    if (++w == words) {
      return -1;
    }
    v = bit[w];
  }
  const int at = (int)(w << 6) + __builtin_ctzll(v);
  bit[w] &= ~((uint64_t)1 << (at & 63));
  return at;
}

/** Sweep the band's rows: each noted cell and the run after it, handed to @p fn as runs of nonzero coverage. */
static void lean_sweep(Lean_Band * b, int rows, int width, int64_t x0, int64_t y0, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx) {
  for (int y = 0; y < rows; y++) {
    if (!b->n[y]) {
      continue;
    }
    b->n[y] = 0;
    float * a = b->a + (size_t)y * b->stride;
    uint64_t * bit = b->bit + (size_t)y * b->words;
    float acc = 0.0f;
    int from = -1; /* the first column of the run open, or -1 */
    int x = lean_next(bit, b->words, 0);
    while (x >= 0) {
      const int nx = lean_next(bit, b->words, x + 1);
      acc += a[x];
      a[x] = 0.0f;
      if (x < width) {
        const uint8_t m = lean_level(acc);
        /* the cell's own coverage is its pixel's; the pixels after it up to the next cell have the sum alone, which is the same sum */
        const int end = nx >= 0 ? (nx < width ? nx : width) : width;
        if (m != 0) {
          if (from < 0) {
            from = x;
          }
          memset(line + (x - from), m, (size_t)(end - x));
        } else if (from >= 0) {
          fn(ctx, y0 + y, x0 + from, (size_t)(x - from), line);
          from = -1;
        }
      }
      x = nx;
    }
    if (from >= 0) {
      fn(ctx, y0 + y, x0 + from, (size_t)(width - from), line);
    }
  }
}

GSHCPU_Result gshcpu_render_lean_cover(const GSHGEOM_Flat_Path * path, int64_t shift_x, int64_t shift_y, GSHCPU_Rect rect,
    void * scratch, size_t scratch_bytes, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx) {
  const int64_t width = rect.x1 - rect.x0;
  const int64_t height = rect.y1 - rect.y0;
  if (!path || width <= 0 || height <= 0 || width > 65000) {
    return GSHCPU_OK;
  }
  const size_t rows_max = (size_t)GSHCPU_RENDER_BAND_ROWS;
  if (scratch_bytes < gshcpu_lean_bytes((size_t)width, rows_max)) {
    return GSHCPU_ERR_INTERNAL;
  }
  Lean_Band b;
  lean_band(&b, scratch, (size_t)width, rows_max);
  const GSHGEOM_Device_Point * pt = gshgeom_flat_path_points(path);
  const size_t subpaths = gshgeom_flat_path_subpath_count(path);
  const double ox = (double)(shift_x - rect.x0);
  const double oy = (double)(shift_y - rect.y0);
  for (int64_t top = 0; top < height; top += (int64_t)rows_max) {
    const int rows = (int)(height - top < (int64_t)rows_max ? height - top : (int64_t)rows_max);
    for (size_t i = 0; i < subpaths; i++) {
      GSHGEOM_Flat_Subpath sub;
      if (gshgeom_flat_path_subpath(path, i, &sub) != GSHGEOM_OK) {
        return GSHCPU_ERR_INTERNAL;
      }
      for (size_t k = 0; k < sub.count; k++) {
        const GSHGEOM_Device_Point p = pt[sub.first + k];
        const GSHGEOM_Device_Point q = pt[sub.first + (k + 1) % sub.count];
        lean_edge(&b, (int)top, rows, (int)width, (double)p.x / 64.0 + ox, (double)p.y / 64.0 + oy, (double)q.x / 64.0 + ox,
            (double)q.y / 64.0 + oy);
      }
    }
    /* the runs are handed over in the paths' frame, as the sparse sweep's are: the receiver adds the shift */
    lean_sweep(&b, rows, (int)width, rect.x0 - shift_x, rect.y0 + top - shift_y, line, fn, ctx);
  }
  return GSHCPU_OK;
}
