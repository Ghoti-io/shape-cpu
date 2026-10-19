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
 * @file coverage.h
 *
 * Coverage: 8-bit exact-area coverage of a `shape-geometry` device path, the
 * scan converter that `font` first had, moved here (story 9; CAP-6; AD-1, AD-12).
 *
 * ## The definition
 *
 * Device space has its origin at the top-left corner of the top-left pixel,
 * y down, and a coordinate is an `int32_t` count of 1/64 pixel (AD-12).  Every
 * edge of the flattened path deposits two signed numbers into each cell it
 * crosses: `cover`, the signed height of the crossing in 26.6 units, and
 * `area`, that height times the sum of the crossing's two x positions within
 * the cell.  A sweep along each row accumulates `cover` from left to right,
 * which is the winding number times a pixel's height, and `area` corrects the
 * cell the edge passes through.  This is FreeType's `ftgrays` arithmetic with 6
 * sub-pixel bits instead of 8, because 26.6 is the family's pixel space and a
 * second sub-pixel resolution would be a second rounding rule.  The fold into
 * 0..255 is: the accumulated area shifted by `2 * 6 + 1 - 8`, its absolute
 * value, saturating at 255 under nonzero, and under even-odd folded at 512 (a
 * winding number of 2 is outside again).  Coverage is linear alpha.
 *
 * Nothing here is floating point, and nothing here reads the host's byte
 * order, so the same path gives the same bytes on every target.
 *
 * ## Curves, open subpaths, orientation
 *
 * A quadratic or cubic in the device path is flattened by `shape-geometry`'s
 * integer flattener (revision ::GSHGEOM_FLATTEN_REVISION, tolerance
 * ::GSHGEOM_FLATTEN_TOLERANCE) and then filled; a caller that has its own
 * polyline passes lines.  A subpath that is not closed is filled as if it were:
 * the closing line is implicit, as in every fill.  The flattener is not
 * symmetric under negation of a coordinate (a midpoint rounds half up), so a
 * caller that flips an axis flips the path **before** asking for coverage.
 *
 * ## Range
 *
 * A coordinate that does not round into 26.6 never reaches this library
 * (`shape-geometry` refuses it with `ERR_LIMIT`, AD-6).  This library works in
 * `int64` wherever a device coordinate can be large, never wraps, never clips
 * and never clamps: a path whose extent exceeds ::GSHCPU_Limits is
 * ::GSHCPU_ERR_LIMIT, refused before anything is allocated.  Clipping, where a
 * front end wants it, is decided in `double` before it asks for a device path.
 *
 * ## Hash and dump (AD-21)
 *
 * ::gshcpu_coverage_hash is FNV-1a 64 in `shape-geometry`'s spelling
 * (hash.h): `u32` format version 1, `u32` kind `COVR` (0x52564f43), `u32`
 * width, `u32` height, `i32` left, `i32` top, `u32` fill rule (its pinned
 * value), then the rows, width bytes each, in order; the stride is not hashed.
 */

#ifndef GHOTI_IO_GSHCPU_COVERAGE_H
#define GHOTI_IO_GSHCPU_COVERAGE_H

#include <ghoti.io/shape-cpu/core.h>
#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-geometry/allocator.h>
#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-geometry/fill.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The allocator (`shape-geometry`'s, which is cutil's); NULL means the default. */
typedef GSHGEOM_Allocator GSHCPU_Allocator;

/**
 * @brief 8-bit linear coverage of one shape.
 *
 * Row 0 is the **top** row (y down), `top` is its device row and `left` the
 * device column of column 0.  The struct is open rather than opaque because a
 * compositor walks rows; `data` belongs to the coverage and is freed by
 * ::gshcpu_coverage_destroy through `allocator`.  An empty result has width
 * and height 0, `data` NULL, `left` and `top` 0.
 */
typedef struct GSHCPU_Coverage {
  uint8_t * data;                       ///< `height * stride` bytes, or NULL if empty.
  size_t stride;                        ///< Bytes per row; at least `width`.
  uint32_t width;                       ///< Pixels across.
  uint32_t height;                      ///< Rows.
  int32_t left;                         ///< Device column of column 0.
  int32_t top;                          ///< Device row of row 0 (y down).
  GSHGEOM_Fill_Rule fill;               ///< The rule that was applied.
  const GSHCPU_Allocator * allocator;   ///< Where `data` came from.
} GSHCPU_Coverage;

