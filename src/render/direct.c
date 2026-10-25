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
 * The direct 8-bit compositor of the fast stroke coverage, and the batch that spreads it over threads.
 *
 * The compositor takes the runs of coverage of one stroke and blends a solid premultiplied source into the target's bytes,
 * source-over in integers, each step rounded to 8 bits as the rasterizers it is compared with round it.
 *
 * The batch holds the strokes of a render until it is full or the render ends, each as a copy of the points of its outline, and
 * then draws them over stripes of the target's rows, a stripe to a task at a time: every stripe draws the strokes that reach it
 * in the order they were queued, and the sums an edge leaves in a row depend on the row alone (lean.c), so the picture is the
 * same bytes for any number of tasks.  A stripe is written by one task; the tasks share the queue and nothing else.  The tasks
 * are run by the caller's executor (render.h): this file makes no thread.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/atomic.h>

#include <stdlib.h>
#include <string.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

/* --- the compositor -------------------------------------------------------------------------------- */

#define D8_LOW 0x00ff00ff00ff00ffull
#define D8_ROUND 0x0080008000800080ull

/** `x / 255` rounded, in each of four 16-bit lanes holding at most 255 * 255. */
static inline uint64_t d8_div255(uint64_t x) {
  const uint64_t t = x + D8_ROUND;
  return ((t + ((t >> 8) & D8_LOW)) >> 8) & D8_LOW;
}

void gshcpu_direct8_color(GSHCPU_Direct8 * d, const uint8_t c[4]) {
  memcpy(d->bytes, c, 4);
  d->lanes = (uint64_t)c[0] | ((uint64_t)c[1] << 16) | ((uint64_t)c[2] << 32) | ((uint64_t)c[3] << 48);
  d->opaque = c[3] == 255;
}

#if defined(__SSE2__)
/** `x / 255` rounded, in each of eight 16-bit lanes holding at most 255 * 255: the scalar step's arithmetic. */
static inline __m128i d8_div255_v(__m128i x) {
  const __m128i t = _mm_add_epi16(x, _mm_set1_epi16(0x80));
  return _mm_srli_epi16(_mm_add_epi16(t, _mm_srli_epi16(t, 8)), 8);
}

/** Four pixels of @p row composed under the coverages @p m (four bytes): the scalar step, a pixel to each half-register. */
static inline void d8_four(unsigned char * row, const uint8_t * m, __m128i src) {
  const __m128i zero = _mm_setzero_si128();
  uint32_t m4;
  memcpy(&m4, m, sizeof m4);
  const __m128i mw = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)m4), zero); /* m0 m1 m2 m3 in 16 bits */
  const __m128i mm = _mm_unpacklo_epi16(mw, mw);
  const __m128i m_lo = _mm_unpacklo_epi32(mm, mm); /* m0 four times, m1 four times */
  const __m128i m_hi = _mm_unpackhi_epi32(mm, mm);
  const __m128i px = _mm_loadu_si128((const __m128i *)(const void *)row);
  const __m128i e_lo = _mm_unpacklo_epi8(px, zero);
  const __m128i e_hi = _mm_unpackhi_epi8(px, zero);
  const __m128i w_lo = d8_div255_v(_mm_mullo_epi16(src, m_lo)); /* the source weighed by the coverage */
  const __m128i w_hi = d8_div255_v(_mm_mullo_epi16(src, m_hi));
  const __m128i k255 = _mm_set1_epi16(255);
  /* the target's share: one minus the weighed alpha (lane 3 of each pixel, to all four lanes of it) */
  const __m128i keep_lo = _mm_sub_epi16(k255, _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_lo, 0xff), 0xff));
  const __m128i keep_hi = _mm_sub_epi16(k255, _mm_shufflehi_epi16(_mm_shufflelo_epi16(w_hi, 0xff), 0xff));
  __m128i r_lo = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_lo, keep_lo)), w_lo);
  __m128i r_hi = _mm_add_epi16(d8_div255_v(_mm_mullo_epi16(e_hi, keep_hi)), w_hi);
  r_lo = _mm_min_epi16(r_lo, k255); /* a lane past 255: the two roundings added up */
  r_hi = _mm_min_epi16(r_hi, k255);
  _mm_storeu_si128((__m128i *)(void *)row, _mm_packus_epi16(r_lo, r_hi));
}
#endif

