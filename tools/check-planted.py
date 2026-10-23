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
  rn1-        N1's saturation (shape-scene's paint.h, Corey 2026-10-21): the
              clamp's two sides, the un-premultiply's guard, its bound and sign,
              and each point it is named at (a conversion's decode, matrix and
              encode, a group's un-premultiply, every apply, a straight store, G4)
  uc-         strokes story S4b, by hand, one for each rule and branch of the exact union coverage (render.h, UC1 to UC12): the
              winding on a cell's left side (the sign and weight of each step, the cover left of the cell and the fold
              column in it, its exact division), the slabs and the order (the fragments a slab holds, the order decided at the
              end, sub-slabs merged, the slope tie dropped or reversed, a fragment's sign), the shared area and its
              arithmetic (the weight, the wrong-way area, the shift, the rounding, the floor of an x), the fold of the
              magnitude, the integral check, the pieces (flat pieces counted, walked, touching the left side; cut from the
              upper end), which cells (the threshold, the window of the pool, the sparse sweep visiting them), the three parts
              (each term of the predicate, the complement, the rule, the clip's own overlap, the threshold, the value kept,
              the step skipped), the cap and the field (off by one, a fallback to the sum, the damage, the ceiling, the
              default read for a set field, the refusal swallowed, work uncounted, the need short, stale counts), and the
              routes of UC10.  Written with the story and not run: their anchors were checked to match once, and the
              generated candidates for cover.c and sink.c, whose lines have moved since the last sweep (503 anchors in
              render-plants.json did not match before this story), are to be regenerated with tools/gen-render-plants.py,
              which now also reads exact.c and stroke.c, and swept again
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

N1's saturation (2026-10-21) changed compose.c and gradient.c, and their 369
candidates were swept again in ghoti-build:gcc16: 67 do not compile, 267 are
caught (they replace compose.c's and gradient.c's in tools/render-plants.json)
and 35 survive, gradient.c's 22 of before and compose.c's of before by kind,
and seven on the new lines, each a tie or a zero the saturation cannot tell
apart: the clamp's `>` and `<` against `>=` and `<=` (at exactly 2^64 both
give 2^64), the un-premultiply's absolute values at zero, its `>=` against `>`
(at equality the quotient is exactly 2^64) and the sign tests' `<` against `<=`
(an alpha of 0 never reaches it, and a zero color with a nonzero alpha never
passes the guard).

