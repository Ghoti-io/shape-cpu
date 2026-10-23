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
 * The CPU sink (render.h): its operations for shape-scene's contract layer, and
 * the render, which goes through the list's expansion one unit at a time,
 * compositing into premultiplied `double` layers, and writes the root layer to
 * the target once at the end (S11).  The planning pass is the contract layer's;
 * this file decides each unit with the same predicate, skips what was refused,
 * and adds only what it finds while drawing (its resource limits).
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/safemath.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/stroke.h>

#include <string.h>

/** What every carved piece is aligned to. */
#define CARVE_ALIGN 16u

/* --- the sink object ------------------------------------------------------------------------ */

typedef struct Cpu_Sink {
  GSHGEOM_Allocator alloc;
  size_t max_dimension;      /* the effective limits */
  size_t max_bytes;
  uint64_t max_cells;
  uint64_t max_exact;        /* the most pieces of a stroke part in one pixel (UC9) */
  GSHGEOM_Limits geometry;   /* max_points for the flattener */
  GSHCPU_Render_Observer observer;
  void * observer_ctx;
  GSHCPU_Stroke_Cache * cache;      /* SC7: the expansions kept across renders */
  GSHCPU_Render_Counters own;       /* the counters, unless a test gave a place for them */
  GSHCPU_Render_Counters * counters;
  unsigned tiers;                   /* GSHCPU_RENDER_TIER_*: the tiers that are on */
  size_t elements;                  /* the most path elements a stored resource may hold: SC11's term */
} Cpu_Sink;

/* --- what a render remembers ----------------------------------------------------------------- */

/**
 * A layer: the root's or a group's (N6).  Its memory is made ready only inside `dirty`, as a unit reaches it (widen):
 * outside, a group's layer is zero, and the root's values the target's with its touched map clear, without any of it
 * being written, so a render pays for the pixels its units reach and not for the region.
 */
typedef struct Frame {
  double * layer;            /* four doubles a pixel of the region */
  GSHSCENE_Color_Space space;
  GSHSCENE_Group group;      /* a group's properties */
  size_t clip_base;          /* the first clip pushed in this group */
  GSHCPU_Rect dirty;         /* the pixels anything was applied to, and the pixels made ready */
} Frame;

/** A clip in force: its coverage inside its rectangle (0 outside it). */
typedef struct Clip {
  GSHCPU_Rect rect;
  GSHCPU_Rect whole;         /* its rectangle in the whole render (the extent, not the damage): where cells are met */
  uint8_t * coverage;        /* rect's width by height */
} Clip;

/** A gradient resolved in this render (S17: once per gradient per render), by resource id. */
typedef struct Resolved {
  uint64_t id;               /* the resource's id */
  int used;
  size_t first;              /* its stops in the render's pool */
  GSHSCENE_Color_Space space; /* its interpolation space, resolved */
} Resolved;

/** What a refusal skips (S6): a refused push its block, a refused instance its picture. */
typedef struct Skip {
  size_t level;              /* the chain length of the refused item; 0 when nothing is skipped */
  size_t depth;              /* pushes open inside the block, at its level */
  int instance;
} Skip;

/** The sizes and places of a render's memory (render.h, Memory). */
typedef struct Layout {
  GSHCPU_Rect region;
  GSHCPU_Rect whole;         /* the region of the whole render: the extent, whatever the damage (S4) */
  size_t width;
  size_t height;
  size_t pixels;
  size_t levels;             /* the list and each picture under it */
  size_t groups;             /* the most groups open at once */
  size_t clips;              /* the most clips open at once */
  size_t walk;               /* bytes of the expansion walk */
  size_t gradients;          /* the gradient fills of the expansion, a picture's counted where it is drawn */
  size_t stops;              /* their stops */
  size_t table;              /* the resolved table's slots: a power of two at least twice the gradient fills */
  int aligned;               /* the expansion holds a stroke that aligns: three more coverage arrays (SC5) */
  int strokes;               /* the expansion holds a stroke: the exact step's memory (UC9) */
  size_t exact_bytes;        /* ... of the first part's, and (only when something aligns) of the second's and the third's */
  size_t exact_second, exact_third; /* ... where, from at_exact */
  size_t acc_bytes;          /* the cell accumulator: one for each of the three parts when something aligns (UC8) */
  size_t at_walk, at_maps, at_stop, at_frames, at_clips, at_root, at_layers, at_touched, at_coverage, at_clip_memory,
      at_acc, at_exact, at_table, at_pool, at_items; /* offsets from the aligned base */
  size_t at_cov_aligned, at_cov_clip, at_cov_result; /* the aligned stroke's arrays */
  size_t fixed;              /* the bytes before the items' arena, the alignment slack included */
} Layout;

typedef struct Run {
  Cpu_Sink * cs;
  GSHSCENE_Job * job;
  const GSHSCENE_Render_Request * request;
  GSHCPU_Format format;
  GIMG_Raster * raster;
  unsigned char * pixels;    /* the target's, and its stride */
  size_t stride;
  Layout layout;
  GSHSCENE_Expansion walk;
  GSHGEOM_Map * maps;        /* the enclosing instances' maps, and room for the item's and a glyph's */
  size_t * stop;             /* the chain a strict refusal stopped at */
  size_t stop_count;
  Frame * frames;
  size_t top;                /* the innermost frame */
  Clip * clips;
  size_t clip_count;
  double * root;
  double * layers;
  uint8_t * touched;
  uint8_t * coverage;
  uint8_t * clip_memory;
  uint8_t * cov_aligned;     /* an aligned stroke's parts and their composition (SC5), only when the layout has them */
  uint8_t * cov_clip;
  uint8_t * cov_result;
  int64_t * acc;
  GSHCPU_Exact exact[3];     /* UC9: the exact step's arrays, when the layout has them (strokes): one for each part of an aligned stroke */
  int acc_dirty;             /* the dense sweep leaves the accumulator written; the sparse one needs it zero */
  int caller;                /* CALLER_MEMORY: nothing allocated, so no cache */
  Resolved * resolved;       /* the gradients resolved so far (S17), open addressing on the id */
  GSHCPU_Stop * pool;        /* their stops */
  size_t pooled;
  GSHCPU_Arena arena;        /* CALLER_MEMORY: the items' geometry */
  const GSHGEOM_Allocator * item_alloc;
  GSHSCENE_Color foreground;
  const GSHSCENE_Resource * palette; /* the list's (N14); the request keeps it alive (S10) */
  Skip skip;
  size_t processed;
  size_t done;
  size_t groups_open;
  uint64_t work;
} Run;

/* --- sizes ------------------------------------------------------------------------------------ */

/** @p n rounded up to the carve alignment; SIZE_MAX on overflow. */
static size_t align_up(size_t n) {
  size_t r;
  if (n == SIZE_MAX || !gcu_safe_add_size(n, CARVE_ALIGN - 1u, &r)) {
    return SIZE_MAX;
  }
  return r & ~(size_t)(CARVE_ALIGN - 1u);
}

/** Place @p bytes at @p *at; 0 on overflow. */
static int place(size_t * at, size_t bytes, size_t * out) {
  size_t a = align_up(bytes);
  *out = *at;
  return a != SIZE_MAX && gcu_safe_add_size(*at, a, at);
}

/** @p a times @p b times @p c; SIZE_MAX on overflow. */
static size_t mul3(size_t a, size_t b, size_t c) {
  size_t r;
  if (!gcu_safe_mul_size(a, b, &r) || !gcu_safe_mul_size(r, c, &r)) {
    return SIZE_MAX;
  }
  return r;
}

/**
 * The most groups and clips open at once in @p list's expansion, a picture's counting where it is drawn; and its
 * gradient fills and their stops added to @p gradients and @p stops (0 when a sum would overflow).
 */
static int nesting(const GSHSCENE_List * list, size_t * groups, size_t * clips, size_t * gradients, size_t * stops,
    int * aligned, int * strokes) {
  size_t g = 0;
  size_t c = 0;
  size_t most_g = 0;
  size_t most_c = 0;
  int ok = 1;
  size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      break;
    }
    GSHSCENE_Gradient gradient;
    if ((item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
        item->paint.kind == GSHSCENE_PAINT_GRADIENT &&
        gshscene_resource_gradient(item->paint.gradient, &gradient) == GSHSCENE_OK) {
      ok = ok && gcu_safe_add_size(*gradients, 1u, gradients) && gcu_safe_add_size(*stops, gradient.stop_count, stops);
    }
    GSHSCENE_Stroke_Style style;
    if (item->kind == GSHSCENE_ITEM_STROKE) {
      *strokes = 1; /* UC9: the exact step's counts and pool */
    }
    if (item->kind == GSHSCENE_ITEM_STROKE && gshscene_resource_stroke_style(item->style, &style) == GSHSCENE_OK &&
        style.alignment != (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER) {
      *aligned = 1; /* SC5: the parts and their composition need arrays of their own */
    }
    switch (item->kind) {
      case GSHSCENE_ITEM_GROUP_PUSH:
        g++;
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        g--;
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        c++;
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        c--;
        break;
      case GSHSCENE_ITEM_INSTANCE: {
        size_t ig = 0;
        size_t ic = 0;
        ok = nesting(gshscene_resource_picture(item->resource), &ig, &ic, gradients, stops, aligned, strokes) && ok;
        most_g = g + ig > most_g ? g + ig : most_g;
        most_c = c + ic > most_c ? c + ic : most_c;
        break;
      }
      default:
        break;
    }
    most_g = g > most_g ? g : most_g;
    most_c = c > most_c ? c : most_c;
  }
  *groups = most_g;
  *clips = most_c;
  return ok;
}