void gshcpu_direct8_span(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  const GSHCPU_Direct8 * d = ctx;
  unsigned char * row = d->pixels + (size_t)(y + d->dy) * d->stride + 4u * (size_t)(x + d->dx);
  /* the next row of the stroke is near this one's columns, and the target is far larger than the cache: ask for it now */
  __builtin_prefetch(row + d->stride, 1, 3);
  __builtin_prefetch(row + d->stride + 4u * (n - 1u), 1, 3);
  size_t i = 0;
#if defined(__SSE2__)
  const __m128i src = _mm_set_epi64x((long long)d->lanes, (long long)d->lanes);
  for (; i + 4u <= n; i += 4, row += 16) {
    d8_four(row, m8 + i, src);
  }
#endif
  for (; i < n; i++, row += 4) {
    const uint32_t m = m8[i];
    if (m == 0) {
      continue;
    }
    if (m == 255u && d->opaque) {
      row[0] = d->bytes[0];
      row[1] = d->bytes[1];
      row[2] = d->bytes[2];
      row[3] = d->bytes[3];
      continue;
    }
    const uint64_t e = (uint64_t)row[0] | ((uint64_t)row[1] << 16) | ((uint64_t)row[2] << 32) | ((uint64_t)row[3] << 48);
    const uint64_t w = m == 255u ? d->lanes : d8_div255(d->lanes * m); /* the source weighed by the coverage */
    const uint64_t keep = 255u - (w >> 48); /* the target's share: one minus the weighed alpha */
    uint64_t r = d8_div255(e * keep) + w;
    const uint64_t over = (r >> 8) & 0x0001000100010001ull; /* a lane past 255: the two roundings added up */
    r = (r | (over * 255u)) & D8_LOW;
    row[0] = (uint8_t)r;
    row[1] = (uint8_t)(r >> 16);
    row[2] = (uint8_t)(r >> 32);
    row[3] = (uint8_t)(r >> 48);
  }
}

/* --- the batch ------------------------------------------------------------------------------------- */

#define BATCH_ENTRIES 256u
#define BATCH_POINTS 131072u /* 1 MiB of points */
#define BATCH_SUBPATHS 8192u
#define STRIPE_ROWS 32
/** The fewest queued strokes worth the threads' start: below it a flush is drawn by the caller alone. */
#define BATCH_MIN_ENTRIES 4u

typedef struct Entry {
  size_t point0;             /* its points, from the batch's array */
  size_t sub0, subs;         /* and its subpaths */
  int64_t shift_x, shift_y;
  GSHCPU_Rect rect;          /* the destination's window */
  GSHCPU_Direct8 d8;
} Entry;

typedef struct Worker {
  struct GSHCPU_Batch * batch;
  void * scratch;
  uint8_t * line;
} Worker;

struct GSHCPU_Batch {
  GSHGEOM_Allocator alloc;
  GSHCPU_Executor executor;
  unsigned threads;
  size_t width;              /* the widest window */
  unsigned char * pixels;
  size_t stride;
  Entry * entry;
  size_t entries;
  GSHGEOM_Device_Point * point;
  size_t points;
  GSHGEOM_Flat_Subpath * sub;
  size_t subs;
  Worker * worker;           /* one for each task, made at the first flush that needs them */
  size_t scratch_bytes;
  /* a flush's */
  GCU_Atomic_Int next;       /* the next stripe to take */
  GCU_Atomic_Int failed;     /* the first result that was not OK, as an int */
  int64_t y0;                /* the first row of stripe 0 */
  int64_t stripes;
};

