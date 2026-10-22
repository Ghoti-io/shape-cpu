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
 * The sink's cache of stroke expansions (render.h, SC7; strokes story S4, F2).
 *
 * An entry is an immutable outline with the key it was made for (the centerline's resource id, the style's, the bit
 * patterns of the canonical map's linear part and fraction, and the construction's revision) and what is known of
 * it without a pass over it.  A hit is the miss bit for bit, because the expansion is a function of exactly that
 * key (SC3) and nothing else; this file never decides anything about the outline, it only holds it.
 *
 * One `cutil` mutex guards the table, the recency list, the counters and every entry's pin count.  It is held for
 * a lookup, an insert and an eviction, and never across an expansion or a free of an outline that is not already
 * unlinked, so renders on different surfaces, `prepare` and `evict` from other threads (S14) may overlap.  An
 * entry a render pinned is never freed under it: an eviction unlinks it and the last release frees it.
 *
 * The bound is on the bytes the table accounts for (the sum of `bytes` of the listed entries, each the points and
 * subpaths of its parts and a fixed overhead): least recently used first, an entry larger than the bound is not
 * kept, and an entry that cannot be made room for (everything older is pinned) is not kept either.  So the memory
 * held never passes the bound by more than the entries a render has pinned and the cache dropped.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/mutex.h>

#include <string.h>

/** Buckets of the table: a power of two. */
#define BUCKETS 256u

typedef struct Entry {
  struct Entry * chain;         /* the bucket's next */
  struct Entry * newer;         /* the recency list: toward the head */
  struct Entry * older;
  GSHCPU_Stroke_Key key;
  GSHGEOM_Stroke_Outline * outline;
  GSHCPU_Stroke_Meta meta;
  size_t refs;                  /* renders that pinned it */
  int listed;                   /* in the table and the list: false once dropped */
} Entry;

struct GSHCPU_Stroke_Cache {
  GSHGEOM_Allocator alloc;
  GCU_MUTEX_T lock;
  uint64_t bound;               /* effective: never 0 */
  uint64_t held;                /* bytes of the listed entries */
  Entry * head;                 /* most recently used */
  Entry * tail;                 /* least recently used */
  Entry * buckets[BUCKETS];
  GSHCPU_Render_Counters * counters;
};

static size_t bucket_of(const GSHCPU_Stroke_Key * k) {
  uint64_t h = k->path_id * UINT64_C(0x9E3779B97F4A7C15);
  h ^= (k->style_id + UINT64_C(0x632BE59BD9B4E019)) * UINT64_C(0xC2B2AE3D27D4EB4F);
  for (size_t i = 0; i < 4; i++) {
    h = (h ^ k->linear[i]) * UINT64_C(0x100000001B3);
  }
  h = (h ^ k->fraction[0]) * UINT64_C(0x100000001B3);
  h = (h ^ k->fraction[1]) * UINT64_C(0x100000001B3);
  h ^= h >> 29;
  return (size_t)(h & (BUCKETS - 1u));
}

static int key_equal(const GSHCPU_Stroke_Key * a, const GSHCPU_Stroke_Key * b) {
  return a->path_id == b->path_id && a->style_id == b->style_id && memcmp(a->linear, b->linear, sizeof a->linear) == 0 &&
      memcmp(a->fraction, b->fraction, sizeof a->fraction) == 0 && a->revision == b->revision;
}

GSHCPU_Result gshcpu_stroke_cache_create(const GSHGEOM_Allocator * alloc, uint64_t bound, GSHCPU_Render_Counters * counters,
    GSHCPU_Stroke_Cache ** out) {
  GSHCPU_Stroke_Cache * c = alloc->calloc_fn(alloc->ctx, 1, sizeof *c);
  if (!c) {
    return GSHCPU_ERR_OOM;
  }
  c->alloc = *alloc;
  c->bound = bound ? bound : GSHCPU_LIMITS_DEFAULT_MAX_CACHE_BYTES;
  c->counters = counters;
  if (GCU_MUTEX_CREATE(c->lock) != 0) {
    alloc->free_fn(alloc->ctx, c);
    return GSHCPU_ERR_INTERNAL;
  }
  *out = c;
  return GSHCPU_OK;
}

/** Take @p e out of the table and the list (the lock held). */
static void unlink_entry(GSHCPU_Stroke_Cache * c, Entry * e) {
  Entry ** at = &c->buckets[bucket_of(&e->key)];
  while (*at != e) {
    at = &(*at)->chain;
  }
  *at = e->chain;
  if (e->newer) {
    e->newer->older = e->older;
  } else {
    c->head = e->older;
  }
  if (e->older) {
    e->older->newer = e->newer;
  } else {
    c->tail = e->newer;
  }
  e->chain = e->newer = e->older = NULL;
  e->listed = 0;
  c->held -= e->meta.bytes;
}

static void free_entry(GSHCPU_Stroke_Cache * c, Entry * e) {
  gshgeom_stroke_outline_destroy(e->outline);
  c->alloc.free_fn(c->alloc.ctx, e);
}

void gshcpu_stroke_cache_destroy(GSHCPU_Stroke_Cache * c) {
  if (!c) {
    return;
  }
  GCU_MUTEX_LOCK(c->lock);
  while (c->head) {
    Entry * e = c->head;
    unlink_entry(c, e);
    if (c->counters) {
      c->counters->cache_evictions++;
    }
    free_entry(c, e); /* no render is in flight (the contract destroys a sink with no job): nothing is pinned */
  }
  GCU_MUTEX_UNLOCK(c->lock);
  GCU_MUTEX_DESTROY(c->lock);
  GSHGEOM_Allocator alloc = c->alloc;
  alloc.free_fn(alloc.ctx, c);
}

