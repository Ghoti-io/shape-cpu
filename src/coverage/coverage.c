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
 * The scan converter: cells, the sweep, the fill rule and the trim.  Moved from
 * `font`'s `src/raster/raster.c` (story 9), keeping its arithmetic.
 *
 * Every edge of the flattened path deposits two signed numbers into each cell
 * it crosses:
 *
 * - `cover`, the signed height of the crossing in 26.6 units, and
 * - `area`, that height times the sum of the crossing's two x positions within
 *   the cell.
 *
 * A sweep along each row then accumulates `cover` from left to right, which is
 * the winding number times a pixel's height, and `area` corrects the cell the
 * edge actually passes through.  This is FreeType's `ftgrays` arithmetic with
 * `PIXEL_BITS` of 6 rather than 8, because 26.6 is the family's pixel space
 * everywhere else and a second sub-pixel resolution would be a second rounding
 * rule.
 *
 * Nothing here is floating point, and nothing here reads the host's byte order.
 *
 * What is new relative to `font`: the input is a `shape-geometry` device path
 * (flattened by `shape-geometry`'s flattener where it has curves), the bitmap
 * rectangle is an argument, every limit is a `GSHCPU_Limits` field checked at
 * its bound, and device coordinates are `int64` until they are known to lie in
 * the rectangle, so that no coordinate in the `int32` range can wrap.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-cpu/core.h>
#include <ghoti.io/shape-cpu/coverage.h>

#include <ghoti.io/shape-geometry/flatten.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Sub-pixel bits: 26.6, the same everywhere in the family. */
#define PIXEL_BITS 6
/** One pixel in 26.6. */
#define ONE_PIXEL 64

/**
 * The shift that turns an accumulated area into 0..256.
 *
 * A fully covered pixel accumulates `2 * ONE_PIXEL * ONE_PIXEL`, and 256 is
 * full coverage, so the shift is `2 * PIXEL_BITS + 1 - 8`.  Spelled from
 * PIXEL_BITS rather than as 5, so that changing the sub-pixel resolution cannot
 * leave this behind.
 */
#define AREA_SHIFT (2 * PIXEL_BITS + 1 - 8)

/** Smallest cell allocation. */
#define MIN_CELLS 64u

/**
 * One pixel's accumulated crossings.
 *
 * `cover` and `area` are 64-bit because they are sums over every edge crossing
 * one pixel: a path with millions of points could cross one cell that many
 * times, and a 32-bit sum of `65536 * 64 * 128` would be within a factor of
 * eight of overflowing rather than comfortably clear of it.
 */
typedef struct Cell {
  int32_t x;     /* Column, from the rectangle's left edge. */
  int32_t y;     /* Row, from the rectangle's top. */
  int64_t cover; /* Signed height of the crossings, 26.6. */
  int64_t area;  /* Sum of height times the two x positions in the cell. */
} Cell;

/** The rasteriser's working state. */
typedef struct Raster {
  const GSHCPU_Allocator * allocator;
  GSHCPU_Result result;       /* The first failure, if any. */

  Cell * cells;
  size_t cell_count;
  size_t cell_capacity;
  size_t max_cells;

  uint32_t width;
  uint32_t height;

  GSHGEOM_Device_Point current; /* Where the pen is, 26.6, rectangle-relative. */
  bool dropped;                 /* A cell fell outside the rectangle. */
} Raster;

/* --- integer helpers ------------------------------------------------------- */

/**
 * Divide, rounding half away from zero, with both signs handled explicitly.
 */
static int64_t round_div(int64_t numerator, int64_t denominator) {
  uint64_t n = (uint64_t)(numerator < 0 ? -numerator : numerator);
  uint64_t d = (uint64_t)(denominator < 0 ? -denominator : denominator);
  uint64_t quotient = (n + d / 2) / d;
  bool negative = (numerator < 0) != (denominator < 0);

  return negative ? -(int64_t)quotient : (int64_t)quotient;
}

/**
 * Divide, rounding towards negative infinity.
 *
 * What a coverage cell's row and column want: a pixel boundary is a floor, and
 * rounding a negative coordinate towards zero puts two different y values in
 * the same scanline on one side of the origin and not the other.
 */
static int64_t floor_div(int64_t numerator, int64_t denominator) {
  int64_t quotient = numerator / denominator;

  if (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) {
    quotient -= 1;
  }
  return quotient;
}

/** Divide, rounding towards positive infinity. */
static int64_t ceil_div(int64_t numerator, int64_t denominator) {
  int64_t quotient = numerator / denominator;

  if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) {
    quotient += 1;
  }
  return quotient;
}

