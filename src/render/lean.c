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
 * The lean cover (the fast stroke coverage): the coverage of one stroke part's outline under the nonzero rule, summed in integers
 * as the area and cover of each cell an edge crosses, by rows of sparse cells.  An edge adds to the cells it crosses and to
 * nothing else, the cells a row holds are kept in a list and sorted, and the sweep of a row visits them and the runs between
 * them, so the work follows the outline and never the rectangle it lies in.  Where pieces of the outline overlap the
 * coverages add and the magnitude is held at one, the way the rasterizers this is compared with draw them; the exact union
 * of the default coverage is not made.
 *
 * The scratch is the sink's accumulator, a band of rows at a time, and is left all zero, so the next user finds it as it
 * expects.  The sums are 32-bit integers in 8192ths of a pixel and the edges are walked in 64ths, the unit of the points, with the
 * quotient and remainder of a row's step, so the sums do not depend on how a target rounds a float; only the share of a piece that
 * spans three cells or more is made from a double, whose product and one cast are the same on every target that rounds as IEEE.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** The scratch of a band of @p rows rows of @p width cells, in @p memory: the row flags, the sums and the bits of the cells noted. */
typedef struct Lean_Band {
  uint32_t * n;   /* 1 for a row that has a cell noted */
  int32_t * a;    /* the sums of each row, in 8192ths of a pixel: width + 2 */
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
  b->a = (int32_t *)(void *)p;
  p += (rows * b->stride * sizeof(float) + 7u) & ~(size_t)7u;
  b->bit = (uint64_t *)(void *)p;
}

/** Add @p v to cell @p x of row @p y, noting the cell when it is new. */
static inline void lean_add(Lean_Band * b, int y, int x, int32_t v) {
  b->n[y] = 1;
  b->bit[(size_t)y * b->words + ((size_t)x >> 6)] |= (uint64_t)1 << (x & 63);
  b->a[(size_t)y * b->stride + (size_t)x] += v;
}

/** Add @p v0 to cell @p x of row @p y and @p v1 to the cell after it: the common case of a piece, whose cells share a word of the set but at a word's last bit. */
static inline void lean_add2(Lean_Band * b, int y, int x, int32_t v0, int32_t v1) {
  b->n[y] = 1;
  uint64_t * w = b->bit + (size_t)y * b->words + ((size_t)x >> 6);
  const unsigned sh = (unsigned)x & 63u;
  if (sh != 63u) {
    *w |= (uint64_t)3 << sh;
  } else {
    w[0] |= (uint64_t)1 << 63;
    w[1] |= 1u;
  }
  int32_t * a = b->a + (size_t)y * b->stride + (size_t)x;
  a[0] += v0;
  a[1] += v1;
}

/**
 * The part of one row of an edge, in 64ths of a pixel: from @p xa to @p xb across the row, @p d of its height (a 64th each, signed by its
 * direction).  The sums are in 8192ths of a pixel: a full pixel is 8192, so what a piece adds in all adds up to @p d * 128, and the
 * sum of a row up to a cell is that cell's coverage.  What is left of the window is a cover at its first column; what is right of it is nothing.
 */
static inline __attribute__((always_inline)) void lean_row(Lean_Band * b, int y, int width, int xa, int xb, int d) {
  if (xa > xb) {
    const int t = xa;
    xa = xb;
    xb = t;
  }
  int32_t ds = (int32_t)d * 128;
  const int right = width << 6;
  if (xb <= 0) {
    lean_add(b, y, 0, ds);
    return;
  }
  if (xa >= right) {
    return;
  }
  if (xa < 0 || xb > right) {
    /* The edge leaves the window inside this row: the share of its height outside is a cover at the window's left or nothing. */
    const int64_t span = (int64_t)xb - xa;
    const int lo = xa < 0 ? 0 : xa;
    const int hi = xb > right ? right : xb;
    if (xa < 0) {
      lean_add(b, y, 0, (int32_t)((int64_t)ds * (lo - xa) / span));
    }
    ds = (int32_t)((int64_t)ds * (hi - lo) / span);
    xa = lo;
    xb = hi;
  }
  const int xai = xa >> 6;
  const int xbi = (xb + 63) >> 6;
  if (xbi <= xai + 1) {
    const int32_t v1 = (ds * (xa + xb - (xai << 7))) >> 7;
    lean_add2(b, y, xai, ds - v1, v1);
    return;
  }
  /* The share of d that has crossed by each pixel's end: twice the area of the line right of the pixel's own left edge, over 128 * w.
   * The first sum is the share left of pixel xai's end, and every sum after is the difference of two. */
  const double scale = (double)ds / (128.0 * (double)(xb - xa));
  const double half = ds < 0 ? -0.5 : 0.5; /* the cast truncates toward zero: a half of the sum's sign rounds it to the nearest */
  int32_t prev = 0;
  for (int k = xai; k < xbi; k++) {
    const int u = (k + 1) << 6;
    const int p = xa > (k << 6) ? xa : (k << 6);
    const int q = xb < u ? xb : u;
    /* 2 * (the integral of the clamped distance to u over [xa, xb]): the part left of the pixel counts 64 wide, the part inside is a trapezoid */
    const int64_t left = (int64_t)(p - xa) * 128;
    const int64_t inside = (int64_t)(q - p) * (2 * (int64_t)u - p - q);
    const int32_t cum = (int32_t)((double)(left + inside) * scale + half);
    lean_add(b, y, k, cum - prev);
    prev = cum;
  }
  lean_add(b, y, xbi, ds - prev);
}