GSHCPU_Batch * gshcpu_batch_create(const GSHGEOM_Allocator * alloc, const GSHCPU_Executor * executor, size_t width,
    unsigned char * pixels, size_t stride) {
  if (!executor || !executor->run || executor->threads < 2u || executor->threads > GSHCPU_EXECUTOR_MAX_THREADS || !width ||
      width > 65000u) {
    return NULL;
  }
  GSHCPU_Batch * b = alloc->calloc_fn(alloc->ctx, 1, sizeof *b);
  if (!b) {
    return NULL;
  }
  b->alloc = *alloc;
  b->executor = *executor;
  b->threads = (unsigned)executor->threads;
  b->width = width;
  b->pixels = pixels;
  b->stride = stride;
  b->entry = alloc->malloc_fn(alloc->ctx, BATCH_ENTRIES * sizeof *b->entry);
  b->point = alloc->malloc_fn(alloc->ctx, BATCH_POINTS * sizeof *b->point);
  b->sub = alloc->malloc_fn(alloc->ctx, BATCH_SUBPATHS * sizeof *b->sub);
  b->scratch_bytes = gshcpu_lean_bytes(width, STRIPE_ROWS);
  gcu_atomic_int_init(&b->next, 0);
  gcu_atomic_int_init(&b->failed, 0);
  if (!b->entry || !b->point || !b->sub) {
    gshcpu_batch_destroy(b);
    return NULL;
  }
  return b;
}

void gshcpu_batch_destroy(GSHCPU_Batch * b) {
  if (!b) {
    return;
  }
  GSHGEOM_Allocator a = b->alloc;
  if (b->worker) {
    for (unsigned i = 0; i < b->threads; i++) {
      a.free_fn(a.ctx, b->worker[i].scratch);
      a.free_fn(a.ctx, b->worker[i].line);
    }
    a.free_fn(a.ctx, b->worker);
  }
  a.free_fn(a.ctx, b->entry);
  a.free_fn(a.ctx, b->point);
  a.free_fn(a.ctx, b->sub);
  a.free_fn(a.ctx, b);
}

/** The stripe @p s of the flush, drawn by @p w: each queued stroke that reaches it, in the order queued. */
static GSHCPU_Result batch_stripe(GSHCPU_Batch * b, Worker * w, int64_t s) {
  const int64_t Y0 = b->y0 + s * STRIPE_ROWS;
  const int64_t Y1 = Y0 + STRIPE_ROWS;
  for (size_t i = 0; i < b->entries; i++) {
    const Entry * e = &b->entry[i];
    GSHCPU_Rect r = e->rect;
    if (r.y1 <= Y0 || r.y0 >= Y1) {
      continue;
    }
    r.y0 = r.y0 > Y0 ? r.y0 : Y0;
    r.y1 = r.y1 < Y1 ? r.y1 : Y1;
    GSHCPU_Result x = gshcpu_render_lean_cover_points(b->point + e->point0, b->sub + e->sub0, e->subs, e->shift_x, e->shift_y, r,
        w->scratch, b->scratch_bytes, w->line, gshcpu_direct8_span, (void *)&e->d8);
    if (x != GSHCPU_OK) {
      return x;
    }
  }
  return GSHCPU_OK;
}

/** Take stripes until there are none, or one fails. */
static void batch_work(Worker * w) {
  GSHCPU_Batch * b = w->batch;
  for (;;) {
    if (gcu_atomic_int_load(&b->failed) != 0) {
      return;
    }
    const int64_t s = gcu_atomic_int_fetch_add(&b->next, 1);
    if (s >= b->stripes) {
      return;
    }
    GSHCPU_Result x = batch_stripe(b, w, s);
    if (x != GSHCPU_OK) {
      int32_t none = 0;
      (void)gcu_atomic_int_compare_exchange(&b->failed, &none, (int32_t)x);
      return;
    }
  }
}

/** The executor's task: the stripes taken by task @p index with its own memory. */
static void batch_task(void * ctx, size_t index) {
  GSHCPU_Batch * b = ctx;
  if (index < b->threads) {
    batch_work(&b->worker[index]);
  }
}