/** a * b into *out, false on overflow. */
static bool mul_size(size_t a, size_t b, size_t * out) {
  if (a != 0 && b > SIZE_MAX / a) {
    return false;
  }
  *out = a * b;
  return true;
}

/** The result of a shape-geometry call, as this library's. */
static GSHCPU_Result from_geometry(GSHGEOM_Result result) {
  switch (result) {
  case GSHGEOM_OK:
    return GSHCPU_OK;
  case GSHGEOM_ERR_LIMIT:
    return GSHCPU_ERR_LIMIT;
  case GSHGEOM_ERR_OOM:
    return GSHCPU_ERR_OOM;
  case GSHGEOM_ERR_INVALID:
    return GSHCPU_ERR_INVALID;
  case GSHGEOM_ERR_IO:
  case GSHGEOM_ERR_FORMAT:
  case GSHGEOM_ERR_UNSUPPORTED:
  case GSHGEOM_ERR_CORRUPT:
  case GSHGEOM_ERR_INTERNAL:
  case GSHGEOM_RESULT_COUNT:
    break;
  }
  return GSHCPU_ERR_INTERNAL;
}

/* --- cells ----------------------------------------------------------------- */

/**
 * Add a crossing to the cell at (@p ex, @p ey).
 *
 * @param dy  The signed height crossed, 26.6.
 * @param fx1 Where the crossing entered, within the cell, 0..ONE_PIXEL.
 * @param fx2 Where it left.
 */
static void cell_add(Raster * raster, int32_t ex, int32_t ey, int32_t dy,
    int32_t fx1, int32_t fx2) {
  Cell * cell = NULL;

  if (dy == 0) {
    /* A horizontal crossing contributes no winding and no area.  Skipped
     * rather than recorded, because a cell of zeroes changes the sweep's cell
     * list without changing its answer. */
    return;
  }
  if (ex < 0 || ey < 0 || (uint32_t)ex >= raster->width
      || (uint32_t)ey >= raster->height) {
    /* Cannot happen once every point lies inside the rectangle, which the
     * caller checked.  Recorded rather than ignored so that a bounds bug is a
     * reported failure and not a missing span. */
    raster->dropped = true;
    return;
  }
  if (raster->cell_count > 0) {
    Cell * last = &raster->cells[raster->cell_count - 1];

    if (last->x == ex && last->y == ey) {
      /* The common case by far: a walk along one edge stays in a cell for
       * several steps.  Merging here keeps the array small enough that the
       * sort afterwards is cheap. */
      last->cover += dy;
      last->area += (int64_t)dy * (fx1 + fx2);
      return;
    }
  }
  if (raster->cell_count >= raster->max_cells) {
    raster->result = GSHCPU_ERR_LIMIT;
    return;
  }
  if (raster->cell_count == raster->cell_capacity) {
    size_t capacity = raster->cell_capacity ? raster->cell_capacity * 2
                                            : MIN_CELLS;
    size_t bytes = 0;
    void * grown = NULL;

    if (capacity > raster->max_cells) {
      capacity = raster->max_cells;
    }
    if (!mul_size(capacity, sizeof *raster->cells, &bytes)) {
      raster->result = GSHCPU_ERR_OOM;
      return;
    }
    grown = raster->allocator->realloc_fn(raster->allocator->ctx,
        raster->cells, bytes);
    if (!grown) {
      raster->result = GSHCPU_ERR_OOM;
      return;
    }
    raster->cells = grown;
    raster->cell_capacity = capacity;
  }
  cell = &raster->cells[raster->cell_count++];
  cell->x = ex;
  cell->y = ey;
  cell->cover = dy;
  cell->area = (int64_t)dy * (fx1 + fx2);
}

/**
 * Accumulate one piece of an edge that lies within a single row.
 *
 * Both endpoints are rectangle-relative 26.6 and share a row.
 */