/** The region a render draws: the extent, meet the damage. */
static GSHCPU_Rect region_of(const GSHSCENE_Target * target) {
  GSHCPU_Rect r = target->extent;
  if (target->damage) {
    r = gshcpu_render_rect_meet(r, *target->damage);
  }
  if (gshcpu_render_rect_empty(r)) {
    r.x1 = r.x0;
    r.y1 = r.y0;
  }
  return r;
}

/** The region the whole render draws: the extent (S4: the damage changes what is drawn, never what is refused). */
static GSHCPU_Rect whole_of(const GSHSCENE_Target * target) {
  GSHSCENE_Target whole = *target;
  whole.damage = NULL;
  return region_of(&whole);
}

/** Lay out a render of @p list on @p target; ::GSHCPU_ERR_LIMIT past a limit or when a size overflows. */
static GSHCPU_Result layout(const Cpu_Sink * cs, const GSHSCENE_List * list, const GSHSCENE_Target * target, Layout * L) {
  memset(L, 0, sizeof *L);
  L->region = region_of(target);
  L->whole = whole_of(target);
  L->width = (size_t)(L->region.x1 - L->region.x0);
  L->height = (size_t)(L->region.y1 - L->region.y0);
  if (L->region.x1 > (int64_t)GSHCPU_MAX_DIMENSION_CEILING || L->region.y1 > (int64_t)GSHCPU_MAX_DIMENSION_CEILING ||
      L->width > cs->max_dimension || L->height > cs->max_dimension) {
    return GSHCPU_ERR_LIMIT;
  }
  L->pixels = L->width * L->height; /* each at most 2^24: no overflow */
  if (L->pixels > cs->max_bytes) {
    return GSHCPU_ERR_LIMIT;
  }
  L->walk = gshscene_expansion_bytes(list);
  L->levels = L->walk / (sizeof(const GSHSCENE_List *) + sizeof(size_t));
  if (!nesting(list, &L->groups, &L->clips, &L->gradients, &L->stops, &L->aligned, &L->strokes)) {
    return GSHCPU_ERR_LIMIT;
  }
  /* UC9: the walk of a stroke is as wide as the whole render's region at most. */
  {
    size_t width = (size_t)(L->whole.x1 - L->whole.x0);
    size_t first = L->strokes ? gshcpu_exact_bytes(width, cs->max_exact, L->aligned ? 3 : 1) : 0;
    size_t other = L->strokes && L->aligned ? gshcpu_exact_bytes(width, cs->max_exact, 0) : 0;
    size_t at_second = align_up(first);
    size_t at_third = align_up(other);
    L->exact_second = 0;
    L->exact_third = 0;
    L->exact_bytes = SIZE_MAX;
    if (first != SIZE_MAX && other != SIZE_MAX && at_second != SIZE_MAX && at_third != SIZE_MAX &&
        gcu_safe_add_size(at_second, at_third, &L->exact_third) && gcu_safe_add_size(L->exact_third, other, &L->exact_bytes)) {
      L->exact_second = at_second;
      L->exact_third = at_second + at_third;
    }
    L->acc_bytes = mul3(2u * (L->width + 1u), GSHCPU_RENDER_BAND_ROWS, sizeof(int64_t));
    if (L->aligned && L->acc_bytes != SIZE_MAX) {
      L->acc_bytes = mul3(L->acc_bytes, 3u, 1u);
    }
  }
  L->table = 0;
  if (L->gradients) {
    L->table = 1;
    while (L->table < L->gradients || L->table - L->gradients < L->gradients) {
      if (L->table > SIZE_MAX / 4u) {
        return GSHCPU_ERR_LIMIT;
      }
      L->table *= 2u; /* at least twice the gradient fills: a probe ends at an empty slot soon */
    }
  }
  size_t at = 0;
  size_t ignored;
  int ok = place(&at, sizeof(Run), &ignored) && place(&at, L->walk, &L->at_walk) &&
      place(&at, mul3(L->levels + 2u, sizeof(GSHGEOM_Map), 1), &L->at_maps) &&
      place(&at, mul3(L->levels, sizeof(size_t), 1), &L->at_stop) &&
      place(&at, mul3(L->groups + 1u, sizeof(Frame), 1), &L->at_frames) &&
      place(&at, mul3(L->clips, sizeof(Clip), 1), &L->at_clips) &&
      place(&at, mul3(L->pixels, 4u, sizeof(double)), &L->at_root) &&
      place(&at, mul3(L->groups, L->pixels, 4u * sizeof(double)), &L->at_layers) &&
      place(&at, L->pixels, &L->at_touched) && place(&at, L->pixels, &L->at_coverage) &&
      place(&at, L->aligned ? L->pixels : 0, &L->at_cov_aligned) && place(&at, L->aligned ? L->pixels : 0, &L->at_cov_clip) &&
      place(&at, L->aligned ? L->pixels : 0, &L->at_cov_result) &&
      place(&at, mul3(L->clips, L->pixels, 1), &L->at_clip_memory) &&
      place(&at, L->acc_bytes, &L->at_acc) &&
      place(&at, L->exact_bytes, &L->at_exact) &&
      place(&at, mul3(L->table, sizeof(Resolved), 1), &L->at_table) &&
      place(&at, mul3(L->stops, sizeof(GSHCPU_Stop), 1), &L->at_pool);
  L->at_items = at;
  if (!ok || !gcu_safe_add_size(at, CARVE_ALIGN - 1u, &L->fixed)) {
    return GSHCPU_ERR_LIMIT;
  }
  return GSHCPU_OK;
}

/* --- skipping (S6) ---------------------------------------------------------------------------- */

static void skip_begin(Skip * s, const GSHSCENE_Item * item, size_t count) {
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH ||
      item->kind == GSHSCENE_ITEM_INSTANCE) {
    s->level = count;
    s->depth = 0;
    s->instance = item->kind == GSHSCENE_ITEM_INSTANCE;
  }
}

/** Whether the unit is inside what is skipped (and step past it). */
static int skip_step(Skip * s, const GSHSCENE_Item * item, size_t count) {
  if (!s->level) {
    return 0;
  }
  if (count > s->level) {
    return 1;
  }
  if (s->instance || count < s->level) {
    s->level = 0;
    return 0;
  }
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
    s->depth++;
  } else if (item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) {
    if (s->depth == 0) {
      s->level = 0; /* the matching pop: skipped, and the block is over */
    } else {
      s->depth--;
    }
  }
  return 1;
}

/* --- the geometry of the items, measured (S9) --------------------------------------------------- */

/**
 * Convert every fill and clip push of the expansion of @p list once, through @p arena (reset for each), and give
 * the most one took.  @p memory holds the walk and the maps (at the layout's offsets from it).
 */
static GSHCPU_Result measure_items(const Cpu_Sink * cs, const GSHSCENE_List * list, const GSHSCENE_Target * target,
    const Layout * L, unsigned char * memory, GSHCPU_Arena * arena, size_t * peak) {
  GSHSCENE_Expansion walk;
  GSHGEOM_Map * maps = (GSHGEOM_Map *)(void *)(memory + L->at_maps);
  if (gshscene_expansion_init(&walk, list, memory + L->at_walk, L->walk) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL;
  }
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  *peak = 0;
  while (gshscene_expansion_next(&walk, &item, &chain, &count)) {
    (void)chain;
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      maps[count - 1] = item->map;
      continue;
    }
    if (item->kind == GSHSCENE_ITEM_STROKE) {
      /* A stroke that is drawn is expanded once through the arena (no cache under CALLER_MEMORY: SC7); its expansion
       * is what an item's geometry is for a fill, so the caller's memory holds the most any item takes. */
      if (gshscene_stroke_notice(item, maps, count - 1, &target->map)) {
        continue; /* a stroke that draws nothing */
      }
      GSHGEOM_Map composed;
      maps[count - 1] = item->map;
      if (gshgeom_map_compose_chain(maps, count, &target->map, &composed) != GSHGEOM_OK) {
        continue;
      }
      GSHCPU_Stroke st;
      gshcpu_render_arena_reset(arena);
      arena->peak = 0;
      /* A run's centerline, its glyphs' outlines and its notes are allocated here in the order the drawing takes them
       * (ST8), so the memory asked for is exact. */
      GSHCPU_Result sr = gshcpu_render_stroke_open(NULL, &arena->self, &cs->geometry, item, &composed, &st);
      if (sr == GSHCPU_OK) {
        gshcpu_render_stroke_close(NULL, &st);
      }
      if (sr == GSHCPU_ERR_OOM) {
        return sr;
      }
      *peak = arena->peak > *peak ? arena->peak : *peak;
      continue;
    }
    if (item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_CLIP_PUSH) {
      continue;
    }
    if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_PLANE) {
      continue; /* a plane has no geometry: it takes nothing from the arena */
    }
    GSHCPU_Item_Geometry g;
    gshcpu_render_arena_reset(arena);
    arena->peak = 0;
    GSHCPU_Result r = gshcpu_render_geometry(item, maps, count - 1, &target->map, &arena->self, &cs->geometry, &g);
    gshcpu_render_geometry_release(&g, &arena->self);
    if (r == GSHCPU_ERR_OOM) {
      return r;
    }
    *peak = arena->peak > *peak ? arena->peak : *peak;
  }
  return GSHCPU_OK;
}

