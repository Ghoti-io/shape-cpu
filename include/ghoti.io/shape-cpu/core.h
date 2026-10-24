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
 * Zero is success.  This is the suite's fixed vocabulary (CONVENTIONS section
 * 5) followed by ::GSHCPU_ERR_REFUSED and ::GSHCPU_ERR_CANCELLED, which the sink
 * contract adds (shape-scene's sink.h, S6 and S7), in the same order as
 * shape-scene's enumeration; the values `font` names do not move.  A render
 * through the sink returns shape-scene's result; these two are here so that this
 * library's own vocabulary can say what its sink does.  Some of the vocabulary is
 * never returned by this library.  ::GSHCPU_RESULT_COUNT closes the enum so that
 * a test can check the string table is complete.
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
  GSHCPU_ERR_REFUSED,     ///< An effect was refused under strict mode (sink.h, S6).
  GSHCPU_ERR_CANCELLED,   ///< The render's cancel token was set (sink.h, S7).
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

/** Default ::GSHCPU_Limits.max_cells, for the standalone coverage functions only: 4 Mi stored cells (24 bytes each, 96 MiB). */
#define GSHCPU_LIMITS_DEFAULT_MAX_CELLS ((size_t)1 << 22)

/**
 * Default ::GSHCPU_Limits.max_deposits: 4 Mi crossings counted over one item of the sink.  The count is work, not memory:
 * the sink adds each crossing straight into a dense accumulator for a band of rows, so its memory does not depend on it
 * (render.h, SC6).
 */
#define GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS ((uint64_t)1 << 22)

/** Default ::GSHCPU_Limits.max_cache_bytes: 16 MiB of stroke expansions held across renders. */
#define GSHCPU_LIMITS_DEFAULT_MAX_CACHE_BYTES ((uint64_t)16 * 1024 * 1024)

/**
 * Default ::GSHCPU_Limits.max_exact_fragments: 32768 pieces of one stroke part in one pixel (render.h, UC9, UC17).  The largest
 * pixel measured over stroked lines of real text (the densest everyday input: round joins and caps, 84 pixels, widths up to
 * 22 units) holds 13363, the benchmark's stroke scenes 4294.
 */
#define GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS ((uint64_t)32768)

/** The largest ::GSHCPU_Limits.max_exact_fragments the sink accepts: 2^20.  A larger value is refused at sink creation. */
#define GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS ((uint64_t)1 << 20)

/**
 * Default ::GSHCPU_Limits.max_exact_crossings: 2^20 pairs of outline pieces that cross inside one pixel (render.h, UC19).  The
 * most any pixel measured holds is 333496, the cost of a pixel at the default is bounded by the time of that many exchanges.
 */
#define GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS ((uint64_t)1 << 20)

/** The largest ::GSHCPU_Limits.max_exact_crossings the sink accepts: 2^32.  A larger value is refused at sink creation. */
#define GSHCPU_LIMITS_MAX_EXACT_CROSSINGS ((uint64_t)1 << 32)

/** ::GSHCPU_Limits.max_cache_bytes of 1: no stroke expansion is kept (every entry is larger). */
#define GSHCPU_LIMITS_CACHE_OFF ((uint64_t)1)

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
 * a truncation, and the output is left as it was.  Pass `SIZE_MAX` for no cap (`UINT64_MAX` for the cache).
 */
typedef struct GSHCPU_Limits {
  size_t max_dimension; ///< Widest and tallest working bitmap, in pixels (including the one-pixel margin ::gshcpu_coverage_from_path adds). At most ::GSHCPU_MAX_DIMENSION_CEILING whatever is asked.
  size_t max_bytes;     ///< Bytes of the working bitmap (width times height).
  /**
   * The standalone coverage functions (`gshcpu_coverage_*`) only: the cells they store, 24 bytes each, so a memory
   * bound.  Two deposits of one pixel merge only when consecutive; any others are separate cells until the sort.  The
   * sink never reads it (it stores no cells): see `max_deposits`.
   */
  size_t max_cells;
  size_t max_points;    ///< Points of the flattened path (handed to `shape-geometry`).
  /**
   * The sink only: the most crossings of an edge with a cell (deposits) it counts over what the whole render draws of one
   * item, summed over the parts of a stroke.  A render past it refuses the item as ::GSHSCENE_REASON_RESOURCE_LIMIT.  It
   * bounds work, about half a microsecond a deposit on the machine it was measured on, and not memory: the sink's memory
   * is a function of the other limits and the region only.  0 is ::GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS, `UINT64_MAX` is no
   * cap.  Where the time a caller can wait is what matters, a deadline through the render's cancel token is the better
   * control; this is the bound that holds without one.  A `uint64_t` whatever the target.  Not read by the coverage
   * functions.
   */
  uint64_t max_deposits;
  /**
   * The sink's cache of stroke expansions (render.h, SC7), in the bytes it accounts for them: 0 is
   * ::GSHCPU_LIMITS_DEFAULT_MAX_CACHE_BYTES, ::GSHCPU_LIMITS_CACHE_OFF (1) keeps nothing, `UINT64_MAX` has no cap.
   * A `uint64_t` whatever the target, so that the structure has the same layout on every one.  Read by the sink; the
   * coverage functions ignore it.
   */
  uint64_t max_cache_bytes;
  /**
   * The most pieces of one stroke part (render.h, UC9) the exact union coverage of a stroke handles in one pixel: edges of
   * the part's outline that cross it, and horizontal ones that touch its left side.  0 is
   * ::GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS (32768), any other value is used as given up to
   * ::GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS (2^20; a larger one is ::GSHCPU_ERR_INVALID at sink creation).  The step is
   * superlinear in the pieces of a pixel, so `UINT64_MAX` is not "no cap" here.  A pixel past it is the sink's resource
   * limit, ::GSHSCENE_REASON_RESOURCE_LIMIT, for the item, decided over what the whole render draws of it as the cell cap
   * is.  A `uint64_t` whatever the target, so that the structure has the same layout on every one.  Read by the sink; the
   * coverage functions ignore it.
   */
  uint64_t max_exact_fragments;
  /**
   * The most crossings the exact union coverage of a stroke makes in one pixel (render.h, UC19): the pairs of the part's
   * outline pieces that cross inside it, which is what the time of the pixel grows with (a pixel takes about a microsecond
   * for every three of them).  0 is ::GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS (2^20), any other value is used as given up to
   * ::GSHCPU_LIMITS_MAX_EXACT_CROSSINGS (2^32; a larger one is ::GSHCPU_ERR_INVALID at sink creation, and `UINT64_MAX` is
   * not "no cap").  A pixel past it is the sink's resource limit, ::GSHSCENE_REASON_RESOURCE_LIMIT, for the item, decided over
   * what the whole render draws of it as the cell cap and the pieces are, so that no pixel can cost more than the budget
   * whatever its pieces.  A `uint64_t` whatever the target, so that the structure has the same layout on every one.  Read by
   * the sink; the coverage functions ignore it.
   */
  uint64_t max_exact_crossings;
} GSHCPU_Limits;

/**
 * @brief Fill @p limits with the built-in defaults.
 *
 * @param limits The structure to fill; NULL is ignored.
 */
GSHCPU_API void gshcpu_limits_default(GSHCPU_Limits * limits);

/** The smallest memory budget ::gshcpu_limits_for_memory accepts: 1 MiB. */
#define GSHCPU_LIMITS_MIN_MEMORY ((uint64_t)1 << 20)

/**
 * @brief Fill @p limits from a memory budget instead of from the fixed defaults.
 *
 * Every field is the default of ::gshcpu_limits_default except the two that hold memory the size of the drawing:
 * `max_cells` (the standalone functions' stored cells, 24 bytes each) is set so that they may use up to half of @p bytes,
 * and `max_cache_bytes` (the sink's stroke cache) an eighth of it.  It sets no work bound: `max_deposits` holds no
 * memory, so a memory budget says nothing about it.  A drawing that needs more than a cap is refused as ever; the budget
 * is the caller's, so that the same scene is refused or drawn the same way wherever it runs.  The budget does not cover
 * the target, the working bitmaps (`max_bytes` bounds those) or the exact union's scratch (`max_exact_fragments`).
 *
 * The library does not ask the system how much memory is free: the answer would depend on the machine and the moment,
 * and a render's refusals are part of its result.  A caller that wants it reads it and passes what it will spend.
 *
 * @param limits The structure to fill.
 * @param bytes  The memory to give the cells and the cache together, at least ::GSHCPU_LIMITS_MIN_MEMORY.
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_INVALID for NULL @p limits or a budget under the minimum (nothing written).
 */
GSHCPU_API GSHCPU_Result gshcpu_limits_for_memory(GSHCPU_Limits * limits, uint64_t bytes);

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