static void edge_scanline(Raster * raster, int32_t ey,
    GSHGEOM_Device_Point from, GSHGEOM_Device_Point to) {
  int32_t ex1 = (int32_t)floor_div(from.x, ONE_PIXEL);
  int32_t ex2 = (int32_t)floor_div(to.x, ONE_PIXEL);
  int32_t dx = to.x - from.x;
  int32_t dy = to.y - from.y;
  int32_t step;
  int32_t ex;
  GSHGEOM_Device_Point cursor = from;

  if (ex1 == ex2) {
    cell_add(raster, ex1, ey, dy,
        from.x - ex1 * ONE_PIXEL, to.x - ex1 * ONE_PIXEL);
    return;
  }
  /* The edge crosses at least one column boundary.  Each boundary's y is found
   * once and shared by the pieces either side of it, so the walk stays closed:
   * rounding a boundary twice is how a seam appears between two columns. */
  step = dx > 0 ? 1 : -1;
  for (ex = ex1; ex != ex2; ex += step) {
    const int32_t boundary = (step > 0 ? ex + 1 : ex) * ONE_PIXEL;
    const int32_t at_y = (int32_t)(from.y
        + round_div((int64_t)(boundary - from.x) * dy, dx));
    GSHGEOM_Device_Point split;

    split.x = boundary;
    split.y = at_y;
    cell_add(raster, ex, ey, split.y - cursor.y,
        cursor.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);
    cursor = split;
  }
  cell_add(raster, ex2, ey, to.y - cursor.y,
      cursor.x - ex2 * ONE_PIXEL, to.x - ex2 * ONE_PIXEL);
}

/**
 * Accumulate one edge of the flattened path, from the pen to @p to.
 *
 * Rows are y-down here.
 */
static void edge_line(Raster * raster, GSHGEOM_Device_Point to) {
  GSHGEOM_Device_Point from = raster->current;
  int32_t ey1;
  int32_t ey2;
  int32_t dy;
  int32_t dx;
  int32_t step;
  int32_t ey;
  GSHGEOM_Device_Point cursor;

  raster->current = to;
  if (from.y == to.y) {
    /* A horizontal edge crosses no scanline and so contributes nothing.  This
     * is not an optimisation: giving it a row to belong to would require
     * choosing one, and either choice puts area in a cell the edge does not
     * bound. */
    return;
  }
  ey1 = (int32_t)floor_div(from.y, ONE_PIXEL);
  ey2 = (int32_t)floor_div(to.y, ONE_PIXEL);
  if (ey1 == ey2) {
    edge_scanline(raster, ey1, from, to);
    return;
  }
  dy = to.y - from.y;
  dx = to.x - from.x;
  step = dy > 0 ? 1 : -1;
  cursor = from;
  for (ey = ey1; ey != ey2; ey += step) {
    const int32_t boundary = (step > 0 ? ey + 1 : ey) * ONE_PIXEL;
    const int32_t at_x = (int32_t)(from.x
        + round_div((int64_t)(boundary - from.y) * dx, dy));
    GSHGEOM_Device_Point split;

    split.x = at_x;
    split.y = boundary;
    edge_scanline(raster, ey, cursor, split);
    cursor = split;
    if (raster->result != GSHCPU_OK) {
      return;
    }
  }
  edge_scanline(raster, ey2, cursor, to);
}

/* --- the sweep ------------------------------------------------------------- */

/**
 * Sort the cells by row and then by column, merging the duplicates.
 *
 * A counting sort by row and an insertion sort within each row, both written
 * here rather than handed to `qsort`: the comparison order of equal keys is
 * unspecified for `qsort`, and while adding integers is commutative - so the
 * merged totals would be the same either way - the determinism claim is easier
 * to hold when the sort is not a platform's.
 */