/**
 * The workspace (S9): the fixed block and the most any item's geometry takes.  A render's own sizing
 * (`CALLER_MEMORY` in the flags) allocates nothing, not even to size itself: it measures inside the caller's
 * workspace, and one too small for the block or for an item gives only a need past it (the contract layer then
 * refuses the render with ::GSHSCENE_ERR_LIMIT; the query asks again without the flag).  Without the flag the need is
 * measured over the sink's allocator, exactly.
 */
static GSHCPU_Result need_of(const Cpu_Sink * cs, const GSHSCENE_Render_Request * request, size_t * out) {
  Layout L;
  GSHCPU_Result r = layout(cs, request->list, request->target, &L);
  if (r != GSHCPU_OK) {
    return r;
  }
  size_t peak = 0;
  GSHCPU_Arena arena;
  if (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    if (!request->workspace || request->workspace_bytes < L.fixed) {
      *out = L.fixed; /* no room for the walk: the block alone is past the workspace */
      return GSHCPU_OK;
    }
    unsigned char * base = (unsigned char *)(((uintptr_t)request->workspace + (CARVE_ALIGN - 1u)) &
        ~(uintptr_t)(CARVE_ALIGN - 1u));
    gshcpu_render_arena_init(&arena, base + L.at_items, request->workspace_bytes - L.fixed);
    r = measure_items(cs, request->list, request->target, &L, base, &arena, &peak);
    if (r == GSHCPU_OK) {
      return gcu_safe_add_size(L.fixed, peak, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;
    }
    if (r != GSHCPU_ERR_OOM) {
      return r;
    }
    /* An item does not fit in what is left: the need is past the workspace, and the query gives it exactly. */
    return gcu_safe_add_size(request->workspace_bytes, 1u, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;
  }
  size_t scratch_bytes = L.at_stop; /* the walk and the maps, from the aligned base (the Run's room unused) */
  unsigned char * scratch = cs->alloc.malloc_fn(cs->alloc.ctx, scratch_bytes + CARVE_ALIGN);
  if (!scratch) {
    return GSHCPU_ERR_OOM;
  }
  unsigned char * base = (unsigned char *)(((uintptr_t)scratch + (CARVE_ALIGN - 1u)) & ~(uintptr_t)(CARVE_ALIGN - 1u));
  gshcpu_render_arena_measure(&arena, &cs->alloc);
  r = measure_items(cs, request->list, request->target, &L, base, &arena, &peak);
  cs->alloc.free_fn(cs->alloc.ctx, scratch);
  if (r != GSHCPU_OK) {
    return r;
  }
  return gcu_safe_add_size(L.fixed, peak, out) ? GSHCPU_OK : GSHCPU_ERR_LIMIT;
}

/* --- the operations ----------------------------------------------------------------------------- */

static GSHSCENE_Result scene_result(GSHCPU_Result r) {
  switch (r) {
    case GSHCPU_OK:
      return GSHSCENE_OK;
    case GSHCPU_ERR_LIMIT:
      return GSHSCENE_ERR_LIMIT;
    case GSHCPU_ERR_OOM:
      return GSHSCENE_ERR_OOM;
    case GSHCPU_ERR_REFUSED:
      return GSHSCENE_ERR_REFUSED;
    case GSHCPU_ERR_CANCELLED:
      return GSHSCENE_ERR_CANCELLED;
    case GSHCPU_ERR_INVALID:
      return GSHSCENE_ERR_INVALID;
    default:
      return GSHSCENE_ERR_INTERNAL;
  }
}

/** The bound of the work between two checks (S7): render.h. */
static uint64_t work_bound(const Cpu_Sink * cs) {
  /* The worst unit is an aligned stroke (SC11): the points of its three parts, the deposits of all of them (the one
   * cap), two passes over the pixels (the composition and the paint), and the expansion's own steps, bounded by the
   * depth cap for every element of the centerline and, for a run, one step more for each element of the glyph
   * outlines that went into it (expansion_work). */
  uint64_t b;
  uint64_t pixels;
  uint64_t expansion;
  if (!gcu_safe_mul_u64((uint64_t)cs->geometry.max_points, 3u, &b) || !gcu_safe_mul_u64((uint64_t)cs->max_bytes, 2u, &pixels) ||
      !gcu_safe_mul_u64((uint64_t)cs->elements, (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + 1u, &expansion) ||
      !gcu_safe_add_u64(b, cs->max_cells, &b) || !gcu_safe_add_u64(b, pixels, &b) || !gcu_safe_add_u64(b, expansion, &b)) {
    return UINT64_MAX;
  }
  /* UC9: and the exact step's, over the pieces of the item's three parts (its deposits, which the cap counts, and the
   * horizontal pieces on cells' left sides, at most one for each point and each column of the widest raster), a cell of
   * an aligned stroke holding the pieces of all three. */
  uint64_t points3;
  uint64_t cell3;
  if (!gcu_safe_mul_u64((uint64_t)cs->geometry.max_points, 3u, &points3) || !gcu_safe_mul_u64(cs->max_exact, 3u, &cell3)) {
    return UINT64_MAX;
  }
  uint64_t exact = gshcpu_exact_work_bound((uint64_t)cs->max_cells, points3, (uint64_t)cs->max_dimension, cell3);
  if (!gcu_safe_add_u64(b, exact, &b)) {
    return UINT64_MAX;
  }
  return b;
}

static GSHSCENE_Result op_caps(void * impl, const GSHSCENE_Target * target, GSHSCENE_Sink_Caps * out) {
  Cpu_Sink * cs = impl;
  if (target->kind != GSHSCENE_TARGET_RASTER || !gshcpu_format_of(target->surface)) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  /* Exactly the item kinds unit() draws, named rather than counted up to the enum's end: a shape-scene whose enum has
   * grown past them must find that kind unclaimed, so that the planning pass refuses it (ITEM_KIND) instead of the
   * sink meeting an item it has no drawing for. */
  out->item_kinds = (UINT32_C(1) << GSHSCENE_ITEM_FILL) | (UINT32_C(1) << GSHSCENE_ITEM_CLIP_PUSH) |
      (UINT32_C(1) << GSHSCENE_ITEM_CLIP_POP) | (UINT32_C(1) << GSHSCENE_ITEM_GROUP_PUSH) |
      (UINT32_C(1) << GSHSCENE_ITEM_GROUP_POP) | (UINT32_C(1) << GSHSCENE_ITEM_INSTANCE) |
      (UINT32_C(1) << GSHSCENE_ITEM_STROKE);
  /* Exactly the operators gshcpu_render_apply composites, named rather than counted up to the enum's end: a
   * shape-scene whose enum has grown past them must not find them claimed here (they would be refused by nothing and
   * composited as no operator at all).  The Porter-Duff operators and PLUS, then the blend modes (B1, story P3). */
  for (uint32_t c = GSHSCENE_COMPOSITE_SRC_OVER; c <= GSHSCENE_COMPOSITE_PLUS; c++) {
    out->composites |= UINT32_C(1) << c;
  }
  for (uint32_t c = GSHSCENE_COMPOSITE_SCREEN; c <= GSHSCENE_COMPOSITE_LUMINOSITY; c++) {
    out->composites |= UINT32_C(1) << c;
  }
  out->flags = GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_CALLER_MEMORY | GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES |
      GSHSCENE_CAP_READBACK | GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_GRADIENT_SPACES;
  /* SC1: a stroke is never drawn as a stroke (stroke_native is 0) but by expansion, for every style, so no stroke is
   * refused for its style. */
  out->flags |= GSHSCENE_CAP_STROKE_EXPAND;
  /* ST8: a stroke whose centerline is a run is drawn too: the run's centerline is one path (ST1), expanded and united as
   * any path's is. */
  out->flags |= GSHSCENE_CAP_STROKE_RUNS;
  /* S17: every kind, in its stated interpolation space (story P2). */
  for (uint32_t k = GSHSCENE_GRADIENT_LINEAR; k < GSHSCENE_GRADIENT_KIND_COUNT; k++) {
    out->gradient_kinds |= UINT32_C(1) << k;
  }
  out->cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  out->cancel_work_bound = work_bound(cs);
  out->work_measure = "cells+points+pixels";
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_workspace_bytes(void * impl, const GSHSCENE_Render_Request * request, size_t * out) {
  return scene_result(need_of(impl, request, out));
}

/* --- one unit ----------------------------------------------------------------------------------- */

/** Refused while drawing (S6): the entry; under strict the render stops here, else the item's block is skipped. */
static GSHSCENE_Result refuse(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  GSHSCENE_Result r = gshscene_job_report(run->job, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT, chain, count);
  if (r != GSHSCENE_OK) {
    return r;
  }
  if (run->request->flags & GSHSCENE_RENDER_STRICT) {
    memcpy(run->stop, chain, count * sizeof *chain);
    run->stop_count = count;
    return GSHSCENE_ERR_REFUSED;
  }
  skip_begin(&run->skip, item, count);
  return GSHSCENE_OK;
}

/** The pixel (x, y) of a region-sized buffer. */
static size_t at(const Run * run, int64_t x, int64_t y) {
  return (size_t)(y - run->layout.region.y0) * run->layout.width + (size_t)(x - run->layout.region.x0);
}

/**
 * Make the pixels of row @p y from @p x0 to @p x1 of @p f ready (widen): a group's layer starts at `(0, 0, 0, 0)`;
 * the root layer is the target (N6), its values read once, here, and its touched map clear.
 */
static void ready_span(Run * run, Frame * f, int64_t x0, int64_t x1, int64_t y) {
  if (x0 >= x1) {
    return;
  }
  size_t p = at(run, x0, y);
  size_t n = (size_t)(x1 - x0);
  if (f != &run->frames[0]) {
    memset(f->layer + 4u * p, 0, 4u * n * sizeof *f->layer);
    return;
  }
  memset(run->touched + p, 0, n);
  const unsigned char * row = run->pixels + (size_t)y * run->stride;
  for (int64_t x = x0; x < x1; x++, p++) {
    gshcpu_render_load(run->format, row, (size_t)x, &run->root[4u * p]);
  }
}

/**
 * Widen @p f's dirty rectangle to take in @p r (inside the region), first making ready the pixels it gains
 * (ready_span).  Nothing reads a frame's memory outside its dirty rectangle: a group's pop reads zero there, and the
 * write visits the root's dirty rectangle only.  So the values drawn are the ones every layer made ready over the
 * whole region at the start would give (story 13b), while an empty list touches none of the region and a small item
 * its own pixels.
 */
static void widen(Run * run, Frame * f, GSHCPU_Rect r) {
  GSHCPU_Rect old = f->dirty;
  GSHCPU_Rect grown = gshcpu_render_rect_join(old, r);
  if (gshcpu_render_rect_empty(r) ||
      (grown.x0 == old.x0 && grown.y0 == old.y0 && grown.x1 == old.x1 && grown.y1 == old.y1)) {
    return;
  }
  if (gshcpu_render_rect_empty(old)) {
    old.x0 = old.x1 = grown.x0; /* nothing is ready: every row of grown is below this one */
    old.y0 = old.y1 = grown.y0;
  }
  for (int64_t y = grown.y0; y < old.y0; y++) {
    ready_span(run, f, grown.x0, grown.x1, y);
  }
  if (grown.x0 < old.x0 || old.x1 < grown.x1) {
    for (int64_t y = old.y0; y < old.y1; y++) {
      ready_span(run, f, grown.x0, old.x0, y);
      ready_span(run, f, old.x1, grown.x1, y);
    }
  }
  for (int64_t y = old.y1; y < grown.y1; y++) {
    ready_span(run, f, grown.x0, grown.x1, y);
  }
  f->dirty = grown;
}

/** Whether (x, y) lies in @p r. */
static int inside(GSHCPU_Rect r, int64_t x, int64_t y) {
  return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

/** The clip's coverage at (x, y), which lies in its rectangle. */
static uint32_t clip_at(const Clip * clip, int64_t x, int64_t y) {
  size_t w = (size_t)(clip->rect.x1 - clip->rect.x0);
  return clip->coverage[(size_t)(y - clip->rect.y0) * w + (size_t)(x - clip->rect.x0)];
}

/**
 * The geometry and the coverage of a fill or clip push, into @p out over @p rect (the item's pixels meet what it
 * can reach), and in @p whole_rect the rectangle the whole render draws of it (its pixels meet @p whole, what it can
 * reach there).  The cell cap is met over that one, so that a damage-limited render refuses the items the whole
 * render refuses (S4); a bound from its edges skips the count where the cap cannot be met.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_LIMIT for a resource limit (the caller refuses the item); OOM.
 */
static GSHSCENE_Result shape(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    GSHCPU_Rect reach, GSHCPU_Rect whole, uint8_t * out, GSHCPU_Rect * rect, GSHCPU_Rect * whole_rect) {
  GSHCPU_Item_Geometry g;
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_reset(&run->arena);
  }
  GSHCPU_Result r = gshcpu_render_geometry(item, run->maps, count - 1, &run->job->target.map, run->item_alloc,
      &run->cs->geometry, &g);
  run->work += g.points;
  /* A run from a font: each glyph's entry (glyph.h, D7); a glyph refused under strict stops the render here (S6). */
  for (size_t i = 0; r == GSHCPU_OK && g.notes && i < g.count; i++) {
    if (!g.notes[i].entry_class) {
      continue;
    }
    GSHSCENE_Result e = gshscene_job_report_glyph(run->job, (GSHSCENE_Entry_Class)g.notes[i].entry_class,
        (GSHSCENE_Reason)g.notes[i].reason, chain, count, i);
    if (e == GSHSCENE_OK && g.notes[i].entry_class == GSHSCENE_ENTRY_REFUSED &&
        (run->request->flags & GSHSCENE_RENDER_STRICT)) {
      memcpy(run->stop, chain, count * sizeof *chain);
      run->stop_count = count;
      e = GSHSCENE_ERR_REFUSED;
    }
    if (e != GSHSCENE_OK) {
      gshcpu_render_geometry_release(&g, run->item_alloc);
      return e;
    }
  }
  if (r == GSHCPU_OK) {
    const GSHGEOM_Flat_Path * const * paths = (const GSHGEOM_Flat_Path * const *)g.paths;
    *rect = gshcpu_render_rect_meet(g.box, reach);
    if (gshcpu_render_rect_empty(*rect)) {
      rect->x1 = rect->x0;
      rect->y1 = rect->y0;
    }
    GSHCPU_Rect walk = gshcpu_render_rect_meet(g.box, whole);
    *whole_rect = walk;
    if (!gshcpu_render_rect_empty(walk)) {
      uint64_t cells = 0;
      /* Drawn in part (a damage): unless the bound says the cap cannot be met over the whole, count only where it
       * is drawn; else count the whole while drawing the part. */
      if ((rect->x0 != walk.x0 || rect->y0 != walk.y0 || rect->x1 != walk.x1 || rect->y1 != walk.y1) &&
          gshcpu_render_cells_bound(paths, g.count, walk, run->cs->max_cells) <= run->cs->max_cells) {
        walk = *rect;
      }
      r = gshcpu_render_cover_counted(paths, g.count, item->fill_rule, *rect, walk, out, run->acc, run->cs->max_cells,
          &cells);
      run->acc_dirty = 1; /* the dense sweep writes the accumulator band by band and leaves it so */
      run->work += cells;
    }
  }
  gshcpu_render_geometry_release(&g, run->item_alloc);
  return scene_result(r);
}

/** The pixels the clips in force in the innermost group allow (N9): the region meet their rectangles. */
static GSHCPU_Rect reach_of(const Run * run, size_t from, size_t to) {
  GSHCPU_Rect r = run->layout.region;
  for (size_t i = from; i < to; i++) {
    r = gshcpu_render_rect_meet(r, run->clips[i].rect);
  }
  return r;
}

/** reach_of in the whole render (S4): the extent meet the clips' rectangles there. */
static GSHCPU_Rect whole_reach_of(const Run * run, size_t from, size_t to) {
  GSHCPU_Rect r = run->layout.whole;
  for (size_t i = from; i < to; i++) {
    r = gshcpu_render_rect_meet(r, run->clips[i].whole);
  }
  return r;
}

/** D8: the weight of @p m8 under the clips @p from to @p to at (x, y). */
static uint32_t weigh_clips(const Run * run, size_t from, size_t to, uint32_t m8, int64_t x, int64_t y) {
  for (size_t i = from; i < to && m8 != 0; i++) {
    m8 = gshcpu_render_weigh(m8, clip_at(&run->clips[i], x, y));
  }
  return m8;
}

/**
 * The fill's source in the layer's space, premultiplied (N7, N10, N14): converted once.  The color is shape-scene's one
 * resolution of the paint (a missing palette entry was refused by the plan and skipped by unit()).
 */
static void source_of(const Run * run, const GSHSCENE_Item * item, const Frame * frame, double s[4]) {
  GSHSCENE_Color color;
  if (gshscene_paint_resolve(&item->paint, &run->foreground, run->palette, &color) != GSHSCENE_OK) {
    memset(&color, 0, sizeof color); /* unreachable: the disposition refused it; transparent draws nothing */
  }
  double rgb[3] = {color.rgba[0], color.rgba[1], color.rgba[2]};
  double alpha = color.rgba[3];
  GSHSCENE_Color_Space space = color.space;
  if (gshscene_color_space_is_target(&space)) {
    space = *gshscene_job_target_space(run->job);
  }
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &space, &frame->space);
  gshcpu_render_convert_straight(&c, rgb, rgb);
  s[0] = rgb[0] * alpha;
  s[1] = rgb[1] * alpha;
  s[2] = rgb[2] * alpha;
  s[3] = alpha;
}

/**
 * The gradient's stops, resolved and made interpolants once per render (S17, G3, G4): found by id, or resolved into
 * the pool now.  @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INTERNAL where the resolve fails (the plan refused that).
 */
static GSHSCENE_Result resolved_of(Run * run, const GSHSCENE_Resource * resource, const GSHSCENE_Gradient * g,
    const Resolved ** out) {
  uint64_t id = gshscene_resource_id(resource);
  size_t mask = run->layout.table - 1u;
  uint64_t h = id * UINT64_C(0x9E3779B97F4A7C15);
  size_t slot = (size_t)(h ^ (h >> 32)) & mask;
  while (run->resolved[slot].used && run->resolved[slot].id != id) {
    slot = (slot + 1u) & mask;
  }
  Resolved * e = &run->resolved[slot];
  if (!e->used) {
    GSHSCENE_Color_Space space;
    if (g->stop_count > run->layout.stops - run->pooled ||
        gshscene_gradient_resolve(resource, &run->foreground, run->palette, gshscene_job_target_space(run->job), &space,
            run->pool + run->pooled, run->layout.stops - run->pooled) != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    gshcpu_render_gradient_stops(run->pool + run->pooled, g->stop_count, g->premultiplied);
    e->used = 1;
    e->id = id;
    e->first = run->pooled;
    e->space = space;
    run->pooled += g->stop_count;
  }
  *out = e;
  return GSHSCENE_OK;
}

/**
 * A gradient fill (G6 to G12, S11): at each pixel the paint is G12's source at the pixel's center, under the chain
 * composed by shape-geometry and inverted once; the color is converted per pixel from the interpolation space to the
 * layer's (N3, nothing when they are equal).  Where G8 gives nothing the pixel is not drawn.  A composed map past the
 * double range, or a rectangle (the whole render's: S4) mapped outside G14's domain, is the sink's own limit (S6).
 */
static GSHSCENE_Result fill_gradient(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    Frame * frame, GSHCPU_Rect rect, GSHCPU_Rect whole, const uint8_t * cov) {
  if (gshcpu_render_rect_empty(whole)) {
    return GSHSCENE_OK; /* nothing of it is in the whole render */
  }
  GSHSCENE_Gradient g;
  const Resolved * e = NULL;
  if (gshscene_resource_gradient(item->paint.gradient, &g) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_Result r = resolved_of(run, item->paint.gradient, &g, &e);
  if (r != GSHSCENE_OK) {
    return r;
  }
  /* AD-24: T I_1 ... I_k M G, the gradient's map the leaf, composed by the one composer. */
  GSHGEOM_Map composed;
  run->maps[count - 1] = item->map;
  run->maps[count] = g.map;
  GSHCPU_Gradient gr;
  if (gshgeom_map_compose_chain(run->maps, count + 1, &run->job->target.map, &composed) != GSHGEOM_OK ||
      gshcpu_render_gradient_prepare(&gr, &g, run->pool + e->first, &composed, item->paint.rgba[3]) != GSHCPU_OK ||
      !gshcpu_render_gradient_in_domain(&gr, whole)) {
    return refuse(run, item, chain, count);
  }
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &e->space, &frame->space);
  widen(run, frame, rect);
  size_t w = (size_t)(rect.x1 - rect.x0);
  for (int64_t y = rect.y0; y < rect.y1; y++) {
    for (int64_t x = rect.x0; x < rect.x1; x++) {
      uint32_t m8 = cov[(size_t)(y - rect.y0) * w + (size_t)(x - rect.x0)];
      m8 = weigh_clips(run, frame->clip_base, run->clip_count, m8, x, y);
      if (m8 == 0) {
        continue;
      }
      double color[4];
      if (!gshcpu_render_gradient_at(&gr, (double)(int32_t)x + 0.5, (double)(int32_t)y + 0.5, color)) {
        continue; /* G8: outside the cone, the source is zero and src-over changes nothing */
      }
      gshcpu_render_convert_straight(&c, color, color);
      double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};
      size_t p = at(run, x, y);
      gshcpu_render_apply(&frame->layer[4u * p], s, (double)m8 / 255.0, GSHSCENE_COMPOSITE_SRC_OVER);
      if (run->top == 0) {
        run->touched[p] = 1;
      }
    }
  }
  run->work += (uint64_t)w * (uint64_t)(rect.y1 - rect.y0);
  return GSHSCENE_OK;
}

/**
 * The pixels of @p rect, weighed by the clips in force and the coverage @p cov (row by row over @p rect), composited
 * with the solid source @p src, SRC_OVER (N5, N7, D8).  A pixel of weight 0 is not touched.
 */
static void composite_solid(Run * run, Frame * frame, const double src[4], GSHCPU_Rect rect, const uint8_t * cov) {
  size_t w = (size_t)(rect.x1 - rect.x0);
  for (int64_t y = rect.y0; y < rect.y1; y++) {
    for (int64_t x = rect.x0; x < rect.x1; x++) {
      uint32_t m8 = cov[(size_t)(y - rect.y0) * w + (size_t)(x - rect.x0)];
      m8 = weigh_clips(run, frame->clip_base, run->clip_count, m8, x, y);
      if (m8 == 0) {
        continue;
      }
      size_t p = at(run, x, y);
      gshcpu_render_apply(&frame->layer[4u * p], src, (double)m8 / 255.0, GSHSCENE_COMPOSITE_SRC_OVER);
      if (run->top == 0) {
        run->touched[p] = 1;
      }
    }
  }
}

/** The coverage of @p cov over @p rect painted as the item's paint says: the fill's tail, which a stroke shares (SC5). */
static GSHSCENE_Result paint_coverage(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    Frame * frame, GSHCPU_Rect rect, GSHCPU_Rect whole, const uint8_t * cov) {
  if (item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
    return fill_gradient(run, item, chain, count, frame, rect, whole, cov);
  }
  double s[4];
  source_of(run, item, frame, s);
  widen(run, frame, rect);
  composite_solid(run, frame, s, rect, cov);
  run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
  return GSHSCENE_OK;
}

/**
 * A fill of the plane (shape-scene's PL2, PL7): the region it draws is every pixel the clips in force allow, each weighed
 * by the clips alone.  There is no outline to rasterize, so the coverage is 255 over the region and the paint's own tail
 * (a solid, or a gradient at the pixel centers) does the rest; no cell or point is charged.
 */
static GSHSCENE_Result fill_plane(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Frame * frame = &run->frames[run->top];
  GSHCPU_Rect rect = reach_of(run, frame->clip_base, run->clip_count);
  GSHCPU_Rect whole = whole_reach_of(run, frame->clip_base, run->clip_count);
  if (gshcpu_render_rect_empty(rect)) {
    rect.x1 = rect.x0;
    rect.y1 = rect.y0;
  } else {
    memset(run->coverage, 0xFF, (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0));
  }
  return paint_coverage(run, item, chain, count, frame, rect, whole, run->coverage);
}

static GSHSCENE_Result fill(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_PLANE) {
    return fill_plane(run, item, chain, count);
  }
  Frame * frame = &run->frames[run->top];
  GSHCPU_Rect rect;
  GSHCPU_Rect whole;
  GSHSCENE_Result r = shape(run, item, chain, count, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count), run->coverage, &rect, &whole);
  if (r == GSHSCENE_ERR_LIMIT) {
    return refuse(run, item, chain, count);
  }
  if (r != GSHSCENE_OK) {
    return r;
  }
  return paint_coverage(run, item, chain, count, frame, rect, whole, run->coverage);
}

/* --- a stroke (SC1 to SC8) ------------------------------------------------------------------------------- */

/** FT-2's receiver: one row of coverages, each weighed by the clips and composited if nonzero. */
typedef struct Sparse {
  Run * run;
  Frame * frame;
  const double * src;
  int64_t dx, dy;            /* the whole-pixel shift: the paths' frame to the destination's */
} Sparse;

static void sparse_span(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  Sparse * sp = ctx;
  Run * run = sp->run;
  Frame * frame = sp->frame;
  for (size_t i = 0; i < n; i++) {
    if (m8[i] == 0) {
      continue;
    }
    int64_t px = x + (int64_t)i + sp->dx;
    int64_t py = y + sp->dy;
    uint32_t m = weigh_clips(run, frame->clip_base, run->clip_count, m8[i], px, py);
    if (m == 0) {
      continue;
    }
    size_t p = at(run, px, py);
    gshcpu_render_apply(&frame->layer[4u * p], sp->src, (double)m / 255.0, GSHSCENE_COMPOSITE_SRC_OVER);
    if (run->top == 0) {
      run->touched[p] = 1;
    }
  }
}

/**
 * The exact step's work since the last call, in all three parts' memory (it is counted once, whichever part did it); the
 * cells it made exact and the cells it could not reproduce go to the sink's counters.
 */
static uint64_t exact_work(Run * run) {
  uint64_t w = 0;
  uint64_t cells = 0;
  uint64_t failures = 0;
  for (int p = 0; p < 3; p++) {
    w += run->exact[p].work;
    cells += run->exact[p].cells;
    failures += run->exact[p].failures;
    run->exact[p].work = 0;
    run->exact[p].cells = 0;
    run->exact[p].failures = 0;
  }
  gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_cells), cells);
  gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_failures), failures);
  return w;
}

