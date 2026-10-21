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
 * Story 9's scan converter (coverage.c), restricted to a rectangle and run a
 * band of rows at a time into a dense accumulator, for the sink.
 *
 * The arithmetic is coverage.c's: an edge is cut at every row boundary and
 * every piece within a row at every column boundary; each crossing deposits
 * `cover` (its signed height, 26.6) and `area` (that height times the sum of its
 * two x positions within the cell) into its cell; a sweep accumulates `cover`
 * along the row and folds `cover * 128 - area` into 0..255 by the fill rule.  The
 * point where an edge crosses a boundary is computed from the edge's own start (a
 * row boundary) or the row piece's own start (a column boundary), with the same
 * rounding, so it is the same point whichever boundaries are visited.  That is
 * what makes the restriction exact:
 *
 * - rows outside the rectangle are not visited (a row's coverage depends only on
 *   the crossings in it);
 * - the pieces of a row left of the rectangle add only their cover, and they
 *   telescope, so they are one deposit into a column to the left of the first,
 *   from the piece's start to the rectangle's left boundary;
 * - the pieces right of it are dropped (nothing to the right of a cell changes
 *   the pixels to its left).
 *
 * The cells are not sorted: each lands in its place in the accumulator, and the
 * sums are the same whatever the order.
 *
 * The cell cap is met over a rectangle that may be larger than the one drawn
 * (gshcpu_render_cover_counted): the rectangle the whole render would draw of
 * the item, so that whether the item is refused does not depend on the damage
 * (S4).  The pieces are then walked over the larger rectangle, every crossing in
 * it is counted, and a crossing outside the drawn one is folded (left of it: its
 * cover telescopes into the fold column as the one deposit would) or dropped
 * (right of it), the rows outside it counted only.  The coverage drawn is what
 * the drawn rectangle alone gives, crossing for crossing.  Coordinates are absolute 26.6, held in
 * `int64`; the caller keeps every point within ::GSHCPU_RENDER_COORD_LIMIT of the
 * origin, so a difference is under 2^31 and a product of two under 2^62.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <string.h>

/** Sub-pixel bits, the pixel and the shift into 0..256: coverage.c's. */
#define PIXEL_BITS 6
#define ONE_PIXEL 64
#define AREA_SHIFT (2 * PIXEL_BITS + 1 - 8)

/* --- rectangles ---------------------------------------------------------------------------- */

int gshcpu_render_rect_empty(GSHCPU_Rect r) {
  return r.x0 >= r.x1 || r.y0 >= r.y1;
}

GSHCPU_Rect gshcpu_render_rect_meet(GSHCPU_Rect a, GSHCPU_Rect b) {
  GSHCPU_Rect r;
  r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
  r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
  r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
  return r;
}

GSHCPU_Rect gshcpu_render_rect_join(GSHCPU_Rect a, GSHCPU_Rect b) {
  if (gshcpu_render_rect_empty(a)) {
    return b;
  }
  if (gshcpu_render_rect_empty(b)) {
    return a;
  }
  GSHCPU_Rect r;
  r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
  r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
  r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
  return r;
}

/* --- integer helpers (coverage.c's) ---------------------------------------------------------- */

/** Divide, rounding half away from zero. */
static int64_t round_div(int64_t numerator, int64_t denominator) {
  uint64_t n = (uint64_t)(numerator < 0 ? -numerator : numerator);
  uint64_t d = (uint64_t)(denominator < 0 ? -denominator : denominator);
  uint64_t quotient = (n + d / 2) / d;
  return (numerator < 0) != (denominator < 0) ? -(int64_t)quotient : (int64_t)quotient;
}

/** Divide, rounding toward negative infinity. */
static int64_t floor_div(int64_t numerator, int64_t denominator) {
  int64_t q = numerator / denominator;
  if ((numerator % denominator != 0) && ((numerator < 0) != (denominator < 0))) {
    q -= 1;
  }
  return q;
}

/* --- the accumulator ---------------------------------------------------------------------------- */

typedef struct Point {
  int64_t x;
  int64_t y;
} Point;

