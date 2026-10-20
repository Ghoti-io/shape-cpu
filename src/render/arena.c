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
 * The arena an item's geometry is allocated from under `CALLER_MEMORY` (S9),
 * and its measure.  Both follow one rule, so that the measure of a list is the
 * size an arena needs to repeat it: a block is a 16-byte header (its size) and its
 * payload rounded up to 16; a free keeps the block; a realloc of the last block
 * grows or shrinks it in place, and of any other block takes a new one and copies.
 * The decisions depend on the order of the calls and their sizes only, never on an
 * address, so the measure taken over the sink's allocator and the arena over a
 * workspace at any address agree (S12).
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/safemath.h>

#include <string.h>

/** Alignment of every block, and the size of its header. */
#define ARENA_ALIGN 16u

/** @p n rounded up to the alignment; SIZE_MAX when that overflows. */
static size_t round_up(size_t n) {
  size_t r;
  if (!gcu_safe_add_size(n, ARENA_ALIGN - 1u, &r)) {
    return SIZE_MAX;
  }
  return r & ~(size_t)(ARENA_ALIGN - 1u);
}

/** The footprint of a block of @p n bytes, header included; SIZE_MAX on overflow. */
static size_t footprint(size_t n) {
  size_t r = round_up(n);
  size_t total;
  if (r == SIZE_MAX || !gcu_safe_add_size(r, ARENA_ALIGN, &total)) {
    return SIZE_MAX;
  }
  return total;
}

/**
 * Take a block of @p n bytes at the top: the offset of its header, or SIZE_MAX when it does not fit (a measure
 * never runs out).
 */
static size_t take(GSHCPU_Arena * a, size_t at, size_t n) {
  size_t f = footprint(n);
  size_t end;
  if (f == SIZE_MAX || !gcu_safe_add_size(at, f, &end) || (!a->measure && end > a->capacity)) {
    return SIZE_MAX;
  }
  a->used = end;
  if (end > a->peak) {
    a->peak = end;
  }
  return at;
}

static void * payload(GSHCPU_Arena * a, size_t offset) {
  return a->base + offset + ARENA_ALIGN;
}

static size_t block_size(const void * p) {
  size_t n;
  memcpy(&n, (const unsigned char *)p - ARENA_ALIGN, sizeof n);
  return n;
}

static void set_size(void * p, size_t n) {
  memcpy((unsigned char *)p - ARENA_ALIGN, &n, sizeof n);
}

static void * arena_malloc(void * ctx, size_t n) {
  GSHCPU_Arena * a = ctx;
  size_t at = take(a, a->used, n);
  if (at == SIZE_MAX) {
    return NULL;
  }
  void * p;
  if (a->measure) {
    p = a->measure->malloc_fn(a->measure->ctx, n);
    if (!p) {
      a->used = at; /* the block was not taken after all */
      return NULL;
    }
  } else {
    p = payload(a, at);
    set_size(p, n);
  }
  a->last = p;
  a->last_offset = at;
  return p;
}

static void * arena_calloc(void * ctx, size_t count, size_t size) {
  size_t n;
  if (!gcu_safe_mul_size(count, size, &n)) {
    return NULL;
  }
  GSHCPU_Arena * a = ctx;
  if (a->measure) {
    size_t at = take(a, a->used, n);
    if (at == SIZE_MAX) {
      return NULL;
    }
    void * p = a->measure->calloc_fn(a->measure->ctx, count, size);
    if (!p) {
      a->used = at;
      return NULL;
    }
    a->last = p;
    a->last_offset = at;
    return p;
  }
  void * p = arena_malloc(ctx, n);
  if (p) {
    memset(p, 0, n);
  }
  return p;
}

static void * arena_realloc(void * ctx, void * old, size_t n) {
  GSHCPU_Arena * a = ctx;
  if (!old) {
    return arena_malloc(ctx, n);
  }
  if (old == a->last) {
    /* The last block grows or shrinks where it is. */
    size_t keep = a->used;
    if (take(a, a->last_offset, n) == SIZE_MAX) {
      return NULL; /* take changes nothing when it fails */
    }
    if (a->measure) {
      void * p = a->measure->realloc_fn(a->measure->ctx, old, n);
      if (!p) {
        a->used = keep;
        return NULL;
      }
      a->last = p;
      return p;
    }
    set_size(old, n);
    return old;
  }
  /* Another block: a new one, and a copy. */
  size_t at = take(a, a->used, n);
  if (at == SIZE_MAX) {
    return NULL;
  }
  void * p;
  if (a->measure) {
    p = a->measure->realloc_fn(a->measure->ctx, old, n);
    if (!p) {
      a->used = at;
      return NULL;
    }
  } else {
    p = payload(a, at);
    size_t had = block_size(old);
    memcpy(p, old, had < n ? had : n);
    set_size(p, n);
  }
  a->last = p;
  a->last_offset = at;
  return p;
}

static void arena_free(void * ctx, void * p) {
  GSHCPU_Arena * a = ctx;
  if (!p) {
    return;
  }
  if (a->measure) {
    a->measure->free_fn(a->measure->ctx, p);
  }
  if (p == a->last) {
    /* The block stays taken; only the address is forgotten, so that a later block at the same address (a measure's
     * allocator may hand it out again) is not taken for this one. */
    a->last = NULL;
  }
}

static void arena_self(GSHCPU_Arena * a) {
  a->self.ctx = a;
  a->self.malloc_fn = arena_malloc;
  a->self.calloc_fn = arena_calloc;
  a->self.realloc_fn = arena_realloc;
  a->self.free_fn = arena_free;
}

void gshcpu_render_arena_init(GSHCPU_Arena * arena, void * memory, size_t bytes) {
  memset(arena, 0, sizeof *arena);
  arena->base = memory;
  arena->capacity = bytes;
  arena_self(arena);
}

void gshcpu_render_arena_measure(GSHCPU_Arena * arena, const GSHGEOM_Allocator * real) {
  memset(arena, 0, sizeof *arena);
  arena->measure = real;
  arena_self(arena);
}

void gshcpu_render_arena_reset(GSHCPU_Arena * arena) {
  arena->used = 0;
  arena->last = NULL;
  arena->last_offset = 0;
}