/**
 * SC11, ST9: the expansion's own steps, counted whether the expansion was made or found, so a report does not depend on the
 * cache or the tier.  A run adds the elements of the glyph outlines that went into its centerline (ST1), once, and its
 * expansion's steps are those of the centerline's elements, clamped to the resource limit the bound states.
 */
static uint64_t expansion_work(const Run * run, const GSHSCENE_Item * item, const GSHCPU_Stroke * s) {
  uint64_t points = 0;
  for (int p = 0; p < GSHCPU_STROKE_PARTS; p++) {
    points += s->meta.points[p];
  }
  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);
  uint64_t elements = path ? (uint64_t)gshgeom_path_element_count(path) : s->run_elements;
  if (elements > run->cs->elements) {
    elements = run->cs->elements;
  }
  return points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + (path ? 0u : elements);
}

/**
 * The glyph notes of a stroked run's centerline (ST1, ST8), reported as a fill's are (glyph.h, D7); a glyph refused
 * under strict stops the render at the item (S6).  @return ::GSHSCENE_OK, or the result to stop with.
 */
static GSHSCENE_Result report_run_notes(Run * run, const GSHCPU_Stroke * s, const size_t * chain, size_t count) {
  for (size_t i = 0; i < s->note_count; i++) {
    const GSHCPU_Run_Note * n = &s->notes[i];
    GSHSCENE_Result e = gshscene_job_report_glyph(
        run->job, (GSHSCENE_Entry_Class)n->entry_class, (GSHSCENE_Reason)n->reason, chain, count, (size_t)n->index);
    if (e == GSHSCENE_OK && n->entry_class == GSHSCENE_ENTRY_REFUSED && (run->request->flags & GSHSCENE_RENDER_STRICT)) {
      memcpy(run->stop, chain, count * sizeof *chain);
      run->stop_count = count;
      e = GSHSCENE_ERR_REFUSED;
    }
    if (e != GSHSCENE_OK) {
      return e;
    }
  }
  return GSHSCENE_OK;
}