/**
 * @brief Coverage of a device path inside a rectangle of pixels.
 *
 * The rectangle is `width` by `height` pixels whose top-left pixel is device
 * pixel (@p left, @p top).  Every point of the path, after any curve is
 * flattened, must lie inside it: `0 <= x - left * 64 < width * 64` and the same
 * for y.  `font` makes the rectangle the glyph's box grown by a pixel on every
 * side, which is what lets a closing edge have a cell of its own.  The result
 * is the whole rectangle, untrimmed (::gshcpu_coverage_trim shrinks it).
 *
 * @param path   The device path (lines, curves, closes); open subpaths are
 *               filled as if closed.
 * @param fill   The fill rule.
 * @param left   Device column of the rectangle's first column.
 * @param top    Device row of its first row.
 * @param width  Pixels across; 0 gives an empty coverage.
 * @param height Rows; 0 gives an empty coverage.
 * @param limits Caps; NULL for the defaults.
 * @param alloc  The allocator; NULL for the default.
 * @param out    Receives the coverage, only on success; release it with
 *               ::gshcpu_coverage_destroy.
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_INVALID for a NULL path or output, a fill
 *         rule outside the enum, or a path that leaves the rectangle;
 *         ::GSHCPU_ERR_LIMIT for a rectangle or a path beyond ::GSHCPU_Limits;
 *         ::GSHCPU_ERR_OOM.
 */
GSHCPU_API GSHCPU_Result gshcpu_coverage_fill(const GSHGEOM_Device_Path * path,
    GSHGEOM_Fill_Rule fill, int32_t left, int32_t top, uint32_t width,
    uint32_t height, const GSHCPU_Limits * limits, const GSHCPU_Allocator * alloc,
    GSHCPU_Coverage * out);

/**
 * @brief Coverage of a device path, in the rectangle the path itself needs.
 *
 * The rectangle is the extent of the flattened path grown by a pixel on every
 * side, and the result is trimmed to the pixels that have any coverage (an
 * empty result is 0 by 0).  Returns what ::gshcpu_coverage_fill returns, and
 * ::GSHCPU_ERR_LIMIT when that extent passes ::GSHCPU_Limits.
 */
GSHCPU_API GSHCPU_Result gshcpu_coverage_from_path(
    const GSHGEOM_Device_Path * path, GSHGEOM_Fill_Rule fill,
    const GSHCPU_Limits * limits, const GSHCPU_Allocator * alloc,
    GSHCPU_Coverage * out);

/**
 * @brief Shrink a coverage to the pixels that have any, in place.
 *
 * One buffer, one pass; `left` and `top` move to match.  A coverage with no
 * nonzero pixel becomes empty (0 by 0, pixels released, `left` and `top` 0).
 *
 * @param coverage The coverage, or NULL.
 */
GSHCPU_API void gshcpu_coverage_trim(GSHCPU_Coverage * coverage);

/**
 * @brief Release a coverage's pixels and zero the struct, so that a second
 *        call is harmless.
 *
 * @param coverage The coverage, or NULL.
 */
GSHCPU_API void gshcpu_coverage_destroy(GSHCPU_Coverage * coverage);

/**
 * @brief One pixel's coverage.
 *
 * @return 0 for NULL or a pixel outside, which is a pixel the shape does not
 *   cover.
 */
GSHCPU_API uint8_t gshcpu_coverage_at(const GSHCPU_Coverage * coverage,
    uint32_t column, uint32_t row);

/**
 * @brief The sum of every pixel's coverage (255 times the area, to within the
 *        rounding), 0 for NULL.
 */
GSHCPU_API uint64_t gshcpu_coverage_total(const GSHCPU_Coverage * coverage);

/**
 * @brief The FNV-1a 64 hash of a coverage, in the spelling above.  NULL
 *        hashes as the empty stream.
 */
GSHCPU_API uint64_t gshcpu_coverage_hash(const GSHCPU_Coverage * coverage);

/**
 * @brief Write the dump: one line `gshcpu.coverage 1 WxH at LEFT,TOP
 *        fill=<name> total=N hash=<16 hex digits>` (LEFT and TOP in decimal).
 *
 * @return ::GSHCPU_OK, ::GSHCPU_ERR_INVALID for NULL, ::GSHCPU_ERR_IO when the
 *         stream fails (including at the flush).
 */
GSHCPU_API GSHCPU_Result gshcpu_coverage_dump(const GSHCPU_Coverage * coverage,
    FILE * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_COVERAGE_H */
