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
 * A table of small values by a 64-bit key, for the layout's count of what each mask picture needs (MC5): a key is found
 * once, however many keys there are.  An open-addressed table that doubles when it is half full.  Its room comes from the
 * allocator it is opened with, or, when that is NULL (a render from the caller's memory allocates nothing, not even to size
 * itself), from the scratch it is opened with, which the sizing may use because nothing else lives in it yet; a table that
 * is out of room stops adding and finds what it holds, so a key past it is only found again by whoever asked (the same
 * answer, more time), never a wrong one.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/safemath.h>

#include <string.h>

/** The scratch's alignment. */
#define MEMO_ALIGN 16u

/** The slot of @p key in a table of @p cap slots (a power of two, at least 2) that starts at @p start. */
static size_t home(uint64_t key, size_t cap) {
  size_t shift = 64u;
  for (size_t c = cap; c > 1u; c >>= 1) {
    shift--;
  }
  return (size_t)((key * UINT64_C(0x9E3779B97F4A7C15)) >> shift);
}

/** Put @p slot in @p table of @p cap slots; the table has a free slot. */
static void put(GSHCPU_Memo_Slot * table, size_t cap, const GSHCPU_Memo_Slot * slot) {
  size_t at = home(slot->key, cap);
  while (table[at].full) {
    at = (at + 1u) & (cap - 1u);
  }
  table[at] = *slot;
}

void gshcpu_memo_open(GSHCPU_Memo * memo, size_t value_bytes, const GSHGEOM_Allocator * alloc, void * scratch, size_t scratch_bytes) {
  memset(memo, 0, sizeof *memo);
  memo->slots = memo->first;
  memo->cap = GSHCPU_MEMO_INLINE;
  memo->value_bytes = value_bytes <= GSHCPU_MEMO_VALUE_MAX ? value_bytes : GSHCPU_MEMO_VALUE_MAX;
  memo->alloc = alloc;
  if (!alloc && scratch) {
    unsigned char * aligned = (unsigned char *)(((uintptr_t)scratch + (MEMO_ALIGN - 1u)) & ~(uintptr_t)(MEMO_ALIGN - 1u));
    size_t lost = (size_t)(aligned - (unsigned char *)scratch);
    if (scratch_bytes >= lost) {
      memo->scratch = aligned;
      memo->scratch_left = scratch_bytes - lost;
    }
  }
}

int gshcpu_memo_find(const GSHCPU_Memo * memo, uint64_t key, void * value) {
  size_t at = home(key, memo->cap);
  while (memo->slots[at].full) {
    if (memo->slots[at].key == key) {
      memcpy(value, memo->slots[at].value, memo->value_bytes);
      return 1;
    }
    at = (at + 1u) & (memo->cap - 1u);
  }
  return 0;
}

/** A table of twice the slots, with every entry moved to it; 0 when there is no room for it. */
static int grow(GSHCPU_Memo * memo) {
  size_t bytes;
  if (memo->cap > SIZE_MAX / 2u / sizeof(GSHCPU_Memo_Slot) || !gcu_safe_mul_size(memo->cap * 2u, sizeof(GSHCPU_Memo_Slot), &bytes)) {
    return 0;
  }
  GSHCPU_Memo_Slot * table = NULL;
  if (memo->alloc) {
    table = memo->alloc->calloc_fn(memo->alloc->ctx, 1u, bytes);
  } else if (memo->scratch_left >= bytes) {
    table = (GSHCPU_Memo_Slot *)(void *)memo->scratch;
    memset(table, 0, bytes);
    memo->scratch += bytes;
    memo->scratch_left -= bytes;
  }
  if (!table) {
    return 0;
  }
  for (size_t i = 0; i < memo->cap; i++) {
    if (memo->slots[i].full) {
      put(table, memo->cap * 2u, &memo->slots[i]);
    }
  }
  if (memo->heap) {
    memo->alloc->free_fn(memo->alloc->ctx, memo->slots);
  }
  memo->heap = memo->alloc != NULL;
  memo->slots = table;
  memo->cap *= 2u;
  return 1;
}

void gshcpu_memo_add(GSHCPU_Memo * memo, uint64_t key, const void * value) {
  if (2u * (memo->used + 1u) > memo->cap && !grow(memo) && memo->used + 2u > memo->cap) {
    return; /* no room to grow, and one free slot is kept so that a search ends */
  }
  GSHCPU_Memo_Slot slot;
  memset(&slot, 0, sizeof slot);
  slot.key = key;
  slot.full = 1u;
  memcpy(slot.value, value, memo->value_bytes);
  put(memo->slots, memo->cap, &slot);
  memo->used++;
}

void gshcpu_memo_close(GSHCPU_Memo * memo) {
  if (memo->heap) {
    memo->alloc->free_fn(memo->alloc->ctx, memo->slots);
  }
  memo->heap = 0;
  memo->slots = memo->first;
  memo->cap = GSHCPU_MEMO_INLINE;
  memo->used = 0;
  memset(memo->first, 0, sizeof memo->first);
}