/**
 * A stroke (SC1 to SC8): the centerline expanded at the canonical map by shape-geometry, the parts covered and
 * composed into one coverage `k`, and from there a fill's tail (the clips, the layers, the operator, the paint).
 */
static GSHSCENE_Result stroke(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Frame * frame = &run->frames[run->top];
  Cpu_Sink * cs = run->cs;
  GSHGEOM_Map composed;
  run->maps[count - 1] = item->map;
  if (gshgeom_map_compose_chain(run->maps, count, &run->job->target.map, &composed) != GSHGEOM_OK) {
    return refuse(run, item, chain, count); /* SC4: a composed map that cannot be formed is the sink's own limit */
  }
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_reset(&run->arena);
  }
  /* SC7: the cache serves a render that allocates, never one that takes its memory from the caller (S9). */
  GSHCPU_Stroke_Cache * cache = !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;
  GSHCPU_Stroke s;
  GSHCPU_Result opened = gshcpu_render_stroke_open(cache, run->item_alloc, &cs->geometry, item, &composed, &s);
  if (opened == GSHCPU_ERR_LIMIT) {
    return refuse(run, item, chain, count);
  }
  if (opened != GSHCPU_OK) {
    return scene_result(opened);
  }
  GSHSCENE_Result noted = report_run_notes(run, &s, chain, count);
  if (noted != GSHSCENE_OK) {
    gshcpu_render_stroke_close(cache, &s);
    return noted;
  }
  if (!s.drawn) {
    gshcpu_render_stroke_close(cache, &s);
    return GSHSCENE_OK; /* an EMPTY or NOT_INVERTIBLE outline (a run of no outlines): the notes are reported, nothing is drawn */
  }
  run->work += expansion_work(run, item, &s);
  GSHCPU_Rect reach = reach_of(run, frame->clip_base, run->clip_count);
  GSHCPU_Rect wreach = whole_reach_of(run, frame->clip_base, run->clip_count);
  GSHCPU_Rect rect;
  GSHCPU_Rect whole;
  uint64_t cells = 0;
  GSHCPU_Result r;
  uint8_t * cov = run->coverage;
  GSHCPU_Exact * exact = cs->tiers & GSHCPU_RENDER_TIER_EXACT ? run->exact : NULL; /* UC1 to UC11 (off only as a gate's control) */
  int solid = item->paint.kind != GSHSCENE_PAINT_GRADIENT;
  if (solid && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) && gshcpu_render_stroke_sparse_ok(&s, cs->max_cells)) {
    /* FT-2 (SC8): the sweep gives only the pixels that can be nonzero; the rest the general loop would skip. */
    if (run->acc_dirty) {
      memset(run->acc, 0, 2u * ((size_t)run->layout.width + 1u) * GSHCPU_RENDER_BAND_ROWS * sizeof *run->acc);
      run->acc_dirty = 0;
    }
    gshcpu_render_stroke_rects(&s, reach, wreach, &rect, &whole);
    double src[4];
    source_of(run, item, frame, src);
    widen(run, frame, rect);
    Sparse sp = {run, frame, src, s.shift_x, s.shift_y};
    GSHCPU_Rect drawn;
    GSHCPU_Rect counted;
    r = gshcpu_render_stroke_cover_sparse(&s, reach, wreach, cs->max_cells, run->acc, exact, run->coverage, sparse_span,
        &sp, &drawn, &counted, &cells);
    run->work += exact_work(run);
    /* Never refused for its cells (the cap was bounded by the detection), but the arithmetic is shown to hold. */
    if (r == GSHCPU_OK) {
      run->work += cells + (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
      gshcpu_stroke_cache_count(cs->cache, s.hairline ? offsetof(GSHCPU_Render_Counters, sparse_hairline)
                                                      : offsetof(GSHCPU_Render_Counters, sparse_plain));
      gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, strokes));
    }
    gshcpu_render_stroke_close(cache, &s);
    return r == GSHCPU_OK ? GSHSCENE_OK : r == GSHCPU_ERR_LIMIT ? refuse(run, item, chain, count) : scene_result(r);
  }
  GSHCPU_Stroke_Buffers buffers = {run->coverage, run->cov_aligned, run->cov_clip, run->cov_result};
  r = gshcpu_render_stroke_cover(&s, reach, wreach, cs->max_cells, run->acc,
      2u * ((size_t)run->layout.width + 1u) * GSHCPU_RENDER_BAND_ROWS, exact, (cs->tiers & GSHCPU_RENDER_TIER_JOINT) != 0, &buffers, &cov, &rect, &whole, &cells);
  run->acc_dirty = 1;
  run->work += cells + exact_work(run);
  GSHSCENE_Result result;
  if (r == GSHCPU_ERR_LIMIT) {
    result = refuse(run, item, chain, count);
  } else if (r != GSHCPU_OK) {
    result = scene_result(r);
  } else {
    gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, strokes));
    if (s.meta.present[GSHCPU_STROKE_ALIGNED] && run->cov_result) {
      run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0); /* the composition's pass */
    }
    result = paint_coverage(run, item, chain, count, frame, rect, whole, cov);
  }
  gshcpu_render_stroke_close(cache, &s);
  return result;
}