/** The tasks' memory: a scratch and a row of coverages each.  @return 1 when they are there. */
static int batch_workers(GSHCPU_Batch * b) {
  if (b->worker) {
    return 1;
  }
  const GSHGEOM_Allocator * a = &b->alloc;
  b->worker = a->calloc_fn(a->ctx, b->threads, sizeof *b->worker);
  if (!b->worker) {
    return 0;
  }
  for (unsigned i = 0; i < b->threads; i++) {
    Worker * w = &b->worker[i];
    w->batch = b;
    w->scratch = a->calloc_fn(a->ctx, 1, b->scratch_bytes);
    w->line = a->malloc_fn(a->ctx, b->width);
    if (!w->scratch || !w->line) {
      return 0; /* the partial set is freed with the batch; the caller does not use it */
    }
  }
  return 1;
}

GSHCPU_Result gshcpu_batch_flush(GSHCPU_Batch * b) {
  if (!b || b->entries == 0) {
    return GSHCPU_OK;
  }
  int64_t lo = b->entry[0].rect.y0;
  int64_t hi = b->entry[0].rect.y1;
  for (size_t i = 1; i < b->entries; i++) {
    lo = b->entry[i].rect.y0 < lo ? b->entry[i].rect.y0 : lo;
    hi = b->entry[i].rect.y1 > hi ? b->entry[i].rect.y1 : hi;
  }
  b->y0 = lo;
  b->stripes = (hi - lo + STRIPE_ROWS - 1) / STRIPE_ROWS;
  gcu_atomic_int_store(&b->next, 0);
  gcu_atomic_int_store(&b->failed, 0);
  if (!batch_workers(b)) {
    b->entries = b->points = b->subs = 0;
    return GSHCPU_ERR_OOM;
  }
  /* A flush of few strokes or a few stripes does not pay for the tasks' wake: the caller draws it alone. */
  if (b->entries >= BATCH_MIN_ENTRIES && b->stripes >= 2) {
    /* The executor runs the tasks (a refusal, or a task it did not run, leaves its stripes to the rest and to the pass below). */
    (void)b->executor.run(b->executor.user, b->threads, batch_task, b);
  }
  batch_work(&b->worker[0]); /* whatever is left: all of it when no executor ran, nothing when they did */
  b->entries = b->points = b->subs = 0;
  return (GSHCPU_Result)gcu_atomic_int_load(&b->failed);
}

int gshcpu_batch_add(GSHCPU_Batch * b, const GSHGEOM_Flat_Path * path, int64_t shift_x, int64_t shift_y, GSHCPU_Rect rect,
    const uint8_t color[4], GSHCPU_Result * result) {
  *result = GSHCPU_OK;
  const size_t points = gshgeom_flat_path_point_count(path);
  const size_t subs = gshgeom_flat_path_subpath_count(path);
  if (points > BATCH_POINTS || subs > BATCH_SUBPATHS || rect.x1 - rect.x0 > (int64_t)b->width) {
    return 0; /* more than a batch holds, or wider than its threads' scratch: the caller draws it itself, after the flush */
  }
  if (b->entries == BATCH_ENTRIES || b->points + points > BATCH_POINTS || b->subs + subs > BATCH_SUBPATHS) {
    *result = gshcpu_batch_flush(b);
    if (*result != GSHCPU_OK) {
      return -1;
    }
  }
  Entry * e = &b->entry[b->entries];
  e->point0 = b->points;
  e->sub0 = b->subs;
  e->subs = subs;
  e->shift_x = shift_x;
  e->shift_y = shift_y;
  e->rect = rect;
  e->d8.pixels = b->pixels;
  e->d8.stride = b->stride;
  e->d8.dx = shift_x;
  e->d8.dy = shift_y;
  gshcpu_direct8_color(&e->d8, color);
  if (points) {
    memcpy(b->point + b->points, gshgeom_flat_path_points(path), points * sizeof *b->point);
  }
  for (size_t i = 0; i < subs; i++) {
    if (gshgeom_flat_path_subpath(path, i, &b->sub[b->subs + i]) != GSHGEOM_OK) {
      *result = GSHCPU_ERR_INTERNAL;
      return -1;
    }
  }
  b->points += points;
  b->subs += subs;
  b->entries++;
  return 1;
}
