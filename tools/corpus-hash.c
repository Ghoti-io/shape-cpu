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
 * The cross-target probe (CAP-6, CAP-12, AD-17): a fixed set of device paths,
 * filled under both rules, hashed in the library's own spelling and folded
 * into one hash.  Every target must print the committed value in
 * tests/corpus/coverage.hash: x86-64, i686 (SSE), arm64, big-endian and
 * Windows.  It is built by `make check-corpus-hash` on the host and by
 * suite/tools/xarch/shape.sh and suite/tools/xwin/shape.sh for the others.
 *
 * What it can see: byte order (the hash is written from shifts, the cells are
 * accumulated in 64 bits), a 32-bit `long` or `size_t` where one was assumed,
 * and a sign or rounding rule that depends on the host (the division of a
 * negative number).  The input is integers only, from a fixed generator.
 * Nothing in the library's coverage path is floating point, so the flags that
 * matter to `shape-geometry`'s probe cannot matter here, but its flattener
 * runs on the curves, and the probe is built with the same flags.
 *
 * -DSHAPE_CPU_PLANT_BIT flips one bit of one result before it is folded in: the
 * planted differing bit that the gate has to catch on the target it is built
 * for.  -DSHAPE_CPU_CONTROL_NATIVE_ORDER folds each result in the host's byte
 * order instead of the hash's: the control that shows a big-endian target
 * disagrees when the byte order is wrong.  (There is no control for a fused
 * multiply-add or for x87 excess precision, as there is in shape-geometry's
 * probe: nothing in the coverage path is floating point, so those flags cannot
 * change a bit here, and a control that cannot differ would show nothing.)
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GSHGEOM_Hash total;
static unsigned long count;

static uint64_t rng_state = UINT64_C(0x9E3779B97F4A7C15);

/* xorshift64*: the same integers everywhere. */
static uint64_t next(void) {
  rng_state ^= rng_state >> 12;
  rng_state ^= rng_state << 25;
  rng_state ^= rng_state >> 27;
  return rng_state * UINT64_C(2685821657736338717);
}

/* An integer in [lo, hi]. */
static int32_t between(int32_t lo, int32_t hi) {
  return lo + (int32_t)(next() % (uint64_t)((int64_t)hi - lo + 1));
}

static void fail(const char * what) {
  fprintf(stderr, "corpus-hash: %s\n", what);
  exit(1);
}

static void feed(uint64_t value) {
#ifdef SHAPE_CPU_PLANT_BIT
  if (count == 100) {
    value ^= 1u;
  }
#endif
  count++;
#ifdef SHAPE_CPU_CONTROL_NATIVE_ORDER
  /* The control: the bytes of the variable in the host's own order, which is
   * what the library's hash is written never to do.  On a little-endian host
   * this is the same stream as the real one, so it prints the committed hash;
   * on a big-endian host it prints another, which is what shows the probe can
   * see a byte-order slip. */
  gshgeom_hash_bytes(&total, &value, sizeof value);
#else
  gshgeom_hash_u64(&total, value);
#endif
}

/* Hash one coverage, trimmed or not, and release it. */
static void take(GSHCPU_Coverage * c) {
  feed(gshcpu_coverage_hash(c));
  feed(gshcpu_coverage_total(c));
  gshcpu_coverage_destroy(c);
}

static void polygon(GSHGEOM_Device_Path * p, int n, int32_t lo, int32_t hi,
    int32_t dx, int32_t dy) {
  for (int i = 0; i < n; i++) {
    GSHGEOM_Device_Point pt;
    pt.x = between(lo, hi) + dx;
    pt.y = between(lo, hi) + dy;
    GSHGEOM_Result r = i == 0 ? gshgeom_device_path_move_to(p, pt)
                              : gshgeom_device_path_line_to(p, pt);
    if (r != GSHGEOM_OK) {
      fail("cannot build a path");
    }
  }
  if (gshgeom_device_path_close(p) != GSHGEOM_OK) {
    fail("cannot close a path");
  }
}

