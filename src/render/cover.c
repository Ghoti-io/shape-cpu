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
  int track;            /* the sparse sweep: note the first and last column each row of the band deposited in */
  int64_t lo[GSHCPU_RENDER_BAND_ROWS]; /* per row of the band; > hi when the row has none */
  int64_t hi[GSHCPU_RENDER_BAND_ROWS];
  GSHCPU_Exact * ex;    /* the exact step (UC1 to UC7), or NULL: then nothing below is touched */
  int rev;              /* the edge being cut goes up and is cut from its lower end's partner: its pieces are handed on reversed */
  int64_t cstride;      /* the columns of ex's count and slot arrays: the walk's width */
  int store;            /* the second pass over the band: keep the pieces of the cells in the window, change nothing */
  int64_t win0, win1;   /* the window: cells of the drawn rectangle, row by row from the band's first, [win0, win1) */
} Cover;

/** The index of cell (@p ex, @p ey) in the exact step's count and slot arrays (the walk's columns). */
static size_t exact_cell_index(const Cover * c, int64_t ex, int64_t ey) {
  return (size_t)((ey - c->band) * c->cstride + (ex - c->walk.x0));
}

/**
 * What a piece of an edge in cell (@p ex, @p ey) is to the exact step (UC2): 1 for a fragment (nonzero height), 2 for a
 * flat piece of nonzero length that touches the cell's left side (it steps the winding there), 0 for nothing the step
 * needs (a flat piece elsewhere in the cell, or a point).
 */
static int piece_kind(int64_t ex, Point a, Point b) {
  if (a.y != b.y) {
    return 1;
  }
  if (a.x == b.x) {
    return 0;
  }
  int64_t left = ex * ONE_PIXEL;
  return a.x == left || b.x == left ? 2 : 0;
}

/** The piece as the step keeps it: both ends in the cell's frame. */
static GSHCPU_Piece local_piece(int64_t ex, int64_t ey, Point a, Point b) {
  GSHCPU_Piece p;
  p.x0 = (uint8_t)(a.x - ex * ONE_PIXEL);
  p.y0 = (uint8_t)(a.y - ey * ONE_PIXEL);
  p.x1 = (uint8_t)(b.x - ex * ONE_PIXEL);
  p.y1 = (uint8_t)(b.y - ey * ONE_PIXEL);
  return p;
}

/** The exact step's first pass: one more piece in the cell; a cell past the cap is the limit. */
static void count_piece(Cover * c, int64_t ex, int64_t ey) {
  if (ex < c->walk.x0 || ex >= c->walk.x1) {
    return;
  }
  uint32_t * n = &c->ex->count[exact_cell_index(c, ex, ey)];
  if ((uint64_t)*n >= c->ex->max_pieces) {
    c->over = 1;
    return;
  }
  (*n)++;
  if (*n == 2 && c->track && c->cover && ex >= c->rect.x0 && ex < c->rect.x1) {
    /* A cell the exact step will change: the sparse sweep must visit it, wherever the deposits are. */
    int64_t column = ex - c->rect.x0 + 1;
    size_t row = (size_t)(ey - c->band);
    if (column < c->lo[row]) {
      c->lo[row] = column;
    }
    if (column > c->hi[row]) {
      c->hi[row] = column;
    }
  }
}

/** The exact step's second pass: keep the piece if its cell is one the step makes exact in this window. */
static void store_piece(Cover * c, int64_t ex, int64_t ey, Point a, Point b) {
  if (ex < c->rect.x0 || ex >= c->rect.x1 || piece_kind(ex, a, b) == 0) {
    return;
  }
  int64_t at = (ey - c->band) * (c->rect.x1 - c->rect.x0) + (ex - c->rect.x0);
  if (at < c->win0 || at >= c->win1) {
    return;
  }
  size_t k = exact_cell_index(c, ex, ey);
  if (c->ex->count[k] < 2) {
    return;
  }
  c->ex->pool[c->ex->slot[k]++] = local_piece(ex, ey, a, b);
  c->ex->work++;
}