static GSHSCENE_Result clip_push(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Frame * frame = &run->frames[run->top];
  Clip * clip = &run->clips[run->clip_count];
  clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;
  GSHSCENE_Result r = shape(run, item, chain, count, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count), clip->coverage, &clip->rect, &clip->whole);
  if (r == GSHSCENE_ERR_LIMIT) {
    return refuse(run, item, chain, count);
  }
  if (r != GSHSCENE_OK) {
    return r;
  }
  run->clip_count++;
  return GSHSCENE_OK;
}

static void group_push(Run * run, const GSHSCENE_Item * item) {
  const Frame * parent = &run->frames[run->top];
  Frame * f = &run->frames[++run->top];
  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u; /* made ready as it is drawn on (widen) */
  f->group = item->group;
  f->space = gshscene_color_space_is_target(&item->group.blend_space) ? parent->space : item->group.blend_space;
  f->clip_base = run->clip_count;
  f->dirty.x0 = f->dirty.y0 = f->dirty.x1 = f->dirty.y1 = 0;
  run->groups_open++;
  run->work += run->layout.pixels;
}

static GSHSCENE_Result group_pop(Run * run) {
  const Frame * f = &run->frames[run->top];
  Frame * parent = &run->frames[run->top - 1u];
  uint32_t op = f->group.composite;
  int bounded = gshcpu_render_bounded(op);
  /* A bounded operator changes nothing where the group drew nothing (N4); an unbounded one acts wherever it is
   * weighed above zero (N8). */
  GSHCPU_Rect region = bounded ? f->dirty : run->layout.region;
  region = gshcpu_render_rect_meet(region, reach_of(run, parent->clip_base, f->clip_base));
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);
  const double o = f->group.opacity;
  /* The group's layer is zero outside its dirty rectangle (widen), where it was never made ready. */
  static const double zero[4] = {0.0, 0.0, 0.0, 0.0};
  widen(run, parent, region);
  for (int64_t y = region.y0; y < region.y1; y++) {
    for (int64_t x = region.x0; x < region.x1; x++) {
      uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);
      if (m8 == 0) {
        continue;
      }
      size_t p = at(run, x, y);
      double s[4];
      gshcpu_render_convert_premultiplied(&c, inside(f->dirty, x, y) ? &f->layer[4u * p] : zero, s);
      for (int i = 0; i < 4; i++) {
        s[i] = o * s[i];
      }
      if (bounded && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0) {
        continue;
      }
      if (!gshcpu_render_apply(&parent->layer[4u * p], s, (double)m8 / 255.0, op)) {
        return GSHSCENE_ERR_INTERNAL; /* unreachable: the planning pass refuses an operator the caps do not claim */
      }
      if (run->top == 1u) {
        run->touched[p] = 1;
      }
    }
  }
  if (!gshcpu_render_rect_empty(region)) {
    run->work += (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);
  }
  run->top--;
  run->groups_open--;
  return GSHSCENE_OK;
}