static GSHCPU_Result sort_cells(Raster * raster, size_t ** out_rows) {
  const GSHCPU_Allocator * allocator = raster->allocator;
  size_t * rows = NULL;
  Cell * sorted = NULL;
  size_t bytes = 0;

  /* rows[y] is where row y starts, and rows[height] closes the last one. */
  rows = allocator->calloc_fn(allocator->ctx, (size_t)raster->height + 2u,
      sizeof *rows);
  if (!rows) {
    return GSHCPU_ERR_OOM;
  }
  if (raster->cell_count == 0) {
    *out_rows = rows;
    return GSHCPU_OK;
  }
  if (!mul_size(raster->cell_count, sizeof *sorted, &bytes)) {
    allocator->free_fn(allocator->ctx, rows);
    return GSHCPU_ERR_OOM;
  }
  sorted = allocator->malloc_fn(allocator->ctx, bytes);
  if (!sorted) {
    allocator->free_fn(allocator->ctx, rows);
    return GSHCPU_ERR_OOM;
  }

  for (size_t i = 0; i < raster->cell_count; ++i) {
    rows[(size_t)raster->cells[i].y + 1u] += 1;
  }
  for (uint32_t y = 0; y < raster->height + 1u; ++y) {
    rows[y + 1u] += rows[y];
  }
  {
    /* A second copy of the starts, consumed while filling; rows[] itself has
     * to survive as the index the sweep reads. */
    size_t * cursor = allocator->calloc_fn(allocator->ctx,
        (size_t)raster->height + 2u, sizeof *cursor);

    if (!cursor) {
      allocator->free_fn(allocator->ctx, sorted);
      allocator->free_fn(allocator->ctx, rows);
      return GSHCPU_ERR_OOM;
    }
    memcpy(cursor, rows, ((size_t)raster->height + 2u) * sizeof *cursor);
    for (size_t i = 0; i < raster->cell_count; ++i) {
      sorted[cursor[(size_t)raster->cells[i].y]++] = raster->cells[i];
    }
    allocator->free_fn(allocator->ctx, cursor);
  }
  memcpy(raster->cells, sorted, raster->cell_count * sizeof *sorted);
  allocator->free_fn(allocator->ctx, sorted);

  /* Within each row: insertion sort by column, merging equal columns as they
   * meet.  Rows are short - a cell per column crossed - so this is linear in
   * practice and needs no scratch. */
  for (uint32_t y = 0; y < raster->height; ++y) {
    size_t first = rows[y];
    size_t last = rows[y + 1u];

    for (size_t i = first + 1; i < last; ++i) {
      Cell held = raster->cells[i];
      size_t j = i;

      while (j > first && raster->cells[j - 1].x > held.x) {
        raster->cells[j] = raster->cells[j - 1];
        j -= 1;
      }
      raster->cells[j] = held;
    }
    /* Merge equal columns, compacting the row in place.  The slots the merge
     * frees stay in the row's span, so they are filled with a sentinel column
     * that sorts after every real one and carries no cover: the sweep stops at
     * the first of them.  Shrinking the span instead would mean a second array
     * of per-row counts for no gain. */
    {
      size_t write = first;

      for (size_t read = first; read < last; ++read) {
        if (write > first
            && raster->cells[write - 1].x == raster->cells[read].x) {
          raster->cells[write - 1].cover += raster->cells[read].cover;
          raster->cells[write - 1].area += raster->cells[read].area;
          continue;
        }
        raster->cells[write++] = raster->cells[read];
      }
      for (size_t fill = write; fill < last; ++fill) {
        raster->cells[fill].x = INT32_MAX;
        raster->cells[fill].cover = 0;
        raster->cells[fill].area = 0;
      }
    }
  }
  *out_rows = rows;
  return GSHCPU_OK;
}

/** Turn an accumulated area into 8-bit coverage under the fill rule. */
static uint8_t area_to_coverage(int64_t area, GSHGEOM_Fill_Rule fill) {
  int64_t value = area >> AREA_SHIFT;

  if (value < 0) {
    value = -value;
  }
  if (fill == GSHGEOM_FILL_EVEN_ODD) {
    /* Winding 2 is outside again, so the ramp folds every 512. */
    value &= 511;
    if (value > 256) {
      value = 512 - value;
    }
    else if (value == 256) {
      value = 255;
    }
  }
  else if (value >= 256) {
    value = 255;
  }
  return (uint8_t)value;
}

