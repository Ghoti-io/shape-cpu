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
 * The one fold of an accumulated area into a coverage (F3, the magnitude fold), shared by the scan converter of
 * coverage.c and the dense cover of render/cover.c.  Nothing here is part of the library's interface.
 *
 * A sweep accumulates the signed area of the region in a pixel, in units of 1/8192 of a pixel (a whole pixel is
 * `2 * 64 * 64`).  Its sign is the winding's direction and says nothing about how much is covered, so the fold takes the
 * magnitude first, reduces it by the rule while it is still exact, and only then drops it to a level of 1/256:
 *
 *  - nonzero: `m = min(|area|, 8192)`;
 *  - even-odd: `m = |area| mod 16384`, reflected at 8192 (`16384 - m` above it), because winding 2 is outside again;
 *  - the level is `m >> 5` (the floor of the area's 256ths), and 256, a whole pixel, is 255.
 *
 * The floor comes after the magnitude and after the reduction, never before, so a region wound either way round has the
 * same coverage in every pixel.  (The fold this replaced shifted the signed area first, which rounds a negative area away
 * from zero, and so came out one level high in a pixel whose area was not a whole level when the contours were wound the
 * other way, and, under even-odd, in the ramp back down for either direction.)
 */

#ifndef GHOTI_IO_GSHCPU_FOLD_H
#define GHOTI_IO_GSHCPU_FOLD_H

#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-geometry/fill.h>

#include <stdint.h>

/** Sub-pixel bits (26.6, the family's pixel space) and the shift from an accumulated area into 0..256. */
#define GSHCPU_FOLD_PIXEL_BITS 6
#define GSHCPU_FOLD_AREA_SHIFT (2 * GSHCPU_FOLD_PIXEL_BITS + 1 - 8)

/** A whole pixel's accumulated area, `2 * 64 * 64`. */
#define GSHCPU_FOLD_WHOLE (INT64_C(2) << (2 * GSHCPU_FOLD_PIXEL_BITS))

/**
 * The rule-reduced magnitude of an accumulated @p area, in units of 1/8192 of a pixel, `0 <= result <= 8192`: what the coverage
 * byte is the shift of.  A union clip adds these over its members and shifts once (CU18).
 */
static inline uint32_t gshcpu_fold_area(int64_t area, GSHGEOM_Fill_Rule rule) {
  uint64_t m = (uint64_t)(area < 0 ? -area : area);
  if (rule == GSHGEOM_FILL_EVEN_ODD) {
    m &= 2u * (uint64_t)GSHCPU_FOLD_WHOLE - 1u; /* 16383: the winding's parity repeats every two pixels' area */
    if (m > (uint64_t)GSHCPU_FOLD_WHOLE) {
      m = 2u * (uint64_t)GSHCPU_FOLD_WHOLE - m;
    }
  } else if (m > (uint64_t)GSHCPU_FOLD_WHOLE) {
    m = (uint64_t)GSHCPU_FOLD_WHOLE; /* nonzero: a pixel's area held at the whole pixel */
  }
  return (uint32_t)m;
}

/** The coverage byte of an accumulated @p area under @p rule: `0..255`, the same for a region wound either way round. */
static inline uint8_t gshcpu_fold(int64_t area, GSHGEOM_Fill_Rule rule) {
  const uint32_t level = gshcpu_fold_area(area, rule) >> GSHCPU_FOLD_AREA_SHIFT;
  return (uint8_t)(level >= 256u ? 255u : level);
}

/**
 * ::gshcpu_fold, and the part of the area its byte leaves out: `area_level = 32 * byte + *rem`, where `area_level` is
 * ::gshcpu_fold_area, so that `0 <= *rem <= 32` (32 only for a whole pixel, whose byte is held at 255).  The copies of a pattern's tile
 * add these (PC4), so that the sum is the byte of the sum of the areas and not a sum of floors.
 */
static inline uint8_t gshcpu_fold_split(int64_t area, GSHGEOM_Fill_Rule rule, uint8_t * rem) {
  const uint32_t whole = gshcpu_fold_area(area, rule);
  const uint32_t level = whole >> GSHCPU_FOLD_AREA_SHIFT;
  const uint32_t byte = level >= 256u ? 255u : level;
  *rem = (uint8_t)(whole - (byte << GSHCPU_FOLD_AREA_SHIFT));
  return (uint8_t)byte;
}

#endif /* GHOTI_IO_GSHCPU_FOLD_H */