int main(void) {
  gshgeom_hash_init(&total);

  /* 1. Random polygons, both rules, the rectangle found and trimmed. */
  for (int i = 0; i < 250; i++) {
    GSHGEOM_Device_Path * p = NULL;
    if (gshgeom_device_path_create(NULL, NULL, &p) != GSHGEOM_OK) {
      fail("cannot create a path");
    }
    int contours = 1 + (int)(next() % 3);
    for (int c = 0; c < contours; c++) {
      polygon(p, 3 + (int)(next() % 6), 0, 40 * 64, 0, 0);
    }
    for (int rule = 0; rule < GSHGEOM_FILL_RULE_COUNT; rule++) {
      GSHCPU_Coverage c;
      if (gshcpu_coverage_from_path(p, (GSHGEOM_Fill_Rule)rule, NULL, NULL, &c)
          != GSHCPU_OK) {
        fail("cannot fill a polygon");
      }
      take(&c);
    }
    gshgeom_device_path_destroy(p);
  }

  /* 2. Random curves: flattened by shape-geometry, then filled. */
  for (int i = 0; i < 120; i++) {
    GSHGEOM_Device_Path * p = NULL;
    if (gshgeom_device_path_create(NULL, NULL, &p) != GSHGEOM_OK) {
      fail("cannot create a path");
    }
    GSHGEOM_Device_Point a, b, c, d;
    a.x = between(0, 30 * 64); a.y = between(0, 30 * 64);
    b.x = between(-10 * 64, 40 * 64); b.y = between(-10 * 64, 40 * 64);
    c.x = between(-10 * 64, 40 * 64); c.y = between(-10 * 64, 40 * 64);
    d.x = between(0, 30 * 64); d.y = between(0, 30 * 64);
    if (gshgeom_device_path_move_to(p, a) != GSHGEOM_OK
        || gshgeom_device_path_quadratic_to(p, b, d) != GSHGEOM_OK
        || gshgeom_device_path_cubic_to(p, c, b, a) != GSHGEOM_OK
        || gshgeom_device_path_close(p) != GSHGEOM_OK) {
      fail("cannot build a curve path");
    }
    for (int rule = 0; rule < GSHGEOM_FILL_RULE_COUNT; rule++) {
      GSHCPU_Coverage cv;
      if (gshcpu_coverage_from_path(p, (GSHGEOM_Fill_Rule)rule, NULL, NULL, &cv)
          != GSHCPU_OK) {
        fail("cannot fill a curve path");
      }
      take(&cv);
    }
    gshgeom_device_path_destroy(p);
  }

  /* 3. The same polygons into a rectangle, untrimmed, with the open subpath
   * and the many-contours cases. */
  for (int i = 0; i < 80; i++) {
    GSHGEOM_Device_Path * p = NULL;
    if (gshgeom_device_path_create(NULL, NULL, &p) != GSHGEOM_OK) {
      fail("cannot create a path");
    }
    polygon(p, 3 + (int)(next() % 5), 64, 24 * 64, 0, 0);
    {
      /* Drawn into variables: the order of evaluation of the elements of an
       * initializer list is not specified, and GCC differs by target. */
      GSHGEOM_Device_Point m, l1, l2;
      m.x = between(64, 24 * 64);
      m.y = between(64, 24 * 64);
      l1.x = between(64, 24 * 64);
      l1.y = between(64, 24 * 64);
      l2.x = between(64, 24 * 64);
      l2.y = between(64, 24 * 64);
      if (gshgeom_device_path_move_to(p, m) != GSHGEOM_OK
          || gshgeom_device_path_line_to(p, l1) != GSHGEOM_OK
          || gshgeom_device_path_line_to(p, l2) != GSHGEOM_OK) {
        fail("cannot build an open subpath");
      }
    }
    GSHCPU_Coverage c;
    if (gshcpu_coverage_fill(p, (GSHGEOM_Fill_Rule)(i % 2), 0, 0, 25, 25, NULL,
            NULL, &c) != GSHCPU_OK) {
      fail("cannot fill into a rectangle");
    }
    take(&c);
    gshgeom_device_path_destroy(p);
  }

  /* 4. The extremes of the 26.6 range: a small shape moved to each corner by
   * whole pixels, which must hash with its own left and top. */
  {
    static const int64_t corners[2] = {INT32_MIN, 0x7FFFFFC0 - 64 * 60};
    for (int i = 0; i < 40; i++) {
      for (int cx = 0; cx < 2; cx++) {
        for (int cy = 0; cy < 2; cy++) {
          GSHGEOM_Device_Path * p = NULL;
          if (gshgeom_device_path_create(NULL, NULL, &p) != GSHGEOM_OK) {
            fail("cannot create a path");
          }
          polygon(p, 3 + (int)(next() % 4), 0, 30 * 64, (int32_t)corners[cx],
              (int32_t)corners[cy]);
          GSHCPU_Coverage c;
          if (gshcpu_coverage_from_path(p, (GSHGEOM_Fill_Rule)(i % 2), NULL,
                  NULL, &c) != GSHCPU_OK) {
            fail("cannot fill at a corner");
          }
          take(&c);
          gshgeom_device_path_destroy(p);
        }
      }
    }
  }

  printf("shape-cpu.hash %016" PRIx64 "\n", gshgeom_hash_value(&total));
  printf("shape-cpu.count %lu\n", count);
  return 0;
}
