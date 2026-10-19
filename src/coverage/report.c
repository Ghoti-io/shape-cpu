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
 * What can be asked of a coverage: its pixels, their sum, its hash and its
 * dump, and its release.  The hash is FNV-1a 64 through `shape-geometry`'s
 * `GSHGEOM_Hash` (AD-21), so the spelling is stated once for the family.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-cpu/coverage.h>

#include <ghoti.io/shape-geometry/hash.h>

#include <stdint.h>
#include <string.h>

/** The format version of the hash and the dump. */
#define COVERAGE_FORMAT_VERSION 1u

/** The kind, so that a coverage never shares a stream with another intermediate: "COVR" little-endian. */
#define COVERAGE_KIND 0x52564f43u

void gshcpu_coverage_destroy(GSHCPU_Coverage * coverage) {
  if (!coverage) {
    return;
  }
  if (coverage->data && coverage->allocator) {
    coverage->allocator->free_fn(coverage->allocator->ctx, coverage->data);
  }
  memset(coverage, 0, sizeof *coverage);
}

uint8_t gshcpu_coverage_at(const GSHCPU_Coverage * coverage, uint32_t column,
    uint32_t row) {
  if (!coverage || !coverage->data || column >= coverage->width
      || row >= coverage->height) {
    return 0;
  }
  return coverage->data[(size_t)row * coverage->stride + column];
}

uint64_t gshcpu_coverage_total(const GSHCPU_Coverage * coverage) {
  uint64_t total = 0;

  if (!coverage || !coverage->data) {
    return 0;
  }
  for (uint32_t row = 0; row < coverage->height; ++row) {
    const uint8_t * line = coverage->data + (size_t)row * coverage->stride;

    for (uint32_t column = 0; column < coverage->width; ++column) {
      total += line[column];
    }
  }
  return total;
}

uint64_t gshcpu_coverage_hash(const GSHCPU_Coverage * coverage) {
  GSHGEOM_Hash hash;

  gshgeom_hash_init(&hash);
  if (!coverage) {
    return gshgeom_hash_value(&hash);
  }
  gshgeom_hash_u32(&hash, COVERAGE_FORMAT_VERSION);
  gshgeom_hash_u32(&hash, COVERAGE_KIND);
  gshgeom_hash_u32(&hash, coverage->width);
  gshgeom_hash_u32(&hash, coverage->height);
  gshgeom_hash_i32(&hash, coverage->left);
  gshgeom_hash_i32(&hash, coverage->top);
  gshgeom_hash_u32(&hash, (uint32_t)coverage->fill);
  /* The rows and not the buffer: the stride is an allocation detail and a
   * rendering that differed only in its padding is the same rendering. */
  if (coverage->data) {
    for (uint32_t row = 0; row < coverage->height; ++row) {
      gshgeom_hash_bytes(&hash,
          coverage->data + (size_t)row * coverage->stride, coverage->width);
    }
  }
  return gshgeom_hash_value(&hash);
}

GSHCPU_Result gshcpu_coverage_dump(const GSHCPU_Coverage * coverage,
    FILE * out) {
  if (!coverage || !out) {
    return GSHCPU_ERR_INVALID;
  }
  if (fprintf(out,
          "gshcpu.coverage %u %ux%u at %d,%d fill=%s total=%llu hash=%016llx\n",
          COVERAGE_FORMAT_VERSION, coverage->width, coverage->height,
          coverage->left, coverage->top,
          gshgeom_fill_rule_string(coverage->fill),
          (unsigned long long)gshcpu_coverage_total(coverage),
          (unsigned long long)gshcpu_coverage_hash(coverage))
      < 0) {
    return GSHCPU_ERR_IO;
  }
  /* ferror as well as fflush: on an unbuffered stream glibc's fprintf can return
   * a count while the stream's error flag is set. */
  return (fflush(out) == 0 && !ferror(out)) ? GSHCPU_OK : GSHCPU_ERR_IO;
}
