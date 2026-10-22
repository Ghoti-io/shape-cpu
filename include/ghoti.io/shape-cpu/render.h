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
 * `3 max_points + max_cells + 2 max_bytes + elements * GSHGEOM_STROKE_MAX_DEPTH` of the sink's ::GSHCPU_Limits (each
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
 * path past `max_points` and more than `max_cells` cell deposits are the sink's
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
 *   paint kind draws.  A centerline that is a glyph run is refused by the planning pass, `STROKE_CENTERLINE`
 *   (this sink does not set `STROKE_RUNS`).  The entries a stroke can receive while drawing are the fill's plus
 *   `RESOURCE_LIMIT` (SC6).
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
 *   `T I_1 ... I_k M G`.  The coverage's tolerance is a **bracket**, `A(R minus D(tau)) - e <= k <= A(R plus D(tau)) + e
 *   + o`, with `A` the area inside the pixel, `D(tau)` the disc of the expansion's tolerance, `e` the 8-bit rounding and
 *   `o = 16/255` the overlap allowance: AD-12's accumulation sums the signed areas of overlapping pieces, so a pixel
 *   two pieces cover in part (an inner join) can be credited up to their overlap more than their union.  Interior and
 *   exterior pixels are exact.  Where the aligned part's boundary crosses the fill region's at an angle `alpha` the
 *   composed error may reach `tau / sin(alpha / 2)`.
 * - **SC6 Limits.**  A map or device path the geometry refuses, the expansion's `ERR_LIMIT` (a part past `max_points`,
 *   the depth cap, the arithmetic bound), a shifted coordinate past the range, or cell deposits past `max_cells` (summed
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
 *   own rectangle (and by `max_cells`), the sweep hands over a row at a time only the pixels that can be nonzero.  It
 *   is detected as a *solid hairline* (device width 1) or a *plain solid stroke* (any other width), each counted.
 * - **SC9 Groups and order.**  A stroke is one unit of its sequence.  In an isolated group it composites `SRC_OVER`
 *   into the group's layer and the group's blend mode acts at its pop.  A non-isolated, knockout or soft-masked group is
 *   refused as before, and the strokes inside are skipped.
 * - **SC10 The test sink.**  shape-scene's test sink states, for a stroke whose centerline is a path, the hash of the
 *   outline this sink draws on a raster target (`outline=` of its `K` record): the canonical expansion of SC3.
 * - **SC11 Work and cancel.**  A stroke counts the points of its parts, the cell deposits, two passes over its pixels
 *   when aligned and one otherwise, and the expansion's own steps (the centerline's elements times the depth cap,
 *   clamped to the resource limit the bound states), counted whether the expansion was made or found so that a report
 *   does not depend on the cache or the tier.
 *
 * ### Memory (S9)
 *
 * A render takes one block: the layers (32 bytes a pixel of the region for the
 * root and for each level of group nesting), the touched map and the coverage
 * scratch (a byte a pixel each), a byte a pixel for each level of clip nesting,
 * the cell accumulator of ::GSHCPU_RENDER_BAND_ROWS rows, the walk, and for
 * gradients a table of twice the gradient fills of the expansion (a picture's
 * counted where it is drawn) and room for their stops, and, only when the expansion holds a stroke that aligns, three
 * more arrays of a byte a pixel (the aligned part, the clip path's coverage and their composition, SC5).  The
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
 * drawn.  `max_cache_bytes` bounds the stroke cache (SC7).
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
