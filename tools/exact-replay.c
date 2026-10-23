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
 * The exact step on the cells of a real drawing, timed: reads the cells exact-census.c wrote (GSHCPU_CENSUS_DUMP) and runs the
 * library's step, and with -DWITH_EARLIER the earlier one kept in the tests too, over all of them a number of times,
 * printing the time of a cell, in all and by the pieces of the busiest part.  Built against the static archive:
 *
 *     cc -std=c17 -O2 -I src/render ... tools/exact-replay.c build/.../libghoti.io-shape-cpu-0.a ...
 *
 *     exact-replay CELLS [REPEATS]
 *
 * Not part of the library.
 */

#define _POSIX_C_SOURCE 200809L

#include "render_internal.h"
#include "../tests/support/exact_s4b.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct Cell {
  GSHCPU_Exact_Params params;
  GSHCPU_Exact_Cell cell;
  GSHCPU_Piece * pieces[3];
  size_t most;
} Cell;

static uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

int main(int argc, char ** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: exact-replay CELLS [REPEATS]\n");
    return 2;
  }
  FILE * f = fopen(argv[1], "rb");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  int repeats = argc > 2 ? atoi(argv[2]) : 5;
  if (argc > 3 && !strcmp(argv[3], "earlier")) {
    gshcpu_exact_hook = gshcpu_s4b_exact_cell;
  }
  size_t cap = 1024;
  size_t n = 0;
  Cell * cells = malloc(cap * sizeof *cells);
  int32_t head[12];
  size_t biggest = 1;
  while (fread(head, sizeof head, 1, f) == 1) {
    if (n == cap) {
      cap *= 2;
      cells = realloc(cells, cap * sizeof *cells);
    }
    Cell * c = &cells[n++];
    memset(c, 0, sizeof *c);
    c->params.mode = head[0];
    c->params.parts = head[1];
    c->params.even_odd = head[2];
    c->params.complement = head[3];
    for (int p = 0; p < 3; p++) {
      c->params.eps[p] = head[4 + p];
      c->cell.count[p] = (size_t)head[7 + p];
      int64_t v[2];
      if (fread(v, sizeof v, 1, f) != 1) {
        return 1;
      }
      c->cell.a_left[p] = v[0];
      c->cell.dense[p] = v[1];
    }
    for (int p = 0; p < c->params.parts; p++) {
      c->pieces[p] = malloc((c->cell.count[p] + 1) * sizeof(GSHCPU_Piece));
      if (fread(c->pieces[p], sizeof(GSHCPU_Piece), c->cell.count[p], f) != c->cell.count[p]) {
        return 1;
      }
      c->cell.pieces[p] = c->pieces[p];
      c->most = c->cell.count[p] > c->most ? c->cell.count[p] : c->most;
    }
    biggest = c->most > biggest ? c->most : biggest;
  }
  fclose(f);
  size_t slots = biggest * 3 + 16;
  size_t bytes = gshcpu_exact_scratch_bytes(slots, 1);
  unsigned char * memory = malloc(bytes + 16);
  GSHCPU_Exact_Scratch scratch;
  gshcpu_exact_scratch_init(&scratch, (void *)(((uintptr_t)memory + 15u) & ~(uintptr_t)15u), slots, 1);
  static const size_t limits[7] = {8, 16, 64, 256, 1024, 16384, (size_t)-1};
  uint64_t band_ns[7] = {0};
  uint64_t band_cells[7] = {0};
  uint64_t total = 0;
  uint64_t sink = 0;
  for (int r = 0; r < repeats; r++) {
    for (size_t i = 0; i < n; i++) {
      Cell * c = &cells[i];
      int64_t out = 0;
      uint64_t work = 0;
      uint64_t t0 = now_ns();
      int rc = gshcpu_exact_cell(&c->params, &scratch, &c->cell, &out, &work);
      uint64_t t1 = now_ns();
      sink += (uint64_t)out + (uint64_t)rc;
      size_t b = 0;
      while (c->most > limits[b]) {
        b++;
      }
      band_ns[b] += t1 - t0;
      band_cells[b]++;
      total += t1 - t0;
    }
  }
  printf("exact-replay: %zu cells, %d repeats, %.1f ns a cell in all, %.3f s (sink %llu)\n", n, repeats,
      (double)total / ((double)n * repeats), (double)total / 1e9, (unsigned long long)sink);
  for (int b = 0; b < 7; b++) {
    if (band_cells[b]) {
      printf("  <=%-6zu %9llu cells %9.1f ns a cell\n", limits[b], (unsigned long long)band_cells[b],
          (double)band_ns[b] / (double)band_cells[b]);
    }
  }
  return 0;
}
