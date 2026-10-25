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

/** The most rows a band of the lean cover holds. */
#define LEAN_BAND_MAX 1024u

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

/**
 * One edge into the band whose first row is @p top (rows @p rows), in the window's frame.  The x at a row's top and bottom is a
 * function of that row boundary alone, `x0 + (y - y0) * dxdy` from the edge's own end, so the sums an edge leaves in a row are the
 * same whatever band it is cut into: a stripe of the window is the picture's rows and nothing else.
 */
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
  const double ys = y0 < lo ? lo : y0;
  const double ye = y1 > hi ? hi : y1;
  /* ys is not below the band's top, which is not negative, so a cast is its floor */
  const int first = (int)ys;
  const int last = (int)ye + ((double)(int)ye < ye);
  double x = x0 + (ys - y0) * dxdy;
  for (int y = first; y < last; y++) {
    const double ya = y > ys ? (double)y : ys;
    const double yb = (double)(y + 1) < ye ? (double)(y + 1) : ye;
    const double dy = yb - ya;
    const double xn = x0 + (yb - y0) * dxdy;
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

/** @p n bytes of @p m at @p p: the runs are short, and a call to memset for three bytes costs more than the three. */
static inline void lean_fill(uint8_t * p, uint8_t m, size_t n) {
  if (n >= 16u) {
    memset(p, m, n);
    return;
  }
  const uint64_t v = (uint64_t)m * 0x0101010101010101ull;
  if (n >= 8u) {
    memcpy(p, &v, 8);
    memcpy(p + n - 8u, &v, 8);
  } else if (n >= 4u) {
    const uint32_t u = (uint32_t)v;
    memcpy(p, &u, 4);
    memcpy(p + n - 4u, &u, 4);
  } else if (n >= 2u) {
    const uint16_t u = (uint16_t)v;
    memcpy(p, &u, 2);
    memcpy(p + n - 2u, &u, 2);
  } else if (n == 1u) {
    p[0] = m;
  }
}

/**
 * Sweep the band's rows: each noted cell and the run after it, handed to @p fn as runs of nonzero coverage.  A cell's run ends at
 * the next noted cell, so a cell is finished when the next one is found (its level is kept until then).
 */
static void lean_sweep(Lean_Band * b, int rows, int width, int64_t x0, int64_t y0, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx) {
  for (int y = 0; y < rows; y++) {
    if (!b->n[y]) {
      continue;
    }
    b->n[y] = 0;
    float * a = b->a + (size_t)y * b->stride;
    uint64_t * bit = b->bit + (size_t)y * b->words;
    float acc = 0.0f;
    int from = -1;   /* the first column of the run open, or -1 */
    int cell = -1;   /* the last noted cell inside the window, whose run is not yet written, or -1 */
    uint8_t level = 0;
    for (size_t w = 0; w < b->words; w++) {
      uint64_t v = bit[w];
      if (!v) {
        continue;
      }
      bit[w] = 0;
      do {
        const int x = (int)(w << 6) + __builtin_ctzll(v);
        v &= v - 1u;
        if (cell >= 0) {
          const int end = x < width ? x : width;
          if (level != 0) {
            if (from < 0) {
              from = cell;
            }
            lean_fill(line + (cell - from), level, (size_t)(end - cell));
          } else if (from >= 0) {
            fn(ctx, y0 + y, x0 + from, (size_t)(cell - from), line);
            from = -1;
          }
        }
        acc += a[x];
        a[x] = 0.0f;
        if (x < width) {
          cell = x;
          level = lean_level(acc);
        } else {
          cell = -1;
        }
      } while (v);
    }
    if (cell >= 0) {
      if (level != 0) {
        if (from < 0) {
          from = cell;
        }
        lean_fill(line + (cell - from), level, (size_t)(width - cell));
      } else if (from >= 0) {
        fn(ctx, y0 + y, x0 + from, (size_t)(cell - from), line);
        from = -1;
      }
    }
    if (from >= 0) {
      fn(ctx, y0 + y, x0 + from, (size_t)(width - from), line);
    }
  }
}

/** The path's subpaths, from the path itself or from an array of them. */
static inline GSHGEOM_Flat_Subpath lean_subpath(const GSHGEOM_Flat_Path * path, const GSHGEOM_Flat_Subpath * subs, size_t i, int * ok) {
  GSHGEOM_Flat_Subpath sub = {0, 0, 0};
  if (subs) {
    *ok = 1;
    return subs[i];
  }
  *ok = gshgeom_flat_path_subpath(path, i, &sub) == GSHGEOM_OK;
  return sub;
}

static GSHCPU_Result lean_cover(const GSHGEOM_Flat_Path * path, const GSHGEOM_Device_Point * pt, const GSHGEOM_Flat_Subpath * subs,
    size_t subpaths, int64_t shift_x, int64_t shift_y, GSHCPU_Rect rect, void * scratch, size_t scratch_bytes, uint8_t * line,
    GSHCPU_Span_Fn fn, void * ctx) {
  const int64_t width = rect.x1 - rect.x0;
  const int64_t height = rect.y1 - rect.y0;
  if (width <= 0 || height <= 0 || width > 65000) {
    return GSHCPU_OK;
  }
  if (scratch_bytes < gshcpu_lean_bytes((size_t)width, (size_t)GSHCPU_RENDER_BAND_ROWS)) {
    return GSHCPU_ERR_INTERNAL;
  }
  /* The scratch is sized for the sink's widest window, and a stroke's is narrow: as many rows in a band as it holds, so that
   * the edges are walked once for a band of the stroke and not once for every sixteen rows of it. */
  size_t rows_max = (size_t)height < LEAN_BAND_MAX ? (size_t)height : LEAN_BAND_MAX;
  while (rows_max > (size_t)GSHCPU_RENDER_BAND_ROWS && gshcpu_lean_bytes((size_t)width, rows_max) > scratch_bytes) {
    rows_max /= 2u;
  }
  Lean_Band b;
  lean_band(&b, scratch, (size_t)width, rows_max);
  /* a window and a shift are within the dimension limit, so the difference fits 32 bits: (a conversion from 64 bits is an x87 load on i686) */
  const double ox = (double)(int32_t)(shift_x - rect.x0);
  const double oy = (double)(int32_t)(shift_y - rect.y0);
  for (int64_t top = 0; top < height; top += (int64_t)rows_max) {
    const int rows = (int)(height - top < (int64_t)rows_max ? height - top : (int64_t)rows_max);
    for (size_t i = 0; i < subpaths; i++) {
      int ok;
      GSHGEOM_Flat_Subpath sub = lean_subpath(path, subs, i, &ok);
      if (!ok) {
        return GSHCPU_ERR_INTERNAL;
      }
      for (size_t k = 0; k < sub.count; k++) {
        const GSHGEOM_Device_Point p = pt[sub.first + k];
        const GSHGEOM_Device_Point q = pt[sub.first + (k + 1 == sub.count ? 0 : k + 1)];
        lean_edge(&b, (int)top, rows, (int)width, (double)p.x / 64.0 + ox, (double)p.y / 64.0 + oy, (double)q.x / 64.0 + ox,
            (double)q.y / 64.0 + oy);
      }
    }
    /* the runs are handed over in the paths' frame, as the sparse sweep's are: the receiver adds the shift */
    lean_sweep(&b, rows, (int)width, rect.x0 - shift_x, rect.y0 + top - shift_y, line, fn, ctx);
  }
  return GSHCPU_OK;
}

GSHCPU_Result gshcpu_render_lean_cover(const GSHGEOM_Flat_Path * path, int64_t shift_x, int64_t shift_y, GSHCPU_Rect rect,
    void * scratch, size_t scratch_bytes, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx) {
  if (!path) {
    return GSHCPU_OK;
  }
  return lean_cover(path, gshgeom_flat_path_points(path), NULL, gshgeom_flat_path_subpath_count(path), shift_x, shift_y, rect, scratch,
      scratch_bytes, line, fn, ctx);
}

GSHCPU_Result gshcpu_render_lean_cover_points(const GSHGEOM_Device_Point * points, const GSHGEOM_Flat_Subpath * subpaths,
    size_t subpath_count, int64_t shift_x, int64_t shift_y, GSHCPU_Rect rect, void * scratch, size_t scratch_bytes, uint8_t * line,
    GSHCPU_Span_Fn fn, void * ctx) {
  if (!points || !subpaths) {
    return GSHCPU_OK;
  }
  return lean_cover(NULL, points, subpaths, subpath_count, shift_x, shift_y, rect, scratch, scratch_bytes, line, fn, ctx);
}