typedef struct Cover {
  GSHCPU_Rect rect;     /* the rectangle drawn */
  GSHCPU_Rect walk;     /* the rectangle counted: rect, or one that holds it */
  int64_t band;         /* the band's first row */
  int64_t rows;         /* its rows */
  int64_t stride;       /* columns of the accumulator: the rectangle's width plus the fold */
  int64_t * cover;      /* rows * stride; NULL for rows counted only */
  int64_t * area;       /* rows * stride */
  uint64_t cells;
  uint64_t max_cells;
  int over;             /* a deposit would have passed max_cells */
} Cover;

/** One crossing of cell (@p ex, @p ey): column @p ex left of the rectangle is the fold column. */
static void deposit(Cover * c, int64_t ex, int64_t ey, int64_t dy, int64_t fx1, int64_t fx2) {
  if (dy == 0 || c->over) {
    return;
  }
  if (c->cells >= c->max_cells) {
    c->over = 1;
    return;
  }
  c->cells++;
  if (!c->cover || ex >= c->rect.x1) {
    return; /* counted, not drawn: a row outside the rectangle drawn, or a crossing right of it */
  }
  int64_t column = ex < c->rect.x0 ? 0 : ex - c->rect.x0 + 1;
  size_t at = (size_t)((ey - c->band) * c->stride + column);
  c->cover[at] += dy;
  c->area[at] += dy * (fx1 + fx2);
}

/** The point where the row piece from @p from to @p to crosses the column boundary at @p b. */
static Point column_cut(Point from, Point to, int64_t b) {
  Point p;
  p.x = b;
  p.y = from.y + round_div((b - from.x) * (to.y - from.y), to.x - from.x);
  return p;
}

/** One piece of an edge within row @p ey (both ends in it, 26.6, absolute), over the columns counted. */
static void scanline(Cover * c, int64_t ey, Point from, Point to) {
  if (from.y == to.y) {
    return; /* every piece has height 0 */
  }
  int64_t ex1 = floor_div(from.x, ONE_PIXEL);
  int64_t ex2 = floor_div(to.x, ONE_PIXEL);
  if (ex1 == ex2) {
    if (ex1 < c->walk.x1) {
      deposit(c, ex1, ey, to.y - from.y, from.x - ex1 * ONE_PIXEL, to.x - ex1 * ONE_PIXEL);
    }
    return;
  }
  int64_t lo = ex1 < ex2 ? ex1 : ex2;
  int64_t hi = ex1 < ex2 ? ex2 : ex1;
  int right = ex2 > ex1;
  /* Left of the rectangle: one deposit of the cover from the piece's start to the boundary x0 (or its end). */
  if (lo < c->walk.x0) {
    Point a = from;
    Point b = to;
    if (hi >= c->walk.x0) {
      Point cut = column_cut(from, to, c->walk.x0 * ONE_PIXEL);
      if (right) {
        b = cut;
      } else {
        a = cut;
      }
    }
    deposit(c, c->walk.x0 - 1, ey, b.y - a.y, 0, 0);
  }
  int64_t first = lo > c->walk.x0 ? lo : c->walk.x0;
  int64_t last = hi < c->walk.x1 - 1 ? hi : c->walk.x1 - 1;
  for (int64_t ex = first; ex <= last; ex++) {
    /* The piece in column ex: from where it enters to where it leaves, each cut from the row piece's start. */
    Point a = ex == ex1 ? from : column_cut(from, to, (right ? ex : ex + 1) * ONE_PIXEL);
    Point b = ex == ex2 ? to : column_cut(from, to, (right ? ex + 1 : ex) * ONE_PIXEL);
    deposit(c, ex, ey, b.y - a.y, a.x - ex * ONE_PIXEL, b.x - ex * ONE_PIXEL);
  }
}

/** The point where the edge from @p from to @p to crosses the row boundary at @p b. */
static Point row_cut(Point from, Point to, int64_t b) {
  Point p;
  p.x = from.x + round_div((b - from.y) * (to.x - from.x), to.y - from.y);
  p.y = b;
  return p;
}