/** Go through one unit.  @return ::GSHSCENE_OK to go on, or the result to stop with. */
static GSHSCENE_Result unit(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  if (skip_step(&run->skip, item, count)) {
    return GSHSCENE_OK;
  }
  GSHSCENE_Disposition d = gshscene_caps_disposition(&run->job->caps, item, run->maps, count - 1,
      &run->job->target.map, gshscene_job_target_space(run->job), run->palette);
  GSHSCENE_Glyph_Route route;
  if (d.entry_class == GSHSCENE_ENTRY_REFUSED ||
      (gshcpu_render_glyph_route(item, run->maps, count - 1, &run->job->target.map, &route) &&
          route.entry_class == GSHSCENE_ENTRY_REFUSED)) {
    /* The planning pass reported it. */
    skip_begin(&run->skip, item, count);
    return GSHSCENE_OK;
  }
  if (item->kind == GSHSCENE_ITEM_STROKE
          ? gshscene_stroke_notice(item, run->maps, count - 1, &run->job->target.map)
          : gshscene_gradient_notice(item, run->maps, count - 1, &run->job->target.map)) {
    return GSHSCENE_OK; /* S17, SC4: a gradient fill or a stroke that draws nothing; the plan wrote its notice */
  }
  switch (item->kind) {
    case GSHSCENE_ITEM_FILL:
      return fill(run, item, chain, count);
    case GSHSCENE_ITEM_STROKE:
      return stroke(run, item, chain, count);
    case GSHSCENE_ITEM_CLIP_PUSH:
      return clip_push(run, item, chain, count);
    case GSHSCENE_ITEM_CLIP_POP:
      run->clip_count--;
      return GSHSCENE_OK;
    case GSHSCENE_ITEM_GROUP_PUSH:
      group_push(run, item);
      return GSHSCENE_OK;
    case GSHSCENE_ITEM_GROUP_POP:
      return group_pop(run);
    case GSHSCENE_ITEM_INSTANCE:
      run->maps[count - 1] = item->map;
      return GSHSCENE_OK;
    case GSHSCENE_ITEM_PLACEHOLDER:
      return GSHSCENE_OK; /* PH6: nothing is drawn; the planning pass reported it, refused or approximated */
    default:
      return GSHSCENE_ERR_INTERNAL;
  }
}

/* --- a render: begin to completion ---------------------------------------------------------------- */

/** The one rounding (S11): every touched pixel of the root layer (all in its dirty rectangle) to the target. */
static void write_target(Run * run) {
  const GSHCPU_Rect d = run->frames[0].dirty;
  for (int64_t y = d.y0; y < d.y1; y++) {
    unsigned char * row = run->pixels + (size_t)y * run->stride;
    for (int64_t x = d.x0; x < d.x1; x++) {
      size_t p = at(run, x, y);
      if (run->touched[p]) {
        gshcpu_render_store(run->format, row, (size_t)x, &run->root[4u * p]);
      }
    }
  }
}

static const char * reason_of(GSHSCENE_Result result) {
  switch (result) {
    case GSHSCENE_ERR_CANCELLED:
      return "cancelled between units";
    case GSHSCENE_ERR_REFUSED:
      return "strict: refused while drawing (the sink's resource limit, or a glyph)";
    case GSHSCENE_ERR_OOM:
      return "out of memory while drawing";
    default:
      return "the CPU sink failed";
  }
}

/** Write what is done (the prefix), hand the result to the contract, free the memory. */
static void finish(Run * run, GSHSCENE_Result result, void * memory) {
  Cpu_Sink * cs = run->cs;
  if (cs->observer) {
    /* The observer sees the whole region (render_internal.h): what no unit reached is made what it would have been
     * had the root been read in full at the start, the target's values and an untouched map. */
    const GSHCPU_Rect R = run->layout.region;
    for (int64_t y = R.y0; y < R.y1; y++) {
      for (int64_t x = R.x0; x < R.x1; x++) {
        if (!inside(run->frames[0].dirty, x, y)) {
          size_t p = at(run, x, y);
          run->touched[p] = 0;
          gshcpu_render_load(run->format, run->pixels + (size_t)y * run->stride, (size_t)x, &run->root[4u * p]);
        }
      }
    }
    cs->observer(cs->observer_ctx, run->layout.region, run->root, run->touched);
  }
  write_target(run);
  GSHSCENE_Job * job = run->job;
  gshscene_job_set_frame_bytes(job, run->layout.fixed);
  gshscene_job_complete(job, result, run->stop, run->stop_count, result == GSHSCENE_OK ? NULL : reason_of(result));
  if (memory) {
    cs->alloc.free_fn(cs->alloc.ctx, memory);
  }
}

