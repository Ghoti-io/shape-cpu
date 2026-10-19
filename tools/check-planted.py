#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Shape CPU.
#
# Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""Show that the tests fail on planted defects (CAP-6, CAP-13).

A test that has never been seen to fail may measure nothing.  For each defect
below this builds the library's sources with one literal replacement applied
(the text must match exactly once and the replacement must change it), links
the tests that are meant to catch it against the result, and requires one of
them to fail.  The unplanted build is the control: every test must pass on it,
which is what makes a failure mean the defect and not the build.

A plant is a replacement in a source file (``src``) or in a public header
(``hdr``: the whole library is rebuilt with the changed header ahead of the
real one on the include path; the tests keep the real one, which is how a
changed constant disagrees with its pinned value).  The probe (the
cross-target hash) is checked too: a bit flipped in one result must change it.

Families (story 9):

  constants   the sub-pixel bits, the pixel, the area shift
  division    the three integer divisions the cell arithmetic rests on: round
              half away from zero, floor, ceiling, and their signs
  cells       the cell accumulator: merging, the cell cap, growth, the zero-
              height crossing
  scanline,
  line        an edge cut at pixel boundaries, each boundary rounded once and
              shared by the pieces either side
  sort        the counting sort by row, the insertion sort by column, the merge
              of equal columns, the sentinel, the leaks on each failure
  fold        the area to 0..255 under each rule: the floor before the absolute
              value, the 512 fold, the ramp, saturation
  sweep       the cover accumulation, the gap fill, the cell's own pixel
  trim        the bounding box of the non-zero pixels and the move of ``left``
              and ``top``, the empty result
  fill        the rectangle: the limits by their bounds, the point check, the
              frame, the subpaths and their closing line, the failure paths
  entry       argument checks, the flattening and its point cap, the extent
              grown by a pixel, the trim
  report      at, total, destroy, the hash's fields, order and width, the dump
  core        the result strings, the default limits and their header constants

What is NOT planted, and why.  A defect that no input can reach, or that
changes no result, cannot be seen by any test:

  - the four bounds tests in ``cell_add`` (``ex < 0``, ``ey < 0``, ``ex`` or
    ``ey`` past the rectangle): once every point lies strictly inside the
    rectangle, which ``fill_flat`` checks, no cell can fall outside it, so the
    guard is unreachable; it is kept because a bounds bug there would write
    past the cell array's rows, and ``dropped`` is reported as an internal
    error rather than ignored
  - the clipping branches of ``write_span`` (``from < 0``, ``from + count >
    width``, ``from >= width``): a span begins at 0 or just after a cell inside
    the rectangle and ends before the next cell, so none is reached
  - ``mul_size``'s overflow test: the product of two dimensions of at most
    2^24 cannot overflow ``size_t``; it is the cell array's guard that the
    cap on ``max_cells`` makes unreachable too
  - the clamp of the cell array's capacity to ``max_cells``: it changes how
    much is allocated, never what is computed
  - the first iteration of the subpath loop (``k = 1``): an edge from the start
    to itself adds nothing
  - the ``INTERNAL`` mapping of a ``shape-geometry`` error that cannot happen
    (``IO``, ``FORMAT``, ``UNSUPPORTED``, ``CORRUPT``, ``INTERNAL``)
  - ``total`` as 64 bits rather than 32: a sum over 2^32 / 255 pixels
  - the ``sweep`` after the last cell (there is none, see the comment there)
  - the floor of a negative row in ``edge_line`` (``from.y / 64`` for
    ``floor_div(from.y, 64)``): coordinates are relative to the rectangle and
    never negative there
  - the check of ``raster->result`` inside an edge's row loop: after a limit is
    hit the remaining calls fail the same way, so only the time changes
  - the one extra slot of ``rows`` in the sort (``height + 1`` in the prefix
    sum): the row index has a spare element nothing reads
  - the sentinel cell's ``cover`` (the sweep stops at its column and never reads
    it), the ``value == 0`` skip of ``write_span`` (it writes zeros over zeros),
    ``cover != 0 &&`` in the sweep's gap test (a span of zero cover is empty,
    and a gap of zero or less is skipped by ``write_span`` itself), a ``static``
    ``cover`` in the sweep (it is back to zero at the end of every row of a
    closed shape), and ``&&`` for ``||`` in the trim's empty test (the two halves
    are true together or not at all)
  - the defaults of the point cap in ``flatten`` (``shape-geometry``'s are the
    same numbers) and the ``!path`` test in ``gshcpu_coverage_fill`` (the
    flattener refuses a NULL path with the same result)