/** One edge: its pieces in the rows of the band. */
static void edge(Cover * c, Point from, Point to) {
  if (from.y == to.y) {
    return; /* crosses no row boundary and has no height */
  }
  int64_t top = c->band * ONE_PIXEL;
  int64_t bottom = (c->band + c->rows) * ONE_PIXEL;
  int64_t ymin = from.y < to.y ? from.y : to.y;
  int64_t ymax = from.y < to.y ? to.y : from.y;
  if (ymax <= top || ymin >= bottom) {
    return;
  }
  int64_t ey1 = floor_div(from.y, ONE_PIXEL);
  int64_t ey2 = floor_div(to.y, ONE_PIXEL);
  int down = ey2 > ey1;
  int64_t lo = ey1 < ey2 ? ey1 : ey2;
  int64_t hi = ey1 < ey2 ? ey2 : ey1;
  if (lo < c->band) {
    lo = c->band;
  }
  if (hi > c->band + c->rows - 1) {
    hi = c->band + c->rows - 1;
  }
  for (int64_t ey = lo; ey <= hi; ey++) {
    Point a = ey == ey1 ? from : row_cut(from, to, (down ? ey : ey + 1) * ONE_PIXEL);
    Point b = ey == ey2 ? to : row_cut(from, to, (down ? ey + 1 : ey) * ONE_PIXEL);
    scanline(c, ey, a, b);
  }
}

/** coverage.c's fold of an accumulated area into 0..255 under @p rule. */
static uint8_t fold(int64_t area, GSHGEOM_Fill_Rule rule) {
  int64_t value = area >> AREA_SHIFT;
  if (value < 0) {
    value = -value;
  }
  if (rule == GSHGEOM_FILL_EVEN_ODD) {
    value &= 511;
    if (value > 256) {
      value = 512 - value;
    } else if (value == 256) {
      value = 255;
    }
  } else if (value >= 256) {
    value = 255;
  }
  return (uint8_t)value;
}

static Point point_of(GSHGEOM_Device_Point p) {
  Point q;
  q.x = p.x;
  q.y = p.y;
  return q;
}

/** Every edge of @p path, the closing line of each subpath included. */
static void path_edges(Cover * c, const GSHGEOM_Flat_Path * path) {
  const GSHGEOM_Device_Point * points = gshgeom_flat_path_points(path);
  size_t subpaths = gshgeom_flat_path_subpath_count(path);
  for (size_t s = 0; s < subpaths && !c->over; s++) {
    GSHGEOM_Flat_Subpath sub;
    if (gshgeom_flat_path_subpath(path, s, &sub) != GSHGEOM_OK || sub.count == 0) {
      continue;
    }
    for (size_t k = 1; k < sub.count; k++) {
      edge(c, point_of(points[sub.first + k - 1]), point_of(points[sub.first + k]));
    }
    /* The closing line: explicit for an open subpath, implicit for a closed one (coverage.c). */
    edge(c, point_of(points[sub.first + sub.count - 1]), point_of(points[sub.first]));
  }
}

GSHCPU_Result gshcpu_render_cover(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells) {
  return gshcpu_render_cover_counted(paths, count, rule, rect, rect, out, acc, max_cells, cells);
}

/** Count the crossings of rows @p y0 to @p y1 (half open) over the columns of @p c's walk, drawing nothing. */
static void count_rows(Cover * c, const GSHGEOM_Flat_Path * const * paths, size_t count, int64_t y0, int64_t y1) {
  if (y0 >= y1) {
    return;
  }
  c->band = y0;
  c->rows = y1 - y0;
  c->cover = NULL;
  c->area = NULL;
  for (size_t i = 0; i < count && !c->over; i++) {
    if (paths[i]) {
      path_edges(c, paths[i]);
    }
  }
}

