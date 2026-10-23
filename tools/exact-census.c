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
 * A census of the cells the exact step (render.h, UC1 to UC20) is made to handle, taken from any program that draws
 * strokes with the library.  It is a preloaded shared object that stands between the scan converter and the step:
 *
 *     make -C libs/shape-cpu install PREFIX=P EXTRA_CFLAGS='-fvisibility=default -DEXACT_WINDING_MAX=32767'
 *     cc -std=c17 -O2 -fPIC -shared -I libs/shape-cpu/src/render `pkg-config --cflags ghoti.io-shape-cpu-0` \
 *        -o exact-census.so libs/shape-cpu/tools/exact-census.c -ldl
 *     GSHCPU_CENSUS_OUT=report.txt LD_PRELOAD=./exact-census.so program ...
 *
 * (The first line is a library built with its symbols visible, so that the preloaded `gshcpu_exact_cell` is the one the
 * scan converter's calls reach; the second names the largest winding the step of S4b may follow, for a program whose
 * windings run deep.  The step of S4c has no such bound to lift and ignores it.)  For every call it measures the cell by
 * itself, from the pieces the call is given and with no help from the step's own working: the raw pieces and the distinct
 * fragments left once a piece and its reverse cancel, the slabs, and the crossings `K` of UC19, counted as the inversions
 * between the order of the fragments at the top of each slab and at its bottom.  The step's own figures of the cell are
 * read from its result: the largest winding it met and the time it took.  At exit it prints, for the cells of every call,
 * the maximum and the 50th, 99th and 99.9th percentile of each figure, the densest cells one by one, and the time the
 * step took in all.  Nothing is written when `GSHCPU_CENSUS_OUT` names no file and the program is not a terminal: the
 * report then goes to the standard error stream.
 *
 * Not part of the library and not linked into it: a tool for the people who set its limits.
 */

#define _GNU_SOURCE

#include "render_internal.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int (*Step_Fn)(const GSHCPU_Exact_Params *, GSHCPU_Exact_Scratch *, const GSHCPU_Exact_Cell *, int64_t *, uint64_t *);

/* --- what is kept of each cell ----------------------------------------------------------------------------------- */

typedef struct Record {
  uint32_t raw;        /* pieces handed to the step, all parts */
  uint32_t raw_part;   /* ... of the part with the most */
  uint32_t distinct;   /* fragments (nonzero height) after a piece and its reverse cancel, equal copies counted once */
  uint32_t weighted;   /* the same, equal copies counted: the pieces left over */
  uint32_t slabs;
  uint64_t crossings;  /* K */
  int32_t winding;     /* the largest the step met */
  int32_t status;
  int32_t mode;
  uint64_t ns;         /* the step's own time */
} Record;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Record * g_records;
static size_t g_count;
static size_t g_cap;
static Step_Fn g_real;
static uint64_t g_ns_total;

/* --- the measure --------------------------------------------------------------------------------------------------- */

typedef struct Frag {
  int32_t a;       /* x(u) = (a + dx u) / dy at depth u below the slab's top */
  int32_t dx;
  int32_t dy;
  uint32_t idx;
} Frag;

static uint64_t piece_key(GSHCPU_Piece pc) {
  uint32_t a = (uint32_t)pc.y0 * 65u + pc.x0;
  uint32_t b = (uint32_t)pc.y1 * 65u + pc.x1;
  return a <= b ? ((uint64_t)a << 33) | ((uint64_t)b << 1) | 1u : ((uint64_t)b << 33) | ((uint64_t)a << 1);
}

typedef struct Keyed {
  uint64_t key;
  GSHCPU_Piece pc;
} Keyed;

