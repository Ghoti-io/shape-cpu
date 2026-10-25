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
 * @file render.h
 *
 * The CPU reference sink (story 13b): `shape-scene`'s sink contract (sink.h,
 * S1 to S16) implemented by compositing a closed list into a raster.  It is the
 * definition every other sink is measured against (AD-1, AD-13, AD-18), so its
 * arithmetic is stated here, normatively, as S11.
 *
 * ### What it draws (S2)
 *
 * A `RASTER` target whose surface is RGBA, interleaved, either 8 bits a channel
 * (unsigned normalized) or 32-bit `float` a channel, with the straight or the
 * premultiplied alpha form the raster's format records (it is read, never
 * assumed).  Every other format (gray, CMYK, planar, 12 or 16 bits), a `STREAM`
 * target and the reserved GPU kinds are ::GSHSCENE_ERR_UNSUPPORTED.  For a target
 * it draws, the caps are: the seven item kinds it draws (fill, clip push and
 * pop, group push and pop, instance, stroke), named one by one, so that a kind
 * shape-scene adds later is refused by the planning pass
 * (`ITEM_KIND`) and never reaches the sink; the composite operators it
 * composites, named one by one (the twelve Porter-Duff operators, `PLUS` and the
 * fifteen blend modes of color-paint story P3; an operator past them is refused
 * by the planning pass, never drawn as another); none of PDF's group fields
 * (non-isolated, knockout, a soft mask: shape-scene's T1 to T7), so the planning
 * pass refuses a group that sets one (S19);
 * `CAN_BLOCK` (it completes inside `begin`), `CALLER_MEMORY`, `CLIP_AA`,
 * `BLEND_SPACES`, `READBACK` and `GLYPHS_FROM_FONT` (story 14a: a run from a font
 * is drawn through its face's provider by shape-scene's route, glyph.h, the fast
 * tier wherever the route returns it); every gradient kind and `GRADIENT_SPACES`
 * (color-paint story P2: linear, radial and sweep, each in its stated
 * interpolation space); `STROKE_EXPAND` and no stroke feature native (SC1); no `THREAD_AFFINE` (what it holds
 * across renders, the cache of SC7, is guarded by one lock, so renders on distinct surfaces may run at once) and no
 * `PREPARE`.  Cancel is `BETWEEN_UNITS` (each unit
 * of the list's expansion); the work between two checks is counted in
 * `"cells+points+pixels"` and its bound (SC11) is, for an aligned stroke,
 * `3 max_points + max_deposits + 2 max_bytes + elements * GSHGEOM_STROKE_MAX_DEPTH` of the sink's ::GSHCPU_Limits (each
 * at its default when zero; `max_points` defaults to `shape-geometry`'s; `elements` is the most path elements a
 * resource may hold, shape-scene's `max_resource_elements` of the limits the sink was created with).
 *
 * ### Geometry (S16, AD-12, AD-24)
 *
 * For a fill or a clip push the maps are composed root to leaf with
 * `gshgeom_map_compose_chain` (the enclosing instances' maps, the item's map,
 * for a glyph of an outline run the translation to its position, the target's map
 * last); the path is made a device path by `gshgeom_device_path_from_model` and
 * flattened by `gshgeom_device_path_flatten` (the definition's flattener, after
 * the map); the coverage is story 9's scan converter, the exact-area
 * accumulation of coverage.h, restricted to the rows and columns drawn.  The
 * restriction is exact: a pixel inside it has the coverage the whole path would
 * give it, because every crossing of a row or column boundary is computed from
 * the edge's own end points, wherever the drawing starts.  An outline glyph run
 * is one shape: its glyphs' windings add.  A point more than 2^24 pixels from the
 * device origin, a map or a device path `shape-geometry` refuses, a flattened
 * path past `max_points` and more than `max_deposits` cell deposits are the sink's
 * resource limits: the item is refused while drawing (S6).  The cells are counted
 * over what the whole render draws of the item (its pixels meet the extent and
 * its clips' reach there), whatever the damage, so a damage-limited render
 * refuses the items the whole render refuses (S4).
 *
 * A run from a font is one shape too.  On the fast tier each glyph's device path is
 * the face's provider's (glyph.h), moved by the whole pixel of its origin; on the
 * general route it is the provider's unhinted outline under the glyph's map
 * (`gshscene_glyph_map`), made a device path after the map; either is flattened as
 * above.  With hinting off the two are the same path under the snapped map (AD-5),
 * so the same pixels.  Each glyph's entry is reported with its index in `second`;
 * a glyph the provider cannot give, or whose path passes `max_points` or the 26.6
 * range, is refused alone (the rest of the run is drawn), and under strict the
 * render stops at the item.
 *
 * ### S11 Arithmetic (normative)
 *
 * - **Working values.**  Every layer is premultiplied `double` RGBA per pixel of
 *   the region drawn (the extent meet the damage): the root layer is the
 *   target's values read once (an 8-bit sample `v` is `v / 255`, a straight color
 *   is multiplied by its alpha), a group's layer starts at `(0, 0, 0, 0)` in its
 *   blend space (N6).
 * - **Paint.**  A fill's paint (the foreground for a `FOREGROUND` paint, N10) is
 *   converted once per fill from its space to the layer's (N3; untouched when the
 *   spaces are equal) and premultiplied.
 * - **Gradients (color-paint story P2; shape-scene's G1 to G14, S17).**  A
 *   gradient fill's source at pixel `(x, y)` is G12's at the device point
 *   `q = (x + 1/2, y + 1/2)`, the pixel's center, weighted by its coverage and
 *   clips as a solid fill's.  The fill's chain (the enclosing instances' maps,
 *   the item's, the gradient's as the leaf, the target's last) is composed by
 *   `gshgeom_map_compose_chain` (AD-24) and inverted once a fill: the 2x2 part is
 *   scaled by the power of two of its largest entry (exact), its determinant is
 *   the exact sum of its two exact products rounded once, and `p = L^-1 (q - t)`.
 *   The stops are `gshscene_gradient_resolve`'s, once a gradient a render (by
 *   resource id), made interpolants (G4) once.  At each pixel: G7's `t` is
 *   `((p - p0) . n_d) / den` with `den` the exact `(p1 - p0) . n_d` rounded once;
 *   G8's `a` is the exact `|c1 - c0|^2 - (r1 - r0)^2` rounded once (so it is zero
 *   exactly when it is), `b` and `c` are evaluated in doubles, the roots are
 *   `q / a` and `c / q` with `q = b + sign(b) sqrt(b^2 - a c)` (`gmath_sqrt`), or
 *   `c / 2b` where `a` is zero, and the larger root whose radius is positive is
 *   taken; G9's angle is `gmath_atan2`, exact on the axes, `+ 2 pi` below zero and
 *   scaled to the unit; G5's `mod` is `x - floor(x / P) P` (`gmath_floor`); a `t`
 *   beyond `2^431` in magnitude (or not finite) is taken as `+-2^431`, so no NaN
 *   reaches a store.  The straight color at `t` (un-premultiplied when the line
 *   is) has its alpha multiplied by the paint's, is converted per pixel from the
 *   interpolation space into the layer's (N3; untouched when they are equal),
 *   premultiplied and applied with `SRC_OVER`; where G8 gives nothing the pixel
 *   is not touched.  A fill that `gshscene_gradient_notice` names (G10, G11) is
 *   skipped.  A composed map whose inverse is not finite, or whose determinant
 *   rounds to zero, and a fill whose rectangle in the whole render (S4) maps
 *   outside half of G14's sampling domain, are the sink's resource limit: the item
 *   is refused while drawing (S6).  Before the write, each component of the color
 *   and `t` are within ::GSHCPU_RENDER_GRADIENT_TOLERANCE times `max(1, |v|)` of
 *   the gradient oracle's (shape-scene's `tests/gradient_vectors.h`).  No
 *   dithering.
 * - **Coverage (D8).**  Coverage is 8-bit.  A clip multiplies each fill's
 *   coverage (N9): for a fill of coverage `k8` under the clips in force in its
 *   group, of coverages `c8` in the order they were pushed, `m8` starts at `k8`
 *   and each clip makes it `(m8 * c8 + 127) / 255` (integer division); a group's
 *   composite is weighed by the same fold of the clips that were in force at its
 *   push, starting from 255.  The fill or composite applies with `m = m8 / 255`
 *   (a correctly rounded division); a pixel with `m8 = 0` is not touched.
 * - **Compositing (N4, N5).**  `apply(D, S, m, op)` is evaluated as
 *   `(1 - m) * D + m * (Fa * Cs + Fb * Cd)` per component (`PLUS` as `min(1,
 *   Cs + Cd)`), in that order of operations, with no fused multiply-add.  A group
 *   with a bounded operator does not touch a pixel where its scaled result is
 *   `(0, 0, 0, 0)` (there `apply` is exactly the backdrop); an unbounded one
 *   applies wherever its weight is above zero.
 * - **Blend modes (shape-scene's B1 to B14; color-paint story P3).**  A
 *   group of blend mode `X` composites by `apply` with `op_X(S, D)`, in the
 *   enclosing layer's space (B5): `Cs` and `Cb` are `S` and `D` un-premultiplied
 *   by N1's evaluation (no division past the bound; where `As Ab` is 0, `B` is
 *   not evaluated and its term is 0), `B_X(Cb, Cs)` is evaluated as B7 and B8
 *   write it, each component saturated (B9), and `op_X` is
 *   `(cs * (1 - Ab) + cb * (1 - As)) + (As * Ab) * B` with alpha
 *   `As + Ab * (1 - As)`, then `apply`'s `(1 - m) * D + m * o`, saturated, with
 *   one rounding at the write.  The order of operations is the formulas' as
 *   written, left to right: screen `(b + s) - b * s`; hard-light `b * (2 * s)`
 *   or screen of `b` and `2 * s - 1`; overlay hard-light with `b` and `s`
 *   exchanged; soft-light `b - ((1 - 2 * s) * b) * (1 - b)`, else
 *   `b + (2 * s - 1) * (d - b)` with `d` the polynomial
 *   `((16 * b - 12) * b + 4) * b` or `gmath_sqrt(b)`; exclusion
 *   `(b + s) - (2 * b) * s`; dodge's and burn's quotients `b / (1 - s)` and
 *   `(1 - b) / s` taken as `sign * 2^66` once the numerator is at least `2^66`
 *   times the denominator (exact, and nothing past it is formed); `Lum` with the
 *   doubles nearest `0.3, 0.59, 0.11`; ClipColor's offsets and denominators from
 *   the differences `C - n` (B10): `L - n` as `Lum(C - n)`, `x - L` as
 *   `(x - n) - (L - n)`, `C - L` as `(C - n) - (L - n)`, each step
 *   `L + ((C - L) * L) / (L - n)` or `L + ((C - L) * (1 - L)) / (x - L)` taken only
 *   when its denominator is positive (so never `0 / 0`, and an all-equal triple is
 *   left alone); SetSat by a three-swap sort.  No silent default: an operator the compositor does not know fails the
 *   render, and the caps claim exactly those it knows.  Before the write a value
 *   is equal to shape-scene's oracle where every step of this evaluation is exact
 *   (STOP-2: the vectors mark those), and otherwise within `1e-12` times
 *   `max(1, |v|)` (the vectors leave out the samples whose evaluation in this
 *   order cannot meet it: a cancellation at large magnitude, or a value within
 *   its error of a discontinuity of B10).  Measured over shape-scene's 1863
 *   samples of `B` and 417 blend cases: polynomial and `min`/`max` modes within
 *   `4.5e-16`, dodge and burn `1.4e-16`, soft-light `7.2e-15` (its root), the
 *   non-separable modes `1.5e-15`.  A value at the corners `+-2^64` is finite and
 *   within the saturation.
 * - **Groups (N8).**  At a group's pop its layer is converted per pixel to the
 *   enclosing layer's space (un-premultiplied, converted, premultiplied again;
 *   alpha 0 gives 0), and not at all when the spaces are equal (N3); then scaled
 *   by its opacity and composited.
 * - **Transfers and conversion (N2, N3).**  The power function is `math`'s
 *   `gmath_pow` (within 1 ulp); the matrix of a gamut is computed in `double` from
 *   its chromaticities as N3 states, and a color moves through the source's
 *   matrix and then the inverse of the destination's.  Nothing comes from libm;
 *   there is no `long double`, no fused multiply-add and no `float` but the
 *   float store.
 * - **Saturation (shape-scene's N1, Corey 2026-10-21).**  Each decode, each
 *   matrix result and each encode of a conversion, every un-premultiply (a
 *   group's conversion, G4's flag, a straight store) and every `apply` is clamped
 *   to `[-2^64, 2^64]`, on the double result, at those points and no others.  An
 *   un-premultiply `C / A` with `|C| >= 2^64 |A|` is `sign(C / A) 2^64` without
 *   dividing, so no infinity is formed; a power only ever takes a value of
 *   magnitude at most `2^64` (the worst is `2^640`), so no conversion overflows.
 * - **The write.**  The one rounding of a render is the write of a touched pixel
 *   to the target, in the target's alpha form: to 8 bits, `floor(v * 255 + 1/2)`
 *   clamped to `[0, 255]` (round half up; a straight color is `C / A`, saturated,
 *   and 0 where `A` is 0); to `float`, the `double` rounded once (round to
 *   nearest), with no clamp but the saturation.  A pixel no unit touched is not written.
 * - **Tolerance (D7, AD-18).**  Before the write, a value is within `1e-12`
 *   times `max(1, |v|)` of the rational oracle's (shape-scene's
 *   `tests/composite_vectors.h`, and this
 *   library's `tests/render_vectors.h` for the D8 weights), and equal to it where
 *   no transfer or matrix is evaluated and the values are dyadic (a blend: see
 *   above).  After it, a stored value is the oracle's rounded the same way, except
 *   that a value within `1e-9` of a rounding boundary may land on either side; a
 *   float store of an inexact value is the one rounding of a value within the
 *   tolerance, which near zero, where floats are far finer than it, is a range of
 *   floats.
 *
 * ### Strokes (SC1 to SC11)
 *
 * A stroke is a fill with another coverage.  Which polygon, in which frame, and how its parts become one 8-bit
 * coverage is decided here; the clips, the layers, the operators, the paint and its per-pixel source are the fill's.
 * The polygon is `shape-geometry`'s one expansion: this sink offsets, joins, caps and dashes nothing itself.
 * Every rule is in real arithmetic unless stated, and numbered so that tests and `design.md` cite it.
 *
 * - **SC1 What the sink claims, and how it honors each field.**  The caps gain the item kind `STROKE` (7) and
 *   `STROKE_EXPAND`; `stroke_native` stays 0, so every required feature is met by expansion and no stroke is refused
 *   for its style.  `width` and `width_space`, `join`, `miter_limit`, `cap`, `degenerate` and the dash fields are
 *   honored by the expansion; `alignment` and `alignment_rule` by the expansion's three parts and then by this
 *   sink (SC5), the one place it acts on a style field; `width_profile` never reaches it (refused at intern).  Every
 *   paint kind draws.  The caps also say `STROKE_RUNS`: a centerline that is a glyph run is drawn as the stroke of
 *   the path shape-scene's ST1 defines for it (ST8 below), on the general route and never hinted (ST3).  The
 *   entries a stroke can receive while drawing are the fill's plus `RESOURCE_LIMIT` (SC6), and for a run the notes of
 *   its glyphs (`GLYPH_UNAVAILABLE`, `RESOURCE_LIMIT`, `second` the glyph's index).
 * - **SC2 The style conversion.**  `gshscene_resource_stroke_geometry` converts the style to `shape-geometry`'s
 *   parameters field by field, each enumeration asserted equal at compile time; `gshgeom_stroke_check` accepts every
 *   style the store interns (a style that passes the store and fails the check is an internal error).
 * - **SC3 The expansion map and the canonical translation.**  The map is `gshgeom_map_compose_chain(instances, item,
 *   target)`, 2D.  Its translation is split exactly into its whole pixels and a fraction in `[0, 1)`
 *   (`gshgeom_stroke_canonical_map`); the expansion is made at the default tolerance at the map with the fraction
 *   only, and the outline is drawn moved by the whole pixels (the rectangle covered is moved the other way; nothing is
 *   copied).  The region is translation-invariant, so SG17 holds at the true map; and the outline is a function of the
 *   centerline, the style, the linear part and the fraction alone.  A coordinate the shift takes past
 *   ::GSHCPU_RENDER_COORD_LIMIT (2^24 pixels) is the sink's resource limit.
 * - **SC4 What draws nothing.**  A unit is skipped by `gshscene_stroke_notice`, which the plan wrote (some map of the
 *   chain, or the composed map, singular; then width 0; then a degenerate gradient): the sink writes no notice and
 *   never fails strict.  A composed map that cannot be formed is not a notice but SC6's limit.  Should the expansion
 *   return `EMPTY` or `NOT_INVERTIBLE` anyway, nothing is drawn.
 * - **SC5 Coverage and its composition.**  The parts are covered as fills are (story 9's accumulation, 8 bits): `b`
 *   from the body (nonzero), `a` from the aligned part (nonzero), `c` from the clip path under its rule over the aligned
 *   part's rectangle.  With `c' = c` (inside) or `255 - c` (outside, the outline's complement flag) and 8-bit integers,
 *   `t = (a c' + 127) / 255` (D8's weigh) and `k = 255 - ((255 - b)(255 - t) + 127) / 255`: the union
 *   `k_b + k_a c' - k_b k_a c'` in D8's arithmetic, exact at both ends, monotone, never above 255.  Absent parts are 0,
 *   so a centered stroke is `k = b`, bit for bit what a fill of the body gives.  `k` then replaces the fill's coverage.
 *   A gradient stroke is the fill's gradient path with this `k`, sampled in the item's local space under the chain
 *   `T I_1 ... I_k M G`.  What `b` and `a` are, a stroke part's coverage, is UC1: the area of the union of its pieces,
 *   not the saturating sum of their areas.  The coverage's tolerance is a **bracket**, `A(R minus D(tau)) - e <= k <=
 *   A(R plus D(tau)) + e + o`, with `A` the area inside the pixel, `D(tau)` the disc of the expansion's tolerance, `e`
 *   the 8-bit rounding and arithmetic and `o = 1/255` the overlap allowance (UC12): the outline's union lies within
 *   `D(tau)` of `R`, so what is left is the fold's rounding and the arithmetic's, under one level.  (Before the exact
 *   step `o` was 64/255, the stopgap of F4: the sum of two partial coverages of one pixel is more than their union, by
 *   as much as the overlap.)  Where the aligned part's boundary crosses the fill region's at an angle `alpha` the
 *   composed error may reach `tau / sin(alpha / 2)`.
 * - **SC6 Limits.**  A map or device path the geometry refuses, the expansion's `ERR_LIMIT` (a part past `max_points`,
 *   the depth cap, the arithmetic bound), a shifted coordinate past the range, or cell deposits past `max_deposits` (summed
 *   over the item's parts and counted over what the whole render draws of the item, never only the damage) are the
 *   sink's resource limit: the item is refused while drawing, strict stops with the finished prefix.  A refusal never
 *   depends on the cache.  `ERR_OOM` is `ERR_OOM`.
 * - **SC7 The expansion cache.**  The sink keeps expansions across renders: by the centerline's and the style's resource
 *   ids, the bit patterns of the canonical map's linear part and fraction, and the construction's revision.  A hit is
 *   the miss bit for bit.  It is bounded by ::GSHCPU_Limits `max_cache_bytes` (0 is 16 MiB, 1 keeps nothing, `UINT64_MAX`
 *   no cap), least recently used first; an entry larger than the bound is used for its render and not kept; an entry a
 *   render is using is never freed under it.  `gshscene_sink_evict` drops every entry naming the id; destruction drops
 *   all; `static_bytes` reports the bytes held.  One lock guards the table, held for a lookup, an insert and an eviction
 *   and never across an expansion.  A `CALLER_MEMORY` render neither reads nor writes the cache: its expansion lives in
 *   the caller's memory (an arena) and is measured with any item's geometry.
 * - **SC8 Fast tiers.**  Each tier is bit-equal to the general tier, which is the oracle and can be forced.  *FT-1* is
 *   the cached expansion.  *FT-2* is the sparse composite: for a stroke with a solid paint and one part (centered, or no
 *   closed subpath aligned) whose cell deposits are bounded, from its edges alone, by a sixteenth of the pixels of its
 *   own rectangle (and by `max_deposits`), the sweep hands over a row at a time only the pixels that can be nonzero.  It
 *   is detected as a *solid hairline* (device width 1) or a *plain solid stroke* (any other width), each counted.
 * - **SC9 Groups and order.**  A stroke is one unit of its sequence.  In an isolated group it composites `SRC_OVER`
 *   into the group's layer and the group's blend mode acts at its pop.  A non-isolated, knockout or soft-masked group is
 *   refused as before, and the strokes inside are skipped.
 * - **SC10 The test sink.**  shape-scene's test sink states, for a stroke whose centerline is a path, the hash of the
 *   outline this sink draws on a raster target (`outline=` of its `K` record): the canonical expansion of SC3.
 * - **SC11 Work and cancel.**  A stroke counts the points of its parts, the cell deposits, two passes over its pixels
 *   when aligned and one otherwise, the expansion's own steps (the centerline's elements times the depth cap,
 *   clamped to the resource limit the bound states), counted whether the expansion was made or found so that a report
 *   does not depend on the cache or the tier, and the exact step's (UC9).  A stroke of a run also counts, once, the
 *   elements of the glyph outlines that went into its centerline (ST9), so the bound's expansion term is the resource
 *   limit times the depth cap plus one.
 *
 * ### Stroked text (strokes story S5, ST8)
 *
 * - **ST8 The sink.**  A stroke whose centerline is a run is drawn by the path above exactly, with the path of
 *   ::gshscene_glyph_run_centerline as the centerline and the **run's resource id** as the centerline id of SC7's key.
 *   The path is built only on a miss, and the cache entry holds the expansion **and the notes of the construction**
 *   (the glyphs the provider could not give, and the elements of the path), which a hit replays, so a hit equals a miss in
 *   the report as well as in the pixels (S12).  Under `CALLER_MEMORY` the cache is neither read nor written, the
 *   path and the glyph outlines are allocated from the arena in the caller's memory in the order the measure walks them, and
 *   the memory asked for is exact: one byte less is `ERR_LIMIT`.  A shifted coordinate, a part past `max_points`, cells past
 *   `max_deposits` or the depth cap refuse the **whole item** with `RESOURCE_LIMIT` (SC6): a run is one region and is
 *   never stroked in part, while a glyph that is left out of the path is a note.  A glyph refused under strict stops
 *   the render at the item, as a fill's does.
 * - **ST9, ST10, ST11.**  The expansion lies within SK10's bounds of the glyphs, the test sink's `outline=` for a run is
 *   the hash of the outline this sink draws for its path, and a stroke of a run and a stroke of its path are bit for bit
 *   the same in pixels, `outline=` and report.
 *
 * ### Exact union coverage of a stroke (strokes story S4b, UC1 to UC12)
 *
 * The outline is made of positively wound pieces whose nonzero fill is the stroke region (D2), and they overlap: the
 * inner side of a curve, the inside of a join, the caps of dashes against their neighbours, the aligned part against the
 * body.  AD-12's accumulation sums the signed areas of the pieces, which is the area of what they cover only where no
 * pixel is partly covered by two pieces that overlap in it; where they do, it credits the overlap twice.  A stroke's
 * coverage is therefore defined as what the pieces cover, and computed in the scan converter's own arithmetic by an exact,
 * integer-only step at the pixels where more than one piece lies.  (A self-overlapping fill has the same property today;
 * this story does not change fills.)  The rules are in real arithmetic unless stated; a **cell** is a pixel of the
 * accumulator, its frame the 26.6 square `[0, 64]^2`, y down, a point on its left or top boundary in the cell, one on its
 * right or bottom boundary in its neighbour.
 *
 * - **UC1 What a stroke part's coverage is.**  For the body and the aligned part (nonzero fills, SC5) the coverage of a
 *   pixel is the fold (UC7) of twice the area, in 26.6 units squared, of the part's nonzero fill inside the pixel, that is,
 *   of the points where the winding number `w` of the part's pieces (as the rasterizer cuts them, UC2) is not zero.  In
 *   the expansion's real arithmetic `w >= 0` (SG16); on the 26.6 lattice a piece of less than a unit's width may be turned
 *   inside out and wind the other way, and the fill counts that sliver covered like any other.  The clip part is a fill of
 *   its region under its own rule and is not changed; where it and an aligned stroke's other parts share a pixel the
 *   coverage is UC8's.  A fill of any kind is not changed by this story.
 * - **UC2 Pieces.**  The scan converter cuts each edge at row boundaries and then each row's piece at column boundaries,
 *   every interior cut point the rounding of the cut from the piece's own start (coverage.c's), and an edge of a stroke part
 *   that goes up is cut from its upper end: the two copies of a segment that two pieces of an outline share, one in each
 *   direction, are then cut at the same points, and the lattice leaves no sliver between them (the cut from the other end
 *   is up to a unit away, and a hole of a unit's width and a pixel's length is three levels).  What is deposited is the
 *   same edge in its own direction.  A **piece** of a cell is one such cut with its two ends in the cell's frame, as
 *   integers in `[0, 64]`, directed along the edge: a **fragment** when its height is not zero, a **flat piece** when it is
 *   zero, has length, and touches the cell's left side (the dense accumulation deposits nothing for it, but it steps the
 *   winding there).  Nothing else of the outline matters inside a cell.
 * - **UC3 Which cells.**  A cell is **exact** when it holds two or more pieces of the part.  With none the winding is one
 *   number, and the dense value (S4's accumulation) is right; with one the winding takes two adjacent values, and so
 *   is it: a contour that enters a cell splits it in two, and two pieces that make one chain (a corner) do too.  For every
 *   other cell the dense value is used unchanged, and the exact value of a cell without overlap is the dense value to the
 *   bit.  (A count of fragments alone would leave to the sum the cell between the ends of two long horizontal strips, which
 *   holds only flat pieces and is as overlapped as any.)
 * - **UC4 The winding at the cell's left side.**  Let `W` be the signed crossing count of the ray to the left of a point
 *   (an edge directed down counts +1), `w = eps W`, `eps` the sign that makes a positively wound piece's inside +1 (-1:
 *   positive winding goes from +x toward +y around the inside, so the edge on the inside's left goes up).  The accumulated
 *   cover of the cells strictly left of the cell in its row, the fold column included, is `A_left = integral_0^64 W(0-, y)
 *   dy`, `W(0-, .)` piecewise constant, stepping only where a contour crosses the line `x = 0-`.  Let `J(h)` be the sum
 *   over pieces of -1 for each that starts at `(0, h)` and +1 for each that ends there (a piece of length 0 counting
 *   nothing: a vertical piece on `x = 0` counts at both its ends, a contour that touches the line and returns counts
 *   nothing).  Then `W_top = (A_left - sum_h J(h) (64 - h)) / 64`, the winding at the top of the side, is an integer: the
 *   division is exact, and a remainder is a piece missed or invented, which refuses the cell (UC9) and never guesses.
 * - **UC5 Slabs, order, winding.**  The distinct heights of the fragments' ends and of the steps of `J` cut the cell into
 *   at most 64 slabs of integer height `H`.  In a slab every fragment spans it or misses it; written top to bottom with sign
 *   `s` (+1 if the contour went down along it) a fragment's `x` at depth `u` is `(a + dx u) / dy`, `a = p.x dy + (Ya - p.y)
 *   dx`, `0 <= a <= 2^12` (a fragment spanning the slab is at `x >= 0` on it).  Their left to right order at a depth is the
 *   order of `x` there, ties by slope (the one that moves left faster with depth is left, as the depth grows past the tie),
 *   then by the fragment's place in the cell's sorted list (the pieces are sorted before the walk, so the result does not
 *   depend on the order they were found in; only coincident fragments, of zero width, reach the last tie).  Two fragments
 *   meet at most once, at `u = N / D`, `D = dx_p dy_q - dx_q dy_p > 0` for the left one `p` moving right of `q`, `N = a_q dy_p
 *   - a_p dy_q`, `N < H D`, `D <= 2^13`, `N < 2^19`.  Walking left to right, the winding of the gap before the first
 *   fragment is `eps (W_top + sum_{h <= Ya} J(h))` and each fragment adds `eps s mu`, `mu` its multiplicity (UC13).
 * - **UC6 The shared area (restated by UC15).**  What the dense sum holds over what the fill covers, in doubled units, is
 *   `2 O - 4 N`, with `O` the integral of `(|w| - 1)^+` (what is counted more than once) and `N` the integral of `-w` where
 *   `w < 0` (the lattice's slivers, which the sum counts negatively and the fill covers).  It is also the dense value less
 *   twice the area of `{w != 0}` (for `w > 0`, `2 w - 2 (w - 1) = 2`; for `w < 0`, `-2 |w| - 2 (|w| - 1) + 4 |w| = 2`), and the
 *   step computes that area, by UC14 and UC15, so that no weight grows with the winding.  The earlier arithmetic of this rule
 *   (a boundary rounded to sixteen fractional bits, a per-gap sum of floors, an error growing with the square of the pieces
 *   of a cell) is retired.
 * - **UC7 The fold.**  The coverage of a cell is `min(255, v' >> 5)` where `v'` is the doubled area of the nonzero fill:
 *   the dense value `v` of the accumulation, less `2 O - 4 N`, at least 0.  A pixel the pieces cover wholly is 255, and one
 *   they cover twice but the fill only in part is the fill's part.  The shift is taken on the magnitude, and an even-odd
 *   clip's parity is reduced before it (a stroke's pieces are wound so that the accumulation is negative, the shared fold's
 *   shift rounds toward negative infinity, and its even-odd ramp back down rounds the other way): the floor of a level
 *   either way round, so `|k - 255 A|` is at most one level for the area `A` the fill covers and a quarter more for the
 *   arithmetic.  The step's error is UC15's.
 * - **UC8 Parts that share a pixel.**  For a stroke with an aligned part the three parts are covered together a band at a
 *   time, and in a cell where at least two of them hold a piece (the clip's too) the coverage is the fold of the doubled area
 *   of `P = {w_b != 0} U ({w_a != 0} & F')`, `F'` the clip's region under its rule (`W_c != 0`, or `W_c` odd) or, when the
 *   outline says so, its complement: the same slab walk with three windings, each from its own part's cover to the left and
 *   its own pieces and flat pieces, and the predicate in place of `(|w| - 1)^+`.  A cell where one part has pieces and the
 *   others have none needs nothing new: a part without a piece has the one winding its cover to the left gives it, so its
 *   coverage is exactly 0 or 255, and SC5's formula with such constants is exact (each nonzero part made exact for its own
 *   overlap first, UC3).  SC5's formula remains the composition everywhere else and the waist's text for a sink that holds
 *   only the three coverages, with its error stated: with `k_b` and `t = k_a c'` as fractions it lies between the region's
 *   bounds `max(k_b, t)` and `min(1, k_b + t)` and differs from the area by up to `min(k_b, t) (1 - max(k_b, t))`, at most
 *   1/4; a thin aligned stroke, whose band and fill edge lie in every pixel along the path, is where it shows (a band
 *   `[0.1, 0.5]` against a fill `x < 0.3` is 0.2 of the pixel by area and 0.12 by the formula).  The clip is the fill
 *   region `F` of the closed subpaths, which can overlap itself (a star's, under the nonzero rule): where only it has pieces
 *   in a cell and they overlap, its coverage is the area of its region by its rule, exactly (the nonzero or the odd
 *   windings), since the area of `F` is what the formula's constants stand for.  This is the clip part of a stroke, not a
 *   fill item: a fill is not changed, and extending the exact step to fills is a separate decision.
 * - **UC9 Limits, refusals, work, memory.**  `GSHCPU_Limits.max_exact_fragments` (a `uint64_t`; 0 is 32768, any other value
 *   is used as given up to `2^20`, which a larger value is ::GSHCPU_ERR_INVALID for at sink creation; `UINT64_MAX` is not
 *   "no cap": the step is superlinear in the pieces of a pixel) bounds the pieces of one part in one cell (the raw pieces, as
 *   the rasterizer finds them, whatever UC13 makes of them).  A cell with more, or a cell whose pieces do not reproduce the
 *   rasterizer's sums, is the sink's resource limit: the item's block is skipped (strict stops with the finished prefix), as
 *   the cell cap (SC6).  No depth of winding is a limit (UC15, UC20).  The pieces are counted over what the whole render draws
 *   of the item, so a damaged render refuses what the whole one does, unless the outline has too few points for any cell to
 *   hold that many (a piece is at most one for each edge, an outline has fewer edges than twice its points).  The step's work
 *   counts toward SC11's: a piece is one unit, each slab `6 n' + 8` (the carry, the repair, the gaps, the ends), each meeting
 *   `ceil(log2 n') + 12`, and when the meetings are counted (UC19) `n' (ceil(log2 n') + 1)` more for each slab; the declared
 *   `cancel_work_bound` adds the most the step may do in an item: its horizontal pieces on cells' left sides (at most one for
 *   each point and each column of the widest raster) and its fragments (at most `max_deposits` of them, which the cap counts)
 *   each at the cost per fragment of the largest cell, whose meetings are at most `n' (n' - 1) / 2` and at most the crossings
 *   budget of UC19.  Memory is a function of the limits and the region only: a count of pieces and a place in the pool for
 *   each cell of a band of the region's width, a pool of pieces (at least 4096, at least one cell's worth), and the scratch of
 *   one cell, counted by `workspace_bytes`, never allocated under `CALLER_MEMORY`; the cells of a band whose pieces do not
 *   fit the pool are made exact a window at a time.
 * - **UC10 Tiers (AD-13).**  The exact step is one function used by every route: the dense sweep and the sparse composite
 *   (FT-2, where a cell the step changes is noted as a deposit is, so that the sweep visits it).  Both are bit-equal to
 *   the general tier by construction and by the gate that holds each to it.  The step is not a tier: the sink with it off
 *   (a test switch, `GSHCPU_RENDER_TIER_EXACT`) is the sum of F4's stopgap, which the gates are shown to fail.
 * - **UC11 Determinism (AD-17, AD-22).**  The result of a cell is a function of its pieces and the cover to its left, in
 *   `int64_t` and `uint64_t` within the bounds above, with no floating point and no division by anything unchecked; it does
 *   not depend on the thread, the cache, the band, the damage or the order the pieces were found in, so the stroke goldens are
 *   the same on every target.
 * - **UC12 The bracket and `o`.**  The bracket keeps `e` (2/255) and takes `o = 1/255`.  A second instrument, the union gate
 *   (`tests/stroke_gate.h`), tests exactness itself: an independent reference computes the area of the union of the pieces
 *   (the polygon the rasterizer covers) per pixel by a scanline sweep of the whole outline in `long double`, and the
 *   coverage must be within a level and a half of it at every pixel of every case.  It exists because the bracket's
 *   `D(tau)` is a sixteenth of a pixel and hides an error of a few levels on a thin feature.
 * - **UC13 Weighted fragments.**  After the pieces of one part in a cell are sorted by segment and a piece and its reverse
 *   cancelled, the pieces left over one segment are `nu` equal copies in one direction: one fragment of multiplicity `mu =
 *   |nu|` with winding step `eps s mu`.  This is exact (a winding is a sum over edges, and coincident edges enclose no
 *   width).  `n'`, the distinct fragments, is at most the pieces `m`; the cap of UC9 counts the pieces, the work and the
 *   memory of the walk are in `n'`.  A partial overlap of unequal collinear opposite pieces is not cancelled: the sweep is
 *   exact with it.
 * - **UC14 The sweep of a slab.**  The list of fragments is the order the previous slab ended in, without the fragments that
 *   end at the slab's top, the ones that start there sorted and merged in, and the result repaired (a pair that met exactly
 *   at the top is ordered by slope, the order just after the meeting; the repair sorts the list again, which costs no more
 *   than a bundle's exchanges would).  A fragment has the coefficient `c = Omega_left - Omega_right` of the two gaps beside
 *   it, `Omega` the weight of UC15.  Two neighbours meet inside the slab iff `D > 0` and `0 < N < H D` (UC5), and the pending
 *   meetings of neighbours are held in a binary heap by depth, ties by place; with none the slab is crossing-free and no heap
 *   is built.  At a meeting the neighbours exchange places, only the gap between them changes (every other gap's prefix has the
 *   same set of steps), the two coefficients change by `+-d`, `d = Omega_0 + Omega_2 - Omega_1 - Omega_1'`, and the pairs on
 *   either side are new neighbours.  Two fragments meet once, so a slab ends in at most `n' (n' - 1) / 2` meetings and a cell,
 *   whose slabs share no pair, in at most that many; meetings at equal depth may be taken in any order (the result is the
 *   same: the exchanges telescope).
 * - **UC15 The area and its arithmetic.**  In a slab the integral of the weight over a row is `64 Omega_n + sum_f c_f x_f`
 *   (summation by parts over the fragments), and `c_f` is constant between the meetings that move `f`.  With `F_f(t) = (2 a t
 *   + dx t^2) / (2 dy)`, the exact antiderivative of `x_f`, and the depths `t_k` at which `c_f` changes, `integral c_f x_f =
 *   sum_k F_f(t_k) (c_{k-1} - c_k) + c_r F_f(H)`: the area of the cell is the sum over its slabs of `64 H Omega_n`, a term
 *   `d (F_p - F_q)` at each meeting of `p` and `q`, and `c_f F_f(H)` at each slab's end.  No depth is rounded.  `Omega` is,
 *   in doubled units, twice the predicate of the region: for a part's own overlap "the winding is not zero" (the shared area
 *   UC6 is the dense value less this area), for the clip's region its rule (non-zero, or odd), for UC8 the predicate of its
 *   region.  `2^30 F` at a meeting is `(Q << 30) + ((Rm << 30) / Den)`, `Q = Num / Den`, `Rm = Num % Den`, `Num = 2 a N D +
 *   dx N^2 < 2^46`, `Den = 2 dy D^2 <= 2^33`, so `Rm << 30 < 2^63` and the value is exactly `floor(2^30 F)`; at the slab's end
 *   `((2 a H + dx H^2) << 30) / (2 dy)` from a numerator below `2^20`.  Orders and meetings compare products below `2^32`.  A
 *   coefficient is `0` or `+-2`, a second difference `d` at most `4` in magnitude, and the sum `acc`, `2^30` times the area in
 *   doubled units, is a `uint64_t` with defined wrap-around that is exact (the area is below `2^13`).  Each `F30` is low by
 *   less than one unit of `2^-30`, so the sum is within `8 K + 2 E` units of the exact one (`K` the meetings, `E` the
 *   evaluations at slab ends, `E <= S n'`); that is at most `0.002` of a doubled unit at a hundred thousand meetings and
 *   `0.0078` at `2^20`.  The area is rounded to a doubled unit with ties up, as the earlier step rounded: the quantity
 *   rounded is the area of UC8's regions, and for a part's own overlap the shared area, with an offset of that bound added
 *   so that a cell whose exact value is a tie (lattice geometry makes them common: a thin feature of whole units is a half
 *   unit of area) rounds up as it exactly does and not either way by the sign of the error.  The coverage differs from the
 *   rounding of the exact area only where the exact value lies within the bound of a boundary.
 * - **UC16 Checks.**  (i) The divisibility of UC4 (a remainder is `EXACT_IDENTITY`: a refused render, never a guess).  (ii)
 *   The doubled areas of the cell's pieces, summed in the rasterizer's own formula (the cover is the sum of the heights, the
 *   area the sum of the height times the two `x` ends), equal the cell's dense value as integers, with a tolerance of zero: a
 *   piece missed or invented is found.  Both run on every cell and cost `O(n')`.  (iii) A test switch makes the step check
 *   that each slab's order after its meetings is sorted by the order at its end, and that the meetings it made are the
 *   meetings the count found: a missed meeting leaves an inversion.
 *
 * - **UC17 The default and the cost of a cell (amends UC9).**  `GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS` is 32768, at
 *   least twice the densest pixel measured over every drawing the suite keeps: 13363 raw pieces of a part (a line of text
 *   stroked 22 units wide with round joins and caps, the three words measured), and 4294 in the benchmark's stroke scenes.  A
 *   cell at the cap and the default budget costs at most 74.4 million units of `W_cell` (about 0.2 s at the measured 3 ns a
 *   unit), and the scratch of one part is 2.4 MB (72 bytes a fragment).  The cost of a cell is
 *   `W_cell(m, n', K) <= m + S (6 n' + 8) + S n' (ceil(log2 n') + 1) + K (ceil(log2 n') + 12)` in SC11's units, `K <= n' (n' -
 *   1) / 2`, which is quadratic in the pixel's fragments and is cut by UC19's budget to a number of meetings that does not
 *   depend on the pieces.
 * - **UC19 The crossings budget.**  `GSHCPU_Limits.max_exact_crossings` (a `uint64_t`; 0 is `2^20`, any other value is used
 *   as given up to `2^32`, which a larger value, `UINT64_MAX` included, is ::GSHCPU_ERR_INVALID for at sink creation) bounds
 *   the crossings of one cell, the pairs of its fragments that meet strictly inside a slab, summed over its slabs.  A cell
 *   with more is the sink's resource limit, as UC9's, decided over what the whole render draws of the item.  The count is
 *   known without the sweep: a pair meets inside a slab iff its order at the slab's top differs strictly from its order at the
 *   slab's end, so `K` is the sum over the slabs of the inversions between the two orders, a merge count of `O(n' log n')` for
 *   each slab, and the sweep then makes exactly `K` exchanges.  The count runs before the sweep and only for a cell that can
 *   pass (`n' (n' - 1) / 2` above the budget), stopping once it has: a refused cell costs the count and not the budget.  The
 *   cells of the walk outside the drawn rectangle (damage) whose pieces could pass the budget are collected as the drawn
 *   cells' are and counted too, the three parts of an aligned stroke together as UC8 makes them exact, so that a damaged render
 *   refuses what the whole render refuses, and the refusal never depends on cache state.  The default `2^20` is more than three
 *   times the most any pixel measured makes (333496), and a cell at the budget takes about a tenth of a second.
 * - **UC20 The winding is no limit.**  The earlier step refused a winding past 255 in magnitude anywhere in a cell, and a line
 *   of stroked text reaches 494 (475 for one word).  The weight of UC15 is the indicator of the region, not a function of the
 *   winding's magnitude, so the step has no bound on it: a winding is held in an `int32_t`, a cell's gaps are at most its
 *   pieces and the winding at the top of its left side at most `2^28` (an outline of a library-bounded size never has more),
 *   and a cell past that is the resource limit.
 * - **UC21 The rounding at exact ties.**  The step returns the exact area of a part (or, for UC8, of the clip) rounded to
 *   the nearest value of the lattice's coverage, and an area that lies exactly half way rounds up.  The lattice makes such
 *   ties common (a feature of whole units is a half unit of area), and the earlier step rounded them by the sign of its own
 *   error, which was neither a rule nor the same for equal inputs; a drawing that held a tie can differ from the earlier step
 *   by one level of coverage, and by nothing elsewhere.  The earlier step is kept in the tests as the oracle of the dual
 *   render, which allows a difference of one level at an exact tie and none anywhere else.
 *

 * ### Memory (S9)
 *
 * A render takes one block: the layers (32 bytes a pixel of the region for the
 * root and for each level of group nesting), the touched map and the coverage
 * scratch (a byte a pixel each), a byte a pixel for each level of clip nesting,
 * the cell accumulator of ::GSHCPU_RENDER_BAND_ROWS rows, the walk, and for
 * gradients a table of twice the gradient fills of the expansion (a picture's
 * counted where it is drawn) and room for their stops, and, only when the expansion holds a stroke that aligns, three
 * more arrays of a byte a pixel (the aligned part, the clip path's coverage and their composition, SC5), and, only when it
 * holds a stroke, the exact step's memory (UC9: a count and a place for each cell of a band as wide as the whole render's
 * region, a pool of pieces and the scratch of one cell, sized by `max_exact_fragments`).  The
 * geometry of each item (its device path and flattened path) is allocated from
 * the sink's allocator, or with `CALLER_MEMORY` from the rest of the workspace,
 * which is sized by converting every item once: the exact need is the block plus
 * the most any item takes.  A `CALLER_MEMORY` render sizes itself inside its
 * workspace, so one whose workspace is large enough allocates nothing at all; a
 * workspace one byte smaller (or any smaller, or none) is ::GSHSCENE_ERR_LIMIT
 * with nothing allocated and no need stated, and
 * ::gshscene_sink_workspace_bytes gives the need (S9).  The block is sized for the region, but a render reads,
 * clears and writes only the part of it its units reach: each layer is made
 * ready (a group's cleared, the root's read from the target) over the rectangle
 * its units have drawn on so far.  An empty list touches none of it.
 *
 * ### Limits
 *
 * The region drawn is at most `max_dimension` pixels wide and tall, at most
 * `max_bytes` pixels in all, and lies within ::GSHCPU_MAX_DIMENSION_CEILING
 * pixels of the origin; past either it is ::GSHSCENE_ERR_LIMIT before anything is
 * drawn.  `max_cache_bytes` bounds the stroke cache (SC7); `max_exact_fragments` the pieces of a stroke part in one
 * pixel (UC9).
 *
 * ### Hash and dump of a render (AD-21)
 *
 * ::gshcpu_render_hash is FNV-1a 64 in `shape-geometry`'s spelling (hash.h):
 * `u32` format version 1, `u32` kind `REND` (`0x444e4552`), `u32` the pixel
 * format (::GSHCPU_Format), `i64` x0, y0, x1 and y1 of the extent, then the
 * pixels of each row in order, left to right, each channel in R, G, B, A order:
 * a `u8` for an 8-bit sample and a `u32` (the IEEE bit pattern) for a `float`.
 * The stride is not hashed.  ::gshcpu_render_dump writes
 * `gshcpu.render 1 format=<name> extent=X0,Y0,X1,Y1 hash=<16 hex digits>` and
 * then one line per row, `r <y>` followed by each pixel as ` ` and its channels'
 * hexadecimal digits (2 a channel for 8 bits, 8 for a `float`'s bit pattern).
 */

#ifndef GHOTI_IO_GSHCPU_RENDER_H
#define GHOTI_IO_GSHCPU_RENDER_H

#include <ghoti.io/shape-cpu/core.h>
#include <ghoti.io/shape-cpu/coverage.h>
#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/image/raster.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/diff.h>
#include <ghoti.io/shape-scene/sink.h>

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The bound of a gradient's evaluation against the gradient oracle (S11): each component of the straight color in the
 * interpolation space, and `t`, within this times `max(1, |v|)` of the oracle's value.
 */
#define GSHCPU_RENDER_GRADIENT_TOLERANCE 1e-12

/** Rows of the cell accumulator: coverage is accumulated this many rows at a time. */
#define GSHCPU_RENDER_BAND_ROWS 16u

/** @brief The pixel formats the sink writes.  The values are pinned (they are hashed). */
typedef enum GSHCPU_Format {
  GSHCPU_FORMAT_RGBA8_STRAIGHT = 1,        ///< RGBA, 8 bits a channel, straight alpha.
  GSHCPU_FORMAT_RGBA8_PREMULTIPLIED = 2,   ///< RGBA, 8 bits a channel, premultiplied.
  GSHCPU_FORMAT_RGBAF32_STRAIGHT = 3,      ///< RGBA, `float` a channel, straight alpha.
  GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED = 4, ///< RGBA, `float` a channel, premultiplied.
  GSHCPU_FORMAT_COUNT                      ///< Closes the enum.
} GSHCPU_Format;

/**
 * @brief The format of @p raster, if the sink writes it.
 *
 * @return A ::GSHCPU_Format, or 0 for NULL and for every other format.
 */
GSHCPU_API GSHCPU_Format gshcpu_format_of(const GIMG_Raster * raster);

/** @brief Name a format ("rgba8", "rgba8-premultiplied", "rgbaf32", "rgbaf32-premultiplied"), or "unknown format". */
GSHCPU_API const char * gshcpu_format_string(GSHCPU_Format format);

/**
 * @brief Make the CPU sink.
 *
 * @param alloc The allocator of the sink and of every render's memory, or NULL for the default; copied.
 * @param scene_limits Only `max_jobs` is read (shape-scene's contract layer); NULL for the defaults.
 * @param limits The sink's own limits (see the file comment); NULL for the defaults.  Copied.
 * @param out The sink; written only on success.  Destroy it with `gshscene_sink_destroy`.
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_INVALID (NULL @p out, nonzero reserved bytes in @p limits); ::GSHCPU_ERR_OOM.
 */
GSHCPU_API GSHCPU_Result gshcpu_sink_create(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, GSHSCENE_Sink ** out);

/**
 * @brief One task of a parallel step: @p index is in `[0, count)` of the ::GSHCPU_Executor_Start_Fn call that started it.
 */
typedef void (*GSHCPU_Task_Fn)(void * ctx, size_t index);

/**
 * @brief The caller's way to start tasks on its own threads, and to wait for them.
 *
 * `start` begins calling `task(ctx, i)` for the indexes in `[0, count)` on threads other than the calling one and returns at
 * once, without waiting for any of them; it may begin only some of them, or none (a refusal).  `wait` returns when every call
 * `start` began has returned.  The sink calls `start` at most once before each `wait`, from one thread at a time, and does
 * not touch what the tasks use between the two; the strokes a task did not draw are drawn by the calling thread, so a
 * refusal costs time and never the picture.  Two calls with different indexes may run at the same time; the sink gives each
 * its own memory.  Several sinks may share one executor, so a `wait` may also wait for the others' tasks.
 * @return 0 on success; nonzero when nothing was started.
 */
typedef int (*GSHCPU_Executor_Start_Fn)(void * user, size_t count, GSHCPU_Task_Fn task, void * ctx);

/** @brief Wait for the tasks the matching ::GSHCPU_Executor_Start_Fn began (see there). */
typedef void (*GSHCPU_Executor_Wait_Fn)(void * user);

/** @brief ::GSHCPU_Executor.threads past this is ::GSHCPU_ERR_INVALID. */
#define GSHCPU_EXECUTOR_MAX_THREADS ((size_t)256)

/**
 * @brief Threads for a sink to draw with, which the sink does not make: it starts @p threads - 1 tasks at a time and draws with the calling thread as well.
 *
 * A render of strokes with the fast stroke coverage (::GSHCPU_STROKE_COVERAGE_FAST) onto an 8-bit premultiplied target, with
 * nothing else in it, queues its strokes (copying their outlines) and draws them over stripes of the target's rows; the
 * strokes of a stripe go in the order they were given and a stripe is written by one task, so the picture is the same bytes
 * for any @p threads and any executor.  While the tasks draw one batch of strokes the sink queues the next, so the thread that
 * calls the sink is preparing strokes while the others draw them.  Any other render, and a render with ::GSHSCENE_RENDER_CALLER_MEMORY (the queue and the
 * tasks' memory come from the sink's allocator), is drawn by the calling thread alone.
 */
typedef struct GSHCPU_Executor {
  size_t threads;               ///< Threads in all, the calling one among them, at most ::GSHCPU_EXECUTOR_MAX_THREADS; below 2, nothing is spread.
  GSHCPU_Executor_Start_Fn start; ///< Begins tasks; NULL for no executor.
  GSHCPU_Executor_Wait_Fn wait;   ///< Waits for them; required with @p start.
  void * user;                  ///< Handed to both.
} GSHCPU_Executor;

/**
 * @brief ::gshcpu_sink_create with an executor (see ::GSHCPU_Executor).
 *
 * @param executor Copied; NULL is none.
 * @return As ::gshcpu_sink_create; ::GSHCPU_ERR_INVALID also for `threads` past ::GSHCPU_EXECUTOR_MAX_THREADS.
 */
GSHCPU_API GSHCPU_Result gshcpu_sink_create_with_executor(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, const GSHCPU_Executor * executor, GSHSCENE_Sink ** out);

/** @brief Nonzero if @p sink was made by ::gshcpu_sink_create. */
GSHCPU_API int gshcpu_sink_is(const GSHSCENE_Sink * sink);

/**
 * @brief The hash of a raster's pixels in @p extent, in the spelling of the file comment.
 *
 * @param extent The rectangle, inside the raster; NULL for the whole raster.
 * @param out The hash; written only on success.
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_INVALID (NULL, an extent that is empty or leaves the raster);
 *   ::GSHCPU_ERR_UNSUPPORTED (a format the sink does not write).
 */
GSHCPU_API GSHCPU_Result gshcpu_render_hash(const GIMG_Raster * raster, const GSHSCENE_Rect * extent, uint64_t * out);

/**
 * @brief Write the dump of a raster's pixels in @p extent (see the file comment).
 *
 * @return As ::gshcpu_render_hash, and ::GSHCPU_ERR_IO when the stream fails (including at the flush).
 */
GSHCPU_API GSHCPU_Result gshcpu_render_dump(const GIMG_Raster * raster, const GSHSCENE_Rect * extent, FILE * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_RENDER_H */