GSHCPU_Result gshcpu_render_cover_counted(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells) {
  Cover c;
  memset(&c, 0, sizeof c);
  c.rect = rect;
  c.walk = walk;
  c.stride = rect.x1 - rect.x0 + 1;
  c.max_cells = max_cells;
  int64_t width = rect.x1 - rect.x0;
  /* The rows counted and not drawn first: above the rectangle drawn and below it (all of the walk's when it is empty). */
  if (gshcpu_render_rect_empty(rect)) {
    count_rows(&c, paths, count, walk.y0, walk.y1);
  } else {
    count_rows(&c, paths, count, walk.y0, rect.y0);
    count_rows(&c, paths, count, rect.y1, walk.y1);
  }
  if (c.over) {
    *cells = c.cells;
    return GSHCPU_ERR_LIMIT;
  }
  for (int64_t band = rect.y0; band < rect.y1; band += GSHCPU_RENDER_BAND_ROWS) {
    c.band = band;
    c.rows = rect.y1 - band < (int64_t)GSHCPU_RENDER_BAND_ROWS ? rect.y1 - band : (int64_t)GSHCPU_RENDER_BAND_ROWS;
    size_t n = (size_t)(c.rows * c.stride);
    c.cover = acc;
    c.area = acc + n;
    memset(acc, 0, 2 * n * sizeof *acc);
    for (size_t i = 0; i < count && !c.over; i++) {
      if (paths[i]) {
        path_edges(&c, paths[i]);
      }
    }
    if (c.over) {
      *cells = c.cells;
      return GSHCPU_ERR_LIMIT;
    }
    for (int64_t r = 0; r < c.rows; r++) {
      const int64_t * cv = c.cover + r * c.stride;
      const int64_t * ar = c.area + r * c.stride;
      uint8_t * line = out + (size_t)((band - rect.y0 + r) * width);
      int64_t accumulated = cv[0] * (ONE_PIXEL * 2);
      for (int64_t j = 1; j <= width; j++) {
        accumulated += cv[j] * (ONE_PIXEL * 2);
        line[j - 1] = fold(accumulated - ar[j], rule);
      }
    }
  }
  *cells = c.cells;
  return GSHCPU_OK;
}

/* --- a bound of the cells, from the edges alone ------------------------------------------------------- */

/** @p a plus @p b, held at UINT64_MAX. */
static uint64_t add_held(uint64_t a, uint64_t b) {
  return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

/**
 * A bound of the deposits of the edge from @p from to @p to over @p walk: each of its R rows in the walk is one
 * piece, which makes at most one deposit left of the walk and one in each column it spans inside it (at most the
 * walk's width W), and the columns of the pieces add up to the edge's own span (the cuts move one way along it), so
 * R + min(span + R, R * W).
 */
static uint64_t edge_bound(Point from, Point to, GSHCPU_Rect walk) {
  if (from.y == to.y) {
    return 0;
  }
  int64_t ey1 = floor_div(from.y, ONE_PIXEL);
  int64_t ey2 = floor_div(to.y, ONE_PIXEL);
  int64_t lo = ey1 < ey2 ? ey1 : ey2;
  int64_t hi = ey1 < ey2 ? ey2 : ey1;
  lo = lo > walk.y0 ? lo : walk.y0;
  hi = hi < walk.y1 - 1 ? hi : walk.y1 - 1;
  if (lo > hi) {
    return 0;
  }
  uint64_t rows = (uint64_t)(hi - lo + 1);
  int64_t ex1 = floor_div(from.x, ONE_PIXEL);
  int64_t ex2 = floor_div(to.x, ONE_PIXEL);
  uint64_t span = (uint64_t)(ex1 < ex2 ? ex2 - ex1 : ex1 - ex2);
  uint64_t width = (uint64_t)(walk.x1 - walk.x0);
  uint64_t columns = span + rows;
  if (rows * width < columns) {
    columns = rows * width; /* rows and width are each under 2^26: no overflow */
  }
  return rows + columns;
}

uint64_t gshcpu_render_cells_bound(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHCPU_Rect walk,
    uint64_t limit) {
  uint64_t bound = 0;
  if (gshcpu_render_rect_empty(walk)) {
    return 0;
  }
  for (size_t i = 0; i < count && bound <= limit; i++) {
    if (!paths[i]) {
      continue;
    }
    const GSHGEOM_Device_Point * points = gshgeom_flat_path_points(paths[i]);
    size_t subpaths = gshgeom_flat_path_subpath_count(paths[i]);
    for (size_t s = 0; s < subpaths && bound <= limit; s++) {
      GSHGEOM_Flat_Subpath sub;
      if (gshgeom_flat_path_subpath(paths[i], s, &sub) != GSHGEOM_OK || sub.count == 0) {
        continue;
      }
      for (size_t k = 1; k < sub.count; k++) {
        bound = add_held(bound, edge_bound(point_of(points[sub.first + k - 1]), point_of(points[sub.first + k]), walk));
      }
      bound = add_held(bound, edge_bound(point_of(points[sub.first + sub.count - 1]), point_of(points[sub.first]), walk));
    }
  }
  return bound;
}
