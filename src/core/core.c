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
 * @file
 *
 * The result strings, the default limits and the version this build reports.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-cpu/core.h>
#include <ghoti.io/shape-cpu/libver.h>

const char * gshcpu_result_string(GSHCPU_Result result) {
  switch (result) {
  case GSHCPU_OK:
    return "ok";
  case GSHCPU_ERR_IO:
    return "input/output error";
  case GSHCPU_ERR_FORMAT:
    return "not a format handled here";
  case GSHCPU_ERR_UNSUPPORTED:
    return "feature not supported";
  case GSHCPU_ERR_LIMIT:
    return "a limit was exceeded";
  case GSHCPU_ERR_CORRUPT:
    return "corrupt data";
  case GSHCPU_ERR_OOM:
    return "out of memory";
  case GSHCPU_ERR_INVALID:
    return "invalid argument";
  case GSHCPU_ERR_INTERNAL:
    return "internal error";
  case GSHCPU_ERR_REFUSED:
    return "refused";
  case GSHCPU_ERR_CANCELLED:
    return "cancelled";
  case GSHCPU_RESULT_COUNT:
    break;
  }
  return "unknown result";
}

void gshcpu_limits_default(GSHCPU_Limits * limits) {
  if (!limits) {
    return;
  }
  limits->max_dimension = GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;
  limits->max_bytes = GSHCPU_LIMITS_DEFAULT_MAX_BYTES;
  limits->max_cells = GSHCPU_LIMITS_DEFAULT_MAX_CELLS;
  /* Zero: shape-geometry's own default for a flattened path. */
  limits->max_points = 0;
  limits->max_cache_bytes = 0; /* the default, GSHCPU_LIMITS_DEFAULT_MAX_CACHE_BYTES */
  limits->max_deposits = 0; /* the default, GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS */
  limits->max_exact_fragments = 0; /* the default, GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS */
  limits->max_exact_crossings = 0; /* the default, GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS */
}

GSHCPU_Result gshcpu_limits_for_memory(GSHCPU_Limits * limits, uint64_t bytes) {
  if (!limits || bytes < GSHCPU_LIMITS_MIN_MEMORY) {
    return GSHCPU_ERR_INVALID;
  }
  gshcpu_limits_default(limits);
  uint64_t cells = (bytes / 2) / 24;
  limits->max_cells = cells > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)cells;
  limits->max_cache_bytes = bytes / 8;
  return GSHCPU_OK;
}

const char * gshcpu_version_string(void) {
  return GSHCPU_VERSION_STRING;
}

unsigned gshcpu_version_number(void) {
  return GSHCPU_VERSION_NUMBER;
}