/** The deposit of the part of a piece left of the walk: its cover only, into the fold column. */
static void fold_deposit(Cover * c, int64_t ey, int64_t dy) {
  if (dy == 0 || c->over || c->store) {
    return;
  }
  if (c->cells >= c->max_cells) {
    c->over = 1;
    return;
  }
  c->cells++;
  if (!c->cover) {
    return; /* counted, not drawn */
  }
  size_t at = (size_t)((ey - c->band) * c->stride);
  c->cover[at] += dy;
  if (c->track) {
    size_t row = (size_t)(ey - c->band);
    if (0 < c->lo[row]) {
      c->lo[row] = 0;
    }
    if (0 > c->hi[row]) {
      c->hi[row] = 0;
    }
  }
}

/** One piece of an edge in cell (@p ex, @p ey): column @p ex left of the rectangle is the fold column. */
static void piece(Cover * c, int64_t ex, int64_t ey, Point a, Point b) {
  if (c->over) {
    return;
  }
  if (c->store) {
    store_piece(c, ex, ey, a, b);
    return;
  }
  int64_t dy = b.y - a.y;
  if (dy == 0) {
    if (c->ex && piece_kind(ex, a, b) == 2) {
      count_piece(c, ex, ey);
    }
    return;
  }
  if (c->cells >= c->max_cells) {
    c->over = 1;
    return;
  }
  c->cells++;
  if (c->ex) {
    count_piece(c, ex, ey);
    if (c->over) {
      return;
    }
  }
  if (!c->cover || ex >= c->rect.x1) {
    return; /* counted, not drawn: a row outside the rectangle drawn, or a crossing right of it */
  }
  int64_t fx1 = a.x - ex * ONE_PIXEL;
  int64_t fx2 = b.x - ex * ONE_PIXEL;
  int64_t column = ex < c->rect.x0 ? 0 : ex - c->rect.x0 + 1;
  size_t at = (size_t)((ey - c->band) * c->stride + column);
  c->cover[at] += dy;
  c->area[at] += dy * (fx1 + fx2);
  if (c->track) {
    size_t row = (size_t)(ey - c->band);
    if (column < c->lo[row]) {
      c->lo[row] = column;
    }
    if (column > c->hi[row]) {
      c->hi[row] = column;
    }
  }
}

/** The point where the row piece from @p from to @p to crosses the column boundary at @p b. */
static Point column_cut(Point from, Point to, int64_t b) {
  Point p;
  p.x = b;
  p.y = from.y + round_div((b - from.x) * (to.y - from.y), to.x - from.x);
  return p;
}

/** A piece cut by scanline: handed to the accumulator in the edge's own direction (reversed when it was cut from its upper end). */
static void emit(Cover * c, int64_t ex, int64_t ey, Point a, Point b) {
  if (c->rev) {
    piece(c, ex, ey, b, a);
  } else {
    piece(c, ex, ey, a, b);
  }
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
      emit(c, ex1, ey, from, to);
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
    fold_deposit(c, ey, c->rev ? a.y - b.y : b.y - a.y);
  }
  int64_t first = lo > c->walk.x0 ? lo : c->walk.x0;
  int64_t last = hi < c->walk.x1 - 1 ? hi : c->walk.x1 - 1;
  for (int64_t ex = first; ex <= last; ex++) {
    /* The piece in column ex: from where it enters to where it leaves, each cut from the row piece's start. */
    Point a = ex == ex1 ? from : column_cut(from, to, (right ? ex : ex + 1) * ONE_PIXEL);
    Point b = ex == ex2 ? to : column_cut(from, to, (right ? ex + 1 : ex) * ONE_PIXEL);
    emit(c, ex, ey, a, b);
  }
}

/** The point where the edge from @p from to @p to crosses the row boundary at @p b. */
static Point row_cut(Point from, Point to, int64_t b) {
  Point p;
  p.x = from.x + round_div((b - from.y) * (to.x - from.x), to.y - from.y);
  p.y = b;
  return p;
}