uint64_t gshcpu_stroke_cache_bytes(GSHCPU_Stroke_Cache * c) {
  GCU_MUTEX_LOCK(c->lock);
  uint64_t held = c->held;
  GCU_MUTEX_UNLOCK(c->lock);
  return held;
}

void gshcpu_stroke_cache_evict(GSHCPU_Stroke_Cache * c, uint64_t id) {
  Entry * doomed = NULL;
  GCU_MUTEX_LOCK(c->lock);
  for (size_t b = 0; b < BUCKETS; b++) {
    Entry * e = c->buckets[b];
    while (e) {
      Entry * next = e->chain;
      if (e->key.path_id == id || e->key.style_id == id) {
        unlink_entry(c, e);
        if (c->counters) {
          c->counters->cache_evictions++;
        }
        if (e->refs == 0) {
          e->chain = doomed; /* freed outside the lock */
          doomed = e;
        }
      }
      e = next;
    }
  }
  GCU_MUTEX_UNLOCK(c->lock);
  while (doomed) {
    Entry * next = doomed->chain;
    free_entry(c, doomed);
    doomed = next;
  }
}

int gshcpu_stroke_cache_acquire(GSHCPU_Stroke_Cache * c, const GSHCPU_Stroke_Key * key, void ** entry,
    GSHGEOM_Stroke_Outline ** outline, GSHCPU_Stroke_Meta * meta) {
  int found = 0;
  GCU_MUTEX_LOCK(c->lock);
  for (Entry * e = c->buckets[bucket_of(key)]; e; e = e->chain) {
    if (key_equal(&e->key, key)) {
      e->refs++;
      /* To the head of the recency list. */
      if (c->head != e) {
        if (e->newer) {
          e->newer->older = e->older;
        }
        if (e->older) {
          e->older->newer = e->newer;
        }
        if (c->tail == e) {
          c->tail = e->newer;
        }
        e->newer = NULL;
        e->older = c->head;
        c->head->newer = e;
        c->head = e;
      }
      *entry = e;
      *outline = e->outline;
      *meta = e->meta;
      if (c->counters) {
        c->counters->cache_hits++;
      }
      found = 1;
      break;
    }
  }
  GCU_MUTEX_UNLOCK(c->lock);
  return found;
}

int gshcpu_stroke_cache_store(GSHCPU_Stroke_Cache * c, const GSHCPU_Stroke_Key * key, GSHGEOM_Stroke_Outline * outline,
    const GSHCPU_Stroke_Meta * meta, void ** entry, GSHGEOM_Stroke_Outline ** held) {
  uint64_t need = (uint64_t)meta->bytes + sizeof(Entry);
  /* An entry that cannot be kept is not allocated; one that can be and fails to be allocated is out of memory like any
   * other allocation of a render (never swallowed: the caller still owns the outline). */
  Entry * fresh = need <= c->bound ? c->alloc.malloc_fn(c->alloc.ctx, sizeof *fresh) : NULL;
  if (need <= c->bound && !fresh) {
    return -1;
  }
  Entry * doomed = NULL;
  int result = 0;
  GCU_MUTEX_LOCK(c->lock);
  if (c->counters) {
    c->counters->cache_misses++;
  }
  for (Entry * e = c->buckets[bucket_of(key)]; e; e = e->chain) {
    if (key_equal(&e->key, key)) {
      e->refs++;
      *entry = e;
      *held = e->outline;
      result = 2;
      break;
    }
  }
  if (result == 0 && fresh) {
    /* Make room, least recently used first, never an entry in use. */
    uint64_t room = c->bound - need;
    Entry * e = c->tail;
    while (c->held > room && e) {
      Entry * newer = e->newer;
      if (e->refs == 0) {
        unlink_entry(c, e);
        if (c->counters) {
          c->counters->cache_evictions++;
        }
        e->chain = doomed;
        doomed = e;
      }
      e = newer;
    }
    if (c->held <= room) {
      memset(fresh, 0, sizeof *fresh);
      fresh->key = *key;
      fresh->outline = outline;
      fresh->meta = *meta;
      fresh->meta.bytes = (size_t)need; /* what is held and given back is the same number */
      fresh->refs = 1;
      fresh->listed = 1;
      size_t b = bucket_of(key);
      fresh->chain = c->buckets[b];
      c->buckets[b] = fresh;
      fresh->older = c->head;
      if (c->head) {
        c->head->newer = fresh;
      } else {
        c->tail = fresh;
      }
      c->head = fresh;
      c->held += need;
      if (c->counters) {
        c->counters->cache_kept++;
      }
      *entry = fresh;
      *held = outline;
      fresh = NULL;
      result = 1;
    }
  }
  GCU_MUTEX_UNLOCK(c->lock);
  if (fresh) {
    c->alloc.free_fn(c->alloc.ctx, fresh);
  }
  while (doomed) {
    Entry * next = doomed->chain;
    free_entry(c, doomed);
    doomed = next;
  }
  return result;
}

void gshcpu_stroke_cache_release(GSHCPU_Stroke_Cache * c, void * entry) {
  Entry * e = entry;
  int free_it = 0;
  GCU_MUTEX_LOCK(c->lock);
  e->refs--;
  free_it = e->refs == 0 && !e->listed;
  GCU_MUTEX_UNLOCK(c->lock);
  if (free_it) {
    free_entry(c, e);
  }
}

void gshcpu_stroke_cache_count(GSHCPU_Stroke_Cache * c, size_t offset) {
  if (!c->counters) {
    return;
  }
  GCU_MUTEX_LOCK(c->lock);
  (*(uint64_t *)(void *)((unsigned char *)c->counters + offset))++;
  GCU_MUTEX_UNLOCK(c->lock);
}