/** The floor of a quotient found in a double, without the libm. */
static inline __attribute__((always_inline)) int64_t lean_floor(double v) {
  const int64_t t = (int64_t)v;
  return t - ((double)t > v);
}

/**
 * One edge into the band whose first row is @p top (rows @p rows), in the window's frame and in 64ths of a pixel.  The x at a row's top and
 * bottom is `x0 + (y - y0) * dx / dy`, rounded to a 64th, at that row boundary and nothing else, found by stepping the quotient and remainder of
 * a row's share, so the sums an edge leaves in a row are the same whatever band it is cut into: a stripe of the window is the picture's
 * rows and nothing else.
 */
static void lean_edge(Lean_Band * b, int top, int rows, int width, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  if (y0 == y1) {
    return;
  }
  int dir = 1;
  if (y0 > y1) {
    int64_t t = x0;
    x0 = x1;
    x1 = t;
    t = y0;
    y0 = y1;
    y1 = t;
    dir = -1;
  }
  const int64_t lo = (int64_t)top << 6;
  const int64_t hi = (int64_t)(top + rows) << 6;
  if (y1 <= lo || y0 >= hi) {
    return;
  }
  const int64_t dx = x1 - x0;
  const int64_t dy = y1 - y0;
  const int64_t ys = y0 < lo ? lo : y0;
  const int64_t ye = y1 > hi ? hi : y1;
  /* x at a boundary y is x0 + the nearest integer to (y - y0) * dx / dy, halves up: the floor of (2 (y - y0) dx + dy) / 2 dy.  A quotient is
   * under 2^22 and its fraction needs 23 bits more, so a double holds the floor of it exactly. */
  const int64_t dd = 2 * dy;
  const double ddf = (double)dd;
  int64_t xa = ys == y0 ? x0 : x0 + lean_floor((double)(2 * (ys - y0) * dx + dy) / ddf);
  int64_t yb = ((ys >> 6) + 1) << 6;
  if (yb > ye) {
    /* one piece, ending where the edge does */
    lean_row(b, (int)(ys >> 6) - top, width, (int)xa, (int)x1, (int)(ye - ys) * dir);
    return;
  }
  const int64_t num = 2 * (yb - y0) * dx + dy;
  int64_t q0 = lean_floor((double)num / ddf);
  int64_t r0 = num - q0 * dd;
  int64_t qs = 0;
  int64_t rs = 0;
  if (yb + 64 <= ye) { /* a second boundary: the step to it */
    const int64_t step = 128 * dx;
    qs = lean_floor((double)step / ddf);
    rs = step - qs * dd;
  }
  int64_t ya = ys;
  int y = (int)(ys >> 6);
  for (;;) {
    if (yb >= ye) {
      const int64_t xb = ye == yb ? x0 + q0 : x1;
      lean_row(b, y - top, width, (int)xa, (int)xb, (int)(ye - ya) * dir);
      return;
    }
    const int64_t xb = x0 + q0;
    lean_row(b, y - top, width, (int)xa, (int)xb, (int)(yb - ya) * dir);
    xa = xb;
    ya = yb;
    yb += 64;
    y++;
    q0 += qs;
    r0 += rs;
    if (r0 >= dd) {
      r0 -= dd;
      q0++;
    }
  }
}

/** The coverage of an accumulated sum, 0 to 255. */
static inline __attribute__((always_inline)) uint8_t lean_level(int32_t acc) {
  int32_t c = acc < 0 ? -acc : acc;
  if (c > 8192) {
    c = 8192;
  }
  return (uint8_t)((c * 255 + 4096) >> 13);
}

/** @p n bytes of @p m at @p p: the runs are short, and a call to memset for three bytes costs more than the three. */
static inline __attribute__((always_inline)) void lean_fill(uint8_t * p, uint8_t m, size_t n) {
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
    int32_t * a = b->a + (size_t)y * b->stride;
    uint64_t * bit = b->bit + (size_t)y * b->words;
    int32_t acc = 0;
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
        a[x] = 0;
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
  const int64_t ox = (int64_t)(int32_t)(shift_x - rect.x0) * 64;
  const int64_t oy = (int64_t)(int32_t)(shift_y - rect.y0) * 64;
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
        lean_edge(&b, (int)top, rows, (int)width, (int64_t)p.x + ox, (int64_t)p.y + oy, (int64_t)q.x + ox, (int64_t)q.y + oy);
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