/**
 * One horizontal edge (the exact step only, UC2): it deposits nothing, but in every cell it crosses it is a flat piece
 * that steps the winding on the cell's left side wherever it starts or ends there.
 */
static void flat_edge(Cover * c, Point from, Point to) {
  int64_t ey = floor_div(from.y, ONE_PIXEL);
  if (ey < c->band || ey >= c->band + c->rows || from.x == to.x) {
    return;
  }
  int right = to.x > from.x;
  int64_t xa = right ? from.x : to.x;
  int64_t xb = right ? to.x : from.x;
  int64_t first = floor_div(xa, ONE_PIXEL);
  int64_t last = floor_div(xb - 1, ONE_PIXEL);
  first = first > c->walk.x0 ? first : c->walk.x0;
  last = last < c->walk.x1 - 1 ? last : c->walk.x1 - 1;
  for (int64_t k = first; k <= last && !c->over; k++) {
    int64_t lo = xa > k * ONE_PIXEL ? xa : k * ONE_PIXEL;
    int64_t hi = xb < (k + 1) * ONE_PIXEL ? xb : (k + 1) * ONE_PIXEL;
    Point a = {right ? lo : hi, from.y};
    Point b = {right ? hi : lo, from.y};
    if (!c->store) {
      c->ex->work++;
    }
    piece(c, k, ey, a, b);
  }
}

/**
 * One edge: its pieces in the rows of the band.  With the exact step an edge that goes up is cut from its upper end, so
 * that the two copies of one segment that two pieces of an outline share, one in each direction, are cut at the same
 * points (every cut point is the rounding of a cut from its edge's own start: from the other end it may be a unit
 * away, and the sliver between the copies would be a hole in the union).  What is deposited is the same edge, in its
 * own direction.
 */