Color-paint story P3 (blend modes) changed compose.c and sink.c.  The generated plants on lines the story left as
they were were carried to their new line numbers, and the 98 candidates of the lines it changed were swept in
ghoti-build:gcc16: 10 do not compile, 60 are caught (added to tools/render-plants.json) and 28 survive, each equivalent
in the definition: a tie or a threshold where both branches give one value (the quotient cap's `>=` at exactly 2^66,
where the quotient is exactly 2^66; hard-light's and soft-light's `<=` at Cs = 1/2 and soft-light's at Cb = 1/4, where
W3C's two branches meet; dodge's and burn's `min(1, q)` at q = 1; darken's and lighten's choice between equal values;
difference's sign at Cb = Cs; the minima, maxima and SetSat's sort between equal components), sign tests at zero (the
quotient's absolute values and its sign at a zero numerator, which never reaches the cap), ClipColor's `n < 0` and
`x > 1` at n = 0 and x = 1 (each step is the identity there, in the reals; doubles differ in the last bits, which the
tolerance allows), and the three loops of ClipColor and SetLum run to `i <= 3`, which write one past a three-element
array: undefined behavior only a sanitizer sees, and the sweep builds without one (`make test-asan` is the gate that
would).  The hand plants above (``r-b-``) put a defect in each mode, a branch, a constant, a clamp, Lum, ClipColor,
SetSat, B9's saturation and cap, B2's terms and alpha, the bounded list, the caps and the compositor's missing case.

ClipColor was then written again from the differences C - n (B10).  The eight generated plants on its old lines were
dropped, and its 22 new candidates swept (``rcl-``): 13 are caught and kept, 2 do not compile (two of its loops run to
`i <= 3`, which GCC now proves out of bounds) and 7 survive, equivalent in the definition: the minimum's and
maximum's choice between equal components (four), the early return's `x > 1` against `x >= 1` (at x = 1 neither step
moves anything), and the steps' `n < 0` and `x > 1` against `<= 0` and `>= 1` (at n = 0 the factor `L / (L - n)` is
1, at x = 1 the factor `(1 - L) / (x - L)` is 1: the identity in the reals, last bits in doubles).  Two generated
plants that deleted the copy into the working triple of COLOR and LUMINOSITY (``rc-401-del``, ``rc-405-del``) no
longer compile, GCC now seeing the uninitialized read through the inlined ClipColor, and are dropped.

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
    ("limits-default-cache-bytes", "src", CO, "  limits->max_cache_bytes = 0; /* the default,", "  limits->max_cache_bytes = 1; /* the default,", L),
    ("limits-default-exact-fragments", "src", CO, "  limits->max_exact_fragments = 0; /* the default,", "  limits->max_exact_fragments = 1; /* the default,", L),
    ("limits-default-exact-crossings", "src", CO, "  limits->max_exact_crossings = 0; /* the default,", "  limits->max_exact_crossings = 1; /* the default,", L),
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
    ("r-op-apply-weights", "src", RC, "  for (int i = 0; i < 4; i++) {\n    d[i] = gshcpu_render_saturate((1.0 - m) * d[i] + m * o[i]);", "  for (int i = 0; i < 4; i++) {\n    d[i] = gshcpu_render_saturate(m * d[i] + (1.0 - m) * o[i]);", RT),
    ("r-op-apply-backdrop", "src", RC, "  for (int i = 0; i < 4; i++) {\n    d[i] = gshcpu_render_saturate((1.0 - m) * d[i] + m * o[i]);", "  for (int i = 0; i < 4; i++) {\n    d[i] = gshcpu_render_saturate(d[i] + m * o[i]);", RT),
    ("r-bounded-dest", "src", RC, "    case GSHSCENE_COMPOSITE_DEST:\n    case GSHSCENE_COMPOSITE_SRC_OVER:\n",
     "    case GSHSCENE_COMPOSITE_SRC_OVER:\n", RT),
    ("r-bounded-plus", "src", RC, "    case GSHSCENE_COMPOSITE_XOR:\n    case GSHSCENE_COMPOSITE_PLUS:\n    /* B3: every blend mode is bounded. */", "    case GSHSCENE_COMPOSITE_XOR:\n    /* B3: every blend mode is bounded. */", RT),
    # P3-12: no silent default, and caps that claim exactly what apply composites.
    ("r-op-unknown-is-clear", "src", RC, "      return 0;\n  }\n  for (int i = 0; i < 4; i++) {\n    if (op == GSHSCENE_COMPOSITE_PLUS) {",
     "      break;\n  }\n  for (int i = 0; i < 4; i++) {\n    if (op == GSHSCENE_COMPOSITE_PLUS) {", RT),
    ("r-bounded-src-in", "src", RC, "    case GSHSCENE_COMPOSITE_XOR:\n    case GSHSCENE_COMPOSITE_PLUS:\n    /* B3", "    case GSHSCENE_COMPOSITE_XOR:\n    case GSHSCENE_COMPOSITE_SRC_IN:\n    case GSHSCENE_COMPOSITE_PLUS:\n    /* B3", RT),
    # B1 to B14 (color-paint story P3): each mode, a branch, a constant, a clamp, Lum, ClipColor, SetSat, B9, B2, B3.
    ('r-b-screen', "src", RC, '  return b + s - b * s;\n}', '  return b + s + b * s;\n}', RT),
    ('r-b-overlay-exchanged', "src", RC, 'return mix_hard_light(s, b); /* hard-light', 'return mix_hard_light(b, s); /* hard-light', RT),
    ('r-b-darken', "src", RC, '      return b < s ? b : s;', '      return b > s ? b : s;', RT),
    ('r-b-lighten', "src", RC, '      return b > s ? b : s;', '      return b < s ? b : s;', RT),
    ('r-b-dodge-zero', "src", RC, '      if (b == 0.0) {\n        return 0.0;', '      if (b == 0.0) {\n        return 1.0;', RT),
    ('r-b-dodge-one', "src", RC, '      if (s == 1.0) {\n        return 1.0;', '      if (s == 1.0) {\n        return 0.0;', RT),
    ('r-b-dodge-clamp', "src", RC, '      return q < 1.0 ? q : 1.0; /* B10', '      return q; /* B10', RT),
    ('r-b-burn-one', "src", RC, '      if (b == 1.0) {\n        return 1.0;', '      if (b == 1.0) {\n        return 0.0;', RT),
    ('r-b-burn-zero', "src", RC, '      if (s == 0.0) {\n        return 0.0;', '      if (s == 0.0) {\n        return 1.0;', RT),
    ('r-b-burn-quotient', "src", RC, 'double q = capped_quotient(1.0 - b, s);', 'double q = capped_quotient(b, s);', RT),
    ('r-b-burn-clamp', "src", RC, '      return 1.0 - (q < 1.0 ? q : 1.0);', '      return 1.0 - q;', RT),
    ('r-b-hard-light-double', "src", RC, 'return s <= 0.5 ? b * (2.0 * s) :', 'return s <= 0.5 ? b * s :', RT),
    ('r-b-hard-light-screen', "src", RC, ': mix_screen(b, 2.0 * s - 1.0);', ': mix_screen(b, 2.0 * s);', RT),
    ('r-b-soft-low', "src", RC, '        return b - (1.0 - 2.0 * s) * b * (1.0 - b);', '        return b - (1.0 - s) * b * (1.0 - b);', RT),
    ('r-b-soft-threshold', "src", RC, 'double d = b <= 0.25 ?', 'double d = b <= 0.3 ?', RT),
    ('r-b-soft-constant', "src", RC, '((16.0 * b - 12.0) * b + 4.0) * b', '((16.0 * b - 11.0) * b + 4.0) * b', RT),
    ('r-b-soft-root', "src", RC, ': gmath_sqrt(b);', ': b;', RT),
    ('r-b-soft-high', "src", RC, '      return b + (2.0 * s - 1.0) * (d - b);', '      return b + (2.0 * s - 1.0) * (d + b);', RT),
    ('r-b-difference', "src", RC, '      return b - s < 0.0 ? s - b : b - s;', '      return b - s;', RT),
    ('r-b-exclusion', "src", RC, '      return b + s - 2.0 * b * s;', '      return b + s - b * s;', RT),
    ('r-b-multiply', "src", RC, '    case GSHSCENE_COMPOSITE_MULTIPLY:\n      return b * s;', '    case GSHSCENE_COMPOSITE_MULTIPLY:\n      return b;', RT),
    ('r-b-lum-r', "src", RC, '#define LUM_R 0.3', '#define LUM_R 0.31', RT),
    ('r-b-lum-g', "src", RC, '#define LUM_G 0.59', '#define LUM_G 0.58', RT),
    ('r-b-lum-b', "src", RC, '#define LUM_B 0.11', '#define LUM_B 0.1', RT),
    ('r-b-clip-literal-x', "src", RC, '  double xl = x - n - ln; /* x - L */', '  double xl = x - l; /* x - L */', RT),
    ('r-b-clip-equal', "src", RC, '  if (n < 0.0 && ln > 0.0) {', '  if (n < 0.0) {', RT),
    ('r-b-clip-literal-low', "src", RC, '  double ln = lum(d);    /* L - n */', '  double ln = l - n;    /* L - n */', RT),
    ('r-b-clip-low-factor', "src", RC, '      t[i] = t[i] * l / ln;', '      t[i] = t[i] / ln;', RT),
    ('r-b-setsat-order', "src", RC, '  if (c[i1] > c[i2]) {\n    t = i1;\n    i1 = i2;\n    i2 = t;\n  }\n', '', RT),
    ('r-b-setsat-scale', "src", RC, '    out[i2] = s;\n', '    out[i2] = s / 2.0;\n', RT),
    ('r-b-hue', "src", RC, '      memcpy(c, cs, sizeof c);\n      set_sat(c, sat_of(cb));', '      memcpy(c, cs, sizeof c);\n      set_sat(c, sat_of(cs));', RT),
    ('r-b-saturation', "src", RC, '      set_sat(c, sat_of(cs));\n      set_lum(c, lum(cb));', '      set_sat(c, sat_of(cs));\n      set_lum(c, lum(cs));', RT),
    ('r-b-color', "src", RC, '    case GSHSCENE_COMPOSITE_COLOR:\n      memcpy(c, cs, sizeof c);', '    case GSHSCENE_COMPOSITE_COLOR:\n      memcpy(c, cb, sizeof c);', RT),
    ('r-b-luminosity', "src", RC, '      memcpy(c, cb, sizeof c);\n      set_lum(c, lum(cs));', '      memcpy(c, cb, sizeof c);\n      set_lum(c, lum(cb));', RT),
    ('r-b-saturation-after-mix', "src", RC, 'out[i] = gshcpu_render_saturate(c[i]); /* B9 */', 'out[i] = c[i]; /* B9 */', RT),
    ('r-b-cap', "src", RC, '#define QUOTIENT_CAP 0x1p66', '#define QUOTIENT_CAP 0x1p60', RT),
    ('r-b-term-alphas', "src", RC, '+ sa * da * b[i];', '+ sa * b[i];', RT),
    ('r-b-alpha', "src", RC, '  o[3] = sa + da * (1.0 - sa);\n}', '  o[3] = sa + da;\n}', RT),
    ('r-b-unpremultiply', "src", RC, '      cs[i] = gshcpu_render_unpremultiply(s[i], sa);', '      cs[i] = s[i];', RT),
    ('r-b-bounded-screen', "src", RC, '    case GSHSCENE_COMPOSITE_SCREEN:\n    case GSHSCENE_COMPOSITE_OVERLAY:\n    case GSHSCENE_COMPOSITE_DARKEN:\n    case GSHSCENE_COMPOSITE_LIGHTEN:\n    case GSHSCENE_COMPOSITE_COLOR_DODGE:\n    case GSHSCENE_COMPOSITE_COLOR_BURN:\n    case GSHSCENE_COMPOSITE_HARD_LIGHT:\n    case GSHSCENE_COMPOSITE_SOFT_LIGHT:\n    case GSHSCENE_COMPOSITE_DIFFERENCE:\n    case GSHSCENE_COMPOSITE_EXCLUSION:\n    case GSHSCENE_COMPOSITE_MULTIPLY:\n    case GSHSCENE_COMPOSITE_HUE:\n    case GSHSCENE_COMPOSITE_SATURATION:\n    case GSHSCENE_COMPOSITE_COLOR:\n    case GSHSCENE_COMPOSITE_LUMINOSITY:\n      return 1;', '    case GSHSCENE_COMPOSITE_OVERLAY:\n    case GSHSCENE_COMPOSITE_DARKEN:\n    case GSHSCENE_COMPOSITE_LIGHTEN:\n    case GSHSCENE_COMPOSITE_COLOR_DODGE:\n    case GSHSCENE_COMPOSITE_COLOR_BURN:\n    case GSHSCENE_COMPOSITE_HARD_LIGHT:\n    case GSHSCENE_COMPOSITE_SOFT_LIGHT:\n    case GSHSCENE_COMPOSITE_DIFFERENCE:\n    case GSHSCENE_COMPOSITE_EXCLUSION:\n    case GSHSCENE_COMPOSITE_MULTIPLY:\n    case GSHSCENE_COMPOSITE_HUE:\n    case GSHSCENE_COMPOSITE_SATURATION:\n    case GSHSCENE_COMPOSITE_COLOR:\n    case GSHSCENE_COMPOSITE_LUMINOSITY:\n      return 1;', RT),
    ('r-b-blends-predicate', "src", RC, 'return op >= GSHSCENE_COMPOSITE_SCREEN && op <= GSHSCENE_COMPOSITE_LUMINOSITY;', 'return op >= GSHSCENE_COMPOSITE_SCREEN && op < GSHSCENE_COMPOSITE_LUMINOSITY;', RT),
    ('r-b-apply-case', "src", RC, "    case GSHSCENE_COMPOSITE_LUMINOSITY:\n      /* B2, inside N5's apply unchanged. */", "      /* B2, inside N5's apply unchanged. */", RT),
    ('r-b-apply-weights', "src", RC, '        d[i] = gshcpu_render_saturate((1.0 - m) * d[i] + m * o[i]); /* N1 */\n      }\n      return 1;', '        d[i] = gshcpu_render_saturate(d[i] + m * o[i]); /* N1 */\n      }\n      return 1;', RT),
    ('r-caps-blend-modes', "src", RS, '  for (uint32_t c = GSHSCENE_COMPOSITE_SCREEN; c <= GSHSCENE_COMPOSITE_LUMINOSITY; c++) {', '  for (uint32_t c = GSHSCENE_COMPOSITE_SCREEN; c <= GSHSCENE_COMPOSITE_MULTIPLY; c++) {', RT),
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
    ("r-store-straight", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : gshcpu_render_unpremultiply(w[i], w[3]);", "      w[i] = w[3] == 0.0 ? 0.0 : w[i];", RT),
    ("r-store-straight-zero", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : gshcpu_render_unpremultiply(w[i], w[3]);", "      w[i] = gshcpu_render_unpremultiply(w[i], w[3]);", RT),
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
    ("r-cover-cells-limit", "src", RV, "  if (c->cells >= c->max_cells) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (c->ex) {", "  if (c->cells > c->max_cells) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (c->ex) {", RT),
    ("r-cover-fold-cells-limit", "src", RV, "  if (c->cells >= c->max_cells) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (!c->cover) {\n    return; /* counted, not drawn */", "  if (c->cells > c->max_cells) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (!c->cover) {\n    return; /* counted, not drawn */", RT),
    ("r-cover-sweep-scale", "src", RV, "      int64_t accumulated = cv[0] * (ONE_PIXEL * 2);\n      for (int64_t j = 1; j <= width; j++) {\n        accumulated += cv[j] * (ONE_PIXEL * 2);\n        line[j - 1] = fold(accumulated - ar[j], rule, ex != NULL);", "      int64_t accumulated = cv[0] * ONE_PIXEL;\n      for (int64_t j = 1; j <= width; j++) {\n        accumulated += cv[j] * (ONE_PIXEL * 2);\n        line[j - 1] = fold(accumulated - ar[j], rule, ex != NULL);", RT),
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
    ("r-work-bound", "src", RS, '!gcu_safe_mul_u64((uint64_t)cs->max_bytes, 2u, &pixels)', '!gcu_safe_mul_u64((uint64_t)cs->max_bytes, 1u, &pixels)', RT),
    ("r-region-ceiling", "src", RS, "L->region.x1 > (int64_t)GSHCPU_MAX_DIMENSION_CEILING", "L->region.x1 > (int64_t)GSHCPU_MAX_DIMENSION_CEILING * 0", RT),
    ("r-region-max-bytes", "src", RS, "  if (L->pixels > cs->max_bytes) {", "  if (L->pixels > cs->max_bytes + 1) {", RT),
    ("r-default-cells", "src", RS, "limits->max_cells : GSHCPU_LIMITS_DEFAULT_MAX_CELLS;", "limits->max_cells : 1000;", RT),
    ("r-default-dimension-ceiling", "src", RS, "    cs->max_dimension = GSHCPU_MAX_DIMENSION_CEILING;", "    cs->max_dimension = 16;", RT),
    ("r-foreground-alpha", "src", RS, "gshscene_list_foreground(request->list, &run->foreground) != GSHSCENE_OK",
     "(gshscene_color_init(&run->foreground), GSHSCENE_OK) != GSHSCENE_OK", RT),
    # Story 14c: a palette fill takes the list's palette, in the paint's resolution and in the sink's disposition.
    ("r-palette-resolve", "src", RS, "gshscene_paint_resolve(&item->paint, &run->foreground, run->palette, &color)",
     "gshscene_paint_resolve(&item->paint, &run->foreground, NULL, &color)", RT),
    ("r-palette-disposition", "src", RS, "&run->job->target.map, gshscene_job_target_space(run->job), run->palette);",
     "&run->job->target.map, gshscene_job_target_space(run->job), NULL);", RT),
    ("r-palette-taken", "src", RS, "  run->palette = gshscene_list_palette(request->list);\n", "", RT),
    ("r-paint-target-space", "src", RS, "    space = *gshscene_job_target_space(run->job);", "    space = frame->space;", RT),
    ("r-group-space-inherit", "src", RS, "? parent->space : item->group.blend_space;", "? (parent ? *gshscene_job_target_space(run->job) : parent->space) : item->group.blend_space;", RT),
    ("r-group-opacity", "src", RS, "        s[i] = o * s[i];", "        s[i] = o * o * s[i];", RT),
    ("r-group-convert-way", "src", RS, "gshcpu_render_convert_prepare(&c, &f->space, &parent->space);", "gshcpu_render_convert_prepare(&c, &parent->space, &f->space);", RT),
    ("r-group-outer-clips", "src", RS, "uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);", "uint32_t m8 = 255u;", RT),
    ("r-group-unbounded-region", "src", RS, "GSHCPU_Rect region = bounded ? f->dirty : run->layout.region;", "GSHCPU_Rect region = f->dirty;", RT),
    ("r-fill-weight", "src", RS, '      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], src, (double)m8 / 255.0, GSHSCENE_COMPOSITE_SRC_OVER);', '      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], src, (double)m8 / 256.0, GSHSCENE_COMPOSITE_SRC_OVER);', RT),
    ("rgr-fill-weight", "src", RS, '      double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};\n      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 255.0,', '      double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};\n      size_t p = at(run, x, y);\n      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 256.0,', ["testRender_gradient"] + RT),
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
    ("rule-S2-caps-instances", "src", RS, '(UINT32_C(1) << GSHSCENE_ITEM_GROUP_POP) | (UINT32_C(1) << GSHSCENE_ITEM_INSTANCE) |\n      (UINT32_C(1) << GSHSCENE_ITEM_STROKE);', '(UINT32_C(1) << GSHSCENE_ITEM_GROUP_POP) |\n      (UINT32_C(1) << GSHSCENE_ITEM_STROKE);', CONF),
    # Strokes story S1: the item kinds named, so a kind shape-scene adds (the stroke, 7) is refused, never claimed: a
    # claim of the kind after instance must fail, and so must the loop up to the enum's end that the names replaced.
    ("r-caps-kinds-loop", "src", RS,
     "  out->item_kinds = (UINT32_C(1) << GSHSCENE_ITEM_FILL) | (UINT32_C(1) << GSHSCENE_ITEM_CLIP_PUSH) |",
     "  for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {\n    out->item_kinds |= UINT32_C(1) << k;\n  }\n"
     "  out->item_kinds |= (UINT32_C(1) << GSHSCENE_ITEM_FILL) | (UINT32_C(1) << GSHSCENE_ITEM_CLIP_PUSH) |", RT),
    ("r-caps-kinds-grown", "src", RS,
     "  out->item_kinds = (UINT32_C(1) << GSHSCENE_ITEM_FILL) | (UINT32_C(1) << GSHSCENE_ITEM_CLIP_PUSH) |",
     "  out->item_kinds = (UINT32_C(1) << (GSHSCENE_ITEM_INSTANCE + 1)) | (UINT32_C(1) << GSHSCENE_ITEM_FILL) |\n"
     "      (UINT32_C(1) << GSHSCENE_ITEM_CLIP_PUSH) |", RT),
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
    ("rgr-notice-drawn", "src", RS, '          : gshscene_gradient_notice(item, run->maps, count - 1, &run->job->target.map)) {', '          : 0) {', GRT),
    ("rgr-sample-corner", "src", RS, "(double)(int32_t)x + 0.5, (double)(int32_t)y + 0.5, color)", "(double)(int32_t)x, (double)(int32_t)y, color)", GRT),
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
    ("rgr-unpremultiplied-not", "src", RGR, "      c[k] = a == 0.0 ? 0.0 : gshcpu_render_unpremultiply(c[k], a);", "      c[k] = a == 0.0 ? 0.0 : c[k];", GRT),
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

# N1's saturation (shape-scene's paint.h; Corey, 2026-10-21): the clamp's two sides, the un-premultiply's guard and
# sign, and each point it is named at (each step of a conversion, a group's un-premultiply, every apply, a straight
# store, G4's un-premultiply).
SAT = ["testRender_parts", "testRender_vectors", "testRender_gradient"]
PLANTS += [
    ("rn1-sat-upper", "src", RC, "  return x > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION\n", "  return x > GSHSCENE_COLOR_SATURATION ? x\n", SAT),
    ("rn1-sat-lower", "src", RC, "      : x < -GSHSCENE_COLOR_SATURATION ? -GSHSCENE_COLOR_SATURATION : x;", "      : x < -GSHSCENE_COLOR_SATURATION ? x : x;", SAT),
    ("rn1-unpremultiply-guard", "src", RC, "  if (mc >= GSHSCENE_COLOR_SATURATION * ma) {", "  if (mc >= GSHSCENE_COLOR_SATURATION * ma && 0) {", SAT),
    ("rn1-unpremultiply-inclusive", "src", RC, "  if (mc >= GSHSCENE_COLOR_SATURATION * ma) {", "  if (mc > 2.0 * GSHSCENE_COLOR_SATURATION * ma) {", SAT),
    ("rn1-unpremultiply-sign", "src", RC, "    return (c < 0.0) != (a < 0.0) ? -GSHSCENE_COLOR_SATURATION", "    return (c < 0.0) ? -GSHSCENE_COLOR_SATURATION", SAT),
    ("rn1-decode", "src", RC, "    lin[i] = gshcpu_render_saturate(gshcpu_render_decode(&c->from, in[i]));", "    lin[i] = gshcpu_render_decode(&c->from, in[i]);", SAT),
    ("rn1-matrix", "src", RC, "    for (int i = 0; i < 3; i++) {\n      lin[i] = gshcpu_render_saturate(lin[i]);", "    for (int i = 0; i < 0; i++) {\n      lin[i] = gshcpu_render_saturate(lin[i]);", SAT),
    ("rn1-encode", "src", RC, "    out[i] = gshcpu_render_saturate(gshcpu_render_encode(&c->to, lin[i]));", "    out[i] = gshcpu_render_encode(&c->to, lin[i]);", SAT),
    ("rn1-group", "src", RC, "  double straight[3] = {gshcpu_render_unpremultiply(in[0], a), gshcpu_render_unpremultiply(in[1], a),\n      gshcpu_render_unpremultiply(in[2], a)};", "  double straight[3] = {in[0] / a, in[1] / a, in[2] / a};", SAT),
    ("rn1-apply", "src", RC, "  for (int i = 0; i < 4; i++) {\n    d[i] = gshcpu_render_saturate((1.0 - m) * d[i] + m * o[i]); /* N1 */", "  for (int i = 0; i < 4; i++) {\n    d[i] = (1.0 - m) * d[i] + m * o[i]; /* N1 */", SAT),
    ("rn1-store", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : gshcpu_render_unpremultiply(w[i], w[3]); /* N1 */", "      w[i] = w[3] == 0.0 ? 0.0 : w[i] / w[3]; /* N1 */", SAT),
    ("rn1-g4", "src", RGR, "      c[k] = a == 0.0 ? 0.0 : gshcpu_render_unpremultiply(c[k], a); /* N1: saturated */", "      c[k] = a == 0.0 ? 0.0 : c[k] / a; /* N1: saturated */", SAT),
]

# --- strokes story S4 (SC1 to SC11): written with the story, swept in the consolidated pass -----------------------------
RSK = "src/render/stroke.c"
RSC = "src/render/stroke_cache.c"
RST = ["testRender_stroke"]
RSTT = ["testRender_stroke", "testRender"]

PLANTS += [
    # SC5: the composition of three coverages
    ("sc5-complement-ignored", "src", RSK, "  uint32_t cp = complement ? 255u - c : c;", "  (void)complement;\n  uint32_t cp = c;", RST),
    ("sc5-complement-inverted", "src", RSK, "  uint32_t cp = complement ? 255u - c : c;", "  uint32_t cp = complement ? c : 255u - c;", RST),
    ("sc5-weigh-unrounded", "src", RSK, "  uint32_t t = gshcpu_render_weigh(a, cp); /* D8: (a * c' + 127) / 255 */", "  uint32_t t = (a * cp) / 255u;", RST),
    ("sc5-union-is-sum", "src", RSK, "  return 255u - ((255u - b) * (255u - t) + 127u) / 255u;", "  return b + t > 255u ? 255u : b + t;", RST),
    ("sc5-union-is-max", "src", RSK, "  return 255u - ((255u - b) * (255u - t) + 127u) / 255u;", "  return b > t ? b : t;", RST),
    ("sc5-union-unrounded", "src", RSK, "  return 255u - ((255u - b) * (255u - t) + 127u) / 255u;", "  return 255u - ((255u - b) * (255u - t)) / 255u;", RST),
    ("sc5-aligned-unclipped", "src", RSK, "          (uint8_t)gshcpu_render_stroke_compose(b, a, c, complement);", "          (uint8_t)gshcpu_render_stroke_compose(b, a, 255u + 0u * c, 0);", RST),
    ("sc5-clip-rule-ignored", "src", RSK, "    GSHGEOM_Fill_Rule rule = p == 2 ? gshgeom_stroke_outline_clip_rule(s->outline) : GSHGEOM_FILL_NONZERO;", "    GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO;", RST),
    ("sc5-clip-rect-is-body", "src", RSK, "  clip.rect = aligned.rect; /* c over the aligned part's rectangle (SC5) */", "  clip.rect = body.rect;", RST),
    ("sc5-complement-flag-dropped", "src", RSK, "  int complement = gshgeom_stroke_outline_clip_complement(s->outline);", "  int complement = 0;", RST),
    ("sc5-aligned-dropped", "src", RSK, "  if (!aligned.path) {\n    *rect = body.rect;\n    *k = buffers->body;", "  if (1) {\n    *rect = body.rect;\n    *k = buffers->body;", RST),
    ("sc5-body-dropped", "src", RSK, "        b = buffers->body[(size_t)(y - body.rect.y0) * bw + (size_t)(x - body.rect.x0)];", "        b = (uint8_t)(0u * bw);", RST),
    ("sc5-joint-is-body", "src", RSK, "  GSHCPU_Rect joint = gshcpu_render_rect_join(body.rect, aligned.rect);\n  if (gshcpu_render_rect_empty(joint)) {\n    joint.x1 = joint.x0;\n    joint.y1 = joint.y0;\n  }\n  size_t bw", "  GSHCPU_Rect joint = body.rect;\n  if (gshcpu_render_rect_empty(joint)) {\n    joint.x1 = joint.x0;\n    joint.y1 = joint.y0;\n  }\n  size_t bw", RST),
    ("uc8-joint-is-body", "src", RSK, "    GSHCPU_Rect joint = gshcpu_render_rect_join(body.rect, aligned.rect);\n    if (gshcpu_render_rect_empty(joint)) {\n      joint.x1 = joint.x0;\n      joint.y1 = joint.y0;\n    }\n    GSHCPU_Rect moved_joint", "    GSHCPU_Rect joint = body.rect;\n    if (gshcpu_render_rect_empty(joint)) {\n      joint.x1 = joint.x0;\n      joint.y1 = joint.y0;\n    }\n    GSHCPU_Rect moved_joint", RST),
    # SC3: the canonical translation
    ("sc3-shift-x-dropped", "src", RSK, "  s.shift_x = sx;", "  s.shift_x = 0;", RSTT),
    ("sc3-shift-y-sign", "src", RSK, "  s.shift_y = sy;", "  s.shift_y = -sy;", RSTT),
    ("sc3-moved-x-sign", "src", RSK, "  r.x0 -= dx;", "  r.x0 += dx;", RSTT),
    ("sc3-moved-one-axis", "src", RSK, "  r.y0 -= dy;\n  r.x1 -= dx;\n  r.y1 -= dy;", "  r.x1 -= dx;\n  (void)dy;", RSTT),
    ("sc3-box-shift-dropped", "src", RSK, "      int64_t x0 = s.meta.lo_x[p] + 64 * sx;", "      int64_t x0 = s.meta.lo_x[p];", RST),
    ("sc3-coordinate-limit-y", "src", RSK, "          y1 > GSHCPU_RENDER_COORD_LIMIT) {\n        r = GSHCPU_ERR_LIMIT;", "          0) {\n        r = GSHCPU_ERR_LIMIT;", RST),
    ("sc3-canonical-map-unused", "src", RSK, "  g = gshgeom_stroke_expand(path, &params, &canon, alloc, limits, &outline);", "  g = gshgeom_stroke_expand(path, &params, composed, alloc, limits, &outline);", RSTT),
    # SC7: the key and the cache
    ("sc7-key-path", "src", RSK, "  key.path_id = gshscene_resource_id(item->resource);", "  key.path_id = 0;", RST),
    ("sc7-key-style", "src", RSK, "  key.style_id = gshscene_resource_id(item->style);", "  key.style_id = 0;", RST),
    ("sc7-key-linear", "src", RSK, "    key.linear[i] = gshgeom_f64_bits(canon.m[i / 2][i % 2]);", "    key.linear[i] = 0;", RST),
    ("sc7-key-fraction-x", "src", RSK, "  key.fraction[0] = gshgeom_f64_bits(canon.t[0]);", "  key.fraction[0] = 0;", RST),
    ("sc7-key-fraction-y", "src", RSK, "  key.fraction[1] = gshgeom_f64_bits(canon.t[1]);", "  key.fraction[1] = 0;", RST),
    ("sc7-key-revision", "src", RSK, "  key.revision = GSHGEOM_STROKE_REVISION;", "  key.revision = 0;", RST),
    ("sc7-hit-limit-unchecked", "src", RSK, "    if (s.meta.present[p] && limits && s.meta.points[p] > (uint64_t)limits->max_points && limits->max_points != 0) {", "    if (0) {", RST),
    ("sc7-cache-not-pinned", "src", RSC, "      e->refs++;\n      /* To the head of the recency list. */", "      /* To the head of the recency list. */", RST),
    ("sc7-evict-pinned", "src", RSC, "      if (e->refs == 0) {\n        unlink_entry(c, e);", "      if (1) {\n        unlink_entry(c, e);", RST),
    ("sc7-bound-ignored", "src", RSC, "  Entry * fresh = need <= c->bound ?", "  Entry * fresh = 1 ?", RST),
    ("sc7-evict-path-only", "src", RSC, "      if (e->key.path_id == id || e->key.style_id == id) {", "      if (e->key.path_id == id) {", RST),
    ("sc7-evict-style-only", "src", RSC, "      if (e->key.path_id == id || e->key.style_id == id) {", "      if (e->key.style_id == id) {", RST),
    ("sc7-evicted-served", "src", RSC, "  e->listed = 0;\n  c->held -= e->meta.bytes;", "  e->listed = 1;\n  c->held -= e->meta.bytes;", RST),
    ("sc7-held-not-reduced", "src", RSC, "  c->held -= e->meta.bytes;", "  (void)e;", RST),
    ("sc7-held-not-added", "src", RSC, "      c->held += need;", "      (void)need;", RST),
    ("sc7-oom-swallowed", "src", RSC, "  if (need <= c->bound && !fresh) {\n    return -1;\n  }", "  if (0) {\n    return -1;\n  }", ["testRender_limits"]),
    ("sc7-lru-tail-first-reversed", "src", RSC, "    Entry * e = c->tail;\n    while (c->held > room && e) {\n      Entry * newer = e->newer;", "    Entry * e = c->head;\n    while (c->held > room && e) {\n      Entry * newer = e->older;", RST),
    # SC8: the sparse tier
    ("sc8-sparse-denominator", "src", RSK, "  uint64_t limit = pixels / 16u;", "  uint64_t limit = pixels / 2u;", RST),
    ("sc8-sparse-aligned", "src", RSK, "  if (!s->drawn || s->meta.present[GSHCPU_STROKE_ALIGNED]) {\n    return 0;", "  if (!s->drawn) {\n    return 0;", RST),
    ("sc8-hairline-width", "src", RSK, "params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE && params.width == 1.0;", "params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE;", RST),
    ("sc8-hairline-space", "src", RSK, "params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE && params.width == 1.0;", "params.width == 1.0;", RST),
    ("sc8-sparse-tail", "src", RV, "    if (tail != 0 && last < width) {", "    if (0) {", RST),
    ("sc8-sparse-first-column", "src", RV, "    int64_t j0 = lo > 1 ? lo : 1;", "    int64_t j0 = lo > 2 ? lo : 2;", RST),
    ("sc8-sparse-not-cleared", "src", RV, "    for (int64_t t = lo; t <= hi && t <= width; t++) {\n      cv[t] = 0;\n      ar[t] = 0;\n    }", "", RST),
    ("sc8-sparse-skips-a-span", "src", RV, "    if (last >= j0) {\n      fn(ctx, c->band + r,", "    if (last >= j0 + 1) {\n      fn(ctx, c->band + r,", RST),
    # SC4, SC6, SC11: what draws nothing, the limits and the work
    ("sc4-stroke-notice-skipped", "src", RS, "          ? gshscene_stroke_notice(item, run->maps, count - 1, &run->job->target.map)", "          ? (GSHSCENE_Reason)0", RST),
    ("sc6-limit-not-refused", "src", RS, "  if (opened == GSHCPU_ERR_LIMIT) {\n    return refuse(run, item, chain, count);\n  }", "  if (opened == GSHCPU_ERR_LIMIT) {\n    return GSHSCENE_OK;\n  }", RST),
    ("sc6-compose-limit", "src", RS, "    return refuse(run, item, chain, count); /* SC4: a composed map that cannot be formed is the sink's own limit */", "    return GSHSCENE_OK;", RST),
    ("sc6-cells-not-summed", "src", RSK, "out, acc, max_cells - total, &used, p == GSHCPU_STROKE_CLIP ? NULL : ex);", "out, acc, max_cells, &used, p == GSHCPU_STROKE_CLIP ? NULL : ex);", RST),
    ("sc6-whole-count-dropped", "src", RSK, '    if (!same_rect(parts[p].rect, walks[p]) && !whole_count && !may_exceed(s, p, exact)) {\n      walks[p] = parts[p].rect; /* the caps cannot', '    if (1 || (!same_rect(parts[p].rect, walks[p]) && !whole_count && !may_exceed(s, p, exact))) {\n      walks[p] = parts[p].rect; /* the caps cannot', RST),
    ("sc9-caller-memory-reads-cache", "src", RS, "  GSHCPU_Stroke_Cache * cache = !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;", "  GSHCPU_Stroke_Cache * cache = (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;", RST),
    ("sc11-expansion-uncounted", "src", RS, "  run->work += expansion_work(run, item, &s);", "", RST),
    ("sc11-bound-pixels", "src", RS, "!gcu_safe_mul_u64((uint64_t)cs->max_bytes, 2u, &pixels)", "!gcu_safe_mul_u64((uint64_t)cs->max_bytes, 1u, &pixels)", ["testRender", "testRender_limits"]),
    ("sc1-caps-no-expand", "src", RS, "  out->flags |= GSHSCENE_CAP_STROKE_EXPAND;", "", ["testRender", "testRender_conformance"]),
    ("sc1-caps-no-kind", "src", RS, "      (UINT32_C(1) << GSHSCENE_ITEM_STROKE);", "      0;", ["testRender", "testRender_conformance"]),
    ("sc5-aligned-buffers-always", "src", RS, "  int ok = place(&at, sizeof(Run), &ignored)", "  L->aligned = 0;\n  int ok = place(&at, sizeof(Run), &ignored)", RST),
]


# --- strokes story S5 (ST1, ST8 to ST11): stroked text; written with the story, swept in the consolidated pass -----------
RTX = ["testRender_stroke_text"]
RTXC = ["testRender_stroke_text", "testRender"]

PLANTS += [
    # ST8: the notes of the construction, kept with the expansion and replayed by a hit
    ("st8-notes-uncollected", "src", RSK, "    n += notes[i].entry_class != 0;", "    n += 0;", RTX),
    ("st8-note-index", "src", RSK, "      records[at].index = i;", "      records[at].index = at;", RTX),
    ("st8-note-reason", "src", RSK, "      records[at].reason = notes[i].reason;", "      records[at].reason = GSHSCENE_REASON_RESOURCE_LIMIT;", RTX),
    ("st8-note-class", "src", RSK, "      records[at].entry_class = notes[i].entry_class;", "      records[at].entry_class = GSHSCENE_ENTRY_NOTICE;", RTX),
    ("st8-notes-not-kept", "src", RSK, "    s.meta.note_count = note_count;\n    s.meta.notes = made;", "    s.meta.note_count = 0;\n    s.meta.notes = NULL;", RTX),
    ("st8-hit-without-notes", "src", RSK, "    s.notes = copy;\n    s.note_count = s.meta.note_count;", "    alloc->free_fn(alloc->ctx, copy);", RTX),
    ("st8-run-elements-lost", "src", RSK, "      s.meta.run_elements = gshgeom_path_element_count(built);", "      s.meta.run_elements = 0;", RTX),
    ("st8-run-elements-not-kept", "src", RSK, "    s.meta.run_elements = run_elements;", "    s.meta.run_elements = 0 * run_elements;", RTX),
    ("st8-limit-not-the-items", "src", RSK, "return cr == GSHSCENE_ERR_OOM ? GSHCPU_ERR_OOM : cr == GSHSCENE_ERR_LIMIT ? GSHCPU_ERR_LIMIT : GSHCPU_ERR_INTERNAL;", "return cr == GSHSCENE_ERR_OOM ? GSHCPU_ERR_OOM : GSHCPU_ERR_INTERNAL;", RTX),
    ("st8-centerline-from-the-path", "src", RSK, "      path = built;\n    }", "    }", RTX),
    ("st8-nothing-drawn-notes-dropped", "src", RS, "  GSHSCENE_Result noted = report_run_notes(run, &s, chain, count);\n  if (noted != GSHSCENE_OK) {", "  GSHSCENE_Result noted = s.drawn ? report_run_notes(run, &s, chain, count) : GSHSCENE_OK;\n  if (noted != GSHSCENE_OK) {", RTX),
    ("st8-notes-unreported", "src", RS, "  for (size_t i = 0; i < s->note_count; i++) {\n    const GSHCPU_Run_Note * n = &s->notes[i];", "  for (size_t i = 0; 0 && i < s->note_count; i++) {\n    const GSHCPU_Run_Note * n = &s->notes[i];", RTX),
    ("st8-note-glyph", "src", RS, "(GSHSCENE_Reason)n->reason, chain, count, (size_t)n->index);", "(GSHSCENE_Reason)n->reason, chain, count, 0);", RTX),
    ("st8-strict-continues", "src", RS, "(run->request->flags & GSHSCENE_RENDER_STRICT)) {\n      memcpy(run->stop, chain, count * sizeof *chain);\n      run->stop_count = count;\n      e = GSHSCENE_ERR_REFUSED;\n    }\n    if (e != GSHSCENE_OK) {\n      return e;", "0) {\n      memcpy(run->stop, chain, count * sizeof *chain);\n      run->stop_count = count;\n      e = GSHSCENE_ERR_REFUSED;\n    }\n    if (e != GSHSCENE_OK) {\n      return e;", RTX),
    ("st8-notes-not-copied", "src", RSC, "    memcpy(notes, meta->notes, meta->note_count * sizeof *notes);", "    memset(notes, 0, meta->note_count * sizeof *notes);", RTX),
    ("st8-notes-not-owned-by-the-entry", "src", RSC, "      fresh->meta.notes = notes;", "      fresh->meta.notes = meta->notes;", RTX),
    # ST8, ST9: the caps, the measure and the work
    ("st8-caps-no-runs", "src", RS, "  out->flags |= GSHSCENE_CAP_STROKE_RUNS;", "", RTXC),
    ("st8-measure-skips-runs", "src", RS, "      if (gshscene_stroke_notice(item, maps, count - 1, &target->map)) {\n        continue; /* a stroke that draws nothing */", "      if (!gshscene_resource_path(item->resource) || gshscene_stroke_notice(item, maps, count - 1, &target->map)) {\n        continue; /* a stroke that draws nothing */", RTX),
    ("st9-work-glyph-elements-dropped", "src", RS, "  return points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + (path ? 0u : elements);", "  return points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH;", RTX),
    ("st9-work-glyph-elements-for-paths", "src", RS, "  return points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + (path ? 0u : elements);", "  return points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + elements;", RTX),
    ("st9-work-run-elements-unclamped", "src", RS, "  uint64_t elements = path ? (uint64_t)gshgeom_path_element_count(path) : s->run_elements;\n  if (elements > run->cs->elements) {", "  uint64_t elements = path ? (uint64_t)gshgeom_path_element_count(path) : s->run_elements;\n  if (0) {", RTX),
    ("st9-bound-without-glyph-term", "src", RS, "(uint64_t)GSHGEOM_STROKE_MAX_DEPTH + 1u, &expansion)", "(uint64_t)GSHGEOM_STROKE_MAX_DEPTH, &expansion)", ["testRender", "testRender_limits"]),
]

# --- strokes story S4b (UC1 to UC12): written with the story, swept in the consolidated pass ------------------------------
RX = "src/render/exact.c"
UCX = ["testRender_exact"]
UCB = ["testRender_stroke_bracket"]
UCXB = ["testRender_exact", "testRender_stroke_bracket"]
UCD = ["testRender_dense"]
UCXD = ["testRender_dense", "testRender_exact", "testRender_dual"]

PLANTS += [
    # UC4: the winding at the cell's left side
    ("uc4-fold-column-left-out", "src", RV, "      int64_t sum = cv[0];\n      for (int64_t j = 1; j <= width; j++) {\n        int64_t left = sum;", "      int64_t sum = 0;\n      for (int64_t j = 1; j <= width; j++) {\n        int64_t left = sum;", UCX),
    ("uc4-fold-column-left-out-aligned", "src", RV, "          row[0] = cv[0];", "          row[0] = 0;", UCX),
    # UC5: slabs, order, winding
    # UC6: the shared area
    ("uc7-fold-signed", "src", RV, "  if (magnitude) {\n    uint64_t m = (uint64_t)(area < 0 ? -area : area);", "  if (0 && magnitude) {\n    uint64_t m = (uint64_t)(area < 0 ? -area : area);", UCXB),
    ("uc7-even-odd-ramp-dropped", "src", RV, "      if (m > 8192u) {\n        m = 16384u - m;\n      }", "", UCXB),
    ("uc7-even-odd-period", "src", RV, "      m &= 16383u; /* 2 * 2^13", "      m &= 32767u; /* 2 * 2^13", UCXB),
    ("uc7-shared-not-subtracted", "src", RV, "        int64_t value = dense - shared;\n        if (value < 0) {\n          value = 0;\n        }\n        ar[j] = accumulated - ex->eps * value;", "        int64_t value = dense;\n        if (value < 0) {\n          value = 0;\n        }\n        ar[j] = accumulated - ex->eps * value;", UCX),
    # UC2, UC3: pieces and which cells
    ("uc2-cut-from-upper-end-dropped", "src", RV, "  if (c->ex && from.y > to.y) {", "  if (0) {", UCXB),
    ("uc2-flat-pieces-not-counted", "src", RV, "    if (c->ex && piece_kind(ex, a, b) == 2) {", "    if (c->ex && piece_kind(ex, a, b) == 3) {", UCX),
    ("uc2-flat-piece-any-touch", "src", RV, "  return a.x == left || b.x == left ? 2 : 0;", "  return a.x == left ? 2 : 0;", UCX),
    ("uc2-flat-edges-not-walked", "src", RV, "    if (c->ex) {\n      flat_edge(c, from, to);\n    }", "", UCX),
    ("uc3-exact-threshold", "src", RV, "      if (n >= 2) {\n        if (kept + n > ex->pool_cap) {", "      if (n >= 3) {\n        if (kept + n > ex->pool_cap) {", UCX),
    ("uc3-window-unbounded", "src", RV, "        if (kept + n > ex->pool_cap) {\n          break;\n        }", "", UCX),
    ("uc3-sparse-exact-cell-not-visited", "src", RV, "  if (*n == 2 && c->track && c->cover && ex >= c->rect.x0 && ex < c->rect.x1) {", "  if (0) {", UCX),
    # UC8: parts that share a pixel
    ("uc8-shared-cell-threshold", "src", RV, "          if (jc.multi >= 2) {\n            GSHCPU_Exact_Cell cell;", "          if (jc.multi >= 3) {\n            GSHCPU_Exact_Cell cell;", UCX),
    ("uc8-pieces-kept-threshold", "src", RV, "    out->store[p] = out->n[p] > 0 && (out->multi >= 2 || out->n[p] >= 2);", "    out->store[p] = out->n[p] > 0 && out->n[p] >= 2;", UCX),
    ("uc8-union-value-ignored", "src", RV, "        result[row + (size_t)j] = k != 0xffffu ? (uint8_t)k\n            : (uint8_t)", "        result[row + (size_t)j] = 0 ? (uint8_t)k\n            : (uint8_t)", UCX),
    ("uc8-clip-overlap-not-exact", "src", RV, "          } else if (jc.n[2] >= 2) {", "          } else if (0) {", UCX),
    ("uc8-complement-flag-dropped", "src", RV, "  three.complement = complement;", "  three.complement = 0;", UCX),
    ("uc8-clip-rule-dropped", "src", RV, "  three.even_odd = in[2].rule == GSHGEOM_FILL_EVEN_ODD;", "  three.even_odd = 0;", UCX),
    ("uc8-joint-step-skipped", "src", RSK, "  if (aligned.path && ex && joint_tier) {", "  if (0) {", UCB),
    ("uc8-clip-fold-signed", "src", RV, "          line[j - 1] = fold(accumulated - ar[j], in[p].rule, 1);", "          line[j - 1] = fold(accumulated - ar[j], in[p].rule, p < 2);", UCXB),
    # UC9: the cap, the field, the refusal
    ("uc9-cap-off-by-one", "src", RV, "  if ((uint64_t)*n >= c->ex->max_pieces) {", "  if ((uint64_t)*n > c->ex->max_pieces) {", UCX),
    ("uc9-cap-falls-back-to-dense", "src", RV, "  if ((uint64_t)*n >= c->ex->max_pieces) {\n    c->over = 1;\n    return;\n  }", "  if ((uint64_t)*n >= c->ex->max_pieces) {\n    return;\n  }", UCX),
    ("uc9-damage-refusal-differs", "src", RSK, '  if (edges > ex->max_pieces) {\n    return 1;\n  }', '  if (0 && edges > ex->max_pieces) {\n    return 1;\n  }', UCX),
    ("uc9-field-ignored", "src", RS, "  cs->max_exact = limits && limits->max_exact_fragments ? limits->max_exact_fragments", "  cs->max_exact = 0 ? limits->max_exact_fragments", UCX),
    ("uc9-field-ceiling-off-by-one", "src", RS, "  if (limits && limits->max_exact_fragments > GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS) {", "  if (limits && limits->max_exact_fragments >= GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS) {", UCX),
    ("uc9-refusal-swallowed", "src", RS, "  if (r == GSHCPU_ERR_LIMIT) {\n    result = refuse(run, item, chain, count);", "  if (r == GSHCPU_ERR_LIMIT) {\n    result = GSHSCENE_OK;", UCX),
    ("uc9-work-uncounted", "src", RS, "  run->work += cells + exact_work(run);", "  run->work += cells;", ["testRender_stroke"]),
    ("uc9-work-bound-uncounted", "src", RS, "  if (!gcu_safe_add_u64(b, exact, &b)) {\n    return UINT64_MAX;\n  }", "  (void)exact;", ["testRender", "testRender_limits"]),
    ("uc9-workspace-short", "src", RS, "    size_t first = L->strokes ? gshcpu_exact_bytes(width, cs->max_exact, L->aligned ? 3 : 1) : 0;", "    size_t first = L->strokes ? gshcpu_exact_bytes(width, cs->max_exact, 1) : 0;", UCX),
    ("uc9-exact-counts-stale", "src", RV, "  memset(c->ex->count, 0, cells * sizeof *c->ex->count);", "", UCX),
    ("uc9-slots-stale", "src", RV, "  memset(c->ex->slot, 0xff, cells * sizeof *c->ex->slot);", "", UCX),
    # UC10: tiers; the exact step on every route
    ("uc10-sparse-exact-skipped", "src", RV, "    if (c.over || (ex && exact_band(&c, paths, count))) {\n      /* Leave the accumulator clean", "    if (c.over) {\n      /* Leave the accumulator clean", UCX),
    ("uc10-dense-exact-skipped", "src", RV, "    if (c.over || (ex && exact_band(&c, paths, count))) {\n      *cells = c.cells;\n      return GSHCPU_ERR_LIMIT;\n    }\n    for (int64_t r = 0; r < c.rows; r++) {\n      const int64_t * cv = c.cover + r * c.stride;\n      const int64_t * ar = c.area + r * c.stride;\n      uint8_t * line = out", "    if (c.over) {\n      *cells = c.cells;\n      return GSHCPU_ERR_LIMIT;\n    }\n    for (int64_t r = 0; r < c.rows; r++) {\n      const int64_t * cv = c.cover + r * c.stride;\n      const int64_t * ar = c.area + r * c.stride;\n      uint8_t * line = out", UCX),
]

# --- strokes story S4c (UC13 to UC16, UC19): the sweep, written with the story, swept in the consolidated pass ------------
PLANTS += [
    ("uc4-j-start-sign", "src", RX, '        *jstep(c, p, y0) -= (int32_t)mu;', '        *jstep(c, p, y0) += (int32_t)mu;', UCXD),
    ("uc4-j-end-sign", "src", RX, '        *jstep(c, p, y1) += (int32_t)mu;', '        *jstep(c, p, y1) -= (int32_t)mu;', UCXD),
    ("uc4-j-start-dropped", "src", RX, '      if (x0 == 0) {\n        *jstep(c, p, y0)', '      if (0) {\n        *jstep(c, p, y0)', UCXD),
    ("uc4-j-end-dropped", "src", RX, '      if (x1 == 0) {\n        *jstep(c, p, y1)', '      if (0) {\n        *jstep(c, p, y1)', UCXD),
    ("uc4-j-weight", "src", RX, '      sum += (int64_t)c->jnet[p][h] * (64 - h);', '      sum += (int64_t)c->jnet[p][h] * h;', UCXD),
    ("uc4-wtop-from-zero", "src", RX, "    c->wtop[p] = rem / 64;", "    c->wtop[p] = 0;", UCXD),
    ("uc4-left-cover-ignored", "src", RX, "    int64_t rem = cell->a_left[p] - sum;", "    int64_t rem = -sum + 0 * cell->a_left[p];", UCXD),
    ("uc4-remainder-unchecked", "src", RX, "    if (rem % 64 != 0) {\n      return GSHCPU_EXACT_IDENTITY;\n    }\n", "", UCXD),
    ("uc13-cancel-keeps-the-wrong-way", "src", RX, '      const uint32_t from = net > 0 ? hi : lo;\n      const uint32_t to = net > 0 ? lo : hi;', '      const uint32_t from = net > 0 ? lo : hi;\n      const uint32_t to = net > 0 ? hi : lo;', UCXD),
    ("uc13-reverse-sign", "src", RX, '        net += (keys[j] & 1u) ? 1 : -1;', '        net += (keys[j] & 1u) ? -1 : 1;', UCXD),
    ("uc13-multiplicity-ignored", "src", RX, "      uint32_t mu = (uint32_t)(net < 0 ? -net : net);", "      uint32_t mu = 1u;", UCXD),
    ("uc13-multiplicity-twice", "src", RX, "      uint32_t mu = (uint32_t)(net < 0 ? -net : net);", "      uint32_t mu = 2u * (uint32_t)(net < 0 ? -net : net);", UCXD),
    ("uc13-weighted-total-ignored", "src", RX, "      c->weighted += mu;", "      c->weighted += 1u;", UCXD),
    ("uc13-one-piece-shortcut-too-wide", "src", RX, "params->mode == GSHCPU_EXACT_OVERLAP && c.weighted <= 1u", "params->mode == GSHCPU_EXACT_OVERLAP && c.weighted <= 2u", UCXD),
    ("uc14-active-top", "src", RX, '  if ((c->endmk >> ya) & 1u) {\n    size_t out = 0;', '  if (0) {\n    size_t out = 0;', UCXD),
    ("uc14-active-bottom", "src", RX, '      if (s->frag[id].py + s->frag[id].dy > ya) {', '      if (s->frag[id].py + s->frag[id].dy >= ya) {', UCXD),
    ("uc14-ended-fragments-carried", "src", RX, "    if (s->frag[id].py + s->frag[id].dy > ya) {", "    if (s->frag[id].py + s->frag[id].dy >= ya) {", UCXD),
    ("uc14-repair-dropped", "src", RX, '      sort_top_short(s, c->ord, s->sc, c->n); /* a pair that met exactly at the top: the order just after is by slope */', '', UCXD),
    ("uc14-slope-tie-dropped", "src", RX, "  int64_t sp = (int64_t)p->dx * q->dy;\n  int64_t sq = (int64_t)q->dx * p->dy;\n  if (sp != sq) {\n    return sp < sq ? -1 : 1;\n  }\n", "", UCXD),
    ("uc14-slope-tie-reversed", "src", RX, "    return sp < sq ? -1 : 1;", "    return sp < sq ? 1 : -1;", UCXD),
    ("uc14-order-at-end-of-slab", "src", RX, "  int64_t xp = (int64_t)s->a[i] * q->dy;\n  int64_t xq = (int64_t)s->a[j] * p->dy;", "  int64_t xp = ((int64_t)s->a[i] + (int64_t)p->dx * 2) * q->dy;\n  int64_t xq = ((int64_t)s->a[j] + (int64_t)q->dx * 2) * p->dy;", UCXD),
    ("uc14-gap-step-sign", "src", RX, "      g[i].w[f->part] += pr->eps[f->part] * f->s * (int32_t)f->mu;", "      g[i].w[f->part] -= pr->eps[f->part] * f->s * (int32_t)f->mu;", UCXD),
    ("uc14-gap-step-multiplicity", "src", RX, "      g[i].w[f->part] += pr->eps[f->part] * f->s * (int32_t)f->mu;", "      g[i].w[f->part] += pr->eps[f->part] * f->s;", UCXD),
    ("uc14-mid-gap-multiplicity", "src", RX, "    mid.w[q->part] += pr->eps[q->part] * q->s * (int32_t)q->mu;", "    mid.w[q->part] += pr->eps[q->part] * q->s;", UCXD),
    ("uc14-meeting-direction", "src", RX, "  if (dd <= 0) {\n    return 0;\n  }", "  if (dd >= 0) {\n    return 0;\n  }", UCXD),
    ("uc14-meeting-at-the-end", "src", RX, "  if (nn <= 0 || nn >= c->height * dd) {", "  if (nn <= 0 || nn > c->height * dd) {", UCXD),
    ("uc14-heap-never-reinserts", "src", RX, "    if (pos > 0) {\n      event_set(c, pos - 1);\n    }\n    if (pos + 2 < n) {\n      event_set(c, pos + 1);\n    }\n", "", UCXD),
    ("uc14-left-neighbour-not-reinserted", "src", RX, "    if (pos > 0) {\n      event_set(c, pos - 1);\n    }\n", "", UCXD),
    ("uc14-right-neighbour-not-reinserted", "src", RX, "    if (pos + 2 < n) {\n      event_set(c, pos + 1);\n    }\n", "", UCXD),
    ("uc14-heap-order-reversed", "src", RX, "  return l < r || (l == r && a < b);", "  return l > r || (l == r && a < b);", UCXD),
    ("uc14-no-exchange", "src", RX, "    c->ord[pos] = iq;\n    c->ord[pos + 1] = ip;\n", "", UCXD),
    ("uc14-second-difference", "src", RX, "    int32_t dd = g[pos].om + g[pos + 2].om - g[pos + 1].om - mid.om;", "    int32_t dd = g[pos].om + g[pos + 2].om - g[pos + 1].om + mid.om;", UCXD),
    ("uc14-mid-gap-kept", "src", RX, "    g[pos + 1] = mid;\n", "", UCXD),
    ("uc15-sign-swapped", "src", RX, "      c->acc += (uint64_t)(int64_t)dd * (fp - fq);", "      c->acc += (uint64_t)(int64_t)dd * (fq - fp);", UCXD),
    ("uc15-f-at-slab-end-p", "src", RX, "      uint64_t fp = f30_at(s, ip, en, ed);", "      uint64_t fp = f30_end(s, ip, height);", UCXD),
    ("uc15-f-at-slab-end-q", "src", RX, "      uint64_t fq = f30_at(s, iq, en, ed);", "      uint64_t fq = f30_end(s, iq, height);", UCXD),
    ("uc15-meeting-depth-rounded", "src", RX, "      uint64_t fp = f30_at(s, ip, en, ed);\n      uint64_t fq = f30_at(s, iq, en, ed);", "      uint64_t tr = (en << 10) / ed;\n      uint64_t fp = f30_at(s, ip, tr, 1024u);\n      uint64_t fq = f30_at(s, iq, tr, 1024u);", UCXD),
    ("uc15-sentinel-dropped", "src", RX, "  c->acc += (uint64_t)((int64_t)64 * height * (int64_t)g[n].om) << 30;", "", UCXD),
    ("uc15-end-term-dropped", "src", RX, "      c->acc += (uint64_t)(int64_t)cf * f30_end(s, c->ord[i], height);\n", "", UCXD),
    ("uc15-overlap-weighted-by-depth", "src", RX, "    return w[0] != 0 ? 2 : 0;\n  }\n  if (p->mode == GSHCPU_EXACT_REGION) {", "    return w[0] < 0 ? -2 * w[0] : 2 * w[0];\n  }\n  if (p->mode == GSHCPU_EXACT_REGION) {", UCXD),
    ("uc15-tie-offset-dropped", "src", RX, "  uint64_t beta = 8u * c.events + 2u * c.end_evals + 1u;", "  uint64_t beta = 0u;", UCXD),
    ("uc15-shared-not-subtracted", "src", RX, "    *out = cell->dense[0] - up;", "    *out = up;", UCXD),
    ("uc15-no-half", "src", RX, "    int64_t t = v - ((int64_t)1 << 29) - (int64_t)beta;", "    int64_t t = v - (int64_t)beta;", UCXD),
    ("uc15-fraction-dropped", "src", RX, "  return (q << 30) + ((rm << 30) / den);", "  return (q << 30) + 0 * rm;", UCXD),
    ("uc15-quadratic-term-dropped", "src", RX, "  int64_t num = 2 * (int64_t)s->a[id] * (int64_t)n * (int64_t)d + (int64_t)f->dx * (int64_t)n * (int64_t)n;", "  int64_t num = 2 * (int64_t)s->a[id] * (int64_t)n * (int64_t)d;", UCXD),
    ("uc16-deposits-unchecked", "src", RX, "    if (verify && cell->dense[p] != params->eps[p] * (128 * (cell->a_left[p] + cover) - area)) {", "    if (0 && area && cover) {", UCXD),
    ("uc16-deposits-tolerant", "src", RX, "    if (verify && cell->dense[p] != params->eps[p] * (128 * (cell->a_left[p] + cover) - area)) {", "    if (verify && (cell->dense[p] - params->eps[p] * (128 * (cell->a_left[p] + cover) - area) > 1 ||\n                   cell->dense[p] - params->eps[p] * (128 * (cell->a_left[p] + cover) - area) < -1)) {", UCXD),
    ("uc19-budget-not-checked", "src", RX, "      if (total > limit) {", "      if (0 && limit) {", UCXD),
    ("uc19-budget-off-by-one", "src", RX, "      if (total > limit) {", "      if (total >= limit) {", UCXD),
    ("uc19-count-skipped-for-cells-that-can-pass", "src", RX, "  return budget != 0 && n >= 2 && (n * (n - 1)) / 2 > budget;", "  return budget != 0 && n >= 2 && (n * (n - 1)) / 4 > budget;", UCXD),
    ("uc19-count-ties-as-meetings", "src", RX, "        if (right_of_at(s, src[i], src[j], h)) {", "        if (!right_of_at(s, src[j], src[i], h)) {", UCXD),
    ("uc8-predicate-body", "src", RX, "  if (w[0] != 0) {\n    return 2;\n  }\n  if (w[1] == 0) {", "  if (w[0] == 0) {\n    return 2;\n  }\n  if (w[1] == 0) {", UCXD),
    ("uc8-aligned-unclipped", "src", RX, "  return in ? 2 : 0;\n}\n\n/* --- the order of a slab's fragments", "  return 2;\n}\n\n/* --- the order of a slab's fragments", UCXD),
    ("uc8-complement-ignored", "src", RX, "  if (p->complement) {\n    in = !in;\n  }", "", UCXD),
    ("uc8-even-odd-ignored", "src", RX, "  int in = p->even_odd ? ((w[2] & 1) != 0) : (w[2] != 0);", "  int in = w[2] != 0;", UCXD),
    ("uc8-region-even-odd-ignored", "src", RX, "    return (p->even_odd ? (w[0] & 1) != 0 : w[0] != 0) ? 2 : 0;", "    return w[0] != 0 ? 2 : 0;", UCXD),
]

# The generated plants (tools/gen-render-plants.py): the mutations of src/render the sweep was seen to catch, and
# PLANT_EXTRA_JSON, a file of candidates to run as well (for re-making tools/render-plants.json).
import json as _json
for _file in [str(ROOT / "tools" / "render-plants.json")] + ([os.environ["PLANT_EXTRA_JSON"]] if os.environ.get("PLANT_EXTRA_JSON") else []):
    if os.path.exists(_file):
        with open(_file) as _f:
            PLANTS += [tuple(x) for x in _json.load(_f)]

# Color-paint story P4: the plane's fill (PL2, PL7).  Written with the story; the sweep is deferred to the consolidated pass.
PLN = ["testRender_plane", "testRender_conformance"]
PLANTS += [
    ("pl7-region-is-extent-only", "src", RS, "  GSHCPU_Rect rect = reach_of(run, frame->clip_base, run->clip_count);\n  GSHCPU_Rect whole = whole_reach_of(run, frame->clip_base, run->clip_count);\n  if (gshcpu_render_rect_empty(rect)) {\n    rect.x1 = rect.x0;", "  GSHCPU_Rect rect = run->layout.region;\n  GSHCPU_Rect whole = whole_reach_of(run, frame->clip_base, run->clip_count);\n  if (gshcpu_render_rect_empty(rect)) {\n    rect.x1 = rect.x0;", PLN),
    ("pl7-coverage-half", "src", RS, "    memset(run->coverage, 0xFF, (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0));", "    memset(run->coverage, 0x80, (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0));", PLN),
    ("pl7-coverage-clear", "src", RS, "    memset(run->coverage, 0xFF, (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0));", "    memset(run->coverage, 0x00, (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0));", PLN),
    ("pl7-plane-as-path", "src", RS, "  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_PLANE) {\n    return fill_plane(run, item, chain, count);\n  }", "", PLN),
    ("pl7-clips-ignored", "src", RS, "  GSHCPU_Rect rect = reach_of(run, frame->clip_base, run->clip_count);\n  GSHCPU_Rect whole = whole_reach_of(run, frame->clip_base, run->clip_count);\n  if (gshcpu_render_rect_empty(rect)) {\n    rect.x1 = rect.x0;", "  GSHCPU_Rect rect = reach_of(run, frame->clip_base, frame->clip_base);\n  GSHCPU_Rect whole = whole_reach_of(run, frame->clip_base, run->clip_count);\n  if (gshcpu_render_rect_empty(rect)) {\n    rect.x1 = rect.x0;", PLN),
    ("pl7-paint-tail-skipped", "src", RS, "  return paint_coverage(run, item, chain, count, frame, rect, whole, run->coverage);\n}\n\nstatic GSHSCENE_Result fill(", "  (void)whole;\n  (void)item;\n  return GSHSCENE_OK;\n}\n\nstatic GSHSCENE_Result fill(", PLN),
]

# Color-paint story P5: the placeholder is gone through and never drawn (PH6).  Written with the story; the sweep is
# deferred to the consolidated pass.  The entry itself is the planning pass's (shape-scene's check-planted.py).
PHL = ["testRender_placeholder", "testRender_conformance"]
PLANTS += [
    ("ph6-placeholder-unknown", "src", RS, "    case GSHSCENE_ITEM_PLACEHOLDER:\n      return GSHSCENE_OK; /* PH6: nothing is drawn; the planning pass reported it, refused or approximated */", "    case GSHSCENE_ITEM_PLACEHOLDER:\n      return GSHSCENE_ERR_INTERNAL;", PHL),
    ("ph6-placeholder-claimed", "src", RS, "      (UINT32_C(1) << GSHSCENE_ITEM_STROKE);", "      (UINT32_C(1) << GSHSCENE_ITEM_STROKE) | (UINT32_C(1) << GSHSCENE_ITEM_PLACEHOLDER);", ["testRender"]),
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
        obj = destdir / (str(src).replace("/", "_") + ".o")
        r = run(CC + list(pre) + CFLAGS + list(extra) + ["-c", src, "-o", str(obj)])
        if r.returncode != 0:
            raise RuntimeError("cannot compile %s:\n%s" % (src, r.stderr))
        objs.append(str(obj))
    return objs


def link_test(test, objs, out):
    # The test-only C support objects (tests/support/*.c: the earlier exact step the dense tests compare against) are
    # built by the Makefile beside the test objects and link into every test.
    support = " ".join(sorted(str(o) for o in (OBJ_DIR / "tests" / "support").glob("*.o")))
    cmd = "%s -o %s %s %s %s %s" % (CXX, out, test_object(test), support, " ".join(objs), LDFLAGS)
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
    # PLANT_ALL_TESTS=1: a plant its own tests do not see is tried against every other test of the library and the probe
    # before it is called a survivor; the test that sees it is named, and the plant's list is the one to correct.
    order = list(tests)
    if os.environ.get("PLANT_ALL_TESTS"):
        order += [t for t in TESTS + ["probe"] if t not in order]
    for test in order:
        if test == "probe":
            rc, msg = probe_hash(objs, d)
            failing = [msg.splitlines()[-1] if msg else "probe"]
        else:
            exe = d / test
            link_test(test, objs, exe)
            rc, failing = run_test(exe)
        if rc != 0:
            caught_by.append((test + ("" if test in tests else " [not in its list]"), failing[:1]))
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