static int by_key(const void * x, const void * y) {
  uint64_t a = ((const Keyed *)x)->key;
  uint64_t b = ((const Keyed *)y)->key;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** The order at depth `0+`: by x there, then by slope, then by index (the step's comparator, restated). */
static int by_entry(const void * x, const void * y) {
  const Frag * p = x;
  const Frag * q = y;
  int64_t xp = (int64_t)p->a * q->dy;
  int64_t xq = (int64_t)q->a * p->dy;
  if (xp != xq) {
    return xp < xq ? -1 : 1;
  }
  int64_t sp = (int64_t)p->dx * q->dy;
  int64_t sq = (int64_t)q->dx * p->dy;
  if (sp != sq) {
    return sp < sq ? -1 : 1;
  }
  return p->idx < q->idx ? -1 : p->idx > q->idx ? 1 : 0;
}

typedef struct Bottom {
  int64_t num;  /* x at the slab's bottom, as num / den */
  int64_t den;
} Bottom;

/** Inversions of @p v (strict: a left element greater than a right one), by a merge sort that leaves @p v sorted. */
static uint64_t inversions(Bottom * v, Bottom * tmp, size_t n) {
  if (n < 2) {
    return 0;
  }
  size_t h = n / 2;
  uint64_t c = inversions(v, tmp, h) + inversions(v + h, tmp, n - h);
  size_t i = 0;
  size_t j = h;
  size_t k = 0;
  while (i < h && j < n) {
    if (v[i].num * v[j].den <= v[j].num * v[i].den) {
      tmp[k++] = v[i++];
    } else {
      c += (uint64_t)(h - i);
      tmp[k++] = v[j++];
    }
  }
  while (i < h) {
    tmp[k++] = v[i++];
  }
  while (j < n) {
    tmp[k++] = v[j++];
  }
  memcpy(v, tmp, n * sizeof *v);
  return c;
}

static void measure(const GSHCPU_Exact_Params * params, const GSHCPU_Exact_Cell * cell, Record * r) {
  size_t raw = 0;
  size_t raw_part = 0;
  for (int p = 0; p < params->parts; p++) {
    raw += cell->count[p];
    raw_part = cell->count[p] > raw_part ? cell->count[p] : raw_part;
  }
  r->raw = (uint32_t)raw;
  r->raw_part = (uint32_t)raw_part;
  Keyed * kept = malloc((raw + 1) * sizeof *kept);
  Frag * frag = malloc((raw + 1) * sizeof *frag);
  Frag * act = malloc((raw + 1) * sizeof *act);
  Bottom * bot = malloc((raw + 1) * sizeof *bot);
  Bottom * tmp = malloc((raw + 1) * sizeof *tmp);
  uint32_t * py = malloc((raw + 1) * sizeof *py);
  uint32_t * pe = malloc((raw + 1) * sizeof *pe);
  uint32_t * fx = malloc((raw + 1) * sizeof *fx);
  if (!kept || !frag || !act || !bot || !tmp || !py || !pe || !fx) {
    abort();
  }
  int32_t jnet[3][65];
  memset(jnet, 0, sizeof jnet);
  uint8_t mark[65];
  memset(mark, 0, sizeof mark);
  mark[0] = 1;
  mark[64] = 1;
  size_t nfrag = 0;
  size_t weighted = 0;
  for (int p = 0; p < params->parts; p++) {
    size_t n = cell->count[p];
    for (size_t i = 0; i < n; i++) {
      kept[i].pc = cell->pieces[p][i];
      kept[i].key = piece_key(kept[i].pc);
    }
    qsort(kept, n, sizeof *kept, by_key);
    for (size_t i = 0; i < n;) {
      uint64_t group = kept[i].key >> 1;
      size_t j = i;
      int64_t net = 0;
      GSHCPU_Piece rep = kept[i].pc;
      while (j < n && (kept[j].key >> 1) == group) {
        net += (kept[j].key & 1u) ? 1 : -1;
        if ((kept[j].key & 1u) ? net > 0 : net < 0) {
          rep = kept[j].pc;
        }
        j++;
      }
      i = j;
      if (net == 0) {
        continue;
      }
      uint32_t mu = (uint32_t)(net < 0 ? -net : net);
      /* the representative must be a piece of the surviving direction */
      for (size_t k = i; k-- > 0 && (kept[k].key >> 1) == group;) {
        if (((kept[k].key & 1u) != 0) == (net > 0)) {
          rep = kept[k].pc;
          break;
        }
      }
      weighted += mu;
      if (rep.x0 == 0) {
        jnet[p][rep.y0] -= (int32_t)mu;
      }
      if (rep.x1 == 0) {
        jnet[p][rep.y1] += (int32_t)mu;
      }
      if (rep.y0 != rep.y1) {
        int down = rep.y1 > rep.y0;
        Frag f;
        uint32_t top = down ? rep.y0 : rep.y1;
        uint32_t bottom = down ? rep.y1 : rep.y0;
        uint32_t px = down ? rep.x0 : rep.x1;
        f.dx = down ? rep.x1 - rep.x0 : rep.x0 - rep.x1;
        f.dy = (int32_t)(bottom - top);
        f.a = (int32_t)px * f.dy; /* at the fragment's own top: the slab's offset is added below */
        f.idx = (uint32_t)nfrag;
        frag[nfrag] = f;
        py[nfrag] = top;
        pe[nfrag] = bottom;
        fx[nfrag] = px;
        mark[top] = 1;
        mark[bottom] = 1;
        nfrag++;
      }
    }
    for (int h = 0; h <= 64; h++) {
      if (jnet[p][h] != 0) {
        mark[h] = 1;
      }
    }
  }
  r->distinct = (uint32_t)nfrag;
  r->weighted = (uint32_t)weighted;
  uint64_t crossings = 0;
  uint32_t slabs = 0;
  for (int h = 0; h <= 64; h++) {
    if (!mark[h]) {
      continue;
    }
    int yb = h + 1;
    while (yb <= 64 && !mark[yb]) {
      yb++;
    }
    if (yb > 64) {
      break;
    }
    slabs++;
    int ya = h;
    int height = yb - ya;
    size_t n = 0;
    for (size_t i = 0; i < nfrag; i++) {
      if ((int)py[i] <= ya && (int)pe[i] >= yb) {
        act[n] = frag[i];
        act[n].a = (int32_t)fx[i] * frag[i].dy + (ya - (int)py[i]) * frag[i].dx;
        n++;
      }
    }
    if (n < 2) {
      continue;
    }
    qsort(act, n, sizeof *act, by_entry);
    for (size_t i = 0; i < n; i++) {
      bot[i].num = (int64_t)act[i].a + (int64_t)act[i].dx * height;
      bot[i].den = act[i].dy;
    }
    crossings += inversions(bot, tmp, n);
  }
  r->slabs = slabs;
  r->crossings = crossings;
  free(kept);
  free(frag);
  free(act);
  free(bot);
  free(tmp);
  free(py);
  free(pe);
  free(fx);
}

/* --- the interposed step ------------------------------------------------------------------------------------------- */

static uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/*
 * With GSHCPU_CENSUS_DUMP=FILE and GSHCPU_CENSUS_EVERY=N, every N-th cell is also written to FILE, for a benchmark of the step on
 * the cells of a real drawing (tools/exact-replay.c): a record is the settings (mode, parts, even-odd, complement, the three
 * signs, the three counts) then the three covers to the left and dense areas, as little-endian 64-bit integers, then the
 * pieces of each part, four bytes each.
 */
static void dump_cell(const GSHCPU_Exact_Params * params, const GSHCPU_Exact_Cell * cell) {
  static FILE * f;
  static unsigned long every;
  static unsigned long seen;
  if (!f && !every) {
    const char * path = getenv("GSHCPU_CENSUS_DUMP");
    const char * n = getenv("GSHCPU_CENSUS_EVERY");
    every = n && *n ? strtoul(n, NULL, 10) : 100;
    every = every ? every : 100;
    f = path && *path ? fopen(path, "wb") : NULL;
    if (!f) {
      every = (unsigned long)-1;
    }
  }
  if (!f || (seen++ % every) != 0) {
    return;
  }
  int32_t head[12];
  head[0] = params->mode;
  head[1] = params->parts;
  head[2] = params->even_odd;
  head[3] = params->complement;
  for (int p = 0; p < 3; p++) {
    head[4 + p] = params->eps[p];
    head[7 + p] = (int32_t)cell->count[p];
  }
  head[10] = 0;
  head[11] = 0;
  fwrite(head, sizeof head, 1, f);
  for (int p = 0; p < 3; p++) {
    int64_t v[2] = {cell->a_left[p], cell->dense[p]};
    fwrite(v, sizeof v, 1, f);
  }
  for (int p = 0; p < params->parts; p++) {
    fwrite(cell->pieces[p], sizeof(GSHCPU_Piece), cell->count[p], f);
  }
}

int gshcpu_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  if (!g_real) {
    g_real = (Step_Fn)dlsym(RTLD_NEXT, "gshcpu_exact_cell");
    if (!g_real) {
      fprintf(stderr, "exact-census: the library's gshcpu_exact_cell is not visible (build it with -fvisibility=default)\n");
      abort();
    }
  }
  Record r;
  memset(&r, 0, sizeof r);
  measure(params, cell, &r);
  dump_cell(params, cell);
  uint64_t t0 = now_ns();
  int rc = g_real(params, s, cell, out, work);
  uint64_t t1 = now_ns();
  r.ns = t1 - t0;
  r.status = rc;
  r.mode = params->mode;
  r.winding = (int32_t)s->last_wmax;
  pthread_mutex_lock(&g_lock);
  if (g_count == g_cap) {
    g_cap = g_cap ? g_cap * 2 : 1u << 16;
    g_records = realloc(g_records, g_cap * sizeof *g_records);
    if (!g_records) {
      abort();
    }
  }
  g_records[g_count++] = r;
  g_ns_total += r.ns;
  pthread_mutex_unlock(&g_lock);
  return rc;
}

/* --- the report ---------------------------------------------------------------------------------------------------- */

static int cmp_u64(const void * x, const void * y) {
  uint64_t a = *(const uint64_t *)x;
  uint64_t b = *(const uint64_t *)y;
  return a < b ? -1 : a > b ? 1 : 0;
}

/** Print the figure whose value for record i is @p v[i]: its maximum and percentiles over the cells. */
static void summarize(FILE * f, const char * name, uint64_t * v, size_t n) {
  if (n == 0) {
    return;
  }
  qsort(v, n, sizeof *v, cmp_u64);
  size_t i50 = (n - 1) / 2;
  size_t i99 = (size_t)((double)(n - 1) * 0.99);
  size_t i999 = (size_t)((double)(n - 1) * 0.999);
  fprintf(f, "  %-22s max %-10llu p99.9 %-8llu p99 %-8llu p50 %-8llu\n", name, (unsigned long long)v[n - 1],
      (unsigned long long)v[i999], (unsigned long long)v[i99], (unsigned long long)v[i50]);
}

static void report(void) {
  const char * path = getenv("GSHCPU_CENSUS_OUT");
  FILE * f = path && *path ? fopen(path, "a") : stderr;
  if (!f) {
    f = stderr;
  }
  size_t n = g_count;
  fprintf(f, "exact-census: %zu cells handed to the step, %.3f s in it\n", n, (double)g_ns_total / 1e9);
  uint64_t * v = malloc((n + 1) * sizeof *v);
  if (!v) {
    return;
  }
#define FIGURE(name, expr) \
  do { \
    for (size_t i = 0; i < n; i++) { \
      const Record * r = &g_records[i]; \
      v[i] = (uint64_t)(expr); \
    } \
    summarize(f, name, v, n); \
  } while (0)
  FIGURE("raw pieces (all parts)", r->raw);
  FIGURE("raw pieces (one part)", r->raw_part);
  FIGURE("distinct fragments", r->distinct);
  FIGURE("pieces after seams", r->weighted);
  FIGURE("slabs", r->slabs);
  FIGURE("crossings K", r->crossings);
  FIGURE("largest winding", r->winding < 0 ? -r->winding : r->winding);
  FIGURE("step time (ns)", r->ns);
#undef FIGURE
  free(v);
  uint64_t refused = 0;
  uint64_t over512 = 0;
  uint64_t ns_over512 = 0;
  uint64_t ns_all = 0;
  uint64_t bands[8] = {0};
  for (size_t i = 0; i < n; i++) {
    const Record * r = &g_records[i];
    refused += r->status != 0;
    if (r->raw_part > 512) {
      over512++;
      ns_over512 += r->ns;
    }
    ns_all += r->ns;
    size_t b = r->raw_part <= 8 ? 0 : r->raw_part <= 16 ? 1 : r->raw_part <= 64 ? 2 : r->raw_part <= 256 ? 3
             : r->raw_part <= 1024 ? 4 : r->raw_part <= 4096 ? 5 : r->raw_part <= 16384 ? 6 : 7;
    bands[b]++;
  }
  fprintf(f, "  cells the step refused: %llu; cells with more than 512 raw pieces in a part: %llu, %.3f s of the step\n",
      (unsigned long long)refused, (unsigned long long)over512, (double)ns_over512 / 1e9);
  fprintf(f, "  raw pieces of the busiest part, cells in each band: <=8 %llu, <=16 %llu, <=64 %llu, <=256 %llu, <=1024 %llu, "
             "<=4096 %llu, <=16384 %llu, more %llu\n",
      (unsigned long long)bands[0], (unsigned long long)bands[1], (unsigned long long)bands[2], (unsigned long long)bands[3],
      (unsigned long long)bands[4], (unsigned long long)bands[5], (unsigned long long)bands[6], (unsigned long long)bands[7]);
  (void)ns_all;
  /* The densest cells, one by one: the ten with the most raw pieces in a part, and the ten with the most crossings. */
  for (int pass = 0; pass < 2; pass++) {
    fprintf(f, "  the ten cells with the most %s:\n", pass == 0 ? "raw pieces" : "crossings");
    uint8_t * used = calloc(n + 1, 1);
    for (int top = 0; top < 10; top++) {
      size_t best = n;
      for (size_t i = 0; i < n; i++) {
        if (used[i]) {
          continue;
        }
        if (best == n ||
            (pass == 0 ? g_records[i].raw_part > g_records[best].raw_part : g_records[i].crossings > g_records[best].crossings)) {
          best = i;
        }
      }
      if (best == n) {
        break;
      }
      used[best] = 1;
      const Record * r = &g_records[best];
      fprintf(f, "    raw %u (part %u) distinct %u after seams %u slabs %u K %llu winding %d status %d time %.3f ms\n", r->raw,
          r->raw_part, r->distinct, r->weighted, r->slabs, (unsigned long long)r->crossings, (int)r->winding, r->status,
          (double)r->ns / 1e6);
    }
    free(used);
  }
  if (f != stderr) {
    fclose(f);
  }
}

__attribute__((destructor)) static void census_exit(void) {
  report();
}
