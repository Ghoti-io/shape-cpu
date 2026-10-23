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
 * Both exact steps on every cell of a drawing, compared cell by cell: a preloaded object that stands between the scan
 * converter and the step, runs the library's step and the earlier one kept in the tests (tests/support/exact_s4b.c, built
 * here with its winding bound raised so that it follows a deep drawing), returns the library's result, and records every cell
 * in which the two differ.
 *
 *     make -C libs/shape-cpu install PREFIX=P EXTRA_CFLAGS='-fvisibility=default'
 *     cc -std=c17 -O2 -fPIC -shared -DEXACT_WINDING_MAX=32767 -I libs/shape-cpu/src/render \
 *        `pkg-config --cflags ghoti.io-shape-cpu-0` -o exact-dual.so libs/shape-cpu/tools/exact-dual.c -ldl -lpthread
 *     GSHCPU_DUAL_OUT=cells.txt LD_PRELOAD=./exact-dual.so program ...
 *
 * At exit it prints how many cells were compared, how many differ and by how much, how many of those hold more pieces of a
 * part than the earlier step was written for (512), and one status difference for each way the two can refuse differently;
 * with GSHCPU_DUAL_OUT it appends a line for each cell that differs, `CELL mode parts even_odd complement eps0 eps1 eps2 a_left
 * dense...`, then each part's pieces, which tools/check-dual-cells.py judges against the exact area in rational arithmetic.
 * Not part of the library.
 */

#define _GNU_SOURCE

#include "../tests/support/exact_s4b.c"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

typedef int (*Step_Fn)(const GSHCPU_Exact_Params *, GSHCPU_Exact_Scratch *, const GSHCPU_Exact_Cell *, int64_t *, uint64_t *);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Step_Fn g_real;
static uint64_t g_cells, g_same, g_differ, g_differ_by_one, g_differ_more, g_differ_dense, g_status_differ;
static uint64_t g_old_refuses, g_new_refuses, g_logged, g_max_diff;
static const uint64_t kLogLimit = 20000;

/* The time each step took, in all and by the pieces of the busiest part of the cell: <= 8, 16, 64, 256, 1024, 16384, more. */
static uint64_t g_ns_now, g_ns_old;
static uint64_t g_band_cells[7], g_band_now[7], g_band_old[7];

static uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

int gshcpu_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  if (!g_real) {
    g_real = (Step_Fn)dlsym(RTLD_NEXT, "gshcpu_exact_cell");
    if (!g_real) {
      fprintf(stderr, "exact-dual: the library's gshcpu_exact_cell is not visible (build it with -fvisibility=default)\n");
      abort();
    }
  }
  int64_t now = 0;
  int64_t old = 0;
  uint64_t w_old = 0;
  uint64_t t0 = now_ns();
  int rn = g_real(params, s, cell, &now, work);
  uint64_t t1 = now_ns();
  int ro = gshcpu_s4b_exact_cell(params, NULL, cell, &old, &w_old);
  uint64_t t2 = now_ns();
  size_t most = 0;
  for (int p = 0; p < params->parts; p++) {
    most = cell->count[p] > most ? cell->count[p] : most;
  }
  pthread_mutex_lock(&g_lock);
  g_cells++;
  {
    size_t band = most <= 8 ? 0 : most <= 16 ? 1 : most <= 64 ? 2 : most <= 256 ? 3 : most <= 1024 ? 4 : most <= 16384 ? 5 : 6;
    g_ns_now += t1 - t0;
    g_ns_old += t2 - t1;
    g_band_cells[band]++;
    g_band_now[band] += t1 - t0;
    g_band_old[band] += t2 - t1;
  }
  if (rn != ro) {
    g_status_differ++;
    g_old_refuses += ro != GSHCPU_EXACT_OK && rn == GSHCPU_EXACT_OK;
    g_new_refuses += rn != GSHCPU_EXACT_OK && ro == GSHCPU_EXACT_OK;
  } else if (rn == GSHCPU_EXACT_OK) {
    if (now == old) {
      g_same++;
    } else {
      uint64_t d = (uint64_t)(now > old ? now - old : old - now);
      g_differ++;
      g_differ_by_one += d == 1;
      g_differ_more += d > 1;
      g_differ_dense += most > 512;
      g_max_diff = d > g_max_diff ? d : g_max_diff;
      const char * path = getenv("GSHCPU_DUAL_OUT");
      if (path && *path && g_logged < kLogLimit) {
        FILE * f = fopen(path, "a");
        if (f) {
          g_logged++;
          fprintf(f, "CELL %d %d %d %d %d %d %d new %lld old %lld", params->mode, params->parts, params->even_odd, params->complement,
              params->eps[0], params->eps[1], params->eps[2], (long long)now, (long long)old);
          for (int p = 0; p < params->parts; p++) {
            fprintf(f, " | %lld %lld", (long long)cell->a_left[p], (long long)cell->dense[p]);
            for (size_t i = 0; i < cell->count[p]; i++) {
              const GSHCPU_Piece q = cell->pieces[p][i];
              fprintf(f, " %d %d %d %d", q.x0, q.y0, q.x1, q.y1);
            }
          }
          fprintf(f, "\n");
          fclose(f);
        }
      }
    }
  }
  pthread_mutex_unlock(&g_lock);
  *out = now;
  return rn;
}

__attribute__((destructor)) static void dual_exit(void) {
  const char * path = getenv("GSHCPU_DUAL_SUMMARY");
  FILE * f = path && *path ? fopen(path, "a") : stderr;
  if (!f) {
    f = stderr;
  }
  fprintf(f,
      "exact-dual: %llu cells compared; %llu the same; %llu differ (%llu by one doubled unit, %llu by more, largest %llu; %llu of them "
      "in a cell of more than 512 pieces of a part); %llu with another status (the earlier step refused %llu that the library drew, "
      "the library refused %llu that the earlier drew)\n",
      (unsigned long long)g_cells, (unsigned long long)g_same, (unsigned long long)g_differ, (unsigned long long)g_differ_by_one,
      (unsigned long long)g_differ_more, (unsigned long long)g_max_diff, (unsigned long long)g_differ_dense,
      (unsigned long long)g_status_differ, (unsigned long long)g_old_refuses, (unsigned long long)g_new_refuses);
  fprintf(f, "exact-dual: the steps' own time: the library's %.3f s, the earlier one's %.3f s; by the pieces of the busiest part:\n",
      (double)g_ns_now / 1e9, (double)g_ns_old / 1e9);
  static const char * const names[7] = {"<=8", "<=16", "<=64", "<=256", "<=1024", "<=16384", "more"};
  for (int b = 0; b < 7; b++) {
    if (g_band_cells[b]) {
      fprintf(f, "  %-8s %9llu cells: library %9.1f ns a cell, earlier %9.1f ns a cell\n", names[b], (unsigned long long)g_band_cells[b],
          (double)g_band_now[b] / (double)g_band_cells[b], (double)g_band_old[b] / (double)g_band_cells[b]);
    }
  }
  if (f != stderr) {
    fclose(f);
  }
}