static GSHSCENE_Result op_begin(void * impl, GSHSCENE_Job * job) {
  Cpu_Sink * cs = impl;
  const GSHSCENE_Render_Request * request = gshscene_job_request(job);
  const GSHSCENE_Target * target = request->target;
  Layout L;
  GSHCPU_Result lr = layout(cs, request->list, target, &L);
  if (lr != GSHCPU_OK) {
    return scene_result(lr);
  }
  void * memory = NULL;
  unsigned char * raw;
  size_t raw_bytes;
  int caller = (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) != 0;
  if (caller) {
    raw = request->workspace; /* the contract layer has checked it holds the need */
    raw_bytes = request->workspace_bytes;
  } else {
    memory = cs->alloc.malloc_fn(cs->alloc.ctx, L.fixed);
    if (!memory) {
      return GSHSCENE_ERR_OOM;
    }
    raw = memory;
    raw_bytes = L.fixed;
  }
  unsigned char * base = (unsigned char *)(((uintptr_t)raw + (CARVE_ALIGN - 1u)) & ~(uintptr_t)(CARVE_ALIGN - 1u));
  Run * run = (Run *)(void *)base;
  memset(run, 0, sizeof *run);
  run->cs = cs;
  run->job = job;
  run->request = request;
  run->layout = L;
  run->raster = target->surface;
  run->format = gshcpu_format_of(run->raster);
  run->pixels = gimg_raster_pixels(run->raster);
  run->stride = gimg_raster_stride_bytes(run->raster);
  run->maps = (GSHGEOM_Map *)(void *)(base + L.at_maps);
  run->stop = (size_t *)(void *)(base + L.at_stop);
  run->frames = (Frame *)(void *)(base + L.at_frames);
  run->clips = (Clip *)(void *)(base + L.at_clips);
  run->root = (double *)(void *)(base + L.at_root);
  run->layers = (double *)(void *)(base + L.at_layers);
  run->touched = base + L.at_touched;
  run->coverage = base + L.at_coverage;
  run->clip_memory = base + L.at_clip_memory;
  if (L.aligned) {
    run->cov_aligned = base + L.at_cov_aligned;
    run->cov_clip = base + L.at_cov_clip;
    run->cov_result = base + L.at_cov_result;
  }
  run->acc = (int64_t *)(void *)(base + L.at_acc);
  if (L.strokes) {
    size_t width = (size_t)(L.whole.x1 - L.whole.x0);
    gshcpu_exact_init(&run->exact[0], base + L.at_exact, width, cs->max_exact, L.aligned ? 3 : 1);
    run->exact[0].eps = GSHCPU_RENDER_EXACT_EPS;
    if (L.aligned) {
      gshcpu_exact_init(&run->exact[1], base + L.at_exact + L.exact_second, width, cs->max_exact, 0);
      gshcpu_exact_init(&run->exact[2], base + L.at_exact + L.exact_third, width, cs->max_exact, 0);
      run->exact[1].eps = GSHCPU_RENDER_EXACT_EPS;
      run->exact[2].eps = GSHCPU_RENDER_EXACT_EPS;
    }
  }
  run->acc_dirty = 1; /* the caller's memory is not zeroed: the sparse sweep clears the accumulator before its first use */
  run->caller = caller;
  run->resolved = (Resolved *)(void *)(base + L.at_table);
  run->pool = (GSHCPU_Stop *)(void *)(base + L.at_pool);
  memset(run->resolved, 0, L.table * sizeof *run->resolved); /* the gradient fills' slots, never the region */
  if (caller) {
    /* The items' geometry from the rest of the workspace. */
    gshcpu_render_arena_init(&run->arena, base + L.at_items, raw_bytes - (size_t)(base - raw) - L.at_items);
    run->item_alloc = &run->arena.self;
  } else {
    run->item_alloc = &cs->alloc;
  }
  if (gshscene_expansion_init(&run->walk, request->list, base + L.at_walk, L.walk) != GSHSCENE_OK ||
      gshscene_list_foreground(request->list, &run->foreground) != GSHSCENE_OK) {
    if (memory) {
      cs->alloc.free_fn(cs->alloc.ctx, memory);
    }
    return GSHSCENE_ERR_INTERNAL;
  }
  run->palette = gshscene_list_palette(request->list);
  /* The root layer is the target (N6): its values are read once, as the units reach them (widen). */
  Frame * root = &run->frames[0];
  memset(root, 0, sizeof *root);
  root->layer = run->root;
  root->space = *gshscene_job_target_space(job);

  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  while (gshscene_expansion_next(&run->walk, &item, &chain, &count)) {
    run->work = 0;
    GSHSCENE_Result r = unit(run, item, chain, count);
    gshscene_job_work(job, run->work);
    if (r != GSHSCENE_OK) {
      if (run->stop_count == 0 && r != GSHSCENE_ERR_REFUSED) {
        memcpy(run->stop, chain, count * sizeof *chain);
        run->stop_count = count;
      }
      finish(run, r, memory);
      return GSHSCENE_OK;
    }
    run->processed++;
    if (run->groups_open == 0) {
      run->done = run->processed;
    }
    if (gshscene_job_unit(job, run->processed, run->done)) {
      finish(run, GSHSCENE_ERR_CANCELLED, memory);
      return GSHSCENE_OK;
    }
  }
  finish(run, GSHSCENE_OK, memory);
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_readback(void * impl, const GSHSCENE_Job * job, GIMG_Raster * out) {
  (void)impl;
  const GSHSCENE_Target * target = &job->target;
  const GIMG_Raster * raster = target->surface;
  size_t bytes = gshcpu_render_pixel_bytes(gshcpu_format_of(raster));
  const unsigned char * src = gimg_raster_pixels_const(raster);
  unsigned char * dst = gimg_raster_pixels(out);
  size_t src_stride = gimg_raster_stride_bytes(raster);
  size_t dst_stride = gimg_raster_stride_bytes(out);
  size_t row = (size_t)(target->extent.x1 - target->extent.x0) * bytes;
  for (int64_t y = target->extent.y0; y < target->extent.y1; y++) {
    memcpy(dst + (size_t)(y - target->extent.y0) * dst_stride, src + (size_t)y * src_stride + (size_t)target->extent.x0 * bytes,
        row);
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_evict(void * impl, uint64_t id) {
  Cpu_Sink * cs = impl;
  gshcpu_stroke_cache_evict(cs->cache, id);
  return GSHSCENE_OK;
}

static uint64_t op_static_bytes(void * impl) {
  Cpu_Sink * cs = impl;
  return gshcpu_stroke_cache_bytes(cs->cache);
}

static void op_destroy(void * impl) {
  Cpu_Sink * cs = impl;
  GSHGEOM_Allocator alloc = cs->alloc;
  gshcpu_stroke_cache_destroy(cs->cache);
  alloc.free_fn(alloc.ctx, cs);
}

/* --- making one -------------------------------------------------------------------------------------- */

int gshcpu_sink_is(const GSHSCENE_Sink * sink) {
  /* The contract's object is opaque; the name in the ops table is what can be asked. */
  const char * name = gshscene_sink_name(sink);
  return sink && name && strcmp(name, "shape-cpu") == 0;
}

GSHCPU_Result gshcpu_render_sink_create_tiered(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, unsigned tiers, GSHCPU_Render_Counters * counters, GSHCPU_Render_Observer observer,
    void * observer_ctx, GSHSCENE_Sink ** out) {
  if (!out) {
    return GSHCPU_ERR_INVALID;
  }
  if (limits && limits->max_exact_fragments > GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS) {
    return GSHCPU_ERR_INVALID; /* UC9: the step is quadratic in a pixel's pieces: a value past the ceiling is refused, not clamped */
  }
  if (!alloc) {
    alloc = gshgeom_allocator_default();
  }
  Cpu_Sink * cs = alloc->calloc_fn(alloc->ctx, 1, sizeof *cs);
  if (!cs) {
    return GSHCPU_ERR_OOM;
  }
  cs->alloc = *alloc;
  cs->observer = observer;
  cs->observer_ctx = observer_ctx;
  cs->tiers = tiers;
  cs->counters = counters ? counters : &cs->own;
  memset(cs->counters, 0, sizeof *cs->counters);
  cs->elements = scene_limits && scene_limits->max_resource_elements ? scene_limits->max_resource_elements
                                                                       : GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS;
  cs->max_dimension = limits && limits->max_dimension ? limits->max_dimension : GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;
  if (cs->max_dimension > GSHCPU_MAX_DIMENSION_CEILING) {
    cs->max_dimension = GSHCPU_MAX_DIMENSION_CEILING;
  }
  cs->max_bytes = limits && limits->max_bytes ? limits->max_bytes : GSHCPU_LIMITS_DEFAULT_MAX_BYTES;
  cs->max_cells = limits && limits->max_cells ? limits->max_cells : GSHCPU_LIMITS_DEFAULT_MAX_CELLS;
  cs->max_exact = limits && limits->max_exact_fragments ? limits->max_exact_fragments
                                                        : GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS;
  gshgeom_limits_default(&cs->geometry);
  if (limits && limits->max_points) {
    cs->geometry.max_points = limits->max_points;
  }
  GSHCPU_Result cr = gshcpu_stroke_cache_create(&cs->alloc, limits ? limits->max_cache_bytes : 0, cs->counters, &cs->cache);
  if (cr != GSHCPU_OK) {
    alloc->free_fn(alloc->ctx, cs);
    return cr;
  }
  GSHSCENE_Sink_Ops ops;
  memset(&ops, 0, sizeof ops);
  ops.name = "shape-cpu";
  ops.thread_affine = 0;
  ops.caps = op_caps;
  ops.workspace_bytes = op_workspace_bytes;
  ops.begin = op_begin;
  ops.readback = op_readback;
  ops.evict = op_evict;
  ops.static_bytes = op_static_bytes;
  ops.destroy = op_destroy;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Result r = gshscene_sink_create(alloc, scene_limits, &ops, cs, &sink);
  if (r != GSHSCENE_OK) {
    gshcpu_stroke_cache_destroy(cs->cache);
    alloc->free_fn(alloc->ctx, cs);
    return r == GSHSCENE_ERR_OOM ? GSHCPU_ERR_OOM : GSHCPU_ERR_INVALID;
  }
  *out = sink;
  return GSHCPU_OK;
}

GSHCPU_Result gshcpu_render_sink_create_observed(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, GSHCPU_Render_Observer observer, void * observer_ctx, GSHSCENE_Sink ** out) {
  return gshcpu_render_sink_create_tiered(alloc, scene_limits, limits, GSHCPU_RENDER_TIERS_ALL, NULL, observer,
      observer_ctx, out);
}

GSHCPU_Result gshcpu_sink_create(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, GSHSCENE_Sink ** out) {
  return gshcpu_render_sink_create_observed(alloc, scene_limits, limits, NULL, NULL, out);
}