/** Write a run of pixels at one coverage. */
static void write_span(GSHCPU_Coverage * coverage, uint32_t row, int32_t from,
    int32_t count, int64_t area) {
  const uint8_t value = area_to_coverage(area, coverage->fill);
  uint8_t * line;

  if (value == 0 || count <= 0 || from >= (int32_t)coverage->width) {
    return;
  }
  if (from < 0) {
    count += from;
    from = 0;
  }
  if (from + count > (int32_t)coverage->width) {
    count = (int32_t)coverage->width - from;
  }
  if (count <= 0) {
    return;
  }
  line = coverage->data + (size_t)row * coverage->stride;
  memset(line + from, value, (size_t)count);
}

/** Walk each row's cells, accumulating winding and writing spans. */
static void sweep(const Raster * raster, const size_t * rows,
    GSHCPU_Coverage * coverage) {
  for (uint32_t y = 0; y < raster->height; ++y) {
    int64_t cover = 0;
    int32_t x = 0;

    for (size_t index = rows[y]; index < rows[y + 1u]; ++index) {
      const Cell * cell = &raster->cells[index];

      if (cell->x == INT32_MAX) {
        break;
      }
      if (cover != 0 && cell->x > x) {
        write_span(coverage, y, x, cell->x - x, cover);
      }
      cover += cell->cover * (ONE_PIXEL * 2);
      write_span(coverage, y, cell->x, 1, cover - cell->area);
      x = cell->x + 1;
    }
    /* No fill after the last cell.  FreeType's sweep has one, because it clips
     * cells to the bitmap and so loses the closing edge of a shape that reaches
     * the right margin; here the caller's rectangle has a margin of its own on
     * every side (the rule of gshcpu_coverage_fill: every point strictly
     * inside), so every closing edge has a cell of its own inside it and the
     * accumulated cover is back to zero by the end of every row. */
  }
}

/* --- trimming -------------------------------------------------------------- */

void gshcpu_coverage_trim(GSHCPU_Coverage * coverage) {
  uint32_t first_row;
  uint32_t last_row = 0;
  uint32_t first_column;
  uint32_t last_column = 0;
  uint32_t width;
  uint32_t height;

  if (!coverage || !coverage->data) {
    return;
  }
  first_row = coverage->height;
  first_column = coverage->width;
  for (uint32_t row = 0; row < coverage->height; ++row) {
    const uint8_t * line = coverage->data + (size_t)row * coverage->stride;

    for (uint32_t column = 0; column < coverage->width; ++column) {
      if (line[column] == 0) {
        continue;
      }
      if (row < first_row) {
        first_row = row;
      }
      last_row = row;
      if (column < first_column) {
        first_column = column;
      }
      if (column > last_column) {
        last_column = column;
      }
    }
  }
  if (first_row > last_row || first_column > last_column) {
    const GSHCPU_Allocator * allocator = coverage->allocator;

    allocator->free_fn(allocator->ctx, coverage->data);
    coverage->data = NULL;
    coverage->width = 0;
    coverage->height = 0;
    coverage->stride = 0;
    coverage->left = 0;
    coverage->top = 0;
    return;
  }
  width = last_column - first_column + 1u;
  height = last_row - first_row + 1u;
  for (uint32_t row = 0; row < height; ++row) {
    memmove(coverage->data + (size_t)row * width,
        coverage->data + (size_t)(row + first_row) * coverage->stride
            + first_column,
        width);
  }
  coverage->left += (int32_t)first_column;
  coverage->top += (int32_t)first_row;
  coverage->width = width;
  coverage->height = height;
  coverage->stride = width;
}

/* --- the fill -------------------------------------------------------------- */

/** An empty coverage: nothing to composite. */
static void make_empty(GSHCPU_Coverage * out, GSHGEOM_Fill_Rule fill,
    const GSHCPU_Allocator * allocator) {
  memset(out, 0, sizeof *out);
  out->fill = fill;
  out->allocator = allocator;
}

/**
 * Fill a flattened path into the rectangle (left, top, width, height).
 *
 * The limits are checked before anything is allocated, and the points before
 * any arithmetic on them that could wrap: a point is held as `int64` relative
 * to the rectangle, and only a point strictly inside it is narrowed.
 */