static void edge(Cover * c, Point from, Point to) {
  if (from.y == to.y) {
    if (c->ex) {
      flat_edge(c, from, to);
    }
    return; /* crosses no row boundary and has no height */
  }
  int64_t top = c->band * ONE_PIXEL;
  int64_t bottom = (c->band + c->rows) * ONE_PIXEL;
  int64_t ymin = from.y < to.y ? from.y : to.y;
  int64_t ymax = from.y < to.y ? to.y : from.y;
  if (ymax <= top || ymin >= bottom) {
    return;
  }
  if (c->ex && from.y > to.y) {
    Point t = from;
    from = to;
    to = t;
    c->rev = 1;
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
  c->rev = 0;
}

/**
 * coverage.c's fold of an accumulated area into 0..255 under @p rule.  Its shift rounds toward negative infinity before
 * the sign is dropped, so a region wound the other way (a stroke's pieces are, UC1) comes out one level up wherever the
 * area is not a whole level: @p magnitude folds the area's magnitude instead, the floor of a level either way round (the
 * fold of UC7).
 */
static uint8_t fold(int64_t area, GSHGEOM_Fill_Rule rule, int magnitude) {
  if (magnitude && area < 0) {
    area = -area;
  }
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

/** Clear the exact step's count array for the band's rows. */
static void exact_clear(Cover * c) {
  memset(c->ex->count, 0, (size_t)c->rows * (size_t)c->cstride * sizeof *c->ex->count);
}

/**
 * Count the crossings of rows @p y0 to @p y1 (half open) over the columns of @p c's walk, drawing nothing.  With the
 * exact step the rows go a band at a time, so that the pieces of every cell are counted against the cap.
 */
static void count_rows(Cover * c, const GSHGEOM_Flat_Path * const * paths, size_t count, int64_t y0, int64_t y1) {
  if (y0 >= y1) {
    return;
  }
  c->cover = NULL;
  c->area = NULL;
  int64_t step = c->ex ? (int64_t)GSHCPU_RENDER_BAND_ROWS : y1 - y0;
  for (int64_t b = y0; b < y1 && !c->over; b += step) {
    c->band = b;
    c->rows = y1 - b < step ? y1 - b : step;
    if (c->ex) {
      exact_clear(c);
    }
    for (size_t i = 0; i < count && !c->over; i++) {
      if (paths[i]) {
        path_edges(c, paths[i]);
      }
    }
  }
}

/**
 * The exact step over the band just deposited (UC3 to UC7): every cell of the drawn rectangle that holds two or more
 * pieces has its area made the union's, by changing its entry in the area array so that the sweep, whichever it is, folds
 * the exact value.  The pieces of those cells are kept in windows that fit the pool (a second pass over the edges for each).
 *
 * @return 0, or 1 when the step refuses (a winding past the bound, a cell whose pieces do not reproduce the sums).
 */
static int exact_band(Cover * c, const GSHGEOM_Flat_Path * const * paths, size_t count) {
  GSHCPU_Exact * ex = c->ex;
  int64_t width = c->rect.x1 - c->rect.x0;
  int64_t total = c->rows * width;
  GSHCPU_Exact_Params params;
  memset(&params, 0, sizeof params);
  params.parts = 1;
  params.mode = GSHCPU_EXACT_OVERLAP;
  params.eps[0] = ex->eps;
  int64_t pos = 0;
  while (pos < total) {
    /* The window: as many cells as the pool holds the pieces of. */
    int64_t idx = pos;
    uint64_t kept = 0;
    while (idx < total) {
      int64_t ey = c->band + idx / width;
      int64_t exx = c->rect.x0 + idx % width;
      size_t k = exact_cell_index(c, exx, ey);
      uint32_t n = ex->count[k];
      if (n >= 2) {
        if (kept + n > ex->pool_cap) {
          break;
        }
        ex->slot[k] = (uint32_t)kept;
        kept += n;
      }
      idx++;
    }
    if (kept == 0) {
      pos = idx;
      continue;
    }
    c->win0 = pos;
    c->win1 = idx;
    c->store = 1;
    for (size_t i = 0; i < count; i++) {
      if (paths[i]) {
        path_edges(c, paths[i]);
      }
    }
    c->store = 0;
    /* The cells of the window: the cover to the left of each from the row's running sum. */
    int64_t row0 = pos / width;
    int64_t row1 = (idx - 1) / width;
    for (int64_t r = row0; r <= row1; r++) {
      const int64_t * cv = c->cover + r * c->stride;
      int64_t * ar = c->area + r * c->stride;
      int64_t sum = cv[0];
      for (int64_t j = 1; j <= width; j++) {
        int64_t left = sum;
        sum += cv[j];
        int64_t at = r * width + (j - 1);
        if (at < pos || at >= idx) {
          continue;
        }
        size_t k = exact_cell_index(c, c->rect.x0 + (j - 1), c->band + r);
        uint32_t n = ex->count[k];
        if (n < 2) {
          continue;
        }
        GSHCPU_Exact_Cell cell;
        memset(&cell, 0, sizeof cell);
        cell.pieces[0] = ex->pool + (ex->slot[k] - n);
        cell.count[0] = n;
        cell.a_left[0] = left;
        int64_t accumulated = sum * (ONE_PIXEL * 2);
        int64_t dense = ex->eps * (accumulated - ar[j]);
        cell.dense[0] = dense;
        int64_t shared = 0;
        int status = gshcpu_exact_cell(&params, &ex->scratch, &cell, &shared, &ex->work);
        if (status != GSHCPU_EXACT_OK) {
          if (status == GSHCPU_EXACT_IDENTITY) {
            ex->failures++;
          }
          return 1;
        }
        int64_t value = dense - shared;
        if (value < 0) {
          value = 0;
        }
        ar[j] = accumulated - ex->eps * value;
        ex->cells++;
      }
    }
    pos = idx;
  }
  return 0;
}

GSHCPU_Result gshcpu_render_cover_counted(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells) {
  return gshcpu_render_cover_exact(paths, count, rule, rect, walk, out, acc, max_cells, cells, NULL);
}

GSHCPU_Result gshcpu_render_cover_exact(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells,
    GSHCPU_Exact * ex) {
  Cover c;
  memset(&c, 0, sizeof c);
  c.rect = rect;
  c.walk = walk;
  c.ex = ex;
  c.cstride = walk.x1 - walk.x0;
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
    if (ex) {
      exact_clear(&c);
    }
    for (size_t i = 0; i < count && !c.over; i++) {
      if (paths[i]) {
        path_edges(&c, paths[i]);
      }
    }
    if (c.over || (ex && exact_band(&c, paths, count))) {
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
        line[j - 1] = fold(accumulated - ar[j], rule, ex != NULL);
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

/* --- the sparse sweep (FT-2, strokes story S4) ---------------------------------------------------------------- */

/**
 * Sweep the band's rows that deposited something and hand each one's coverages to @p fn, then clear what was
 * written.  A column before a row's first deposit has an accumulated cover of zero and no area, so its coverage is
 * zero; the columns after its last have the accumulated cover and no area of their own, so their coverage is one
 * constant, which is the dense sweep's value there too (the fold column is a deposit at column 0).
 */
static void sweep_band(Cover * c, GSHGEOM_Fill_Rule rule, int64_t width, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx) {
  for (int64_t r = 0; r < c->rows; r++) {
    if (c->hi[r] < c->lo[r]) {
      continue; /* no deposit: the row is zero */
    }
    int64_t * cv = c->cover + r * c->stride;
    int64_t * ar = c->area + r * c->stride;
    int64_t lo = c->lo[r];
    int64_t hi = c->hi[r];
    int64_t j0 = lo > 1 ? lo : 1;
    int64_t accumulated = cv[0] * (ONE_PIXEL * 2);
    int64_t j = j0;
    for (; j <= hi && j <= width; j++) {
      accumulated += cv[j] * (ONE_PIXEL * 2);
      line[j - j0] = fold(accumulated - ar[j], rule, c->ex != NULL);
    }
    int64_t last = j - 1; /* the last column given a value; j0 - 1 when none */
    uint8_t tail = fold(accumulated, rule, c->ex != NULL);
    if (tail != 0 && last < width) {
      for (int64_t t = last + 1; t <= width; t++) {
        line[t - j0] = tail;
      }
      last = width;
    }
    if (last >= j0) {
      fn(ctx, c->band + r, c->rect.x0 + (j0 - 1), (size_t)(last - j0 + 1), line);
    }
    for (int64_t t = lo; t <= hi && t <= width; t++) {
      cv[t] = 0;
      ar[t] = 0;
    }
  }
}

GSHCPU_Result gshcpu_render_cover_sparse(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, int64_t * acc, uint8_t * line, uint64_t max_cells, uint64_t * cells,
    GSHCPU_Span_Fn fn, void * ctx, GSHCPU_Exact * ex) {
  Cover c;
  memset(&c, 0, sizeof c);
  c.rect = rect;
  c.walk = walk;
  c.ex = ex;
  c.cstride = walk.x1 - walk.x0;
  c.stride = rect.x1 - rect.x0 + 1;
  c.max_cells = max_cells;
  int64_t width = rect.x1 - rect.x0;
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
    c.track = 1;
    for (int64_t r = 0; r < c.rows; r++) {
      c.lo[r] = INT64_MAX;
      c.hi[r] = -1;
    }
    if (ex) {
      exact_clear(&c);
    }
    for (size_t i = 0; i < count && !c.over; i++) {
      if (paths[i]) {
        path_edges(&c, paths[i]);
      }
    }
    if (c.over || (ex && exact_band(&c, paths, count))) {
      /* Leave the accumulator clean, as it was found: clear what the deposits made before the limit wrote. */
      for (int64_t r = 0; r < c.rows; r++) {
        for (int64_t t = c.lo[r]; t <= c.hi[r]; t++) {
          c.cover[r * c.stride + t] = 0;
          c.area[r * c.stride + t] = 0;
        }
      }
      *cells = c.cells;
      return GSHCPU_ERR_LIMIT;
    }
    sweep_band(&c, rule, width, line, fn, ctx);
  }
  *cells = c.cells;
  return GSHCPU_OK;
}
