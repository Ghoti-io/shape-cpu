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
 * by the planning pass, never drawn as another); a soft mask, every mode, transfer, coefficient set and space
 * of it (`GROUP_SOFT_MASK` and every `GSHSCENE_MASK_NATIVE_*` bit, MC1); and PDF's other group fields, a group that is non-isolated,
 * knockout or both (`GROUP_NON_ISOLATED` and `GROUP_KNOCKOUT`, shape-scene's T1 to T7, KC1), with or without a mask, so the planning
 * pass refuses none of them;
 * `CAN_BLOCK` (it completes inside `begin`), `CALLER_MEMORY`, `CLIP_AA`,
 * `BLEND_SPACES`, `READBACK` and `GLYPHS_FROM_FONT` (story 14a: a run from a font
 * is drawn through its face's provider by shape-scene's route, glyph.h, the fast
 * tier wherever the route returns it); every gradient kind and `GRADIENT_SPACES`
 * (color-paint story P2: linear, radial and sweep, each in its stated
 * interpolation space); every image filter, edge mode and format and `IMAGE_FILTER_SPACES` (masks-images-patterns story I3, the
 * Images section below); `STROKE_EXPAND` and no stroke feature native (SC1); no `THREAD_AFFINE` (what it holds
 * across renders, the cache of SC7, is guarded by one lock, so renders on distinct surfaces may run at once) and no
 * `PREPARE`.  It claims `ANTIALIAS_NONE` (AC1 to AC6, below).  Cancel is `BETWEEN_UNITS` (each unit
 * of the list's expansion); the work between two checks is counted in
 * `"cells+points+pixels+texels"` and its bound (SC11) is, for an aligned stroke,
 * `3 max_points + max_deposits + 2 max_bytes + elements * GSHGEOM_STROKE_MAX_DEPTH` plus the exact step's bound and
 * `max_image_work` of the sink's ::GSHCPU_Limits (each
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
 *   is not touched.  A fill that `gshscene_paint_notice` names (G10, G11) is
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
 *   into the group's layer and the group's blend mode acts at its pop.  In a non-isolated or knockout group it is one element of
 *   a general layer (KC2), and its coverage is that element's shape; a soft-masked group is drawn (MC1).
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

 * ### The general tier of a union clip (shape-exact-primitives story A3b, CG1 to CG12)
 *
 * The sink claims ::GSHSCENE_CAP_CLIP_UNION_GENERAL as well as ::GSHSCENE_CAP_CLIP_UNION, so it draws every union clip (shape-scene's
 * `clip_union.h`).  A union whose members the planning predicate shows disjoint is the fast tier's (FT-U1: each member's area added into one
 * plane, quantized once); any other is the general tier's, which this section defines.  The rules are in the units of CU2: an area is a whole number
 * of 1/8192 pixel, a pixel's byte is `min(255, area >> 5)`.
 *
 * - **CG1 What the tier is.**  The exact area of the union of the members' regions on the 26.6 lattice, by the exact step (UC4 to UC7, UC13
 *   to UC16) taken over the pieces of several members at once.  No boolean is formed and no geometry is new: a region is the points where
 *   a member's rule holds at the member's winding of the pieces the scan converter cuts (UC2), and the area is computed in integers from
 *   the pieces of each pixel.
 * - **CG2 Groups.**  The members are partitioned into groups, the connected components of the relation "the interiors of the two members' device boxes meet"
 *   (CU16's overlap, formed by the same `gshscene_clip_union_member_box`); a group is named by its least member.  An empty box, or one without
 *   width or height, meets nothing.  The comparisons of every pair are billed (CG11).  With `GSHCPU_RENDER_FORCE_ONE_GROUP` the members are one group (a test
 *   switch: it makes the exact step compute the union of members the predicate shows disjoint, which it must give as their sum).
 * - **CG3 The area of a pixel.**  The sum over the groups, held at 8192, of the group's area.  A group of one member has its own area `a_i` (CU2's
 *   number: the member's accumulated winding area reduced by its rule, the saturating sum of AD-12 where its contours overlap themselves inside
 *   the pixel).  A group of several has the **floor** of the exact area of the union of its members' regions in the pixel, each region the points
 *   where the member's own rule (nonzero, or odd) holds at the member's own winding, whatever the orientation of its contours.  The floor is
 *   CU2's own and is taken before the sum, which is the same as taking it after (the other terms are whole numbers).
 * - **CG4 Equality with the fast tier.**  When the predicate shows the members disjoint every member is a group of its own, and the sum is the
 *   fast tier's, bit for bit, whatever each member does inside a pixel; the gates draw hundreds of generated unions both ways, and the same
 *   unions with the members as one group, whose exact union must be that sum wherever no member overlaps itself.
 * - **CG5 The step for a group.**  `GSHCPU_EXACT_GROUP`: up to 1024 parts, the members that have pieces in the pixel, each with its pieces, the cover to the
 *   left of the pixel and its dense area (UC4), and a rule of its own.  The slabs, the order, the meetings, the heap and the integral are UC5, UC14
 *   and UC15 unchanged; what differs is the weight of a gap: twice the indicator of "some part's rule holds at that part's winding in the gap".  A gap
 *   keeps the number of parts inside it and, for the part of the fragment on either side of it, that part's winding there; an exchange of two
 *   neighbours changes the one gap between them, in constant time, from those two windings.  The windings of the parts are carried once from the left of the
 *   pixel (UC4 for each part).  No winding is bounded but by UC20's `2^28`.
 * - **CG6 The arithmetic and its bound.**  The sum of the `2^30` times the area is UC15's, low by less than `8 K + 2 E` units of `2^-30` for `K`
 *   meetings and `E` evaluations at slabs' ends.  The step adds that bound and one to it and drops the fraction, so an area that is exactly a whole number
 *   of units comes out whole, and an area within the bound below the next whole number may come out one unit high (the byte is then one level high only
 *   when that unit is a multiple of 32).  The bound is `(8 K + 2 E + 1) 2^-30` of a unit: under `2^-12` up to `2^17` meetings (the vectors' pixels have
 *   under a hundred), and the meetings of a group's pixel are held at `2^25` whatever UC19's budget (`GSHCPU_EXACT_GROUP_CROSSINGS`), which keeps the bound
 *   under half a unit for the largest pixel, so that the floor is never a unit off by more than the rule says.  The oracle commits a vector only where
 *   the exact area is a whole unit or at least `2^-12` below the next.  The step has no `long double`, no 128-bit type and no floating point.
 * - **CG7 The passes.**  A group of several members is swept three times.  The first counts each member's pieces in every pixel (the marks, seven bits
 *   held at 127).  The second adds each member's dense area into the group's plane, sets the high mark of a pixel a member without a piece in it covers
 *   whole, and keeps, for every pixel the marks say holds two or more pieces of the group in all, the member's pieces there with the cover and the dense
 *   area.  The third sorts the kept records by pixel and member and, for each pixel, makes the area exact from the pieces of its members (or
 *   whole, when the high mark is set); the plane's value there is replaced by it.  Every other pixel keeps the dense sum held at 8192, which is exact:
 *   with no piece the members are whole or empty, and with one piece the winding takes two adjacent values (UC3).
 * - **CG8 Memory.**  A function of the list, the target and the limits: the plan (a box, a rectangle and three indices for each member); an accumulator wide
 *   as the union over the whole render; the plane (two bytes a pixel) over that; and, when a group has several members, the group's plane and marks over the
 *   most pixels of one, the exact memory (`gshcpu_exact_group_bytes`: the scratch of a pixel for `max_exact_fragments` pieces in all and a record of
 *   steps and windings for each part), and `max_union_pieces` records of 32 bytes and pieces of 4.  It is taken from the item arena in one order by the push and by the
 *   measure, so a list with no union has the memory, work and hashes it had, and one with a union asks for exactly what the push takes (under
 *   `CALLER_MEMORY` nothing is allocated).
 * - **CG9 Limits and what is refused.**  Each is the sink's resource limit for the clip push (`RESOURCE_LIMIT`), the block skipped and strict failing there; the union is never
 *   drawn with a member left out, and the work is over the whole render's rectangle of the union whatever the damage, so a damaged render refuses what the whole
 *   one does (S4).  (a) A member whose box cannot be formed, or whose cells pass `max_deposits`; (b) a member with more than `max_exact_fragments`
 *   pieces in a pixel, and a pixel in which the members together hold more than that (the scratch of CG8); (c) a pixel with more crossings than
 *   `max_exact_crossings` (UC19) or than `2^25` (CG6); (d) more pieces kept than `GSHCPU_Limits.max_union_pieces` (default 2^17, ceiling 2^22) in a
 *   group; (e) comparisons past `max_union_tests` (CG11); (f) a group whose rectangle has more than `2^32 - 1` pixels (a kept record names its pixel by
 *   a 32-bit index).  Not refused: a rule for each member (nonzero and even-odd mix freely), any orientation, a member that overlaps itself, a depth of
 *   overlap, a group of up to the store's `max_clip_members` (1024).  Still refused, and documented: a glyph run from a font as a member
 *   (`ERR_UNSUPPORTED` when the union is interned, CU-D8).  A union in a pattern's tile is formed in each copy by this tier, its members cut to the cell, and a
 *   limit of it refuses the use painted with the pattern (CU21: the area is the plane `union_area` forms, the byte and the remainder as a single clip's, the copies'
 *   areas add as PC4 says, the predicate is billed once for a pass, and a tile that holds one is not drawn by the period window).
 * - **CG10 Tiers, caps, counters.**  `GSHCPU_RENDER_TIER_UNION` (FT-U1) and `GSHCPU_RENDER_TIER_UNION_GENERAL` are separate switches: the sink claims `CLIP_UNION`
 *   while either is on and `CLIP_UNION_GENERAL` while the second is; with the first off and the second on every union is the general tier's, the switch that
 *   forces it.  The counters are `union_general` (pushes), `union_groups` (groups of several members made exact), `union_exact` (pixels made exact) and
 *   `union_pieces` (pieces kept); `union_fast`, `union_sweeps` and `union_tests` are CU22's.
 * - **CG11 The bill and the declared work.**  A union the general tier draws costs the predicate's comparisons and `n (n - 1) / 2` more for its groups, billed together against
 *   `max_union_tests` for every union push of the render in unit order, whatever the damage.  The declared bound of the work between two checks gains
 *   the most one union push does: each of the store's `max_clip_members` swept twice, two passes over the pixels, and the exact step over their pieces.
 * - **CG12 The check.**  The oracle of shape-scene (`tools/clip-union-oracle.py`) in exact rationals over the pieces; its vectors are drawn byte for byte
 *   by the default tiers, by the general tier forced, and, for the vectors whose members do not overlap themselves, by the exact step on one group; the
 *   exact step of a stroke part (UC1 to UC7) is a second engine for positive polygons, equal but where the area is within half a unit below a level.
 *
 * ### Images (masks-images-patterns story I3, IC1 to IC14; shape-scene's IP1 to IP18)
 *
 * An image paint is drawn on a fill, on a stroke (the paint sampled in the item's local space, with the stroke's coverage) and
 * inside a picture.  This sink is the reference the others are measured against (AD-18): the sampling definition below is
 * normative for it, and a GPU sink's tolerance, a writer's `APPROXIMATED` and a front end's filter mapping rest on what it does.
 *
 * - **IC1 What it claims.**  Every filter, every edge mode, every image format (gray, RGB and RGBA at 8, 12 and 16 bits and as
 *   floats, both alpha forms, and the alpha-only mark) and ::GSHSCENE_CAP_IMAGE_FILTER_SPACES, so an image paint never receives
 *   the reasons `IMAGE`, `IMAGE_FILTER` or `IMAGE_FILTER_SPACE`.  What it can refuse while drawing is `RESOURCE_LIMIT` (IC9).  The
 *   work measure is `cells+points+pixels+texels`, and the declared cancel bound includes `max_image_work`: cancellation is
 *   between units, and one image item can take that long.
 * - **IC2 The sample, and the region.**  Pixel `(x, y)` takes its value at the device point `q = (x + 1/2, y + 1/2)` for `NEAREST`,
 *   `LINEAR` and `CUBIC`, and over the device square `[x, x + 1] x [y, y + 1]` for `AREA`, weighted by its coverage and its clips
 *   as a solid fill's.  One sample or one region a pixel: a damaged render equals the whole render inside the damage.
 * - **IC3 The chain, the inverse, the filter.**  The chain `T I_1 .. I_k M G` (the paint's map the leaf) is composed once a fill by
 *   `gshgeom_map_compose_chain` and inverted once as a gradient's is (the 2x2 part scaled by the power of two of its largest entry,
 *   the determinant an exact sum of exact products rounded once): `p = L^-1 (q - t)`.  The filter is chosen once a fill, by
 *   `gshgeom_map_stretches_every_direction` of the composed map: `magnify` where it returns true, `minify` otherwise (an exactly
 *   unit scale minifies).  A composed map whose inverse is not finite or whose determinant rounds to zero is the sink's resource
 *   limit.  A fill that `gshscene_paint_notice` names (IP11) is skipped.
 * - **IC4 Decode.**  A sample of `n` bits is `v / (2^n - 1)` by one correctly rounded division, a float is the float widened and
 *   saturated to `[-2^64, 2^64]`, a straight texel is multiplied by its alpha (one product a channel) and a premultiplied one
 *   taken as stored: `gshscene_image_decode`'s values bit for bit, read in place by rows and never copied.  Where the filter space
 *   differs from the image's (IP5) each texel is converted by N2 and N3 (un-premultiplied, decoded, multiplied by the matrix,
 *   encoded, saturated, premultiplied again), and the filtered value is converted to the layer's space the same way.
 * - **IC5 Edges.**  A tap is an integer: `floor(u - 1/2)` of a double inside the sampling domain.  `PAD` clamps it into the window,
 *   `REPEAT` takes the normalized remainder (`((k - lo) % n + n) % n`, never C's signed `%` alone), `REFLECT` the same with `2n` and
 *   the edge texel repeated, `NONE` is an outside test.  No double is reduced modulo a period, and no texel outside the window is
 *   read.
 * - **IC6 The point filters.**  For each channel of the premultiplied value, no fused multiply-add, one rounding an operation.
 *   `NEAREST` is the texel `floor`.  `LINEAR`: `s = u - 1/2`, `i = floor s`, `f = s - i`, weights `(1 - f, f)`; each of the two
 *   tap rows `r_j = wx0 P(i, j) + wx1 P(i + 1, j)`, then `wy0 r_0 + wy1 r_1`.  `CUBIC`: the four weights of Mitchell-Netravali
 *   (`B = C = 1/3`) by Horner's rule, `((21 d - 36) d) d + 16` and `((-7 d + 36) d - 60) d + 32` over 18, `d = f + 1, f, 1 - f,
 *   2 - f`; each tap row the sum of its four products left to right, then the rows' weighted sum; the result is clamped
 *   componentwise to the least and greatest of the sixteen taps' same component.
 * - **IC7 `AREA`.**  The exact mean of the extended image over the preimage of the pixel square, a parallelogram with a corner
 *   `L^-1 (q_00 - t)` and edges `L^-1 (1, 0)` and `L^-1 (0, 1)`, the same for every pixel.  It is found a row of cells at a time:
 *   the cells the parallelogram covers entirely in a row form one run, whose texels are summed as a run (the cells past a padded
 *   window as a count of the edge texel, the whole periods of a repeating axis as that many sums of the window's row, the rest one
 *   by one), and only the cells its boundary crosses are clipped (the area of the intersection of two convex polygons, by
 *   Sutherland-Hodgman, in a fixed order).  A parallelogram with axis-aligned edges takes a cell's weight as the product of its
 *   overlap in x and its overlap in y, and a run of cells it crosses entirely by its row's overlap once; and because that sum is
 *   separable, the rows it covers across are not visited one by one however many there are: of a repeating axis the whole periods are as
 *   many sums of the window's rows (twice as many in a reflecting axis), of a padded axis the rows past the window are a count of its
 *   edge row, of a bare axis they are nothing.  Sums are compensated
 *   (Neumaier) and in a fixed order, the row of the window is summed pairwise over blocks of 1024 texels, and the mean is divided
 *   by the exact area of the parallelogram once, at the end; so the mean is within `2^-40` relative of the exact mean and the same
 *   on every target.  A constant image gives that constant within the tolerance; a unit-aligned unit square gives the texel exactly.
 *   The sum of a row of the window is kept for the pixels that follow in a table of 32 rows on the stack, direct-mapped on the
 *   row: a hit is the same doubles as a miss, and the work a pixel is counted at is the same either way.
 * - **IC8 To the layer, and the write.**  The filtered value taken to the layer's space is multiplied by the paint's alpha
 *   multiplier on all four components and applied with `SRC_OVER` weighted by the coverage and the clips (a pixel of `m8 = 0` is
 *   not touched); one rounding at the write, no dithering.  For an alpha-only image the filter acts on the one number `a'` and the
 *   source is the tint's straight color (resolved once a fill, converted once to the layer's space) premultiplied by `m a_t a'`.
 * - **IC9 Limits.**  `max_area_pixel_work` bounds the texels and cells one pixel of `AREA` visits and `max_image_work` the visits
 *   of one item over what the whole render draws of it, `UINT64_MAX` no cap.  Both bounds are functions of the composed map, the
 *   window and the rectangle alone, met before any pixel is drawn, so a damage, the caller's memory and the thread change nothing
 *   about which items are refused.  With `cx` and `cy` the columns and rows of cells a preimage can touch (`ceil` of its extent in
 *   each axis, plus two), `n` the window's width and `run = min(cx, 3 n)`, a pixel visits at most
 *   `cy run + 4 (cx + cy) + 8 cx + 2 cy + 8` (the rows' runs, the cells the boundary crosses, the rows that hold a vertex, and
 *   the rows' own steps); the item's bound is that times the pixels of its whole-render rectangle.  A preimage with axis-aligned edges
 *   visits at most `rows (run + 3) + 8`, where `rows` is the lesser of `cy` and what IC7 leaves to visit: with `m` the window's height,
 *   `3 m + 2` for a repeating or reflecting axis and `m + 4` for a padded or bare one, so that its bound does not grow with the
 *   minification.  A preimage wider than `2^33`
 *   texels is past the domain.  Past a bound, past the sampling domain (the whole-render rectangle maps outside `[-2^32, 2^32]`
 *   texels on an axis) or under a map whose inverse fails, the item is refused while drawing: `RESOURCE_LIMIT`, its block skipped,
 *   strict stopping with the finished prefix.  `NEAREST`, `LINEAR` and `CUBIC` take at most sixteen taps a pixel and need no limit.
 *   Each limit passes exactly at its bound and refuses one past it (a test asserts them by the bound).
 * - **IC10 Tiers (AD-13).**  Each is the general tier's pixels by construction, is counted, and can be forced off by a test:
 *   *FT-1* an integer translation with a unit linear part is a copy of the texel under the pixel, for `NEAREST`, `LINEAR` and
 *   `AREA` (`CUBIC` is not one: its weights at a texel's center are 1/18, 8/9 and 1/18); *FT-2* a map with one nonzero entry in each
 *   row and column (a scale, a mirror, a quarter turn) finds the taps of the coordinate that depends on the pixel's row once for
 *   the row; *FT-3* an 8-bit sample is decoded through a table of the doubles the division gives.  `AREA` has no separable tier.
 * - **IC11 Memory.**  An image fill needs nothing in the block beyond a fill's: the image is read in place, and the sampler, the
 *   decode table (2 KiB) and the row-sum table live in the painter's frame.  Nothing is allocated for an image, with the caller's
 *   memory or without.  A list that holds an image paint is composited in double layers, never byte layers, since an image's value
 *   is no one byte color.
 * - **IC13 Tolerance.**  The sampling primitives equal the image oracle's vectors (shape-scene's installed `image_vectors.h`)
 *   exactly where the oracle says they are exact and otherwise within the tolerance each vector states; a second engine that clips
 *   every cell agrees with `AREA`.
 * - **IC14 Hash and dump.**  `gshcpu_render_hash` and `gshcpu_render_dump` are unchanged.
 *
 * ### Masks (MC1 to MC10; shape-scene's MK1 to MK14)
 *
 * A group whose soft-mask slot names a mask is composited at its pop with the mask's value at each pixel weighing the
 * composite.  The mask's meaning (what its value is at a point, the transfer, the coefficient sets, the frame, what
 * it acts on) is the waist's, in shape-scene's `mask.h` and its documentation: MK2 to MK7.  The rules below are what this
 * sink adds, in real arithmetic unless stated; shape-cpu is the reference every other sink is measured against (AD-18).
 * They are cited by the tests, the planted defects and the vector generator.
 *
 * - **MC1 What the sink claims.**  For a raster target the caps gain `GSHSCENE_CAP_GROUP_SOFT_MASK` and `mask_native` with
 *   every `GSHSCENE_MASK_NATIVE_*` bit, so no mask is refused for `SOFT_MASK` or `MASK_FEATURE`.  A masked group may also be
 *   non-isolated or knockout (KC1, KC10).  Under an isolated parent the opacity and the shape differ for a mask only for the operators
 *   that are not bounded (`DEST_IN`, `SRC`, `SRC_IN`, `SRC_OUT`, `CLEAR`) and for `PLUS` where its sum clamps; under a knockout parent
 *   they differ for every operator (MK7).
 * - **MC2 When and where the mask is drawn.**  At the group's pop, over the **pop region** `P`: the region the pop composites
 *   (the group's dirty rectangle for a bounded operator, the whole region for an unbounded one) met with the reach of the
 *   clips in force around the group and with the damage; nothing is drawn when the render would composite nothing of the
 *   group.  The mask's picture is walked by an expansion of its own under the chain of maps `T I_1 ... I_k Q` (the
 *   target's map, the enclosing instances', then the group's placement) into **one extra frame**: an isolated layer in the
 *   mask's render space that starts at `(0, 0, 0, 0)`, whose units are gone through as any unit is, restricted to `P` (a
 *   unit whose rectangle misses `P` draws nothing).  The clips in force around the group are not in force inside the mask
 *   (the frame's clip base is the clip count at the pop).  The frame is released at the end of the pop.  A pixel's value in the
 *   frame does not depend on `P`: every operation is per pixel, so a mask drawn over a window is the crop of the mask drawn
 *   over the whole region, bit for bit.  A masked group inside a mask picture draws its own frame inside the outer pass.
 * - **MC3 The value.**  For each pixel of `P` with a nonzero clip weight: the mask frame is read at the pixel (zero outside its
 *   dirty rectangle, as a group's layer is), and `v` is the waist's MK2 in `double`, each `sat` of N1 at the point MK2 names it:
 *   `ALPHA`: `clamp01(TR(clamp01(a)))`; `LUMINANCE`: the premultiplied layer converted from the render space to the luminance
 *   space by N3, `r = sat(c_L + (1 - a) B_L)` with the backdrop `B_L` converted into the luminance space once for the pop,
 *   `y = sat(k_r r_r + k_g r_g + k_b r_b)` in that order, and `clamp01(TR(clamp01(y)))`.  The coefficients, the spaces and `TR`
 *   are shape-scene's one resolution and one evaluation (`gshscene_mask_resolve`, `gshscene_mask_transfer`); this sink holds no
 *   transfer and no coefficient table of its own.  `v` is not rounded: the one rounding of a render is the write (D7).  A
 *   vector's `v` is exact only where every step is exact in doubles, and otherwise within `1e-12 max(1, |v|)` of the oracle
 *   before the write (P3's rule), and D7's rule after it.
 * - **MC4 Applying it.**  For a mask acting on the opacity, each component of the group's converted source is multiplied by
 *   `o v` where it was multiplied by `o` (the product `o v` is formed first); for one acting on the shape, the pop's weight
 *   `m8 / 255` becomes `(m8 / 255) v`.  Nothing else in the pop changes: the bounded operator's skip of an all-zero source is
 *   taken after the multiplication, the pixel is marked touched when `apply` is called, and `apply` is the one every pop uses.
 *   With `v = 1` the pop is bit for bit the pop of a group with no mask.
 * - **MC5 Memory is exact.**  The layout counts a mask frame as one more open group at the pop of each masked group, plus
 *   everything the mask's picture needs while it is drawn: the groups and clips it opens (a masked group inside it adds its
 *   own mask frame), its gradient fills and their stops, its strokes, and the walk memory of every picture of a mask being
 *   drawn (`gshscene_expansion_bytes`, one on another), with the frame records and the chain of a mask's unit.  A picture is
 *   counted once, however many masks name it (a memo of the first 64 pictures; a picture past them is counted again where
 *   it is used, which changes the time and not the number).  `workspace_bytes` is exact, a `CALLER_MEMORY` render calls no
 *   allocator and gives the pixels of the render from the sink's own memory, and one byte less is `ERR_LIMIT`.  A list with no
 *   masked group lays out exactly as before: the mask's state is carved only when the list has one.
 * - **MC6 Limits inside a mask.**  An item inside a mask's picture meets the sink's limits (cells, points, the coordinate range)
 *   as any item does, and its cell deposits are counted over its rectangle met with the **whole render's** region, never over
 *   `P` or the damage.  A refusal is `RESOURCE_LIMIT` with the chain `[... , i, j ...]` (`i` the index of the group's push, `j` the
 *   unit inside the picture, deeper through instances), the item is skipped (the mask lacks it), and a strict render stops with
 *   the finished prefix.  Whether a mask is drawn at all is decided by the whole render's pop region, kept beside the
 *   damaged one for the purpose, so a damage-limited render refuses exactly what the whole render refuses, including a mask
 *   whose group lies outside the damage.  A refusal never depends on a tier.  A bound over the whole region over-counts for a
 *   small group (an item whose rectangle is large is charged in full): conservative, and deterministic.
 * - **MC7 Work and cancel.**  The mask pass's work (points, cell deposits, pixel visits of its units) is added to the work of
 *   the group's pop, and the per-pixel evaluation of MC3 counts one visit for each pixel of `P`, so the pop's composite is two
 *   passes over its region.  Cancel is checked **between the mask's units** as between any units: the declared
 *   `cancel_work_bound` is the worst single unit's, which is either a unit of the mask or the pop's composite, and the
 *   pop's two passes over its pixels are within the bound's two-passes term.  The expansion count and the progress units do
 *   not count a mask's units.
 * - **MC8 Fast tiers (AD-13).**  Each is bit-equal to the general tier by construction, can be forced off by a test, is counted,
 *   and a test shows it taken.  *FT-M1, the outside value:* a pixel of `P` outside the mask frame's dirty rectangle has the
 *   layer `(0, 0, 0, 0)`, so `v` is MK5's constant, found once for the pop by the same function and used for those pixels.
 *   *FT-M2, the identity transfer:* the transfer's call is skipped for the identity (the clamp remains).  Both are the general
 *   formula with a constant or an identity folded.  A list holding a masked group is composited in double layers, not the byte
 *   layers of the fast fill coverage: a mask is never quantized to 8 bits between its layer and the write.
 * - **MC9 Cost for groups that set nothing.**  A group with no mask pays no mask code: no frame, no walk memory in the layout, no
 *   per-pixel call.  A list with no masked group has the `workspace_bytes`, the work counts, the pixels and the hashes it had
 *   before masks.
 * - **MC10 State.**  A render keeps nothing about a mask after its pop: no cache, nothing held by `prepare`, `evict` and
 *   `static_bytes` unchanged; renders on distinct surfaces and threads cannot interact through masks.
 *
 * ### Non-isolated and knockout groups (KC1 to KC11; shape-scene's T1 to T7)
 *
 * PDF's two kinds of group are the waist's (T1 to T5, with the installed vectors); this sink composites them.  The rules below say how
 * the reference realizes T3 to T5 and in what arithmetic; they do not restate them.  `f` is an element's shape, `S` its opacity source
 * (premultiplied, alpha `q`), `A = f S`, `Bk` its backdrop, `L` the layer, `L0` the group's initial backdrop, `Ag` its group alpha and
 * `fg` its group shape.  They are cited by the tests, the planted defects and the vector generator.
 *
 * - **KC1 What the sink claims.**  `GSHSCENE_CAP_GROUP_NON_ISOLATED` and `GSHSCENE_CAP_GROUP_KNOCKOUT`, with the soft-mask claim of
 *   MC1, so every combination of the three is drawn and the planning pass refuses none (`GROUP_ISOLATION` and `GROUP_KNOCKOUT` stay
 *   the reasons for sinks that do not claim them).
 * - **KC2 General layers.**  A layer is **general** if its group is non-isolated or knockout.  An element drawn into a general layer
 *   (a fill, a stroke, an instance's items one by one, the pop of a child group) composites by KC4; every element drawn into any
 *   other layer, and the root, composites as it did before.  A group's own flags decide its layer, not its parent's: an isolated,
 *   non-knockout group inside a knockout group is a plain layer whose pop is an element of a general one.  An instance draws its
 *   picture's items individually, so they are separate elements of a knockout group; a front-end that wants a form to be one element
 *   wraps the instance in a group.  A glyph run's fill and a stroke are each one element.
 * - **KC3 The channels.**  Besides `L` (premultiplied, four doubles a pixel, in the layer's space) a layer carries `Ag` (one double a
 *   pixel) **iff its group is non-isolated** (for an isolated knockout group `L0 = 0`, so `Ag` is the alpha of `L` at every step, and
 *   the layer's alpha is read as it), and `fg` (one double a pixel) **iff some proper ancestor group is knockout**, because `fg`
 *   reaches the result only as the shape of the group's pop in a knockout parent, and a parent's own `fg` is a function of its
 *   elements' shapes, this group's pop among them.  A plain layer that carries `fg` steps it with each element (`fg' = fg + f - fg f`)
 *   and composites by the same step as a general one, which with `Bk = L` is the plain composite bit for bit.  Where `fg` is not carried
 *   the pop's shape `m fg` is taken as `m`: the result does not depend on it unless the parent is knockout.  A mask's picture is its
 *   own isolated group, so the knockout groups around the masked group are not ancestors of the groups inside the picture.  A channel
 *   starts at 0, made ready where `L` is.
 * - **KC4 The element step.**  For PDF's operators (source-over and the blend modes) the layer becomes
 *   `sat((1 - f)(L - Bk) + op_X(A, Bk))` and, for the alpha part, the same on alpha alone, where `op_X(A, Bk)` is what the plain
 *   `apply` makes of the weight and the source (B4: `apply(D, S, m, op_X) = op_X(m S, D)`); this is T3's `(1 - f) L + (f - Aa) Bk + T`
 *   rewritten, since `T + (1 - Aa) Bk = op_X(A, Bk)`, and the generator checks the identity in exact rationals against T3's literal
 *   form at every step of every vector.  For every other operator the layer becomes `sat((1 - f) L + f op(S, Bk))` with `S` the opacity
 *   source alone.  `Bk = L` in a layer that is not knockout and `Bk = L0` (KC5) in one that is.  The group alpha steps by the same
 *   step on alpha alone with `Agb` (`Ag` for a layer that is not knockout, 0 for one that is) in place of `Bk`'s alpha, and the group
 *   shape by `fg + f - fg f`.  `f` is a fill's coverage times the clips in force (N7, N9), a stroke's composed coverage (SC5) times
 *   the clips, and for the pop of a child the shape T5 gives: `m fg` of the child for PDF's operators and `m` for the others, with the
 *   mask's substitution of MK7 (`o` becomes `o v`, or `m` becomes `m v`) for a masked child.  An element of no shape (`m = 0` and
 *   `f = 0`) leaves the layer, its group alpha and its group shape as they are.  A bounded operator whose source is nothing leaves the
 *   layer as it is where the backdrop is the layer (T6); in a knockout group it knocks out its shape's share of what the layer holds
 *   beyond its initial backdrop (T3 with `A = 0`), so it is skipped there only when the shape is 0 as well.  The group shape steps
 *   whatever the source.
 * - **KC5 The initial backdrop is read, not copied.**  T4: an isolated group's `L0` is `(0, 0, 0, 0)`; a non-isolated group's is the
 *   backdrop it has as an element of its parent (the parent's value, or the parent's own `L0` when the parent is knockout),
 *   converted by N3 from the parent's space into the group's.  The parent is not modified between the group's push and its pop (the
 *   group's items are the only things drawn in that interval), so the sink does not copy it at the push: it reads the parent's pixel,
 *   through knockout parents, with the one prepared conversion each time (nothing where the spaces are equal: N3 leaves the value
 *   exactly alone).  A read gives the bits a copy at the push would have held, being the same function of the same unmodified pixel.
 *   The layer of a non-isolated group is **initialized** to `L0` where it is made ready, so a pixel no element reaches has `L = L0`
 *   bit for bit.
 * - **KC6 The group's result.**  At the pop `G = sat(L - (1 - Ag) L0)` on the color channels, with alpha `Ag`, where `L0` is KC5's
 *   (0 for an isolated group); then `G` is converted to the parent's space (N3, saturated) and is the element of the parent: by KC4 when
 *   the parent is general or carries a group shape, by the plain composite when it does not.  At a pixel no element reached `L = L0`
 *   exactly and `Ag = 0`, so `G = 0` exactly: a bounded operator changes nothing there and the pop region is the group's dirty
 *   rectangle, as for an isolated group.  An isolated group's `G` is its layer.
 * - **KC7 Arithmetic and tolerance.**  `double`, with N1's saturation at the points T3 to T5 state, one rounding at the write (D7).  A
 *   vector's value is exact only where every step is exact in doubles (the generator traces KC4's order of operations and bounds each
 *   step) and otherwise within `1e-12 max(1, |v|)` of the oracle before the write.
 * - **KC8 Memory is exact.**  The layout counts, beside MC5's frames, the most over every point of the expansion of the non-isolated
 *   groups open and, separately, of the groups open that carry a group shape, each times the region's pixels times 8 bytes, a mask's
 *   picture counting where its group pops with the groups open there, and a record for each frame.  `workspace_bytes` is exact, a
 *   `CALLER_MEMORY` render allocates nothing and gives the same pixels, and one byte short is `ERR_LIMIT`.  A list with none of these
 *   groups has the layout it had.  The stack that tells a group's flags at its pop is made from the sink's allocator or, from the
 *   caller's memory, from its first bytes, before anything else lives in them, and takes nothing until a group is non-isolated or knockout.
 * - **KC9 Cost, and the default path.**  Only general layers (and plain layers that carry a group shape), and the pops into them or
 *   out of them, run KC4.  A list with none has the `workspace_bytes`, work counts, pixels and hashes it had, and the plain
 *   path is not routed through KC4.  The two are the same function in the reals (T6) and not bit for bit in doubles where the order of
 *   the operations differs, so the equivalence is a gate and not a tier: an internal switch (`GSHCPU_RENDER_FORCE_GENERAL`) routes
 *   every layer, the root's too, through KC4, and every fixture of the corpus rendered both ways agrees, exactly where the arithmetic
 *   is exact.  Where a layer's elements are a fill's, KC4 is `apply` bit for bit whenever `Bk = L`.
 * - **KC10 Masks, clips, strokes, damage.**  A masked general group: T5's `G` is computed first (KC6), then MK7's substitution acts on
 *   the element (the mask pass of MC2 runs at the pop, over the pop region, as before).  The clips in force inside a general group
 *   multiply `f` (N9); the clips around it multiply once at its pop.  A general layer reads its backdrop wherever its units reach,
 *   so the parent is made ready over the same pixels first; inside a damage the render equals the whole render, and limit refusals are
 *   identical.  The readiness of a layer (the initial backdrop's conversion) is not counted in the work between two checks, as the
 *   root's load is not.
 * - **KC11 Conflation is the definition.**  In a knockout group a shape's partial coverage `f < 1` leaves `1 - f` of the layer's
 *   previous value and `f` of the element against `L0`, so two abutting shapes each half-covering a pixel leave a quarter of the
 *   initial backdrop showing through where an exact union would show none.  That is T3 (a knockout is defined on shape, and shape is
 *   coverage); this sink still refuses the conflation-free flag, and a test pins the values so a "fix" is seen.
 *
 * ### Patterns (PC1 to PC11; shape-scene's PT1 to PT14)
 *
 * A pattern is drawn as defined: the **lattice picture** of a use (the tile's units, each over all the copies of the lattice that meet
 * the pixels the use draws) is rendered into an isolated layer, and the fill or stroke is N7 with that layer's pixel as its source.
 * Each rule below is stated in real arithmetic unless it says otherwise.
 *
 * - **PC1 What the sink claims.**  `GSHSCENE_CAP_PATTERN`, not `PATTERN_RESAMPLED`: every field of the pattern is honored (the repeats, the
 *   gaps and overlaps, the tints).  A tile may hold what any list holds: fills, strokes, clips, groups of every kind, masks, images,
 *   gradients and patterns.
 * - **PC2 The fill.**  The source of a pixel with coverage after the clips is the lattice layer's pixel (PC7: an uncolored pattern reads only
 *   its alpha, times the tint converted once) times the paint's alpha multiplier; `apply(L, S, m8 / 255, SRC_OVER)` follows.  The lattice
 *   layer is a group layer over the pixels the use draws, starting `(0, 0, 0, 0)` in the layer's blend space.
 * - **PC3 The canonical translation of a copy.**  The copy `(i, j)`'s device translation is `gshgeom_lattice_translation`'s `tau + i a + j b`
 *   (`a`, `b` the device step vectors, each product rounded once, never accumulated), split by `gshgeom_stroke_canonical_map` into the
 *   whole pixels `s` and a fraction `f` in `[0, 1)`; where `a` and `b` are integer vectors the split is exact (`s = floor(tau) + i a +
 *   j b` in integers, `f` the fraction of `tau` for every copy: the sum in `double` would round the fraction), both by
 *   `gshgeom_lattice_canonical` (LT5).  The copy is the tile under the chain's linear part with the translation `f`: its outlines
 *   are flattened there, cut to the cell's quadrilateral under the same map by `gshgeom_flat_path_clip_convex`, and scan converted over the
 *   region moved by `-s`; a paint in the tile is evaluated at `q - s`.  A copy is therefore a function of the linear part, the fraction and
 *   the shift: copies of equal fractions, and a pattern moved by whole pixels, are the same bits moved.  A shifted coordinate past
 *   ::GSHCPU_RENDER_COORD_LIMIT is `RESOURCE_LIMIT`.
 * - **PC4 A unit with copies.**  Each unit of the tile (a fill, a stroke, a clip push) is one element over all the copies of its use.  At
 *   a pixel, copy `x` has the coverage `m_x`, its own coverage folded with the clips of the tile in force *in that copy* (each clip
 *   cut to that copy's cell, D8), and the source `S_x`.  **The copies add areas, not floors (F3):** a copy that no clip weighs has the area
 *   `a_x` of its region in the pixel in 1/8192 pixel (the byte `m_x` is `min(255, floor(a_x / 32))` and the part the byte leaves out is
 *   carried), and the element has the shape `min(255, floor(sum a_x / 32)) / 255`, the byte of the sum of the areas, made once, and the
 *   mean source `sum(a_x S_x) / sum(a_x)`, summed in lattice order with no fused multiply-add: the waist's `(1 - sum w_x A_x) L + sum w_x S_x`
 *   with `w_x = a_x / max(8192, sum a)`, to within one rounding of the mean.  The cells of a lattice that meet in a pixel have areas that sum to
 *   the whole pixel, so a cell-filling fill is opaque at every seam and at every vertex whichever way its contours are wound; the sum of
 *   their floors is not (254 at a vertex for the usual fractions).  A copy that a clip of the tile weighs has the weighed byte `m_x` and no
 *   remainder (the clip is cut to the copy's cell, the weight is of a byte, and the product of two coverages at a seam is not made whole by a
 *   sum), and so has a stroke (the exact step has made its byte); a pixel only one copy touches is N7 exactly, the byte.  A clip is made again
 *   for each copy (its outline under that copy's canonical map, cut to that copy's cell); a clip block, to a group inside or around it, is
 *   the union of its copies' regions: the byte of the sum of their areas, held at 255.
 * - **PC5 Groups, strokes, images and nested patterns in the tile.**  A group is one group over all copies, popped once.  A stroke is one
 *   unit: the expansion's body and aligned part are cut to the cell by the convex clip (CK1; in this copy's stroke only), and SC5 composes
 *   the cut parts with the clip path, so a stroke that ends at a cell edge leaves no seam.  An image paint, a gradient or a pattern is evaluated per copy under the copy's canonical map; a pattern in a tile is a
 *   pass inside the copy's frame of coordinates, its layer drawn over the unit's pixels in that copy.  A nest is as deep as the tile's
 *   pictures allow (R1's PT2).
 * - **PC6 Overlapping cells.**  Where a step is smaller than the cell on an axis that repeats, the copies are drawn one at a time in lattice order,
 *   all of a copy's units onto the lattice layer before the next copy's, each cut to its cell by the geometric clip and by nothing else.
 * - **PC6a A tile that draws past its cell (shape-scene PT15).**  For a pattern whose `overflow` is 1 the lattice's cell is the pattern's extent
 *   (`gshscene_pattern_extent`: the cell widened to the box of what the tile paints), so a copy is cut to its extent and not to its cell, the copies a
 *   pixel is looked for in are those whose extent meets it, and the copies are drawn one at a time in lattice order (PC6) whenever the extent is wider
 *   than a step, so a later copy is over an earlier one where they spill into each other.  The sink has `GSHSCENE_CAP_PATTERN_OVERFLOW`.  A tile whose
 *   extent is not known (a stroke with a device width, a hatch) is clipped to its cell as before, and the planning pass has reported it
 *   `APPROXIMATED`, `PATTERN_OVERFLOW`.  A pattern whose `overflow` is 0 is drawn exactly as it was.
 * - **PC7 Colored, uncolored, alpha.**  The tint is resolved and converted once; the lattice layer is drawn with the tile's own colors and only its
 *   alpha is read.
 * - **PC8 Limits.**  `GSHCPU_Limits.max_pattern_cells`: the copies of a use, counted by `gshgeom_lattice_each` (a cell that only touches the
 *   rectangle counts) over what the whole render draws of it, at every level of nesting and summed, are at most it; one more is
 *   `RESOURCE_LIMIT` for the use, strict stopping with the finished prefix.  The count is of the list and the target and does not depend
 *   on the damage or the caller's memory.  A stroke in a tile counts for what a copy of it costs: for every copy of its pass one more copy
 *   for each 16 points of its outline (under the pass's linear part), charged once before a copy is drawn, so that 2^22 copies of a stroke of
 *   thousands of points are refused, not drawn; a stroke of fewer than 16 points counts for nothing more.  A limit met inside a tile (a copy's points or cells, a coordinate past the range, a map the
 *   geometry refuses) refuses the use.  The nested count is taken over the outer copies that are drawn.
 * - **PC9 The period tier.**  Where the step vectors `a` and `b` are integer vectors (every component an exact integer below `2^31`, both axes
 *   repeating, `|D|` of their determinant nonzero and at most `2^31`) the lattice picture is the same at `q` and at every `q + i a + j b`, so the
 *   pass draws one **window** `[0, p) x [0, s)` of the Hermite normal form `(p, r, s)` of the lattice (`p s = |D|`), moved to the corner of the
 *   use's rectangle, and reads a pixel at `rep(q - w) + w` (LT3).  The window has to lie in the rectangle, and the tile has to hold no
 *   pattern (PC8's nested count) and no soft mask.  It is bit-equal to the general tier by construction and by a gate (every fixture, the tests'
 *   cases and every fuzz input rendered with `GSHCPU_RENDER_TIER_PERIOD` on and off: the same bytes and report), and a limit never depends on
 *   it: a copy whose whole cell could meet a limit the window's copies would not (deposits, a gradient's or an image's domain or work, the range
 *   of its pixels) makes the pass give the window up for the general tier.
 * - **PC10 The cache.**  The window's pixels are kept in the table of the stroke expansions (SC7: one mutex, least recently used first, pinned
 *   while read, `evict(id)` for the pattern's or the tile's id, `static_bytes`, bypassed under `CALLER_MEMORY`), under the key of the
 *   pattern's and the tile's ids, the canonical map's linear part and fraction, the layer's and target's spaces, the list's foreground and
 *   palette when the tile reads them, and the revisions of the pieces whose bits the raster holds; a hit is the miss bit for bit.
 * - **PC11 Caller memory, damage, cancel.**  Under `CALLER_MEMORY` the lattice layers, the accumulators and each copy's geometry are in the
 *   caller's block (the size reported covers the use's own item and, above it, the most any unit of a tile takes); a pixel inside a damage is the
 *   pixel of the whole render.  Cancel is checked before every copy of every unit and between units.
 *
 * ### Hatches (HX13; shape-scene's HX1 to HX16)
 *
 * A fill painted with a hatch is drawn as the stroke of its trimmed, dashed lines, by the stroke's own machinery (SC1 to SC8); nothing
 * about the lines is decided here.  The sink claims `GSHSCENE_CAP_HATCH`.
 *
 * - **The expansion.**  The region (the fill's path, or a run's glyph outlines as one path) is hatched by shape-geometry's
 *   `gshgeom_hatch_pieces` at the canonical map (SC3: the composed map's linear part with the translation's fraction, drawn moved by the
 *   whole pixels), and the pieces are stroked by `gshgeom_hatch_expand_pieces` with the hatch's style: `hatch.h`'s HX4 to HX8, whose
 *   outline equals the stroke expansion of the same pieces as one explicit path.  Its body is covered as a stroke's is (SC5, with no
 *   aligned part), then the clips and the operator; the source is the hatch's ink with its alpha times the hatch paint's multiplier.  The
 *   fill's own coverage is not multiplied in.  A translucent crosshatch is one union (SG13): its crossings are not darker.
 * - **The cache.**  The expansion is cached under SC7's table, keyed by the region's id, the style's, the hatch's, the fill rule, the bit
 *   patterns of the canonical linear part and the fraction, and the stroke's and the hatch's revisions; a hit equals a miss bit for bit,
 *   `evict(id)` drops the entries naming the hatch, the region or the style, and nothing is read or written under `CALLER_MEMORY`.  FT-2's
 *   sparse composite is taken (a hatch is mostly empty), and changes nothing.
 * - **The limit.**  `GSHCPU_Limits.max_hatch_lines` bounds the lines of the use (HX5's count, over all the families, of the lines that meet the
 *   closed bounds of the region the whole render draws of the fill, never only the damage or the clips): a count of more is the fill's
 *   `RESOURCE_LIMIT`, on a hit as on a miss, whatever the tier.  The count is `GSHCPU_Render_Counters.hatch_lines`.  The pieces per fill
 *   and the outline's points are bounded by `max_points` and `max_dash_steps` as a stroke's are.  The work bound counts a line's trim as one
 *   step for each element of the region.
 * - **Caller memory and bounds.**  The pieces and the outline are in the caller's block, sized as a stroke's is (measure_items); a hatch
 *   fill's reach is its region's outset by the pen (shape-scene's HX15), which the expansion's boxes give exactly.
 *
 * ### Coverage by the centers of the pixels (AC1 to AC6; shape-scene's AA1 to AA6)
 *
 * A fill or a stroke whose `antialias` is `GSHSCENE_ANTIALIAS_NONE` or `GSHSCENE_ANTIALIAS_NONE_TOP_LEFT` has, in the pixel `(x, y)`, the coverage 255 when the point
 * `(64 x + 32, 64 y + 32)` (the pixel's center in 1/64 pixel) is inside the item's shape and 0 when it is not, and everything after it (the
 * clips, the paint, the opacity, the operator, the layer of a group) is as for any coverage.  The sink claims
 * `GSHSCENE_CAP_ANTIALIAS_NONE` and `GSHSCENE_CAP_ANTIALIAS_NONE_TOP_LEFT`; the two differ only in the tie (AC1).
 *
 * - **AC1 The rule.**  Inside is the winding of the flattened device outline at the center, by the item's fill rule (a stroke's, the nonzero
 *   rule), with a center on an edge or a vertex decided by the item's tie rule (shape-scene's AA3): for `GSHSCENE_ANTIALIAS_NONE_TOP_LEFT`
 *   the winding of the point moved by an infinitely small `(e, e^2)`, for `GSHSCENE_ANTIALIAS_NONE` (the bottom-right rule) the winding of
 *   the point moved by `-(e, e^2)`, each computed exactly in integers.  Under the top-left rule an edge from `p` to `q` straddles the center
 *   `c` when exactly one of `p.y <= c.y` and `q.y <= c.y` holds, meets the line `y = c.y` at `x_e`, and counts, with the sign of
 *   `q.y - p.y`, when `x_e > c.x`; under the bottom-right rule it straddles when exactly one of `p.y < c.y` and `q.y < c.y` holds and
 *   counts when `x_e >= c.x`; the sum is the winding.  The point of the rule is exactness: the same winding is found whichever way a contour goes, with no tolerance, and a center
 *   on an edge belongs to the shape on its right or below it, so that shapes sharing an edge neither overlap nor leave a gap.  The glyphs
 *   of a run and the contours of a path add their windings, as their areas add.  `list.h` (shape-scene) states it in full.
 * - **AC2 One coverage, every tier.**  The fast fill coverage (`GSHCPU_FILL_COVERAGE_FAST`), the fast stroke coverage and the sparse
 *   composite of a stroke are all areas, summed and rounded, and a center is decided exactly, so an item with the mode takes none of
 *   them: whatever the limits say, it is covered by `gshcpu_render_cover_center` (a function of the edges and the rule, with no
 *   dependence on a tier, the cache, the damage or the caller's memory).  The result is the same bytes in every combination, which a
 *   test holds against an independent reference.  The exact union of a stroke's pieces (UC1) has nothing to make exact: a center is
 *   inside the union of the pieces when the winding of all of them, the nonzero rule, says so, and the exact step is not used.
 * - **AC3 Strokes.**  A stroke is expanded as any stroke is (SC3: the translation's fraction in the outline, whole pixels in the shift),
 *   and the parts of an aligned stroke are each decided by AC1 and composed by SC5's formula, which on 0 and 255 is the union of the body
 *   with the aligned part met with the clip or its complement.  The centers are those of the device's pixels: the canonical frame is
 *   the device's moved by whole pixels, which moves no center across an edge.  A hatch is the stroke of its lines and is decided so.
 * - **AC4 Patterns.**  An item painted with a pattern is covered per copy of the lattice (the items inside a tile are items of their own, covered as any item is).  The copy's cell is the
 *   quadrilateral PC3 names, and the item is not cut to it (a point of a cut outline is rounded to the lattice, so a center near the edge
 *   it moved could change sides): a center is inside the item by AC1 and inside the cell's quadrilateral by AC1 as well, and the cells of a
 *   lattice meet at their edges without overlap or gap, so every center is covered by one copy.  The area a byte leaves out (PC4)
 *   is 0 for a pixel nothing covers and 32 of 8192 for a whole one.
 * - **AC5 The limit.**  The work is the cell deposits of the other coverage: here one deposit is one edge's crossing of one row's center line
 *   (an edge that straddles it), counted over the rows of what the whole render draws of the item (S4), so a count past
 *   `max_deposits` is the item's `RESOURCE_LIMIT` whatever the damage, before anything is written; and the pixels of the item's rectangle,
 *   which are bounded by `max_bytes`, are added.  A deposit is never more than a cell's, so an item the other coverage draws within the cap
 *   is drawn here.
 * - **AC6 What is not decided here.**  A clip's edge (`CLIP_AA`) and a union clip's are antialiased whatever the items in a block ask; a
 *   fill of the plane has no outline, covers every pixel the clips allow, and the mode is nothing to it.
 *
 * ### Memory (S9)
 *
 * A render takes one block: the layers (32 bytes a pixel of the region for the
 * root and for each level of group nesting, a mask frame counting as one, and 8 bytes a pixel for each non-isolated group open at once
 * and for each group open at once that carries a group shape, KC8), the touched map and the coverage
 * scratch (a byte a pixel each), a byte a pixel for each level of clip nesting,
 * the cell accumulator of ::GSHCPU_RENDER_BAND_ROWS rows, the walk, and for
 * gradients a table of twice the gradient fills of the expansion (a picture's
 * counted where it is drawn) and room for their stops, and, only when a group has a mask, the mask frame of MC5, and, only when the expansion holds a stroke that aligns, three
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
 * pixel (UC9); `max_area_pixel_work` and `max_image_work` the visits of the `AREA` filter of an image paint (IC9); `max_dash_steps` the steps of a stroke's dash pattern, which
 * are not in the work measure but cost a fraction of a microsecond each (shape-geometry refuses a stroke past it before it walks the pattern).
 * `max_work` the work units of shape-geometry's budget that the expansion of one stroke, the lines of one hatch fill and the measure of one path may
 * spend (its `GSHGEOM_Limits.max_work`): a stroke or a hatch fill that would spend more is refused as ::GSHSCENE_REASON_RESOURCE_LIMIT, whatever the damage, since the
 * expansion is a function of the stroke, its map and the limits alone.
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
