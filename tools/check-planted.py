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

Story 13b, the sink (src/render):

  r-          by hand: every constant of S11's arithmetic (the sRGB curve's five,
              the white point, a value of each gamut and the row each gamut
              takes), the gamma's exponent and sign, the matrix's construction,
              order and use, each factor of each Porter-Duff operator, PLUS's
              clamp, apply's two weights, the operators that are bounded, the
              D8 weight's rounding and divisor, the stores' scale, half and top,
              the loads, the alpha forms, the scan converter's constants, cuts,
              fold column, cell cap and closing edge, the coordinate range, the
              geometry's maps and box, the arena's alignment, capacity and copy,
              the hash's version, kind and format codes, the work bound, the
              region's limits, the paint's and the group's spaces, opacity and
              clips, the weight of a fill, the touched map, the clips' memory,
              what a render makes ready (the whole region every time, the root
              not read, a group read where it was never made ready)
  rule-S*     a violation of each rule of the sink contract a sink can break,
              each caught by shape-scene's conformance suite unchanged
              (testRender_conformance) except where the suite cannot look: S1
              (a job that never completes), S2 (caps that leave out a kind), S3
              (a failure that does not release what it took), S4 (damage
              ignored), S5 (an entry keyed by the wrong chain), S6 (a planned
              refusal drawn), S7 (the check skipped; the prefix reported done
              inside a group; cells uncounted), S9 (memory taken anyway; the need
              short of the items; an arena shorter than its workspace), S10 (a
              reference kept), S11 (source-over's factor), S14 (affinity the caps
              do not state), S15 (readback from the wrong place) and S16 (the
              target's map composed first).  S8 (the conflation flag) and S13
              (non-blocking completion) are the contract layer's: a sink cannot
              break them, and its caps cannot misstate them in a way the layer
              does not refuse.  S12 (output independent of the allocator and the
              workspace's address) has no plant: nothing in the sink reads an
              address, and a plant that made it would be the test, not the sink
  rc- rv- rg- generated (tools/gen-render-plants.py; tools/render-plants.json
  ra- rh- rs- holds the ones caught): every comparison, logical operator,
              condition, statement, hash width, printed width, output letter and
              returned status of src/render's compose, cover, geometry, arena,
              hash and sink, mutated once

Of the 962 generated candidates on story 13b's final tree, 192 did not compile
and were dropped, 681 were caught (tools/render-plants.json), and 89 could not
be seen by any test.  Story 14a changed geometry.c and sink.c and swept their
550 candidates again on its final tree: 91 do not compile, 420 are caught (in
tools/render-plants.json beside the other four files' 329, unchanged), and 39
survive.  The empty-frame fix (2026-10-20: layers made ready as the units reach
them) changed sink.c and swept its 444 candidates again: 81 do not compile, 331
are caught (they replace sink.c's in tools/render-plants.json), and 32 survived;
one of those, a group's pop visiting one row past its region, is caught since
by a test of its own.  Story 14c changed sink.c (a fill's color is shape-scene's
one resolution of its paint, and the disposition reads the list's palette) and
swept its 440 candidates again: 80 do not compile, 329 are caught (they replace
sink.c's in tools/render-plants.json) and 31 survive, the same 31 as before by
kind and none on the story's lines.  Story 16's two decisions (2026-10-21: the
cell cap met over the whole render's region whatever the damage, and a short
caller-memory workspace refused without measuring it) changed cover.c and
sink.c and swept their 677 candidates again: 123 do not compile, 478 are caught
(they replace cover.c's and sink.c's in tools/render-plants.json), and 76
survive: sink.c's 31, the same as before by kind, and cover.c's 45, the old
converter's by the kinds below and twelve on the new lines (ties in the cell
bound's row clamp, its absolute span, its minimum and the held sum at exactly
UINT64_MAX; the bound's subpath loop and read, as the converter's; zero rows
let through to a count of nothing; and count_rows' two NULL stores, which the
zeroed Cover already holds, since it runs before any band).  The first sweep
of those lines left fourteen mutants no test caught (a crossing just right of
the walk counted, the bound's clamps, minimum, last edge and early stop, the
shortcut's guard and its cap test, the clips' reach in the whole render, a
workspace of exactly the block); each is caught by a test since.

Color-paint story P2 (gradients) changed sink.c and added gradient.c: sink.c's
526 candidates were swept again on its tree in ghoti-build:gcc16, 93 do not
compile, 389 are caught (they replace sink.c's in tools/render-plants.json) and
44 survive, the 31 of before by kind and, on the story's lines, the gradient
table's sizing guards (an overflow no list of the store's limits reaches, a
table one slot larger, a field the layout's memset already zeroed), the
resolved table's bookkeeping (a gradient resolved again instead of found gives
the same stops: once per render has no witness but the time), the item's map
written again where the geometry wrote it, and a gradient fill's work count.
gradient.c's 228 (``rd-``), after the first sweep's gaps were closed: 31 do not
compile, 175 are caught and 22 survive: ties at equal values (the largest
entry, the two roots, the domain's edges, the clamp at exactly 2^431, the
absolute value's zero), sign tests where zero was handled before (the angle's
axes, ``rad < 0``), the guards of the color line that only rounding reaches (the
clamp into the line, the loop's reach to the last stop), the square of a low
part (below a double's resolution unless every larger term cancels), the
memset of the prepared gradient (every field read is set) and the radial
degenerate flag (the plan's notice skips those fills first).  The first sweep
of the story's lines found eight mutants no test caught (a low part of each
difference, the twice-hi-lo term, an isoline along an axis, the pad below equal
first stops, the smaller root, the pool's offset between two gradients, the
clip's weight of a gradient fill); each is caught by a test since, and three
hand plants were rewritten to compile or dropped (the stated tolerance is the
test's constant, so no library build can move it).

The survivors, by kind:

  - a tie between equal values: `<` for `<=` where both sides choosing the same
    value is the same answer (the rectangles' meet and join, the lowest and
    highest row and column of a piece, the box's minimum and maximum, the
    nesting's maxima, PLUS's clamp at exactly 1, the store's 0 and 255 ends,
    the arena's peak, the copy's shorter length)
  - a sign test at zero where zero gives the same answer: `x < 0` for `x <= 0`
    in the gamma's sign, the absolute values and signs of the integer
    divisions (whose denominators are never zero), the fold's absolute value
  - shortcuts that a later step repeats: the band test of an edge (the row
    clamp below it leaves the loop empty), a row loop's extra band at its end
    (it has no rows), the subpath read's failure (no index is out of range), the
    column direction where the piece has one column (handled before), each of
    the two lines that make an empty rectangle empty (the other one alone makes
    every loop and the work empty), and the readiness's own: widen's return
    when nothing grows (`&&` for `||`) and its test for side spans (`<=` for
    `<`), and ready_span's empty-span test (`>` for `>=`), which only let an
    empty span through to a memset of nothing
  - stores the next step overwrites or never reads: the conversion's memset, the
    default row of the gamut table, the layout's memset, the skip's depth reset
    (a skip always ends at depth 0), a glyph run read that is not used, the
    geometry's release clearing what is gone, `ops.thread_affine = 0` over a
    zeroed table, the non-caller
    `raw_bytes`, the glyph note's `memset` (every path that reads it after a
    failure overwrites it with the limit's note)
  - the destroy of a device path on a glyph's failure paths: the path is set
    only on success, so it is NULL there; and `r == OK && outline` for `||`
    (an outline is set only on success)
  - the arena's forgetting of a freed last block and of a reset's last block:
    no freed or stale pointer is ever reallocated
  - the work of a group's pop: it is never more than its push's (the region),
    so the most work between checks cannot show it
  - the measure's per-item reset of its peak: the peak is a high-water mark of a
    use that the reset already zeroes, so it is the largest item either way
  - unreachable: the skip of a refused instance (shape-cpu refuses no
    instance: its caps draw every kind and no resource limit applies to one),
    a geometry status other than OOM and LIMIT, a glyph run that cannot be
    read where the resource is not a path, a route that fails or refuses a run
    the plan routed (the plan decided it on the same map and skips a refused
    one), the layout's overflow on a 64-bit
    host, the expansion's failure to start, `GSHSCENE_ERR_REFUSED` and
    `GSHSCENE_ERR_INVALID` from the geometry's mapping, a failure other than
    cancel, refusal and memory (whose reason is "the CPU sink failed"), the
    clamp of `max_dimension` to the ceiling (the region's own ceiling check
    refuses first)
  - `fprintf(...) < 0` for `<= 0`: no line of the dump prints nothing; and the
    final `fflush` and `ferror` of the dump, either of which reports a failed
    stream
  - the stop chain's condition `&&` for `||`: a strict refusal has set the
    chain already, so both read the same

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
    ("dump-write-error", "src", RP, "      < 0) {\n    return GSHCPU_ERR_IO;\n  }\n  /* ferror", "      < 0) {\n    return GSHCPU_OK;\n  }\n  /* ferror", R),
    ("dump-flush-error", "src", RP, "  return (fflush(out) == 0 && !ferror(out)) ? GSHCPU_OK : GSHCPU_ERR_IO;", "  return GSHCPU_OK;", R),
    ("dump-ignores-ferror", "src", RP, "(fflush(out) == 0 && !ferror(out))", "(fflush(out) == 0)", R),

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

# --- story 13b: the sink -------------------------------------------------------------------------------------------------
# The named plants: every constant of S11's arithmetic and of the formats, the operators' factors one by one, and a
# violation of each rule of the sink contract that a sink can break (rule-S*; S8, S12 and S13 are argued in the
# docstring).  The rest of src/render is mutated by tools/gen-render-plants.py (tools/render-plants.json, below).

RC = "src/render/compose.c"
RV = "src/render/cover.c"
RG = "src/render/geometry.c"
RA = "src/render/arena.c"
RH = "src/render/hash.c"
RS = "src/render/sink.c"
RI = "src/render/render_internal.h"
RHD = "include/ghoti.io/shape-cpu/render.h"
# Quick and likely first: the sweep stops at the first test that fails.
RT = ["testRender_vectors", "testRender_parts", "testRender", "testRender_limits", "testRender_conformance", "probe"]
CONF = ["testRender_conformance"]


def factor(op, old, new):
    """Replace one factor of one operator's case in apply()."""
    return "    case GSHSCENE_COMPOSITE_%s:\n%s" % (op, old), "    case GSHSCENE_COMPOSITE_%s:\n%s" % (op, new)


_FACTORS = [
    ("clear-fa", "CLEAR", "      break;", "      fa = 1.0;\n      break;"),
    ("src-fa", "SRC", "      fa = 1.0;", "      fa = 0.5;"),
    ("dest-fb", "DEST", "      fb = 1.0;", "      fb = 0.5;"),
    ("src-over-fa", "SRC_OVER", "      fa = 1.0;\n      fb = 1.0 - sa;", "      fa = 0.5;\n      fb = 1.0 - sa;"),
    ("src-over-fb", "SRC_OVER", "      fa = 1.0;\n      fb = 1.0 - sa;", "      fa = 1.0;\n      fb = 1.0 - da;"),
    ("dest-over-fa", "DEST_OVER", "      fa = 1.0 - da;", "      fa = 1.0 - sa;"),
    ("dest-over-fb", "DEST_OVER", "      fa = 1.0 - da;\n      fb = 1.0;", "      fa = 1.0 - da;\n      fb = sa;"),
    ("src-in-fa", "SRC_IN", "      fa = da;", "      fa = sa;"),
    ("dest-in-fb", "DEST_IN", "      fb = sa;", "      fb = da;"),
    ("src-out-fa", "SRC_OUT", "      fa = 1.0 - da;", "      fa = 1.0 - sa;"),
    ("dest-out-fb", "DEST_OUT", "      fb = 1.0 - sa;", "      fb = 1.0 - da;"),
    ("src-atop-fa", "SRC_ATOP", "      fa = da;", "      fa = 1.0 - da;"),
    ("src-atop-fb", "SRC_ATOP", "      fa = da;\n      fb = 1.0 - sa;", "      fa = da;\n      fb = sa;"),
    ("dest-atop-fa", "DEST_ATOP", "      fa = 1.0 - da;", "      fa = da;"),
    ("dest-atop-fb", "DEST_ATOP", "      fa = 1.0 - da;\n      fb = sa;", "      fa = 1.0 - da;\n      fb = 1.0 - sa;"),
    ("xor-fa", "XOR", "      fa = 1.0 - da;", "      fa = da;"),
    ("xor-fb", "XOR", "      fa = 1.0 - da;\n      fb = 1.0 - sa;", "      fa = 1.0 - da;\n      fb = sa;"),
]

PLANTS += [
    # --- S11: the transfers, the gamuts, the white ------------------------------------------------------------------
    ("r-srgb-decode-knee", "src", RC, "#define SRGB_DECODE_KNEE 0.04045", "#define SRGB_DECODE_KNEE 0.04046", RT),
    ("r-srgb-encode-knee", "src", RC, "#define SRGB_ENCODE_KNEE 0.0031308", "#define SRGB_ENCODE_KNEE 0.0031309", RT),
    ("r-srgb-slope", "src", RC, "#define SRGB_SLOPE 12.92", "#define SRGB_SLOPE 12.9", RT),
    ("r-srgb-offset", "src", RC, "#define SRGB_OFFSET 0.055", "#define SRGB_OFFSET 0.054", RT),
    ("r-srgb-scale", "src", RC, "#define SRGB_SCALE 1.055", "#define SRGB_SCALE 1.054", RT),
    ("r-srgb-exponent", "src", RC, "#define SRGB_EXPONENT 2.4", "#define SRGB_EXPONENT 2.2", RT),
    ("r-white-x", "src", RC, "#define WHITE_X 0.3127", "#define WHITE_X 0.3128", RT),
    ("r-white-y", "src", RC, "#define WHITE_Y 0.3290", "#define WHITE_Y 0.3291", RT),
    ("r-gamut-srgb-green", "src", RC, "{{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}},       /* sRGB */",
     "{{0.64, 0.33}, {0.30, 0.61}, {0.15, 0.06}},       /* sRGB */", RT),
    ("r-gamut-adobe-green", "src", RC, "{0.21, 0.71}", "{0.21, 0.70}", RT),
    ("r-gamut-p3-red", "src", RC, "{{0.680, 0.320}", "{{0.681, 0.320}", RT),
    ("r-gamut-p3-blue", "src", RC, "{0.150, 0.060}}, /* Display P3 */", "{0.150, 0.061}}, /* Display P3 */", RT),
    ("r-gamut-bt2020-green", "src", RC, "{0.170, 0.797}", "{0.170, 0.796}", RT),
    ("r-gamut-bt2020-blue", "src", RC, "{0.131, 0.046}", "{0.131, 0.047}", RT),
    ("r-gamut-adobe-row", "src", RC, "    case GCOL_PRIMARIES_ADOBE_RGB:\n      row = 1;", "    case GCOL_PRIMARIES_ADOBE_RGB:\n      row = 0;", RT),
    ("r-gamut-p3-row", "src", RC, "    case GCOL_PRIMARIES_DISPLAY_P3:\n      row = 2;", "    case GCOL_PRIMARIES_DISPLAY_P3:\n      row = 3;", RT),
    ("r-gamut-bt2020-row", "src", RC, "    case GCOL_PRIMARIES_BT2020:\n      row = 3;", "    case GCOL_PRIMARIES_BT2020:\n      row = 2;", RT),
    ("r-gamma-decode-exponent", "src", RC, "return x < 0.0 ? -gmath_pow(-x, space->gamma) : gmath_pow(x, space->gamma);",
     "return x < 0.0 ? -gmath_pow(-x, space->gamma) : gmath_pow(x, 1.0 / space->gamma);", RT),
    ("r-gamma-encode-exponent", "src", RC, "return y < 0.0 ? -gmath_pow(-y, 1.0 / space->gamma) : gmath_pow(y, 1.0 / space->gamma);",
     "return y < 0.0 ? -gmath_pow(-y, 1.0 / space->gamma) : gmath_pow(y, space->gamma);", RT),
    ("r-gamma-negative", "src", RC, "return x < 0.0 ? -gmath_pow(-x, space->gamma)", "return x < 0.0 ? gmath_pow(-x, space->gamma)", RT),
    ("r-xyz-z-row", "src", RC, "    p.m[2][c] = (1.0 - xy[c][0] - xy[c][1]) / xy[c][1];", "    p.m[2][c] = (1.0 - xy[c][0]) / xy[c][1];", RT),
    ("r-xyz-scale", "src", RC, "      out->m[r][c] = s[c] * p.m[r][c];", "      out->m[r][c] = s[r] * p.m[r][c];", RT),
    ("r-inverse-index", "src", RC, "      int c1 = (i + 2) % 3;", "      int c1 = (i + 1) % 3;", RT),
    ("r-det-sign", "src", RC, "- m[0][1] * (m[1][0] * m[2][2]", "+ m[0][1] * (m[1][0] * m[2][2]", RT),
    ("r-matrix-order", "src", RC, "    mul3(&c->to_xyz, lin, lin);\n    mul3(&c->from_xyz, lin, lin);",
     "    mul3(&c->from_xyz, lin, lin);\n    mul3(&c->to_xyz, lin, lin);", RT),
    ("r-matrix-whenever", "src", RC, "c->matrix = !c->same && from->primaries != to->primaries;", "c->matrix = !c->same;", RT),
    ("r-premultiply-again", "src", RC, "  out[1] = straight[1] * a;", "  out[1] = straight[1];", RT),
    ("r-space-equal-gamma", "src", RC, "a->transfer == b->transfer && a->gamma == b->gamma;", "a->transfer == b->transfer;", RT),
    # --- S11: the operators -------------------------------------------------------------------------------------------
] + [("r-op-" + n, "src", RC) + factor(op, o, w) + (RT,) for n, op, o, w in _FACTORS] + [
    ("r-op-plus-clamp", "src", RC, "o[i] = sum < 1.0 ? sum : 1.0;", "o[i] = sum < 2.0 ? sum : 2.0;", RT),
    ("r-op-apply-weights", "src", RC, "d[i] = (1.0 - m) * d[i] + m * o[i];", "d[i] = m * d[i] + (1.0 - m) * o[i];", RT),
    ("r-op-apply-backdrop", "src", RC, "d[i] = (1.0 - m) * d[i] + m * o[i];", "d[i] = d[i] + m * o[i];", RT),
    ("r-bounded-dest", "src", RC, "    case GSHSCENE_COMPOSITE_DEST:\n    case GSHSCENE_COMPOSITE_SRC_OVER:\n",
     "    case GSHSCENE_COMPOSITE_SRC_OVER:\n", RT),
    ("r-bounded-plus", "src", RC, "    case GSHSCENE_COMPOSITE_PLUS:\n      return 1;", "      return 1;\n    case GSHSCENE_COMPOSITE_PLUS:", RT),
    ("r-bounded-src-in", "src", RC, "    case GSHSCENE_COMPOSITE_XOR:\n    case GSHSCENE_COMPOSITE_PLUS:\n      return 1;",
     "    case GSHSCENE_COMPOSITE_XOR:\n    case GSHSCENE_COMPOSITE_SRC_IN:\n    case GSHSCENE_COMPOSITE_PLUS:\n      return 1;", RT),
    # --- D8 and the write -----------------------------------------------------------------------------------------------
    ("r-weigh-round", "src", RC, "return (m8 * c8 + 127u) / 255u;", "return (m8 * c8 + 128u) / 255u;", RT),
    ("r-weigh-floor", "src", RC, "return (m8 * c8 + 127u) / 255u;", "return (m8 * c8) / 255u;", RT),
    ("r-weigh-divisor", "src", RC, "return (m8 * c8 + 127u) / 255u;", "return (m8 * c8 + 127u) / 256u;", RT),
    ("r-store8-scale", "src", RC, "  double t = v * 255.0;", "  double t = v * 256.0;", RT),
    ("r-store8-half", "src", RC, "(fraction >= 0.5 ? 1 : 0)", "(fraction > 0.5 ? 1 : 0)", RT),
    ("r-store8-truncate", "src", RC, "(fraction >= 0.5 ? 1 : 0)", "(fraction >= 1.0 ? 1 : 0)", RT),
    ("r-store8-top", "src", RC, "  if (t >= 255.0) {\n    return 255;", "  if (t >= 254.0) {\n    return 255;", RT),
    ("r-store-float-twice", "src", RC, "  return (float)v;", "  return (float)(double)(float)(v * (1.0 + 0x1p-30));", RT),
    ("r-load-scale", "src", RC, "      v[i] = (double)p[i] / 255.0;", "      v[i] = (double)p[i] / 256.0;", RT),
    ("r-load-straight", "src", RC, "      v[i] = v[i] * v[3];", "      v[i] = v[i];", RT),
    ("r-store-straight", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : w[i] / w[3];", "      w[i] = w[3] == 0.0 ? 0.0 : w[i];", RT),
    ("r-store-straight-zero", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : w[i] / w[3];", "      w[i] = w[i] / w[3];", RT),
    ("r-pixel-bytes", "src", RC, "? 16u : 4u;", "? 8u : 4u;", RT),
    ("r-premultiplied-forms", "src", RC, "  return format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;",
     "  return format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED;", RT),
    # --- the clipped scan converter ---------------------------------------------------------------------------------
    ("r-cover-area-shift", "src", RV, "#define AREA_SHIFT (2 * PIXEL_BITS + 1 - 8)", "#define AREA_SHIFT (2 * PIXEL_BITS - 8)", RT),
    ("r-cover-pixel", "src", RV, "#define ONE_PIXEL 64", "#define ONE_PIXEL 63", RT),
    ("r-cover-fold-column", "src", RV, "int64_t column = ex < c->rect.x0 ? 0 : ex - c->rect.x0 + 1;",
     "int64_t column = ex < c->rect.x0 ? 1 : ex - c->rect.x0 + 1;", RT),
    ("r-cover-round-half", "src", RV, "uint64_t quotient = (n + d / 2) / d;", "uint64_t quotient = (n + (d - 1) / 2) / d;", RT),
    ("r-cover-floor", "src", RV, "    q -= 1;", "    q += 0;", RT),
    ("r-cover-cells-limit", "src", RV, "  if (c->cells >= c->max_cells) {", "  if (c->cells > c->max_cells) {", RT),
    ("r-cover-sweep-scale", "src", RV, "      int64_t accumulated = cv[0] * (ONE_PIXEL * 2);", "      int64_t accumulated = cv[0] * ONE_PIXEL;", RT),
    ("r-cover-even-odd-fold", "src", RV, "      value = 512 - value;", "      value = 511 - value;", RT),
    ("r-cover-even-odd-mask", "src", RV, "    value &= 511;", "    value &= 1023;", RT),
    ("r-cover-saturate", "src", RV, "  } else if (value >= 256) {\n    value = 255;", "  } else if (value >= 255) {\n    value = 254;", RT),
    ("r-cover-row-cut", "src", RV, "p.x = from.x + round_div((b - from.y) * (to.x - from.x), to.y - from.y);",
     "p.x = from.x + (b - from.y) * (to.x - from.x) / (to.y - from.y);", RT),
    ("r-cover-column-cut", "src", RV, "p.y = from.y + round_div((b - from.x) * (to.y - from.y), to.x - from.x);",
     "p.y = from.y + (b - from.x) * (to.y - from.y) / (to.x - from.x);", RT),
    ("r-cover-closing", "src", RV, "    edge(c, point_of(points[sub.first + sub.count - 1]), point_of(points[sub.first]));", "", RT),
    ("r-coord-limit", "srch", RI, "#define GSHCPU_RENDER_COORD_LIMIT ((int64_t)1 << 30)", "#define GSHCPU_RENDER_COORD_LIMIT ((int64_t)1 << 31)", RT),
    # --- geometry (S16, AD-24) ------------------------------------------------------------------------------------------
    ("rule-S16-target-first", "src", RG, "  GSHGEOM_Result r = gshgeom_map_compose_chain(chain, count, target, &map);",
     "  GSHGEOM_Map ident;\n  gshgeom_map_identity(2, &ident);\n  GSHGEOM_Result r = gshgeom_map_compose_chain(chain, count, &ident, &map);\n  if (r == GSHGEOM_OK) {\n    r = gshgeom_map_compose(&map, target, &map);\n  }", RT),
    ("r-geometry-item-map", "src", RG, "  memset(out, 0, sizeof *out);\n  chain[count] = item->map;", "  memset(out, 0, sizeof *out);\n  gshgeom_map_identity(2, &chain[count]);", RT),
    ("r-geometry-glyph-y", "src", RG, "run.glyphs[i].x, run.glyphs[i].y, 0.0,", "run.glyphs[i].x, run.glyphs[i].x, 0.0,", RT),
    ("r-geometry-oom", "src", RG, "return r == GSHGEOM_ERR_OOM ? GSHCPU_ERR_OOM : GSHCPU_ERR_LIMIT;", "return GSHCPU_ERR_LIMIT;", RT),
    ("r-geometry-box-ceiling", "src", RG, "    out->box.x1 = -pixel_floor(-x1);", "    out->box.x1 = pixel_floor(x1);", RT),
    ("r-geometry-floor", "src", RG, "  return v % 64 != 0 && v < 0 ? q - 1 : q;", "  return q;", RT),
    # --- the arena ------------------------------------------------------------------------------------------------------
    ("r-arena-align", "src", RA, "#define ARENA_ALIGN 16u", "#define ARENA_ALIGN 32u", RT),
    ("r-arena-capacity", "src", RA, "(!a->measure && end > a->capacity)", "(!a->measure && end >= a->capacity)", RT),
    ("r-arena-copy", "src", RA, "    memcpy(p, old, had < n ? had : n);", "    memcpy(p, old, had < n ? had - 1 : n);", RT),
    # --- the formats and the hash -----------------------------------------------------------------------------------
    ("r-hash-version", "src", RH, "#define RENDER_FORMAT_VERSION 1u", "#define RENDER_FORMAT_VERSION 2u", RT),
    ("r-hash-kind", "src", RH, "#define RENDER_KIND 0x444e4552u", "#define RENDER_KIND 0x444e4553u", RT),
    ("r-hash-format-code", "hdr", RHD, "  GSHCPU_FORMAT_RGBAF32_STRAIGHT = 3,", "  GSHCPU_FORMAT_RGBAF32_STRAIGHT = 6,", RT),
    ("r-format-bits", "src", RH, "    bits = 32;", "    bits = 16;", RT),
    ("r-format-float-straight", "src", RH, "return premultiplied ? GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED : GSHCPU_FORMAT_RGBAF32_STRAIGHT;",
     "return GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;", RT),
    # --- the sink -------------------------------------------------------------------------------------------------------
    ("r-work-bound", "src", RS, "!gcu_safe_add_u64(b, (uint64_t)cs->max_bytes, &b)", "!gcu_safe_add_u64(b, 0, &b)", RT),
    ("r-region-ceiling", "src", RS, "L->region.x1 > (int64_t)GSHCPU_MAX_DIMENSION_CEILING", "L->region.x1 > (int64_t)GSHCPU_MAX_DIMENSION_CEILING * 0", RT),
    ("r-region-max-bytes", "src", RS, "  if (L->pixels > cs->max_bytes) {", "  if (L->pixels > cs->max_bytes + 1) {", RT),
    ("r-default-cells", "src", RS, "limits->max_cells : GSHCPU_LIMITS_DEFAULT_MAX_CELLS;", "limits->max_cells : 1000;", RT),
    ("r-default-dimension-ceiling", "src", RS, "    cs->max_dimension = GSHCPU_MAX_DIMENSION_CEILING;", "    cs->max_dimension = 16;", RT),
    ("r-foreground-alpha", "src", RS, "gshscene_list_foreground(request->list, &run->foreground) != GSHSCENE_OK",
     "(gshscene_color_init(&run->foreground), GSHSCENE_OK) != GSHSCENE_OK", RT),
    # Story 14c: a palette fill takes the list's palette, in the paint's resolution and in the sink's disposition.
    ("r-palette-resolve", "src", RS, "gshscene_paint_resolve(&item->paint, &run->foreground, run->palette, &color)",
     "gshscene_paint_resolve(&item->paint, &run->foreground, NULL, &color)", RT),
    ("r-palette-disposition", "src", RS, "gshscene_caps_disposition(&run->job->caps, item, gshscene_job_target_space(run->job), run->palette);",
     "gshscene_caps_disposition(&run->job->caps, item, gshscene_job_target_space(run->job), NULL);", RT),
    ("r-palette-taken", "src", RS, "  run->palette = gshscene_list_palette(request->list);\n", "", RT),
    ("r-paint-target-space", "src", RS, "    space = *gshscene_job_target_space(run->job);", "    space = frame->space;", RT),
    ("r-group-space-inherit", "src", RS, "? parent->space : item->group.blend_space;", "? (parent ? *gshscene_job_target_space(run->job) : parent->space) : item->group.blend_space;", RT),
    ("r-group-opacity", "src", RS, "        s[i] = o * s[i];", "        s[i] = o * o * s[i];", RT),
    ("r-group-convert-way", "src", RS, "gshcpu_render_convert_prepare(&c, &f->space, &parent->space);", "gshcpu_render_convert_prepare(&c, &parent->space, &f->space);", RT),
    ("r-group-outer-clips", "src", RS, "uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);", "uint32_t m8 = 255u;", RT),
    ("r-group-unbounded-region", "src", RS, "GSHCPU_Rect region = bounded ? f->dirty : run->layout.region;", "GSHCPU_Rect region = f->dirty;", RT),
    ("r-fill-weight", "src", RS, "        continue;\n      }\n      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 255.0, GSHSCENE_COMPOSITE_SRC_OVER);",
     "        continue;\n      }\n      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 256.0, GSHSCENE_COMPOSITE_SRC_OVER);", RT),
    ("rgr-fill-weight", "src", RS, "      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 255.0, GSHSCENE_COMPOSITE_SRC_OVER);\n      if (run->top == 0) {\n        run->touched[p] = 1;\n      }\n    }\n  }\n  run->work += (uint64_t)w * (uint64_t)(rect.y1 - rect.y0);\n  return GSHSCENE_OK;\n}\n\nstatic GSHSCENE_Result fill(", "      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 256.0, GSHSCENE_COMPOSITE_SRC_OVER);\n      if (run->top == 0) {\n        run->touched[p] = 1;\n      }\n    }\n  }\n  run->work += (uint64_t)w * (uint64_t)(rect.y1 - rect.y0);\n  return GSHSCENE_OK;\n}\n\nstatic GSHSCENE_Result fill(", ["testRender_gradient"] + RT),
    ("rgr-clips-unweighed", "src", RS, "      m8 = weigh_clips(run, frame->clip_base, run->clip_count, m8, x, y);\n      if (m8 == 0) {\n        continue;\n      }\n      double color[4];", "      if (m8 == 0) {\n        continue;\n      }\n      double color[4];", ["testRender_gradient"] + RT),
    ("r-touched", "src", RS, "      if (run->touched[p]) {", "      if (1) {", RT),
    ("r-clip-memory", "src", RS, "clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;", "clip->coverage = run->clip_memory;", RT),
    # What a render makes ready (widen, ready_span): the whole region on every frame changes no pixel, only the bytes
    # touched, which testRender_limits counts; the root not read from the target, and a group read where it is not ready.
    ("r-ready-whole-region", "src", RS, "  GSHCPU_Rect grown = gshcpu_render_rect_join(old, r);", "  GSHCPU_Rect grown = gshcpu_render_rect_join(old, run->layout.region);", ["testRender_limits"]),
    ("r-root-unread", "src", RS, "    gshcpu_render_load(run->format, row, (size_t)x, &run->root[4u * p]);", "    (void)row;", RT),
    ("r-group-reads-unready", "src", RS, "inside(f->dirty, x, y) ? &f->layer[4u * p] : zero", "1 ? &f->layer[4u * p] : zero", RT),
    # --- the contract's rules (shape-scene's sink.h): each violated, the conformance suite must fail -----------------
    ("rule-S1-never-completes", "src", RS, "  finish(run, GSHSCENE_OK, memory);\n  return GSHSCENE_OK;\n}",
     "  (void)memory;\n  return GSHSCENE_OK;\n}", CONF),
    ("rule-S2-caps-instances", "src", RS, "for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {",
     "for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_INSTANCE; k++) {", CONF),
    ("rule-S3-failure-leaks", "src", RS, "      finish(run, r, memory);\n      return GSHSCENE_OK;", "      return r;", CONF),
    ("rule-S4-damage-ignored", "src", RS, "    r = gshcpu_render_rect_meet(r, *target->damage);", "    (void)0;", CONF),
    ("rule-S5-entry-chain", "src", RS, "GSHSCENE_REASON_RESOURCE_LIMIT, chain, count);", "GSHSCENE_REASON_RESOURCE_LIMIT, chain, count - 1);", RT),
    # rule-S6-planned-refusal-drawn is gone (story 14a): shape-cpu's caps refuse no item, so its first clause can only
    # be reached by a refused route, which rgl-refused-route-drawn plants.
    ("rule-S7-check-skipped", "src", RS, "    if (gshscene_job_unit(job, run->processed, run->done)) {",
     "    if (gshscene_job_unit(job, run->processed, run->done) && 0) {", CONF),
    ("rule-S7-done-in-group", "src", RS, "    if (run->groups_open == 0) {\n      run->done = run->processed;", "    if (1) {\n      run->done = run->processed;", RT),
    ("rule-S7-cells-uncounted", "src", RS, "      run->work += cells;", "      (void)cells;", RT),
    ("rule-S9-allocates", "src", RS, "  if (caller) {\n    raw = request->workspace;", "  if (0) {\n    raw = request->workspace;", CONF),
    ("rule-S9-need-short", "src", RS, "      return gcu_safe_add_size(L.fixed, peak, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;\n    }",
     "      return gcu_safe_add_size(L.fixed, 0, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;\n    }", CONF),
    ("rule-S9-arena-short", "src", RS, "raw_bytes - (size_t)(base - raw) - L.at_items);", "raw_bytes - (size_t)(base - raw) - L.at_items - 16);", CONF),
    # 2026-10-21 (story 16's two decisions): the behaviour before each fix, planted back.  S4: the cell cap met only
    # where a damage-limited render draws (the walk is the rectangle drawn), and the bound that lets it skip the count
    # too low.  S9: a short workspace sized over the sink's allocator, short for an item or for the block.
    ("rule-S4-cells-in-damage", "src", RS, "*rect, walk, out, run->acc", "*rect, *rect, out, run->acc", RT),
    ("rule-S4-cells-bound-low", "src", RV, "  return rows + columns;", "  return rows;", RT),
    ("rule-S9-short-item-measures", "src", RS, "    return gcu_safe_add_size(request->workspace_bytes, 1u, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;\n  }", "  }", RT),
    ("rule-S9-short-block-measures", "src", RS, "  if (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) {\n    if (!request->workspace",
     "  if ((request->flags & GSHSCENE_RENDER_CALLER_MEMORY) && request->workspace && request->workspace_bytes >= L.fixed) {\n    if (!request->workspace", RT),
    ("rule-S10-keeps-a-reference", "src", RS, "  double s[4];\n  source_of(run, item, frame, s);",
     "  double s[4];\n  gshscene_resource_retain(item->resource);\n  source_of(run, item, frame, s);", CONF),
    ("rule-S11-src-over", "src", RC, "      fa = 1.0;\n      fb = 1.0 - sa;", "      fa = 1.0;\n      fb = 1.0 - sa * 0.5;", RT),
    ("rule-S14-affine-unstated", "src", RS, "  ops.thread_affine = 0;", "  ops.thread_affine = 1;", CONF),
    ("rule-S15-readback-offset", "src", RS, "+ (size_t)target->extent.x0 * bytes,", "+ (size_t)target->extent.y0 * bytes,", CONF),
]

# Story 14a: runs from a font by both routes.  The cap; the route the plan refused, skipped; each glyph's entry, its
# index, and the strict stop; the fast tier taken (AD-5's "always general" plant), the general route's map, the
# flattener's limit as a glyph's own.  geometry.c and sink.c are also mutated by the generator.
GLR = ["testGlyph_render", "testRender_conformance"]
PLANTS += [
    ("rgl-cap", "src", RS, "      GSHSCENE_CAP_READBACK | GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_GRADIENT_SPACES;", "      GSHSCENE_CAP_READBACK | GSHSCENE_CAP_GRADIENT_SPACES;", ["testRender"] + GLR),
    ("rgl-refused-route-drawn", "src", RS, "          route.entry_class == GSHSCENE_ENTRY_REFUSED)) {", "          0)) {", CONF),
    ("rgl-notes-unreported", "src", RS, "    if (!g.notes[i].entry_class) {\n      continue;", "    if (1) {\n      continue;", GLR),
    ("rgl-strict-goes-on", "src", RS, "        (run->request->flags & GSHSCENE_RENDER_STRICT)) {", "        0) {", GLR),
    ("rgl-entry-index", "src", RS, "        (GSHSCENE_Reason)g.notes[i].reason, chain, count, i);", "        (GSHSCENE_Reason)g.notes[i].reason, chain, count, 0);", GLR),
    ("rgl-always-general", "src", RG, "  if (route->route == GSHSCENE_GLYPH_FAST) {", "  if (0) {", GLR),
    ("rgl-general-map", "src", RG, "gshgeom_device_path_from_model(outline, &glyph_map, alloc, limits, &device);", "gshgeom_device_path_from_model(outline, map, alloc, limits, &device);", GLR),
    ("rgl-flatten-limit-unnoted", "src", RG, "      limit_note(note); /* past the flattener's points */\n", "", GLR),
    ("rgl-route-raster", "src", RG, "         gshscene_glyph_route(item->resource, &map, GSHSCENE_TARGET_RASTER, out) == GSHSCENE_OK;", "         gshscene_glyph_route(item->resource, &map, GSHSCENE_TARGET_STREAM, out) == GSHSCENE_OK;", GLR),
]

# Color-paint story P2: gradients.  The caps; the notice's skip; the sample point, the chain and its domain (S4, S6);
# the stops' pool; the conversion and premultiplication; and in gradient.c each kind's rule, the extend, the color
# line, the inverse and the end of the double's reach.  Both files are also mutated by the generator.
RGR = "src/render/gradient.c"
GRT = ["testRender_gradient"] + RT
PLANTS += [
    ("rgr-cap-kinds", "src", RS, "  for (uint32_t k = GSHSCENE_GRADIENT_LINEAR; k < GSHSCENE_GRADIENT_KIND_COUNT; k++) {", "  for (uint32_t k = GSHSCENE_GRADIENT_LINEAR; k < GSHSCENE_GRADIENT_SWEEP; k++) {", GRT),
    ("rgr-cap-spaces", "src", RS, "GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_GRADIENT_SPACES;", "GSHSCENE_CAP_GLYPHS_FROM_FONT;", GRT),
    ("rgr-notice-drawn", "src", RS, "  if (gshscene_gradient_notice(item, run->maps, count - 1, &run->job->target.map)) {", "  if (0) {", GRT),
    ("rgr-sample-corner", "src", RS, "(double)x + 0.5, (double)y + 0.5, color)", "(double)x, (double)y, color)", GRT),
    ("rgr-chain-no-gradient-map", "src", RS, "gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &composed)", "gshgeom_map_compose_chain(run->maps, count, &run->job->target.map, &composed)", GRT),
    ("rgr-chain-no-target", "src", RS, "gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &composed)", "gshgeom_map_compose_chain(run->maps, count + 1, NULL, &composed)", GRT),
    ("rgr-domain-unchecked", "src", RS, " ||\n      !gshcpu_render_gradient_in_domain(&gr, whole)) {", ") {", GRT),
    ("rgr-domain-in-damage", "src", RS, "!gshcpu_render_gradient_in_domain(&gr, whole)) {", "!gshcpu_render_gradient_in_domain(&gr, rect)) {", GRT),
    ("rgr-convert-skipped", "src", RS, "      gshcpu_render_convert_straight(&c, color, color);\n", "", GRT),
    ("rgr-not-premultiplied", "src", RS, "      double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};", "      double s[4] = {color[0], color[1], color[2], color[3]};", GRT),
    ("rgr-paint-alpha", "src", RS, "&composed, item->paint.rgba[3]) != GSHCPU_OK", "&composed, 1.0) != GSHCPU_OK", GRT),
    ("rgr-pool-offset", "src", RS, "run->pool + e->first, &composed", "run->pool, &composed", GRT),
    ("rgr-pool-unadvanced", "src", RS, "    run->pooled += g->stop_count;\n", "", GRT),
    ("rgr-interpolant-straight", "src", RGR, "      stops[i].rgba[k] = stops[i].rgba[k] * a;", "      stops[i].rgba[k] = stops[i].rgba[k] * (a > 2.0 ? a : 1.0);", GRT),
    ("rgr-unpremultiplied-not", "src", RGR, "      c[k] = a == 0.0 ? 0.0 : c[k] / a;", "      c[k] = a == 0.0 ? 0.0 : c[k];", GRT),
    ("rgr-paint-alpha-unread", "src", RGR, "  c[3] = c[3] * g->alpha;", "  c[3] = c[3];", GRT),
    ("rgr-linear-normal", "src", RGR, "        out->n[0] = -g->d[1];", "        out->n[0] = g->d[1];", GRT),
    ("rgr-linear-den", "src", RGR, "        grow_square_of_difference(&den, g->p1[1], g->p0[1], 1.0);", "", GRT),
    ("rgr-linear-den-lo", "src", RGR, "        grow_product(&den, dx_lo, out->n[0]);", "", GRT),
    ("rgr-radial-a-sign", "src", RGR, "      grow_square_of_difference(&a, g->r1, g->r0, -1.0);", "      grow_square_of_difference(&a, g->r1, g->r0, 1.0);", GRT),
    ("rgr-radial-smaller-first", "src", RGR, "        w[nw++] = u > v ? u : v;\n        w[nw++] = u > v ? v : u;", "        w[nw++] = u > v ? v : u;\n        w[nw++] = u > v ? u : v;", GRT),
    ("rgr-radial-radius", "src", RGR, "      if (g->r0 + w[i] * g->dr > 0.0) {", "      if (g->r0 - w[i] * g->dr > 0.0) {", GRT),
    ("rgr-radial-linear-root", "src", RGR, "      w[nw++] = c / (2.0 * b);", "      w[nw++] = c / b;", GRT),
    ("rgr-radial-b", "src", RGR, "    double b = x * g->n[0] + y * g->n[1] + g->r0 * g->dr;", "    double b = x * g->n[0] + y * g->n[1];", GRT),
    ("rgr-sweep-wrap", "src", RGR, "    rad += TWO_PI;", "    rad += 0.0;", GRT),
    ("rgr-sweep-half-turn", "src", RGR, "    return dx < 0.0 ? 0.5 * g->turn : 0.0;", "    return 0.0;", GRT),
    ("rgr-sweep-unit", "src", RGR, "rad : rad / TWO_PI * g->turn;", "rad : rad;", GRT),
    ("rgr-sweep-step", "src", RGR, "    *t = theta < g->a0 ? -GSHCPU_RENDER_T_LIMIT : GSHCPU_RENDER_T_LIMIT;", "    *t = theta < g->a0 ? GSHCPU_RENDER_T_LIMIT : -GSHCPU_RENDER_T_LIMIT;", GRT),
    ("rgr-repeat", "src", RGR, "          x = x - gmath_floor(x / g->period) * g->period;", "          x = x - gmath_floor(x / g->period);", GRT),
    ("rgr-reflect", "src", RGR, "          x = g->period - (d < 0.0 ? -d : d);", "          x = g->period - d;", GRT),
    ("rgr-pad-below-last", "src", RGR, "      if (t < g->o_first) {\n        copy4(c, g->stops[0].rgba);", "      if (t < g->o_first) {\n        copy4(c, g->stops[g->count - 1].rgba);", GRT),
    ("rgr-equal-offsets-first", "src", RGR, "    if (t < s[mid].offset) {", "    if (t <= s[mid].offset) {", GRT),
    ("rgr-one-stop", "src", RGR, "  if (g->count == 1) {", "  if (0) {", GRT),
    ("rgr-clamp-low", "src", RGR, "    t = -GSHCPU_RENDER_T_LIMIT; /* P2-4", "    t = t; /* P2-4", GRT),
    ("rgr-invert-unscaled", "src", RGR, "    m[i] = gmath_ldexp(m[i], -e);", "    m[i] = m[i];", GRT),
    ("rgr-invert-translation", "src", RGR, "  double u = qx - g->inv[4];", "  double u = qx;", GRT),
    ("rgr-invert-row", "src", RGR, "  inv[1] = gmath_ldexp(-m[1] / d, -e);", "  inv[1] = gmath_ldexp(-m[2] / d, -e);", GRT),
    ("rgr-domain-half", "src", RGR, "  const double bound = GSHSCENE_GRADIENT_DOMAIN * 0.5;", "  const double bound = GSHSCENE_GRADIENT_DOMAIN * 4.0;", GRT),
]

# The generated plants (tools/gen-render-plants.py): the mutations of src/render the sweep was seen to catch, and
# PLANT_EXTRA_JSON, a file of candidates to run as well (for re-making tools/render-plants.json).
import json as _json
for _file in [str(ROOT / "tools" / "render-plants.json")] + ([os.environ["PLANT_EXTRA_JSON"]] if os.environ.get("PLANT_EXTRA_JSON") else []):
    if os.path.exists(_file):
        with open(_file) as _f:
            PLANTS += [tuple(x) for x in _json.load(_f)]

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
        obj = destdir / (str(src).replace("/", "_") + ".o")
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
    c = run(["tools/check-corpus-hash.sh", str(exe), "tests/corpus/coverage.hash", "tests/corpus/render.hash"],
            env_extra={"LD_LIBRARY_PATH": LD_PATH})
    return c.returncode, (c.stdout + c.stderr).strip()


def apply_line(text, spec, new, label):
    """A plant addressed by line: spec is "LINE:FRAGMENT"; FRAGMENT must occur exactly once on that line."""
    number, fragment = spec.split(":", 1)
    lines = text.split("\n")
    line = lines[int(number) - 1]
    n = line.count(fragment)
    if n != 1:
        raise RuntimeError("plant %s: the fragment occurs %d times on line %s, not once:\n%s\n%s" % (label, n, number, fragment, line))
    if fragment == new:
        raise RuntimeError("plant %s: the replacement changes nothing" % label)
    lines[int(number) - 1] = line.replace(fragment, new)
    return "\n".join(lines)


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
    if kind in ("src", "srcl", "srch"):
        original = (ROOT / fname).read_text()
        text = apply(original, old, new, pid) if kind != "srcl" else apply_line(original, old, new, pid)
        planted = d / "planted" / fname
        planted.parent.mkdir(parents=True, exist_ok=True)
        # A source may include a neighbour by a relative path ("../exact/exact.h"),
        # so the planted file sits in a copy of the whole tree.
        shutil.copytree(ROOT / "src", d / "planted" / "src", dirs_exist_ok=True)
        planted.write_text(text)
        if fname.endswith(".h"):
            # A private header: every source is rebuilt from the planted copy of the tree.
            objs = compile_all(d / "obj", [str(d / "planted" / s) for s in srcs])
        else:
            obj = d / "planted.o"
            r = run(CC + CFLAGS + ["-c", str(planted), "-o", str(obj)])
            if r.returncode != 0:
                raise RuntimeError("plant %s does not compile:\n%s" % (pid, r.stderr))
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
        if test == "probe":
            rc, msg = probe_hash(objs, d)
            failing = [msg.splitlines()[-1] if msg else "probe"]
        else:
            exe = d / test
            link_test(test, objs, exe)
            rc, failing = run_test(exe)
        if rc != 0:
            caught_by.append((test, failing[:1]))
            break  # one test seeing it is enough; the lists are ordered by cost
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