static GSHCPU_Result fill_flat(const GSHGEOM_Flat_Path * flat,
    GSHGEOM_Fill_Rule fill, int32_t left, int32_t top, size_t width,
    size_t height, const GSHCPU_Limits * limits,
    const GSHCPU_Allocator * allocator, GSHCPU_Coverage * out) {
  const GSHGEOM_Device_Point * points = gshgeom_flat_path_points(flat);
  const size_t point_count = gshgeom_flat_path_point_count(flat);
  const size_t subpath_count = gshgeom_flat_path_subpath_count(flat);
  size_t max_dimension = limits && limits->max_dimension
      ? limits->max_dimension : GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;
  const size_t max_bytes = limits && limits->max_bytes
      ? limits->max_bytes : GSHCPU_LIMITS_DEFAULT_MAX_BYTES;
  const size_t max_cells = limits && limits->max_cells
      ? limits->max_cells : GSHCPU_LIMITS_DEFAULT_MAX_CELLS;
  const int64_t origin_x = (int64_t)left * ONE_PIXEL;
  const int64_t origin_y = (int64_t)top * ONE_PIXEL;
  GSHCPU_Coverage coverage;
  Raster raster;
  size_t * rows = NULL;
  size_t bytes = 0;
  GSHCPU_Result result;

  if (max_dimension > GSHCPU_MAX_DIMENSION_CEILING) {
    max_dimension = GSHCPU_MAX_DIMENSION_CEILING;
  }
  if (width > max_dimension || height > max_dimension) {
    return GSHCPU_ERR_LIMIT;
  }
  if (width == 0 || height == 0) {
    make_empty(out, fill, allocator);
    return GSHCPU_OK;
  }
  if (!mul_size(width, height, &bytes) || bytes > max_bytes) {
    return GSHCPU_ERR_LIMIT;
  }
  for (size_t i = 0; i < point_count; ++i) {
    const int64_t x = (int64_t)points[i].x - origin_x;
    const int64_t y = (int64_t)points[i].y - origin_y;

    if (x < 0 || y < 0 || x >= (int64_t)width * ONE_PIXEL
        || y >= (int64_t)height * ONE_PIXEL) {
      return GSHCPU_ERR_INVALID;
    }
  }

  memset(&coverage, 0, sizeof coverage);
  coverage.allocator = allocator;
  coverage.fill = fill;
  coverage.width = (uint32_t)width;
  coverage.height = (uint32_t)height;
  coverage.stride = width;
  coverage.left = left;
  coverage.top = top;
  coverage.data = allocator->calloc_fn(allocator->ctx, bytes, 1u);
  if (!coverage.data) {
    return GSHCPU_ERR_OOM;
  }

  memset(&raster, 0, sizeof raster);
  raster.allocator = allocator;
  raster.result = GSHCPU_OK;
  raster.max_cells = max_cells;
  raster.width = coverage.width;
  raster.height = coverage.height;

  for (size_t s = 0; s < subpath_count && raster.result == GSHCPU_OK; ++s) {
    GSHGEOM_Flat_Subpath sub;
    GSHGEOM_Device_Point start;

    if (gshgeom_flat_path_subpath(flat, s, &sub) != GSHGEOM_OK) {
      raster.result = GSHCPU_ERR_INTERNAL;
      break;
    }
    start.x = (int32_t)((int64_t)points[sub.first].x - origin_x);
    start.y = (int32_t)((int64_t)points[sub.first].y - origin_y);
    raster.current = start;
    for (size_t k = 1; k < sub.count && raster.result == GSHCPU_OK; ++k) {
      GSHGEOM_Device_Point to;

      to.x = (int32_t)((int64_t)points[sub.first + k].x - origin_x);
      to.y = (int32_t)((int64_t)points[sub.first + k].y - origin_y);
      edge_line(&raster, to);
    }
    /* The closing line, explicit for an open subpath and implicit for a closed
     * one; the winding number is only well defined with it. */
    if (raster.result == GSHCPU_OK) {
      edge_line(&raster, start);
    }
  }
  result = raster.result;
  if (result == GSHCPU_OK && raster.dropped) {
    /* The caller's rectangle and its points disagreed, which the check above
     * makes this library's bug and not the caller's. */
    result = GSHCPU_ERR_INTERNAL;
  }
  if (result == GSHCPU_OK) {
    result = sort_cells(&raster, &rows);
  }
  if (result == GSHCPU_OK) {
    sweep(&raster, rows, &coverage);
  }
  allocator->free_fn(allocator->ctx, rows);
  allocator->free_fn(allocator->ctx, raster.cells);
  if (result != GSHCPU_OK) {
    allocator->free_fn(allocator->ctx, coverage.data);
    return result;
  }
  *out = coverage;
  return GSHCPU_OK;
}

