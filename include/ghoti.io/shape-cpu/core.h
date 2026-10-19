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
 * @file core.h
 *
 * The result codes, the limits on what one render may ask for (CAP-15, AD-19)
 * and the version this build reports.
 */

#ifndef GHOTI_IO_GSHCPU_CORE_H
#define GHOTI_IO_GSHCPU_CORE_H

#include <ghoti.io/shape-cpu/macros.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of an operation.
 *
 * Zero is success.  This is the suite's fixed vocabulary with nothing added,
 * the same enumeration in the same order as the other libraries of the shape
 * family; most of it is never returned by this library.  ::GSHCPU_RESULT_COUNT
 * closes the enum so that a test can check the string table is complete.
 */
typedef enum {
  GSHCPU_OK = 0,          ///< The operation succeeded.
  GSHCPU_ERR_IO,          ///< A write to a caller's stream failed.
  GSHCPU_ERR_FORMAT,      ///< Not returned by this library.
  GSHCPU_ERR_UNSUPPORTED, ///< Not returned by this library.
  GSHCPU_ERR_LIMIT,       ///< A stated cap was exceeded; nothing was truncated.
  GSHCPU_ERR_CORRUPT,     ///< Not returned by this library.
  GSHCPU_ERR_OOM,         ///< The allocator returned NULL.
  GSHCPU_ERR_INVALID,     ///< A caller-supplied argument is wrong.
  GSHCPU_ERR_INTERNAL,    ///< The library's own invariant failed.
  GSHCPU_RESULT_COUNT
} GSHCPU_Result;

/**
 * @brief Static description of a result code.
 *
 * @param result The result code, including values outside the enum.
 * @return A static string, never NULL, never allocated.
 */
GSHCPU_API const char * gshcpu_result_string(GSHCPU_Result result);

/** Default ::GSHCPU_Limits.max_dimension: 16,384 pixels. */
#define GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION ((size_t)16384)

/** Default ::GSHCPU_Limits.max_bytes: 256 MiB. */
#define GSHCPU_LIMITS_DEFAULT_MAX_BYTES ((size_t)256 * 1024 * 1024)

/** Default ::GSHCPU_Limits.max_cells: 4 Mi cells (24 bytes each). */
#define GSHCPU_LIMITS_DEFAULT_MAX_CELLS ((size_t)1 << 22)

/**
 * The ceiling on a bitmap's width or height, in pixels, whatever a caller
 * sets: coordinates relative to a bitmap are 26.6 and stay far inside
 * `int32` (2^24 pixels is 2^30 units), so no arithmetic here can wrap.
 */
#define GSHCPU_MAX_DIMENSION_CEILING ((size_t)1 << 24)

/**
 * @brief Caps for the quantities a caller can make unbounded.
 *
 * A field of 0 means the default above; NULL where a `const GSHCPU_Limits *`
 * is taken means every default.  Exceeding a cap is ::GSHCPU_ERR_LIMIT exactly
 * at the bound (a quantity equal to the cap passes, one more does not), never
 * a truncation, and the output is left as it was.  Pass `SIZE_MAX` for no cap.
 */
typedef struct GSHCPU_Limits {
  size_t max_dimension; ///< Widest and tallest working bitmap, in pixels (including the one-pixel margin ::gshcpu_coverage_from_path adds). At most ::GSHCPU_MAX_DIMENSION_CEILING whatever is asked.
  size_t max_bytes;     ///< Bytes of the working bitmap (width times height).
  size_t max_cells;     ///< Cells the accumulator may hold.
  size_t max_points;    ///< Points of the flattened path (handed to `shape-geometry`).
  uint8_t _reserved[8]; ///< Reserved; zero.
} GSHCPU_Limits;

/**
 * @brief Fill @p limits with the built-in defaults.
 *
 * @param limits The structure to fill; NULL is ignored.
 */
GSHCPU_API void gshcpu_limits_default(GSHCPU_Limits * limits);

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * @return A static string, never NULL.  "0.0.0", or "0.0.0-dev" when BRANCH
 *   was overridden.
 */
GSHCPU_API const char * gshcpu_version_string(void);

/**
 * @brief This build's version, packed as ::GSHCPU_MAKE_VERSION packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GSHCPU_API unsigned gshcpu_version_number(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_CORE_H */