Run by ``make check-planted`` from the library directory, with the build's
flags in the PLANT_* environment variables.
"""
import concurrent.futures
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def env(name, default=None):
    value = os.environ.get(name, default)
    if value is None:
        sys.exit("check-planted: %s is not set (run it with `make check-planted`)" % name)
    return value


CC = shlex.split(env("PLANT_CC"))
CFLAGS = shlex.split(env("PLANT_CFLAGS"))
PROBE_CFLAGS = shlex.split(env("PLANT_PROBE_CFLAGS"))
PROBE_LIBS = env("PLANT_PROBE_LIBS")
CXX = env("PLANT_CXX")
CXX_INCLUDE = env("PLANT_CXX_INCLUDE")
LDFLAGS = env("PLANT_LDFLAGS")
TESTS = env("PLANT_TESTS").split()
OBJ_DIR = Path(env("PLANT_OBJ_DIR"))
LD_PATH = env("PLANT_LD_PATH")
WORK = Path(env("PLANT_WORK"))
JOBS = int(env("PLANT_JOBS", "4"))
# PLANT_ONLY=<regex> runs only the plants whose id matches (and not the probe's
# own plants): for re-running the few that a change to a test touched.  The
# control always runs.  Unset, every plant runs, which is what `make test` wants.
ONLY = os.environ.get("PLANT_ONLY")
# PLANT_PROBE_ONLY=1: the control and the probe's own plants, no source plant
# (for re-running the probe's gate after a change to the probe alone).
PROBE_ONLY = bool(os.environ.get("PLANT_PROBE_ONLY"))


def test_object(test):
    """testCurve -> build/.../tests/test_curve.o"""
    snake = re.sub(r"(?<!^)(?=[A-Z])", "_", test[len("test"):]).lower()
    return OBJ_DIR / "tests" / ("test_%s.o" % snake)


def sources():
    return sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src").rglob("*.c"))


# --- the plants -------------------------------------------------------------
# (id, kind, file, old, new, tests that must catch it)
# kind: "src" or "hdr" (the whole library is rebuilt with the changed header
# ahead of the real one on the include path; the tests keep the real one, which
# is how a changed constant disagrees with its pinned value).

CV = "src/coverage/coverage.c"
RP = "src/coverage/report.c"
CO = "src/core/core.c"
CH = "include/ghoti.io/shape-cpu/core.h"

K = ["testCoverage"]
L = ["testCoverage_limits"]
R = ["testCoverage_report"]
C = ["testCore"]
KL = K + L
KLR = K + L + R

PLANTS = [
    # --- the constants of the definition --------------------------------------
    ("pixel-bits", "src", CV, "#define PIXEL_BITS 6", "#define PIXEL_BITS 5", KLR),
    ("one-pixel", "src", CV, "#define ONE_PIXEL 64", "#define ONE_PIXEL 32", KLR),
    ("area-shift-no-plus-one", "src", CV, "#define AREA_SHIFT (2 * PIXEL_BITS + 1 - 8)", "#define AREA_SHIFT (2 * PIXEL_BITS - 8)", KLR),
    ("area-shift-eight-bits", "src", CV, "#define AREA_SHIFT (2 * PIXEL_BITS + 1 - 8)", "#define AREA_SHIFT (2 * PIXEL_BITS + 1 - 7)", KLR),
    ("min-cells-zero", "src", CV, "#define MIN_CELLS 64u", "#define MIN_CELLS 0u", KL),

    # --- integer division -----------------------------------------------------
    ("round-div-truncates", "src", CV, "uint64_t quotient = (n + d / 2) / d;", "uint64_t quotient = n / d;", K),
    ("round-div-rounds-up-ties-and-more", "src", CV, "uint64_t quotient = (n + d / 2) / d;", "uint64_t quotient = (n + d - 1) / d;", K),
    ("round-div-sign", "src", CV, "bool negative = (numerator < 0) != (denominator < 0);", "bool negative = (numerator < 0) == (denominator < 0);", K),
    ("round-div-ignores-sign", "src", CV, "return negative ? -(int64_t)quotient : (int64_t)quotient;", "(void)negative;\n  return (int64_t)quotient;", K),
    ("floor-div-truncates", "src", CV, "  if (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) {\n    quotient -= 1;", "  if (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) {\n    quotient -= 0;", K + L),
    ("floor-div-sign-test", "src", CV, "  if (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) {\n    quotient -= 1;", "  if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) {\n    quotient -= 1;", K + L),
    ("floor-div-exact-division", "src", CV, "  if (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) {\n    quotient -= 1;", "  if (((numerator < 0) != (denominator < 0))) {\n    quotient -= 1;", K + L),
    ("ceil-div-truncates", "src", CV, "  if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) {\n    quotient += 1;", "  if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) {\n    quotient += 0;", K + L),
    ("ceil-div-sign-test", "src", CV, "  if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) {\n    quotient += 1;", "  if (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) {\n    quotient += 1;", K + L),
    ("ceil-div-exact-division", "src", CV, "  if (numerator % denominator != 0 && ((numerator < 0) == (denominator < 0))) {\n    quotient += 1;", "  if (((numerator < 0) == (denominator < 0))) {\n    quotient += 1;", K + L),

    # --- results ----------------------------------------------------------------
    ("limit-is-invalid", "src", CV, "  case GSHGEOM_ERR_LIMIT:\n    return GSHCPU_ERR_LIMIT;", "  case GSHGEOM_ERR_LIMIT:\n    return GSHCPU_ERR_INVALID;", L),
    ("oom-is-limit", "src", CV, "  case GSHGEOM_ERR_OOM:\n    return GSHCPU_ERR_OOM;", "  case GSHGEOM_ERR_OOM:\n    return GSHCPU_ERR_LIMIT;", L),

    # --- cells ------------------------------------------------------------------
    ("cell-keeps-horizontal", "src", CV, "  if (dy == 0) {\n    /* A horizontal crossing", "  if (dy == 0 && 0) {\n    /* A horizontal crossing", L),
    ("cell-merge-or", "src", CV, "if (last->x == ex && last->y == ey) {", "if (last->x == ex || last->y == ey) {", K),
    ("cell-merge-x-only", "src", CV, "if (last->x == ex && last->y == ey) {", "if (last->x == ex) {", K),
    ("cell-merge-cover", "src", CV, "      last->cover += dy;", "      last->cover -= dy;", K),
    ("cell-merge-area", "src", CV, "      last->area += (int64_t)dy * (fx1 + fx2);", "      last->area += (int64_t)dy * (fx1 - fx2);", K),
    ("cell-new-cover", "src", CV, "  cell->cover = dy;", "  cell->cover = dy + 1;", K),
    ("cell-new-area", "src", CV, "  cell->area = (int64_t)dy * (fx1 + fx2);", "  cell->area = (int64_t)dy * fx1;", K),
    ("cell-new-position", "src", CV, "  cell->x = ex;\n  cell->y = ey;", "  cell->x = ey;\n  cell->y = ex;", K),
    ("cell-cap-inclusive", "src", CV, "if (raster->cell_count >= raster->max_cells) {", "if (raster->cell_count > raster->max_cells) {", L),
    ("cell-cap-ignored", "src", CV, "if (raster->cell_count >= raster->max_cells) {\n    raster->result = GSHCPU_ERR_LIMIT;\n    return;\n  }", "if (0) {\n    raster->result = GSHCPU_ERR_LIMIT;\n    return;\n  }", L),
    ("cell-cap-is-oom", "src", CV, "    raster->result = GSHCPU_ERR_LIMIT;\n    return;", "    raster->result = GSHCPU_ERR_OOM;\n    return;", L),
    ("cell-growth-does-not-double", "src", CV, "size_t capacity = raster->cell_capacity ? raster->cell_capacity * 2\n                                            : MIN_CELLS;", "size_t capacity = raster->cell_capacity ? raster->cell_capacity + 1\n                                            : MIN_CELLS;", L),
    ("cell-realloc-failure-ignored", "src", CV, "    if (!grown) {\n      raster->result = GSHCPU_ERR_OOM;\n      return;\n    }", "    if (!grown) {\n      return;\n    }", L),
    ("cell-keeps-old-array", "src", CV, "    raster->cells = grown;\n    raster->cell_capacity = capacity;", "    raster->cell_capacity = capacity;", KL),

    # --- one piece of an edge within a row -------------------------------------------
    ("scanline-same-column-test", "src", CV, "  if (ex1 == ex2) {\n    cell_add(raster, ex1, ey, dy,", "  if (ex1 != ex2) {\n    cell_add(raster, ex1, ey, dy,", K),
    ("scanline-fx-from", "src", CV, "        from.x - ex1 * ONE_PIXEL, to.x - ex1 * ONE_PIXEL);", "        from.x - ex1 * ONE_PIXEL, from.x - ex1 * ONE_PIXEL);", K),
    ("scanline-step", "src", CV, "  step = dx > 0 ? 1 : -1;\n  for (ex = ex1; ex != ex2; ex += step) {", "  step = dx > 0 ? -1 : 1;\n  for (ex = ex1; ex != ex2; ex += step) {", K),
    ("scanline-boundary", "src", CV, "    const int32_t boundary = (step > 0 ? ex + 1 : ex) * ONE_PIXEL;", "    const int32_t boundary = (step > 0 ? ex : ex + 1) * ONE_PIXEL;", K),
    ("scanline-boundary-y-truncates", "src", CV, "        + round_div((int64_t)(boundary - from.x) * dy, dx));\n    GSHGEOM_Device_Point split;\n\n    split.x = boundary;", "        + ((int64_t)(boundary - from.x) * dy) / dx);\n    GSHGEOM_Device_Point split;\n\n    split.x = boundary;", K),
    ("scanline-boundary-y-from", "src", CV, "    const int32_t at_y = (int32_t)(from.y\n        + round_div((int64_t)(boundary - from.x) * dy, dx));", "    const int32_t at_y = (int32_t)(to.y\n        + round_div((int64_t)(boundary - from.x) * dy, dx));", K),
    ("scanline-piece-height", "src", CV, "    cell_add(raster, ex, ey, split.y - cursor.y,\n        cursor.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);", "    cell_add(raster, ex, ey, split.y - from.y,\n        cursor.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);", K),
    ("scanline-piece-enter", "src", CV, "    cell_add(raster, ex, ey, split.y - cursor.y,\n        cursor.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);", "    cell_add(raster, ex, ey, split.y - cursor.y,\n        from.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);", K),
    ("scanline-cursor-not-advanced", "src", CV, "        cursor.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);\n    cursor = split;\n  }", "        cursor.x - ex * ONE_PIXEL, split.x - ex * ONE_PIXEL);\n  }", K),
    ("scanline-last-piece", "src", CV, "  cell_add(raster, ex2, ey, to.y - cursor.y,\n      cursor.x - ex2 * ONE_PIXEL, to.x - ex2 * ONE_PIXEL);", "  cell_add(raster, ex2, ey, to.y - from.y,\n      cursor.x - ex2 * ONE_PIXEL, to.x - ex2 * ONE_PIXEL);", K),

    # --- an edge ------------------------------------------------------------------------
    ("line-keeps-horizontal-pen", "src", CV, "  raster->current = to;\n  if (from.y == to.y) {", "  if (from.y == to.y) {", K),
    ("line-horizontal-test", "src", CV, "  if (from.y == to.y) {\n    /* A horizontal edge", "  if (from.y != to.y) {\n    /* A horizontal edge", K),
    ("line-single-row-test", "src", CV, "  if (ey1 == ey2) {\n    edge_scanline(raster, ey1, from, to);", "  if (ey1 != ey2) {\n    edge_scanline(raster, ey1, from, to);", K),
    ("line-step", "src", CV, "  step = dy > 0 ? 1 : -1;\n  cursor = from;", "  step = dy > 0 ? -1 : 1;\n  cursor = from;", K),
    ("line-boundary", "src", CV, "    const int32_t boundary = (step > 0 ? ey + 1 : ey) * ONE_PIXEL;", "    const int32_t boundary = (step > 0 ? ey : ey + 1) * ONE_PIXEL;", K),
    ("line-boundary-x-truncates", "src", CV, "        + round_div((int64_t)(boundary - from.y) * dx, dy));\n    GSHGEOM_Device_Point split;\n\n    split.x = at_x;", "        + ((int64_t)(boundary - from.y) * dx) / dy);\n    GSHGEOM_Device_Point split;\n\n    split.x = at_x;", K),
    ("line-boundary-x-from", "src", CV, "    const int32_t at_x = (int32_t)(from.x\n        + round_div", "    const int32_t at_x = (int32_t)(to.x\n        + round_div", K),
    ("line-cursor-not-advanced", "src", CV, "    edge_scanline(raster, ey, cursor, split);\n    cursor = split;", "    edge_scanline(raster, ey, cursor, split);", K),
    ("line-last-piece", "src", CV, "  edge_scanline(raster, ey2, cursor, to);\n}", "  edge_scanline(raster, ey2, from, to);\n}", K),
    ("sort-rows-alloc-oom", "src", CV, "  if (!rows) {\n    return GSHCPU_ERR_OOM;\n  }\n  if (raster->cell_count == 0) {", "  if (!rows) {\n    return GSHCPU_ERR_LIMIT;\n  }\n  if (raster->cell_count == 0) {", L),
    ("sort-count-offset", "src", CV, "    rows[(size_t)raster->cells[i].y + 1u] += 1;", "    rows[(size_t)raster->cells[i].y] += 1;", K),
    ("sort-prefix-sum", "src", CV, "    rows[y + 1u] += rows[y];", "    rows[y + 1u] += 1;", K),
    ("sort-place-start", "src", CV, "      sorted[cursor[(size_t)raster->cells[i].y]++] = raster->cells[i];", "      sorted[cursor[(size_t)raster->cells[i].y + 1u]++] = raster->cells[i];", K),
    ("sort-no-copy-back", "src", CV, "  memcpy(raster->cells, sorted, raster->cell_count * sizeof *sorted);", "  memcpy(sorted, raster->cells, raster->cell_count * sizeof *sorted);", K),
    ("sort-insertion-descending", "src", CV, "while (j > first && raster->cells[j - 1].x > held.x) {", "while (j > first && raster->cells[j - 1].x < held.x) {", K),
    ("sort-insertion-start", "src", CV, "    for (size_t i = first + 1; i < last; ++i) {\n      Cell held", "    for (size_t i = first + 2; i < last; ++i) {\n      Cell held", K),
    ("sort-merge-cover", "src", CV, "          raster->cells[write - 1].cover += raster->cells[read].cover;", "          raster->cells[write - 1].cover -= raster->cells[read].cover;", K),
    ("sort-merge-area", "src", CV, "          raster->cells[write - 1].area += raster->cells[read].area;", "          raster->cells[write - 1].area -= raster->cells[read].area;", K),
    ("sort-merge-x", "src", CV, "            && raster->cells[write - 1].x == raster->cells[read].x) {", "            && raster->cells[write - 1].x <= raster->cells[read].x) {", K),
    ("sort-no-sentinel", "src", CV, "      for (size_t fill = write; fill < last; ++fill) {", "      for (size_t fill = write; fill < write; ++fill) {", K),
    ("sort-cursor-leak", "src", CV, "    allocator->free_fn(allocator->ctx, cursor);\n  }", "  }", L),
    ("sort-sorted-leak", "src", CV, "  memcpy(raster->cells, sorted, raster->cell_count * sizeof *sorted);\n  allocator->free_fn(allocator->ctx, sorted);", "  memcpy(raster->cells, sorted, raster->cell_count * sizeof *sorted);", L),
    ("sort-malloc-failure-leaks-rows", "src", CV, "  sorted = allocator->malloc_fn(allocator->ctx, bytes);\n  if (!sorted) {\n    allocator->free_fn(allocator->ctx, rows);", "  sorted = allocator->malloc_fn(allocator->ctx, bytes);\n  if (!sorted) {", L),
    ("sort-cursor-failure-leaks-sorted", "src", CV, "    if (!cursor) {\n      allocator->free_fn(allocator->ctx, sorted);", "    if (!cursor) {", L),
    ("sort-cursor-failure-leaks-rows", "src", CV, "      allocator->free_fn(allocator->ctx, sorted);\n      allocator->free_fn(allocator->ctx, rows);\n      return GSHCPU_ERR_OOM;\n    }\n    memcpy(cursor", "      allocator->free_fn(allocator->ctx, sorted);\n      return GSHCPU_ERR_OOM;\n    }\n    memcpy(cursor", L),

    # --- from area to coverage ---------------------------------------------------------------
    ("area-no-abs", "src", CV, "  if (value < 0) {\n    value = -value;\n  }", "  if (value < 0) {\n    value = value;\n  }", K),
    ("area-abs-before-shift", "src", CV, "  int64_t value = area >> AREA_SHIFT;\n\n  if (value < 0) {\n    value = -value;\n  }", "  int64_t value = (area < 0 ? -area : area) >> AREA_SHIFT;", K),
    ("fold-modulus", "src", CV, "    value &= 511;", "    value &= 255;", K),
    ("fold-ramp-knee", "src", CV, "    if (value > 256) {\n      value = 512 - value;", "    if (value >= 256) {\n      value = 512 - value;", K),
    ("fold-ramp-top", "src", CV, "      value = 512 - value;", "      value = 511 - value;", K),
    ("fold-full", "src", CV, "    else if (value == 256) {\n      value = 255;", "    else if (value == 256) {\n      value = 254;", K),
    ("fold-full-removed", "src", CV, "    else if (value == 256) {\n      value = 255;\n    }", "    else if (value == 256) {\n      value = 0;\n    }", K),
    ("saturate-threshold", "src", CV, "  else if (value >= 256) {\n    value = 255;", "  else if (value > 256) {\n    value = 255;", K),
    ("saturate-value", "src", CV, "  else if (value >= 256) {\n    value = 255;", "  else if (value >= 256) {\n    value = 254;", K),
    ("rule-test", "src", CV, "  if (fill == GSHGEOM_FILL_EVEN_ODD) {\n    /* Winding 2", "  if (fill == GSHGEOM_FILL_NONZERO) {\n    /* Winding 2", K),
    ("span-writes-count-minus-one", "src", CV, "  memset(line + from, value, (size_t)count);", "  memset(line + from, value, (size_t)count - 1u);", K),
    ("span-row", "src", CV, "  line = coverage->data + (size_t)row * coverage->stride;\n  memset", "  line = coverage->data + (size_t)row;\n  memset", K),

    # --- the sweep ---------------------------------------------------------------------------------
    ("sweep-gap-skipped", "src", CV, "      if (cover != 0 && cell->x > x) {\n        write_span(coverage, y, x, cell->x - x, cover);\n      }", "      if (0) {\n        write_span(coverage, y, x, cell->x - x, cover);\n      }", K),
    ("sweep-gap-length", "src", CV, "write_span(coverage, y, x, cell->x - x, cover);", "write_span(coverage, y, x, cell->x - x - 1, cover);", K),
    ("sweep-cover-scale", "src", CV, "      cover += cell->cover * (ONE_PIXEL * 2);", "      cover += cell->cover * ONE_PIXEL;", K),
    ("sweep-cell-value", "src", CV, "      write_span(coverage, y, cell->x, 1, cover - cell->area);", "      write_span(coverage, y, cell->x, 1, cover + cell->area);", K),
    ("sweep-cell-without-area", "src", CV, "      write_span(coverage, y, cell->x, 1, cover - cell->area);", "      write_span(coverage, y, cell->x, 1, cover);", K),
    ("sweep-next-x", "src", CV, "      x = cell->x + 1;", "      x = cell->x;", K),
    ("sweep-sentinel-test", "src", CV, "      if (cell->x == INT32_MAX) {\n        break;\n      }", "      if (cell->x == 0) {\n        break;\n      }", K + L),
    ("trim-first-row", "src", CV, "      if (row < first_row) {\n        first_row = row;", "      if (row > first_row) {\n        first_row = row;", K),
    ("trim-last-row", "src", CV, "      last_row = row;", "      last_row = 0;", K),
    ("trim-first-column", "src", CV, "      if (column < first_column) {", "      if (column > first_column) {", K),
    ("trim-last-column", "src", CV, "      if (column > last_column) {", "      if (column < last_column) {", K),
    ("trim-width", "src", CV, "  width = last_column - first_column + 1u;", "  width = last_column - first_column;", K),
    ("trim-height", "src", CV, "  height = last_row - first_row + 1u;", "  height = last_row - first_row;", K),
    ("trim-row-source", "src", CV, "        coverage->data + (size_t)(row + first_row) * coverage->stride\n            + first_column,", "        coverage->data + (size_t)(row + first_row) * coverage->stride,", K),
    ("trim-row-destination", "src", CV, "    memmove(coverage->data + (size_t)row * width,", "    memmove(coverage->data + (size_t)row * coverage->stride,", K),
    ("trim-left", "src", CV, "  coverage->left += (int32_t)first_column;", "  coverage->left -= (int32_t)first_column;", K),
    ("trim-top", "src", CV, "  coverage->top += (int32_t)first_row;", "  coverage->top -= (int32_t)first_row;", K),
    ("trim-stride", "src", CV, "  coverage->stride = width;\n}", "}", K),
    ("trim-empty-keeps-data", "src", CV, "    allocator->free_fn(allocator->ctx, coverage->data);\n    coverage->data = NULL;", "    (void)allocator;\n    coverage->data = NULL;", L),
    ("trim-empty-keeps-width", "src", CV, "    coverage->width = 0;\n    coverage->height = 0;\n    coverage->stride = 0;", "    coverage->height = 0;\n    coverage->stride = 0;", K),
    ("trim-empty-keeps-height", "src", CV, "    coverage->height = 0;\n    coverage->stride = 0;\n    coverage->left = 0;", "    coverage->stride = 0;\n    coverage->left = 0;", K),
    ("trim-empty-keeps-stride", "src", CV, "    coverage->stride = 0;\n    coverage->left = 0;", "    coverage->left = 0;", K),
    ("trim-empty-keeps-left", "src", CV, "    coverage->left = 0;\n    coverage->top = 0;\n    return;", "    coverage->top = 0;\n    return;", K),
    ("trim-empty-keeps-top", "src", CV, "    coverage->top = 0;\n    return;", "    return;", K),
    ("trim-no-data", "src", CV, "  if (!coverage || !coverage->data) {\n    return;\n  }\n  first_row", "  if (!coverage) {\n    return;\n  }\n  first_row", K),

    # --- the empty coverage -------------------------------------------------------------------------------
    ("empty-fill", "src", CV, "  memset(out, 0, sizeof *out);\n  out->fill = fill;", "  memset(out, 0, sizeof *out);\n  (void)fill;", K),
    ("empty-allocator", "src", CV, "  out->allocator = allocator;\n}", "  (void)allocator;\n}", K),

    # --- the fill into a rectangle ---------------------------------------------------------------------------
    ("default-max-dimension", "src", CV, "      ? limits->max_dimension : GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;", "      ? limits->max_dimension : GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION * 2;", L),
    ("default-max-cells", "src", CV, "      ? limits->max_cells : GSHCPU_LIMITS_DEFAULT_MAX_CELLS;", "      ? limits->max_cells : 0;", KL),
    ("default-max-bytes", "src", CV, "      ? limits->max_bytes : GSHCPU_LIMITS_DEFAULT_MAX_BYTES;", "      ? limits->max_bytes : 0;", KL),
    ("ceiling-clamp", "src", CV, "  if (max_dimension > GSHCPU_MAX_DIMENSION_CEILING) {\n    max_dimension = GSHCPU_MAX_DIMENSION_CEILING;\n  }", "", L),
    ("dimension-both-needed", "src", CV, "if (width > max_dimension || height > max_dimension) {", "if (width > max_dimension && height > max_dimension) {", L),
    ("dimension-inclusive", "src", CV, "if (width > max_dimension || height > max_dimension) {", "if (width >= max_dimension || height >= max_dimension) {", L),
    ("dimension-width-only", "src", CV, "if (width > max_dimension || height > max_dimension) {", "if (width > max_dimension) {", L),
    ("dimension-height-only", "src", CV, "if (width > max_dimension || height > max_dimension) {", "if (height > max_dimension) {", L),
    ("empty-needs-both-zero", "src", CV, "  if (width == 0 || height == 0) {", "  if (width == 0 && height == 0) {", K),
    ("bytes-inclusive", "src", CV, "if (!mul_size(width, height, &bytes) || bytes > max_bytes) {", "if (!mul_size(width, height, &bytes) || bytes >= max_bytes) {", L),
    ("bytes-ignored", "src", CV, "if (!mul_size(width, height, &bytes) || bytes > max_bytes) {", "if (!mul_size(width, height, &bytes) || (bytes > max_bytes && 0)) {", L),
    ("point-x-before", "src", CV, "    if (x < 0 || y < 0 || x >= (int64_t)width * ONE_PIXEL", "    if (x < -1 || y < 0 || x >= (int64_t)width * ONE_PIXEL", K + L),
    ("point-y-before", "src", CV, "    if (x < 0 || y < 0 || x >= (int64_t)width * ONE_PIXEL", "    if (x < 0 || y < -1 || x >= (int64_t)width * ONE_PIXEL", K + L),
    ("point-x-after", "src", CV, "x >= (int64_t)width * ONE_PIXEL\n        || y >= (int64_t)height * ONE_PIXEL) {", "x > (int64_t)width * ONE_PIXEL\n        || y >= (int64_t)height * ONE_PIXEL) {", K),
    ("point-y-after", "src", CV, "x >= (int64_t)width * ONE_PIXEL\n        || y >= (int64_t)height * ONE_PIXEL) {", "x >= (int64_t)width * ONE_PIXEL\n        || y > (int64_t)height * ONE_PIXEL) {", K),
    ("point-check-ignored", "src", CV, "    if (x < 0 || y < 0 || x >= (int64_t)width * ONE_PIXEL\n        || y >= (int64_t)height * ONE_PIXEL) {\n      return GSHCPU_ERR_INVALID;\n    }", "    if ((x < 0 || y < 0 || x >= (int64_t)width * ONE_PIXEL\n        || y >= (int64_t)height * ONE_PIXEL) && 0) {\n      return GSHCPU_ERR_INVALID;\n    }", K),
    ("origin-x-pixels", "src", CV, "  const int64_t origin_x = (int64_t)left * ONE_PIXEL;", "  const int64_t origin_x = (int64_t)left;", K + L),
    ("origin-y-pixels", "src", CV, "  const int64_t origin_y = (int64_t)top * ONE_PIXEL;", "  const int64_t origin_y = (int64_t)top;", K + L),
    ("coverage-left", "src", CV, "  coverage.left = left;\n  coverage.top = top;", "  coverage.top = top;", K),
    ("coverage-top", "src", CV, "  coverage.left = left;\n  coverage.top = top;", "  coverage.left = left;", K),
    ("coverage-stride", "src", CV, "  coverage.stride = width;", "  coverage.stride = width + 1;", K),
    ("coverage-fill", "src", CV, "  coverage.fill = fill;\n  coverage.width", "  coverage.width", K),
    ("coverage-allocator", "src", CV, "  coverage.allocator = allocator;\n  coverage.fill", "  coverage.fill", K),
    ("coverage-alloc-oom", "src", CV, "  coverage.data = allocator->calloc_fn(allocator->ctx, bytes, 1u);\n  if (!coverage.data) {\n    return GSHCPU_ERR_OOM;", "  coverage.data = allocator->calloc_fn(allocator->ctx, bytes, 1u);\n  if (!coverage.data) {\n    return GSHCPU_ERR_INVALID;", L),
    ("subpath-loop-short", "src", CV, "for (size_t s = 0; s < subpath_count && raster.result == GSHCPU_OK; ++s) {", "for (size_t s = 0; s + 1 < subpath_count && raster.result == GSHCPU_OK; ++s) {", K),
    ("subpath-first-skipped", "src", CV, "    for (size_t k = 1; k < sub.count && raster.result == GSHCPU_OK; ++k) {", "    for (size_t k = 2; k < sub.count && raster.result == GSHCPU_OK; ++k) {", K),
    ("subpath-last-skipped", "src", CV, "    for (size_t k = 1; k < sub.count && raster.result == GSHCPU_OK; ++k) {", "    for (size_t k = 1; k + 1 < sub.count && raster.result == GSHCPU_OK; ++k) {", K),
    ("subpath-start-y", "src", CV, "    start.y = (int32_t)((int64_t)points[sub.first].y - origin_y);", "    start.y = (int32_t)((int64_t)points[sub.first].y);", K),
    ("subpath-start-x", "src", CV, "    start.x = (int32_t)((int64_t)points[sub.first].x - origin_x);", "    start.x = (int32_t)((int64_t)points[sub.first].x);", K),
    ("subpath-point-x", "src", CV, "      to.x = (int32_t)((int64_t)points[sub.first + k].x - origin_x);", "      to.x = (int32_t)((int64_t)points[sub.first + k].x);", K),
    ("subpath-point-y", "src", CV, "      to.y = (int32_t)((int64_t)points[sub.first + k].y - origin_y);", "      to.y = (int32_t)((int64_t)points[sub.first + k].y);", K),
    ("subpath-point-index", "src", CV, "points[sub.first + k].x - origin_x", "points[sub.first].x - origin_x", K),
    ("subpath-not-closed", "src", CV, "    if (raster.result == GSHCPU_OK) {\n      edge_line(&raster, start);\n    }", "    if (0) {\n      edge_line(&raster, start);\n    }", K),
    ("subpath-closed-only", "src", CV, "    if (raster.result == GSHCPU_OK) {\n      edge_line(&raster, start);\n    }", "    if (raster.result == GSHCPU_OK && sub.closed) {\n      edge_line(&raster, start);\n    }", K),
    ("subpath-pen", "src", CV, "    raster.current = start;\n", "", K),
    ("rows-leak", "src", CV, "  allocator->free_fn(allocator->ctx, rows);\n  allocator->free_fn(allocator->ctx, raster.cells);", "  allocator->free_fn(allocator->ctx, raster.cells);", L),
    ("cells-leak", "src", CV, "  allocator->free_fn(allocator->ctx, rows);\n  allocator->free_fn(allocator->ctx, raster.cells);", "  allocator->free_fn(allocator->ctx, rows);", L),
    ("failure-leaks-pixels", "src", CV, "  if (result != GSHCPU_OK) {\n    allocator->free_fn(allocator->ctx, coverage.data);\n    return result;", "  if (result != GSHCPU_OK) {\n    return result;", L),
    ("failure-writes-output", "src", CV, "    allocator->free_fn(allocator->ctx, coverage.data);\n    return result;\n  }\n  *out = coverage;", "    allocator->free_fn(allocator->ctx, coverage.data);\n    *out = coverage;\n    return result;\n  }\n  *out = coverage;", L),
    ("sort-error-ignored", "src", CV, "  if (result == GSHCPU_OK) {\n    result = sort_cells(&raster, &rows);\n  }", "  if (result == GSHCPU_OK) {\n    sort_cells(&raster, &rows);\n  }", L),

    # --- the flattening and the entry points ---------------------------------------------------------------------
    ("flatten-ignores-point-cap", "src", CV, "  if (limits && limits->max_points) {\n    geometry.max_points = limits->max_points;\n  }", "  (void)limits;", L),
    ("fill-rule-bound", "src", CV, "GSHCPU_Result gshcpu_coverage_fill(const GSHGEOM_Device_Path * path,\n    GSHGEOM_Fill_Rule fill, int32_t left, int32_t top, uint32_t width,\n    uint32_t height, const GSHCPU_Limits * limits,\n    const GSHCPU_Allocator * allocator, GSHCPU_Coverage * out) {\n  GSHGEOM_Flat_Path * flat = NULL;\n  GSHCPU_Result result;\n\n  if (!path || !out || (unsigned)fill >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {", "GSHCPU_Result gshcpu_coverage_fill(const GSHGEOM_Device_Path * path,\n    GSHGEOM_Fill_Rule fill, int32_t left, int32_t top, uint32_t width,\n    uint32_t height, const GSHCPU_Limits * limits,\n    const GSHCPU_Allocator * allocator, GSHCPU_Coverage * out) {\n  GSHGEOM_Flat_Path * flat = NULL;\n  GSHCPU_Result result;\n\n  if (!path || !out || (unsigned)fill > (unsigned)GSHGEOM_FILL_RULE_COUNT) {", K),
    ("from-path-rule-bound", "src", CV, "  GSHCPU_Result result;\n\n  if (!path || !out || (unsigned)fill >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (!allocator) {\n    allocator = gshgeom_allocator_default();\n  }\n  result = flatten(path, limits, allocator, &flat);\n  if (result != GSHCPU_OK) {\n    return result;\n  }\n  points", "  GSHCPU_Result result;\n\n  if (!path || !out) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (!allocator) {\n    allocator = gshgeom_allocator_default();\n  }\n  result = flatten(path, limits, allocator, &flat);\n  if (result != GSHCPU_OK) {\n    return result;\n  }\n  points", K),
    ("fill-no-out-check", "src", CV, "  if (!path || !out || (unsigned)fill >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (!allocator) {\n    allocator = gshgeom_allocator_default();\n  }\n  result = flatten(path, limits, allocator, &flat);\n  if (result != GSHCPU_OK) {\n    return result;\n  }\n  result = fill_flat(", "  if (!path || (unsigned)fill >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (!allocator) {\n    allocator = gshgeom_allocator_default();\n  }\n  result = flatten(path, limits, allocator, &flat);\n  if (result != GSHCPU_OK) {\n    return result;\n  }\n  result = fill_flat(", K),
    ("fill-flat-leaks", "src", CV, "  result = fill_flat(flat, fill, left, top, width, height, limits, allocator,\n      out);\n  gshgeom_flat_path_destroy(flat);", "  result = fill_flat(flat, fill, left, top, width, height, limits, allocator,\n      out);", L),
    ("from-path-leaks-flat", "src", CV, "  result = fill_flat(flat, fill, (int32_t)left, (int32_t)top, (size_t)width,\n      (size_t)height, limits, allocator, out);\n  gshgeom_flat_path_destroy(flat);", "  result = fill_flat(flat, fill, (int32_t)left, (int32_t)top, (size_t)width,\n      (size_t)height, limits, allocator, out);", L),
    ("from-path-empty-leaks-flat", "src", CV, "  if (point_count == 0) {\n    gshgeom_flat_path_destroy(flat);", "  if (point_count == 0) {", L),
    ("from-path-empty-test", "src", CV, "  if (point_count == 0) {\n    gshgeom_flat_path_destroy(flat);", "  if (point_count == 1) {\n    gshgeom_flat_path_destroy(flat);", K),
    ("from-path-no-trim", "src", CV, "  if (result == GSHCPU_OK) {\n    gshcpu_coverage_trim(out);\n  }", "", K),
    ("extent-x-min", "src", CV, "    if (points[i].x < x_min) {", "    if (points[i].x > x_min) {", K),
    ("extent-x-max", "src", CV, "    if (points[i].x > x_max) {", "    if (points[i].x < x_max) {", K),
    ("extent-y-min", "src", CV, "    if (points[i].y < y_min) {", "    if (points[i].y > y_min) {", K),
    ("extent-y-max", "src", CV, "    if (points[i].y > y_max) {", "    if (points[i].y < y_max) {", K),
    ("extent-first-point", "src", CV, "  x_min = x_max = points[0].x;\n  y_min = y_max = points[0].y;", "  x_min = x_max = 0;\n  y_min = y_max = 0;", K),
    ("extent-left-margin", "src", CV, "  left = floor_div(x_min, ONE_PIXEL) - 1;", "  left = floor_div(x_min, ONE_PIXEL);", K + L),
    ("extent-top-margin", "src", CV, "  top = floor_div(y_min, ONE_PIXEL) - 1;", "  top = floor_div(y_min, ONE_PIXEL);", K + L),
    ("extent-right-margin", "src", CV, "  width = ceil_div(x_max, ONE_PIXEL) + 1 - left;", "  width = ceil_div(x_max, ONE_PIXEL) - left;", K + L),
    ("extent-bottom-margin", "src", CV, "  height = ceil_div(y_max, ONE_PIXEL) + 1 - top;", "  height = ceil_div(y_max, ONE_PIXEL) - top;", K + L),
    ("extent-left-floor", "src", CV, "  left = floor_div(x_min, ONE_PIXEL) - 1;", "  left = (x_min / ONE_PIXEL) - 1;", K + L),
    ("extent-right-ceil", "src", CV, "  width = ceil_div(x_max, ONE_PIXEL) + 1 - left;", "  width = floor_div(x_max, ONE_PIXEL) + 1 - left;", K + L),
    ("extent-bottom-ceil", "src", CV, "  height = ceil_div(y_max, ONE_PIXEL) + 1 - top;", "  height = floor_div(y_max, ONE_PIXEL) + 1 - top;", K + L),

    # --- what can be asked of a coverage --------------------------------------------------------------------------------
    ("destroy-no-free", "src", RP, "    coverage->allocator->free_fn(coverage->allocator->ctx, coverage->data);", "    (void)coverage;", L),
    ("destroy-no-zero", "src", RP, "  memset(coverage, 0, sizeof *coverage);", "  coverage->data = NULL;", R),
    ("at-column-bound", "src", RP, "column >= coverage->width", "column > coverage->width", R),
    ("at-row-bound", "src", RP, "row >= coverage->height) {\n    return 0;", "row > coverage->height) {\n    return 0;", R),
    ("at-stride", "src", RP, "  return coverage->data[(size_t)row * coverage->stride + column];", "  return coverage->data[(size_t)row * coverage->width + column];", R),
    ("total-stride", "src", RP, "    const uint8_t * line = coverage->data + (size_t)row * coverage->stride;\n\n    for (uint32_t column = 0; column < coverage->width; ++column) {\n      total += line[column];", "    const uint8_t * line = coverage->data + (size_t)row * coverage->width;\n\n    for (uint32_t column = 0; column < coverage->width; ++column) {\n      total += line[column];", R),
    ("total-last-column", "src", RP, "    for (uint32_t column = 0; column < coverage->width; ++column) {\n      total += line[column];", "    for (uint32_t column = 0; column + 1 < coverage->width; ++column) {\n      total += line[column];", R),
    ("hash-version", "src", RP, "#define COVERAGE_FORMAT_VERSION 1u", "#define COVERAGE_FORMAT_VERSION 2u", R),
    ("hash-kind", "src", RP, "#define COVERAGE_KIND 0x52564f43u", "#define COVERAGE_KIND 0x52564f44u", R),
    ("hash-no-version", "src", RP, "  gshgeom_hash_u32(&hash, COVERAGE_FORMAT_VERSION);\n", "", R),
    ("hash-no-kind", "src", RP, "  gshgeom_hash_u32(&hash, COVERAGE_KIND);\n", "", R),
    ("hash-no-width", "src", RP, "  gshgeom_hash_u32(&hash, coverage->width);\n", "", R),
    ("hash-no-height", "src", RP, "  gshgeom_hash_u32(&hash, coverage->height);\n", "", R),
    ("hash-no-left", "src", RP, "  gshgeom_hash_i32(&hash, coverage->left);\n", "", R),
    ("hash-no-top", "src", RP, "  gshgeom_hash_i32(&hash, coverage->top);\n", "", R),
    ("hash-no-fill", "src", RP, "  gshgeom_hash_u32(&hash, (uint32_t)coverage->fill);\n", "", R),
    ("hash-left-unsigned", "src", RP, "  gshgeom_hash_i32(&hash, coverage->left);", "  gshgeom_hash_u32(&hash, coverage->left < 0 ? 0u : (uint32_t)coverage->left);", R),
    ("hash-top-unsigned", "src", RP, "  gshgeom_hash_i32(&hash, coverage->top);", "  gshgeom_hash_u32(&hash, coverage->top < 0 ? 0u : (uint32_t)coverage->top);", R),
    ("hash-swapped-width-height", "src", RP, "  gshgeom_hash_u32(&hash, coverage->width);\n  gshgeom_hash_u32(&hash, coverage->height);", "  gshgeom_hash_u32(&hash, coverage->height);\n  gshgeom_hash_u32(&hash, coverage->width);", R),
    ("hash-swapped-left-top", "src", RP, "  gshgeom_hash_i32(&hash, coverage->left);\n  gshgeom_hash_i32(&hash, coverage->top);", "  gshgeom_hash_i32(&hash, coverage->top);\n  gshgeom_hash_i32(&hash, coverage->left);", R),
    ("hash-rows-stride", "src", RP, "          coverage->data + (size_t)row * coverage->stride, coverage->width);", "          coverage->data + (size_t)row * coverage->width, coverage->width);", R),
    ("hash-rows-with-padding", "src", RP, "          coverage->data + (size_t)row * coverage->stride, coverage->width);", "          coverage->data + (size_t)row * coverage->stride, coverage->stride);", R),
    ("hash-last-row", "src", RP, "    for (uint32_t row = 0; row < coverage->height; ++row) {\n      gshgeom_hash_bytes", "    for (uint32_t row = 0; row + 1 < coverage->height; ++row) {\n      gshgeom_hash_bytes", R),
    ("hash-null-is-zero", "src", RP, "  if (!coverage) {\n    return gshgeom_hash_value(&hash);\n  }", "  if (!coverage) {\n    return 0;\n  }", R),
    ("dump-null-stream", "src", RP, "  if (!coverage || !out) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (fprintf", "  if (!coverage) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (fprintf", R),
    ("dump-null-coverage", "src", RP, "  if (!coverage || !out) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (fprintf", "  if (!out) {\n    return GSHCPU_ERR_INVALID;\n  }\n  if (fprintf", R),
    ("dump-name", "src", RP, '"gshcpu.coverage %u', '"gshcpu.coverages %u', R),
    ("dump-position-text", "src", RP, "%ux%u at %d,%d fill", "%ux%u at %d %d fill", R),
    ("dump-size-order", "src", RP, "COVERAGE_FORMAT_VERSION, coverage->width, coverage->height,", "COVERAGE_FORMAT_VERSION, coverage->height, coverage->width,", R),
    ("dump-position-order", "src", RP, "          coverage->left, coverage->top,\n", "          coverage->top, coverage->left,\n", R),
    ("dump-fill-name", "src", RP, "          gshgeom_fill_rule_string(coverage->fill),", '          "non-zero",', R),
    ("dump-total", "src", RP, "(unsigned long long)gshcpu_coverage_total(coverage),", "(unsigned long long)0,", R),
    ("dump-hash-width", "src", RP, "hash=%016llx", "hash=%llx", R),
    ("dump-write-error", "src", RP, "      < 0) {\n    return GSHCPU_ERR_IO;\n  }\n  return fflush", "      < 0) {\n    return GSHCPU_OK;\n  }\n  return fflush", R),
    ("dump-flush-error", "src", RP, "  return fflush(out) == 0 ? GSHCPU_OK : GSHCPU_ERR_IO;", "  return GSHCPU_OK;", R),

    # --- the results and the limits ---------------------------------------------------------------------------------------
    ("result-string-ok", "src", CO, '    return "ok";', '    return "fine";', C + R),
    ("result-string-limit", "src", CO, '    return "a limit was exceeded";', '    return "limit";', C + R),
    ("result-string-oom", "src", CO, '    return "out of memory";', '    return "oom";', C + R),
    ("result-string-invalid", "src", CO, '    return "invalid argument";', '    return "invalid";', C + R),
    ("result-string-unknown", "src", CO, '  return "unknown result";', '  return "unknown";', C + R),
    ("result-string-count-names-itself", "src", CO, "  case GSHCPU_RESULT_COUNT:\n    break;", "  case GSHCPU_RESULT_COUNT:\n    return \"ok\";", C + R),
    ("limits-default-dimension", "src", CO, "  limits->max_dimension = GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;", "  limits->max_dimension = 0;", L),
    ("limits-default-bytes", "src", CO, "  limits->max_bytes = GSHCPU_LIMITS_DEFAULT_MAX_BYTES;", "  limits->max_bytes = 0;", L),
    ("limits-default-cells", "src", CO, "  limits->max_cells = GSHCPU_LIMITS_DEFAULT_MAX_CELLS;", "  limits->max_cells = 0;", L),
    ("limits-default-points", "src", CO, "  limits->max_points = 0;", "  limits->max_points = 1;", L),
    ("limits-default-reserved", "src", CO, "    limits->_reserved[i] = 0;", "    limits->_reserved[i] = 1;", L),
    ("limits-default-null", "src", CO, "void gshcpu_limits_default(GSHCPU_Limits * limits) {\n  if (!limits) {\n    return;\n  }", "void gshcpu_limits_default(GSHCPU_Limits * limits) {", L),
    ("header-default-dimension", "hdr", CH, "#define GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION ((size_t)16384)", "#define GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION ((size_t)16385)", L),
    ("header-default-bytes", "hdr", CH, "#define GSHCPU_LIMITS_DEFAULT_MAX_BYTES ((size_t)256 * 1024 * 1024)", "#define GSHCPU_LIMITS_DEFAULT_MAX_BYTES ((size_t)128 * 1024 * 1024)", L),
    ("header-default-cells", "hdr", CH, "#define GSHCPU_LIMITS_DEFAULT_MAX_CELLS ((size_t)1 << 22)", "#define GSHCPU_LIMITS_DEFAULT_MAX_CELLS ((size_t)1 << 21)", L),
    ("header-ceiling", "hdr", CH, "#define GSHCPU_MAX_DIMENSION_CEILING ((size_t)1 << 24)", "#define GSHCPU_MAX_DIMENSION_CEILING ((size_t)1 << 25)", L),
    ("header-result-order", "hdr", CH, "  GSHCPU_ERR_IO,          ///< A write to a caller's stream failed.\n  GSHCPU_ERR_FORMAT,      ///< Not returned by this library.", "  GSHCPU_ERR_FORMAT,      ///< Not returned by this library.\n  GSHCPU_ERR_IO,          ///< A write to a caller's stream failed.", C + R),
]

def run(cmd, shell=False, env_extra=None, cwd=None):
    environment = dict(os.environ)
    if env_extra:
        environment.update(env_extra)
    return subprocess.run(cmd, shell=shell, capture_output=True, text=True,
                          env=environment, cwd=cwd or ROOT)


def compile_all(destdir, srcs, extra=(), pre=()):
    """Compile every source to destdir/<name>.o; return the object list."""
    destdir.mkdir(parents=True, exist_ok=True)
    objs = []
    for src in srcs:
        obj = destdir / (src.replace("/", "_") + ".o")
        r = run(CC + list(pre) + CFLAGS + list(extra) + ["-c", src, "-o", str(obj)])
        if r.returncode != 0:
            raise RuntimeError("cannot compile %s:\n%s" % (src, r.stderr))
        objs.append(str(obj))
    return objs


def link_test(test, objs, out):
    cmd = "%s -o %s %s %s %s" % (CXX, out, test_object(test), " ".join(objs), LDFLAGS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot link %s:\n%s" % (test, r.stderr))


def _limits():
    """A planted defect may loop or allocate without end; bound both."""
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (3 << 30, 3 << 30))
    resource.setrlimit(resource.RLIMIT_CPU, (120, 120))


def run_test(exe):
    environment = dict(os.environ)
    environment["LD_LIBRARY_PATH"] = LD_PATH
    try:
        r = subprocess.run([str(exe), "--gtest_brief=1"], capture_output=True, text=True,
                           env=environment, cwd=ROOT, timeout=180, preexec_fn=_limits)
    except subprocess.TimeoutExpired:
        return 1, ["[  FAILED  ] (timed out)"]
    failing = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith("[  FAILED  ]")]
    return r.returncode, failing


def probe_hash(objs, d, extra=()):
    exe = d / "corpus-hash"
    cmd = "%s -o %s tools/corpus-hash.c %s %s" % (
        " ".join(CC + PROBE_CFLAGS + list(extra)), exe, " ".join(objs), PROBE_LIBS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot build the probe:\n%s" % r.stderr)
    c = run(["tools/check-corpus-hash.sh", str(exe), "tests/corpus/coverage.hash"],
            env_extra={"LD_LIBRARY_PATH": LD_PATH})
    return c.returncode, (c.stdout + c.stderr).strip()


def apply(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise RuntimeError("plant %s: the text to replace occurs %d times, not once:\n%s" % (label, n, old))
    if old == new:
        raise RuntimeError("plant %s: the replacement changes nothing" % label)
    return text.replace(old, new)


def do_plant(plant, control_objs):
    pid, kind, fname, old, new, tests = plant
    d = WORK / pid
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    srcs = sources()
    if kind == "src":
        text = apply((ROOT / fname).read_text(), old, new, pid)
        planted = d / "planted" / fname
        planted.parent.mkdir(parents=True, exist_ok=True)
        # A source may include a neighbour by a relative path ("../exact/exact.h"),
        # so the planted file sits in a copy of the whole tree.
        shutil.copytree(ROOT / "src", d / "planted" / "src", dirs_exist_ok=True)
        planted.write_text(text)
        obj = d / "planted.o"
        r = run(CC + CFLAGS + ["-c", str(planted), "-o", str(obj)])
        if r.returncode != 0:
            raise RuntimeError("plant %s does not compile:\n%s" % (pid, r.stderr))
        base = str(ROOT / fname)
        objs = [str(obj) if o.endswith(fname.replace("/", "_") + ".o") else o for o in control_objs]
        assert str(obj) in objs, pid
    else:
        text = apply((ROOT / fname).read_text(), old, new, pid)
        hdr = d / "include" / Path(fname).relative_to("include")
        shutil.copytree(ROOT / "include", d / "include")
        hdr.write_text(text)
        objs = compile_all(d / "obj", srcs, pre=["-I" + str(d / "include")])
    caught_by = []
    for test in tests:
        exe = d / test
        link_test(test, objs, exe)
        rc, failing = run_test(exe)
        if rc != 0:
            caught_by.append((test, failing[:1]))
    return pid, caught_by


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    srcs = sources()
    failures = 0

    # The control: everything passes.
    control = compile_all(WORK / "control", srcs)
    for test in TESTS:
        exe = WORK / ("control-" + test)
        link_test(test, control, exe)
        rc, failing = run_test(exe)
        if rc != 0:
            print("FAIL  control: %s fails on the unplanted build: %s" % (test, failing))
            return 1
    rc, msg = probe_hash(control, WORK)
    if rc != 0:
        print("FAIL  control: the probe does not print the committed hash: %s" % msg)
        return 1
    print("ok    control: %d tests and the probe pass on the unplanted build" % len(TESTS))

    ids = [p[0] for p in PLANTS]
    if len(set(ids)) != len(ids):
        print("FAIL  duplicate plant ids")
        return 1
    plants = [p for p in PLANTS if not ONLY or re.search(ONLY, p[0])]
    if PROBE_ONLY:
        plants = []
    if not plants and not PROBE_ONLY:
        print("FAIL  PLANT_ONLY matches no plant")
        return 1

    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        futures = {pool.submit(do_plant, p, control): p for p in plants}
        results = {}
        for fut in concurrent.futures.as_completed(futures):
            p = futures[fut]
            try:
                results[p[0]] = fut.result()[1]
            except Exception as e:  # a plant that cannot be built is a defect of the harness
                results[p[0]] = e
    for p in plants:
        r = results[p[0]]
        if isinstance(r, Exception):
            print("FAIL  %-34s %s" % (p[0], r))
            failures += 1
        elif not r:
            print("FAIL  %-34s not caught by %s" % (p[0], " ".join(p[5])))
            failures += 1
        else:
            print("ok    %-34s caught by %s" % (p[0], ", ".join("%s (%s)" % (t, (f[0][14:70] if f else "crash")) for t, f in r)))

    if ONLY and not PROBE_ONLY:
        print("%d plants run (PLANT_ONLY=%s), %d not caught" % (len(plants), ONLY, failures))
        return 1 if failures else 0

    # The probe's own plants.
    (WORK / "bit").mkdir(parents=True, exist_ok=True)
    for name, flag in (("probe-planted-bit", "-DSHAPE_CPU_PLANT_BIT"),):
        (WORK / name).mkdir(parents=True, exist_ok=True)
        rc, msg = probe_hash(control, WORK / name, extra=[flag])
        if rc == 0:
            print("FAIL  %s: the probe printed the committed hash with a bit flipped" % name)
            failures += 1
        else:
            print("ok    %s: %s" % (name, msg.splitlines()[-1]))

    print("%d plants, %d not caught" % (len(PLANTS) + 1, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