/** Flatten a device path under the limits' point cap. */
static GSHCPU_Result flatten(const GSHGEOM_Device_Path * path,
    const GSHCPU_Limits * limits, const GSHCPU_Allocator * allocator,
    GSHGEOM_Flat_Path ** flat) {
  GSHGEOM_Limits geometry;

  gshgeom_limits_default(&geometry);
  if (limits && limits->max_points) {
    geometry.max_points = limits->max_points;
  }
  return from_geometry(
      gshgeom_device_path_flatten(path, allocator, &geometry, flat));
}

GSHCPU_Result gshcpu_coverage_fill(const GSHGEOM_Device_Path * path,
    GSHGEOM_Fill_Rule fill, int32_t left, int32_t top, uint32_t width,
    uint32_t height, const GSHCPU_Limits * limits,
    const GSHCPU_Allocator * allocator, GSHCPU_Coverage * out) {
  GSHGEOM_Flat_Path * flat = NULL;
  GSHCPU_Result result;

  if (!path || !out || (unsigned)fill >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {
    return GSHCPU_ERR_INVALID;
  }
  if (!allocator) {
    allocator = gshgeom_allocator_default();
  }
  result = flatten(path, limits, allocator, &flat);
  if (result != GSHCPU_OK) {
    return result;
  }
  result = fill_flat(flat, fill, left, top, width, height, limits, allocator,
      out);
  gshgeom_flat_path_destroy(flat);
  return result;
}

GSHCPU_Result gshcpu_coverage_from_path(const GSHGEOM_Device_Path * path,
    GSHGEOM_Fill_Rule fill, const GSHCPU_Limits * limits,
    const GSHCPU_Allocator * allocator, GSHCPU_Coverage * out) {
  GSHGEOM_Flat_Path * flat = NULL;
  const GSHGEOM_Device_Point * points;
  size_t point_count;
  int64_t x_min;
  int64_t x_max;
  int64_t y_min;
  int64_t y_max;
  int64_t left;
  int64_t top;
  int64_t width;
  int64_t height;
  GSHCPU_Result result;

  if (!path || !out || (unsigned)fill >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {
    return GSHCPU_ERR_INVALID;
  }
  if (!allocator) {
    allocator = gshgeom_allocator_default();
  }
  result = flatten(path, limits, allocator, &flat);
  if (result != GSHCPU_OK) {
    return result;
  }
  points = gshgeom_flat_path_points(flat);
  point_count = gshgeom_flat_path_point_count(flat);
  if (point_count == 0) {
    gshgeom_flat_path_destroy(flat);
    make_empty(out, fill, allocator);
    return GSHCPU_OK;
  }
  x_min = x_max = points[0].x;
  y_min = y_max = points[0].y;
  for (size_t i = 1; i < point_count; ++i) {
    if (points[i].x < x_min) {
      x_min = points[i].x;
    }
    if (points[i].x > x_max) {
      x_max = points[i].x;
    }
    if (points[i].y < y_min) {
      y_min = points[i].y;
    }
    if (points[i].y > y_max) {
      y_max = points[i].y;
    }
  }
  /* The extent grown to whole pixels, then one pixel on every side: the margin
   * is what lets a closing edge on the extent's last pixel boundary have a cell
   * of its own.  Computed in int64, so that no extent can wrap. */
  left = floor_div(x_min, ONE_PIXEL) - 1;
  top = floor_div(y_min, ONE_PIXEL) - 1;
  width = ceil_div(x_max, ONE_PIXEL) + 1 - left;
  height = ceil_div(y_max, ONE_PIXEL) + 1 - top;
  result = fill_flat(flat, fill, (int32_t)left, (int32_t)top, (size_t)width,
      (size_t)height, limits, allocator, out);
  gshgeom_flat_path_destroy(flat);
  if (result == GSHCPU_OK) {
    gshcpu_coverage_trim(out);
  }
  return result;
}
