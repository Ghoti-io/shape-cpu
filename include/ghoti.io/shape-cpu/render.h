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
 * it draws, the caps are: every item kind and every composite operator;
 * `CAN_BLOCK` (it completes inside `begin`), `CALLER_MEMORY`, `CLIP_AA`,
 * `BLEND_SPACES`, `READBACK` and `GLYPHS_FROM_FONT` (story 14a: a run from a font
 * is drawn through its face's provider by shape-scene's route, glyph.h, the fast
 * tier wherever the route returns it); no `THREAD_AFFINE` (it holds no mutable
 * state of its own, so renders on distinct surfaces may run at once) and no
 * `PREPARE`.  Cancel is `BETWEEN_UNITS` (each unit
 * of the list's expansion); the work between two checks is counted in
 * `"cells+points+pixels"` and its bound is `max_points + max_cells + max_bytes`
 * of the sink's ::GSHCPU_Limits (each at its default when zero; `max_points`
 * defaults to `shape-geometry`'s).
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
 * resource limits: the item is refused while drawing (S6).
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
 * - **The write.**  The one rounding of a render is the write of a touched pixel
 *   to the target, in the target's alpha form: to 8 bits, `floor(v * 255 + 1/2)`
 *   clamped to `[0, 255]` (round half up; a straight color is `C / A`, and 0 where
 *   `A` is 0); to `float`, the `double` rounded once (round to nearest), with no
 *   clamp.  A pixel no unit touched is not written.
 * - **Tolerance (D7, AD-18).**  Before the write, a value is within `1e-12` of
 *   the rational oracle's (shape-scene's `tests/composite_vectors.h`, and this
 *   library's `tests/render_vectors.h` for the D8 weights), and equal to it where
 *   no transfer or matrix is evaluated and the values are dyadic.  After it, a
 *   stored value is the oracle's rounded the same way, except that a value within
 *   `1e-9` of a rounding boundary may land on either side.
 *
 * ### Memory (S9)
 *
 * A render takes one block: the layers (32 bytes a pixel of the region for the
 * root and for each level of group nesting), the touched map and the coverage
 * scratch (a byte a pixel each), a byte a pixel for each level of clip nesting,
 * the cell accumulator of ::GSHCPU_RENDER_BAND_ROWS rows, and the walk.  The
 * geometry of each item (its device path and flattened path) is allocated from
 * the sink's allocator, or with `CALLER_MEMORY` from the rest of the workspace,
 * which is sized by converting every item once: the exact need is the block plus
 * the most any item takes.  A workspace one byte smaller is ::GSHSCENE_ERR_LIMIT.
 * Sizing a list for a workspace the caller gives uses that workspace as its
 * scratch, so a `CALLER_MEMORY` render whose workspace is large enough allocates
 * nothing at all.
 *
 * ### Limits
 *
 * The region drawn is at most `max_dimension` pixels wide and tall, at most
 * `max_bytes` pixels in all, and lies within ::GSHCPU_MAX_DIMENSION_CEILING
 * pixels of the origin; past either it is ::GSHSCENE_ERR_LIMIT before anything is
 * drawn.
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
