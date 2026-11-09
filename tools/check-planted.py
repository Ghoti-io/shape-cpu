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
  rm-         masks story M2, by hand, for each rule and branch of the soft mask in the sink (render.h, MC1 to MC9): the value
              (mask.c: the mode, what it acts on, the transfer's skip, the coefficients, the backdrop's conversion and weight,
              the picture's conversion into the luminance space, the luminance of the straight color, a dropped clamp, the
              frame's space), the pop (the opacity and the shape weighed, the skip of a zero source), the claims, where and
              when the mask is drawn (over the group's rectangle or the whole region, the clips around the group in force in it,
              the placement, the frame's release, the chain naming the pop, the walk's memory), the limits (cells counted over
              the pop region, the whole render's pop region taken from the damage, a child's pop not joined into its parent's),
              the layout (the mask frame, the picture's clips, gradients, strokes, nested walks and records, the measure), cancel
              and work, the two tiers and their counters, and what a list with no mask pays
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

The sweep of the whole tree after the strokes and color-paint stories, in ghoti-build:gcc16 (``PLANT_ALL_TESTS=1`` tries a
plant against every test and the probe before it is called a survivor; the generated candidates name every test).  Of the
hand-written plants 14 that did not compile were rewritten and 28 of the stroke, exact-step and text plants were not seen;
the tests added with the sweep see the aligned bands pixel by pixel on the union of the pieces and on the build without it,
the union formula in D8's arithmetic, the boxes of a stroke on every side, the cache key's y fraction, which widths are
hairlines, and when a stroke is the sparse composite's.  Of the 2891 generated candidates not held, 644 do not compile
(dropped), 1633 are caught (now held) and 614 survive; of those 613 are, by kind (the other was not reached by the
second sweep):

  - 293 comparisons flipped at a tie or a loop bound (the minimum of two equal numbers, a sort's equal keys, a loop one past
    an array whose next element is unread), and 93 logic operators and negations where an earlier check decides alike
  - 216 statements deleted: a store over zeroed memory, a ``memset`` over a record that is then written, a ``free`` or
    ``destroy`` (a leak, seen by a sanitizer), the terms of a workspace size
  - 10 status codes swapped where the caller compares only against ``OK``, and one message

The image and gradient stories (2026-10-28) changed sink.c, stroke.c, cover.c, gradient.c and image code, which moved the lines
the generated plants name and the text some hand-written ones anchor on: 400 hand-written and generated plants no longer matched
(0 or 2 places) and 36 were no longer caught.  The hand-written ones were anchored on the code as it is (the byte layers' group
composition, which has a second copy of every rule, gained a plant each, ``r-group-*-8``, caught by the fast fill test), and the
generated ones were made again: of the 3594 candidates the held list did not have (the lines of those that moved and the
survivors of the earlier sweeps), 871 do not compile, 1588 are caught (now held, 2527 in all) and 1135 survive, of the kinds
listed above.  ``gshcpu_render_trunc_i64`` has its own plants (``ritr-``, kind "srct": the tests are built against the planted
header), each caught by RenderImageTruncate.

Not seen and not shown equivalent, each needing a case built for it, when that sweep ended: the cells of the parts of an
aligned stroke summed against the cap (``sc6-cells-not-summed``), the pinning of a cache entry while a render uses it and its
eviction while pinned (``sc7-cache-not-pinned``, ``sc7-evict-pinned``, ``sc7-evicted-served``), a hit past the sink's point cap
(``sc7-hit-limit-unchecked``), the run's centerline limit and its notes when nothing is drawn (``st8-limit-not-the-items``,
``st8-nothing-drawn-notes-dropped``, ``st9-work-run-elements-unclamped``), three arithmetic details of the exact step
(``uc15-meeting-depth-rounded``, ``uc4-remainder-unchecked``, ``uc7-even-odd-period``), ``r-caps-kinds-grown`` and ``rv-383-gt``.
Since then: tests see ``sc6-cells-not-summed`` (the least cap that draws an aligned stroke is the same with the joint step off),
the three cache plants (the cache driven below the sink, with the tracking allocator), ``st9-work-run-elements-unclamped`` (a
sink whose element limit is below its centerline's) and ``sc8-sparse-quiet-value`` (its old mutation had become the same
program: the column before a quiet stretch is cleared as it is read); ``r-caps-kinds-grown`` claims the item kind that follows
the last one the sink draws (its old one had become STROKE).  The ones still not seen are in SURVIVORS below, each with what a
case for it needs; the gate prints them as "known" and fails if one is caught.

The soft mask (2026-10-31) added mask.c and changed sink.c (the layout and the measure, the walk of a mask's picture, the masked
pop), which moved every line the generated plants of sink.c name: the 643 held entries were dropped and sink.c's candidates made
again with mask.c's (1484 and 28), and swept in ghoti-build:gcc16 on a 32-thread build host with ``PLANT_ALL_TESTS=1``, together with
the hand plants of the mask pass (``rm-``) and the five hand plants of the group pop the new code had re-anchored.  Of the 1512
generated candidates 310 do not compile, 922 are caught (now held, 903 of sink.c's and 19 of mask.c's) and 280 survive, the kinds
listed above; every hand plant is caught.  The first sweep left eighteen of the hand plants standing (eleven did not compile for an
unused variable, seven were not seen) and showed three generated ones that no test had asked about: a cancel inside the mask pass
(``rs-2227-del``: the cancel test of the first draft was cancelled between the main walk's units and never reached the pass), an
instance's map in the measure of a mask's picture (``rs-673-del``) and the pop that closes a group which draws nothing
(``rs-2248-del``); each has a test since and is held.  Of the survivors on the lines of the soft mask (16 of sink.c's 280 and 9 of
mask.c's 28) none can change a result: the guards of the nested walks' memory that the layout makes unreachable (``rs-660-gt``, ``rs-2191-gt``), the three group
fields set on the mask frame and its whole-render rectangle (``rs-2204-del`` to ``rs-2206-del``, ``rs-2208-del``: the frame is
never popped, and nothing reads them), the clearing of the skip state and of the work at the start of a pass (``rs-2212-del``,
``rs-2222-del``: both are clear already), the flag that caches the outside value (``rs-2290-del``), the comparisons that are ties of a
maximum (``rs-396-and``, ``rs-399-gt``, ``rs-400-gt``, ``rs-409-gt``), the clear of the frame records (``rs-2602-del``: read before
written only by a branch on uninitialized memory, which a Valgrind run sees and the plant build is not run under), a
loop bound in mask.c that only reads or writes one past a three-element array (``rq-56-lt``, ``rq-78-lt``), the
clamp's ties at 0 and 1 (``rq-41-gt``, ``rq-41-gt2``), the clearing of an evaluator every field of which is then set
(``rq-47-del``), and four statements of the value whose deletion leaves a local uninitialized (``rq-71-del``, ``rq-76-del``,
``rq-79-del``, ``rq-81-del``: undefined behavior that a sanitizer sees; the hand plants ``rm-picture-not-converted-into-l``,
``rm-backdrop-ignored`` and ``rm-luminance-clamp-dropped`` are the same defects written so that they compile to a value).

The layout's memo (2026-10-31) moved from a 64-entry table in sink.c to src/render/memo.c (a table that doubles, from the allocator or
from the caller's workspace), changed seven lines of sink.c without moving any other, and added the counter ``mask_pictures`` that
the tests read.  Eleven held entries whose lines changed were dropped and the 78 candidates of the changed lines and of memo.c (62 and
16) swept in ghoti-build:gcc16 on a 12-thread host with ``PLANT_ALL_TESTS=1``: 16 do not compile, 55 are caught (now held, 45 of
memo.c's and 10 of sink.c's) and 7 survive, all of memo.c's and none can change a result: the hash of a key when the shift is not
taken (``rn-47-del``: every key then starts its probe at slot 0, which is slower and the same), the comparisons that are ties of a
size (``rn-65-le``: the value that is exactly the most, ``rn-70-ge``: a scratch with nothing left after its alignment,
``rn-122-gt``: a table grown one entry earlier, ``rn-92-gt``: the overflow guard of a table of 2^58 slots), the ``||`` for ``&&`` that
the allocator's first choice makes the same (``rn-67-and``) and the clear of a slot's padding that is copied and never read
(``rn-126-del``).  The sweep showed that the first test of the memo, which named each picture twice in a row, could not see a table
that forgets on growth: a picture is named again after all the others now (``rm-memo-forgets-what-it-held-on-growth``).  The hand
plants of the memo (``rm-memo-``) and of PLUS's clamp (``rm-plus-``) are caught.

Masks-images-patterns story M3 (non-isolated and knockout groups, KC1 to KC11) added src/render/group.c, the stack of open groups to
memo.c, the element step to compose.c and the channels, the layout and the pop to sink.c.  The hand plants are ``rj-``, written for every
rule and branch (the backdrop and its conversion, the group's result, which layers carry which channel, the layout's counts, the step and
its skips, the pop and its dispatch, the caps, the stack's chunks): 91 of them, 87 caught and 4 equivalent (``SURVIVORS``).  The sink's own
tables moved a hundred lines or more, so the 2812 held generated entries of sink.c, compose.c and memo.c were realigned to their lines (935
moved, 41 dropped because their line changed), and the 335 generated candidates of the changed lines and of group.c (``rz-``: 22, sink.c:
250, memo.c: 42, compose.c: 21) were swept in ghoti-build:gcc16 with the tests of the library: 60 do not compile, 245 are caught (now held)
and 30 survive, of the kinds below.  The first sweep left 37; the survivors of what the tests lacked were answered with tests (an
allocation that fails in the layout is an out-of-memory and never a limit, the count of general layers, a masked group that draws nothing
is a unit done, a masked general pop is counted, the stack's chunks are given back, the stack and the memo share the caller's memory), and
seven of them are caught now.

  - ties of a maximum or a limit that give the same value either way (``rs-474-gt``, ``rs-475-gt``, ``rs-486-gt``, ``rs-487-gt``,
    ``rs-539-gt``, ``rs-540-gt``: the most open groups; ``rc-801-lt``: PLUS where the sum is exactly 1; ``rn-159-ge``: a scratch with
    exactly the room);
  - unreachable, cached or informational (``rs-532-and``: the count of an instance fails only for an allocation that returns at once;
    ``rs-616-ret``: the records of the frames cannot overflow; ``rs-619-del``, ``rs-620-del``: zeros the count already gives;
    ``rs-2542-del``: a constant found once and found again; ``rn-180-del``: a flag nothing reads; ``rn-221-del``, ``rn-222-del``,
    ``rn-223-del``: the state of a stack that is closed);
  - an element of no weight and no shape never reaches the step, the pop skips a clip weight of 0 first (``rz-39-eq``, ``rz-39-and``,
    ``rz-39-eq2``), and the colors loop's extra pass writes an alpha that the next statement replaces (``rz-71-lt``);
  - visible only to a counter under the test switch, where a mask's frame is a layer of its own (``rs-2461-del``, ``rs-2462-del``), or
    only to the accounting of work, which the declared bound's test holds and no test pins pixel by pixel (``rs-2741-neg``,
    ``rs-2742-del``);
  - visible only to a sanitizer: an uninitialized record read before it is set (``rs-2515-del``), a chunk's link before it is set
    (``rn-166-del``), and a stale record at a mask frame's depth, which then writes a group shape past a region of no size into the arrays
    that follow it, none of which a plain build reads back (``rs-2460-del``);
  - a workspace smaller than the memo's own tables is what shows a scratch cursor that is not advanced (``rn-162-del``), and a tier
    that is bit-equal to the path without it (``rs-3069-ne``, the direct 8-bit composite on a line this change touched).

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
# kind: "srct" is "src" with the listed tests built again against the planted tree (for an inline function of a private header
# that the tests call themselves).  "src" or "hdr" (the whole library is rebuilt with the changed header
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
    ("one-pixel", "src", CV, "#define ONE_PIXEL 64", "#define ONE_PIXEL 32", KLR),
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
    ("line-horizontal-test", "src", CV, "  if (from.y == to.y) {\n    /* A horizontal edge", "  if (from.y != to.y) {\n    /* A horizontal edge", K),
    ("line-single-row-test", "src", CV, "  if (ey1 == ey2) {\n    edge_scanline(raster, ey1, from, to);", "  if (ey1 != ey2) {\n    edge_scanline(raster, ey1, from, to);", K),
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
RN = "src/render/memo.c"
RI = "src/render/render_internal.h"
RHD = "include/ghoti.io/shape-cpu/render.h"
# Quick and likely first: the sweep stops at the first test that fails.
RT = ["testRender_vectors", "testRender_parts", "testRender", "testRender_limits", "testRender_conformance", "probe"]
RF8 = ["testRender_fast_fill"]  # the byte layers (fast8) are the fast fill coverage's
CONF = ["testRender_conformance"]


def factor(op, old, new):
    """Replace one factor of one operator's case in apply()."""
    return "    case GSHSCENE_COMPOSITE_%s:\n%s" % (op, old), "    case GSHSCENE_COMPOSITE_%s:\n%s" % (op, new)


_FACTORS = [
    ("clear-fa", "CLEAR", "      return 1;", "      *fa = 1.0;\n      return 1;"),
    ("src-fa", "SRC", "      *fa = 1.0;", "      *fa = 0.5;"),
    ("dest-fb", "DEST", "      *fb = 1.0;", "      *fb = 0.5;"),
    ("src-over-fa", "SRC_OVER", "      *fa = 1.0;\n      *fb = 1.0 - sa;", "      *fa = 0.5;\n      *fb = 1.0 - sa;"),
    ("src-over-fb", "SRC_OVER", "      *fa = 1.0;\n      *fb = 1.0 - sa;", "      *fa = 1.0;\n      *fb = 1.0 - da;"),
    ("dest-over-fa", "DEST_OVER", "      *fa = 1.0 - da;", "      *fa = 1.0 - sa;"),
    ("dest-over-fb", "DEST_OVER", "      *fa = 1.0 - da;\n      *fb = 1.0;", "      *fa = 1.0 - da;\n      *fb = sa;"),
    ("src-in-fa", "SRC_IN", "      *fa = da;", "      *fa = sa;"),
    ("dest-in-fb", "DEST_IN", "      *fb = sa;", "      *fb = da;"),
    ("src-out-fa", "SRC_OUT", "      *fa = 1.0 - da;", "      *fa = 1.0 - sa;"),
    ("dest-out-fb", "DEST_OUT", "      *fb = 1.0 - sa;", "      *fb = 1.0 - da;"),
    ("src-atop-fa", "SRC_ATOP", "      *fa = da;", "      *fa = 1.0 - da;"),
    ("src-atop-fb", "SRC_ATOP", "      *fa = da;\n      *fb = 1.0 - sa;", "      *fa = da;\n      *fb = sa;"),
    ("dest-atop-fa", "DEST_ATOP", "      *fa = 1.0 - da;", "      *fa = da;"),
    ("dest-atop-fb", "DEST_ATOP", "      *fa = 1.0 - da;\n      *fb = sa;", "      *fa = 1.0 - da;\n      *fb = 1.0 - sa;"),
    ("xor-fa", "XOR", "      *fa = 1.0 - da;", "      *fa = da;"),
    ("xor-fb", "XOR", "      *fa = 1.0 - da;\n      *fb = 1.0 - sa;", "      *fa = 1.0 - da;\n      *fb = sa;"),
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
    ("r-op-unknown-is-clear", "src", RC, "    return 0;\n  }\n  for (int i = 0; i < 4; i++) {\n    if (family == 2) {",
     "  }\n  for (int i = 0; i < 4; i++) {\n    if (family == 2) {", RT),
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
    ('r-b-apply-case', "src", RC, "  return op >= GSHSCENE_COMPOSITE_SCREEN && op <= GSHSCENE_COMPOSITE_LUMINOSITY;\n}\n\nint gshcpu_render_blends", "  return op >= GSHSCENE_COMPOSITE_SCREEN && op <= GSHSCENE_COMPOSITE_COLOR;\n}\n\nint gshcpu_render_blends", RT),
    ('r-b-apply-weights', "src", RC, '      d[i] = gshcpu_render_saturate((1.0 - m) * d[i] + m * o[i]); /* N1 */\n    }\n    return 1;', '      d[i] = gshcpu_render_saturate(d[i] + m * o[i]); /* N1 */\n    }\n    return 1;', RT),
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
    ("r-load-scale", "src", RC, "254/255.0, 255/255.0,\n};", "254/255.0, 255/256.0,\n};", RT),
    ("r-load-straight-8", "src", RC, "      out[0] = gshcpu_k_level[p[0]] * a;", "      out[0] = gshcpu_k_level[p[0]];", RT),
    ("r-load-alpha-8", "src", RC, "    out[3] = a;\n    return;", "    out[3] = 1.0;\n    return;", RT),
    ("r-load-straight", "src", RC, "      v[i] = v[i] * v[3];", "      v[i] = v[i];", RT),
    ("r-store-straight", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : gshcpu_render_unpremultiply(w[i], w[3]);", "      w[i] = w[3] == 0.0 ? 0.0 : w[i];", RT),
    ("r-store-straight-zero", "src", RC, "      w[i] = w[3] == 0.0 ? 0.0 : gshcpu_render_unpremultiply(w[i], w[3]);", "      w[i] = gshcpu_render_unpremultiply(w[i], w[3]);", RT),
    ("r-pixel-bytes", "src", RC, "? 16u : 4u;", "? 8u : 4u;", RT),
    ("r-premultiplied-forms", "src", RC, "  return format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;",
     "  return format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED;", RT),
    # --- the clipped scan converter ---------------------------------------------------------------------------------
    ("r-cover-pixel", "src", RV, "#define ONE_PIXEL 64", "#define ONE_PIXEL 63", RT),
    ("r-cover-fold-column", "src", RV, "int64_t column = ex < c->rect.x0 ? 0 : ex - c->rect.x0 + 1;",
     "int64_t column = ex < c->rect.x0 ? 1 : ex - c->rect.x0 + 1;", RT),
    ("r-cover-round-half", "src", RV, "uint64_t quotient = (n + d / 2) / d;", "uint64_t quotient = (n + (d - 1) / 2) / d;", RT),
    ("r-cover-floor", "src", RV, "    q -= 1;", "    q += 0;", RT),
    ("r-cover-cells-limit", "src", RV, "  if (c->cells >= c->max_deposits) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (c->ex) {", "  if (c->cells > c->max_deposits) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (c->ex) {", RT),
    ("r-cover-fold-cells-limit", "src", RV, "  if (c->cells >= c->max_deposits) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (!c->cover) {\n    return; /* counted, not drawn */", "  if (c->cells > c->max_deposits) {\n    c->over = 1;\n    return;\n  }\n  c->cells++;\n  if (!c->cover) {\n    return; /* counted, not drawn */", RT),
    ("r-cover-sweep-scale", "src", RV, "      int64_t accumulated = cv[0] * (ONE_PIXEL * 2);\n      for (int64_t j = 1; j <= width; j++) {\n        accumulated += cv[j] * (ONE_PIXEL * 2);\n        line[j - 1] = fold(accumulated - ar[j], rule, ex != NULL);", "      int64_t accumulated = cv[0] * ONE_PIXEL;\n      for (int64_t j = 1; j <= width; j++) {\n        accumulated += cv[j] * (ONE_PIXEL * 2);\n        line[j - 1] = fold(accumulated - ar[j], rule, ex != NULL);", RT),
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
    ("r-default-cells", "src", RS, "limits->max_deposits : GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS;", "limits->max_deposits : 1000;", RT),
    ("r-default-dimension-ceiling", "src", RS, "    cs->max_dimension = GSHCPU_MAX_DIMENSION_CEILING;", "    cs->max_dimension = 16;", RT),
    ("r-foreground-alpha", "src", RS, "gshscene_list_foreground(request->list, &run->foreground) != GSHSCENE_OK",
     "(gshscene_color_init(&run->foreground), GSHSCENE_OK) != GSHSCENE_OK", RT),
    # Story 14c: a palette fill takes the list's palette, in the paint's resolution and in the sink's disposition.
    ("r-palette-resolve", "src", RS, "gshscene_paint_resolve(&item->paint, &run->foreground, run->palette, &color)",
     "gshscene_paint_resolve(&item->paint, &run->foreground, NULL, &color)", RT),
    ("r-palette-disposition", "src", RS, "&run->job->target.map, gshscene_job_target_space(run->job), run->palette);",
     "&run->job->target.map, gshscene_job_target_space(run->job), NULL);", RT),
    ("r-palette-taken", "src", RS, "  run->palette = gshscene_list_palette(request->list);\n", "", RT),
    ("r-paint-target-space", "src", RS, "\n  GSHSCENE_Color_Space space = color.space;\n  if (gshscene_color_space_is_target(&space)) {\n    space = *gshscene_job_target_space(run->job);", "\n  GSHSCENE_Color_Space space = color.space;\n  if (gshscene_color_space_is_target(&space)) {\n    space = frame->space;", RT),
    ("r-group-space-inherit", "src", RS, "? parent->space : item->group.blend_space;", "? (parent ? *gshscene_job_target_space(run->job) : parent->space) : item->group.blend_space;", RT),
    ("r-group-opacity", "src", RS, "      for (int i = 0; i < 4; i++) {\n        s[i] = o * s[i];", "      for (int i = 0; i < 4; i++) {\n        s[i] = o * o * s[i];", RT),
    ("r-group-opacity-8", "src", RS, "        gshcpu_render_convert_premultiplied(&c, in, s);\n        for (int i = 0; i < 4; i++) {\n          s[i] = o * s[i];", "        gshcpu_render_convert_premultiplied(&c, in, s);\n        for (int i = 0; i < 4; i++) {\n          s[i] = o * o * s[i];", RF8),
    ("r-group-convert-way", "src", RS, "  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);\n  const double o = f->group.opacity;\n  /* The group's layer", "  gshcpu_render_convert_prepare(&c, &parent->space, &f->space);\n  const double o = f->group.opacity;\n  /* The group's layer", RT),
    ("r-group-convert-way-8", "src", RS, "  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);\n  const double o = f->group.opacity;\n  widen(run, parent, region);\n  const int plain", "  gshcpu_render_convert_prepare(&c, &parent->space, &f->space);\n  const double o = f->group.opacity;\n  widen(run, parent, region);\n  const int plain", RF8),
    ("r-group-outer-clips", "src", RS, "\n      uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);\n      if (m8 == 0) {\n        continue;\n      }\n      size_t p = at(run, x, y);\n      double s[4];", "\n      uint32_t m8 = 255u;\n      if (m8 == 0) {\n        continue;\n      }\n      size_t p = at(run, x, y);\n      double s[4];", RT),
    ("r-group-outer-clips-8", "src", RS, "\n        uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);", "\n        uint32_t m8 = 255u;", RF8),
    ("r-group-unbounded-region", "src", RS, "   * weighed above zero (N8). */\n  GSHCPU_Rect region = bounded ? f->dirty : window_of(run);", "   * weighed above zero (N8). */\n  GSHCPU_Rect region = f->dirty;", RT),
    ("r-group-unbounded-region-8", "src", RS, "  int bounded = gshcpu_render_bounded(op);\n  GSHCPU_Rect region = bounded ? f->dirty : run->layout.region;", "  int bounded = gshcpu_render_bounded(op);\n  GSHCPU_Rect region = f->dirty;", RF8),
    ("r-fill-weight", "src", RS, "  apply_src_over(&frame->layer[4u * p], src, gshcpu_k_level[m8]);", "  apply_src_over(&frame->layer[4u * p], src, gshcpu_k_level[m8 - (m8 == 255u)]);", RT),
    ("rgr-fill-weight", "src", RS, "    const double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};\n    painter_pixel(run, frame, px, y, m, s);", "    const double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};\n    painter_pixel(run, frame, px, y, m - (m == 255u), s);", ["testRender_gradient"] + RT),
    ("r-src-over-factor", "src", RS, "  const double fb = 1.0 - s[3];", "  const double fb = s[3];", RT),
    ("r-src-over-keep", "src", RS, "  const double keep = 1.0 - m;", "  const double keep = m;", RT),
    ("r-src-over-source", "src", RS, "    const double o = 1.0 * s[i] + fb * d[i];", "    const double o = fb * d[i];", RT),
    ("r-src-over-mix", "src", RS, "    const double x = keep * d[i] + m * o;", "    const double x = keep * d[i] + o;", RT),
    ("r-src-over-saturate", "src", RS, "    d[i] = x > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION", "    d[i] = x > 0.0 ? GSHSCENE_COLOR_SATURATION", RT),
    ("rgr-clips-unweighed", "src", RS, "    uint32_t m = clipped ? weigh_clips(run, frame->clip_base, run->clip_count, m8[i], px, y) : m8[i];\n    if (m == 0) {\n      continue;\n    }\n    double color[4];", "    uint32_t m = m8[i];\n    if (m == 0) {\n      continue;\n    }\n    double color[4];", ["testRender_gradient"] + RT),
    ("r-touched", "src", RS, "      if (run->touched[p]) {", "      if (1) {", RT),
    ("r-clip-memory", "src", RS, "clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;", "clip->coverage = run->clip_memory;", RT),
    # What a render makes ready (widen, ready_span): the whole region on every frame changes no pixel, only the bytes
    # touched, which testRender_limits counts; the root not read from the target, and a group read where it is not ready.
    ("r-ready-whole-region", "src", RS, "  GSHCPU_Rect grown = gshcpu_render_rect_join(old, r);", "  GSHCPU_Rect grown = gshcpu_render_rect_join(old, run->layout.region);", ["testRender_limits"]),
    ("r-root-unread", "src", RS, "    gshcpu_render_load(run->format, row, (size_t)x, &run->root[4u * p]);", "    (void)row;", RT),
    ("r-group-reads-unready", "src", RS, "size_t p = at(run, x, y);\n      double s[4];\n      gshcpu_render_convert_premultiplied(&c, inside(f->dirty, x, y) ? &f->layer[4u * p] : zero, s);", "size_t p = at(run, x, y);\n      double s[4];\n      gshcpu_render_convert_premultiplied(&c, 1 ? &f->layer[4u * p] : zero, s);", RT),
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
     "  out->item_kinds = (UINT32_C(1) << GSHSCENE_ITEM_PLACEHOLDER) | (UINT32_C(1) << GSHSCENE_ITEM_FILL) |\n"
     "      (UINT32_C(1) << GSHSCENE_ITEM_CLIP_PUSH) |", RT + ["testRender_placeholder"]),
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
    ("rule-S4-cells-in-damage", "src", RS, "gshcpu_render_cover_counted(paths, g.count, item->fill_rule, *rect, walk, out, run->acc", "gshcpu_render_cover_counted(paths, g.count, item->fill_rule, *rect, *rect, out, run->acc", RT),
    ("rule-S4-cells-bound-low", "src", RV, "  return rows + columns;", "  return rows;", RT),
    ("rule-S9-short-item-measures", "src", RS, "    return gcu_safe_add_size(request->workspace_bytes, 1u, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;\n  }", "  }", RT),
    ("rule-S9-short-block-measures", "src", RS, "  if (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) {\n    if (!request->workspace",
     "  if ((request->flags & GSHSCENE_RENDER_CALLER_MEMORY) && request->workspace && request->workspace_bytes >= L.fixed) {\n    if (!request->workspace", RT),
    ("rule-S10-keeps-a-reference", "src", RS, "  if (!p->gradient) {\n    source_of(run, item, frame, p->src);", "  if (!p->gradient) {\n    gshscene_resource_retain(item->resource);\n    source_of(run, item, frame, p->src);", CONF),
    ("rule-S11-src-over", "src", RC, "      *fa = 1.0;\n      *fb = 1.0 - sa;", "      *fa = 1.0;\n      *fb = 1.0 - sa * 0.5;", RT),
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
    ("rgr-notice-drawn", "src", RS, '          : gshscene_paint_notice(item, run->maps, count - 1, &run->job->target.map)) {', '          : 0) {', GRT),
    ("rgr-sample-corner", "src", RS, "(double)(int32_t)px + 0.5, (double)(int32_t)y + 0.5, color)", "(double)(int32_t)px, (double)(int32_t)y, color)", GRT),
    ("rgr-chain-no-gradient-map", "src", RS, "gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &composed) != GSHGEOM_OK ||\n      gshcpu_render_gradient_prepare", "gshgeom_map_compose_chain(run->maps, count, &run->job->target.map, &composed) != GSHGEOM_OK ||\n      gshcpu_render_gradient_prepare", GRT),
    ("rgr-chain-no-target", "src", RS, "gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &composed) != GSHGEOM_OK ||\n      gshcpu_render_gradient_prepare", "gshgeom_map_compose_chain(run->maps, count + 1, NULL, &composed) != GSHGEOM_OK ||\n      gshcpu_render_gradient_prepare", GRT),
    ("rgr-domain-unchecked", "src", RS, " ||\n      !gshcpu_render_gradient_in_domain(&p->gr, whole)) {", ") {", GRT),
    ("rgr-domain-in-damage", "src", RS, "!gshcpu_render_gradient_in_domain(&p->gr, whole)) {", "!gshcpu_render_gradient_in_domain(&p->gr, rect)) {", GRT),
    ("rgr-convert-skipped", "src", RS, "    gshcpu_render_convert_straight(&p->conv, color, color);\n", "", GRT),
    ("rgr-not-premultiplied", "src", RS, "    const double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};", "    const double s[4] = {color[0], color[1], color[2], color[3]};", GRT),
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
    ("rgr-sweep-half-turn", "src", RGR, "static double angle_of(const GSHCPU_Gradient * g, double dx, double dy) {\n  if (dy == 0.0) {\n    return dx < 0.0 ? 0.5 * g->turn : 0.0;", "static double angle_of(const GSHCPU_Gradient * g, double dx, double dy) {\n  if (dy == 0.0) {\n    return 0.0;", GRT),
    ("rgr-sweep-fast-half-turn", "src", RGR, "static inline double angle_fast(const GSHCPU_Gradient * g, double dx, double dy) {\n  if (dy == 0.0) {\n    return dx < 0.0 ? 0.5 * g->turn : 0.0;", "static inline double angle_fast(const GSHCPU_Gradient * g, double dx, double dy) {\n  if (dy == 0.0) {\n    return 0.0;", GRT),
    ("rgr-sweep-unit", "src", RGR, "    rad += TWO_PI;\n  }\n  return g->unit == GSHGEOM_ANGLE_RADIANS ? rad : rad / TWO_PI * g->turn;", "    rad += TWO_PI;\n  }\n  return g->unit == GSHGEOM_ANGLE_RADIANS ? rad : rad;", GRT),
    ("rgr-sweep-fast-unit", "src", RGR, "    rad = TWO_PI - rad; /* the angle in [0, 2 pi) */\n  }\n  return g->unit == GSHGEOM_ANGLE_RADIANS ? rad : rad / TWO_PI * g->turn;", "    rad = TWO_PI - rad; /* the angle in [0, 2 pi) */\n  }\n  return g->unit == GSHGEOM_ANGLE_RADIANS ? rad : rad;", GRT),
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
    ("sc7-key-style", "src", RSK, "  key.style_id = gshscene_resource_id(pen);", "  key.style_id = 0;", RST),
    ("sc7-key-linear", "src", RSK, "    key.linear[i] = gshgeom_f64_bits(canon.m[i / 2][i % 2]);", "    key.linear[i] = 0;", RST),
    ("sc7-key-fraction-x", "src", RSK, "  key.fraction[0] = gshgeom_f64_bits(canon.t[0]);", "  key.fraction[0] = 0;", RST),
    ("sc7-key-fraction-y", "src", RSK, "  key.fraction[1] = gshgeom_f64_bits(canon.t[1]);", "  key.fraction[1] = 0;", RST),
    # Removed, sc7-key-revision: equivalent: the revision in a cache key is a build constant, and a key is compared only with keys of the same process, so zero in its place separates nothing.
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
    ("sc8-sparse-aligned", "src", RSK, "  if (!s->drawn || s->meta.present[GSHCPU_STROKE_ALIGNED]) {\n    return 0;", "  if (!s->drawn) {\n    return 0;", RST),
    ("sc8-hairline-width", "src", RSK, "params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE && params.width == 1.0;", "params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE;", RST),
    ("sc8-hairline-space", "src", RSK, "params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE && params.width == 1.0;", "params.width == 1.0;", RST),
    ("sc8-sparse-tail", "src", RV, "    if (tail != 0 && end < width) {", "    if (0) {", RST),
    ("sc8-sparse-first-column", "src", RV, "    int64_t j0 = lo > 1 ? lo : 1;", "    int64_t j0 = lo > 2 ? lo : 2;", RST),
    ("sc8-sparse-not-cleared", "src", RV, "      cv[j] = 0;\n      ar[j] = 0;\n      if (v != 0) {", "      if (v != 0) {", RST),
    ("sc8-sparse-skips-a-span", "src", RV, "    if (from >= 0) {\n      batch_add(&batch, c->band + r, c->rect.x0 + (from - 1), (size_t)(end - from + 1), line + (from - j0));", "    if (from >= 0 && end > from) {\n      batch_add(&batch, c->band + r, c->rect.x0 + (from - 1), (size_t)(end - from + 1), line + (from - j0));", RST),
    ("sc8-sparse-run-not-closed", "src", RV, "      } else if (from >= 0) {\n        batch_add(&batch, c->band + r, c->rect.x0 + (from - 1), (size_t)(j - from), line + (from - j0));\n        from = -1;\n      }\n      j++;", "      }\n      j++;", RST),
    ("sc8-sparse-quiet-extent", "src", RV, "      while (k <= end && cv[k] == 0 && ar[k] == 0) {", "      while (k <= end && cv[k] == 0) {", RST),
    ("sc8-sparse-first-run-column", "src", RV, "        if (from < 0) {\n          from = j;\n        }\n        line[j - j0] = v;", "        if (from < 0) {\n          from = j + 1;\n        }\n        line[j - j0] = v;", RST),
    # SC4, SC6, SC11: what draws nothing, the limits and the work
    ("sc4-stroke-notice-skipped", "src", RS, "          ? gshscene_stroke_notice(item, run->maps, count - 1, &run->job->target.map)", "          ? (GSHSCENE_Reason)0", RST),
    ("sc6-limit-not-refused", "src", RS, "  if (opened == GSHCPU_ERR_LIMIT) {\n    return refuse(run, item, chain, count);\n  }", "  if (opened == GSHCPU_ERR_LIMIT) {\n    return GSHSCENE_OK;\n  }", RST),
    ("sc6-compose-limit", "src", RS, "    return refuse(run, item, chain, count); /* SC4: a composed map that cannot be formed is the sink's own limit */", "    return GSHSCENE_OK;", RST),
    ("sc6-cells-not-summed", "src", RSK, "out, acc, max_deposits - total, &used, p == GSHCPU_STROKE_CLIP ? NULL : ex);", "out, acc, max_deposits, &used, p == GSHCPU_STROKE_CLIP ? NULL : ex);", RST),
    ("sc6-whole-count-dropped", "src", RSK, '    if (!same_rect(parts[p].rect, walks[p]) && !whole_count && !may_exceed(s, p, exact)) {\n      walks[p] = parts[p].rect; /* the caps cannot', '    if (1 || (!same_rect(parts[p].rect, walks[p]) && !whole_count && !may_exceed(s, p, exact))) {\n      walks[p] = parts[p].rect; /* the caps cannot', RST + ["testRender_exact"]),
    ("sc9-caller-memory-reads-cache", "src", RS, "  GSHCPU_Stroke_Cache * cache = !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;", "  GSHCPU_Stroke_Cache * cache = (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;", RST),
    ("sc11-expansion-uncounted", "src", RS, "  run->work += expansion_work(run, item, &s);", "", RST + ["testRender_stroke_text"]),
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
    ("st8-measure-skips-runs", "src", RS, "                 : gshscene_stroke_notice(item, maps, count - 1, &target->map)) {\n      return GSHCPU_OK; /* a stroke that draws nothing */", "                 : (!gshscene_resource_path(item->resource) || gshscene_stroke_notice(item, maps, count - 1, &target->map))) {\n      return GSHCPU_OK; /* a stroke that draws nothing */", RTX),
    ("st9-work-glyph-elements-dropped", "src", RS, "  uint64_t work = points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + (path ? 0u : elements);", "  uint64_t work = points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH;", RTX),
    ("st9-work-glyph-elements-for-paths", "src", RS, "  uint64_t work = points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + (path ? 0u : elements);", "  uint64_t work = points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + elements;", RTX),
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
    ("uc4-fold-column-left-out", "src", RV, "      int64_t sum = cv[0];\n      int64_t j0 = 1;", "      int64_t sum = 0;\n      int64_t j0 = 1;", UCX),
    ("uc4-fold-column-left-out-aligned", "src", RV, "          row[0] = cv[0];", "          row[0] = 0;", UCX),
    # UC5: slabs, order, winding
    # UC6: the shared area
    ("uc7-shared-not-subtracted", "src", RV, "        int64_t value = dense - shared;\n        if (value < 0) {\n          value = 0;\n        }\n        ar[j] = accumulated - ex->eps * value;", "        int64_t value = dense;\n        if (value < 0) {\n          value = 0;\n        }\n        ar[j] = accumulated - ex->eps * value;", UCX),
    # UC2, UC3: pieces and which cells
    ("uc2-flat-pieces-not-counted", "src", RV, "    if (c->ex && piece_kind(ex, a, b) == 2) {", "    if (c->ex && piece_kind(ex, a, b) == 3) {", UCX),
    ("uc2-flat-piece-any-touch", "src", RV, "  return a.x == left || b.x == left ? 2 : 0;", "  return a.x == left ? 2 : 0;", UCX),
    ("uc2-flat-edges-not-walked", "src", RV, "    if (c->ex) {\n      flat_edge(c, from, to);\n    }", "", UCX),
    ("uc3-exact-threshold", "src", RV, "        if (n >= 2) {\n          if (kept + n > ex->pool_cap) {", "        if (n >= 3) {\n          if (kept + n > ex->pool_cap) {", UCX),
    ("uc3-window-unbounded", "src", RV, "          if (kept + n > ex->pool_cap) {\n            full = 1;\n            break;\n          }", "", UCX + ["testRender_stroke_text"]),
    ("uc3-scan-first-column", "src", RV, "        int64_t xlo = c->lo[r] > 1 ? c->lo[r] - 1 : 0;", "        int64_t xlo = c->lo[r] > 1 ? c->lo[r] : 0;", UCX),
    ("uc3-scan-last-column", "src", RV, "        xe = c->hi[r] - 1 < xe ? c->hi[r] - 1 : xe;", "        xe = c->hi[r] - 2 < xe ? c->hi[r] - 2 : xe;", UCX),
    ("uc4-cell-sum-start", "src", RV, "        j0 = c->lo[r] > 1 ? c->lo[r] : 1; /* nothing is deposited left", "        j0 = c->lo[r] > 2 ? c->lo[r] : 2; /* nothing is deposited left", UCX),
    ("uc3-sparse-exact-cell-not-visited", "src", RV, "  if (*n == 2 && c->track && c->cover && ex >= c->rect.x0 && ex < c->rect.x1) {", "  if (0) {", UCX),
    # UC8: parts that share a pixel
    ("uc8-shared-cell-threshold", "src", RV, "          if (jc.multi >= 2) {\n            GSHCPU_Exact_Cell cell;", "          if (jc.multi >= 3) {\n            GSHCPU_Exact_Cell cell;", UCX),
    ("uc8-pieces-kept-threshold", "src", RV, "    out->store[p] = out->n[p] > 0 && (out->multi >= 2 || out->n[p] >= 2);", "    out->store[p] = out->n[p] > 0 && out->n[p] >= 2;", UCX),
    ("uc8-union-value-ignored", "src", RV, "        result[row + (size_t)j] = k != 0xffffu ? (uint8_t)k\n            : (uint8_t)", "        result[row + (size_t)j] = 0 ? (uint8_t)k\n            : (uint8_t)", UCX),
    ("uc8-clip-overlap-not-exact", "src", RV, "          } else if (jc.n[2] >= 2) {", "          } else if (0) {", UCX),
    ("uc8-complement-flag-dropped", "src", RV, "  three.complement = complement;", "  three.complement = 0;", UCX),
    ("uc8-clip-rule-dropped", "src", RV, "  three.even_odd = in[2].rule == GSHGEOM_FILL_EVEN_ODD;", "  three.even_odd = 0;", UCX),
    ("uc8-joint-step-skipped", "src", RSK, "  if (aligned.path && ex && joint_tier) {", "  if (0) {", UCB),
    # UC9: the cap, the field, the refusal
    ("uc9-cap-off-by-one", "src", RV, "  if ((uint64_t)*n >= c->ex->max_pieces) {", "  if ((uint64_t)*n > c->ex->max_pieces) {", UCX),
    ("uc9-cap-falls-back-to-dense", "src", RV, "  if ((uint64_t)*n >= c->ex->max_pieces) {\n    c->over = 1;\n    return;\n  }", "  if ((uint64_t)*n >= c->ex->max_pieces) {\n    return;\n  }", UCX),
    ("uc9-damage-refusal-differs", "src", RSK, '  if (edges > ex->max_pieces) {\n    return 1;\n  }', '  if (0 && edges > ex->max_pieces) {\n    return 1;\n  }', UCX),
    ("uc9-field-ignored", "src", RS, "  cs->max_exact = limits && limits->max_exact_fragments ? limits->max_exact_fragments", "  cs->max_exact = 0 ? limits->max_exact_fragments", UCX),
    ("uc9-field-ceiling-off-by-one", "src", RS, "  if (limits && limits->max_exact_fragments > GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS) {", "  if (limits && limits->max_exact_fragments >= GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS) {", UCX),
    ("uc9-refusal-swallowed", "src", RS, "  if (r == GSHCPU_ERR_LIMIT) {\n    result = refuse(run, item, chain, count);", "  if (r == GSHCPU_ERR_LIMIT) {\n    result = GSHSCENE_OK;", UCX),
    ("uc9-work-uncounted", "src", RS, "  run->work += cells + exact_work(run);", "  run->work += cells;", ["testRender_stroke"]),
    ("uc9-work-bound-uncounted", "src", RS, "  if (!gcu_safe_add_u64(b, exact, &b) || !gcu_safe_add_u64(b, cs->max_image_work, &b)) {", "  (void)exact;\n  if (!gcu_safe_add_u64(b, cs->max_image_work, &b)) {", ["testRender", "testRender_limits"]),
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
    ("uc16-deposits-unchecked", "src", RX, "    if (verify && cell->dense[p] != params->eps[p] * (128 * (cell->a_left[p] + cover) - area)) {", "    if (0 && area && cover && verify) {", UCXD),
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
# --- The primitive tag never changes coverage (shape-scene primitive.h, PR9) ------------------------------------------
# Each plant adds a branch that reads the tag and changes what is drawn; tests/unit/test_render_primitives.cpp (a tagged path and its
# untagged copy render bit for bit alike) must fail on it, and tools/check-tag-ignored.sh fails on the read itself (it shows itself failing
# on a planted call, kind and type).
PLANTS += [
    ('pt-tag-flip-x-draws-nothing', 'src', 'src/render/geometry.c', '  memset(out, 0, sizeof *out);\n  chain[count] = item->map;\n  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);', '  memset(out, 0, sizeof *out);\n  {\n#include <ghoti.io/shape-scene/primitive.h>\n    GSHSCENE_Primitive tag;\n    if (gshscene_resource_primitive(item->resource, &tag) == GSHSCENE_OK && tag.flip_x) {\n      return GSHCPU_OK;\n    }\n  }\n  chain[count] = item->map;\n  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);', ["testRender_primitives"]),
    ('pt-tag-ellipse-draws-nothing', 'src', 'src/render/geometry.c', '  memset(out, 0, sizeof *out);\n  chain[count] = item->map;\n  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);', '  memset(out, 0, sizeof *out);\n  {\n#include <ghoti.io/shape-scene/primitive.h>\n    GSHSCENE_Primitive tag;\n    if (gshscene_resource_primitive(item->resource, &tag) == GSHSCENE_OK && tag.kind == GSHSCENE_PRIMITIVE_ELLIPSE) {\n      return GSHCPU_OK;\n    }\n  }\n  chain[count] = item->map;\n  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);', ["testRender_primitives"]),
    ('pt-tag-rect-stroke-round-join', 'src', 'src/render/stroke.c', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }\n  {\n#include <ghoti.io/shape-scene/primitive.h>\n    GSHSCENE_Primitive tag;\n    if (gshscene_resource_primitive(item->resource, &tag) == GSHSCENE_OK && tag.kind == GSHSCENE_PRIMITIVE_RECT) {\n      params.join = GSHGEOM_STROKE_JOIN_ROUND;\n    }\n  }', ["testRender_primitives"]),
    ('pt-tag-ellipse-dash-offset', 'src', 'src/render/stroke.c', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }\n  {\n#include <ghoti.io/shape-scene/primitive.h>\n    GSHSCENE_Primitive tag;\n    if (gshscene_resource_primitive(item->resource, &tag) == GSHSCENE_OK && tag.kind == GSHSCENE_PRIMITIVE_ELLIPSE) {\n      params.dash_offset += 1.0;\n    }\n  }', ["testRender_primitives"]),
    ('pt-tag-rounded-rect-wider', 'src', 'src/render/stroke.c', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }\n  {\n#include <ghoti.io/shape-scene/primitive.h>\n    GSHSCENE_Primitive tag;\n    if (gshscene_resource_primitive(item->resource, &tag) == GSHSCENE_OK && tag.kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT) {\n      params.width += 0.5;\n    }\n  }', ["testRender_primitives"]),
    ('pt-tag-flip-y-reverses-dashes', 'src', 'src/render/stroke.c', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }', '  if ((!path && !is_run) || gshscene_resource_stroke_geometry(pen, &params) != GSHSCENE_OK) {\n    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */\n  }\n  {\n#include <ghoti.io/shape-scene/primitive.h>\n    GSHSCENE_Primitive tag;\n    if (gshscene_resource_primitive(item->resource, &tag) == GSHSCENE_OK && tag.flip_y) {\n      params.dash_offset += 0.75;\n    }\n  }', ["testRender_primitives"]),
]

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
    ("pl7-paint-tail-skipped", "src", RS, "  return paint_coverage(run, item, chain, count, frame, rect, whole, run->coverage);\n}\n\nstatic GSHSCENE_Result fill(", "  (void)whole;\n  (void)item;\n  (void)chain;\n  (void)count;\n  (void)frame;\n  (void)rect;\n  return GSHSCENE_OK;\n}\n\nstatic GSHSCENE_Result fill(", PLN),
]

# Color-paint story P5: the placeholder is gone through and never drawn (PH6).  Written with the story; the sweep is
# deferred to the consolidated pass.  The entry itself is the planning pass's (shape-scene's check-planted.py).
PHL = ["testRender_placeholder", "testRender_conformance"]
PLANTS += [
    ("ph6-placeholder-unknown", "src", RS, "    case GSHSCENE_ITEM_PLACEHOLDER:\n      return GSHSCENE_OK; /* PH6: nothing is drawn; the planning pass reported it, refused or approximated */", "    case GSHSCENE_ITEM_PLACEHOLDER:\n      return GSHSCENE_ERR_INTERNAL;", PHL),
    ("ph6-placeholder-claimed", "src", RS, "      (UINT32_C(1) << GSHSCENE_ITEM_STROKE);", "      (UINT32_C(1) << GSHSCENE_ITEM_STROKE) | (UINT32_C(1) << GSHSCENE_ITEM_PLACEHOLDER);", ["testRender"]),
]


# Masks-images-patterns story I3: the image sampler (IC1 to IC14).  Written with the story; the sweep is deferred to the
# consolidated pass.  The id says the rule: ri- the point filters, the edges and the decode (image.c), ria- AREA (image_area.c), rit-
# the tiers, ris- the sink's wiring and limits.  An equivalent mutant is not planted (the half-open piece boundary of the kernel at
# d = 1, a compensation term below the tolerance, the unused lut for 12 bits, a count of whole periods one short (the rest is summed
# cell by cell, to within the tolerance), the redundant guard that a row spans the parallelogram, whose cross-sections default to zero).
IMF = "src/render/image.c"
IMA = "src/render/image_area.c"
IMT = ["testRender_image", "testRender"]
TRT = ["testRender_image"]  # RenderImageTruncate
PLANTS += [
    ("ri-edge-pad-high", "src", IMF, "return k < lo ? lo : k > hi - 1 ? hi - 1 : k;", "return k < lo ? lo : k > hi - 1 ? hi : k;", IMT),
    ("ri-edge-pad-low", "src", IMF, "return k < lo ? lo : k > hi - 1 ? hi - 1 : k;", "return k < lo + 1 ? lo + 1 : k > hi - 1 ? hi - 1 : k;", IMT),
    ("ri-edge-repeat-signed", "src", IMF, "return lo + modulo(k - lo, n);", "return lo + (k - lo) % n;", IMT),
    ("ri-modulo-unnormalized", "src", IMF, "return r < 0 ? r + n : r;", "return r;", IMT),
    ("ri-edge-reflect-off", "src", IMF, "return m < n ? lo + m : lo + 2 * n - 1 - m;", "return m < n ? lo + m : lo + 2 * n - m;", IMT),
    ("ri-edge-reflect-repeat", "src", IMF, "return m < n ? lo + m : lo + 2 * n - 1 - m;", "return lo + m % n;", IMT),
    ("ri-edge-none-as-pad", "src", IMF, "return k >= lo && k < hi ? k : -1;", "return k < lo ? lo : k > hi - 1 ? hi - 1 : k;", IMT),
    ("ri-edge-none-closed", "src", IMF, "return k >= lo && k < hi ? k : -1;", "return k >= lo && k <= hi ? k : -1;", IMT),
    ("ri-decode-12-scale", "src", IMF, "im->bits == 12u ? 4095.0 : 65535.0", "im->bits == 12u ? 65535.0 : 4095.0", IMT),
    ("ri-decode-8-scale", "src", IMF, "(double)*at / 255.0;", "(double)*at / 256.0;", IMT),
    ("ri-decode-float-unsaturated", "src", IMF, "return gshcpu_render_saturate((double)f);", "return (double)f;", IMT),
    ("ri-decode-straight-ignored", "src", IMF, "out[k] = im->straight ? a * c[k] : c[k];", "out[k] = c[k];", IMT),
    ("ri-decode-premultiplied-twice", "src", IMF, "out[k] = im->straight ? a * c[k] : c[k];", "out[k] = a * c[k];", IMT),
    ("ri-decode-gray-one-channel", "src", IMF, "c[0] = c[1] = c[2] = sample(im, row, x, 0);", "c[0] = sample(im, row, x, 0);\n    c[1] = c[2] = 0.0;", IMT),
    ("ri-decode-alpha-of-rgb", "src", IMF, "if (im->layout == (uint32_t)GSHSCENE_IMAGE_RGBA) {", "if (im->layout >= (uint32_t)GSHSCENE_IMAGE_RGB) {", IMT),
    ("ri-decode-alpha-only-colored", "src", IMF, "out[0] = out[1] = out[2] = 0.0;\n    out[3] = sample(im, row, x, 0);", "out[0] = out[1] = out[2] = sample(im, row, x, 0);\n    out[3] = sample(im, row, x, 0);", IMT),
    ("ri-window-x0", "src", IMF, "im->x0 = whole ? 0 : window[0];", "im->x0 = 0;", IMT),
    ("ri-window-y0", "src", IMF, "im->y0 = whole ? 0 : window[1];", "im->y0 = 0;", IMT),
    ("ri-window-x1", "src", IMF, "im->x1 = whole ? info.width : window[2];", "im->x1 = info.width;", IMT),
    ("ri-window-y1", "src", IMF, "im->y1 = whole ? info.height : window[3];", "im->y1 = info.height;", IMT),
    ("ri-window-whole-test", "src", IMF, "const int whole = !window[0] && !window[1] && !window[2] && !window[3];", "const int whole = !window[0] && !window[1];", IMT),
    ("ri-convert-in-skipped", "src", IMF, "im->convert_in = !im->in.same;", "im->convert_in = 0;", IMT),
    ("ri-convert-out-skipped", "src", IMF, "im->convert_out = !im->out.same;", "im->convert_out = 0;", IMT),
    # gshcpu_render_trunc_i64 (the cast without the x87 instruction): the sign, the 20-bit split and its recombination, the clamp
    ("ritr-sign-dropped", "srct", "src/render/render_internal.h", "  return neg ? -v : v;", "  return v;", TRT),
    ("ritr-negative-not-detected", "srct", "src/render/render_internal.h", "  const int neg = w < 0.0;", "  const int neg = w < -1.0;", TRT),
    ("ritr-magnitude-signed", "srct", "src/render/render_internal.h", "  const double m = neg ? 0.0 - w : w;", "  const double m = w;", TRT),
    ("ritr-split-shift-high", "srct", "src/render/render_internal.h", "  const double hi = gmath_floor(m * 0x1p-20);", "  const double hi = gmath_floor(m * 0x1p-19);", TRT),
    ("ritr-split-remainder", "srct", "src/render/render_internal.h", "  const double lo = m - hi * 0x1p20;", "  const double lo = m - hi * 0x1p19;", TRT),
    ("ritr-recombine-shift", "srct", "src/render/render_internal.h", "  const int64_t v = ((int64_t)(int32_t)hi << 20) + (int64_t)(int32_t)lo;", "  const int64_t v = ((int64_t)(int32_t)hi << 19) + (int64_t)(int32_t)lo;", TRT),
    ("ritr-low-half-dropped", "srct", "src/render/render_internal.h", "  const int64_t v = ((int64_t)(int32_t)hi << 20) + (int64_t)(int32_t)lo;", "  const int64_t v = ((int64_t)(int32_t)hi << 20) + (int64_t)(int32_t)(lo - lo);", TRT),
    ("ritr-clamp-bound", "srct", "src/render/render_internal.h", "  const double limit = 0x1p40;", "  const double limit = 0x1p41;", TRT),
    ("ritr-clamp-high", "srct", "src/render/render_internal.h", "    return w > 0.0 ? (int64_t)1 << 40 : (w < 0.0 ? -((int64_t)1 << 40) : 0);", "    return w > 0.0 ? (int64_t)1 << 39 : (w < 0.0 ? -((int64_t)1 << 40) : 0);", TRT),
    ("ritr-clamp-low-sign", "srct", "src/render/render_internal.h", "    return w > 0.0 ? (int64_t)1 << 40 : (w < 0.0 ? -((int64_t)1 << 40) : 0);", "    return w > 0.0 ? (int64_t)1 << 40 : (w < 0.0 ? ((int64_t)1 << 40) : 0);", TRT),
    ("ritr-nan-not-zero", "srct", "src/render/render_internal.h", "    return w > 0.0 ? (int64_t)1 << 40 : (w < 0.0 ? -((int64_t)1 << 40) : 0);", "    return w > 0.0 ? (int64_t)1 << 40 : (w < 0.0 ? -((int64_t)1 << 40) : 1);", TRT),
    ("ritr-clamp-nan-as-high", "srct", "src/render/render_internal.h", "    return w > 0.0 ? (int64_t)1 << 40 : (w < 0.0 ? -((int64_t)1 << 40) : 0);", "    return w >= 0.0 || w != w ? (int64_t)1 << 40 : -((int64_t)1 << 40);", TRT),
    ("ri-filter-space-target", "src", IMF, "    filtering = own;\n", "    filtering = *target_space;\n", IMT),
    ("ri-tint-target-space", "src", RS, "\n    GSHSCENE_Color_Space space = color.space;\n    if (gshscene_color_space_is_target(&space)) {\n      space = *gshscene_job_target_space(run->job);", "\n    GSHSCENE_Color_Space space = color.space;\n    if (gshscene_color_space_is_target(&space)) {\n      space = frame->space;", IMT),
    ("ri-kernel-near", "src", IMF, "return (((21.0 * d - 36.0) * d) * d + 16.0) / 18.0;", "return (((21.0 * d - 36.0) * d) * d + 15.0) / 18.0;", IMT),
    ("ri-kernel-far", "src", IMF, "return (((-7.0 * d + 36.0) * d - 60.0) * d + 32.0) / 18.0;", "return (((-7.0 * d + 36.0) * d - 60.0) * d + 31.0) / 18.0;", IMT),
    ("ri-taps-nearest-rounds", "src", IMF, "t->i = gshcpu_render_trunc_i64(gmath_floor(c));\n    return;", "t->i = gshcpu_render_trunc_i64(gmath_floor(c + 0.5));\n    return;", IMT),
    ("ri-taps-center", "src", IMF, "const double s = c - 0.5;", "const double s = c;", IMT),
    ("ri-taps-linear-swapped", "src", IMF, "    t->w[0] = 1.0 - f;\n    t->w[1] = f;", "    t->w[0] = f;\n    t->w[1] = 1.0 - f;", IMT),
    ("ri-taps-cubic-first", "src", IMF, "t->w[0] = kernel(f + 1.0);", "t->w[0] = kernel(f);", IMT),
    ("ri-taps-cubic-last", "src", IMF, "t->w[3] = kernel(2.0 - f);", "t->w[3] = kernel(1.0 - f);", IMT),
    ("ri-nearest-xy", "src", IMF, "texel(im, x->i, y->i, out);", "texel(im, y->i, x->i, out);", IMT),
    ("ri-linear-column", "src", IMF, "texel(im, i + 1, j + b, t1);", "texel(im, i, j + b, t1);", IMT),
    ("ri-linear-row", "src", IMF, "texel(im, i, j + b, t0);", "texel(im, i, j, t0);", IMT),
    ("ri-linear-row-weights", "src", IMF, "out[k] = wy0 * r[0][k] + wy1 * r[1][k];", "out[k] = wy1 * r[0][k] + wy0 * r[1][k];", IMT),
    ("ri-cubic-tap-offset", "src", IMF, "texel(im, i - 1 + a, j - 1 + b, t[a]);", "texel(im, i + a, j + b, t[a]);", IMT),
    ("ri-cubic-clamp-dropped", "src", IMF, "    s = s < lo[k] ? lo[k] : s;\n    out[k] = s > hi[k] ? hi[k] : s;", "    out[k] = s;", IMT),
    ("ri-cubic-clamp-low", "src", IMF, "    s = s < lo[k] ? lo[k] : s;\n    out[k] = s > hi[k] ? hi[k] : s;", "    out[k] = s > hi[k] ? hi[k] : s;", IMT),
    ("ri-cubic-clamp-high", "src", IMF, "    s = s < lo[k] ? lo[k] : s;\n    out[k] = s > hi[k] ? hi[k] : s;", "    out[k] = s < lo[k] ? lo[k] : s;", IMT),
    ("ri-cubic-clamp-range", "src", IMF, "          lo[k] = t[a][k] < lo[k] ? t[a][k] : lo[k];\n          hi[k] = t[a][k] > hi[k] ? t[a][k] : hi[k];", "          lo[k] = t[a][k] < lo[k] ? t[a][k] : lo[k];\n          hi[k] = hi[k];", IMT),
    ("ri-source-multiplier", "src", IMF, "    src[k] = f[k] * im->mult;", "    src[k] = f[k];", IMT),
    ("ri-tint-multiplier", "src", IMF, "const double k = (im->mult * im->tint_alpha) * f[3];", "const double k = im->tint_alpha * f[3];", IMT),
    ("ri-tint-alpha", "src", IMF, "const double k = (im->mult * im->tint_alpha) * f[3];", "const double k = im->mult * f[3];", IMT),
    ("ri-tint-premultiplied", "src", IMF, "    src[0] = im->tint[0] * k;", "    src[0] = im->tint[0];", IMT),
    ("ri-area-corner", "src", IMF, "const double u = (qx - 0.5) - im->inv[4];", "const double u = qx - im->inv[4];", IMT),
    ("ri-area-edge-b", "src", IMF, "const double eb[2] = {im->inv[1], im->inv[3]};", "const double eb[2] = {im->inv[3], im->inv[1]};", IMT),
    ("ri-inverse-translation", "src", IMF, "const double u = qx - im->inv[4];", "const double u = qx + im->inv[4];", IMT),
    ("ri-domain-y", "src", IMF, "if (!(px >= -DOMAIN && px <= DOMAIN && py >= -DOMAIN && py <= DOMAIN)) {", "if (!(px >= -DOMAIN && px <= DOMAIN && py == py)) {", IMT),
    ("ri-domain-x", "src", IMF, "if (!(px >= -DOMAIN && px <= DOMAIN && py >= -DOMAIN && py <= DOMAIN)) {", "if (!(px == px && py >= -DOMAIN && py <= DOMAIN)) {", IMT),
    ("rit-copy-cubic", "src", IMF, "integral &&\n      im->filter != (uint32_t)GSHSCENE_IMAGE_FILTER_CUBIC) {", "integral) {", IMT),
    ("rit-copy-integral", "src", IMF, "a[4] == gmath_floor(a[4]) && a[5] == gmath_floor(a[5]) &&", "a[4] == gmath_floor(a[4]) &&", IMT),
    ("rit-copy-sign", "src", IMF, "im->copy_dx = gshcpu_render_trunc_i64(a[4]);", "im->copy_dx = -gshcpu_render_trunc_i64(a[4]);", IMT),
    ("rit-copy-linear-part", "src", IMF, "a[0] == 1.0 && a[1] == 0.0 && a[2] == 0.0 && a[3] == 1.0 && integral", "a[0] == 1.0 && a[3] == 1.0 && integral", IMT),
    ("rit-separable-key", "src", IMF, "im->hoisted_key != qy", "im->hoisted_key != qx", IMT),
    ("rit-separable-anti", "src", IMF, "diagonal ? im->inv[3] * v : im->inv[1] * v", "diagonal ? im->inv[3] * v : im->inv[2] * v", IMT),
    ("rit-separable-shear", "src", IMF, "if (a[1] == 0.0 && a[2] == 0.0 && a[0] != 0.0 && a[3] != 0.0) {", "if (a[1] == 0.0 && a[0] != 0.0 && a[3] != 0.0) {", IMT),
    ("rit-separable-area", "src", IMF, "(tiers & GSHCPU_RENDER_TIER_IMAGE_SEPARABLE) && im->filter != (uint32_t)GSHSCENE_IMAGE_FILTER_AREA", "(tiers & GSHCPU_RENDER_TIER_IMAGE_SEPARABLE)", IMT),
    ("rit-table-scale", "src", IMF, "lut[v] = (double)v / 255.0;", "lut[v] = (double)v / 254.0;", IMT),
    ("rit-table-never", "src", IMF, "if ((tiers & GSHCPU_RENDER_TIER_IMAGE_TABLE) && lut", "if (0 && lut", IMT),
    ('ria-rect-reflect-once', "src", IMA, 'sum_add_scaled(acc, gshcpu_render_i64_to_double(whole) * (reflect ? 2.0 : 1.0), v);', 'sum_add_scaled(acc, gshcpu_render_i64_to_double(whole) * (reflect ? 1.0 : 1.0), v);', IMT),
    ('ria-rect-reflect-period', "src", IMA, 'const int64_t period = reflect ? 2 * n : n;', 'const int64_t period = n;', IMT),
    ('ria-rect-rest-from', "src", IMA, 'from = ya + whole * period;', 'from = ya;', IMT),
    ('ria-rect-pad-above', "src", IMA, 'const int64_t above = (yb < y0 ? yb : y0) - ya;', 'const int64_t above = (yb < y0 ? yb : y0) - ya + 1;', IMT),
    ('ria-rect-pad-below', "src", IMA, 'const int64_t below = yb - (ya > y1 ? ya : y1);', 'const int64_t below = yb - y1;', IMT),
    ('ria-rect-top-rows', "src", IMA, 'for (int64_t j = rj0; j < rj1 && (yb <= ya || j < ya); j++) {', 'for (int64_t j = rj0; j < rj1 && j < ya; j++) {', IMT),
    ('ria-rect-bottom-rows', "src", IMA, 'for (int64_t j = (yb > ya ? yb : rj1); j < rj1; j++) {', 'for (int64_t j = rj1; j < rj1; j++) {', IMT),
    ('ria-rect-bound-rows', "src", IMA, 'const uint64_t rows = repeats ? sat_add(sat_mul(3u, ny), 2u) : sat_add(ny, 4u);', 'const uint64_t rows = repeats ? sat_add(ny, 4u) : sat_add(ny, 4u);', IMT),
    ('ria-rect-bound-run', "src", IMA, '*out = sat_add(sat_mul(cy, sat_add(run, 3u)), 8u);', '*out = sat_add(sat_mul(cy, run), 8u);', IMT),
    ("ria-row-sum-key", "src", IMA, "if (!slot->valid || slot->row != iy) {", "if (!slot->valid) {", IMT),
    ("ria-pad-left", "src", IMA, "const int64_t left = ia < x0 ? (ib < x0 ? ib : x0) - ia : 0;", "const int64_t left = ia < x0 ? (ib < x0 ? ib : x0) - ia + 1 : 0;", IMT),
    ("ria-pad-right", "src", IMA, "const int64_t right = ib > x1 ? ib - (ia > x1 ? ia : x1) : 0;", "const int64_t right = ib > x1 ? ib - x1 : 0;", IMT),
    ("ria-pad-mid", "src", IMA, "for (int64_t i = ia > x0 ? ia : x0; i < (ib < x1 ? ib : x1); i++) {\n        gshcpu_render_image_window_texel(im, (uint32_t)i, (uint32_t)ty, t);\n        sum_add(acc, t);\n        im->visits++;\n      }\n      if (right > 0) {", "for (int64_t i = ia > x0 ? ia : x0; i < (ib < x1 ? ib : x1) - 1; i++) {\n        gshcpu_render_image_window_texel(im, (uint32_t)i, (uint32_t)ty, t);\n        sum_add(acc, t);\n        im->visits++;\n      }\n      if (right > 0) {", IMT),
    ("ria-whole-periods", "src", IMA, "const int64_t whole = (ib - ia) / period;", "const int64_t whole = (ib - ia) / period + 1;", IMT),
    ("ria-reflect-doubled", "src", IMA, "(im->extend_x == GSHSCENE_IMAGE_EXTEND_REPEAT ? 1.0 : 2.0)", "(im->extend_x == GSHSCENE_IMAGE_EXTEND_REPEAT ? 1.0 : 1.0)", IMT),
    ("ria-reflect-period", "src", IMA, "im->extend_x == GSHSCENE_IMAGE_EXTEND_REPEAT ? n : 2 * n;", "im->extend_x == GSHSCENE_IMAGE_EXTEND_REPEAT ? n : n;", IMT),
    ("ria-remainder-start", "src", IMA, "from = ia + whole * period;", "from = ia;", IMT),
    ("ria-clip-half-plane", "src", IMA, "const double da = edge == 0 ? ax : edge == 1 ? 1.0 - ax : edge == 2 ? ay : 1.0 - ay;", "const double da = edge == 0 ? ax : edge == 1 ? ax - 1.0 : edge == 2 ? ay : 1.0 - ay;", IMT),
    ("ria-clip-area-half", "src", IMA, "return (a < 0.0 ? -a : a) / 2.0;", "return (a < 0.0 ? -a : a);", IMT),
    ("ria-run-floor", "src", IMA, "ra = gshcpu_render_trunc_i64(-gmath_floor(-dmax(l0, l1)));", "ra = gshcpu_render_trunc_i64(gmath_floor(dmax(l0, l1)));", IMT),
    ("ria-run-ceil", "src", IMA, "rb = gshcpu_render_trunc_i64(gmath_floor(dmin(r0, r1)));", "rb = gshcpu_render_trunc_i64(-gmath_floor(-dmin(r0, r1)));", IMT),
    ("ria-vertex-row", "src", IMA, "vertex_inside = vertex_inside || (vy[k] > gshcpu_render_i64_to_double(j) && vy[k] < gshcpu_render_i64_to_double(j + 1));", "vertex_inside = 0;", IMT),
    ("ria-rectangle-weight", "src", IMA, "    const double w = wx * wy;", "    const double w = wx;", IMT),
    ("ria-rectangle-run", "src", IMA, "        sum_add_scaled(acc, wy, r);", "        sum_add_scaled(acc, 1.0, r);", IMT),
    ('ria-rectangle-rows', "src", IMA, 'for (int64_t j = rj0; j < rj1 && (yb <= ya || j < ya); j++) {\n    const double wy = dmin(gshcpu_render_i64_to_double(j + 1), yr) - dmax(gshcpu_render_i64_to_double(j), yl);', 'for (int64_t j = rj0; j < rj1 && (yb <= ya || j < ya); j++) {\n    const double wy = 1.0;', IMT),
    ('ria-rectangle-rows-bottom', "src", IMA, 'for (int64_t j = (yb > ya ? yb : rj1); j < rj1; j++) {\n    const double wy = dmin(gshcpu_render_i64_to_double(j + 1), yr) - dmax(gshcpu_render_i64_to_double(j), yl);', 'for (int64_t j = (yb > ya ? yb : rj1); j < rj1; j++) {\n    const double wy = 1.0;', IMT),
    ("ria-mean-divisor", "src", IMA, "    mean[k] = mean[k] / area;", "    mean[k] = mean[k];", IMT),
    ("ria-bound-boundary", "src", IMA, "  b = sat_add(b, sat_mul(4u, sat_add(cx, cy))); /* the cells the boundary crosses */\n", "", IMT),
    ("ria-bound-vertex-rows", "src", IMA, "  b = sat_add(b, sat_mul(8u, cx)); /* the rows holding a vertex are clipped across */\n", "", IMT),
    ("ria-bound-rows", "src", IMA, "  uint64_t b = sat_mul(cy, run);", "  uint64_t b = sat_mul(cy, run > 0u ? 1u : 1u);", IMT),
    ("ria-bound-wide", "src", IMA, "if (!(ex <= WIDEST && ey <= WIDEST)) {", "if (0) {", IMT),
    ("ris-filter-choice", "src", RS, "const uint32_t filter = gshgeom_map_stretches_every_direction(&composed) ? ip.magnify : ip.minify;", "const uint32_t filter = gshgeom_map_stretches_every_direction(&composed) ? ip.minify : ip.magnify;", IMT),
    ("ris-chain-no-paint-map", "src", RS, "  run->maps[count] = ip.map;\n", "  run->maps[count] = item->map;\n", IMT),
    ("ris-chain-no-target", "src", RS, "gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &composed) != GSHGEOM_OK) {\n    p->refused = 1;", "gshgeom_map_compose_chain(run->maps, count + 1, NULL, &composed) != GSHGEOM_OK) {\n    p->refused = 1;", IMT),
    ("ris-domain-unchecked", "src", RS, "!gshcpu_render_image_place(&p->im, &composed) || !gshcpu_render_image_in_domain(&p->im, whole)) {", "!gshcpu_render_image_place(&p->im, &composed)) {", IMT),
    ("ris-domain-in-damage", "src", RS, "!gshcpu_render_image_in_domain(&p->im, whole)) {", "!gshcpu_render_image_in_domain(&p->im, rect)) {", IMT),
    ("ris-tint-unconverted", "src", RS, "    gshcpu_render_convert_straight(&c, tint, tint);\n", "", IMT),
    ("ris-tint-alpha", "src", RS, "    tint_alpha = color.rgba[3];", "    tint_alpha = 1.0;", IMT),
    ("ris-multiplier", "src", RS, "gshcpu_render_image_source(&p->im, item->paint.rgba[3], tint, tint_alpha);", "gshcpu_render_image_source(&p->im, 1.0, tint, tint_alpha);", IMT),
    ("ris-area-bound-skipped", "src", RS, "  if (filter == (uint32_t)GSHSCENE_IMAGE_FILTER_AREA) {\n    /* IC9:", "  if (0) {\n    /* IC9:", IMT),
    ("ris-pixel-bound", "src", RS, "pixel > run->cs->max_area_pixel_work ||", "pixel > run->cs->max_area_pixel_work + 1 ||", IMT),
    ("ris-item-bound", "src", RS, "item_bound > run->cs->max_image_work) {", "item_bound > run->cs->max_image_work + 1) {", IMT),
    ("ris-bound-rect", "src", RS, "const uint64_t w = (uint64_t)(whole.x1 - whole.x0);", "const uint64_t w = (uint64_t)(rect.x1 - rect.x0);", IMT),
    ("ris-caps-extends", "src", RS, "out->image_extends = (UINT32_C(1) << GSHSCENE_IMAGE_EXTEND_COUNT) - 1u;", "out->image_extends = (UINT32_C(1) << GSHSCENE_IMAGE_EXTEND_COUNT) - 3u;", IMT),
    ("ris-caps-filters", "src", RS, "out->image_filters = (UINT32_C(1) << GSHSCENE_IMAGE_FILTER_COUNT) - 1u;", "out->image_filters = (UINT32_C(1) << GSHSCENE_IMAGE_FILTER_COUNT) - 2u;", IMT),
    ("ris-caps-formats", "src", RS, "out->image_formats = GSHSCENE_IMAGE_FORMAT_ALL;", "out->image_formats = GSHSCENE_IMAGE_FORMAT_ALL & ~GSHSCENE_IMAGE_FORMAT_FLOAT32;", IMT),
    ("ris-caps-spaces", "src", RS, "  out->flags |= GSHSCENE_CAP_IMAGE_FILTER_SPACES;\n", "", IMT),
    ("ris-caps-work-measure", "src", RS, '"cells+points+pixels+texels"', '"cells+points+pixels"', IMT),
    ("ris-cancel-bound", "src", RS, "|| !gcu_safe_add_u64(b, cs->max_image_work, &b)) {", ") {", IMT),
    ("ris-byte-layers", "src", RS, "      *wide = *wide || item->paint.kind == GSHSCENE_PAINT_IMAGE;\n", "", IMT),
    ("ris-stroke-solid", "src", RS, "int solid = item->paint.kind != GSHSCENE_PAINT_GRADIENT && item->paint.kind != GSHSCENE_PAINT_IMAGE;", "int solid = item->paint.kind != GSHSCENE_PAINT_GRADIENT;", IMT),
    ("ris-clips-unweighed", "src", RS, "      uint32_t m = clipped ? weigh_clips(run, frame->clip_base, run->clip_count, m8[i], px, y) : m8[i];\n      if (m == 0) {\n        continue;\n      }\n      double s[4];", "      uint32_t m = m8[i];\n      if (m == 0) {\n        continue;\n      }\n      double s[4];", IMT),
    ("ris-sample-corner", "src", RS, "gshcpu_render_image_at(&p->im, (double)(int32_t)px + 0.5, (double)(int32_t)y + 0.5, s);", "gshcpu_render_image_at(&p->im, (double)(int32_t)px, (double)(int32_t)y, s);", IMT),
    ("ris-dash-cap-unread", "src", RS, "    cs->geometry.max_dash_steps = limits->max_dash_steps;\n", "", IMT + ["testRender_limits"]),
    ("ris-tile-base-counted-twice", "src", RS, "peak, used + bytes, sat_add(base, unit));", "peak, used + bytes, sat_add(base, sat_add(unit, base)));", ["testRender_pattern_nesting"]),
    ("ris-work-cap-unread", "src", RS, "    if (limits->max_work) {\n      cs->geometry.max_work = limits->max_work;\n    }\n", "", ["testRender_limits"]),
    ("ris-notice-image", "src", RS, "gshscene_paint_notice(item, run->maps, count - 1, &run->job->target.map)) {", "0) {", IMT),
]


# Masks-images-patterns story M2: the soft mask in the sink (MC1 to MC10).  Written with the story; the sweep is deferred to the
# consolidated pass.  The id says the rule: rm- the value (mask.c, MC3) and its pop (MC4), then, by name, the region and the frame
# (MC2), the limits (MC6), the layout (MC5), cancel and work (MC7), the tiers (MC8).  A mutant of a statement that cannot change a
# result is not planted: the waist clamps the transfer's input and output (the clamp of the alpha path and of the final value are
# redundant to it), the clearing of the skip state at the start of a pass and its restore at the end (it is clear on both sides: the pop was not
# skipped), the flag of fills a picture holds (only the direct 8-bit stroke tier reads it, and a list with a group never takes it), and
# the maps the measure sets for the geometry of an item that the arena's need does not depend on.
MKF = "src/render/mask.c"
MKT = ["testRender_mask_vectors", "testRender_mask", "testRender_conformance"]
MKS = ["testRender_mask", "testRender_mask_vectors", "testRender_conformance"]
MKP = ["testRender_mask", "probe"]
MKM = ["testRender_mask"]
PLANTS += [
    # MC3: the value
    ("rm-alpha-reads-luminance", "src", MKF, "e->alpha = m.mode == (uint32_t)GSHSCENE_MASK_ALPHA;", "e->alpha = 0;", MKT),
    ("rm-luminance-reads-alpha", "src", MKF, "e->alpha = m.mode == (uint32_t)GSHSCENE_MASK_ALPHA;", "e->alpha = 1;", MKT),
    ("rm-acts-on-inverted", "src", MKF, "e->shape = m.acts_on == (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE;", "e->shape = m.acts_on != (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE;", MKT),
    ("rm-acts-on-ignored", "src", MKF, "e->shape = m.acts_on == (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE;", "e->shape = 0;", MKT),
    ("rm-identity-always", "src", MKF, "e->identity = m.transfer.kind == (uint32_t)GSHSCENE_TRANSFER_IDENTITY;", "e->identity = 1;", MKT),
    ("rm-identity-never", "src", MKF, "e->identity = m.transfer.kind == (uint32_t)GSHSCENE_TRANSFER_IDENTITY;", "e->identity = 0;", MKS),
    ("rm-transfer-skipped-for-all", "src", MKF, "if (identity_tier && e->identity) {", "if (identity_tier) {", MKT),
    ("rm-coefficients-reversed", "src", MKF, "e->k[i] = res.k[i];", "e->k[i] = res.k[2 - i];", MKT),
    ("rm-coefficient-red-for-all", "src", MKF, "e->k[i] = res.k[i];", "e->k[i] = res.k[0];", MKT),
    ("rm-coefficients-on-the-wrong-channels", "src", MKF, "e->k[0] * over[0] + e->k[1] * over[1] + e->k[2] * over[2]", "e->k[0] * over[2] + e->k[1] * over[1] + e->k[2] * over[0]", MKT),
    ("rm-backdrop-not-converted", "src", MKF, "gshcpu_render_convert_prepare(&backdrop, &res.backdrop_space, &res.luminance_space);", "gshcpu_render_convert_prepare(&backdrop, &res.backdrop_space, &res.backdrop_space);", MKT),
    ("rm-backdrop-ignored", "src", MKF, "over[i] = gshcpu_render_saturate(c[i] + (1.0 - a) * e->backdrop[i]);", "over[i] = gshcpu_render_saturate(c[i]);", MKT),
    ("rm-backdrop-not-weighed-by-alpha", "src", MKF, "c[i] + (1.0 - a) * e->backdrop[i]", "c[i] + 1.0 * e->backdrop[i]", MKT),
    ("rm-backdrop-weighed-by-alpha", "src", MKF, "c[i] + (1.0 - a) * e->backdrop[i]", "c[i] + a * e->backdrop[i]", MKT),
    ("rm-picture-not-converted-into-l", "src", MKF, "gshcpu_render_convert_premultiplied(&e->to_l, g, c);", "memcpy(c, g, sizeof c);", MKT),
    ("rm-conversion-backwards", "src", MKF, "gshcpu_render_convert_prepare(&e->to_l, &res.render_space, &res.luminance_space);", "gshcpu_render_convert_prepare(&e->to_l, &res.luminance_space, &res.render_space);", MKT),
    ("rm-luminance-of-the-straight-color", "src", MKF, "over[i] = gshcpu_render_saturate(c[i] + (1.0 - a) * e->backdrop[i]);", "over[i] = gshcpu_render_saturate((a > 0.0 ? c[i] / a : 0.0) + (1.0 - a) * e->backdrop[i]);", MKT),
    ("rm-luminance-clamp-dropped", "src", MKF, "y = clamp01(gshcpu_render_saturate(e->k[0] * over[0] + e->k[1] * over[1] + e->k[2] * over[2]));", "y = gshcpu_render_saturate(e->k[0] * over[0] + e->k[1] * over[1] + e->k[2] * over[2]);", MKT),
    ("rm-frame-space-is-the-enclosing", "src", MKF, "e->frame_space = res.render_space;", "e->frame_space = *enclosing;", MKT),
    # MC4: applying it, and MC1: the order of the reasons
    ("rm-opacity-not-weighed", "src", RS, "const double ov = o * v; /* MC4: the opacity source is o v */", "const double ov = o; /* MC4 */", MKT),
    ("rm-shape-not-weighed", "src", RS, "weight = weight * v; /* MC4: the shape is weighed by v, the opacity as it was */", "weight = weight; /* MC4 */", MKT),
    ("rm-shape-merged-into-opacity", "src", RS, "weight = weight * v; /* MC4: the shape is weighed by v, the opacity as it was */\n        for (int i = 0; i < 4; i++) {\n          s[i] = o * s[i];", "weight = weight * v; /* MC4 */\n        for (int i = 0; i < 4; i++) {\n          s[i] = o * v * s[i];", MKT),
    ("rm-skip-of-a-zero-source-dropped", "src", RS, "          s[i] = ov * s[i];\n        }\n      }\n      if (bounded && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0) {\n        continue;\n      }\n      if (!gshcpu_render_apply(&parent->layer[4u * p], s, weight, op)) {", "          s[i] = ov * s[i];\n        }\n      }\n      (void)bounded;\n      if (!gshcpu_render_apply(&parent->layer[4u * p], s, weight, op)) {", MKT),
    # Removed, masks-images-patterns story M3: rm-masked-group-claims-isolation (the soft-mask claim with knockout): the sink claims knockout now (KC1)
    ("rm-mask-features-unclaimed", "src", RS, "out->mask_native = GSHSCENE_MASK_NATIVE_ALL;", "out->mask_native = GSHSCENE_MASK_NATIVE_ALL & ~(uint32_t)GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING;", ["testRender", "testRender_mask", "testRender_conformance"]),
    ("rm-mask-flag-unclaimed", "src", RS, "out->flags |= GSHSCENE_CAP_GROUP_SOFT_MASK;", "out->flags |= 0;", ["testRender", "testRender_mask", "testRender_conformance"]),
    # MC2: where and when the mask is drawn, and the frame
    ("rm-mask-over-the-groups-rectangle", "src", RS, "GSHSCENE_Result r = mask_pass(run, &mp->me, chain, count, region, whole);", "(void)region;\n  GSHSCENE_Result r = mask_pass(run, &mp->me, chain, count, f->dirty, whole);", MKS),
    ("rm-mask-over-the-whole-region", "src", RS, "GSHSCENE_Result r = mask_pass(run, &mp->me, chain, count, region, whole);", "(void)region;\n  GSHSCENE_Result r = mask_pass(run, &mp->me, chain, count, run->layout.region, whole);", MKS),
    ("rm-clips-around-the-group-in-force-in-the-mask", "src", RS, "  f->clip_base = run->clip_count;\n  if (run->general) {\n    /* The mask frame is its own isolated", "  f->clip_base = run->frames[run->top - 2u].clip_base;\n  if (run->general) {\n    /* The mask frame is its own isolated", MKT),
    ("rm-mask-frame-starts-nonzero", "src", RS, "  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = me->frame_space;", "  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = me->frame_space;\n  f->dirty = run->layout.region;", MKS),
    ("rm-placement-dropped", "src", RS, "  run->maps[count - 1u] = group->group.soft_mask_map;\n", "", MKS),
    ("rm-mask-frame-not-released", "src", RS, "  run->top = top; /* the mask frame is released at the end of the pop: nothing below reads another */\n", "", MKS),
    ("rm-chain-names-the-pop", "src", RS, "  ms->chain[count - 1u] = mfs[run->top].push;", "  ms->chain[count - 1u] = chain[count - 1u];", MKS),
    ("rm-push-index-not-kept", "src", RS, "    mf->push = index;", "    mf->push = 0;\n    (void)index;", MKS),
    ("rm-push-index-from-the-wrong-unit", "src", RS, "      group_push(run, item, chain[count - 1u]);", "      group_push(run, item, 0);", MKS),
    ("rm-pop-composites-nothing-of-an-empty-whole", "src", RS, "  if (gshcpu_render_rect_empty(whole)) {\n    run->top--;\n    run->groups_open--;\n    return GSHSCENE_OK; /* the whole render composites nothing of this group, and draws no mask for it */\n  }", "  if (gshcpu_render_rect_empty(region)) {\n    run->top--;\n    run->groups_open--;\n    return GSHSCENE_OK;\n  }", MKS),
    ("rm-window-not-restored", "src", RS, "  ms->window = saved_window;\n", "  (void)saved_window;\n", MKS),
    ("rm-whole-window-not-restored", "src", RS, "  ms->whole_window = saved_whole;\n", "  (void)saved_whole;\n", MKS),
    # MC6: limits are the whole render's
    ("rm-cell-cap-over-the-pop-region", "src", RS, "static GSHCPU_Rect whole_reach_of(const Run * run, size_t from, size_t to) {\n  GSHCPU_Rect r = run->layout.whole;", "static GSHCPU_Rect whole_reach_of(const Run * run, size_t from, size_t to) {\n  GSHCPU_Rect r = run->masked && ms_of(run)->walk_used ? ms_of(run)->window : run->layout.whole;", MKS),
    ("rm-whole-pop-region-is-the-damaged", "src", RS, "    whole = bounded ? frames[run->top].whole_dirty : ms_of(run)->whole_window;", "    whole = bounded ? f->dirty : window_of(run);", MKS),
    ("rm-whole-dirty-not-noted", "src", RS, "    mf->whole_dirty = gshcpu_render_rect_join(mf->whole_dirty, whole);\n  }\n}", "    (void)mf;\n  }\n}", MKS),
    ("rm-whole-dirty-not-noted-for-the-damaged-rectangle", "src", RS, "  if (run->masked && !gshcpu_render_rect_empty(whole)) {\n    Mask_Frame * mf = &ms_of(run)->frames[f - run->frames];", "  if (run->masked && !gshcpu_render_rect_empty(r)) {\n    Mask_Frame * mf = &ms_of(run)->frames[f - run->frames];", MKS),
    ("rm-child-pop-not-joined-into-the-parent-whole", "src", RS, "      frames[run->top - 1u].whole_dirty = gshcpu_render_rect_join(frames[run->top - 1u].whole_dirty, whole);", "      (void)frames;", MKT),
    ("rm-whole-window-of-a-pass-is-the-whole-region", "src", RS, "  ms->whole_window = whole_window;\n  ms->walk_used += bytes;", "  ms->whole_window = run->layout.whole;\n  (void)whole_window;\n  ms->walk_used += bytes;", MKS),
    ("rm-pop-reads-the-groups-unready-layer", "src", RS, "      double s[4];\n      gshcpu_render_convert_premultiplied(&c, inside(f->dirty, x, y) ? &f->layer[4u * p] : zero, s);\n      double weight", "      double s[4];\n      gshcpu_render_convert_premultiplied(&c, 1 ? &f->layer[4u * p] : zero, s);\n      double weight", MKS),
    ("rm-pop-reads-the-masks-unready-layer", "src", RS, "  if (inside(mp->mf->dirty, x, y)) {\n    return gshcpu_render_mask_value(&mp->me, &mp->mf->layer[4u * p], mp->identity_tier);", "  (void)x;\n  (void)y;\n  if (1) {\n    return gshcpu_render_mask_value(&mp->me, &mp->mf->layer[4u * p], mp->identity_tier);", MKS),
    ("rm-whole-dirty-of-the-frame-not-reset", "src", RS, "    memset(&mf->whole_dirty, 0, sizeof mf->whole_dirty);\n    mf->push = index;", "    mf->push = index;", MKS),
    # MC5: the layout
    ("rm-layout-mask-frame-omitted", "src", RS, "most_g = sat_add(sat_add(g, 1u), pn.groups) > most_g ? sat_add(sat_add(g, 1u), pn.groups) : most_g;", "most_g = sat_add(g, pn.groups) > most_g ? sat_add(g, pn.groups) : most_g;", MKS),
    ("rm-layout-pictures-clips-omitted", "src", RS, "most_c = sat_add(c, pn.clips) > most_c ? sat_add(c, pn.clips) : most_c;", "most_c = most_c;", MKS),
    ("rm-layout-pictures-gradients-omitted", "src", RS, "ok = ok && gcu_safe_add_size(*gradients, pn.gradients, gradients) && gcu_safe_add_size(*stops, pn.stops, stops);", "ok = ok;", MKS),
    ("rm-layout-pictures-strokes-omitted", "src", RS, "          *strokes = *strokes || pn.strokes;\n", "", MKS),
    ("rm-layout-pictures-aligned-omitted", "src", RS, "          *aligned = *aligned || pn.aligned;\n", "", MKS),
    ("rm-layout-nested-walk-omitted", "src", RS, "nest->walk = here > nest->walk ? here : nest->walk;", "nest->walk = nest->walk;\n            (void)here;", MKS),
    ("rm-layout-nested-walk-not-stacked", "src", RS, "const size_t here = sat_add(gshscene_expansion_bytes(gshscene_resource_picture(m.picture)), pn.walk);", "const size_t here = gshscene_expansion_bytes(gshscene_resource_picture(m.picture));", MKS),
    ("rm-layout-masked-list-not-wide", "src", RS, "          nest->masked = 1;\n          *wide = 1;", "          nest->masked = 1;", MKP),
    ("rm-layout-frame-records-short", "src", RS, "!place(&at, mul3(L->groups + 1u, sizeof(Mask_Frame), 1), &M->at_frames)", "!place(&at, mul3(1u, sizeof(Mask_Frame), 1), &M->at_frames)", MKS),
    ("rm-layout-chain-short", "src", RS, "!place(&at, mul3(L->levels + 1u, sizeof(size_t), 1), &M->at_chain)", "!place(&at, mul3(1u, sizeof(size_t), 1), &M->at_chain)", MKS),
    ("rm-layout-mask-area-always", "src", RS, "    M->bytes = at;\n  }", "    M->bytes = at;\n  }\n  M->bytes = M->bytes ? M->bytes : 16u;", MKS),
    ("rm-measure-skips-the-masks-pictures", "src", RS, "      r = measure_mask(cs, target, memory + M->at, M, item, maps, count, arena, peak, 0, 0);", "      r = GSHCPU_OK;\n      (void)M;", MKS),
    ("rm-measure-nested-mask-skipped", "src", RS, "      r = measure_mask(cs, target, area, M, item, maps, count, arena, peak, used + bytes, in_tile);", "      r = GSHCPU_OK;", MKS),
    ("rm-nested-walks-share-one-place", "src", RS, "      r = measure_mask(cs, target, area, M, item, maps, count, arena, peak, used + bytes, in_tile);", "      r = measure_mask(cs, target, area, M, item, maps, count, arena, peak, used, in_tile);", MKS),
    ("rm-pass-walk-not-stacked", "src", RS, "      gshscene_expansion_init(&walk, picture, ms->walk + ms->walk_used, bytes) != GSHSCENE_OK) {", "      gshscene_expansion_init(&walk, picture, ms->walk, bytes) != GSHSCENE_OK) {", MKS),
    ("rm-walk-not-released", "src", RS, "  ms->walk_used -= bytes;\n", "", MKS),
    # MC7: cancel and work
    ("rm-cancel-not-checked-in-the-pass", "src", RS, "    if (result == GSHSCENE_OK && gshscene_job_checkpoint(run->job)) {", "    if (0) {", MKS),
    ("rm-pop-work-one-pass", "src", RS, "    run->work += 2u * (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);", "    run->work += 1u * (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);", MKS),
    ("rm-pass-work-kept-by-the-pop", "src", RS, "    gshscene_job_work(run->job, run->work);\n    run->work = 0;\n    if (result == GSHSCENE_OK && gshscene_job_checkpoint", "    run->work = 0;\n    if (result == GSHSCENE_OK && gshscene_job_checkpoint", MKS),
    # MC8: the tiers
    ("rm-outside-tier-off", "src", RS, "  if (mp->outside_tier) {", "  if (mp->outside_tier && 0) {", MKS),
    ("rm-outside-value-zero-by-the-tier", "src", RS, "      mp->v_outside = gshcpu_render_mask_value(&mp->me, zero, mp->identity_tier);", "      mp->v_outside = 0.0;", MKT),
    ("rm-outside-value-zero-by-the-general-tier", "src", RS, "  return gshcpu_render_mask_value(&mp->me, zero, mp->identity_tier);\n}", "  return 0.0;\n}", MKT),
    ("rm-outside-value-taken-inside", "src", RS, "  if (inside(mp->mf->dirty, x, y)) {\n    return gshcpu_render_mask_value(&mp->me, &mp->mf->layer[4u * p], mp->identity_tier);", "  (void)x;\n  (void)y;\n  if (0) {\n    return gshcpu_render_mask_value(&mp->me, &mp->mf->layer[4u * p], mp->identity_tier);", MKT),
    ("rm-outside-value-skips-a-pixel", "src", RS, "      mp->have_outside = 1;\n    }\n    mp->took_outside = 1;", "      mp->have_outside = 1;\n      mp->took_outside = 1;\n      return 0.0;\n    }\n    mp->took_outside = 1;", MKS),
    ("rm-identity-tier-ignores-its-switch", "src", RS, "  mp->identity_tier = (run->cs->tiers & GSHCPU_RENDER_TIER_MASK_IDENTITY) != 0;", "  mp->identity_tier = 1;", MKS),
    ("rm-outside-tier-ignores-its-switch", "src", RS, "  mp->outside_tier = (run->cs->tiers & GSHCPU_RENDER_TIER_MASK_OUTSIDE) != 0;", "  mp->outside_tier = 1;", MKS),
    ("rm-tier-counter-not-counted", "src", RS, "  if (mp->took_outside) {\n    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, mask_outside));\n  }", "  (void)mp->took_outside;", MKS),
    ("rm-identity-counter-not-counted", "src", RS, "  if (mp->identity_tier && mp->me.identity && mp->evaluated) {\n    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, mask_identity));\n  }", "  (void)mp->evaluated;", MKS),
    # MC9: a group with no mask pays nothing
    ("rm-mask-area-in-every-list", "src", RS, "  run->masked = M.bytes != 0;", "  run->masked = 1;", MKS),
    # MC5, the layout's memo: a picture is walked once however many there are (the counter ``mask_pictures`` counts the walks), in the
    # allocator's room and, from the caller's memory, in the caller's buffer
    ("rm-memo-never-grows-in-the-allocators-room", "src", RN, "    table = memo->alloc->calloc_fn(memo->alloc->ctx, 1u, bytes);", "    table = NULL;", MKM),
    ("rm-memo-never-grows-in-the-workspace", "src", RN, "  } else if (memo->scratch_left >= bytes) {", "  } else if (0) {", MKM),
    ("rm-memo-forgets-what-it-held-on-growth", "src", RN, "      put(table, memo->cap * 2u, &memo->slots[i]);", "      (void)table;", MKM),
    ("rm-memo-remembers-nothing", "src", RN, "  put(memo->slots, memo->cap, &slot);\n  memo->used++;", "  (void)slot;", MKM),
    ("rm-memo-finds-nothing", "src", RN, "    if (memo->slots[at].key == key) {", "    if (0) {", MKM),
    ("rm-memo-finds-the-wrong-key", "src", RN, "    if (memo->slots[at].key == key) {", "    if (1) {", MKM),
    ("rm-memo-keeps-a-short-value", "src", RN, "  memcpy(slot.value, value, memo->value_bytes);", "  memcpy(slot.value, value, memo->value_bytes < 8u ? memo->value_bytes : 8u);", MKM),
    ("rm-memo-leaks-the-allocators-table", "src", RN, "  if (memo->heap) {\n    memo->alloc->free_fn(memo->alloc->ctx, memo->slots);\n  }\n  memo->heap = 0;", "  memo->heap = 0;", MKM + ["testRender_limits"]),
    ("rm-memo-allocates-under-caller-memory", "src", RS, "(request->flags & GSHSCENE_RENDER_CALLER_MEMORY) ? NULL : &cs->alloc, request->workspace", "&cs->alloc, request->workspace", MKM),
    # MC4, MK7: PLUS is affine in the source only below its clamp, so the opacity and the shape agree there and not above it
    ("rm-plus-opacity-weighed-twice", "src", RS, "const double ov = o * v; /* MC4: the opacity source is o v */", "const double ov = op == (uint32_t)GSHSCENE_COMPOSITE_PLUS ? o * v * v : o * v;", ["testRender_mask"]),
    ("rm-plus-shape-weighed-twice", "src", RS, "weight = weight * v; /* MC4: the shape is weighed by v, the opacity as it was */\n        for (int i = 0; i < 4; i++) {\n          s[i] = o * s[i];", "weight = weight * v; /* MC4 */\n        for (int i = 0; i < 4; i++) {\n          s[i] = (op == (uint32_t)GSHSCENE_COMPOSITE_PLUS ? o * v : o) * s[i];", ["testRender_mask"]),
    ("rm-unmasked-group-reaches-the-mask-code", "src", RS, "  if (run->masked && f->group.soft_mask) {\n    return group_pop_masked(run, chain, count, region, whole);\n  }", "  if (1) {\n    return group_pop_masked(run, chain, count, region, whole);\n  }", MKS),
]


RGF = "src/render/group.c"
PLANTS += [
    # Masks-images-patterns story M3: non-isolated and knockout groups (KC1 to KC11).  The id says the rule: backdrop and result (KC4 to KC6),
    # channels (KC3), layout (KC8), the step, the pop, caps and the default path (KC1, KC2, KC9).
    # Written with the story; the sweep of the generated plants is run in the consolidated pass.
    ("rj-knockout-backdrop-is-the-layer", "src", RS, "  if (g->knockout) {\n    initial_backdrop(run, (size_t)(frame - run->frames), p, bk);\n    b = bk;\n  }", "  if (0) {\n    initial_backdrop(run, (size_t)(frame - run->frames), p, bk);\n    b = bk;\n  }", ['testRender_group', 'probe']),
    ("rj-pop-knockout-backdrop-is-the-layer", "src", RS, "        if (pg->knockout) {\n          initial_backdrop(run, top - 1u, p, bk);\n          b = bk;\n        }", "        if (0) {\n          initial_backdrop(run, top - 1u, p, bk);\n          b = bk;\n        }", ['testRender_group', 'probe']),
    ("rj-plain-backdrop-is-the-initial-backdrop", "src", RS, "  if (gs_of(run)->frames[i].knockout) {\n    initial_backdrop(run, i, p, out);", "  if (1) {\n    initial_backdrop(run, i, p, out);", ['testRender_group', 'probe']),
    ("rj-backdrop-read-from-the-root", "src", RS, "  backdrop_of(run, i - 1u, p, b);\n  gshcpu_render_convert_premultiplied(&g->from_parent, b, out);", "  backdrop_of(run, 0, p, b);\n  gshcpu_render_convert_premultiplied(&g->from_parent, b, out);", ['testRender_group', 'probe']),
    ("rj-backdrop-not-converted-into-the-group", "src", RS, "  gshcpu_render_convert_premultiplied(&g->from_parent, b, out);\n}", "  memcpy(out, b, 4u * sizeof(double));\n}", ['testRender_group', 'probe']),
    ("rj-conversion-prepared-the-wrong-way", "src", RS, "gshcpu_render_convert_prepare(&g->from_parent, &parent->space, &f->space);", "gshcpu_render_convert_prepare(&g->from_parent, &f->space, &parent->space);", ['testRender_group', 'probe']),
    ("rj-result-not-converted-back", "src", RS, "      gshcpu_render_convert_premultiplied(&c, gres, s);\n      double weight = (double)m8 / 255.0;\n      if (masked && mp.me.shape) {", "      memcpy(s, gres, sizeof s);\n      double weight = (double)m8 / 255.0;\n      if (masked && mp.me.shape) {", ['testRender_group', 'probe']),
    ("rj-nonisolated-layer-starts-empty", "src", RS, "      if (g->nonisolated) {\n        const size_t i = (size_t)(f - run->frames);", "      if (0) {\n        const size_t i = (size_t)(f - run->frames);", ['testRender_group', 'probe']),
    ("rj-parent-not-made-ready", "src", RS, "    widen(run, f - 1, grown);\n  }", "    (void)run;\n  }", ['testRender_group', 'probe']),
    ("rj-initial-backdrop-left-in-the-result", "src", RGF, "    g[i] = gshcpu_render_saturate(l[i] - keep * l0[i]); /* T5: the initial backdrop taken out */", "    g[i] = gshcpu_render_saturate(l[i] - 0.0 * keep * l0[i]); /* T5: the initial backdrop taken out */", ['testRender_group', 'probe']),
    ("rj-result-alpha-is-the-layers", "src", RS, "  gshcpu_render_group_result(l, gf->ag ? gf->ag[p] : l[3], !gf->nonisolated, l0, g);", "  gshcpu_render_group_result(l, l[3], !gf->nonisolated, l0, g);", ['testRender_group', 'probe']),
    ("rj-result-keeps-the-whole-backdrop", "src", RGF, "  const double keep = 1.0 - ag;", "  const double keep = 0.0;", ['testRender_group', 'probe']),
    ("rj-group-shape-not-carried-through-a-child", "src", RS, "    g->ko_anc = pg->ko_anc || pg->knockout;", "    g->ko_anc = pg->knockout;", ['testRender_group', 'probe']),
    ("rj-group-shape-carried-by-every-group", "src", RS, "    g->ko_anc = pg->ko_anc || pg->knockout;", "    g->ko_anc = pg->ko_anc || pg->knockout || run->top > 0;", ['testRender_group']),
    ("rj-group-shape-carried-by-the-knockout-group-itself", "src", RS, "    g->ko_anc = pg->ko_anc || pg->knockout;", "    g->ko_anc = pg->ko_anc || g->knockout;", ['testRender_group', 'probe']),
    ("rj-group-alpha-not-carried-by-non-isolated", "src", RS, "    if (g->nonisolated) {\n      g->ag = gs->ag + gs->ag_used++ * run->layout.pixels;", "    if (0) {\n      g->ag = gs->ag + gs->ag_used++ * run->layout.pixels;", ['testRender_group', 'probe']),
    ("rj-plain-layer-under-knockout-does-not-step-its-shape", "src", RS, "    f->general = g->on || g->fg != NULL;", "    f->general = g->on;", ['testRender_group', 'probe']),
    ("rj-every-group-is-general", "src", RS, "    g->on = g->nonisolated || g->knockout || (run->cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;\n    if (g->nonisolated) {", "    g->on = 1;\n    if (g->nonisolated) {", ['testRender_group']),
    ("rj-force-switch-ignored", "src", RS, "    g->on = g->nonisolated || g->knockout || (run->cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;\n    if (g->nonisolated) {", "    g->on = g->nonisolated || g->knockout;\n    if (g->nonisolated) {", ['testRender_group']),
    ("rj-channels-not-released-at-the-pop", "src", RS, "  if (g.ag) {\n    gs->ag_used--;\n  }", "  if (0) {\n    gs->ag_used--;\n  }", ['testRender_group']),
    ("rj-shape-channel-not-released-at-the-pop", "src", RS, "  if (g.fg) {\n    gs->fg_used--;\n  }", "  if (0) {\n    gs->fg_used--;\n  }", ['testRender_group']),
    ("rj-group-channels-not-zeroed-where-made-ready", "src", RS, "      if (g->ag) {\n        memset(g->ag + p, 0, n * sizeof *g->ag);\n      }", "      if (0) {\n        memset(g->ag + p, 0, n * sizeof *g->ag);\n      }", ['testRender_group']),
    ("rj-group-shape-not-zeroed-where-made-ready", "src", RS, "      if (g->fg) {\n        memset(g->fg + p, 0, n * sizeof *g->fg);\n      }", "      if (0) {\n        memset(g->fg + p, 0, n * sizeof *g->fg);\n      }", ['testRender_group']),
    ("rj-layout-omits-the-alpha-channels", "src", RS, "      place(&at, mul3(G->ag_slots, L->pixels, sizeof(double)), &G->at_ag) &&", "      place(&at, 0, &G->at_ag) &&", ['testRender_group']),
    ("rj-layout-omits-the-shape-channels", "src", RS, "      place(&at, mul3(G->fg_slots, L->pixels, sizeof(double)), &G->at_fg) &&", "      place(&at, 0, &G->at_fg) &&", ['testRender_group']),
    ("rj-layout-counts-one-alpha-channel-too-many", "src", RS, "        most_ag = open_ag > most_ag ? open_ag : most_ag;", "        most_ag = open_ag + 1u > most_ag ? open_ag + 1u : most_ag;", ['testRender_group']),
    ("rj-layout-counts-one-shape-channel-too-many", "src", RS, "        most_fg = open_fg > most_fg ? open_fg : most_fg;", "        most_fg = open_fg + 1u > most_fg ? open_fg + 1u : most_fg;", ['testRender_group']),
    ("rj-layout-counts-an-isolated-group-alpha", "src", RS, "        if (ni) {\n          open_ag++;\n        }", "        if (1) {\n          open_ag++;\n        }", ['testRender_group']),
    ("rj-layout-counts-no-shape-under-an-ancestor", "src", RS, "        if (nest->ko > 0) {\n          open_fg++;\n        }", "        if (0) {\n          open_fg++;\n        }", ['testRender_group']),
    ("rj-layout-knockout-count-not-undone", "src", RS, "          if (bits & 2u) {\n            nest->ko--;\n          }", "          if (0) {\n            nest->ko--;\n          }", ['testRender_group']),
    ("rj-layout-alpha-count-not-undone", "src", RS, "          if (bits & 1u) {\n            open_ag--;\n          }", "          if (0) {\n            open_ag--;\n          }", ['testRender_group']),
    ("rj-layout-shape-count-not-undone", "src", RS, "        if (nest->ko > 0) {\n          open_fg--; /* it carried a group shape exactly when a knockout group was open around it, which is as many as now */\n        }", "        if (0) {\n          open_fg--; /* it carried a group shape exactly when a knockout group was open around it, which is as many as now */\n        }", ['testRender_group']),
    ("rj-layout-mask-picture-inherits-the-knockout-ancestors", "src", RS, "  Nest sub = {nest->memo, nest->cache, 0, 0, 0, 0, nest->bits, 0};", "  Nest sub = {nest->memo, nest->cache, 0, 0, nest->ko, 0, nest->bits, 0};", ['testRender_group']),
    ("rj-layout-instance-groups-lose-the-open-ones", "src", RS, "        most_ag = open_ag + iag > most_ag ? open_ag + iag : most_ag;", "        most_ag = iag > most_ag ? iag : most_ag;", ['testRender_group']),
    ("rj-layout-mask-picture-groups-lose-the-open-ones", "src", RS, "          most_fg = sat_add(open_fg, pn.fg) > most_fg ? sat_add(open_fg, pn.fg) : most_fg;", "          most_fg = pn.fg > most_fg ? pn.fg : most_fg;", ['testRender_group']),
    ("rj-layout-plain-groups-take-a-stack-entry", "src", RS, "        if ((ni || ko) && !gshcpu_bits_push(", "        if (!gshcpu_bits_push(", ['testRender_group']),
    ("rj-layout-pop-matches-the-wrong-depth", "src", RS, "(gshcpu_bits_top(nest->bits) >> 2) == nest->depth) {", "(gshcpu_bits_top(nest->bits) >> 2) != nest->depth) {", ['testRender_group']),
    ("rj-layout-no-room-is-an-allocation-failure", "src", RS, "    return nest.oom && !(request->flags & GSHSCENE_RENDER_CALLER_MEMORY) ? GSHCPU_ERR_OOM : GSHCPU_ERR_LIMIT;", "    return nest.oom ? GSHCPU_ERR_OOM : GSHCPU_ERR_LIMIT;", ['testRender_group', 'testRender_conformance']),
    ("rj-layout-allocates-under-caller-memory-for-the-stack", "src", RN, "  } else {\n    const size_t bytes = (sizeof *c + (MEMO_ALIGN - 1u)) & ~(size_t)(MEMO_ALIGN - 1u);", "  } else if (0) {\n    const size_t bytes = (sizeof *c + (MEMO_ALIGN - 1u)) & ~(size_t)(MEMO_ALIGN - 1u);", ['testRender_group']),
    ("rj-layout-forced-switch-leaves-the-bytes-layers", "src", RS, "    L->wide = 1;\n    G->records", "    G->records", ['testRender_group', 'probe']),
    ("rj-stack-pop-keeps-the-emptied-chunk", "src", RN, "  if (at == 0) {\n    /* The chunk is empty: the top is the one before it (kept linked, for the next push to reuse). */\n    bits->top = bits->top->prev;\n  }", "", ['testRender_group']),
    ("rj-stack-push-never-moves-to-the-next-chunk", "src", RN, "  if (bits->len == 0 || at == 0) {", "  if (bits->len == 0) {", ['testRender_group']),
    ("rj-stack-push-always-takes-a-new-chunk", "src", RN, "    GSHCPU_Bits_Chunk * next = bits->top ? bits->top->next : bits->first;", "    GSHCPU_Bits_Chunk * next = NULL;", ['testRender_group']),
    ("rj-stack-top-reads-the-wrong-entry", "src", RN, "  return bits->top->v[(bits->len - 1u) % GSHCPU_BITS_CHUNK];", "  return bits->top->v[bits->len % GSHCPU_BITS_CHUNK];", ['testRender_group']),
    ("rj-stack-failure-not-reported", "src", RN, "      if (!next) {\n        bits->failed = 1;\n        return 0;\n      }", "      if (!next) {\n        bits->failed = 1;\n        return 1;\n      }", ['testRender_group', 'testRender_conformance']),
    ("rj-stack-chunks-leak", "src", RN, "  if (bits->memo->alloc) {\n    GSHCPU_Bits_Chunk * c = bits->first;", "  if (0) {\n    GSHCPU_Bits_Chunk * c = bits->first;", ['testRender_group']),
    ("rj-element-shape-merged-into-opacity", "src", RC, "    const double keep = 1.0 - shape;\n    for (int i = 0; i < 4; i++) {\n      l[i] = gshcpu_render_saturate(keep * (l[i] - b[i]) + t[i]);", "    const double keep = 1.0 - shape * s[3];\n    for (int i = 0; i < 4; i++) {\n      l[i] = gshcpu_render_saturate(keep * (l[i] - b[i]) + t[i]);", ['testRender_group', 'probe']),
    ("rj-element-opacity-merged-into-shape", "src", RC, "    memcpy(t, b, sizeof t);\n    if (!apply_core(t, s, m, op)) {", "    memcpy(t, b, sizeof t);\n    if (!apply_core(t, s, shape, op)) {", ['testRender_group', 'probe']),
    ("rj-element-keeps-the-layer-not-the-excess", "src", RC, "      l[i] = gshcpu_render_saturate(keep * (l[i] - b[i]) + t[i]);", "      l[i] = gshcpu_render_saturate(keep * l[i] + t[i]);", ['testRender_group', 'probe']),
    ("rj-element-backdrop-aliased-after-the-first-channel", "src", RC, "  double b[4];\n  memcpy(b, bk, sizeof b); /* the backdrop may be the layer itself */", "  double * b = (double *)bk;", ['testRender_group', 'probe']),
    ("rj-element-other-operators-weigh-by-the-shape", "src", RC, "  const double keep = 1.0 - m;\n  for (int i = 0; i < 4; i++) {\n    double o;", "  const double keep = 1.0 - m;\n  m = shape;\n  for (int i = 0; i < 4; i++) {\n    double o;", ['testRender_group']),
    ("rj-element-plus-unclamped", "src", RC, "      const double sum = s[i] + b[i];\n      o = sum < 1.0 ? sum : 1.0;\n    } else {\n      o = fa * s[i] + fb * b[i];", "      const double sum = s[i] + b[i];\n      o = sum;\n    } else {\n      o = fa * s[i] + fb * b[i];", ['testRender_group', 'probe']),
    ("rj-element-blend-modes-are-not-pdf-operators", "src", RC, "  return op == GSHSCENE_COMPOSITE_SRC_OVER || is_blend(op);", "  return op == GSHSCENE_COMPOSITE_SRC_OVER;", ['testRender_group', 'probe']),
    ("rj-element-source-over-is-not-pdf-operator", "src", RC, "  return op == GSHSCENE_COMPOSITE_SRC_OVER || is_blend(op);", "  return is_blend(op);", ['testRender_group', 'probe']),
    ("rj-element-pd-factors-shared-plus-flag", "src", RC, "    case GSHSCENE_COMPOSITE_PLUS:\n      return 2;", "    case GSHSCENE_COMPOSITE_PLUS:\n      return 1;", ['testRender_group', 'probe']),
    ("rj-step-alpha-part-forgotten", "src", RGF, "  if (ag) {\n    /* The group alpha steps", "  if (0) {\n    /* The group alpha steps", ['testRender_group', 'probe']),
    ("rj-step-alpha-backdrop-is-the-layers", "src", RGF, "    const double agb = knockout ? 0.0 : *ag;", "    const double agb = bk[3];", ['testRender_group', 'probe']),
    ("rj-step-alpha-backdrop-is-never-nothing", "src", RGF, "    const double agb = knockout ? 0.0 : *ag;", "    const double agb = *ag;", ['testRender_group', 'probe']),
    ("rj-step-skips-a-knockout-element-of-no-source", "src", RGF, "s[3] == 0.0 && (!knockout || shape == 0.0)) {", "s[3] == 0.0) {", ['testRender_group', 'probe']),
    ("rj-step-skips-a-knockout-element-of-no-source-and-some-shape", "src", RGF, "s[3] == 0.0 && (!knockout || shape == 0.0)) {", "s[3] == 0.0 && (!knockout || shape != 0.0)) {", ['testRender_group', 'probe']),
    ("rj-step-never-skips-an-element-of-no-source", "src", RGF, "  if (gshcpu_render_bounded(op) && s[0] == 0.0", "  if (0 && gshcpu_render_bounded(op) && s[0] == 0.0", ['testRender_group']),
    ("rj-step-skips-on-either-weight-zero", "src", RGF, "  if (m == 0.0 && shape == 0.0) {", "  if (m == 0.0 || shape == 0.0) {", ['testRender_group', 'probe']),
    ("rj-step-group-shape-adds", "src", RGF, "    *fg = (*fg + shape) - *fg * shape; /* the group shape, after each element: fg + f - fg f */", "    *fg = *fg + shape;", ['testRender_group', 'probe']),
    ("rj-step-group-shape-steps-by-the-weight", "src", RGF, "    *fg = (*fg + shape) - *fg * shape;", "    *fg = (*fg + m) - *fg * m;", ['testRender_group', 'probe']),
    ("rj-step-group-shape-skipped-with-the-source", "src", RGF, "  if (fg) {\n    *fg = (*fg + shape)", "  if (fg && (s[3] != 0.0)) {\n    *fg = (*fg + shape)", ['testRender_group', 'probe']),
    ("rj-step-reports-a-skip-as-composited", "src", RGF, "    return 0;\n  }\n  if (!gshcpu_render_element(", "    return 1;\n  }\n  if (!gshcpu_render_element(", ['testRender_group']),
    ("rj-pop-shape-is-the-weight-for-pdf-operators", "src", RS, "pdf ? weight * fgc : weight, op);", "(pdf ? weight : weight), op);", ['testRender_group', 'probe']),
    ("rj-pop-shape-is-the-group-shape-for-every-operator", "src", RS, "pdf ? weight * fgc : weight, op);", "(pdf ? weight * fgc : weight * fgc), op);", ['testRender_group', 'probe']),
    ("rj-pop-shape-ignores-the-clips", "src", RS, "pdf ? weight * fgc : weight, op);", "pdf ? fgc : weight, op);", ['testRender_group', 'probe']),
    ("rj-pop-nothing-drawn-has-a-whole-shape", "src", RS, "      double fgc = gs->frames[top].fg ? 0.0 : 1.0; /* nothing drawn here: no shape, where it is carried */", "      double fgc = 1.0;", ['testRender_group', 'probe']),
    ("rj-pop-group-shape-read-from-the-parent", "src", RS, "  *fg = gf->fg ? gf->fg[p] : 1.0;", "  *fg = gs_of(run)->frames[top - 1u].fg ? gs_of(run)->frames[top - 1u].fg[p] : 1.0;", ['testRender_group', 'probe']),
    ("rj-pop-opacity-not-applied", "src", RS, "        const double ov = o * v; /* MC4: the opacity source is o v (v = 1 without a mask: o) */", "        const double ov = v;", ['testRender_group', 'probe']),
    ("rj-pop-mask-ignored", "src", RS, "        const double ov = o * v; /* MC4: the opacity source is o v (v = 1 without a mask: o) */", "        const double ov = o;", ['testRender_group', 'testRender_mask', 'probe']),
    ("rj-pop-mask-acts-on-the-wrong-input", "src", RS, "      if (masked && mp.me.shape) {\n        weight = weight * v; /* MC4 (MK7)", "      if (masked && !mp.me.shape) {\n        weight = weight * v; /* MC4 (MK7)", ['testRender_group', 'testRender_mask', 'probe']),
    ("rj-pop-mask-value-unused", "src", RS, "      const double v = masked ? mask_pop_value(run, &mp, x, y, p) : 1.0;", "      const double v = 1.0;", ['testRender_group', 'testRender_mask', 'probe']),
    ("rj-pop-general-parent-composited-plain", "src", RS, "      if (pg->on || pg->fg) {\n        double bk[4];", "      if (0) {\n        double bk[4];", ['testRender_group', 'probe']),
    ("rj-pop-bounded-skip-taken-for-a-general-parent", "src", RS, "        const int stepped = gshcpu_render_group_step(", "        if (bounded && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0) {\n          continue;\n        }\n        const int stepped = gshcpu_render_group_step(", ['testRender_group', 'probe']),
    ("rj-pop-root-not-touched", "src", RS, "        if (stepped > 0 && run->top == 1u) {\n          run->touched[p] = 1;\n        }", "        if (0) {\n          run->touched[p] = 1;\n        }", ['testRender_group']),
    ("rj-pop-general-pops-uncounted", "src", RS, "  gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, general_pops));", "", ['testRender_group']),
    ("rj-pop-dispatch-misses-a-general-parent", "src", RS, "    if (g->on || g->fg || pg->on || pg->fg) {", "    if (g->on || g->fg || pg->fg) {", ['testRender_group', 'probe']),
    ("rj-pop-dispatch-misses-a-general-child", "src", RS, "    if (g->on || g->fg || pg->on || pg->fg) {", "    if (g->fg || pg->on || pg->fg) {", ['testRender_group', 'probe']),
    ("rj-pop-dispatch-always-general", "src", RS, "    if (g->on || g->fg || pg->on || pg->fg) {", "    if (1 || g->on || g->fg || pg->on || pg->fg) {", ['testRender_group']),
    ("rj-pop-region-of-an-unbounded-operator-is-the-dirty-rectangle", "src", RS, "  GSHCPU_Rect region = bounded ? f->dirty : window_of(run);\n  region = gshcpu_render_rect_meet(region, reach_of(run, parent->clip_base, f->clip_base));\n  GSHCPU_Rect whole = {0, 0, 0, 0};", "  GSHCPU_Rect region = f->dirty;\n  region = gshcpu_render_rect_meet(region, reach_of(run, parent->clip_base, f->clip_base));\n  GSHCPU_Rect whole = {0, 0, 0, 0};", ['testRender_group']),
    ("rj-caps-without-non-isolated", "src", RS, "  out->flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT;", "  out->flags |= GSHSCENE_CAP_GROUP_KNOCKOUT;", ['testRender_group']),
    ("rj-caps-without-knockout", "src", RS, "  out->flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT;", "  out->flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED;", ['testRender_group']),
    ("rj-stroke-sparse-fill-not-counted", "src", RS, "      count_general_fill(run, frame);\n      Sparse sp = {run, frame, src, s.shift_x, s.shift_y};", "      Sparse sp = {run, frame, src, s.shift_x, s.shift_y};", ['testRender_group']),
    ("rj-stroke-sparse-ignores-the-general-layer", "src", RS, "    if (frame->general) {\n      general_fill(run, frame, p, sp->src, gshcpu_k_level[m]);\n    } else {", "    if (0) {\n      general_fill(run, frame, p, sp->src, gshcpu_k_level[m]);\n    } else {", ['testRender_group', 'probe']),
    ("rj-painter-ignores-the-general-layer", "src", RS, "  if (frame->general) {\n    general_fill(run, frame, p, src, gshcpu_k_level[m8]);\n  } else {", "  if (0) {\n    general_fill(run, frame, p, src, gshcpu_k_level[m8]);\n  } else {", ['testRender_group', 'probe']),
    ("rj-fill-weight-is-not-the-shape", "src", RS, "  (void)gshcpu_render_group_step(&frame->layer[4u * p], b, g->ag ? g->ag + p : NULL, g->fg ? g->fg + p : NULL, g->knockout, src, f, f,", "  (void)gshcpu_render_group_step(&frame->layer[4u * p], b, g->ag ? g->ag + p : NULL, g->fg ? g->fg + p : NULL, g->knockout, src, f, 1.0,", ['testRender_group', 'probe']),
    ("rj-mask-frame-keeps-the-knockout-ancestors", "src", RS, "    Gen_Frame * g = &gs_of(run)->frames[run->top];\n    memset(g, 0, sizeof *g);\n    g->on = (run->cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;", "    Gen_Frame * g = &gs_of(run)->frames[run->top];\n    *g = gs_of(run)->frames[run->top - 1u];\n    g->on = (run->cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;", ['testRender_group', 'probe']),
    ("rj-direct-bytes-tier-taken-with-general-layers", "src", RS, "L.clips == 0 &&\n      !L.aligned && !L.other && !G.on &&", "L.clips == 0 &&\n      !L.aligned && !L.other &&", ['testRender_group']),
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


def link_test(test, objs, out, test_obj=None):
    # The test-only C support objects (tests/support/*.c: the earlier exact step the dense tests compare against) are
    # built by the Makefile beside the test objects and link into every test.
    support = " ".join(sorted(str(o) for o in (OBJ_DIR / "tests" / "support").glob("*.o")))
    cmd = "%s -o %s %s %s %s %s" % (CXX, out, test_obj or test_object(test), support, " ".join(objs), LDFLAGS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot link %s:\n%s" % (test, r.stderr))


def _limits():
    """A planted defect may loop or allocate without end; bound both."""
    import resource
    limit = int(float(os.environ.get("PLANT_AS_GB", "3")) * (1 << 30))
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
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
    if kind in ("src", "srcl", "srch", "srct"):
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
            test_obj = None
            if kind == "srct":
                # The planted header is inline code the tests use themselves: the test is built from a copy of the tree beside
                # the planted sources, so that its relative include finds the planted header.
                (d / "planted" / "tests").mkdir(parents=True, exist_ok=True)
                shutil.copytree(ROOT / "tests", d / "planted" / "tests", ignore=shutil.ignore_patterns("corpus", "fuzz"),
                                dirs_exist_ok=True)
                source = d / "planted" / "tests" / "unit" / test_object(test).name.replace(".o", ".cpp")
                test_obj = str(d / (test + ".o"))
                r = run("%s %s -I%s -c %s -o %s" % (CXX, CXX_INCLUDE, d / "planted" / "tests", source, test_obj), shell=True)
                if r.returncode != 0:
                    raise RuntimeError("plant %s: cannot build %s:\n%s" % (pid, test, r.stderr))
            link_test(test, objs, exe, test_obj)
            rc, failing = run_test(exe)
        if rc != 0:
            caught_by.append((test + ("" if test in tests else " [not in its list]"), failing[:1]))
            break  # one test seeing it is enough; the lists are ordered by cost
    return pid, caught_by



# --- masks-images-patterns story R2: patterns in the sink (render.h, PC1 to PC11) ---------------------------------------------------
# One hand plant for each rule and branch of the pattern code, by rule: the claim (PC1), the fill and the paint (PC2, PC7), the copy and its
# canonical translation (PC3), a unit with copies, its clips and the lattice order (PC4), strokes cut to the cell (PC5), overlapping cells (PC6),
# the limits and the chain (PC8), the period tier: its detection, its window, the limits it cannot vouch for and the give-up (PC9), the cache: its
# key, its table and its raster (PC10), the facts a tile is asked (PC9, PC10) and what a pass keeps and restores (PC11).  `make check-planted
# PLANT_ONLY='^rp-'` runs them.  Written with the story; the survivors are in SURVIVORS with their cause.
RP_TESTS = ["testRender_pattern", "testRender_pattern_period", "testRender_pattern_rules", "testRender_pattern_keys", "testRender_pattern_vectors", "testRender_limits", "testRender_conformance", "probe"]
PLANTS += [
    ('rp-claim-dropped', "src", RS, '  out->flags |= GSHSCENE_CAP_PATTERN;', '', RP_TESTS),
    ('rp-pixel-alpha-multiplier-dropped', "src", RS, '      s[i] = v[i] * p->alpha;', '      s[i] = v[i];', RP_TESTS),
    ('rp-pixel-tint-keeps-tile-colors', "src", RS, "    const double a = v[3] * p->alpha; /* only the layer's alpha is read: the tile's colors are discarded */\n    for (int i = 0; i < 4; i++) {\n      s[i] = p->tint[i] * a;\n    }", '    for (int i = 0; i < 4; i++) {\n      s[i] = v[i] * p->alpha * (p->tint[3] + 1.0) * 0.5;\n    }', RP_TESTS),
    ('rp-pixel-tint-alpha-without-multiplier', "src", RS, "    const double a = v[3] * p->alpha; /* only the layer's alpha is read: the tile's colors are discarded */", '    const double a = v[3];', RP_TESTS),
    ('rp-pixel-outside-dirty-read', "src", RS, '  if (inside(f->dirty, x, y)) {\n    memcpy(v, &f->layer[4u * ((size_t)(y - p->loy) * p->run->layout.width + (size_t)(x - p->lox))], sizeof v);\n  }', '  memcpy(v, &f->layer[4u * ((size_t)(y - p->loy) * p->run->layout.width + (size_t)(x - p->lox))], sizeof v);', RP_TESTS),
    ('rp-pixel-period-x-origin-lost', "src", RS, '    gshgeom_lattice_reduce(&p->per, x - p->wx, y - p->wy, r); /* PC9, LT3: the window pixel congruent to (x, y) */', '    gshgeom_lattice_reduce(&p->per, x, y - p->wy, r); /* PC9, LT3: the window pixel congruent to (x, y) */', RP_TESTS),
    ('rp-pixel-period-y-origin-lost', "src", RS, '    gshgeom_lattice_reduce(&p->per, x - p->wx, y - p->wy, r); /* PC9, LT3: the window pixel congruent to (x, y) */', '    gshgeom_lattice_reduce(&p->per, x - p->wx, y, r); /* PC9, LT3: the window pixel congruent to (x, y) */', RP_TESTS),
    ('rp-pixel-period-x-window-corner-lost', "src", RS, '    return pattern_read(p, p->wx + r[0], p->wy + r[1], s);', '    return pattern_read(p, r[0], p->wy + r[1], s);', RP_TESTS),
    ('rp-pixel-period-y-window-corner-lost', "src", RS, '    return pattern_read(p, p->wx + r[0], p->wy + r[1], s);', '    return pattern_read(p, p->wx + r[0], r[1], s);', RP_TESTS),
    ('rp-pixel-period-axes-swapped', "src", RS, '    return pattern_read(p, p->wx + r[0], p->wy + r[1], s);', '    return pattern_read(p, p->wx + r[1], p->wy + r[0], s);', RP_TESTS),
    ('rp-span-period-x-origin-lost', "src", RS, '      gshgeom_lattice_reduce(&p->per, x - p->wx, y - p->wy, r);\n      wxr = r[0];', '      gshgeom_lattice_reduce(&p->per, x, y - p->wy, r);\n      wxr = r[0];', RP_TESTS),
    ('rp-span-period-y-origin-lost', "src", RS, '      gshgeom_lattice_reduce(&p->per, x - p->wx, y - p->wy, r);\n      wxr = r[0];', '      gshgeom_lattice_reduce(&p->per, x - p->wx, y, r);\n      wxr = r[0];', RP_TESTS),
    ('rp-span-period-window-row-lost', "src", RS, '      wyr = p->wy + r[1];', '      wyr = r[1];', RP_TESTS),
    ('rp-span-period-no-wrap', "src", RS, '        wxr = wxr + 1 == p->per.p ? 0 : wxr + 1;', '        wxr = wxr + 1;', RP_TESTS),
    ('rp-span-period-wraps-late', "src", RS, '        wxr = wxr + 1 == p->per.p ? 0 : wxr + 1;', '        wxr = wxr == p->per.p ? 0 : wxr + 1;', RP_TESTS),
    ('rp-span-period-column-not-advanced-over-skipped', "src", RS, '      const int64_t col = wxr; /* this pixel\'s reduced column, then the next one\'s */\n      if (p->period) {\n        wxr = wxr + 1 == p->per.p ? 0 : wxr + 1;\n      }\n      if (m8[i] == 0) {\n        continue;\n      }', '      const int64_t col = wxr;\n      if (m8[i] == 0) {\n        continue;\n      }\n      if (p->period) {\n        wxr = wxr + 1 == p->per.p ? 0 : wxr + 1;\n      }', RP_TESTS),
    ('rp-copy-quad-at-raw-translation', "src", RS, 'gshgeom_round_26_6(q[2 * k] + c->canon.t[0], &px) != GSHGEOM_OK || gshgeom_round_26_6(q[2 * k + 1] + c->canon.t[1], &py)', 'gshgeom_round_26_6(q[2 * k], &px) != GSHGEOM_OK || gshgeom_round_26_6(q[2 * k + 1] + c->canon.t[1], &py)', RP_TESTS),
    ('rp-copy-quad-y-at-raw-translation', "src", RS, 'gshgeom_round_26_6(q[2 * k + 1] + c->canon.t[1], &py)', 'gshgeom_round_26_6(q[2 * k + 1], &py)', RP_TESTS),
    ('rp-copy-quad-x-of-y', "src", RS, '    c->quad[k].x = px;\n    c->quad[k].y = py;', '    c->quad[k].x = py;\n    c->quad[k].y = py;', RP_TESTS),
    ('rp-copy-qbox-x0-ceil', "src", RS, '  c->qbox.x0 = floor_div64(lo_x);', '  c->qbox.x0 = floor_div64(lo_x) + 1;', RP_TESTS),
    ('rp-copy-qbox-y1-floor', "src", RS, '  c->qbox.y1 = floor_div64(hi_y) + 1;', '  c->qbox.y1 = floor_div64(hi_y);', RP_TESTS),
    ('rp-copy-qbox-x1-floor', "src", RS, '  c->qbox.x1 = floor_div64(hi_x) + 1;', '  c->qbox.x1 = floor_div64(hi_x);', RP_TESTS),
    ('rp-copy-base-is-the-chain', "src", RS, '  L->cur_qbox = c->qbox;\n  L->base = c->canon;', '  L->cur_qbox = c->qbox;\n  L->base = L->C;', RP_TESTS),
    ('rp-copy-canonical-error-lost', "src", RS, '  if (gshgeom_lattice_canonical(&L->C, &L->lat, i, j, &c->canon, &c->sx, &c->sy) != GSHGEOM_OK) {\n    return GSHSCENE_ERR_LIMIT; /* a shifted coordinate past the range the sink draws */\n  }', '  (void)gshgeom_lattice_canonical(&L->C, &L->lat, i, j, &c->canon, &c->sx, &c->sy);', RP_TESTS),
    ('rp-copy-quad-error-lost', "src", RS, '  if (gshgeom_lattice_quad(&L->C, &L->lat, q) != GSHGEOM_OK) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  /* LT5', '  (void)gshgeom_lattice_quad(&L->C, &L->lat, q);\n  /* LT5', RP_TESTS),
    ('rp-copy-moved-back-region-x', "src", RS, '  *rc = gshcpu_render_rect_meet(rect_moved(reach, -c->sx, -c->sy), c->qbox);', '  *rc = gshcpu_render_rect_meet(rect_moved(reach, c->sx, -c->sy), c->qbox);', RP_TESTS),
    ('rp-copy-moved-back-region-y', "src", RS, '  *rc = gshcpu_render_rect_meet(rect_moved(reach, -c->sx, -c->sy), c->qbox);', '  *rc = gshcpu_render_rect_meet(rect_moved(reach, -c->sx, c->sy), c->qbox);', RP_TESTS),
    ('rp-copy-whole-moved-back-wrong', "src", RS, '  *wc = gshcpu_render_rect_meet(rect_moved(wreach, -c->sx, -c->sy), c->qbox);', '  *wc = gshcpu_render_rect_meet(rect_moved(wreach, c->sx, c->sy), c->qbox);', RP_TESTS),
    ('rp-copy-range-unchecked', "src", RS, '  if (!rect_in_range(*wc)) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  return pat_cover(', '  return pat_cover(', RP_TESTS),
    ('rp-copy-applied-at-wrong-shift', "src", RS, '      const int64_t dx = x + c->sx;\n      const int64_t dy = y + c->sy;', '      const int64_t dx = x - c->sx;\n      const int64_t dy = y + c->sy;', RP_TESTS),
    ('rp-copy-urect-shift-lost', "src", RS, '  L->urect = gshcpu_render_rect_join(L->urect, rect_moved(rc, c->sx, c->sy));', '  L->urect = gshcpu_render_rect_join(L->urect, rc);', RP_TESTS),
    ('rp-paint-position-shift-lost', "src", RS, '  pr(run)->pshx = c->sx;\n  pr(run)->pshy = c->sy;\n  GSHSCENE_Result r = painter_begin(', '  pr(run)->pshx = 0;\n  pr(run)->pshy = c->sy;\n  GSHSCENE_Result r = painter_begin(', RP_TESTS),
    ('rp-unit-first-copy-weight', "src", RS, '    L->sum8[p] = m;\n    L->cnt[p] = 1;\n    return;', '    L->sum8[p] = 255u;\n    L->cnt[p] = 1;\n    return;', RP_TESTS),
    ('rp-unit-second-copy-keeps-first-source', "src", RS, '      a[i] = m0 * a[i] + (double)m * s[i];', '      a[i] = m0 * a[i] + (double)m * a[i];', RP_TESTS),
    ('rp-unit-third-copy-unweighted', "src", RS, '      a[i] += (double)m * s[i];', '      a[i] += s[i];', RP_TESTS),
    ('rp-unit-sum-not-kept', "src", RS, '  L->sum8[p] += m;\n}', '}', RP_TESTS),
    ('rp-unit-count-not-kept', "src", RS, '    if (c < 255) {\n      L->cnt[p] = (uint8_t)(c + 1);\n    }', '', RP_TESTS),
    ('rp-unit-single-copy-through-mean', "src", RS, '      if (c == 1) {\n        painter_pixel(run, frame, x, y, pat_level8(L->sum8[p]), &L->acc[4u * p]);\n        continue;\n      }', '', RP_TESTS),
    ('rp-unit-shape-not-saturated', "src", RS, '      painter_pixel(run, frame, x, y, pat_level8(total), s);', '      painter_pixel(run, frame, x, y, total >> GSHCPU_FOLD_AREA_SHIFT, s);', RP_TESTS),
    ('rp-unit-shape-always-whole', "src", RS, '      painter_pixel(run, frame, x, y, pat_level8(total), s);', '      painter_pixel(run, frame, x, y, 255u, s);', RP_TESTS),
    ('rp-unit-mean-not-renormalized', "src", RS, '        s[i] = L->acc[4u * p + (size_t)i] / (double)total;', '        s[i] = L->acc[4u * p + (size_t)i] / 255.0;', RP_TESTS),
    ('rp-unit-counts-not-cleared', "src", RS, '      L->cnt[p] = 0;\n          if (c == 1) {', '          if (c == 1) {', RP_TESTS),
    ('rp-unit-empty-region-applies', "src", RS, '  const GSHCPU_Rect u = L->urect;\n  if (gshcpu_render_rect_empty(u)) {\n    return;\n  }\n  widen_w(run, frame, u, u);', '  const GSHCPU_Rect u = L->urect;\n  widen_w(run, frame, u, u);', RP_TESTS),
    ('rp-unit-work-not-counted', "src", RS, '  run->work += (uint64_t)(u.x1 - u.x0) * (uint64_t)(u.y1 - u.y0);\n  L->urect.x1 = L->urect.x0;', '  L->urect.x1 = L->urect.x0;', RP_TESTS),
    ('rp-unit-region-not-reset', "src", RS, "  L->urect.x1 = L->urect.x0;\n  L->urect.y1 = L->urect.y0;\n}\n\n/** The source of the pixel (@p x, @p y) of a painter's frame", "}\n\n/** The source of the pixel (@p x, @p y) of a painter's frame", RP_TESTS),
    ('rp-unit-zero-weight-added', "src", RS, '      if (m == 0) {\n        continue;\n      }\n      const int64_t dx', '      const int64_t dx', RP_TESTS),
    ('rp-unit-source-refused-added', "src", RS, '      if (painter_source(&p, x, y, s)) {\n              pat_add(L, at(run, dx, dy), m, s);\n      }', '      painter_source(&p, x, y, s);\n      pat_add(L, at(run, dx, dy), m, s);', RP_TESTS),
    ('rp-unit-clips-not-folded', "src", RS, '  if (!gshcpu_render_rect_empty(rc) && u->frame->clip_base < run->clip_count) {\n    const GSHSCENE_Result f = pat_fold_clips(run, L, u->frame, c, rc, cov, rem);', '  if (0 && !gshcpu_render_rect_empty(rc) && u->frame->clip_base < run->clip_count) {\n    const GSHSCENE_Result f = pat_fold_clips(run, L, u->frame, c, rc, cov, rem);', RP_TESTS),
    ('rp-clip-fold-empty-clip-keeps-coverage', "src", RS, '    if (!have) {\n      memset(cov, 0, n); /* the clip holds nothing of this copy here */\n      if (rem) {\n        memset(rem, 0, n);\n      }\n      return GSHSCENE_OK;\n    }', '    if (!have) {\n      continue;\n    }', RP_TESTS),
    ('rp-clip-fold-outside-rect-kept', "src", RS, '          const uint32_t m = (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) ? L->tmp[(size_t)(y - r.y0) * rw + (size_t)(x - r.x0)] : 0u;', '          const uint32_t m = (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) ? L->tmp[(size_t)(y - r.y0) * rw + (size_t)(x - r.x0)] : 255u;', RP_TESTS),
    ('rp-clip-fold-not-weighed', "src", RS, '          pat_weigh(&cov[at_px], rem ? &rem[at_px] : NULL, m);', '          cov[at_px] = (uint8_t)m;', RP_TESTS),
    ('rp-clip-fold-whole-rect-not-weighed', "src", RS, '        pat_weigh(&cov[i], rem ? &rem[i] : NULL, L->tmp[i]);', '        cov[i] = L->tmp[i];', RP_TESTS),
    ('rp-clip-fold-tmp-not-cleared', "src", RS, '    memset(L->tmp, 0, n);\n    GSHSCENE_Result res = pat_cover(', '    GSHSCENE_Result res = pat_cover(', RP_TESTS),
    ('rp-clip-union-unsaturated', "src", RS, '          clip->coverage[(size_t)(y - clip->rect.y0) * cw + (size_t)(x - clip->rect.x0)] = (uint8_t)pat_level8(L->sum8[p]);', '          clip->coverage[(size_t)(y - clip->rect.y0) * cw + (size_t)(x - clip->rect.x0)] = (uint8_t)(L->sum8[p] >> GSHCPU_FOLD_AREA_SHIFT);', RP_TESTS),
    ('rp-clip-union-last-copy-only', "src", RS, '      } else {\n        L->sum8[p] += m;\n      }', '      } else {\n        L->sum8[p] = m;\n      }', RP_TESTS),
    ('rp-clip-copy-shift-lost', "src", RS, '      const size_t p = at(run, x + c.sx, y + c.sy);', '      const size_t p = at(run, x, y);', RP_TESTS),
    ('rp-clip-memory-not-cleared', "src", RS, '    memset(clip->coverage, 0, (size_t)(clip->rect.x1 - clip->rect.x0) * (size_t)(clip->rect.y1 - clip->rect.y0));\n    GSHSCENE_Result r = pat_each(run, L, clip->rect, pat_clip_copy, &u);', '    GSHSCENE_Result r = pat_each(run, L, clip->rect, pat_clip_copy, &u);', RP_TESTS),
    ('rp-clip-not-kept-for-the-copies', "src", RS, '    memcpy(pc->maps, run->maps, count * sizeof *pc->maps);', '    memset(pc->maps, 0, count * sizeof *pc->maps);', RP_TESTS),
    ('rp-clip-maps-short', "src", RS, '    memcpy(pc->maps, run->maps, count * sizeof *pc->maps);', '    memcpy(pc->maps, run->maps, (count - 1u) * sizeof *pc->maps);', RP_TESTS),
    ('rp-order-reversed', "src", RS, '  for (int64_t i = lo;; i++) {\n    if (gshscene_job_checkpoint(e->run->job)) {\n      e->r = GSHSCENE_ERR_CANCELLED; /* PC11: before every copy */\n      return 1;\n    }\n    e->r = e->fn(e->run, e->L, i, j, e->ctx);\n    if (e->r != GSHSCENE_OK) {\n      return 1;\n    }\n    if (i == hi) {\n      return 0;\n    }\n  }', '  for (int64_t i = hi;; i--) {\n    if (gshscene_job_checkpoint(e->run->job)) {\n      e->r = GSHSCENE_ERR_CANCELLED; /* PC11: before every copy */\n      return 1;\n    }\n    e->r = e->fn(e->run, e->L, i, j, e->ctx);\n    if (e->r != GSHSCENE_OK) {\n      return 1;\n    }\n    if (i == lo) {\n      return 0;\n    }\n  }', RP_TESTS),
    ('rp-order-cancel-not-checked', "src", RS, '    if (gshscene_job_checkpoint(e->run->job)) {\n      e->r = GSHSCENE_ERR_CANCELLED; /* PC11: before every copy */\n      return 1;\n    }\n    e->r = e->fn(', '    e->r = e->fn(', RP_TESTS),
    ('rp-order-copy-error-lost', "src", RS, '    e->r = e->fn(e->run, e->L, i, j, e->ctx);\n    if (e->r != GSHSCENE_OK) {\n      return 1;\n    }', '    e->r = e->fn(e->run, e->L, i, j, e->ctx);\n    e->r = GSHSCENE_OK;', RP_TESTS),
    ('rp-each-error-lost', "src", RS, '  if (e.r != GSHSCENE_OK) {\n    return e.r;\n  }\n  return g == GSHGEOM_OK ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;', '  return g == GSHGEOM_OK ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;', RP_TESTS),
    ('rp-each-single-copy-ignored', "src", RS, '  if (L->single) {\n    return fn(run, L, L->ci, L->cj, ctx);\n  }', '', RP_TESTS),
    ('rp-stroke-not-cut', "src", RS, '  const GSHCPU_Result cut = center ? gshcpu_render_stroke_cell(&s, c.quad, run->item_alloc, &cs->geometry, &cell)\n                                   : gshcpu_render_stroke_cut(&s, c.quad, run->item_alloc, &cs->geometry);\n  if (cut != GSHCPU_OK) {', '  const GSHCPU_Result cut = center ? gshcpu_render_stroke_cell(&s, c.quad, run->item_alloc, &cs->geometry, &cell)\n                                   : GSHCPU_OK;\n  if (cut != GSHCPU_OK) {', RP_TESTS),
    ('rp-stroke-cut-error-lost', "src", RS, '    return cut == GSHCPU_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : scene_result(cut);\n  }\n  if (!s.drawn) {', '    return GSHSCENE_OK;\n  }\n  if (!s.drawn) {', RP_TESTS),
    ('rp-stroke-cut-nothing-drawn-anyway', "src", RS, '  if (!s.drawn) {\n    gshgeom_flat_path_destroy(cell);\n    gshcpu_render_stroke_close(cache, &s);\n    return GSHSCENE_OK;\n  }\n  if (L->guard) {', '  if (L->guard) {', RP_TESTS),
    ('rp-stroke-cut-body-skipped', "src", RSK, '  for (int p = GSHCPU_STROKE_BODY; p <= GSHCPU_STROKE_ALIGNED; p++) {\n    if (!s->meta.present[p]) {\n      continue;\n    }\n    GSHGEOM_Flat_Path * cut = NULL;', '  for (int p = GSHCPU_STROKE_ALIGNED; p <= GSHCPU_STROKE_ALIGNED; p++) {\n    if (!s->meta.present[p]) {\n      continue;\n    }\n    GSHGEOM_Flat_Path * cut = NULL;', RP_TESTS),
    ('rp-stroke-cut-aligned-skipped', "src", RSK, '  for (int p = GSHCPU_STROKE_BODY; p <= GSHCPU_STROKE_ALIGNED; p++) {\n    if (!s->meta.present[p]) {\n      continue;\n    }\n    GSHGEOM_Flat_Path * cut = NULL;', '  for (int p = GSHCPU_STROKE_BODY; p < GSHCPU_STROKE_ALIGNED; p++) {\n    if (!s->meta.present[p]) {\n      continue;\n    }\n    GSHGEOM_Flat_Path * cut = NULL;', RP_TESTS),
    ('rp-stroke-cut-of-the-uncut-part', "src", RSK, 'clip_convex(cover_of(s->outline, p), frame, alloc, limits, &cut);', 'clip_convex(part_of(s->outline, p), frame, alloc, limits, &cut);', RP_TESTS),
    ('rp-stroke-cut-error-swallowed', "src", RSK, '    if (g != GSHGEOM_OK) {\n      return g == GSHGEOM_ERR_OOM ? GSHCPU_ERR_OOM : g == GSHGEOM_ERR_LIMIT ? GSHCPU_ERR_LIMIT : GSHCPU_ERR_INTERNAL;\n    }\n    s->cut_set[p] = 1;', '    if (g != GSHGEOM_OK) {\n      return GSHCPU_OK;\n    }\n    s->cut_set[p] = 1;', RP_TESTS),
    ('rp-stroke-cut-empty-part-kept', "src", RSK, '      s->meta.present[p] = 0;\n      s->meta.points[p] = 0;\n      s->meta.bound[p] = 0;\n      s->box[p] = empty_rect();\n      continue;', '      s->cut[p] = cut;\n      continue;', RP_TESTS),
    ('rp-stroke-cut-empty-part-still-present', "src", RSK, '      s->meta.present[p] = 0;\n      s->meta.points[p] = 0;', '      s->meta.points[p] = 0;', RP_TESTS),
    ('rp-stroke-cut-meta-stale-lo-x', "src", RSK, '      s->meta.lo_x[p] = i == 0 || x < s->meta.lo_x[p] ? x : s->meta.lo_x[p];', '      s->meta.lo_x[p] = s->meta.lo_x[p];', RP_TESTS),
    ('rp-stroke-cut-meta-stale-hi-y', "src", RSK, '      s->meta.hi_y[p] = i == 0 || y > s->meta.hi_y[p] ? y : s->meta.hi_y[p];', '      s->meta.hi_y[p] = s->meta.hi_y[p];', RP_TESTS),
    ('rp-stroke-cut-points-stale', "src", RSK, '    s->meta.points[p] = n;\n    for (size_t i = 0; i < n; i++) {', '    for (size_t i = 0; i < n; i++) {', RP_TESTS),
    ('rp-stroke-cut-box-stale', "src", RSK, '    const GSHCPU_Result r = set_box(s, p);\n    if (r != GSHCPU_OK) {\n      return r;\n    }\n  }\n  if (!s->meta.present[GSHCPU_STROKE_BODY]', '  }\n  if (!s->meta.present[GSHCPU_STROKE_BODY]', RP_TESTS),
    ('rp-stroke-cut-nothing-left-drawn', "src", RSK, '    s->drawn = 0; /* nothing of it is inside the cell */', '', RP_TESTS),
    ('rp-stroke-cut-nothing-left-needs-both', "src", RSK, '  if (!s->meta.present[GSHCPU_STROKE_BODY] && !s->meta.present[GSHCPU_STROKE_ALIGNED]) {', '  if (!s->meta.present[GSHCPU_STROKE_BODY] || !s->meta.present[GSHCPU_STROKE_ALIGNED]) {', RP_TESTS),
    ('rp-stroke-cut-parts-not-freed', "src", RSK, '    if (s->cut[p]) {\n      gshgeom_flat_path_destroy(s->cut[p]);\n    }\n    s->cut[p] = NULL;', '    s->cut[p] = NULL;', RP_TESTS),
    ('rp-stroke-cut-part-ignored-by-the-cover', "src", RSK, '  return s->cut_set[p] ? s->cut[p] : part_of(s->outline, p);', '  return part_of(s->outline, p);', RP_TESTS),
    ('rp-stroke-cut-cover-ignored-by-the-cover', "src", RSK, '  return s->cut_set[p] ? s->cut[p] : cover_of(s->outline, p);', '  return cover_of(s->outline, p);', RP_TESTS),
    ('rp-stroke-box-x-shift-lost', "src", RSK, '  const int64_t x0 = s->meta.lo_x[p] + 64 * s->shift_x;', '  const int64_t x0 = s->meta.lo_x[p];', RP_TESTS),
    ('rp-stroke-box-limit-unchecked', "src", RSK, '  if (x0 < -GSHCPU_RENDER_COORD_LIMIT || y0 < -GSHCPU_RENDER_COORD_LIMIT || x1 > GSHCPU_RENDER_COORD_LIMIT ||\n      y1 > GSHCPU_RENDER_COORD_LIMIT) {\n    return GSHCPU_ERR_LIMIT;\n  }\n  s->box[p].x0', '  s->box[p].x0', RP_TESTS),
    ('rp-overlap-never', "src", RS, '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);', '  const int overlaps = 0;', RP_TESTS),
    ('rp-overlap-always', "src", RS, '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);', '  const int overlaps = 1;', RP_TESTS),
    ('rp-overlap-x-only', "src", RS, '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);', '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w);', RP_TESTS),
    ('rp-overlap-y-only', "src", RS, '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);', '  const int overlaps = (pt.repeat_y && pt.y_step < pt.h);', RP_TESTS),
    ('rp-overlap-step-equal-overlaps', "src", RS, '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);', '  const int overlaps = (pt.repeat_x && pt.x_step <= pt.w) || (pt.repeat_y && pt.y_step <= pt.h);', RP_TESTS),
    ('rp-overlap-unused-step-counts', "src", RS, '  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);', '  const int overlaps = (pt.x_step < pt.w) || (pt.y_step < pt.h);', RP_TESTS),
    ('rp-overlap-single-not-cleared', "src", RS, '  const GSHSCENE_Result r = pat_walk(run, L, ctx);\n  L->single = 0;\n  return r;', '  const GSHSCENE_Result r = pat_walk(run, L, ctx);\n  return r;', RP_TESTS),
    ('rp-tint-never', "src", RS, '  p->has_tint = pt.has_tint != 0;', '  p->has_tint = 0;', RP_TESTS),
    ('rp-tint-always', "src", RS, '  p->has_tint = pt.has_tint != 0;', '  p->has_tint = 1;', RP_TESTS),
    ('rp-tint-not-converted', "src", RS, '  if (p->has_tint) {\n    paint_source(run, &pt.tint, frame, p->tint);\n  }', '  if (p->has_tint) {\n    p->tint[0] = pt.tint.rgba[0] * pt.tint.rgba[3];\n    p->tint[1] = pt.tint.rgba[1] * pt.tint.rgba[3];\n    p->tint[2] = pt.tint.rgba[2] * pt.tint.rgba[3];\n    p->tint[3] = pt.tint.rgba[3];\n  }', RP_TESTS),
    ('rp-alpha-multiplier-from-the-pattern', "src", RS, '  p->alpha = item->paint.rgba[3];\n  if (p->has_tint) {', '  p->alpha = 1.0;\n  if (p->has_tint) {', RP_TESTS),
    ('rp-count-over-the-drawn-rect', "src", RS, '  const int64_t cr[4] = {whole.x0, whole.y0, whole.x1, whole.y1};', '  const int64_t cr[4] = {rect.x0, rect.y0, rect.x1, rect.y1};', RP_TESTS),
    ('rp-count-total-not-reset', "src", RS, '  if (pr(run)->pat_depth == 0) {\n    pr(run)->pat_total = 0;\n  }', '', RP_TESTS),
    ('rp-count-total-not-summed', "src", RS, '  pr(run)->pat_total += n;', '', RP_TESTS),
    ('rp-count-total-overwritten', "src", RS, '  pr(run)->pat_total += n;', '  pr(run)->pat_total = n;', RP_TESTS),
    ('rp-count-remaining-not-reduced', "src", RS, '  remaining = pr(run)->pat_total >= remaining ? 0 : remaining - pr(run)->pat_total;', '', RP_TESTS),
    ('rp-count-remaining-underflows', "src", RS, '  remaining = pr(run)->pat_total >= remaining ? 0 : remaining - pr(run)->pat_total;', '  remaining = remaining - pr(run)->pat_total;', RP_TESTS),
    ('rp-count-remaining-by-one', "src", RS, '  remaining = pr(run)->pat_total >= remaining ? 0 : remaining - pr(run)->pat_total;', '  remaining = pr(run)->pat_total >= remaining ? 0 : remaining - pr(run)->pat_total + 1;', RP_TESTS),
    ('rp-count-unlimited-capped', "src", RS, '  remaining = remaining == UINT64_MAX ? (uint64_t)GSHGEOM_LATTICE_INDEX_MAX : remaining;', '  remaining = remaining == UINT64_MAX ? 1u << 20 : remaining;', RP_TESTS),
    ('rp-count-limit-ceiling-lost', "src", RS, '  remaining = remaining > (uint64_t)GSHGEOM_LATTICE_INDEX_MAX ? (uint64_t)GSHGEOM_LATTICE_INDEX_MAX : remaining;', '', RP_TESTS),
    ('rp-count-refusal-lost', "src", RS, '  if (gshgeom_lattice_each(&L->C, &L->lat, cr, remaining, NULL, NULL, &n) != GSHGEOM_OK) {\n    p->refused = 1;\n    return refuse(run, item, chain, count);\n  }', '  gshgeom_lattice_each(&L->C, &L->lat, cr, remaining, NULL, NULL, &n);', RP_TESTS),
    ('rp-count-refusal-not-reported', "src", RS, '  if (gshgeom_lattice_each(&L->C, &L->lat, cr, remaining, NULL, NULL, &n) != GSHGEOM_OK) {\n    p->refused = 1;\n    return refuse(run, item, chain, count);\n  }', '  if (gshgeom_lattice_each(&L->C, &L->lat, cr, remaining, NULL, NULL, &n) != GSHGEOM_OK) {\n    p->refused = 1;\n    return GSHSCENE_OK;\n  }', RP_TESTS),
    ('rp-count-nothing-is-drawn-anyway', "src", RS, '  if (n == 0) {\n    p->refused = 1;\n    return GSHSCENE_OK;\n  }', '', RP_TESTS),
    ('rp-chain-singular-not-refused', "src", RS, '  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &L->C) != GSHGEOM_OK) {\n    p->refused = 1;\n    return refuse(run, item, chain, count);\n  }', '  gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &L->C);', RP_TESTS),
    ('rp-chain-pattern-map-not-composed', "src", RS, '  run->maps[count] = pt.map;\n  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &L->C)', '  run->maps[count] = item->map;\n  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &L->C)', RP_TESTS),
    ('rp-chain-item-map-not-composed', "src", RS, '  run->maps[count - 1] = item->map;\n  run->maps[count] = pt.map;', '  run->maps[count - 1] = pt.map;\n  run->maps[count] = pt.map;', RP_TESTS),
    ('rp-chain-target-map-not-used', "src", RS, '  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &L->C) != GSHGEOM_OK) {\n    p->refused = 1;\n    return refuse(run, item, chain, count);\n  }\n  /* PC8', '  if (gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &L->C) != GSHGEOM_OK) {\n    p->refused = 1;\n    return refuse(run, item, chain, count);\n  }\n  /* PC8', RP_TESTS),
    ('rp-lattice-cell-x-from-y', "src", RS, '  L->lat.x = pt.x;\n  L->lat.y = pt.y;', '  L->lat.x = pt.y;\n  L->lat.y = pt.y;', RP_TESTS),
    ('rp-lattice-size-swapped', "src", RS, '  L->lat.w = pt.w;\n  L->lat.h = pt.h;', '  L->lat.w = pt.h;\n  L->lat.h = pt.w;', RP_TESTS),
    ('rp-lattice-steps-swapped', "src", RS, '  L->lat.x_step = pt.x_step;\n  L->lat.y_step = pt.y_step;', '  L->lat.x_step = pt.y_step;\n  L->lat.y_step = pt.x_step;', RP_TESTS),
    ('rp-lattice-repeats-swapped', "src", RS, '  L->lat.repeat_x = pt.repeat_x;\n  L->lat.repeat_y = pt.repeat_y;', '  L->lat.repeat_x = pt.repeat_y;\n  L->lat.repeat_y = pt.repeat_x;', RP_TESTS),
    ('rp-lattice-repeat-x-always', "src", RS, '  L->lat.repeat_x = pt.repeat_x;', '  L->lat.repeat_x = 1;', RP_TESTS),
    ('rp-tier-switch-inverted', "src", RS, '  if (!(run->cs->tiers & GSHCPU_RENDER_TIER_PERIOD)) {\n    return 0;\n  }\n  int64_t a[2];', '  if (run->cs->tiers & GSHCPU_RENDER_TIER_PERIOD) {\n    return 0;\n  }\n  int64_t a[2];', RP_TESTS),
    ('rp-tier-integer-test-dropped', "src", RS, 'gshgeom_lattice_integer_steps(&L->C, &L->lat, a, b, &integer) != GSHGEOM_OK || !integer ||', 'gshgeom_lattice_integer_steps(&L->C, &L->lat, a, b, &integer) != GSHGEOM_OK ||', RP_TESTS),
    ('rp-tier-window-width-unchecked', "src", RS, '  if (per.p > rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {', '  if (per.s > rect.y1 - rect.y0) {', RP_TESTS),
    ('rp-tier-window-height-unchecked', "src", RS, '  if (per.p > rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {', '  if (per.p > rect.x1 - rect.x0) {', RP_TESTS),
    ('rp-tier-window-width-strict', "src", RS, '  if (per.p > rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {', '  if (per.p >= rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {', RP_TESTS),
    ('rp-tier-window-height-strict', "src", RS, '  if (per.p > rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {', '  if (per.p > rect.x1 - rect.x0 || per.s >= rect.y1 - rect.y0) {', RP_TESTS),
    ('rp-tier-window-width-by-height', "src", RS, '  if (per.p > rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {', '  if (per.p > rect.y1 - rect.y0 || per.s > rect.y1 - rect.y0) {', RP_TESTS),
    ('rp-tier-nested-pattern-allowed', "src", RS, '  if (facts->pattern || facts->masked || facts->union_clip) {\n    return 0;\n  }\n  L->per = per;', '  if (facts->masked || facts->union_clip) {\n    return 0;\n  }\n  L->per = per;', RP_TESTS),
    ('rp-tier-mask-allowed', "src", RS, '  if (facts->pattern || facts->masked || facts->union_clip) {\n    return 0;\n  }\n  L->per = per;', '  if (facts->pattern || facts->union_clip) {\n    return 0;\n  }\n  L->per = per;', RP_TESTS),
    ('rp-tier-window-x-corner', "src", RS, '  L->wx = rect.x0;\n  L->wy = rect.y0;\n  L->tx = tx;', '  L->wx = rect.x1 - per.p;\n  L->wy = rect.y0;\n  L->tx = tx;', RP_TESTS),
    ('rp-tier-window-y-corner', "src", RS, '  L->wy = rect.y0;\n  L->tx = tx;', '  L->wy = rect.y1 - per.s;\n  L->tx = tx;', RP_TESTS),
    ('rp-tier-class-origin-x', "src", RS, '  L->tx = tx;\n  L->ty = ty;\n  L->canon0 = canon;', '  L->tx = 0;\n  L->ty = ty;\n  L->canon0 = canon;', RP_TESTS),
    ('rp-tier-class-origin-y', "src", RS, '  L->tx = tx;\n  L->ty = ty;\n  L->canon0 = canon;', '  L->tx = tx;\n  L->ty = 0;\n  L->canon0 = canon;', RP_TESTS),
    ('rp-tier-window-width-is-height', "src", RS, '    prect.x1 = prect.x0 + L->per.p;\n    prect.y1 = prect.y0 + L->per.s;', '    prect.x1 = prect.x0 + L->per.s;\n    prect.y1 = prect.y0 + L->per.s;', RP_TESTS),
    ('rp-tier-window-height-is-width', "src", RS, '    prect.x1 = prect.x0 + L->per.p;\n    prect.y1 = prect.y0 + L->per.s;', '    prect.x1 = prect.x0 + L->per.p;\n    prect.y1 = prect.y0 + L->per.p;', RP_TESTS),
    ('rp-tier-pass-over-the-whole-rect', "src", RS, '  ms->window = prect;\n  ms->whole_window = whole;', '  ms->window = rect;\n  ms->whole_window = whole;', RP_TESTS),
    ('rp-tier-count-zeroed-over-the-window-only', "src", RS, '  for (int64_t y = prect.y0; y < prect.y1; y++) {\n    memset(L->cnt + at(run, prect.x0, y), 0, (size_t)(prect.x1 - prect.x0));\n  }', '  for (int64_t y = prect.y0; y < prect.y1; y++) {\n    memset(L->cnt + at(run, prect.x0, y), 0, 1u);\n  }', RP_TESTS),
    ('rp-tier-window-not-made-ready', "src", RS, '      widen(run, f, prect); /* the window made ready whole: the pixels no unit reached are transparent */', '      /* the window not made ready */', RP_TESTS),
    ('rp-tier-counter-lost', "src", RS, '    gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, pattern_period));', '', RP_TESTS),
    ('rp-tier-painter-not-told', "src", RS, '  p->period = L->period;\n  if (L->period) {', '  p->period = 0;\n  if (L->period) {', RP_TESTS),
    ('rp-tier-painter-window-x-lost', "src", RS, '    p->wx = L->wx;\n    p->wy = L->wy;', '    p->wx = 0;\n    p->wy = L->wy;', RP_TESTS),
    ('rp-tier-painter-window-y-lost', "src", RS, '    p->wx = L->wx;\n    p->wy = L->wy;', '    p->wx = L->wx;\n    p->wy = 0;', RP_TESTS),
    ('rp-tier-painter-period-lost', "src", RS, '    p->per = L->per;\n    p->wx = L->wx;', '    p->wx = L->wx;', RP_TESTS),
    ('rp-tier-guard-off', "src", RS, '    L->guard = L->period;\n    result = pat_pass(run, L, tile, overlaps, prect);', '    L->guard = 0;\n    result = pat_pass(run, L, tile, overlaps, prect);', RP_TESTS),
    ('rp-tier-guard-left-on', "src", RS, '    result = pat_pass(run, L, tile, overlaps, prect);\n    L->guard = 0;', '    result = pat_pass(run, L, tile, overlaps, prect);', RP_TESTS),
    ('rp-tier-cells-guard-range-lost', "src", RS, '  if (!rect_in_range(c->qbox) ||\n      gshcpu_render_cells_bound(paths, n, c->qbox, run->cs->max_deposits) > run->cs->max_deposits) {', '  if (gshcpu_render_cells_bound(paths, n, c->qbox, run->cs->max_deposits) > run->cs->max_deposits) {', RP_TESTS),
    ('rp-tier-cells-guard-bound-over-the-window', "src", RS, 'gshcpu_render_cells_bound(paths, n, c->qbox, run->cs->max_deposits) > run->cs->max_deposits) {\n    L->abort_tier = 1;', 'gshcpu_render_cells_bound(paths, n, run->layout.region, run->cs->max_deposits) > run->cs->max_deposits) {\n    L->abort_tier = 1;', RP_TESTS),
    ('rp-tier-cells-guard-at-the-cap', "src", RS, 'gshcpu_render_cells_bound(paths, n, c->qbox, run->cs->max_deposits) > run->cs->max_deposits) {\n    L->abort_tier = 1;', 'gshcpu_render_cells_bound(paths, n, c->qbox, run->cs->max_deposits) >= run->cs->max_deposits) {\n    L->abort_tier = 1;', RP_TESTS),
    ('rp-tier-cells-guard-not-called', "src", RS, '  if (result == GSHSCENE_OK && n > 0 && L->guard) {\n    period_guard_cells(run, L, paths, n, c);\n  }', '', RP_TESTS),
    ('rp-tier-stroke-guard-not-called', "src", RS, '  if (L->guard) {\n    period_guard_stroke(run, L, &s);\n  }', '', RP_TESTS),
    ('rp-tier-stroke-guard-range-lost', "src", RS, '  if (!rect_in_range(L->cur_qbox) || bound > run->cs->max_deposits) {', '  if (bound > run->cs->max_deposits) {', RP_TESTS),
    ('rp-tier-stroke-guard-body-only', "src", RS, '  for (int p = GSHCPU_STROKE_BODY; p <= GSHCPU_STROKE_ALIGNED; p++) {\n    bound += s->meta.bound', '  for (int p = GSHCPU_STROKE_BODY; p < GSHCPU_STROKE_ALIGNED; p++) {\n    bound += s->meta.bound', RP_TESTS),
    ('rp-tier-stroke-guard-aligned-only', "src", RS, '  for (int p = GSHCPU_STROKE_BODY; p <= GSHCPU_STROKE_ALIGNED; p++) {\n    bound += s->meta.bound', '  for (int p = GSHCPU_STROKE_ALIGNED; p <= GSHCPU_STROKE_ALIGNED; p++) {\n    bound += s->meta.bound', RP_TESTS),
    ('rp-tier-stroke-guard-sum-at-the-cap', "src", RS, '  if (!rect_in_range(L->cur_qbox) || bound > run->cs->max_deposits) {', '  if (!rect_in_range(L->cur_qbox) || bound >= run->cs->max_deposits) {', RP_TESTS),
    ('rp-tier-gradient-guard-lost', "src", RS, '  if (cur_pat(run) && cur_pat(run)->guard && !gshcpu_render_gradient_in_domain(&p->gr, cur_pat(run)->cur_qbox)) {\n    cur_pat(run)->abort_tier = 1; /* PC9: the whole cell, which the window cannot vouch for */\n  }', '', RP_TESTS),
    ('rp-tier-gradient-guard-over-the-drawn-rect', "src", RS, '!gshcpu_render_gradient_in_domain(&p->gr, cur_pat(run)->cur_qbox)) {\n    cur_pat(run)->abort_tier = 1;', '!gshcpu_render_gradient_in_domain(&p->gr, rect)) {\n    cur_pat(run)->abort_tier = 1;', RP_TESTS),
    ('rp-tier-image-domain-guard-lost', "src", RS, '    if (!gshcpu_render_image_in_domain(&p->im, pl->cur_qbox)) {\n      pl->abort_tier = 1;\n    } else if', '    if (0) {\n      pl->abort_tier = 1;\n    } else if', RP_TESTS),
    ('rp-tier-image-work-guard-lost', "src", RS, '            !gcu_safe_mul_u64(full_bound, full_pixel, &full_bound) || full_bound > run->cs->max_image_work)) {\n      pl->abort_tier = 1;', '            !gcu_safe_mul_u64(full_bound, full_pixel, &full_bound) || 0)) {\n      pl->abort_tier = 1;', RP_TESTS),
    ('rp-tier-image-pixel-work-guard-lost', "src", RS, '(!gshcpu_render_image_area_bound(&p->im, &full_pixel) || full_pixel > run->cs->max_area_pixel_work ||', '(!gshcpu_render_image_area_bound(&p->im, &full_pixel) || 0 ||', RP_TESTS),
    ('rp-tier-image-work-guard-over-the-drawn-rect', "src", RS, '!gcu_safe_mul_u64((uint64_t)(pl->cur_qbox.x1 - pl->cur_qbox.x0), (uint64_t)(pl->cur_qbox.y1 - pl->cur_qbox.y0), &full_bound)', '!gcu_safe_mul_u64((uint64_t)(whole.x1 - whole.x0), (uint64_t)(whole.y1 - whole.y0), &full_bound)', RP_TESTS),
    ('rp-tier-image-guard-for-other-filters', "src", RS, '    } else if (filter == (uint32_t)GSHSCENE_IMAGE_FILTER_AREA &&\n        (!gshcpu_render_image_area_bound', '    } else if (1 &&\n        (!gshcpu_render_image_area_bound', RP_TESTS),
    ('rp-tier-giveup-not-counted', "src", RS, '      gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, period_aborts));', '', RP_TESTS),
    ('rp-tier-giveup-only-on-ok', "src", RS, '    if (L->period && L->abort_tier && (result == GSHSCENE_OK || result == GSHSCENE_ERR_LIMIT)) {', '    if (L->period && L->abort_tier && result == GSHSCENE_OK) {', RP_TESTS),
    ('rp-tier-giveup-never', "src", RS, '    if (L->period && L->abort_tier && (result == GSHSCENE_OK || result == GSHSCENE_ERR_LIMIT)) {', '    if (0) {', RP_TESTS),
    ('rp-tier-giveup-ignores-the-flag', "src", RS, '    if (L->period && L->abort_tier && (result == GSHSCENE_OK || result == GSHSCENE_ERR_LIMIT)) {', '    if (L->period && (result == GSHSCENE_OK || result == GSHSCENE_ERR_LIMIT)) {', RP_TESTS),
    ('rp-tier-giveup-keeps-the-period-flag', "src", RS, '      L->period = 0;\n      L->abort_tier = 0;\n      L->copies = 0;', '      L->abort_tier = 0;\n      L->copies = 0;', RP_TESTS),
    ('rp-tier-giveup-keeps-the-window-rectangle', "src", RS, '      ms->window = rect;\n      run->top = L->frame;', '      run->top = L->frame;', RP_TESTS),
    ('rp-tier-giveup-keeps-the-layer', "src", RS, '      memset(&f->dirty, 0, sizeof f->dirty);\n      memset(&ms->frames[run->top].whole_dirty, 0, sizeof ms->frames[run->top].whole_dirty);', '      memset(&ms->frames[run->top].whole_dirty, 0, sizeof ms->frames[run->top].whole_dirty);', RP_TESTS),
    ('rp-tier-giveup-keeps-the-counts', "src", RS, '      for (int64_t y = rect.y0; y < rect.y1; y++) {\n        memset(L->cnt + at(run, rect.x0, y), 0, (size_t)(rect.x1 - rect.x0));\n      }\n      result = pat_pass(run, L, tile, overlaps, rect);', '      result = pat_pass(run, L, tile, overlaps, rect);', RP_TESTS),
    ('rp-tier-giveup-passes-the-window-again', "src", RS, '      result = pat_pass(run, L, tile, overlaps, rect);\n    } else if (L->period && result == GSHSCENE_OK) {', '      result = pat_pass(run, L, tile, overlaps, prect);\n    } else if (L->period && result == GSHSCENE_OK) {', RP_TESTS),
    ('rp-tier-giveup-keeps-the-stack', "src", RS, '      run->top = L->frame;\n      run->clip_count = f->clip_base;\n      run->groups_open = saved_open;\n      memset(&run->skip, 0, sizeof run->skip);', '      memset(&run->skip, 0, sizeof run->skip);', RP_TESTS),
    ('rp-tier-giveup-keeps-the-arena', "src", RS, '      if (run->item_alloc == &run->arena.self) {\n        gshcpu_render_arena_rewind(&run->arena, L->arena_mark);\n      }\n      L->period = 0;', '      L->period = 0;', RP_TESTS),
    ('rp-tier-giveup-keeps-the-unit-region', "src", RS, '      L->urect.x0 = L->urect.x1 = rect.x0;\n      L->urect.y0 = L->urect.y1 = rect.y0;\n      for (int64_t y = rect.y0;', '      for (int64_t y = rect.y0;', RP_TESTS),
    ('rp-cache-never-used', "src", RS, '  GSHCPU_Stroke_Cache * pcache = L->period && !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;', '  GSHCPU_Stroke_Cache * pcache = NULL;', RP_TESTS),
    ('rp-cache-used-under-caller-memory', "src", RS, '  GSHCPU_Stroke_Cache * pcache = L->period && !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;', '  GSHCPU_Stroke_Cache * pcache = L->period && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;', RP_TESTS),
    ('rp-cache-used-with-the-tier-off', "src", RS, '  GSHCPU_Stroke_Cache * pcache = L->period && !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;', '  GSHCPU_Stroke_Cache * pcache = L->period && !run->caller ? cs->cache : NULL;', RP_TESTS),
    ('rp-cache-hit-ignored', "src", RS, '      period_copy(run, L, f, (double *)(void *)raster, 0);\n      gshcpu_stroke_cache_release(pcache, entry);\n      served = 1;', '      gshcpu_stroke_cache_release(pcache, entry);', RP_TESTS),
    ('rp-cache-hit-not-released', "src", RS, '      period_copy(run, L, f, (double *)(void *)raster, 0);\n      gshcpu_stroke_cache_release(pcache, entry);', '      period_copy(run, L, f, (double *)(void *)raster, 0);', RP_TESTS),
    ('rp-cache-hit-window-not-ready', "src", RS, '      widen(run, f, prect);\n      period_copy(run, L, f, (double *)(void *)raster, 0);', '      period_copy(run, L, f, (double *)(void *)raster, 0);', RP_TESTS),
    ('rp-cache-hit-served-not-set', "src", RS, '      served = 1;\n    }\n  }\n  if (!served) {', '    }\n  }\n  if (!served) {', RP_TESTS),
    ('rp-cache-miss-never-stored', "src", RS, '      if (pcache && gshcpu_period_cache_fits(pcache, (size_t)(L->per.p * L->per.s))) {', '      if (0 && pcache && gshcpu_period_cache_fits(pcache, (size_t)(L->per.p * L->per.s))) {', RP_TESTS),
    ('rp-cache-fit-unchecked', "src", RS, '      if (pcache && gshcpu_period_cache_fits(pcache, (size_t)(L->per.p * L->per.s))) {', '      if (pcache) {', RP_TESTS),
    ('rp-cache-store-copies-the-raster-wrong-way', "src", RS, '          period_copy(run, L, f, raster, 1);\n          const int kept', '          period_copy(run, L, f, raster, 0);\n          const int kept', RP_TESTS),
    ('rp-cache-store-not-released', "src", RS, '          if (kept == 1 || kept == 2) {\n            gshcpu_stroke_cache_release(pcache, entry);\n          }', '', RP_TESTS),
    ('rp-cache-store-rejected-raster-leaks', "src", RS, '          if (kept != 1) {\n            gshcpu_period_cache_free(pcache, raster);\n          }', '', RP_TESTS),
    ('rp-cache-store-kept-raster-freed', "src", RS, '          if (kept != 1) {\n            gshcpu_period_cache_free(pcache, raster);\n          }', '          gshcpu_period_cache_free(pcache, raster);', RP_TESTS),
    ('rp-cache-store-oom-swallowed', "src", RS, '          if (kept < 0) {\n            result = GSHSCENE_ERR_OOM;\n          }', '', RP_TESTS),
    ('rp-cache-make-oom-swallowed', "src", RS, '        if (!raster) {\n          result = GSHSCENE_ERR_OOM;\n        } else {', '        if (!raster) {\n        } else {', RP_TESTS),
    ('rp-cache-class-order-x-lost', "src", RS, '      gshgeom_lattice_reduce(&L->per, L->wx + u - L->tx, L->wy + v - L->ty, r);', '      gshgeom_lattice_reduce(&L->per, L->wx + u, L->wy + v - L->ty, r);', RP_TESTS),
    ('rp-cache-class-order-y-lost', "src", RS, '      gshgeom_lattice_reduce(&L->per, L->wx + u - L->tx, L->wy + v - L->ty, r);', '      gshgeom_lattice_reduce(&L->per, L->wx + u - L->tx, L->wy + v, r);', RP_TESTS),
    ('rp-cache-raster-index-transposed', "src", RS, '      double * cell = &raster[4u * (size_t)(r[1] * L->per.p + r[0])];', '      double * cell = &raster[4u * (size_t)(r[0] * L->per.s + r[1])];', RP_TESTS),
    ('rp-cache-raster-index-by-width', "src", RS, '      double * cell = &raster[4u * (size_t)(r[1] * L->per.p + r[0])];', '      double * cell = &raster[4u * (size_t)(r[1] * L->per.s + r[0])];', RP_TESTS),
    ('rp-cache-pixel-of-the-window-wrong-row', "src", RS, '      double * px = &f->layer[4u * at(run, L->wx + u, L->wy + v)];', '      double * px = &f->layer[4u * at(run, L->wx + u, L->wy)];', RP_TESTS),
    ('rp-cache-pixel-copy-short', "src", RS, '        memcpy(cell, px, 4u * sizeof(double));', '        memcpy(cell, px, 3u * sizeof(double));', RP_TESTS),
    ('rp-cache-restore-short', "src", RS, '        memcpy(px, cell, 4u * sizeof(double));', '        memcpy(px, cell, 3u * sizeof(double));', RP_TESTS),
    ('rp-key-tile-id-lost', "src", RS, '  k->tile_id = gshscene_resource_id(L->pat.tile);', '  k->tile_id = 0;', RP_TESTS),
    ('rp-key-linear-lost', "src", RS, '    k->linear[i] = gshgeom_f64_bits(L->canon0.m[i / 2][i % 2]);', '    k->linear[i] = 0;', RP_TESTS),
    ('rp-key-linear-transposed-last', "src", RS, '    k->linear[i] = gshgeom_f64_bits(L->canon0.m[i / 2][i % 2]);', '    k->linear[i] = gshgeom_f64_bits(L->canon0.m[0][i % 2]);', RP_TESTS),
    ('rp-key-fraction-x-lost', "src", RS, '  k->fraction[0] = gshgeom_f64_bits(L->canon0.t[0]);', '  k->fraction[0] = 0;', RP_TESTS),
    ('rp-key-fraction-y-lost', "src", RS, '  k->fraction[1] = gshgeom_f64_bits(L->canon0.t[1]);', '  k->fraction[1] = 0;', RP_TESTS),
    ('rp-key-layer-space-lost', "src", RS, '  k->layer_space[0] = space_word(&frame->space);', '  k->layer_space[0] = 0;', RP_TESTS),
    ('rp-key-layer-gamma-lost', "src", RS, '  k->layer_space[1] = gshgeom_f64_bits(frame->space.gamma);', '  k->layer_space[1] = 0;', RP_TESTS),
    ('rp-key-target-space-lost', "src", RS, '  k->target_space[0] = space_word(target);', '  k->target_space[0] = 0;', RP_TESTS),
    ('rp-key-target-gamma-lost', "src", RS, '  k->target_space[1] = gshgeom_f64_bits(target->gamma);', '  k->target_space[1] = 0;', RP_TESTS),
    ('rp-key-foreground-color-lost', "src", RS, '      k->foreground[i] = gshgeom_f64_bits(run->foreground.rgba[i]);', '      k->foreground[i] = 0;', RP_TESTS),
    ('rp-key-foreground-alpha-lost', "src", RS, '    for (int i = 0; i < 4; i++) {\n      k->foreground[i] = gshgeom_f64_bits(run->foreground.rgba[i]);\n    }', '    for (int i = 0; i < 3; i++) {\n      k->foreground[i] = gshgeom_f64_bits(run->foreground.rgba[i]);\n    }', RP_TESTS),
    ('rp-key-foreground-space-lost', "src", RS, '    k->foreground_space[0] = space_word(&run->foreground.space);', '    k->foreground_space[0] = 0;', RP_TESTS),
    ('rp-key-palette-lost', "src", RS, '    k->palette_id = run->palette ? gshscene_resource_id(run->palette) : 0u;', '    k->palette_id = 0u;', RP_TESTS),
    ('rp-key-revision-stroke-lost', "src", RS, '  k->revisions[0] = GSHGEOM_STROKE_REVISION;', '  k->revisions[0] = 0;', RP_TESTS),
    ('rp-key-revision-flatten-lost', "src", RS, '  k->revisions[1] = GSHGEOM_FLATTEN_REVISION;', '  k->revisions[1] = 0;', RP_TESTS),
    ('rp-key-revision-clip-lost', "src", RS, '  k->revisions[2] = GSHGEOM_CLIP_REVISION;', '  k->revisions[2] = 0;', RP_TESTS),
    ('rp-key-revision-lattice-lost', "src", RS, '  k->revisions[3] = GSHGEOM_LATTICE_REVISION;', '  k->revisions[3] = 0;', RP_TESTS),
    ('rp-key-not-zeroed', "src", RS, '  memset(k, 0, sizeof *k);\n  k->pattern_id', '  k->pattern_id', RP_TESTS),
    ('rp-key-space-primaries-lost', "src", RS, '  return (uint64_t)(uint32_t)s->primaries | ((uint64_t)(uint32_t)s->transfer << 32);', '  return ((uint64_t)(uint32_t)s->transfer << 32);', RP_TESTS),
    ('rp-key-space-transfer-lost', "src", RS, '  return (uint64_t)(uint32_t)s->primaries | ((uint64_t)(uint32_t)s->transfer << 32);', '  return (uint64_t)(uint32_t)s->primaries;', RP_TESTS),
    ('rp-facts-depth-unbounded', "src", RS, '  if (depth > TILE_FACTS_DEPTH) {\n    f->pattern = 1;\n    return;\n  }', '', RP_TESTS),
    ('rp-facts-pattern-fill-missed', "src", RS, '      if (item->paint.kind == GSHSCENE_PAINT_PATTERN) {\n        f->pattern = 1;', '      if (0) {\n        f->pattern = 1;', RP_TESTS),
    ('rp-facts-foreground-fill-missed', "src", RS, '      } else if (paint_reads_fg(&item->paint)) {\n        f->reads_fg = 1;', '      } else if (0) {\n        f->reads_fg = 1;', RP_TESTS),
    ('rp-facts-foreground-only-kind', "src", RS, '  return paint->kind == GSHSCENE_PAINT_FOREGROUND || paint->kind == GSHSCENE_PAINT_PALETTE;', '  return paint->kind == GSHSCENE_PAINT_FOREGROUND;', RP_TESTS),
    ('rp-facts-palette-only-kind', "src", RS, '  return paint->kind == GSHSCENE_PAINT_FOREGROUND || paint->kind == GSHSCENE_PAINT_PALETTE;', '  return paint->kind == GSHSCENE_PAINT_PALETTE;', RP_TESTS),
    ('rp-facts-gradient-stops-missed', "src", RS, '            f->reads_fg = f->reads_fg || paint_reads_fg(&g.stops[k].color);', '            f->reads_fg = f->reads_fg;', RP_TESTS),
    ('rp-facts-gradient-error-reads-nothing', "src", RS, '        if (gshscene_resource_gradient(item->paint.resource, &g) != GSHSCENE_OK) {\n          f->reads_fg = 1;\n        } else {', '        if (gshscene_resource_gradient(item->paint.resource, &g) != GSHSCENE_OK) {\n        } else {', RP_TESTS),
    ('rp-facts-image-tint-missed', "src", RS, '(ip.tint && paint_reads_fg(ip.tint));', '0;', RP_TESTS),
    ('rp-facts-mask-missed', "src", RS, '    } else if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {\n      f->masked = 1;', '    } else if (0) {\n      f->masked = 1;', RP_TESTS),
    ('rp-facts-instance-not-followed', "src", RS, '    } else if (item->kind == GSHSCENE_ITEM_INSTANCE) {\n      tile_facts(gshscene_resource_picture(item->resource), depth + 1u, f);', '    } else if (item->kind == GSHSCENE_ITEM_INSTANCE) {', RP_TESTS),
    ('rp-facts-stroke-paint-missed', "src", RS, '    if (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) {\n      GSHSCENE_Gradient g;', '    if (item->kind == GSHSCENE_ITEM_FILL) {\n      GSHSCENE_Gradient g;', RP_TESTS),
    ('rp-facts-listing-stops-early', "src", RS, '    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {\n      break;\n    }\n    if (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) {\n      GSHSCENE_Gradient g;', '    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK || i > 0) {\n      break;\n    }\n    if (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) {\n      GSHSCENE_Gradient g;', RP_TESTS),
    ('rp-table-stroke-hit-on-a-period', "src", RSC, '    if (!e->kind && key_equal(&e->key, key)) {\n      e->refs++;\n      /* To the head of the recency list. */', '    if (key_equal(&e->key, key)) {\n      e->refs++;\n      /* To the head of the recency list. */', RP_TESTS),
    ('rp-table-period-hit-on-a-stroke', "src", RSC, '    if (e->kind && period_key_equal(&e->pkey, key)) {\n      e->refs++;\n      touch(c, e);', '    if (period_key_equal(&e->pkey, key)) {\n      e->refs++;\n      touch(c, e);', RP_TESTS),
    ('rp-table-period-key-prefix-only', "src", RSC, '  return memcmp(a, b, sizeof *a) == 0; /* the key is built with every byte written (padding included): see period_key_zeroed */', '  return memcmp(a, b, 64u) == 0;', RP_TESTS),
    ('rp-table-hit-not-touched', "src", RSC, '      e->refs++;\n      touch(c, e);\n      *entry = e;\n      *raster = e->raster;', '      e->refs++;\n      *entry = e;\n      *raster = e->raster;', RP_TESTS),
    ('rp-table-hit-not-pinned', "src", RSC, '      e->refs++;\n      touch(c, e);\n      *entry = e;\n      *raster = e->raster;', '      touch(c, e);\n      *entry = e;\n      *raster = e->raster;', RP_TESTS),
    ('rp-table-hit-not-counted', "src", RSC, '      if (c->counters) {\n        c->counters->period_hits++;\n      }', '', RP_TESTS),
    ('rp-table-miss-not-counted', "src", RSC, '  if (c->counters) {\n    c->counters->period_misses++;\n  }', '', RP_TESTS),
    ('rp-table-kept-not-counted', "src", RSC, '      if (c->counters) {\n        c->counters->period_kept++;\n      }', '', RP_TESTS),
    ('rp-table-need-without-the-entry', "src", RSC, '  return (uint64_t)pixels * 4u * sizeof(double) + sizeof(Entry);', '  return (uint64_t)pixels * 4u * sizeof(double);', RP_TESTS),
    ('rp-table-need-three-doubles', "src", RSC, '  return (uint64_t)pixels * 4u * sizeof(double) + sizeof(Entry);', '  return (uint64_t)pixels * 3u * sizeof(double) + sizeof(Entry);', RP_TESTS),
    ('rp-table-fits-over-the-bound', "src", RSC, '  return period_need(pixels) <= c->bound;', '  return period_need(pixels) <= c->bound + 4096u;', RP_TESTS),
    ('rp-table-store-over-the-bound', "src", RSC, '  Entry * fresh = need <= c->bound ? c->alloc.malloc_fn(c->alloc.ctx, sizeof *fresh) : NULL;\n  if (need <= c->bound && !fresh) {\n    return -1;\n  }\n  Entry * doomed = NULL;\n  int result = 0;\n  GCU_MUTEX_LOCK(c->lock);\n  if (c->counters) {\n    c->counters->period_misses++;', '  Entry * fresh = c->alloc.malloc_fn(c->alloc.ctx, sizeof *fresh);\n  if (!fresh) {\n    return -1;\n  }\n  Entry * doomed = NULL;\n  int result = 0;\n  GCU_MUTEX_LOCK(c->lock);\n  if (c->counters) {\n    c->counters->period_misses++;', RP_TESTS),
    ('rp-table-store-duplicate-not-found', "src", RSC, '    if (e->kind && period_key_equal(&e->pkey, key)) {\n      e->refs++;\n      *entry = e;\n      result = 2;', '    if (0) {\n      e->refs++;\n      *entry = e;\n      result = 2;', RP_TESTS),
    ('rp-table-store-room-never-made', "src", RSC, '    const uint64_t room = c->bound - need;\n    Entry * e = c->tail;\n    while (c->held > room && e) {', '    const uint64_t room = c->bound - need;\n    Entry * e = NULL;\n    while (c->held > room && e) {', RP_TESTS),
    ('rp-table-store-evicts-pinned', "src", RSC, '      Entry * newer = e->newer;\n      if (e->refs == 0) {\n        unlink_entry(c, e);\n        if (c->counters) {\n          c->counters->cache_evictions++;\n        }\n        e->chain = doomed;\n        doomed = e;\n      }\n      e = newer;\n    }\n    if (c->held <= room) {\n      memset(fresh, 0, sizeof *fresh);\n      fresh->kind = 1;', '      Entry * newer = e->newer;\n      {\n        unlink_entry(c, e);\n        if (c->counters) {\n          c->counters->cache_evictions++;\n        }\n        e->chain = doomed;\n        doomed = e;\n      }\n      e = newer;\n    }\n    if (c->held <= room) {\n      memset(fresh, 0, sizeof *fresh);\n      fresh->kind = 1;', RP_TESTS),
    ('rp-table-store-held-not-added', "src", RSC, '      c->held += need;\n      if (c->counters) {\n        c->counters->period_kept++;', '      if (c->counters) {\n        c->counters->period_kept++;', RP_TESTS),
    ('rp-table-store-kind-not-set', "src", RSC, '      memset(fresh, 0, sizeof *fresh);\n      fresh->kind = 1;\n      fresh->pkey = *key;', '      memset(fresh, 0, sizeof *fresh);\n      fresh->pkey = *key;', RP_TESTS),
    ('rp-table-store-key-not-set', "src", RSC, '      fresh->pkey = *key;\n      fresh->raster = raster;', '      fresh->raster = raster;', RP_TESTS),
    ('rp-table-store-bucket-of-the-stroke-key', "src", RSC, '      const size_t b = bucket_of_period(key);\n      fresh->chain = c->buckets[b];', '      const size_t b = 0;\n      fresh->chain = c->buckets[b];', RP_TESTS),
    ('rp-table-unlink-bucket-of-the-stroke-key', "src", RSC, '  Entry ** at = &c->buckets[bucket_of_entry(e)];', '  Entry ** at = &c->buckets[bucket_of(&e->key)];', RP_TESTS),
    ('rp-table-free-raster-as-outline', "src", RSC, '  if (e->kind) {\n    c->alloc.free_fn(c->alloc.ctx, e->raster);\n  } else {', '  if (0) {\n    c->alloc.free_fn(c->alloc.ctx, e->raster);\n  } else {', RP_TESTS),
    ('rp-table-evict-pattern-id-missed', "src", RSC, '(e->pkey.pattern_id == id || e->pkey.tile_id == id)', '(e->pkey.tile_id == id)', RP_TESTS),
    ('rp-table-evict-tile-id-missed', "src", RSC, '(e->pkey.pattern_id == id || e->pkey.tile_id == id)', '(e->pkey.pattern_id == id)', RP_TESTS),
    ('rp-table-evict-ignores-the-period', "src", RSC, '      if (e->kind ? (e->pkey.pattern_id == id || e->pkey.tile_id == id) : (e->key.path_id == id || e->key.style_id == id || (e->key.hatch_id != 0 && e->key.hatch_id == id))) {', '      if (e->kind ? 0 : (e->key.path_id == id || e->key.style_id == id || (e->key.hatch_id != 0 && e->key.hatch_id == id))) {', RP_TESTS),
    ('rp-table-bucket-ignores-the-fraction', "src", RSC, '  h = (h ^ k->fraction[1]) * UINT64_C(0x100000001B3);\n  h ^= h >> 29;\n  return (size_t)(h & (BUCKETS - 1u));\n}\n\nstatic size_t bucket_of_entry', '  h ^= h >> 29;\n  return (size_t)(h & (BUCKETS - 1u));\n}\n\nstatic size_t bucket_of_entry', RP_TESTS),
    ('rp-table-make-too-small', "src", RSC, '  return c->alloc.malloc_fn(c->alloc.ctx, pixels * 4u * sizeof(double));', '  return c->alloc.malloc_fn(c->alloc.ctx, pixels * 3u * sizeof(double));', RP_TESTS),
    ('rp-memory-quad-buffer-still-sized', "src", RS, '      place(&at, pixels, &o->at_tmp) &&', '      place(&at, pixels / 2u, &o->at_tmp) &&', RP_TESTS),
    ('rp-memory-pass-levels-short', "src", RS, '  M->pat_levels = nest.pdepth_max;', '  M->pat_levels = nest.pdepth_max > 1u ? nest.pdepth_max - 1u : nest.pdepth_max;', RP_TESTS),
    ('rp-memory-depth-not-restored', "src", RS, '  pr(run)->pat = saved_pat;\n  pr(run)->pat_depth--;', '  pr(run)->pat = saved_pat;', RP_TESTS),
    ('rp-limit-refusal-not-reported', "src", RS, '  if (result == GSHSCENE_ERR_LIMIT) {\n    p->refused = 1;\n    return refuse(run, item, chain, count);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  p->lf', '  if (result == GSHSCENE_ERR_LIMIT) {\n    p->refused = 1;\n    return GSHSCENE_OK;\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  p->lf', RP_TESTS),
    ('rp-limit-error-swallowed', "src", RS, '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  p->lf', '  p->lf', RP_TESTS),
    ('rp-pass-counter-lost', "src", RS, '  gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, patterns));', '', RP_TESTS),
    ('rp-pass-copies-counter-lost', "src", RS, '  gshcpu_stroke_cache_add(cs->cache, offsetof(GSHCPU_Render_Counters, pattern_copies), L->copies);', '', RP_TESTS),
    ('rp-pass-copy-not-counted', "src", RS, '  L->urect = gshcpu_render_rect_join(L->urect, rect_moved(rc, c->sx, c->sy));\n  L->copies++;', '  L->urect = gshcpu_render_rect_join(L->urect, rect_moved(rc, c->sx, c->sy));', RP_TESTS),
    ('rp-pass-widens-nothing', "src", RS, '  if (!quiet_of(run)) {\n    widen_w(run, frame, rect, whole);\n  }\n  run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);\n  return GSHSCENE_OK;\n}\n\n/** Go through one unit.', '  run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);\n  return GSHSCENE_OK;\n}\n\n/** Go through one unit.', RP_TESTS),
    ('rp-pass-work-not-counted', "src", RS, '  run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);\n  return GSHSCENE_OK;\n}\n\n/** Go through one unit.', '  return GSHSCENE_OK;\n}\n\n/** Go through one unit.', RP_TESTS),
    ('rp-pass-layer-not-isolated', "src", RS, "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n  f->group.opacity = 1.0;\n  f->group.composite = GSHSCENE_COMPOSITE_SRC_OVER;\n  f->group.isolation = GSHSCENE_ISOLATION_ISOLATED;", "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n  f->group.opacity = 1.0;\n  f->group.composite = GSHSCENE_COMPOSITE_SRC_OVER;\n  f->group.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;", RP_TESTS),
    ('rp-pass-layer-opacity', "src", RS, "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n  f->group.opacity = 1.0;", "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n  f->group.opacity = 0.5;", RP_TESTS),
    ('rp-pass-layer-space', "src", RS, "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n", "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n", RP_TESTS),
    ('rp-pass-layer-clip-base', "src", RS, "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n  f->group.opacity = 1.0;\n  f->group.composite = GSHSCENE_COMPOSITE_SRC_OVER;\n  f->group.isolation = GSHSCENE_ISOLATION_ISOLATED;\n  f->clip_base = run->clip_count;", "  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */\n  Frame * f = &run->frames[++run->top];\n  memset(f, 0, sizeof *f);\n  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;\n  f->space = frame->space;\n  f->group.opacity = 1.0;\n  f->group.composite = GSHSCENE_COMPOSITE_SRC_OVER;\n  f->group.isolation = GSHSCENE_ISOLATION_ISOLATED;\n  f->clip_base = 0;", RP_TESTS),
    ('rp-pass-origin-shift-lost', "src", RS, '  pr(run)->org_x -= pr(run)->pshx;\n  pr(run)->org_y -= pr(run)->pshy;', '  pr(run)->org_x -= 0;\n  pr(run)->org_y -= pr(run)->pshy;', RP_TESTS + ['testRender']),
    ('rp-pass-whole-region-not-set', "src", RS, '  pr(run)->whole_cur = whole;\n  ms->window = prect;', '  ms->window = prect;', RP_TESTS),
    ('rp-pass-empty-whole-drawn', "src", RS, '  if (gshcpu_render_rect_empty(whole)) {\n    p->refused = 1; /* nothing of it is in the whole render */\n    return GSHSCENE_OK;\n  }\n  GSHSCENE_Pattern pt;', '  GSHSCENE_Pattern pt;', RP_TESTS),
]


PLANTS += [
    # --- A3: union clips (shape-scene clip_union.h; CU18 to CU22) ---
    ('cu-cover-plane-unsaturated', 'src', 'src/render/cover.c',
     'row[j - 1] = (uint16_t)(sum > 8192u ? 8192u : sum); /* CU18: saturated at the whole pixel */',
     'row[j - 1] = (uint16_t)sum;',
     ['testRender_clip_union']),
    ('cu-cover-plane-signed-fold', 'src', 'src/render/cover.c',
     'const uint32_t area = gshcpu_fold_area(accumulated - ar[j], rule);\n          uint32_t sum',
     'const uint32_t area = gshcpu_fold_area(accumulated - ar[j] > 0 ? accumulated - ar[j] : 0, rule);\n          uint32_t sum',
     ['testRender_clip_union']),
    ('cu-cover-even-odd-as-nonzero', 'srct', 'src/core/fold.h',
     '  if (rule == GSHGEOM_FILL_EVEN_ODD) {\n    m &= 2u',
     '  if (rule == (GSHGEOM_Fill_Rule)99) {\n    m &= 2u',
     ['testRender_clip_union']),
    ('cu-cover-plane-row-offset', 'src', 'src/render/cover.c',
     ' + (rect.x0 - prect.x0));\n        for',
     ' + 0);\n        for',
     ['testRender_clip_union']),
    ('cu-sink-member-rule-of-item', 'src', 'src/render/sink.c',
     'r = gshcpu_render_cover_area(paths, g.count, member.fill_rule, mrect,',
     'r = gshcpu_render_cover_area(paths, g.count, item->fill_rule, mrect,',
     ['testRender_clip_union']),
    ('cu-sink-skips-last-member', 'src', 'src/render/sink.c',
     'for (size_t i = 0; i < n && result == GSHSCENE_OK && !refused; i++) {',
     'for (size_t i = 0; i + 1 < n && result == GSHSCENE_OK && !refused; i++) {',
     ['testRender_clip_union']),
    ('cu-sink-quantize-shift', 'src', 'src/render/sink.c',
     'const uint32_t level = (uint32_t)plane[k] >> 5; /* CU2: quantized once, on the sum */',
     'const uint32_t level = (uint32_t)plane[k] >> 6; /* CU2: quantized once, on the sum */',
     ['testRender_clip_union']),
    ('cu-sink-plane-not-cleared', 'src', 'src/render/sink.c',
     'plane = run->item_alloc->calloc_fn(run->item_alloc->ctx, pixels, sizeof *plane); /* zero: cleared for every push */',
     'plane = run->item_alloc->malloc_fn(run->item_alloc->ctx, pixels * sizeof *plane);',
     ['testRender_clip_union']),
    ('cu-sink-tests-unbilled', 'src', 'src/render/sink.c',
     '      uint64_t tests = verdict.tests;\n',
     '      uint64_t tests = 0;\n',
     ['testRender_clip_union']),
    ('cu-sink-tests-limit-off-by-one', 'src', 'src/render/sink.c',
     'run_tests(run) > run->cs->max_union_tests',
     'run_tests(run) >= run->cs->max_union_tests',
     ['testRender_clip_union']),
    ('cu-sink-refused-member-dropped', 'src', 'src/render/sink.c',
     '    if (r == GSHCPU_ERR_LIMIT) {\n      refused = 1;',
     '    if (r == GSHCPU_ERR_LIMIT) {\n      refused = 0;',
     ['testRender_clip_union']),
    ('cu-sink-claims-general', 'src', 'src/render/sink.c',
     '    out->flags |= GSHSCENE_CAP_CLIP_UNION;\n',
     '    out->flags |= GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL;\n',
     ['testRender_clip_union']),
    ('cu-sink-tier-not-checked', 'src', 'src/render/sink.c',
     '  if (cs->tiers & (GSHCPU_RENDER_TIER_UNION | GSHCPU_RENDER_TIER_UNION_GENERAL)) {\n    out->flags |= GSHSCENE_CAP_CLIP_UNION;',
     '  if (1) {\n    out->flags |= GSHSCENE_CAP_CLIP_UNION;',
     ['testRender_clip_union']),
    ('cu-sink-fast-uncounted', 'src', 'src/render/sink.c',
     '  cs->counters->union_fast++;\n',
     '  cs->counters->union_fast += 0;\n',
     ['testRender_clip_union']),
    ('cu-sink-sweeps-uncounted', 'src', 'src/render/sink.c',
     '      cs->counters->union_sweeps++;\n    }\n    gshcpu_render_geometry_release(&g, run->item_alloc);',
     '      cs->counters->union_sweeps += 0;\n    }\n    gshcpu_render_geometry_release(&g, run->item_alloc);',
     ['testRender_clip_union']),
    ('cu-sink-measure-no-plane', 'src', 'src/render/sink.c',
     'calloc_fn(arena->self.ctx, pixels ? pixels : 1u, sizeof(uint16_t));',
     'calloc_fn(arena->self.ctx, 1 + 0 * pixels, 1);',
     ['testRender_clip_union']),
    ('cu-sink-box-right-edge-of-first', 'src', 'src/render/sink.c',
     '      bound.x1 = m.x1 > bound.x1 ? m.x1 : bound.x1;',
     '      bound.x1 = bound.x1;',
     ['testRender_clip_union']),
    ('cu-sink-union-push-skipped', 'src', 'src/render/sink.c',
     '  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {\n    return union_push(run, item, chain, count);',
     '  if (0) {\n    return union_push(run, item, chain, count);',
     ['testRender_clip_union']),
]


_CG = ['testRender_clip_union_general']
_CGU = ['testRender_clip_union_general', 'testRender_clip_union']

# --- A3b: the general tier of a union clip (render.h, CG1 to CG12): one plant for each rule and branch of the step for several parts, of the
# marks and the kept pixels, of the groups, the push, the measure and the bill.  Branches the design makes equivalent are not planted; the ones
# whose equivalence needs a witness are in SURVIVORS.
PLANTS += [
    # --- exact.c: the group's gaps, rules and arithmetic ---
    ('cg-x-weight-needs-two', 'src', 'src/render/exact.c',
     'g[i].om = inside > 0 ? 2 : 0;', 'g[i].om = inside > 1 ? 2 : 0;', _CG),
    ('cg-x-count-not-kept-at-exchange', 'src', 'src/render/exact.c',
     'mid.cnt = g[pos].cnt - in_rule(pr, mq, x) + in_rule(pr, mq, x + dq);', 'mid.cnt = g[pos].cnt;', _CG),
    ('cg-x-count-old-state-ignored', 'src', 'src/render/exact.c',
     'mid.cnt = g[pos].cnt - in_rule(pr, mq, x) + in_rule(pr, mq, x + dq);', 'mid.cnt = g[pos].cnt + in_rule(pr, mq, x + dq);', _CG),
    ('cg-x-winding-same-part-step', 'src', 'src/render/exact.c',
     'const int32_t x = g[pos + 2].wl - dq - (mp == mq ? dp : 0);', 'const int32_t x = g[pos + 2].wl - dq;', _CG),
    ('cg-x-mid-right-same-part', 'src', 'src/render/exact.c',
     'mid.wr = lmp + (mp == mq ? dq : 0);', 'mid.wr = lmp;', _CG),
    ('cg-x-mid-left-step', 'src', 'src/render/exact.c',
     'mid.wl = x + dq;', 'mid.wl = x;', _CG),
    ('cg-x-right-gap-left-step', 'src', 'src/render/exact.c',
     'g[pos + 2].wl = mid.wr + dp; /* R\'s left fragment is now p */', 'g[pos + 2].wl = mid.wr; /* R\'s left fragment is now p */', _CG),
    ('cg-x-left-gap-right-record', 'src', 'src/render/exact.c',
     'g[pos].wr = x;               /* L\'s right fragment is now q */', 'g[pos].wr = lmp;               /* L\'s right fragment is now q */', _CG),
    ('cg-x-exchange-weight-dropped', 'src', 'src/render/exact.c',
     'note_wmax(c, mid.wr);\n    int32_t dd = g[pos].om + g[pos + 2].om - g[pos + 1].om - mid.om;',
     'note_wmax(c, mid.wr);\n    int32_t dd = g[pos].om + g[pos + 2].om - g[pos + 1].om - g[pos + 1].om;', _CG),
    ('cg-x-even-odd-as-nonzero', 'src', 'src/render/exact.c',
     'return pr->rule_of[part] ? (w & 1) != 0 : w != 0;', '(void)pr; (void)part; return w != 0;', _CG),
    ('cg-x-nonzero-as-parity', 'src', 'src/render/exact.c',
     'return pr->rule_of[part] ? (w & 1) != 0 : w != 0;', '(void)pr; (void)part; return (w & 1) != 0;', _CG),
    ('cg-x-rules-swapped', 'src', 'src/render/exact.c',
     'return pr->rule_of[part] ? (w & 1) != 0 : w != 0;', 'return !pr->rule_of[part] ? (w & 1) != 0 : w != 0;', _CG),
    ('cg-x-count-before-step-kept', 'src', 'src/render/exact.c',
     '      inside -= in_rule(pr, part, cur[part]);\n      cur[part] += GSHCPU_RENDER_EXACT_EPS * f->s * (int32_t)f->mu;',
     '      cur[part] += GSHCPU_RENDER_EXACT_EPS * f->s * (int32_t)f->mu;', _CG),
    ('cg-x-gap-left-winding', 'src', 'src/render/exact.c',
     '      g[i].wl = cur[part];', '      g[i].wl = 0;', _CG),
    ('cg-x-gap-right-winding', 'src', 'src/render/exact.c',
     'g[i].wr = i < c->n ? cur[s->frag[c->ord[i]].part] : 0;', 'g[i].wr = 0;', _CG),
    ('cg-x-initial-count', 'src', 'src/render/exact.c',
     '    cur[p] = (int32_t)c->wind0[p];\n    note_wmax(c, cur[p]);\n    inside += in_rule(pr, p, cur[p]);',
     '    cur[p] = (int32_t)c->wind0[p];\n    note_wmax(c, cur[p]);', _CG),
    ('cg-x-empty-slab-count', 'src', 'src/render/exact.c',
     '      inside += in_rule(pr, p, (int32_t)c->wind0[p]);', '      inside += 0;', _CG),
    ('cg-x-floor-offset-dropped', 'src', 'src/render/exact.c',
     '*out = (v + (int64_t)beta) >> 30;', '*out = v >> 30;', _CG),
    ('cg-x-round-for-floor', 'src', 'src/render/exact.c',
     '*out = (v + (int64_t)beta) >> 30;', '*out = (v + ((int64_t)1 << 29) + (int64_t)beta) >> 30;', _CG),
    ('cg-x-group-budget-hold-dropped', 'src', 'src/render/exact.c',
     'return params->max_crossings == 0 || params->max_crossings > gshcpu_exact_group_crossings ? gshcpu_exact_group_crossings\n                                                                                              : params->max_crossings;',
     'return params->max_crossings;', _CG),
    ('cg-x-hook-takes-groups', 'src', 'src/render/exact.c',
     'if (gshcpu_exact_hook && params->mode != GSHCPU_EXACT_GROUP) {', 'if (gshcpu_exact_hook) {', ['testRender_dual']),
    ('cg-x-part-orientation', 'src', 'src/render/exact.c',
     'return pr->mode == GSHCPU_EXACT_GROUP ? GSHCPU_RENDER_EXACT_EPS : pr->eps[p];', 'return pr->eps[p];', _CG),
    ('cg-x-group-count-accessor', 'src', 'src/render/exact.c',
     'return pr->mode == GSHCPU_EXACT_GROUP ? cell->gcount[p] : cell->count[p];', '(void)pr; return cell->count[p];', _CG),
    ('cg-x-group-a-left-accessor', 'src', 'src/render/exact.c',
     'return pr->mode == GSHCPU_EXACT_GROUP ? cell->ga_left[p] : cell->a_left[p];', '(void)pr; return cell->a_left[p];', _CG),
    ('cg-x-group-dense-accessor', 'src', 'src/render/exact.c',
     'return pr->mode == GSHCPU_EXACT_GROUP ? cell->gdense[p] : cell->dense[p];', '(void)pr; return cell->dense[p];', _CG),
    ('cg-x-steps-not-cleared', 'src', 'src/render/exact.c',
     'memset(c->jm, 0, (size_t)params->parts * sizeof *c->jm);', ';', _CG),
    ('cg-x-steps64-not-cleared', 'src', 'src/render/exact.c',
     'memset(c->j64, 0, (size_t)params->parts * sizeof *c->j64);', ';', _CG),
    ('cg-x-no-rules-accepted', 'src', 'src/render/exact.c',
     'return GSHCPU_EXACT_LIMIT; /* more parts than the scratch holds: the caller counts first, so reaching this is a defect, and refuses */',
     'return GSHCPU_EXACT_OK; /* more parts than the scratch holds: the caller counts first, so reaching this is a defect, and refuses */', _CG),
    ('cg-x-scratch-capacity-off-by-one', 'src', 'src/render/exact.c',
     'if (pieces > s->capacity) {', 'if (pieces >= s->capacity) {', _CG),
    ('cg-x-part-wider-than-byte', 'src', 'src/render/exact.c',
     'f->part = (uint16_t)p;', 'f->part = (uint8_t)p;', _CG),
    # --- cover.c: the marks, the pixels kept, the whole-pixel mark ---
    ('cg-c-kept-needs-two-members', 'src', 'src/render/cover.c',
     'if (cap ? (n >= 1 && group_wanted(cap, c->rect.x0 + x, c->band + r)) : n >= 2) {', 'if (cap ? (n >= 2 && group_wanted(cap, c->rect.x0 + x, c->band + r)) : n >= 2) {', _CG),
    ('cg-c-kept-every-piece', 'src', 'src/render/cover.c',
     'if (cap ? (n >= 1 && group_wanted(cap, c->rect.x0 + x, c->band + r)) : n >= 2) {', 'if (cap ? n >= 1 : n >= 2) {', _CG),
    ('cg-c-wanted-one-piece', 'src', 'src/render/cover.c',
     'return (*group_mark(cap, px, py) & GSHCPU_GROUP_COUNT) >= 2u;', 'return (*group_mark(cap, px, py) & GSHCPU_GROUP_COUNT) >= 1u;', _CG),
    ('cg-c-wanted-three-pieces', 'src', 'src/render/cover.c',
     'return (*group_mark(cap, px, py) & GSHCPU_GROUP_COUNT) >= 2u;', 'return (*group_mark(cap, px, py) & GSHCPU_GROUP_COUNT) >= 3u;', _CG),
    ('cg-c-skip-eight-selection', 'src', 'src/render/cover.c',
     'if (x + 7 <= xe && !(cap ? any_nonzero8(&ex->count[base + (size_t)x]) : any_multi8(&ex->count[base + (size_t)x]))) {',
     'if (x + 7 <= xe && !any_multi8(&ex->count[base + (size_t)x])) {', _CG),
    ('cg-c-skip-eight-window', 'src', 'src/render/cover.c',
     'if (j + 7 <= j1 && !(cap ? any_nonzero8(&ex->count[cbase + (size_t)(j - 1)]) : any_multi8(&ex->count[cbase + (size_t)(j - 1)]))) {',
     'if (j + 7 <= j1 && !any_multi8(&ex->count[cbase + (size_t)(j - 1)])) {', _CG),
    ('cg-c-record-cover-left', 'src', 'src/render/cover.c',
     'rec->a_left = left;', 'rec->a_left = 0;', _CG),
    ('cg-c-record-dense', 'src', 'src/render/cover.c',
     'rec->dense = ex->eps * (sum * (ONE_PIXEL * 2) - ar[j]);', 'rec->dense = 0;', _CG),
    ('cg-c-record-part', 'src', 'src/render/cover.c',
     'rec->part = cap->part;', 'rec->part = 0;', _CG),
    ('cg-c-record-pixel-column', 'src', 'src/render/cover.c',
     '(c->rect.x0 + (j - 1) - cap->rect.x0));', '(j - 1));', _CG),
    ('cg-c-record-pixel-row', 'src', 'src/render/cover.c',
     'rec->cell = (uint32_t)((c->band + r - cap->rect.y0) * (cap->rect.x1 - cap->rect.x0)', 'rec->cell = (uint32_t)((r) * (cap->rect.x1 - cap->rect.x0)', _CG),
    ('cg-c-record-first', 'src', 'src/render/cover.c',
     'rec->first = (uint32_t)cap->piece_n;', 'rec->first = 0;', _CG),
    ('cg-c-record-pieces-copy', 'src', 'src/render/cover.c',
     'memcpy(cap->pieces + cap->piece_n, from, (size_t)n * sizeof *from);', 'memcpy(cap->pieces + cap->piece_n, from, (size_t)(n - 1) * sizeof *from);', _CG),
    ('cg-c-pieces-cap-off-by-one', 'src', 'src/render/cover.c',
     'if (cap->piece_cap - cap->piece_n < n) {', 'if (cap->piece_cap - cap->piece_n <= n) {', _CG),
    ('cg-c-marks-forget-earlier-members', 'src', 'src/render/cover.c',
     'const uint32_t held = (uint32_t)(*mark & GSHCPU_GROUP_COUNT) + counts[x];', 'const uint32_t held = counts[x];', _CG),
    ('cg-c-marks-unsaturated', 'src', 'src/render/cover.c',
     '*mark = (uint8_t)(held > GSHCPU_GROUP_COUNT ? GSHCPU_GROUP_COUNT : held);', '*mark = (uint8_t)held;', _CG),
    ('cg-c-whole-mark-not-set', 'src', 'src/render/cover.c',
     '*group_mark(gcap, rect.x0 + (j - 1), band + r) |= (uint8_t)GSHCPU_GROUP_FULL;', '*group_mark(gcap, rect.x0 + (j - 1), band + r) |= 0;', _CG),
    ('cg-c-whole-mark-with-pieces', 'src', 'src/render/cover.c',
     'if (gcap && area >= 8192u && ex->count[exact_cell_index(&c, rect.x0 + (j - 1), band + r)] == 0) {', 'if (gcap && area >= 8192u) {', _CG),
    ('cg-c-plane-rows-offset', 'src', 'src/render/cover.c',
     'GSHCPU_Result r = cover_run(paths, count, rule, rect, rect, NULL, NULL, plane, prect, acc, max_deposits, cells, ex, cap);',
     '(void)prect;\n  GSHCPU_Result r = cover_run(paths, count, rule, rect, rect, NULL, NULL, plane, rect, acc, max_deposits, cells, ex, cap);', _CG),
    ('cg-c-marks-rect-clipped-short', 'src', 'src/render/cover.c',
     '  c.walk = rect;\n  c.ex = ex;\n  c.cstride = rect.x1 - rect.x0;', '  c.walk = rect;\n  c.ex = ex;\n  c.cstride = rect.x1 - rect.x0 - 1;', _CG),
    # --- sink.c: the plan, the groups, the push, the measure, the bill ---
    ('cg-s-boxes-meet-when-touching', 'src', 'src/render/sink.c',
     '  return x0 < x1 && y0 < y1;\n}\n\nstatic uint16_t union_find', '  return x0 <= x1 && y0 <= y1;\n}\n\nstatic uint16_t union_find', _CG),
    ('cg-s-boxes-meet-on-one-axis', 'src', 'src/render/sink.c',
     '  return x0 < x1 && y0 < y1;\n}\n\nstatic uint16_t union_find', '  (void)y0;\n  (void)y1;\n  return x0 < x1;\n}\n\nstatic uint16_t union_find', _CG),
    ('cg-s-one-group-switch-ignored', 'src', 'src/render/sink.c',
     'if (one_group ? 1 : boxes_meet(&pl->box[i], &pl->box[j])) {', 'if ((void)one_group, boxes_meet(&pl->box[i], &pl->box[j])) {', _CG),
    ('cg-s-one-group-always', 'src', 'src/render/sink.c',
     'if (one_group ? 1 : boxes_meet(&pl->box[i], &pl->box[j])) {', 'if ((void)one_group, 1) {', _CG),
    ('cg-s-no-groups', 'src', 'src/render/sink.c',
     'if (one_group ? 1 : boxes_meet(&pl->box[i], &pl->box[j])) {', 'if (one_group) {', _CG),
    ('cg-s-group-list-order', 'src', 'src/render/sink.c',
     'for (size_t i = n; i-- > 0;) {\n    pl->next[i] = pl->head[pl->root[i]];', 'for (size_t i = 0; i < n; i++) {\n    pl->next[i] = pl->head[pl->root[i]];', _CG),
    ('cg-s-group-rect-is-a-meet', 'src', 'src/render/sink.c',
     'g = gshcpu_render_rect_join(g, pl->rect[i]);', 'g = gshcpu_render_rect_meet(g, pl->rect[i]);', _CG),
    ('cg-s-group-rect-not-clipped', 'src', 'src/render/sink.c',
     '  g = gshcpu_render_rect_meet(g, wide);\n  if (gshcpu_render_rect_empty(g)) {', '  (void)wide;\n  if (gshcpu_render_rect_empty(g)) {', _CG),
    ('cg-s-work-over-the-damage', 'src', 'src/render/sink.c',
     '  pl->wide = gshcpu_render_rect_meet(bound, whole);', '  pl->wide = gshcpu_render_rect_meet(bound, reach);\n  (void)whole;', _CG),
    ('cg-s-pixel-limit-off-by-one', 'src', 'src/render/sink.c',
     'if (pixels64 > gshcpu_union_pixels_max) {', 'if (pixels64 >= gshcpu_union_pixels_max) {', _CG),
    ('cg-s-pixel-limit-removed', 'src', 'src/render/sink.c',
     'if (pixels64 > gshcpu_union_pixels_max) {', 'if (0) {', _CG),
    ('cg-s-pieces-limit-field', 'src', 'src/render/sink.c',
     '  b->piece_cap = cs->max_union_pieces > (uint64_t)SIZE_MAX / sizeof(GSHCPU_Group_Record) ? 0 : (size_t)cs->max_union_pieces;',
     '  b->piece_cap = cs->max_union_pieces > (uint64_t)SIZE_MAX / sizeof(GSHCPU_Group_Record) ? 0 : (size_t)cs->max_union_pieces + 1u;', _CG),
    ('cg-s-singleton-as-group', 'src', 'src/render/sink.c',
     '    if (pl->next[r] == UNION_NONE) {\n      /* A group of one', '    if (0) {\n      /* A group of one', _CG),
    ('cg-s-non-root-swept', 'src', 'src/render/sink.c',
     '    if (pl->root[r] != r) {\n      continue;\n    }\n    if (in_arena) {', '    if (0) {\n      continue;\n    }\n    if (in_arena) {', _CG),
    ('cg-s-group-pass-skipped', 'src', 'src/render/sink.c',
     'for (int pass = 0; pass < 2 && !*refused; pass++) {', 'for (int pass = 1; pass < 2 && !*refused; pass++) {', _CG),
    ('cg-s-rewind-in-group', 'src', 'src/render/sink.c',
     '      if (in_arena) {\n        gshcpu_render_arena_rewind(&run->arena, mark);\n      }\n      GSHCPU_Result r = union_member_open(run, item, i, count, src, &member, &g);',
     '      (void)mark;\n      (void)in_arena;\n      GSHCPU_Result r = union_member_open(run, item, i, count, src, &member, &g);', _CG),
    ('cg-s-records-unsorted', 'src', 'src/render/sink.c',
     '  sort_records(rec, total);', '  (void)sort_records;', _CG),
    ('cg-s-sort-child-choice', 'src', 'src/render/sink.c',
     '      if (child + 1 < n && record_less(&v[child], &v[child + 1])) {\n        child++;\n      }', '      if (child + 1 < n && record_less(&v[child], &v[child + 1])) {\n        ;\n      }', _CG),
    ('cg-s-sort-end-child', 'src', 'src/render/sink.c',
     '      if (child + 1 < end && record_less(&v[child], &v[child + 1])) {\n        child++;\n      }', '      if (child + 1 < end && record_less(&v[child], &v[child + 1])) {\n        ;\n      }', _CG),
    ('cg-s-whole-pixel-ignored', 'src', 'src/render/sink.c',
     '    if (!(b->mark[rec[a].cell] & GSHCPU_GROUP_FULL)) {', '    if (1) {', _CG),
    ('cg-s-cell-rule-of-part', 'src', 'src/render/sink.c',
     'b->cell_rules[k] = b->group_rules[rec[t].part];', 'b->cell_rules[k] = b->group_rules[0];', _CG),
    ('cg-s-group-rule-of-member', 'src', 'src/render/sink.c',
     'b->group_rules[parts] = member.fill_rule == GSHGEOM_FILL_EVEN_ODD;', 'b->group_rules[parts] = item->fill_rule == GSHGEOM_FILL_EVEN_ODD;', _CG),
    ('cg-s-exact-status-ignored', 'src', 'src/render/sink.c',
     '      if (status != GSHCPU_EXACT_OK) {\n        if (status == GSHCPU_EXACT_IDENTITY) {\n          ex->failures++;\n        }\n        *refused = 1; /* a cell past',
     '      if (status != GSHCPU_EXACT_OK) {\n        if (status == GSHCPU_EXACT_IDENTITY) {\n          ex->failures++;\n        }\n        *refused = 0; /* a cell past', _CG),
    ('cg-s-exact-uncounted', 'src', 'src/render/sink.c',
     '      cs->counters->union_exact++;', '      cs->counters->union_exact += 0;', _CG),
    ('cg-s-pieces-uncounted', 'src', 'src/render/sink.c',
     '  cs->counters->union_pieces += cap.piece_n;', '  cs->counters->union_pieces += 0;', _CG),
    ('cg-s-groups-uncounted', 'src', 'src/render/sink.c',
     '    cs->counters->union_groups++;', '    cs->counters->union_groups += 0;', _CG),
    ('cg-s-general-uncounted', 'src', 'src/render/sink.c',
     '  cs->counters->union_general++;', '  cs->counters->union_general += 0;', _CG),
    ('cg-s-merge-offset', 'src', 'src/render/sink.c',
     'uint16_t * to = b->plane + (size_t)((y - W.y0) * Ww + (rg.x0 - W.x0));', 'uint16_t * to = b->plane + (size_t)((y - W.y0) * Ww);', _CG),
    ('cg-s-merge-skipped', 'src', 'src/render/sink.c',
     '    if (result == GSHSCENE_OK && !refused) {\n      /* The group\'s area, added into the plane', '    if (0) {\n      /* The group\'s area, added into the plane', _CG),
    ('cg-s-quantize-shift', 'src', 'src/render/sink.c',
     'const uint32_t level = (uint32_t)from[x] >> 5; /* CU2: quantized once, on the sum */', 'const uint32_t level = (uint32_t)from[x] >> 6; /* CU2: quantized once, on the sum */', _CG),
    ('cg-s-quantize-clamp', 'src', 'src/render/sink.c',
     'to[x] = (uint8_t)(level > 255u ? 255u : level);\n      }\n    }\n    run->work += (uint64_t)qw', 'to[x] = (uint8_t)level;\n      }\n    }\n    run->work += (uint64_t)qw', _CG),
    ('cg-s-quantize-row-offset', 'src', 'src/render/sink.c',
     'const uint16_t * from = ua.b.plane + (size_t)((y - W.y0) * Ww + (q.x0 - W.x0));\n      uint8_t * to = clip->coverage', 'const uint16_t * from = ua.b.plane + (size_t)((y - W.y0) * Ww);\n      uint8_t * to = clip->coverage', _CG),
    ('cg-s-refused-group-drawn', 'src', 'src/render/sink.c',
     '  if (result != GSHSCENE_OK || refused) {\n    union_area_free(run, ua);\n    return result != GSHSCENE_OK ? result : GSHSCENE_ERR_LIMIT; /* a member over a cap, or a pixel over a limit, refuses the union', '  if (result != GSHSCENE_OK) {\n    union_area_free(run, ua);\n    return result; /* a member over a cap, or a pixel over a limit, refuses the union', _CG),
    ('cg-s-unprovable-member-dropped', 'src', 'src/render/sink.c',
     'return refuse(run, item, chain, count);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  clip->rect = ua.pl.clip;', '(void)chain;\n    return GSHSCENE_OK;\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  clip->rect = ua.pl.clip;', _CG),
    ('cg-s-bill-groups-comparisons', 'src', 'src/render/sink.c',
     '        tests += n * (n - 1u) / 2u;', '        (void)n;', _CG),
    ('cg-s-bill-fast-too', 'src', 'src/render/sink.c',
     '      if (run->union_general) {\n        const uint64_t n', '      if (1) {\n        const uint64_t n', _CG),
    ('cg-s-tier-decision-always-general', 'src', 'src/render/sink.c',
     'run->union_general = !((run->cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint);', 'run->union_general = 1;', _CGU),
    ('cg-s-tier-decision-never-general', 'src', 'src/render/sink.c',
     'run->union_general = !((run->cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint);', 'run->union_general = 0;', _CGU),
    ('cg-s-tier-decision-ignores-switch', 'src', 'src/render/sink.c',
     'run->union_general = !((run->cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint);', 'run->union_general = !verdict.disjoint;', _CGU),
    ('cg-s-tier-decision-ignores-verdict', 'src', 'src/render/sink.c',
     'run->union_general = !((run->cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint);', 'run->union_general = !(run->cs->tiers & GSHCPU_RENDER_TIER_UNION);', _CGU),
    ('cg-s-dispatch', 'src', 'src/render/sink.c',
     'return run->union_general ? union_push_general(run, item, chain, count) : union_push_fast(run, item, chain, count);', 'return union_push_fast(run, item, chain, count);', _CGU),
    ('cg-s-caps-general-needs-switch', 'src', 'src/render/sink.c',
     '  if (cs->tiers & GSHCPU_RENDER_TIER_UNION_GENERAL) {\n    out->flags |= GSHSCENE_CAP_CLIP_UNION_GENERAL;', '  if (1) {\n    out->flags |= GSHSCENE_CAP_CLIP_UNION_GENERAL;', _CGU),
    ('cg-s-caps-union-needs-either-switch', 'src', 'src/render/sink.c',
     '  if (cs->tiers & (GSHCPU_RENDER_TIER_UNION | GSHCPU_RENDER_TIER_UNION_GENERAL)) {', '  if (cs->tiers & GSHCPU_RENDER_TIER_UNION) {', _CGU),
    ('cg-s-pieces-limit-read', 'src', 'src/render/sink.c',
     'cs->max_union_pieces = limits && limits->max_union_pieces ? limits->max_union_pieces : GSHCPU_LIMITS_DEFAULT_MAX_UNION_PIECES;', 'cs->max_union_pieces = GSHCPU_LIMITS_DEFAULT_MAX_UNION_PIECES;', _CG),
    ('cg-s-pieces-limit-ceiling', 'src', 'src/render/sink.c',
     'if (limits && limits->max_union_pieces > GSHCPU_LIMITS_MAX_UNION_PIECES) {', 'if (limits && limits->max_union_pieces > GSHCPU_LIMITS_MAX_UNION_PIECES + 1u) {', _CG),
    ('cg-s-measure-takes-fast-path', 'src', 'src/render/sink.c',
     '        !((cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint)) {', '        0) {', _CG),
    ('cg-s-measure-rewinds-per-member', 'src', 'src/render/sink.c',
     '          gshcpu_render_arena_rewind(arena, mark);\n          GSHCPU_Result mr = gshcpu_render_geometry(&member, maps, count - 1, &target->map, &arena->self, &cs->geometry, &mg);',
     '          (void)mark;\n          GSHCPU_Result mr = gshcpu_render_geometry(&member, maps, count - 1, &target->map, &arena->self, &cs->geometry, &mg);', _CG),
    ('cg-s-measure-whole-extent', 'src', 'src/render/sink.c',
     'union_plan(item, maps, count - 1, &target->map, region_of(target), whole_of(target),', 'union_plan(item, maps, count - 1, &target->map, region_of(target), region_of(target),', _CG),
    ('cg-s-measure-blocks-not-taken', 'src', 'src/render/sink.c',
     '      pr = union_alloc(cs, &pl, &arena->self, &blocks);', '      pr = GSHSCENE_ERR_LIMIT; memset(&blocks, 0, sizeof blocks);', _CG),
    ('cg-s-fast-measure-plane-kept', 'src', 'src/render/sink.c',
     '    arena->self.free_fn(arena->self.ctx, plane); /* the measure really took it from the sink\'s allocator */', '    /* the measure really took it from the sink\'s allocator */', ['testRender_limits']),
    ('cg-s-work-bound-members', 'src', 'src/render/sink.c',
     '  uint64_t members = (uint64_t)cs->union_members;', '  uint64_t members = 0;', ['testRender', 'testRender_limits']),
    ('cg-s-work-bound-exact', 'src', 'src/render/sink.c',
     '  if (!gcu_safe_add_u64(b, sweeps, &b) || !gcu_safe_add_u64(b, union_exact, &b)) {', '  (void)union_exact;\n  if (!gcu_safe_add_u64(b, sweeps, &b)) {', ['testRender', 'testRender_limits']),
    ('cg-x-parts-limit', 'src', 'src/render/exact.c',
     'max_parts > GSHCPU_EXACT_GROUP_PARTS', 'max_parts > 512u', _CG),
    ('cg-c-whole-mark-bit', 'src', 'src/render/cover.c',
     '*group_mark(gcap, rect.x0 + (j - 1), band + r) |= (uint8_t)GSHCPU_GROUP_FULL;', '*group_mark(gcap, rect.x0 + (j - 1), band + r) |= 0x40u;', _CG),
    ('cg-s-default-pieces-limit', 'src', 'src/render/sink.c',
     ' : GSHCPU_LIMITS_DEFAULT_MAX_UNION_PIECES;', ' : ((uint64_t)1 << 16);', _CG),
    ('cg-c-slot-not-the-criterion', 'src', 'src/render/cover.c',
     '          if (ex->slot[k] == GSHCPU_EXACT_NO_SLOT) {\n            continue; /* not a pixel the marks chose', '          if (0) {\n            continue; /* not a pixel the marks chose', _CG),
]


# Coverage by the centers of the pixels (shape-scene's list.h, AA1 to AA6; render.h, AC1 to AC6): the arithmetic of the rule (each row bound,
# the center, the tie, the sign, the total, the clamp, the fold, the mask, the cap and the rows it is counted over), and the sink's use of it
# (each tier an item must not take, the parts of a stroke, the pattern's cell, the hatch, the capability).  The rule's own reference is
# tools/gen-center-vectors.py, whose planted defects its selftest holds.  Run alone with PLANT_ONLY='^aa-'.
# Not planted, being equivalent in the design: the sum of the signs of the edges that straddle a center line (every contour is closed, so it
# is zero in every row, and the code does not keep it); a quadrilateral mask on the clip part of an aligned stroke in a pattern's copy (the
# aligned part is met with the cell already, and (a and Q) and not (c and Q) is a and Q and (not c or not Q), which Q makes (a and Q and not c));
# and the crossings of the cell's own four edges in the count of the cap (at most two a row, below any cap a test can set between).
AAC = ["testRender_antialias", "testRender_antialias_diff"]
AAP = ["testRender_antialias", "testRender_antialias_diff", "probe"]
PLANTS += [
    ('aa-center-row-first', 'src', RV, '  int64_t a = -floor_div(-(ylo - 32), 64);', '  int64_t a = floor_div(ylo - 32, 64);', AAC),
    ('aa-center-row-last', 'src', RV, '  int64_t b = -floor_div(-(yhi - 32), 64) - 1;', '  int64_t b = -floor_div(-(yhi - 32), 64);', AAC),
    ('aa-center-row-line', 'src', RV, '  const int64_t t = r * ONE_PIXEL + 32 - p.y;', '  const int64_t t = r * ONE_PIXEL - p.y;', AAC),
    ('aa-center-tie-inside', 'src', RV, '  const int64_t n = floor_div(rest == 0 ? m - 1 : m, 64);', '  const int64_t n = floor_div(m + 0 * rest, 64);', AAC),
    ('aa-center-tie-reversed', 'src', RV, '  const int64_t n = floor_div(rest == 0 ? m - 1 : m, 64);', '  const int64_t n = floor_div(rest == 0 ? m : m - 1, 64);', AAC),
    ('aa-center-x-origin', 'src', RV, '  const int64_t m = p.x + whole - 32;', '  const int64_t m = p.x + whole;', AAC),
    ('aa-center-floor-of-numerator', 'src', RV, '  const int64_t whole = floor_div(numerator, dy);', '  const int64_t whole = numerator / dy;', AAC),
    ('aa-center-sign', 'src', RV, '    q = t;\n    dir = -1;', '    q = t;\n    dir = 1;', AAC),
    ('aa-center-first-pixel', 'src', RV, '  int64_t j = n + 1 - b->x0;', '  int64_t j = n - b->x0;', AAC),
    ('aa-center-left-clamp', 'src', RV, '  j = j < 0 ? 0 : j;\n', '  j = j < 0 ? b->width : j;\n', AAC),
    ('aa-center-right-bound', 'src', RV, '  if (j < b->width) {\n    b->delta[(r - b->band) * b->stride + j] += dir;', '  if (j < b->width - 1) {\n    b->delta[(r - b->band) * b->stride + j] += dir;', AAC),
    ('aa-center-even-odd', 'src', RV, '        const int inside = rule == GSHGEOM_FILL_EVEN_ODD ? (ws & 1) != 0 : ws != 0;', '        (void)rule;\n        const int inside = ws != 0;', AAC),
    ('aa-center-nonzero-as-positive', 'src', RV, '        const int inside = rule == GSHGEOM_FILL_EVEN_ODD ? (ws & 1) != 0 : ws != 0;', '        const int inside = rule == GSHGEOM_FILL_EVEN_ODD ? (ws & 1) != 0 : ws > 0;', AAC),
    ('aa-center-mask-ignored', 'src', RV, '        line[j] = (uint8_t)(inside && (!mask || wc != 0) ? 255u : 0u);', '        line[j] = (uint8_t)(inside && (!mask || wc != 0 || 1) ? 255u : 0u);', AAP + ["testRender_pattern"]),
    ('aa-center-mask-not-deposited', 'src', RV, '    if (mask) {\n      center_edges(mask, band, band + rows, center_deposit_edge, &cut);\n    }', '', AAP),
    ('aa-center-cap-inclusive', 'src', RV, '  if (counted.total > max_deposits) {\n    return GSHCPU_ERR_LIMIT;\n  }\n  if (gshcpu_render_rect_empty(rect)) {', '  if (counted.total >= max_deposits) {\n    return GSHCPU_ERR_LIMIT;\n  }\n  if (gshcpu_render_rect_empty(rect)) {', AAC),
    ('aa-center-cap-uncounted', 'src', RV, '  if (counted.total > max_deposits) {\n    return GSHCPU_ERR_LIMIT;\n  }\n  if (gshcpu_render_rect_empty(rect)) {', '  (void)max_deposits;\n  if (gshcpu_render_rect_empty(rect)) {', AAC),
    ('aa-center-count-rows-of-the-rect', 'src', RV, '        center_edges(paths[i], walk.y0, walk.y1, center_count_edge, &counted);', '        center_edges(paths[i], rect.y0, rect.y1, center_count_edge, &counted);', AAC),
    ('aa-center-paths-skipped-in-count', 'src', RV, '        center_edges(paths[i], walk.y0, walk.y1, center_count_edge, &counted);', '        (void)0;', AAC),
    ('aa-center-band-clear', 'src', RV, '    memset(acc, 0, 2 * n * sizeof *acc);\n    for (size_t i = 0; i < count; i++) {\n      if (paths[i]) {\n        center_edges(paths[i], band, band + rows, center_deposit_edge, &shape);', '    for (size_t i = 0; i < count; i++) {\n      if (paths[i]) {\n        center_edges(paths[i], band, band + rows, center_deposit_edge, &shape);', AAC),
    ('aa-center-closing-edge', 'src', RV, '      const Point p = point_of(points[sub.first + (k ? k - 1 : sub.count - 1)]);', '      const Point p = point_of(points[sub.first + (k ? k - 1 : k)]);', AAC),
    ('aa-fill-hint-ignored', 'src', RS, '      if (item->kind == GSHSCENE_ITEM_FILL && item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE) {\n        /* AA2: whole or nothing', '      if (0) {\n        /* AA2: whole or nothing', AAC),
    ('aa-fill-fast-tier', 'src', RS, '      if (item->kind == GSHSCENE_ITEM_FILL && item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE) {\n        /* AA2: whole or nothing', '      if (item->kind == GSHSCENE_ITEM_FILL && item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE && !run->cs->fast_fill) {\n        /* AA2: whole or nothing', AAC),
    ('aa-fill-work-uncounted', 'src', RS, '        run->work += cells + (uint64_t)(rect->x1 - rect->x0) * (uint64_t)(rect->y1 - rect->y0);\n      } else if (run->cs->fast_fill', '        run->work += (uint64_t)(rect->x1 - rect->x0) * (uint64_t)(rect->y1 - rect->y0);\n      } else if (run->cs->fast_fill', AAC),
    ('aa-fill-work-pixels', 'src', RS, '        run->work += cells + (uint64_t)(rect->x1 - rect->x0) * (uint64_t)(rect->y1 - rect->y0);\n      } else if (run->cs->fast_fill', '        run->work += cells;\n      } else if (run->cs->fast_fill', AAC),
    ('aa-fill-acc-dirty', 'src', RS, '        r = gshcpu_render_cover_center(paths, g.count, item->fill_rule, NULL, *rect, walk, out, run->acc, run->cs->max_deposits, &cells);\n        run->acc_zero = 0;', '        r = gshcpu_render_cover_center(paths, g.count, item->fill_rule, NULL, *rect, walk, out, run->acc, run->cs->max_deposits, &cells);', AAP),
    ('aa-fill-rule', 'src', RS, '        r = gshcpu_render_cover_center(paths, g.count, item->fill_rule, NULL, *rect, walk, out,', '        r = gshcpu_render_cover_center(paths, g.count, GSHGEOM_FILL_NONZERO, NULL, *rect, walk, out,', AAC),
    ('aa-fill-walk-is-rect', 'src', RS, '        r = gshcpu_render_cover_center(paths, g.count, item->fill_rule, NULL, *rect, walk, out,', '        r = gshcpu_render_cover_center(paths, g.count, item->fill_rule, NULL, *rect, *rect, out,', AAC),
    ('aa-stroke-hint-ignored', 'src', RS, '  const int center = item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE;\n  if (solid && !center && cs->fast', '  const int center = 0;\n  if (solid && !center && cs->fast', AAC),
    ('aa-stroke-fast-tier', 'src', RS, '  if (solid && !center && cs->fast && (cs->tiers', '  if (solid && cs->fast && (cs->tiers', AAC),
    ('aa-stroke-fast-gradient-tier', 'src', RS, '  if (!solid && !center && item->paint.kind != GSHSCENE_PAINT_PATTERN && cs->fast_fill', '  if (!solid && item->paint.kind != GSHSCENE_PAINT_PATTERN && cs->fast_fill', AAC),
    ('aa-stroke-sparse-tier', 'src', RS, '  if (solid && !center && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) && gshcpu_render_stroke_sparse_ok(&s, cs->max_deposits, exact)) {', '  if (solid && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) && gshcpu_render_stroke_sparse_ok(&s, cs->max_deposits, exact)) {', AAC),
    ('aa-stroke-buffers-center', 'src', RS, 'run->cov_clip, run->cov_result, center, NULL};\n  r = gshcpu_render_stroke_cover(', 'run->cov_clip, run->cov_result, 0, NULL};\n  r = gshcpu_render_stroke_cover(', AAC),
    ('aa-pat-stroke-buffers-center', 'src', RS, 'run->cov_clip, run->cov_result, center, cell};', 'run->cov_clip, run->cov_result, 0, cell};', AAC + ["testRender_pattern"]),
    ('aa-pat-stroke-mask', 'src', RS, 'run->cov_clip, run->cov_result, center, cell};', 'run->cov_clip, run->cov_result, center, NULL};', AAC),
    ('aa-pat-stroke-hint-ignored', 'src', RS, '  const int center = item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE;\n  GSHGEOM_Flat_Path * cell = NULL;', '  const int center = 0;\n  GSHGEOM_Flat_Path * cell = NULL;', AAC),
    ('aa-pat-fill-hint-ignored', 'src', RS, '  const int center = item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE;\n  const GSHGEOM_Flat_Path * mask = NULL;', '  const int center = 0;\n  const GSHGEOM_Flat_Path * mask = NULL;', AAC),
    ('aa-pat-fill-mask', 'src', RS, '      mask = plane;\n      clipped = run->item_alloc->calloc_fn(', '      clipped = run->item_alloc->calloc_fn(', AAC),
    ('aa-pat-fill-clipped-anyway', 'src', RS, '    if (center) {\n      /* AA2: the item\'s own outline, not cut', '    if (0) {\n      /* AA2: the item\'s own outline, not cut', AAC),
    ('aa-stroke-joint-exact', 'src', RSK, '  if (center) {\n    ex = NULL; /* AA2: a center is decided', '  if (0) {\n    ex = NULL; /* AA2: a center is decided', AAC),
    ('aa-stroke-center-part', 'src', RSK, '    GSHCPU_Result r = center\n        ? gshcpu_render_cover_center(', '    GSHCPU_Result r = 0\n        ? gshcpu_render_cover_center(', AAC),
    ('aa-stroke-mask-on-aligned', 'src', RSK, 'p == GSHCPU_STROKE_CLIP ? NULL : buffers->mask,\n              moved(part->rect', 'p != GSHCPU_STROKE_BODY ? NULL : buffers->mask,\n              moved(part->rect', AAC),
    ('aa-stroke-cell-shift-x', 'src', RSK, '    const int64_t x = (int64_t)quad[k].x - 64 * s->shift_x;\n    const int64_t y = (int64_t)quad[k].y - 64 * s->shift_y;\n    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {\n      return GSHCPU_ERR_LIMIT;\n    }\n    frame[k].x = (int32_t)x;\n    frame[k].y = (int32_t)y;\n  }\n  const GSHGEOM_Flat_Subpath sub',
     '    const int64_t x = (int64_t)quad[k].x;\n    const int64_t y = (int64_t)quad[k].y - 64 * s->shift_y;\n    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {\n      return GSHCPU_ERR_LIMIT;\n    }\n    frame[k].x = (int32_t)x;\n    frame[k].y = (int32_t)y;\n  }\n  const GSHGEOM_Flat_Subpath sub', AAC + ["testRender_pattern"]),
    ('aa-hatch-mode', 'src', RS, '  *out = *item;\n  out->kind = GSHSCENE_ITEM_STROKE;\n  out->style = item->paint.resource;', '  *out = *item;\n  out->antialias = 0;\n  out->kind = GSHSCENE_ITEM_STROKE;\n  out->style = item->paint.resource;', AAC),
    ('aa-cap', 'src', RS, '  out->flags |= GSHSCENE_CAP_ANTIALIAS_NONE;', '', AAC + ["testRender"]),
]


# The hand-written plants no test is seen to catch, with why.  The gate prints each as "known" and does not fail on it; it fails
# when one of them IS caught (take it out of this table then) and when a name here is not in the plant table.  The first is
# equivalent in the design; the others are not shown to be, and each names the case a test would have to build.
HXS = ['testRender_hatch', 'testRender_limits', 'testRender', 'testRender_conformance', 'testCoverage_limits', 'probe']
PLANTS += [
    ('hx-is-hatch', 'src', 'src/render/stroke.c', 'const int is_hatch = item->style && gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_HATCH;', 'const int is_hatch = 0;', HXS),
    ('hx-pen', 'src', 'src/render/stroke.c', 'const GSHSCENE_Resource * pen = is_hatch ? hatch.style : item->style;', 'const GSHSCENE_Resource * pen = item->style;', HXS),
    ('hx-key-hatch', 'src', 'src/render/stroke.c', '    key.hatch_id = gshscene_resource_id(item->style);\n', '', HXS),
    ('hx-key-rule', 'src', 'src/render/stroke.c', '    key.rule = (uint64_t)item->fill_rule;\n', '', HXS),
    ('hx-key-revision', 'src', 'src/render/stroke.c', '    key.hatch_revision = GSHGEOM_HATCH_REVISION;\n', '', HXS),
    ('hx-geom-unit', 'src', 'src/render/stroke.c', '      geom.angle_unit = hatch.angle_unit;\n', '', HXS),
    ('hx-geom-nesting', 'src', 'src/render/stroke.c', '      geom.nesting = hatch.nesting;\n', '', HXS),
    ('hx-geom-map', 'src', 'src/render/stroke.c', '      geom.map = hatch.map;\n', '', HXS),
    ('hx-geom-origin-x', 'src', 'src/render/stroke.c', '        families[i].origin_x = hatch.families[i].origin_x;\n', '        families[i].origin_x = 0.0;\n', HXS),
    ('hx-geom-origin-y', 'src', 'src/render/stroke.c', '        families[i].origin_y = hatch.families[i].origin_y;\n', '        families[i].origin_y = 0.0;\n', HXS),
    ('hx-geom-along', 'src', 'src/render/stroke.c', '        families[i].offset_along = hatch.families[i].offset_along;\n', '        families[i].offset_along = 0.0;\n', HXS),
    ('hx-geom-angle', 'src', 'src/render/stroke.c', '        families[i].angle = hatch.families[i].angle;\n', '        families[i].angle = 0.0;\n', HXS),
    ('hx-geom-dashes', 'src', 'src/render/stroke.c', '        families[i].dashes = hatch.families[i].dashes;\n', '        families[i].dashes = NULL;\n', HXS),
    ('hx-rule', 'src', 'src/render/stroke.c', 'gshgeom_hatch_pieces(path, item->fill_rule, &geom,', 'gshgeom_hatch_pieces(path, GSHGEOM_FILL_NONZERO, &geom,', HXS),
    ('hx-max-lines', 'src', 'src/render/stroke.c', '&canon, max_lines, alloc, limits, &pieces);', '&canon, 0, alloc, limits, &pieces);', HXS),
    ('hx-lines-made', 'src', 'src/render/stroke.c', '        lines_made = gshgeom_hatch_pieces_lines(pieces);', '        lines_made = 0;', HXS),
    ('hx-hit-lines', 'src', 'src/render/stroke.c', 'if (is_hatch && s.meta.lines > max_lines) {', 'if (0) {', HXS),
    ('hx-hit-lines-bound', 'src', 'src/render/stroke.c', 'if (is_hatch && s.meta.lines > max_lines) {', 'if (is_hatch && s.meta.lines >= max_lines) {', HXS),
    ('hx-hatch-item-kind', 'src', 'src/render/sink.c', '  if (item->kind != GSHSCENE_ITEM_FILL || item->paint.kind != GSHSCENE_PAINT_HATCH || !item->paint.resource ||', '  if (item->paint.kind != GSHSCENE_PAINT_HATCH || !item->paint.resource ||', HXS),
    ('hx-hatch-item-style', 'src', 'src/render/sink.c', '  out->style = item->paint.resource;\n  out->paint = h.ink;', '  out->paint = h.ink;', HXS),
    ('hx-hatch-item-ink', 'src', 'src/render/sink.c', '  out->paint = h.ink;\n  out->paint.rgba[3]', '  out->paint = item->paint;\n  out->paint.rgba[3]', HXS),
    ('hx-hatch-item-alpha', 'src', 'src/render/sink.c', '  out->paint.rgba[3] = h.ink.rgba[3] * item->paint.rgba[3];', '  out->paint.rgba[3] = h.ink.rgba[3];', HXS),
    ('hx-hatch-item-alpha-ink', 'src', 'src/render/sink.c', '  out->paint.rgba[3] = h.ink.rgba[3] * item->paint.rgba[3];', '  out->paint.rgba[3] = item->paint.rgba[3];', HXS),
    ('hx-hatch-item-stroke', 'src', 'src/render/sink.c', '  out->kind = GSHSCENE_ITEM_STROKE;\n  out->style = item->paint.resource;', '  out->style = item->paint.resource;', HXS),
    ('hx-cap', 'src', 'src/render/sink.c', '  out->flags |= GSHSCENE_CAP_HATCH;', '', HXS),
    ('hx-limit-default', 'src', 'src/render/sink.c', 'cs->max_hatch_lines = limits && limits->max_hatch_lines ? limits->max_hatch_lines : GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES;', 'cs->max_hatch_lines = GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES;', HXS),
    ('hx-limit-zero', 'src', 'src/render/sink.c', 'cs->max_hatch_lines = limits && limits->max_hatch_lines ? limits->max_hatch_lines : GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES;', 'cs->max_hatch_lines = limits ? limits->max_hatch_lines : GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES;', HXS),
    ('hx-work-bound', 'src', 'src/render/sink.c', '  if (!gcu_safe_mul_u64(cs->max_hatch_lines, (uint64_t)cs->elements, &trim) || !gcu_safe_add_u64(b, trim, &b)) {', '  if (!gcu_safe_mul_u64(0, (uint64_t)cs->elements, &trim) || !gcu_safe_add_u64(b, trim, &b)) {', HXS),
    ('hx-work-lines', 'src', 'src/render/sink.c', 'if (!gcu_safe_mul_u64(s->meta.lines, elements ? elements : 1u, &trim) || !gcu_safe_add_u64(work, trim, &work)) {', 'if (!gcu_safe_mul_u64(0, elements ? elements : 1u, &trim) || !gcu_safe_add_u64(work, trim, &work)) {', HXS),
    ('hx-work-elements', 'src', 'src/render/sink.c', 'if (!gcu_safe_mul_u64(s->meta.lines, elements ? elements : 1u, &trim) || !gcu_safe_add_u64(work, trim, &work)) {', 'if (!gcu_safe_mul_u64(s->meta.lines, 1u, &trim) || !gcu_safe_add_u64(work, trim, &work)) {', HXS),
    ('hx-measure-strokes', 'src', 'src/render/sink.c', "      *strokes = 1; /* HX13: drawn as a stroke, so the exact step's counts and pool too */\n", '', HXS),
    ('hx-measure-wide', 'src', 'src/render/sink.c', '        *wide = *wide || out_of_range(&hatched.paint);\n', '', HXS),
    ('hx-unit-hatch', 'src', 'src/render/sink.c', '      if (hatch_fill_item(item, &hatched)) {\n        /* HX13: drawn as a stroke of its lines', '      if (0 && hatch_fill_item(item, &hatched)) {\n        /* HX13: drawn as a stroke of its lines', HXS),
    ('hx-unit-pattern', 'src', 'src/render/sink.c', '        return cur_pat(run) ? pat_stroke(run, &hatched, chain, count) : stroke(run, &hatched, chain, count);', '        return stroke(run, &hatched, chain, count);', HXS),
    ('hx-notice', 'src', 'src/render/sink.c', '    if (is_hatch ? gshscene_paint_notice(item, maps, count - 1, &target->map)\n                 : gshscene_stroke_notice(item, maps, count - 1, &target->map)) {', '    if (gshscene_stroke_notice(item, maps, count - 1, &target->map)) {', HXS),
    ('hx-measure-item', 'src', 'src/render/sink.c', '    if (is_hatch) {\n      item = &hatched;\n    }\n', '', HXS),
    ('hx-work-hatch-kind', 'src', 'src/render/sink.c', '  if (item->style && gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_HATCH) {\n    /* HX12: each line', '  if (0) {\n    /* HX12: each line', HXS),
    ('hx-cache-hash-hatch', 'src', 'src/render/stroke_cache.c', '  h = (h ^ k->hatch_id) * UINT64_C(0x100000001B3);\n', '', HXS),
    ('hx-cache-hash-rule', 'src', 'src/render/stroke_cache.c', '  h = (h ^ k->rule) * UINT64_C(0x100000001B3);\n', '', HXS),
    ('hx-cache-eq-revision', 'src', 'src/render/stroke_cache.c', '      a->hatch_revision == b->hatch_revision && a->hatch_id == b->hatch_id && a->rule == b->rule;', '      a->hatch_id == b->hatch_id && a->rule == b->rule;', HXS),
    ('hx-cache-eq-hatch', 'src', 'src/render/stroke_cache.c', '      a->hatch_revision == b->hatch_revision && a->hatch_id == b->hatch_id && a->rule == b->rule;', '      a->hatch_revision == b->hatch_revision && a->rule == b->rule;', HXS),
    ('hx-cache-eq-rule', 'src', 'src/render/stroke_cache.c', '      a->hatch_revision == b->hatch_revision && a->hatch_id == b->hatch_id && a->rule == b->rule;', '      a->hatch_revision == b->hatch_revision && a->hatch_id == b->hatch_id;', HXS),
    ('hx-cache-evict-hatch', 'src', 'src/render/stroke_cache.c', '(e->key.hatch_id != 0 && e->key.hatch_id == id))) {', '0)) {', HXS),
    ('hx-core-default', 'src', 'src/core/core.c', '  limits->max_hatch_lines = 0; /* the default, GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES */', '  limits->max_hatch_lines = 1;', HXS),
]

# --- F3: the magnitude fold (fold.h), the cut from the upper end in every sweep, and the area a pattern's copies add (PC4) ----------
# One plant for each rule and branch of the shared fold, of the edge that is cut from its upper end and deposits negated when it goes up, and
# of the remainder a copy carries to the pattern's sum.  `srct`: the tests include fold.h themselves, so they are built against the planted tree.
FD = "src/core/fold.h"
F3H = ["testCoverage", "testRender", "testRender_vectors", "testRender_pattern", "testRender_pattern_rules", "testRender_clip_union",
       "testCoverage_limits", "testRender_exact", "probe"]
F3G = ["testCoverage", "testRender", "testRender_vectors", "testCoverage_limits", "testRender_exact", "testRender_clip_union",
       "testRender_stroke", "testRender_pattern", "probe"]
F3P = ["testRender_pattern_rules", "testRender_pattern", "testRender_pattern_period", "testRender", "testRender_vectors"]
PLANTS += [
    ("f3-pixel-bits", "srct", FD, "#define GSHCPU_FOLD_PIXEL_BITS 6", "#define GSHCPU_FOLD_PIXEL_BITS 5", F3H),
    ("f3-area-shift-no-plus-one", "srct", FD, "#define GSHCPU_FOLD_AREA_SHIFT (2 * GSHCPU_FOLD_PIXEL_BITS + 1 - 8)", "#define GSHCPU_FOLD_AREA_SHIFT (2 * GSHCPU_FOLD_PIXEL_BITS - 8)", F3H),
    ("f3-area-shift-eight-bits", "srct", FD, "#define GSHCPU_FOLD_AREA_SHIFT (2 * GSHCPU_FOLD_PIXEL_BITS + 1 - 8)", "#define GSHCPU_FOLD_AREA_SHIFT (2 * GSHCPU_FOLD_PIXEL_BITS + 1 - 7)", F3H),
    ("f3-whole-pixel", "srct", FD, "#define GSHCPU_FOLD_WHOLE (INT64_C(2) << (2 * GSHCPU_FOLD_PIXEL_BITS))", "#define GSHCPU_FOLD_WHOLE (INT64_C(1) << (2 * GSHCPU_FOLD_PIXEL_BITS))", F3H),
    ("f3-fold-sign-kept", "srct", FD, "  uint64_t m = (uint64_t)(area < 0 ? -area : area);", "  uint64_t m = (uint64_t)area;", F3H),
    ("f3-fold-floor-after-sign", "srct", FD, "  const uint32_t level = gshcpu_fold_area(area, rule) >> GSHCPU_FOLD_AREA_SHIFT;\n  return", "  const uint32_t level = (gshcpu_fold_area(area, rule) + (area < 0 ? 31u : 0u)) >> GSHCPU_FOLD_AREA_SHIFT;\n  return", F3H),
    ("f3-fold-rule-swapped", "srct", FD, "  if (rule == GSHGEOM_FILL_EVEN_ODD) {\n    m &= 2u", "  if (rule != GSHGEOM_FILL_EVEN_ODD) {\n    m &= 2u", F3H),
    ("f3-even-odd-period-one-pixel", "srct", FD, "    m &= 2u * (uint64_t)GSHCPU_FOLD_WHOLE - 1u;", "    m &= (uint64_t)GSHCPU_FOLD_WHOLE - 1u;", F3H),
    ("f3-even-odd-period-four-pixels", "srct", FD, "    m &= 2u * (uint64_t)GSHCPU_FOLD_WHOLE - 1u;", "    m &= 4u * (uint64_t)GSHCPU_FOLD_WHOLE - 1u;", F3H),
    ("f3-even-odd-ramp-dropped", "srct", FD, "    if (m > (uint64_t)GSHCPU_FOLD_WHOLE) {\n      m = 2u * (uint64_t)GSHCPU_FOLD_WHOLE - m;\n    }", "", F3H),
    ("f3-even-odd-ramp-top", "srct", FD, "      m = 2u * (uint64_t)GSHCPU_FOLD_WHOLE - m;", "      m = 2u * (uint64_t)GSHCPU_FOLD_WHOLE - m + 1u;", F3H),
    ("f3-nonzero-cap-never", "srct", FD, "  } else if (m > (uint64_t)GSHCPU_FOLD_WHOLE) {", "  } else if (m > 2u * (uint64_t)GSHCPU_FOLD_WHOLE) {", F3H),
    ("f3-byte-whole-is-zero", "srct", FD, "  return (uint8_t)(level >= 256u ? 255u : level);", "  return (uint8_t)(level > 256u ? 255u : level);", F3H),
    ("f3-byte-whole-is-254", "srct", FD, "  return (uint8_t)(level >= 256u ? 255u : level);", "  return (uint8_t)(level >= 256u ? 254u : level);", F3H),
    ("f3-split-remainder-masked", "srct", FD, "  *rem = (uint8_t)(whole - (byte << GSHCPU_FOLD_AREA_SHIFT));", "  *rem = (uint8_t)(whole & 31u);", F3H),
    ("f3-split-remainder-zero", "srct", FD, "  *rem = (uint8_t)(whole - (byte << GSHCPU_FOLD_AREA_SHIFT));", "  *rem = 0;", F3H),
    # the scan converter of coverage.c: an edge is cut from its upper end, an upward one deposits the negation
    ("f3-cv-up-edge-not-negated", "src", CV, "    raster->flip = true;\n    edge_down(raster, to, from);", "    raster->flip = false;\n    edge_down(raster, to, from);", K),
    ("f3-cv-flip-not-reset", "src", CV, "    edge_down(raster, to, from);\n    raster->flip = false;\n    return;", "    edge_down(raster, to, from);\n    return;", K),
    ("f3-cv-flip-ignored", "src", CV, "  if (raster->flip) {\n    dy = -dy;", "  if (0) {\n    dy = -dy;", K),
    ("f3-cv-pen-not-moved", "src", CV, "  raster->current = to;\n  if (from.y > to.y) {", "  if (from.y > to.y) {", K),
    ("f3-cv-row-boundary-above", "src", CV, "    const int32_t boundary = (ey + 1) * ONE_PIXEL;", "    const int32_t boundary = ey * ONE_PIXEL;", K),
    ("f3-cv-row-cut-from-end", "src", CV, "    const int32_t at_x = (int32_t)(from.x\n        + round_div((int64_t)(boundary - from.y) * dx, dy));", "    const int32_t at_x = (int32_t)(to.x\n        + round_div((int64_t)(boundary - to.y) * dx, dy));", K),
    # the dense cover of render/cover.c
    ("f3-rv-up-edge-dropped", "src", RV, "  if (from.y > to.y) {\n    Point t = from;", "  if (0 && from.y > to.y) {\n    Point t = from;", F3G),
    ("f3-rv-up-piece-not-reversed", "src", RV, "  if (c->rev) {\n    piece(c, ex, ey, b, a);", "  if (0) {\n    piece(c, ex, ey, b, a);", F3G),
    ("f3-rv-up-edge-left-deposit", "src", RV, "    fold_deposit(c, ey, c->rev ? a.y - b.y : b.y - a.y);", "    fold_deposit(c, ey, b.y - a.y);", F3G),
    ("f3-rv-row-boundary-above", "src", RV, "    Point b = ey == ey2 ? to : row_cut(from, to, (ey + 1) * ONE_PIXEL);", "    Point b = ey == ey2 ? to : row_cut(from, to, ey * ONE_PIXEL);", F3G),
    ("f3-rv-row-cut-from-end", "src", RV, "    Point a = ey == ey1 ? from : row_cut(from, to, ey * ONE_PIXEL);", "    Point a = ey == ey1 ? from : row_cut(to, from, ey * ONE_PIXEL);", F3G),
    ("f3-rv-split-remainder-lost", "src", RV, "          line[j - 1] = gshcpu_fold_split(accumulated - ar[j], rule, &rline[j - 1]);", "          line[j - 1] = gshcpu_fold(accumulated - ar[j], rule);\n          rline[j - 1] = 0;", F3P),
    ("f3-rv-split-row-offset", "src", RV, "        uint8_t * rline = rem + (size_t)((band - rect.y0 + r) * width);", "        uint8_t * rline = rem + (size_t)(r * width);", F3P),
    ("f3-rv-split-branch-off", "src", RV, "      if (rem) {\n        uint8_t * rline", "      if (0 && rem) {\n        uint8_t * rline", F3P),
    # the pattern's sum (PC4)
    ("f3-rs-pat-split-not-used", "src", RS, "        r = rem ? gshcpu_render_cover_split(", "        r = 0 ? gshcpu_render_cover_split(", F3P),
    ("f3-rs-pat-remainder-not-passed", "src", RS, "run->coverage, L->rem, have);", "run->coverage, NULL, have);", F3P),
    ("f3-rs-pat-remainder-not-added", "src", RS, "      const uint32_t m = ((uint32_t)cov[at_px] << GSHCPU_FOLD_AREA_SHIFT) + (rem ? rem[at_px] : 0u);", "      const uint32_t m = ((uint32_t)cov[at_px] << GSHCPU_FOLD_AREA_SHIFT);", F3P),
    ("f3-rs-pat-byte-shift", "src", RS, "      const uint32_t m = ((uint32_t)cov[at_px] << GSHCPU_FOLD_AREA_SHIFT) + (rem ? rem[at_px] : 0u);", "      const uint32_t m = ((uint32_t)cov[at_px] << (GSHCPU_FOLD_AREA_SHIFT - 1)) + (rem ? rem[at_px] : 0u);", F3P),
    ("f3-rs-pat-level-whole", "src", RS, "  return level > 255u ? 255u : level;", "  return level > 256u ? 255u : level;", F3P),
    ("f3-rs-pat-level-shift", "src", RS, "  const uint32_t level = a >> GSHCPU_FOLD_AREA_SHIFT;\n  return level", "  const uint32_t level = a >> (GSHCPU_FOLD_AREA_SHIFT - 1);\n  return level", F3P),
    ("f3-rs-pat-weighed-keeps-remainder", "src", RS, "    *rem = 0;", "    *rem = *rem;", F3P),
    ("f3-rs-pat-clip-remainder-lost", "src", RS, "      const uint32_t m = ((uint32_t)run->coverage[at_px] << GSHCPU_FOLD_AREA_SHIFT) + L->rem[at_px];", "      const uint32_t m = ((uint32_t)run->coverage[at_px] << GSHCPU_FOLD_AREA_SHIFT);", F3P),
    ("f3-rs-pat-clip-first-copy-lost", "src", RS, "        L->cnt[p] = 1;\n        L->sum8[p] = m;", "        L->cnt[p] = 1;\n        L->sum8[p] = 0;", F3P),
    ("f3-rs-pat-clip-count-not-cleared", "src", RS, "(uint8_t)pat_level8(L->sum8[p]);\n          L->cnt[p] = 0;", "(uint8_t)pat_level8(L->sum8[p]);", F3P),
    ("f3-rs-pat-clip-untouched-written", "src", RS, "        if (L->cnt[p] != 0) {\n          clip->coverage", "        if (1) {\n          clip->coverage", F3P),
]

CU21T = ["testRender_pattern_union", "testRender_pattern_rules", "testRender_pattern_vectors", "testRender_clip_union", "testRender_clip_union_general"]
PLANTS += [
    # --- CU21: a union clip in a pattern's tile (the story's CT1 to CT7) ---
    ('cu21-cell-cut-lost', 'src', RS,
     '  if (r != GSHCPU_OK || !src->quad) {\n    return r;\n  }\n  m->cut =',
     '  if (r != GSHCPU_OK || 1) {\n    return r;\n  }\n  m->cut =', CU21T),
    ('cu21-cut-count-is-the-uncut-count', 'src', RS,
     '  m->count = m->cut_n;\n  return GSHCPU_OK;',
     '  m->count = m->g.count;\n  return GSHCPU_OK;', CU21T),
    ('cu21-cut-pieces-not-swept', 'src', RS,
     '  m->paths = (const GSHGEOM_Flat_Path * const *)(void *)m->cut;\n  m->count = 0;',
     '  m->count = 0;', CU21T),
    ('cu21-tile-union-not-routed', 'src', RS,
     '  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {\n    return pat_cover_union(',
     '  if (0) {\n    return pat_cover_union(', CU21T),
    ('cu21-tile-byte-not-held', 'src', RS,
     '      const uint32_t byte = level > 255u ? 255u : level;\n      const size_t at_px',
     '      const uint32_t byte = level;\n      const size_t at_px', CU21T),
    ('cu21-tile-remainder-lost', 'src', RS,
     '        rem[at_px] = (uint8_t)(area - (byte << GSHCPU_FOLD_AREA_SHIFT));',
     '        rem[at_px] = 0;', CU21T),
    ('cu21-tile-remainder-of-the-byte', 'src', RS,
     '        rem[at_px] = (uint8_t)(area - (byte << GSHCPU_FOLD_AREA_SHIFT));',
     '        rem[at_px] = (uint8_t)(area & 31u);', CU21T),
    ('cu21-tile-area-not-held-at-the-pixel', 'src', RS,
     '      const uint32_t area = from[x] > 8192u ? 8192u : from[x]; /* a pixel holds no more than itself */',
     '      const uint32_t area = from[x];', CU21T),
    ('cu21-tile-whole-is-the-reach', 'src', RS,
     '  const GSHSCENE_Result r = union_area(run, item, count, &src, *rc, *wc, &ua);',
     '  const GSHSCENE_Result r = union_area(run, item, count, &src, *rc, *rc, &ua);', CU21T),
    ('cu21-tile-members-under-the-target', 'src', RS,
     '  const Union_Src src = {maps, &L->base, c->quad};',
     '  const Union_Src src = {maps, &run->job->target.map, c->quad};', CU21T),
    ('cu21-tile-members-under-the-runs-maps', 'src', RS,
     '  const Union_Src src = {maps, &L->base, c->quad};',
     '  const Union_Src src = {maps == NULL ? run->maps : run->maps, &L->base, c->quad};', CU21T),
    ('cu21-tile-rect-not-the-plans', 'src', RS,
     '  *rc = ua.pl.clip;\n  *wc = ua.pl.wide;\n  if (!ua.live) {',
     '  *wc = ua.pl.wide;\n  if (!ua.live) {', CU21T),
    ('cu21-tile-whole-not-the-plans', 'src', RS,
     '  *rc = ua.pl.clip;\n  *wc = ua.pl.wide;\n  if (!ua.live) {',
     '  *rc = ua.pl.clip;\n  if (!ua.live) {', CU21T),
    ('cu21-tile-have-lost', 'src', RS,
     '  union_area_free(run, &ua);\n  *have = 1;\n  return GSHSCENE_OK;',
     '  union_area_free(run, &ua);\n  return GSHSCENE_OK;', CU21T),
    ('cu21-tile-plane-not-freed', 'src', RS,
     '  run->work += (uint64_t)qw * (uint64_t)(q.y1 - q.y0);\n  union_area_free(run, &ua);\n  *have = 1;',
     '  run->work += (uint64_t)qw * (uint64_t)(q.y1 - q.y0);\n  *have = 1;', CU21T),
    ('cu21-tile-quantized-twice', 'src', RS,
     '      const uint32_t level = area >> GSHCPU_FOLD_AREA_SHIFT;\n      const uint32_t byte',
     '      const uint32_t level = (area >> GSHCPU_FOLD_AREA_SHIFT) - (area >> (GSHCPU_FOLD_AREA_SHIFT + 6));\n      const uint32_t byte', CU21T),
    ('cu21-tile-arena-not-rewound', 'src', RS,
     '  *have = 0;\n  if (run->item_alloc == &run->arena.self) {\n    gshcpu_render_arena_rewind(&run->arena, L->arena_mark);\n  }\n  const Union_Src src',
     '  *have = 0;\n  const Union_Src src', CU21T),
    ('cu21-tile-facts-union-lost', 'src', RS,
     '      f->union_clip = 1;',
     '      f->union_clip = 0;', CU21T),
    ('cu21-tile-period-eligible', 'src', RS,
     '  if (facts->pattern || facts->masked || facts->union_clip) {',
     '  if (facts->pattern || facts->masked) {', CU21T),
    ('cu21-tile-facts-skips-instances-union', 'src', RS,
     '    } else if (item->kind == GSHSCENE_ITEM_CLIP_PUSH && gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {\n      f->union_clip = 1;',
     '    } else if (item->kind == GSHSCENE_ITEM_CLIP_PUSH && gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_PLANE) {\n      f->union_clip = 1;', CU21T),
    ('cu21-bill-repeated-for-each-walk', 'src', RS,
     '  return L && L->single && L->walks > 1;',
     '  (void)L;\n  return 0;', CU21T),
    ('cu21-bill-first-walk-skipped', 'src', RS,
     '  return L && L->single && L->walks > 1;',
     '  return L && L->single && L->walks > 0;', CU21T),
    ('cu21-bill-walks-not-counted', 'src', RS,
     '  L->single = 1;\n  L->walks++;',
     '  L->single = 1;', CU21T),
    ('cu21-bill-walks-not-reset', 'src', RS,
     '  L->single = 0;\n  L->walks = 0;\n  L->copies = 0;',
     '  L->single = 0;\n  L->copies = 0;', CU21T),
    ('cu21-bill-outer-level-only', 'src', RS,
     '  const Pat_Level * L = cur_pat(run);\n  return L && L->single',
     '  const Pat_Level * L = run->masked && pr(run)->pat_depth > 0 ? pat_level(run, 0) : NULL;\n  return L && L->single', CU21T),
    ('cu21-bill-skipped-for-every-tile-union', 'src', RS,
     '    if (!tile_repeat_walk(run) &&\n        gshscene_clip_union_disjoint(',
     '    if (!cur_pat(run) &&\n        gshscene_clip_union_disjoint(', CU21T),
    ('cu21-measure-fast-for-disjoint-members', 'src', RS,
     '    if (in_tile || gshscene_clip_union_disjoint(item, maps, count - 1, &target->map, &verdict) != GSHSCENE_OK ||',
     '    if ((void)in_tile, gshscene_clip_union_disjoint(item, maps, count - 1, &target->map, &verdict) != GSHSCENE_OK ||', CU21T),
    ('cu21-measure-mask-not-in-tile', 'src', RS,
     '      r = measure_mask(cs, &inner, area, M, item, tmaps, m_count, arena, &local, used + bytes, 1);',
     '      r = measure_mask(cs, &inner, area, M, item, tmaps, m_count, arena, &local, used + bytes, 0);', CU21T),
    ('cu21-tile-plane-row-offset', 'src', RS,
     '    const uint16_t * from = ua.b.plane + (size_t)((y - W.y0) * Ww + (q.x0 - W.x0));\n    for (size_t x = 0; x < qw; x++) {',
     '    const uint16_t * from = ua.b.plane + (size_t)((y - W.y0) * Ww);\n    for (size_t x = 0; x < qw; x++) {', CU21T),
    ('cu21-union-tier-count-lost', 'src', RS,
     '  Union_Plan * pl = &ua->pl;\n  cs->counters->union_general++;',
     '  Union_Plan * pl = &ua->pl;', CU21T),
]

# S4 (pass B): the in-tile limits of every copy of the whole render, the damage notwithstanding.  The unit enumerates the copies of
# `wreach` (the whole render's part of the unit), a copy the damage does not reach is covered and tested with nothing to draw, and the
# overlapping pass walks the copies of the whole use.  ALimitMetInsideATileIsMetWhateverTheDamageDraws (fills and strokes, abutting and
# overlapping cells) and ADepositCap... hold them.
S4_TESTS = ["testRender_pattern_rules", "testRender_pattern_union", "testRender_pattern", "testRender_conformance", "probe"]
PLANTS += [
    ('rp-s4-fill-copies-of-the-damage', "src", RS, '  GSHSCENE_Result r = pat_each(run, L, u.wreach, pat_fill_copy, &u);', '  GSHSCENE_Result r = pat_each(run, L, u.reach, pat_fill_copy, &u);', S4_TESTS),
    ('rp-s4-stroke-copies-of-the-damage', "src", RS, '  GSHSCENE_Result r = pat_each(run, L, u.wreach, pat_stroke_copy, &u);', '  GSHSCENE_Result r = pat_each(run, L, u.reach, pat_stroke_copy, &u);', S4_TESTS),
    ('rp-s4-fill-unit-outside-the-damage-skipped', "src", RS,
     '  if (gshcpu_render_rect_empty(u.wreach)) {\n    return GSHSCENE_OK; /* nothing of the unit is in the whole render */\n  }\n  L->urect.x1 = L->urect.x0;\n  L->urect.y1 = L->urect.y0;\n  /* S4: the copies of the whole render, not only the ones the damage draws, so that a limit met inside a copy (a cell cap, a gradient\n   * or an image past G14\'s domain, a coordinate past the range) refuses the use whatever the damage; a copy the damage does not reach\n   * is covered and tested over the whole render\'s part of it and draws nothing. */\n  GSHSCENE_Result r = pat_each(run, L, u.wreach, pat_fill_copy, &u);',
     '  if (gshcpu_render_rect_empty(u.reach)) {\n    return GSHSCENE_OK; /* nothing of the unit is in the whole render */\n  }\n  L->urect.x1 = L->urect.x0;\n  L->urect.y1 = L->urect.y0;\n  /* S4: the copies of the whole render, not only the ones the damage draws, so that a limit met inside a copy (a cell cap, a gradient\n   * or an image past G14\'s domain, a coordinate past the range) refuses the use whatever the damage; a copy the damage does not reach\n   * is covered and tested over the whole render\'s part of it and draws nothing. */\n  GSHSCENE_Result r = pat_each(run, L, u.wreach, pat_fill_copy, &u);', S4_TESTS),
    ('rp-s4-clip-copies-of-the-damage', "src", RS, '    GSHSCENE_Result r = pat_each(run, L, clip->whole, pat_clip_copy, &u); /* S4: the copies of the whole render */', '    GSHSCENE_Result r = pat_each(run, L, clip->rect, pat_clip_copy, &u); /* S4: the copies of the whole render */', S4_TESTS),
    ('rp-s4-clip-outside-the-damage-skipped', "src", RS, '  if (!gshcpu_render_rect_empty(clip->whole)) {\n    if (drawn) {', '  if (drawn) {\n    if (drawn) {', S4_TESTS),
    ('rp-s4-stroke-paint-only-when-drawn', "src", RS, '  result = pat_paint_copy(run, L, u, &c, rect, whole, cov, NULL); /* S4: the paint is made ready over the whole render\'s part, drawn or not */', '  if (!gshcpu_render_rect_empty(rect)) {\n    result = pat_paint_copy(run, L, u, &c, rect, whole, cov, NULL);\n  }', S4_TESTS),
    ('rp-s4-overlap-pass-copies-of-the-damage', "src", RS, '    result = pat_pass(run, L, tile, overlaps, L->period ? prect : whole);', '    result = pat_pass(run, L, tile, overlaps, prect);', S4_TESTS),
    # Removed, rp-s4-overlap-pass-after-abort-copies-of-the-damage: equivalent.  The pass is drawn over the damage's rectangle instead of the whole
    # render's only after the period window has been given up, and the window is taken only when it lies in that rectangle; a rectangle that holds
    # the window of an overlapping lattice holds a whole copy of every class, so a copy outside it cannot refuse where the ones inside do not.  The
    # witness, which holds with or without the plant, is RenderPatternRules.ADamageThatTheWindowFitsInRefusesWhatTheWholeRenderDoesOverCellsThatOverlap.
    ('rp-s4-copy-without-pixels-counted', "src", RS, '  if (!gshcpu_render_rect_empty(rc)) {\n    L->urect = gshcpu_render_rect_join(L->urect, rect_moved(rc, c->sx, c->sy));\n    L->copies++;\n  }', '  {\n    L->urect = gshcpu_render_rect_join(L->urect, rect_moved(rc, c->sx, c->sy));\n    L->copies++;\n  }', S4_TESTS),
]

# CG4 (pass B): a member of a union that can touch no pixel is skipped by the fast tier as the general tier skips it; neither refuses the
# push for the geometry of such a member.
UNION_TESTS = ["testRender_clip_union", "testRender_clip_union_general", "testRender_conformance", "probe"]
PLANTS += [
    ('rp-union-fast-member-without-pixels-swept', "src", RS, "        !union_member_pixels(&mdb, &mpx)) {\n      continue; /* the box was formed by the first loop", "        0) {\n      continue; /* the box was formed by the first loop", UNION_TESTS),
    ('rp-union-fast-member-without-pixels-bounds', "src", RS, "    if (!union_member_pixels(&db, &m)) {\n      continue;\n    }\n    if (!have) {", "    if (!union_member_pixels(&db, &m)) {\n      m = GSHCPU_Rect{0, 0, 0, 0};\n    }\n    if (!have) {", UNION_TESTS),
    ('rp-union-member-pixels-empty-box-only', "src", RS, "  return !gshcpu_render_rect_empty(*m);\n}", "  return 1;\n}", UNION_TESTS),
    ('rp-union-member-pixels-floor-ceil-swapped', "src", RS, "  m->x0 = box_floor(db->x0);\n  m->y0 = box_floor(db->y0);\n  m->x1 = box_ceil(db->x1);", "  m->x0 = box_ceil(db->x0);\n  m->y0 = box_floor(db->y0);\n  m->x1 = box_ceil(db->x1);", UNION_TESTS),
]

SURVIVORS = {
    # shape-exact-primitives CU21 (cu21-): equivalent, each with the argument and the check that backs it.
    'cu21-cut-count-is-the-uncut-count': 'equivalent: the entries of the cut array past the ones kept are NULL (calloc), and every cover function skips a NULL path as it skips the NULL entries of a geometry',
    'cu21-tile-remainder-of-the-byte': 'equivalent: the remainder differs from the area minus the byte only for a whole pixel (32 against 0), whose byte is 255, so the clip block\'s sum already reads at least 255 levels and its byte is 255 either way',
    'cu21-tile-area-not-held-at-the-pixel': 'equivalent: a plane above the pixel gives byte 255 and a remainder that only feeds a sum read as min(255, sum >> 5), already 255; witness: AMemberThatOverlapsItselfBesideAnAbuttingOneIsTheSameInATileAndDrawnDirectly draws such a pixel as the tile and as the instances and finds them equal',
    'cu21-tile-members-under-the-runs-maps': 'equivalent: the maps of a clip are the instances\' maps at its push, and a picture is balanced, so run->maps holds the same instance maps (to the clip\'s count - 1) whenever the clip is read again; the saved copy is kept as the single clip keeps it',
    'cu21-tile-whole-not-the-plans': 'equivalent: the callers of pat_cover for a clip (pat_clip_copy, pat_fold_clips) do not read the whole rectangle back, only the rectangle written; it is set for the interface a fill uses',
    # masks-images-patterns story H1 (hx-): equivalent mutants, each with the cause.
    'hx-notice': 'equivalent in what is asked for: measure_one tests the notice of the original fill item, where the plant reads a stroke notice of a fill (no style, so nothing noticed) and goes on to expand a hatch that draws nothing - an empty pen, a singular map or no spacing - which opens no lines and so no arena memory; the peak, and the block asked for under caller memory, are the same (testRender_hatch holds the need of those three below that of a drawing hatch)',
    'hx-key-revision': 'equivalent: the hatch revision is a constant of the build, equal in every key, so it never separates two; it is in the key for the day it changes',
    'hx-cache-eq-revision': 'equivalent: the same constant, left out of the equality',
    'hx-hit-lines': 'equivalent: a sink\'s lines limit is fixed for its life and a miss over it stores nothing, so a hit never holds more lines than the limit',
    'hx-hatch-item-kind': 'equivalent: only a fill carries a hatch paint (gshscene_list_stroke refuses one, UNSUPPORTED), so the kind test repeats the record\'s',
    'hx-hatch-item-stroke': 'equivalent: the synthetic item reaches stroke(), pat_stroke() and gshcpu_render_stroke_open_lines(), none of which reads the item\'s kind (the dispatch that does reads the original item)',
    'hx-cache-hash-hatch': 'equivalent: the bucket is a function of the key and equality decides, so a hash that leaves a field out is only a worse spread',
    'hx-cache-hash-rule': 'equivalent: the same, for the rule',
    'hx-cache-eq-rule': 'equivalent in practice: the two keys of one region, pen and hatch under the two rules differ in the hash by the multiplier (an odd constant), which moves the bucket by 0xB3 in its low byte, so they never share a bucket; 25311 triples of ids were checked and none did, and equality is only ever asked of keys in one bucket',

    # masks-images-patterns story R2 (rp-): the hand plants no test sees.  The ones marked equivalent have the argument beside them; the others are not shown to be.
    "rp-cache-fit-unchecked": 'equivalent: the table refuses an entry past its bound itself; the check only spares the copy out of the window',
    "rp-cache-raster-index-transposed": 'equivalent: the raster is private to period_copy, which writes and reads it with the one index function, and `r[0] * s + r[1]` is a bijection of the classes onto [0, p s) as `r[1] * p + r[0]` is',
    "rp-clip-fold-tmp-not-cleared": 'equivalent: pat_cover writes every pixel of its rectangle (the dense loop of cover.c assigns the fold of each column), and pat_fold_clips reads only that rectangle; where nothing is covered it clears the weights itself',
    "rp-clip-maps-short": "equivalent: the last of the `count` maps is the item's own, which pat_cover does not read (the geometry takes `count - 1` maps and the item's map separately)",
    "rp-copy-quad-error-lost": 'equivalent: pattern_begin counts the copies with gshgeom_lattice_each, which builds the same cell quadrilateral from the same map and lattice and refuses the use before any copy opens, so the call cannot fail in a copy',
    "rp-count-remaining-underflows": 'equivalent: pat_total is the sum of counts each of which was held to the remaining budget, so it never exceeds the limit it is subtracted from (and the clamp at the lattice index maximum is past what can be counted)',
    "rp-facts-depth-unbounded": "equivalent: a picture's depth is capped at 32 by shape-scene, below the 64 this walk stops at",
    "rp-facts-gradient-error-reads-nothing": 'equivalent: a gradient resource that fails to read cannot be in a list (the paint was made from it), and picture depth is capped at 32',
    "rp-key-not-zeroed": 'equivalent: every field of the key is written and it has no padding, so the memset adds nothing',
    "rp-key-revision-clip-lost": 'equivalent: a revision in a key is a build constant and a key is compared only with keys of the same process, so zero in its place separates nothing',
    "rp-key-revision-flatten-lost": 'equivalent: a revision in a key is a build constant and a key is compared only with keys of the same process, so zero in its place separates nothing',
    "rp-key-revision-lattice-lost": 'equivalent: a revision in a key is a build constant and a key is compared only with keys of the same process, so zero in its place separates nothing',
    "rp-key-revision-stroke-lost": 'equivalent: a revision in a key is a build constant and a key is compared only with keys of the same process, so zero in its place separates nothing',
    "rp-key-tile-id-lost": "equivalent: a pattern resource holds its tile, so the pattern's id names the tile; the tile's id adds nothing to the key",
    "rp-overlap-unused-step-counts": "equivalent: shape-scene's store replaces the step of an axis that does not repeat by the cell's size (pattern.c), so `x_step < w` is false there whatever the caller wrote; the test AStepThatNoCopyUsesIsNotAnOverlap shows the sink sees it so",
    "rp-pass-layer-not-isolated": "equivalent: the group fields of the lattice layer's frame are read only when a group is popped, and the lattice layer is never popped as a group (the pass restores the stack); isolation, opacity and composite are set for the frame to be a complete record",
    "rp-pass-layer-opacity": "equivalent: the group fields of the lattice layer's frame are read only when a group is popped, and the lattice layer is never popped as a group (the pass restores the stack); isolation, opacity and composite are set for the frame to be a complete record",
    "rp-stroke-cut-part-ignored-by-the-cover": 'equivalent in what is drawn and what is refused: stroke_part feeds only the bound that chooses between counting the whole walk and counting the drawn rectangle, and a part whose bound is within the limit cannot pass it, so either count gives the same refusal; the work reported is the one thing that moves',
    "rp-table-bucket-ignores-the-fraction": 'equivalent: the bucket only chooses a chain; keys that differ only in the fraction share a bucket and are told apart by the comparison',
    "rp-tier-giveup-keeps-the-arena": "equivalent: every copy of the pass rewinds the arena to the pass's mark before it allocates (pat_cover and pat_stroke_copy), and nothing allocates between the give-up and the first copy",
    "rp-tier-giveup-keeps-the-stack": "equivalent: a pass that returns OK has balanced its pushes, and one that stops at a limit meets the same limit under the general tier (a copy's counts are those of the whole render, the window's or not), which refuses; the restore at the end of the pass sets the stack, clips and groups to what they were in either case (sweeps of the deposit limit over a clip block around a group, at steps of the cell and smaller, saw no difference)",
    "rp-tier-giveup-keeps-the-unit-region": 'equivalent: every unit empties its rectangle before it is used (pat_fill, pat_stroke), and nothing reads it between the give-up and the first unit',
    "rp-tier-guard-left-on": 'equivalent: pattern_begin clears `guard` and `abort_tier` before every pass and nothing reads them between passes',
    "rp-unit-count-not-kept": "equivalent: a pixel's count is compared only with 0, 1 and 'more'; the byte saturates at 255 only so that it cannot wrap to 0, and a count stuck at 2 takes the same branches",
    "rp-unit-empty-region-applies": 'equivalent: an empty rectangle makes widen_w do nothing and both loops visit no pixel, and the work added is the area of an empty rectangle, zero',
    "rp-unit-region-not-reset": "equivalent: pattern_begin, pat_fill and pat_stroke each empty the unit's rectangle before they use it, and pat_clip_copy does not read it",
    "rj-element-backdrop-aliased-after-the-first-channel": "equivalent: the step copies the backdrop into the operator's operand before it writes "
        "the layer, and writes each channel from the same channel of both, so the layer being its own backdrop changes nothing",
    "rj-element-other-operators-weigh-by-the-shape": "equivalent: every element of an operator that is not PDF's has its shape equal to its "
        "weight (a fill's coverage; a pop's clips and mask), so the one is the other",
    "rj-step-skips-on-either-weight-zero": "equivalent: an element whose weight or shape is 0 has the other 0 or a source of nothing too "
        "(a pop's shape is its weight times a group shape of at most 1, and a group that drew has a shape), so what it skips changes nothing",
    "rj-pop-nothing-drawn-has-a-whole-shape": "equivalent: a pixel the child never reached is visited only by an unbounded operator, which is "
        "none of PDF's, and the shape of every other operator is its weight, so the group shape read there is never used",
    "sc7-hit-limit-unchecked": "equivalent: a cache is one sink's, under one geometry limit, and the expansion refuses a part "
        "past max_points before an entry is made, so a hit is never past it",
    "st8-limit-not-the-items": "not seen: a run's centerline is the item's limit only past a limit each glyph passes and the "
        "whole does not (a glyph past it is that glyph's note); no test sets one",
    "st8-nothing-drawn-notes-dropped": "not seen: the entries of the tests that reach it come from the planning pass, which "
        "reports a glyph the provider cannot give before the sink does; the case is a note only the expansion makes",
    "uc3-window-unbounded": "not seen by a plain build: it writes past the piece pool, which a sanitizer sees",
    "uc4-remainder-unchecked": "not seen: the check of the dense area after it refuses an inconsistent cover with the same status; "
        "the case is a cover the remainder alone refuses",
    "uc14-gap-step-multiplicity": "not seen: needs coincident pieces of one direction at a gap step of a cell that has other "
        "pieces (the mid-gap variant is caught)",
    "cg-x-steps64-not-cleared": "equivalent in the design (CG5): a step of a part's winding at the cell's last row is written to the part's record and its mark, "
        "and read by nothing (the sum of UC4 weights it 64 - 64, and no slab starts at 64), so the memset only keeps an uninitialised flag from being read, "
        "which a sanitizer sees and the plant harness does not run; the witness is TheStepsAtTheBottomOfTheLeftSideOfACellNeverChangeALaterCell: a cell "
        "whose piece ends at (0, 64), made with a scratch, leaves a later cell the area it has with a scratch of its own",
    "rv-670-lt": "not seen by a plain build: `p <= n` reads one part past the three that both callers pass (an array on the stack), "
        "which a sanitizer sees and the plant harness does not run",
    "rv-741-lt": "not seen by a plain build: `p <= n` reads one part past the three that the caller passes (an array on the stack); seen by "
        "`make test-asan` (UBSan: index 3 out of bounds for type 'uint32_t [3]', checked 2026-11-07 on this plant), which the plant harness does not run",
    "rv-1503-lt": "not seen by a plain build or by test-asan (checked 2026-11-07): the band's clearing loop `i <= total` writes 0xffff one element "
        "past `union_k` and `clip_k`, which are carved from one arena back to back; the written value is the initial value of the next array's first "
        "element, so nothing reads a changed byte; it is an out-of-bounds write all the same, left open: the next step is a caller-memory test whose "
        "block of memory is a malloc of exactly the size the report names for the render, under ASan",
    "rp-chain-singular-not-refused": "OPEN (pass B 2026-11-07, not seen by any test, nor by the sweep with every test tried): the pattern pass refuses an item whose "
        "chain cannot be composed with the target's map; the plant drops that refusal. Either an earlier check refuses every such chain (then this is "
        "a repeated guard, to be shown by a test that feeds a pattern fill a singular placement and reads the earlier refusal), or a test of the "
        "refusal is missing: the next step is that test",
    "rp-tier-nested-pattern-allowed": "OPEN (pass B 2026-11-07, not seen by any test, nor with every test tried): the period tier is not taken for a tile that holds "
        "a pattern; the plant lets the guard on that fact through and no counter or byte differs, so a later check refuses the tier for the same "
        "tile or the tier draws the same bytes there: the next step is a test that reads the tier's counter for a tile that holds a pattern with an "
        "integer period of its own, in the render whose outer pass would otherwise take the tier",
    "rp-facts-pattern-fill-missed": "OPEN (pass B 2026-11-07, not seen by any test, nor with every test tried): the walk that finds the facts of a tile does not note a "
        "pattern fill in it; the guard that reads the fact is the one above (rp-tier-nested-pattern-allowed): the two are one test to write",
    "uc15-meeting-depth-rounded-2": "not seen in the sweep that keeps the cell's own exchanges (the group sweep's copy, uc15-meeting-depth-rounded, is caught by the union vectors): needs a meeting whose depth is not a multiple of 1/1024 at a resolution where the "
        "rounding changes a level",
}


# PLANT_OBJ_EQUIV=1: the mechanical proof of equivalence.  A plant of one source file is compiled beside the unplanted file, both from a
# copy of the tree at the same relative path, with -g0, no identification string and the copy's prefix mapped away; if the two objects
# are byte for byte the same, or their full disassemblies and section contents are (the symbol table and the relocations included),
# the mutant is the same program and no test can tell it from the code.  Printed as `equiv` with the digest; a plant whose objects differ
# is `differs` (a test must catch it, or a test is missing), one that does not compile is `nocompile` (the compiler caught it).
# Header plants are not decided by this (every object that includes the header is rebuilt): they print `skipped`.
def _object_text(obj):
    out = []
    for args in (["-d", "-r", "--no-show-raw-insn"], ["-s"], ["-t"]):
        r = subprocess.run(["objdump"] + args + [str(obj)], capture_output=True, text=True)
        out.append(r.stdout)
    return "\n".join(out)


def object_equivalence(plant):
    pid, kind, fname, old, new, _tests = plant
    if kind not in ("src", "srcl") or fname.endswith(".h"):
        return pid, "skipped", "not a plant of one source file"
    d = WORK / ("eq-" + pid)
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    original = (ROOT / fname).read_text()
    try:
        text = apply(original, old, new, pid) if kind == "src" else apply_line(original, old, new, pid)
    except RuntimeError as e:
        return pid, "error", str(e).splitlines()[0]
    objs = []
    for tag, body in (("control", original), ("planted", text)):
        root = d / tag
        shutil.copytree(ROOT / "src", root / "src", dirs_exist_ok=True)
        (root / fname).write_text(body)
        obj = root / "x.o"
        r = run(CC + CFLAGS + ["-g0", "-fno-ident", "-ffile-prefix-map=%s=." % root, "-c", str(root / fname), "-o", str(obj)])
        if r.returncode != 0:
            shutil.rmtree(d, ignore_errors=True)
            return pid, "nocompile", r.stderr.strip().splitlines()[0][:100] if r.stderr.strip() else ""
        objs.append(obj)
    same = objs[0].read_bytes() == objs[1].read_bytes()
    how = "identical object bytes"
    if not same:
        a, b = _object_text(objs[0]), _object_text(objs[1])
        same = a == b
        how = "identical disassembly, section contents, symbols and relocations"
    import hashlib
    digest = hashlib.sha256(objs[0].read_bytes()).hexdigest()[:16]
    shutil.rmtree(d, ignore_errors=True)
    return pid, ("equiv" if same else "differs"), (how + ", control object sha256 " + digest) if same else ""


def object_equivalence_main(plants):
    counts = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        for pid, verdict, detail in pool.map(object_equivalence, plants):
            counts[verdict] = counts.get(verdict, 0) + 1
            print("%-9s %-40s %s" % (verdict, pid, detail))
    print("object equivalence: %d plants: %s" % (len(plants), ", ".join("%d %s" % (v, k) for k, v in sorted(counts.items()))))
    return counts


# Plants whose text moved with the code they plant (tools/reanchored-plants.json: the same defects, re-found in the code as it stands by
# matching the lines of the old text against the file; each is run like any other and must be caught).  They replace the entries of the
# same id above.
_reanchored = ROOT / "tools" / "reanchored-plants.json"
if _reanchored.exists():
    with open(_reanchored) as _f:
        _rp = [tuple(x) for x in _json.load(_f)]
    _rp_ids = {x[0] for x in _rp}
    PLANTS = [x for x in PLANTS if x[0] not in _rp_ids] + _rp

# Plants that do not compile (tools/retired-plants.json: id -> why): the compiler refuses the mutant, so there is nothing for a test to catch.
_retired_file = ROOT / "tools" / "retired-plants.json"
if _retired_file.exists():
    with open(_retired_file) as _f:
        _retired = _json.load(_f)
    PLANTS = [x for x in PLANTS if x[0] not in _retired]

# Plants a test that is not in their lists catches (a sweep with PLANT_ALL_TESTS=1 found them): tools/extra-catchers.json names that test for
# each, so the plain sweep sees them caught.
_extra = ROOT / "tools" / "extra-catchers.json"
if _extra.exists():
    with open(_extra) as _f:
        _ex = _json.load(_f)
    PLANTS = [x if x[0] not in _ex else tuple(list(x[:5]) + [list(x[5]) + [t for t in _ex[x[0]] if t not in x[5]]]) for x in PLANTS]


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    if os.environ.get("PLANT_OBJ_EQUIV") == "check":
        # The plants tools/equivalent-plants.json holds are equivalent by their object code: every one must say so again.
        import json as _eqjson
        with open(ROOT / "tools" / "equivalent-plants.json") as _f:
            listed = [tuple(x) for x in _eqjson.load(_f)]
        counts = object_equivalence_main(listed)
        if counts.get("equiv", 0) != len(listed):
            print("FAIL  tools/equivalent-plants.json names plants that are not object-identical")
            return 1
        print("ok    %d plants of tools/equivalent-plants.json are equivalent by their object code" % len(listed))
        return 0
    if os.environ.get("PLANT_OBJ_EQUIV"):
        chosen = [p for p in PLANTS if not ONLY or re.search(ONLY, p[0])]
        object_equivalence_main(chosen)
        return 0
    srcs = sources()
    failures = 0
    known = 0

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
            if p[0] in SURVIVORS:
                known += 1
                print("known %-34s not caught: %s" % (p[0], SURVIVORS[p[0]]))
            else:
                print("FAIL  %-34s not caught by %s" % (p[0], " ".join(p[5])))
                failures += 1
        elif p[0] in SURVIVORS:
            print("FAIL  %-34s is caught by %s: take it out of SURVIVORS" % (p[0], r[0][0]))
            failures += 1
        else:
            print("ok    %-34s caught by %s" % (p[0], ", ".join("%s (%s)" % (t, (f[0][14:70] if f else "crash")) for t, f in r)))

    if ONLY and not PROBE_ONLY:
        print("%d plants run (PLANT_ONLY=%s), %d not caught, %d known survivors" % (len(plants), ONLY, failures, known))
        return 1 if failures else 0
    stale = sorted(set(SURVIVORS) - set(ids))
    if stale:
        print("FAIL  SURVIVORS names plants that are not in the table: %s" % " ".join(stale))
        failures += 1

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

    print("%d plants, %d not caught, %d known survivors" % (len(PLANTS) + 1, failures, known))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
