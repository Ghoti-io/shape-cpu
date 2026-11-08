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
 *
 * A masked group (render.h, MC1 to MC10) is composited at its pop: the walk of the mask's picture into one more layer over
 * the pop's region (mask_pass), the value of the mask at each pixel (mask.c), and the pop's composite weighed by it.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include "../core/fold.h"

#include <ghoti.io/cutil/safemath.h>
#include <ghoti.io/shape-geometry/clip.h>
#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-geometry/hash.h>
#include <ghoti.io/shape-geometry/lattice.h>
#include <ghoti.io/shape-geometry/stroke.h>
#include <ghoti.io/shape-scene/clip_union.h>
#include <ghoti.io/shape-scene/hatch.h>
#include <ghoti.io/shape-scene/pattern.h>
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
  uint64_t max_deposits;
  uint64_t max_exact;        /* the most pieces of a stroke part in one pixel (UC9) */
  uint64_t max_crossings;    /* the most crossings of those pieces in one pixel (UC19) */
  GSHGEOM_Limits geometry;   /* max_points for the flattener */
  GSHCPU_Render_Observer observer;
  void * observer_ctx;
  GSHCPU_Stroke_Cache * cache;      /* SC7: the expansions kept across renders */
  GSHCPU_Render_Counters own;       /* the counters, unless a test gave a place for them */
  GSHCPU_Render_Counters * counters;
  int fast;                         /* GSHCPU_Limits.stroke_coverage is FAST */
  int fast_fill;                    /* GSHCPU_Limits.fill_coverage is FAST */
  GSHCPU_Executor executor;         /* the caller's, or none (run NULL) */
  unsigned tiers;                   /* GSHCPU_RENDER_TIER_*: the tiers that are on */
  size_t elements;                  /* the most path elements a stored resource may hold: SC11's term */
  uint64_t max_area_pixel_work;     /* IC9: the most texels and cells one pixel of AREA visits */
  uint64_t max_image_work;          /* IC9: the most one image item visits over the whole render */
  uint64_t max_pattern_cells;       /* PC8: the most copies of one use of a pattern, summed over the nesting */
  uint64_t max_hatch_lines;         /* HX12: the most lines of one hatch fill */
  uint64_t max_union_tests;         /* CU19: the most box comparisons of the predicate over all the union pushes of a render */
  uint64_t max_union_pieces;        /* CG9: the most pieces the general tier keeps for the pixels of one group of one push */
  size_t union_members;             /* the most members a union may have (the store's limit): CG11's term of the declared work bound */
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
  int bytes;                 /* fast8: the layer is premultiplied bytes (px8), not doubles: the root and a group in the target's space */
  int general;               /* a general layer (KC2): its elements composite by KC4; takes the padding the record had */
  unsigned char * px8;       /* fast8: the layer as premultiplied bytes (the target's own for the root), pixel (x, y) at ... */
  size_t stride8;            /* ... px8 + (y - oy) * stride8 + 4 (x - ox) */
  int64_t ox, oy;
} Frame;

/** A clip in force: its coverage inside its rectangle (0 outside it). */
typedef struct Clip {
  GSHCPU_Rect rect;
  GSHCPU_Rect whole;         /* its rectangle in the whole render (the extent, not the damage): where cells are met */
  uint8_t * coverage;        /* rect's width by height */
} Clip;

/** A gradient resolved in this render (S17: once per gradient per render), by resource id. */
/** The color tables kept for the fast fill coverage: the gradients most recently painted. */
#define TABLE_SLOTS 2u

/** The points of the table of a transfer between two spaces of one gamut (fast8). */
#define XFER_POINTS 4096u

/** Which gradient, paint alpha and layer space a color table was made for. */
typedef struct Table_Key {
  int used;
  uint64_t id;
  double alpha;
  int bytes;                 /* the table is bytes (fast8), not doubles */
  GSHSCENE_Color_Space space;
} Table_Key;

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
  int other;                 /* it holds a fill, or a stroke painted with a gradient */
  int wide;                  /* it holds a color with a component outside [0, 1]: no byte layer can hold it */
  size_t exact_bytes;        /* ... of the first part's, and (only when something aligns) of the second's and the third's */
  size_t exact_second, exact_third; /* ... where, from at_exact */
  size_t acc_bytes;          /* the cell accumulator: one for each of the three parts when something aligns (UC8); with a fast coverage, room for the lean cover's bands */
  size_t acc_exact;          /* ... the part of it the dense sweeps write */
  size_t at_walk, at_maps, at_stop, at_frames, at_clips, at_root, at_layers, at_touched, at_coverage, at_clip_memory,
      at_acc, at_exact, at_table, at_pool, at_tables, at_items; /* offsets from the aligned base */
  size_t at_cov_aligned, at_cov_clip, at_cov_result; /* the aligned stroke's arrays */
  size_t fixed;              /* the bytes before the items' arena, the alignment slack included */
} Layout;

/**
 * What a render of a list with a masked group keeps beside its Run (MC5, MC9): carved from the render's memory only then, directly
 * after the Run, so that a list with no masked group has the memory it had before masks.
 */
typedef struct Mask_Frame {
  GSHCPU_Rect whole_dirty;   /* the pixels the whole render's units reach of this layer: Frame.dirty without the damage (MC6) */
  size_t push;               /* the index of the group's push in its list: the chain of the mask's items names it (MK9) */
} Mask_Frame;

/**
 * What a pass of a pattern's tile keeps beside the render's state (PC2): in the mask area, which a list that holds a pattern gets (so a
 * list that holds none has the memory it had before patterns).
 */
typedef struct Pat_Level Pat_Level;

typedef struct Pat_Run {
  Pat_Level * pat;           /* the pass in progress (the innermost), or NULL */
  unsigned char * pat_mem;   /* the passes' memory, one stride for each level */
  size_t pat_depth;          /* the passes in progress */
  uint64_t pat_total;        /* PC8: the copies counted for the use in progress, summed over its nesting */
  int64_t org_x, org_y;      /* the pixel the layers' first element is: at() (the region's corner, or in a nested pass its corner moved by the copy's shift) */
  GSHCPU_Rect whole_cur;     /* what the whole render draws, in the frame of coordinates in force (the render's, or a nested pass's) */
  size_t pat_levels;         /* the most passes at once, and the bytes of each one's memory */
  size_t pat_stride;
  int quiet;                 /* painter_begin does not widen the frame (the pass accumulates, then widens where it applies) */
  int64_t pshx, pshy;        /* the whole-pixel shift of the copy whose unit's paint is being begun (PC3) */
} Pat_Run;

typedef struct Mask_State {
  Pat_Run P;
  GSHCPU_Rect window;        /* the pixels the units reach: the region, or in a mask pass the pop region P (MC2) */
  GSHCPU_Rect whole_window;  /* the same in the whole render: the whole region, or in a mask pass the whole render's P */
  Mask_Frame * frames;       /* one for each frame, the root's too */
  size_t * chain;            /* the chain of a mask's item: the group's, the push's index, the item's in its picture */
  unsigned char * walk;      /* the walks of the pictures of the masks being drawn, one on another */
  size_t walk_bytes;
  size_t walk_used;
} Mask_State;

/** The sizes of that area, and where its parts lie in it (from its start). */
typedef struct Mask_Layout {
  size_t bytes;              /* 0 when no group in the list has a mask */
  size_t walk;               /* the most the nested walks of masks' pictures take */
  size_t at;                 /* where the area starts, from the Run: directly after it */
  size_t at_frames, at_chain, at_walk;
  size_t pat_levels;         /* the deepest nest of patterns: a pass of the tile's units for each */
  size_t pat_stride;         /* the bytes of one level's pass memory */
  size_t at_pat;
} Mask_Layout;

/**
 * What a general layer keeps beside its Frame (KC2, KC3): carved after the Frame array only when the list has a group that is non-isolated
 * or knockout, so that a list with none has the memory it had before them (KC9).
 */
typedef struct Gen_Frame {
  double * ag;                    /* a non-isolated group: its group alpha, one double a pixel; else NULL (the layer's alpha is it) */
  double * fg;                    /* a proper ancestor is knockout: the group shape, one double a pixel; else NULL */
  GSHCPU_Convert from_parent;     /* a non-isolated group: the parent's space into this layer's (N3), for the initial backdrop */
  int on;                         /* a general layer */
  int nonisolated;
  int knockout;
  int ko_anc;                     /* some proper ancestor in this walk is knockout (KC3) */
} Gen_Frame;

typedef struct Gen_State {
  double * ag;                    /* the group alpha channels: one for each non-isolated group open at once */
  double * fg;                    /* the group shape channels */
  size_t ag_used, fg_used;        /* the channels of the groups open now */
  Gen_Frame * frames;             /* one for each Frame, the root's too */
} Gen_State;

/** The sizes of that, and where the channels lie (from the aligned base). */
typedef struct Gen_Layout {
  int on;                         /* a general layer may exist: a group that is non-isolated or knockout, or the forced switch */
  size_t ag_slots;                /* the most non-isolated groups open at once (KC8) */
  size_t fg_slots;                /* the most groups open at once that have a knockout proper ancestor */
  size_t records;                 /* bytes after the Frame array: the state and a record for each Frame */
  size_t at_ag, at_fg;
} Gen_Layout;

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
  int straight8;             /* ... and the target is straight alpha: its root is a byte layer of its own, loaded and stored */
  int fast8;                 /* the fast fill coverage on an 8-bit premultiplied or straight target: the layers are bytes, the root the target itself */
  int direct8;               /* the fast coverage composes straight into the target's bytes (strokes alone, 8-bit premultiplied) */
  unsigned masked : 1;       /* the list holds a masked group: a Mask_State follows the Run (it takes the Run's padding, so a list with no mask lays out as it did) */
  unsigned general : 1;      /* the list holds a general layer (KC2): a Gen_State follows the Frame array */
  unsigned union_general : 1; /* the union push in hand is drawn by the general tier (CG10): set by unit() with the predicate's verdict */
  GSHCPU_Batch * batch;      /* ... and its strokes are queued and drawn over stripes by several threads (GSHCPU_Limits.threads) */
  size_t acc_zero;           /* the accumulator's first bytes known to be zero: the caller's memory is not zeroed, the dense sweeps leave theirs written, and a sparse one needs the part it uses zero */
  int caller;                /* CALLER_MEMORY: nothing allocated, so no cache */
  uint32_t union_tests_lo;   /* CU19: the comparisons the union pushes of this render have cost so far, low half; the two halves sit in the Run's padding, so a list with no union lays out as it did (CU20) */
  Resolved * resolved;       /* the gradients resolved so far (S17), open addressing on the id */
  GSHCPU_Stop * pool;        /* their stops */
  double * tables;           /* TABLE_SLOTS color tables (gradient.c), when the layout has gradients */
  Table_Key table_key[TABLE_SLOTS];
  float xfer[XFER_POINTS + 1u]; /* fast8: the transfer of a group's space into its parent's, at XFER_POINTS + 1 points of 0..1 */
  GSHSCENE_Color_Space xfer_from, xfer_to;
  int xfer_ready;
  uint32_t union_tests_hi;   /* ... and the high half */
  size_t table_next;
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

/**
 * One pass of a pattern's tile (PC2): the lattice picture of one use drawn into a layer, and what the pass keeps while it draws the
 * tile's units over the copies.  The passes nest as the patterns do (PC5), one record for each.
 */
/** PC4: a clip pushed in the tile, kept so that the clip of any one copy can be made again (the unit's weight in a copy is its own clip's). */
typedef struct Pat_Clip {
  const GSHSCENE_Item * item;
  size_t count;              /* the chain length of the item */
  GSHGEOM_Map * maps;        /* the enclosing instances' maps at the push, and room for two more */
} Pat_Clip;

struct Pat_Level {
  GSHSCENE_Pattern pat;
  GSHGEOM_Lattice lat;
  GSHGEOM_Map C;             /* pattern space to the pass's frame of coordinates: T I_1 .. I_k M G, composed once (AD-24) */
  GSHGEOM_Map base;          /* what the tile's items are composed with: the current copy's canonical map (PC3) */
  GSHGEOM_Map base0;         /* the linear part with no translation: what the disposition of a unit is decided under */
  size_t frame;              /* the lattice layer's frame */
  int single;                /* overlapping cells (PC6): one copy at a time, in lattice order */
  int walks;                 /* ... and how many of those walks the pass has made (CU21: a union push is metered in the first) */
  int64_t ci, cj;
  GSHCPU_Rect urect;         /* the pixels the unit being drawn has accumulated */
  uint32_t * sum8;           /* PC4: the unit's accumulator, a pixel of the region each: the sum of the copies' areas in 1/8192 pixel ... */
  uint8_t * cnt;             /* ... how many copies touched it ... */
  double * acc;              /* ... and the first copy's source, or the sum of the sources weighted by the areas */
  uint8_t * coverage;        /* this level's coverage buffers (the Run's are swapped for them while the pass runs) */
  uint8_t * rem;             /* PC4: the area the coverage's byte leaves out, in 1/8192 pixel (gshcpu_fold_split), of the copy being drawn */
  uint8_t * cov_aligned;
  uint8_t * cov_clip;
  uint8_t * cov_result;
  uint8_t * tmp;             /* one clip's coverage in a copy, folded into the unit's */
  Pat_Clip * pclips;         /* the clips pushed in the tile, by their place in the clip stack */
  GSHGEOM_Map * pmaps;       /* their maps, `levels + 2` for each */
  uint8_t * saved_coverage;
  uint8_t * saved_aligned;
  uint8_t * saved_clip;
  uint8_t * saved_result;
  uint64_t copies;           /* copies drawn by the pass (the counter) */
  size_t arena_mark;         /* CALLER_MEMORY: the arena's use when the pass began: the item painted with the pattern keeps what is below it */
  /* PC9: the period tier.  The lattice layer holds one window of the lattice picture, [wx, wx + p) x [wy, wy + s) of the pass's frame, and
   * a pixel q of the frame has the value of the window pixel congruent to it modulo the lattice of whole-pixel steps. */
  int period;                /* the pass is the period tier's (until it is given up) */
  GSHGEOM_Lattice_Period per;
  int64_t wx, wy;            /* the window's corner */
  int64_t tx, ty;            /* the whole pixels of copy (0, 0): the class of q, for the cache, is rep(q - t) */
  GSHGEOM_Map canon0;        /* copy (0, 0)'s canonical map: the linear part and the fraction every copy of the lattice shares */
  int guard;                 /* the window pass is running: a copy the window cannot vouch for sets abort_tier */
  int abort_tier;
  GSHCPU_Rect cur_qbox;      /* the pixels the current copy's cell touches, in its canonical frame */
};

/** The Run's size rounded to the carve alignment: where the mask area starts when the list has one. */
#define RUN_BYTES ((sizeof(Run) + (CARVE_ALIGN - 1u)) & ~(size_t)(CARVE_ALIGN - 1u))

/** The Mask_State of a render whose list holds a masked group (run->masked). */
static Mask_State * ms_of(const Run * run) {
  return (Mask_State *)(void *)((unsigned char *)(void *)(Run *)run + RUN_BYTES);
}

/** The Gen_State of a render whose list holds a general layer (run->general): directly after the Frame array. */
static Gen_State * gs_of(const Run * run) {
  return (Gen_State *)(void *)((unsigned char *)(void *)run->frames + (run->layout.groups + 1u) * sizeof(Frame));
}

/** The pass state of a render whose list holds a pattern (run->masked: the mask area holds it). */
static Pat_Run * pr(const Run * run) {
  return &ms_of(run)->P;
}

/** The pass in progress, or NULL (and NULL where the list has no mask area to hold one). */
static inline Pat_Level * cur_pat(const Run * run) {
  return run->masked ? pr(run)->pat : NULL;
}

/** Whether the painter is not to widen the frame (a pass accumulates and widens where it applies). */
static inline int quiet_of(const Run * run) {
  return run->masked && pr(run)->quiet;
}

/** The record of @p f, one of the run's frames. */
static Gen_Frame * gen_of(const Run * run, const Frame * f) {
  return &gs_of(run)->frames[f - run->frames];
}

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
 * What drawing a picture needs of the layout (MC5): the most groups and clips open at once inside it, its gradient fills and
 * their stops, what kinds of item it holds, and the most walk memory the pictures of the masks inside it take.
 */
typedef struct Need {
  size_t groups, clips, gradients, stops, walk;
  size_t ag, fg;             /* KC8: the most non-isolated groups, and groups with a knockout proper ancestor, open at once in it */
  size_t pdepth, pmaps;      /* PC5: the deepest nest of patterns in it, and the most chain slots the tiles of the nest take */
  int aligned, strokes, other, wide, ok, general;
} Need;

/**
 * The needs of the pictures of masks already found, by the picture's id (MC5): a picture shared by many masks is counted once,
 * however many pictures there are.  The table grows from the sink's allocator, or from the caller's workspace under
 * `CALLER_MEMORY`, which allocates nothing; one that cannot grow stops remembering, and a picture past it is counted again.
 */
typedef GSHCPU_Memo Nest_Memo;
typedef char need_fits_the_memo[sizeof(Need) <= GSHCPU_MEMO_VALUE_MAX ? 1 : -1];

/** One counting walk: the memo it shares with the walks of the masks' pictures, and what it finds of masks. */
typedef struct Nest {
  Nest_Memo * memo;
  GSHCPU_Stroke_Cache * cache; /* the counters */
  size_t walk;               /* the most walk memory the pictures of masks, one in another, take at one pop */
  int masked;                /* a group in the list has a mask */
  size_t ko;                 /* the knockout groups open now in this walk: a mask's picture starts again at none (MK2) */
  size_t depth;              /* the groups open now in this walk */
  GSHCPU_Bits * bits;        /* the open groups that are non-isolated or knockout, as (depth << 2) | flags, to undo at their pop */
  int oom;                   /* the stack of open groups found no room */
  int patterned;             /* a fill or stroke in the list is painted with a pattern */
  size_t pdepth_max;         /* the deepest nest of patterns met */
  size_t pmaps_max;          /* the most chain slots the tiles of one nest take */
} Nest;

/** @p a plus @p b, saturating at SIZE_MAX. */
static size_t sat_add(size_t a, size_t b) {
  size_t r;
  return gcu_safe_add_size(a, b, &r) ? r : SIZE_MAX;
}

/**
 * HX13: a hatch fill is drawn as the stroke of its trimmed, dashed lines, by the stroke's own machinery.  @p out is the synthetic stroke item:
 * the region as the centerline, the hatch resource in the style slot (which gshcpu_render_stroke_open_lines reads), the fill's rule, and as
 * the paint the hatch's ink with its alpha times the hatch paint's multiplier (HX3).  @return 1 when @p item is a hatch fill.
 */
static int hatch_fill_item(const GSHSCENE_Item * item, GSHSCENE_Item * out) {
  GSHSCENE_Hatch h;
  if (item->kind != GSHSCENE_ITEM_FILL || item->paint.kind != GSHSCENE_PAINT_HATCH || !item->paint.resource ||
      gshscene_resource_hatch(item->paint.resource, &h) != GSHSCENE_OK) {
    return 0;
  }
  *out = *item;
  out->kind = GSHSCENE_ITEM_STROKE;
  out->style = item->paint.resource;
  out->paint = h.ink;
  out->paint.rgba[3] = h.ink.rgba[3] * item->paint.rgba[3];
  return 1;
}

/**
 * The most groups and clips open at once in @p list's expansion, a picture's counting where it is drawn; and its
 * gradient fills and their stops added to @p gradients and @p stops (0 when a sum would overflow).
 */
static int out_of_range(const GSHSCENE_Paint * paint) {
  return paint->kind == GSHSCENE_PAINT_SOLID &&
      !(paint->rgba[0] >= 0.0 && paint->rgba[0] <= 1.0 && paint->rgba[1] >= 0.0 && paint->rgba[1] <= 1.0 &&
          paint->rgba[2] >= 0.0 && paint->rgba[2] <= 1.0);
}

static int nesting(const GSHSCENE_List * list, size_t * groups, size_t * clips, size_t * gradients, size_t * stops,
    int * aligned, int * strokes, int * other, int * wide, int * general, size_t * ag, size_t * fg, Nest * nest);

/** The needs of the picture of @p mask (MC5), drawn into its frame at a pop: found once for a picture, however many masks name it
 * and however many pictures there are, and counted again only where the memo had no room; `mask_pictures` counts the walks. */
static int mask_need(const GSHSCENE_Resource * mask, Need * out, Nest * nest) {
  GSHSCENE_Mask m;
  memset(out, 0, sizeof *out);
  if (gshscene_resource_mask(mask, &m) != GSHSCENE_OK) {
    return 0;
  }
  const uint64_t id = gshscene_resource_id(m.picture);
  if (gshcpu_memo_find(nest->memo, id, out)) {
    return out->ok;
  }
  /* The picture is its own isolated, non-knockout group (MK2): the knockout groups around the masked group are not its ancestors. */
  Nest sub = {nest->memo, nest->cache, 0, 0, 0, 0, nest->bits, 0, 0, 0, 0};
  gshcpu_stroke_cache_count(nest->cache, offsetof(GSHCPU_Render_Counters, mask_pictures));
  out->ok = nesting(gshscene_resource_picture(m.picture), &out->groups, &out->clips, &out->gradients, &out->stops, &out->aligned,
      &out->strokes, &out->other, &out->wide, &out->general, &out->ag, &out->fg, &sub);
  nest->oom = nest->oom || sub.oom;
  out->walk = sub.walk;
  out->pdepth = sub.pdepth_max;
  out->pmaps = sub.pmaps_max;
  gshcpu_memo_add(nest->memo, id, out);
  return out->ok;
}

/**
 * The needs of the tile of @p pattern (PC5), drawn into a lattice layer by a pass: found once for a tile, however many patterns name it
 * (the memo is the masks'; a picture that is both a tile and a mask's picture has the one answer).
 */
static int pattern_need(const GSHSCENE_Resource * pattern, Need * out, Nest * nest) {
  GSHSCENE_Pattern pt;
  memset(out, 0, sizeof *out);
  if (gshscene_resource_pattern(pattern, &pt) != GSHSCENE_OK) {
    return 0;
  }
  const uint64_t id = gshscene_resource_id(pt.tile);
  if (gshcpu_memo_find(nest->memo, id, out)) {
    return out->ok;
  }
  Nest sub = {nest->memo, nest->cache, 0, 0, 0, 0, nest->bits, 0, 0, 0, 0};
  out->ok = nesting(gshscene_resource_picture(pt.tile), &out->groups, &out->clips, &out->gradients, &out->stops, &out->aligned,
      &out->strokes, &out->other, &out->wide, &out->general, &out->ag, &out->fg, &sub);
  nest->oom = nest->oom || sub.oom;
  out->walk = sub.walk;
  out->pdepth = sub.pdepth_max;
  out->pmaps = sub.pmaps_max;
  gshcpu_memo_add(nest->memo, id, out);
  return out->ok;
}

/** `nesting` of @p list, with a memo whose room is the sink's allocator's, or the request's workspace under CALLER_MEMORY. */
static int counted_nesting(const Cpu_Sink * cs, const GSHSCENE_List * list, Layout * L, Gen_Layout * G, Nest * nest,
    const GSHSCENE_Render_Request * request) {
  Nest_Memo memo;
  gshcpu_memo_open(&memo, sizeof(Need), (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) ? NULL : &cs->alloc, request->workspace, request->workspace_bytes);
  GSHCPU_Bits bits;
  gshcpu_bits_open(&bits, &memo);
  nest->memo = &memo;
  nest->bits = &bits;
  int general = 0;
  const int ok = nesting(list, &L->groups, &L->clips, &L->gradients, &L->stops, &L->aligned, &L->strokes, &L->other, &L->wide, &general,
      &G->ag_slots, &G->fg_slots, nest);
  G->on = general;
  gshcpu_bits_close(&bits);
  gshcpu_memo_close(&memo);
  return ok;
}

static int nesting(const GSHSCENE_List * list, size_t * groups, size_t * clips, size_t * gradients, size_t * stops,
    int * aligned, int * strokes, int * other, int * wide, int * general, size_t * ag, size_t * fg, Nest * nest) {
  size_t g = 0;
  size_t c = 0;
  size_t most_g = 0;
  size_t most_c = 0;
  size_t open_ag = 0;        /* KC8: the non-isolated groups open now, and the groups with a knockout proper ancestor */
  size_t open_fg = 0;
  size_t most_ag = 0;
  size_t most_fg = 0;
  const size_t base = nest->bits->len;
  int ok = 1;
  *general = 0;
  size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      break;
    }
    GSHSCENE_Gradient gradient;
    if ((item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
        item->paint.kind == GSHSCENE_PAINT_GRADIENT &&
        gshscene_resource_gradient(item->paint.resource, &gradient) == GSHSCENE_OK) {
      ok = ok && gcu_safe_add_size(*gradients, 1u, gradients) && gcu_safe_add_size(*stops, gradient.stop_count, stops);
      for (size_t k = 0; k < gradient.stop_count && gradient.stops; k++) {
        *wide = *wide || out_of_range(&gradient.stops[k].color);
      }
    }
    if (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) {
      *wide = *wide || out_of_range(&item->paint);
      /* An image's value is any filtered texel taken to the layer's space: a float texel, or a gamut conversion, leaves [0, 1], and
       * the byte layers' compositor has no source that is not one color; an image is composited into double layers. */
      *wide = *wide || item->paint.kind == GSHSCENE_PAINT_IMAGE;
    }
    GSHSCENE_Stroke_Style style;
    if (item->kind == GSHSCENE_ITEM_FILL ||
        (item->kind == GSHSCENE_ITEM_STROKE && (item->paint.kind == GSHSCENE_PAINT_GRADIENT || item->paint.kind == GSHSCENE_PAINT_IMAGE))) {
      *other = 1; /* a drawing the 8-bit compositor of the fast coverage does not make: fills, gradients, images */
    }
    if ((item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) && item->paint.kind == GSHSCENE_PAINT_PATTERN) {
      /* PC5: the pass draws the tile into one more layer, whose units are the tile's: its groups and clips open with it, its gradients
       * and strokes.  The layers are doubles (the unit's accumulator is, and a lattice layer is a layer of the tile's colors). */
      Need pn;
      GSHSCENE_Pattern pt;
      ok = pattern_need(item->paint.resource, &pn, nest) && ok;
      nest->patterned = 1;
      *wide = 1;
      *other = 1;
      if (gshscene_resource_pattern(item->paint.resource, &pt) == GSHSCENE_OK) {
        const size_t bytes = gshscene_expansion_bytes(gshscene_resource_picture(pt.tile));
        const size_t here = sat_add(bytes, pn.walk);
        nest->walk = here > nest->walk ? here : nest->walk;
        const size_t slots = sat_add(sat_add(bytes / (sizeof(const GSHSCENE_List *) + sizeof(size_t)), 2u), pn.pmaps);
        nest->pmaps_max = slots > nest->pmaps_max ? slots : nest->pmaps_max;
        nest->pdepth_max = sat_add(1u, pn.pdepth) > nest->pdepth_max ? sat_add(1u, pn.pdepth) : nest->pdepth_max;
      }
      most_g = sat_add(sat_add(g, 1u), pn.groups) > most_g ? sat_add(sat_add(g, 1u), pn.groups) : most_g;
      most_c = sat_add(c, pn.clips) > most_c ? sat_add(c, pn.clips) : most_c;
      most_ag = sat_add(open_ag, pn.ag) > most_ag ? sat_add(open_ag, pn.ag) : most_ag;
      most_fg = sat_add(open_fg, pn.fg) > most_fg ? sat_add(open_fg, pn.fg) : most_fg;
      *general = *general || pn.general;
      ok = ok && gcu_safe_add_size(*gradients, pn.gradients, gradients) && gcu_safe_add_size(*stops, pn.stops, stops);
      *aligned = *aligned || pn.aligned;
      *strokes = *strokes || pn.strokes;
      *wide = *wide || pn.wide;
    }
    if (item->kind == GSHSCENE_ITEM_STROKE) {
      *strokes = 1; /* UC9: the exact step's counts and pool */
    }
    if (item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_HATCH) {
      GSHSCENE_Item hatched;
      *strokes = 1; /* HX13: drawn as a stroke, so the exact step's counts and pool too */
      if (hatch_fill_item(item, &hatched)) {
        *wide = *wide || out_of_range(&hatched.paint);
      }
    }
    if (item->kind == GSHSCENE_ITEM_STROKE && gshscene_resource_stroke_style(item->style, &style) == GSHSCENE_OK &&
        style.alignment != (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER) {
      *aligned = 1; /* SC5: the parts and their composition need arrays of their own */
    }
    switch (item->kind) {
      case GSHSCENE_ITEM_GROUP_PUSH: {
        g++;
        /* KC3: a non-isolated group carries a group alpha; a group with a knockout proper ancestor carries a group shape.  A group
         * that is either is one entry on a stack, read back at its pop; a plain group takes none, so a list without them takes
         * no room for the stack. */
        const int ni = item->group.isolation == (uint32_t)GSHSCENE_ISOLATION_NON_ISOLATED;
        const int ko = item->group.knockout != 0;
        nest->depth++;
        if ((ni || ko) && !gshcpu_bits_push(nest->bits, ((uint64_t)nest->depth << 2) | (ni ? 1u : 0u) | (ko ? 2u : 0u))) {
          nest->oom = 1;
          return 0;
        }
        *general = *general || ni || ko;
        if (ni) {
          open_ag++;
        }
        if (nest->ko > 0) {
          open_fg++;
        }
        if (ko) {
          nest->ko++;
        }
        most_ag = open_ag > most_ag ? open_ag : most_ag;
        most_fg = open_fg > most_fg ? open_fg : most_fg;
        if (item->group.soft_mask) {
          /* MC5: at this group's pop its mask is drawn into one more layer, and everything the picture needs while it is drawn
           * is open with it: the groups and clips of the picture (a masked group inside it, one more again), its gradients
           * and strokes.  The layers are doubles: a mask is not a byte layer (MC8, the tiers' note). */
          Need pn;
          ok = mask_need(item->group.soft_mask, &pn, nest) && ok;
          nest->masked = 1;
          *wide = 1;
          most_g = sat_add(sat_add(g, 1u), pn.groups) > most_g ? sat_add(sat_add(g, 1u), pn.groups) : most_g;
          most_c = sat_add(c, pn.clips) > most_c ? sat_add(c, pn.clips) : most_c;
          most_ag = sat_add(open_ag, pn.ag) > most_ag ? sat_add(open_ag, pn.ag) : most_ag;
          most_fg = sat_add(open_fg, pn.fg) > most_fg ? sat_add(open_fg, pn.fg) : most_fg;
          nest->pdepth_max = pn.pdepth > nest->pdepth_max ? pn.pdepth : nest->pdepth_max;
          nest->pmaps_max = pn.pmaps > nest->pmaps_max ? pn.pmaps : nest->pmaps_max;
          nest->patterned = nest->patterned || pn.pdepth > 0;
          *general = *general || pn.general;
          ok = ok && gcu_safe_add_size(*gradients, pn.gradients, gradients) && gcu_safe_add_size(*stops, pn.stops, stops);
          *aligned = *aligned || pn.aligned;
          *strokes = *strokes || pn.strokes;
          *other = *other || pn.other;
          *wide = *wide || pn.wide;
          GSHSCENE_Mask m;
          if (gshscene_resource_mask(item->group.soft_mask, &m) == GSHSCENE_OK) {
            const size_t here = sat_add(gshscene_expansion_bytes(gshscene_resource_picture(m.picture)), pn.walk);
            nest->walk = here > nest->walk ? here : nest->walk;
          }
        }
        break;
      }
      case GSHSCENE_ITEM_GROUP_POP:
        g--;
        if (nest->bits->len > base && (gshcpu_bits_top(nest->bits) >> 2) == nest->depth) {
          const uint64_t bits = gshcpu_bits_pop(nest->bits);
          if (bits & 2u) {
            nest->ko--;
          }
          if (bits & 1u) {
            open_ag--;
          }
        }
        if (nest->ko > 0) {
          open_fg--; /* it carried a group shape exactly when a knockout group was open around it, which is as many as now */
        }
        nest->depth--;
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
        size_t iag = 0;
        size_t ifg = 0;
        int igeneral = 0;
        /* The instance's items are drawn into this layer, its groups inside the groups open here (and under their knockout ones). */
        ok = nesting(gshscene_resource_picture(item->resource), &ig, &ic, gradients, stops, aligned, strokes, other, wide, &igeneral,
                 &iag, &ifg, nest) && ok;
        if (nest->oom) {
          return 0;
        }
        *general = *general || igeneral;
        most_g = g + ig > most_g ? g + ig : most_g;
        most_c = c + ic > most_c ? c + ic : most_c;
        most_ag = open_ag + iag > most_ag ? open_ag + iag : most_ag;
        most_fg = open_fg + ifg > most_fg ? open_fg + ifg : most_fg;
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
  *ag = most_ag;
  *fg = most_fg;
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

/** Where the parts of one pass level's memory lie (PC11): the record, the unit's accumulator, and the coverage buffers. */
typedef struct Pat_Offsets {
  size_t at_sum8, at_cnt, at_acc, at_coverage, at_rem, at_aligned, at_clip, at_result, at_tmp, at_pclips, at_pmaps, bytes;
} Pat_Offsets;

/** The sizes of one level's memory for @p pixels pixels; 0 on overflow. */
static int pat_offsets(size_t pixels, int aligned, size_t clips, size_t levels, Pat_Offsets * o) {
  size_t at = 0;
  size_t ignored;
  const int ok = place(&at, sizeof(Pat_Level), &ignored) && place(&at, mul3(pixels, sizeof(uint32_t), 1), &o->at_sum8) &&
      place(&at, pixels, &o->at_cnt) && place(&at, mul3(pixels, 4u, sizeof(double)), &o->at_acc) &&
      place(&at, pixels, &o->at_coverage) && place(&at, pixels, &o->at_rem) && place(&at, aligned ? pixels : 0, &o->at_aligned) &&
      place(&at, aligned ? pixels : 0, &o->at_clip) && place(&at, aligned ? pixels : 0, &o->at_result) &&
      place(&at, pixels, &o->at_tmp) && place(&at, mul3(clips, sizeof(Pat_Clip), 1), &o->at_pclips) &&
      place(&at, mul3(clips, sat_add(levels, 2u), sizeof(GSHGEOM_Map)), &o->at_pmaps);
  o->bytes = at;
  return ok;
}

/**
 * Lay out a render of @p list on @p target; ::GSHCPU_ERR_LIMIT past a limit or when a size overflows.  @p M gets the mask area
 * (MC5) when a group in the list, or in a picture it draws, has a mask (MC9: none, as before masks).  Under CALLER_MEMORY the
 * count of the mask pictures takes its memo's room from @p request's workspace, where nothing else lives yet (it allocates nothing).
 */
static GSHCPU_Result layout(const Cpu_Sink * cs, const GSHSCENE_List * list, const GSHSCENE_Target * target, Layout * L,
    Mask_Layout * M, Gen_Layout * G, const GSHSCENE_Render_Request * request) {
  memset(L, 0, sizeof *L);
  memset(M, 0, sizeof *M);
  memset(G, 0, sizeof *G);
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
  /* The count of the mask pictures (MC5) shares its memo with the walks of the pictures: each picture is walked once, however
   * many pictures there are. */
  Nest nest = {NULL, cs->cache, 0, 0, 0, 0, NULL, 0, 0, 0, 0};
  if (!counted_nesting(cs, list, L, G, &nest, request)) {
    /* The stack of open groups found no room: an allocator that failed, or a workspace too short to hold it (nothing is allocated). */
    return nest.oom && !(request->flags & GSHSCENE_RENDER_CALLER_MEMORY) ? GSHCPU_ERR_OOM : GSHCPU_ERR_LIMIT;
  }
  /* PC5: the tiles of the patterns are walked by passes, with the machinery of a mask's picture (its walk, its window); each pass's
   * units take chain slots after the use's. */
  L->levels = sat_add(L->levels, nest.pmaps_max);
  M->pat_levels = nest.pdepth_max;
  if (nest.patterned) {
    nest.masked = 1;
    Pat_Offsets po;
    if (!pat_offsets(L->pixels, L->aligned, L->clips, L->levels, &po)) {
      return GSHCPU_ERR_LIMIT;
    }
    M->pat_stride = po.bytes;
  }
  if (cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) {
    G->on = 1; /* KC9's gate: every layer, the root's too, takes the element step */
  }
  if (G->on) {
    /* KC8: a record for each frame, and a channel for each non-isolated group open at once and each group with a knockout ancestor.
     * No byte layer can hold a general layer's channels, and a list that holds one has none (the layers are doubles, as a masked list's). */
    L->wide = 1;
    G->records = align_up(sizeof(Gen_State));
    if (G->records == SIZE_MAX || !gcu_safe_add_size(G->records, mul3(L->groups + 1u, sizeof(Gen_Frame), 1), &G->records)) {
      return GSHCPU_ERR_LIMIT;
    }
  } else {
    G->ag_slots = 0;
    G->fg_slots = 0;
  }
  if (nest.masked) {
    /* MC5: the area after the Run: the state, a frame record for each layer, the chain of a mask's item, and the walks of the
     * pictures of the masks being drawn, one on another. */
    size_t at = 0;
    size_t state;
    M->walk = nest.walk;
    if (!place(&at, sizeof(Mask_State), &state) || !place(&at, mul3(L->groups + 1u, sizeof(Mask_Frame), 1), &M->at_frames) ||
        !place(&at, mul3(L->levels + 1u, sizeof(size_t), 1), &M->at_chain) || !place(&at, M->walk, &M->at_walk)) {
      return GSHCPU_ERR_LIMIT;
    }
    M->bytes = at;
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
    L->acc_exact = L->acc_bytes;
    if ((cs->fast || cs->fast_fill) && L->acc_bytes != SIZE_MAX) {
      /* The lean cover walks the edges once for every band of rows its scratch holds: room for as many rows as the window has. */
      const size_t lean = gshcpu_lean_scratch(L->width, L->height);
      L->acc_bytes = lean > L->acc_bytes ? lean : L->acc_bytes;
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
  int ok = place(&at, sizeof(Run), &ignored) && place(&at, M->bytes, &M->at) && place(&at, L->walk, &L->at_walk) &&
      place(&at, mul3(L->levels + 2u, sizeof(GSHGEOM_Map), 1), &L->at_maps) &&
      place(&at, mul3(L->levels, sizeof(size_t), 1), &L->at_stop) &&
      place(&at, sat_add(mul3(L->groups + 1u, sizeof(Frame), 1), G->records), &L->at_frames) &&
      place(&at, mul3(L->clips, sizeof(Clip), 1), &L->at_clips) &&
      place(&at, mul3(L->pixels, 4u, sizeof(double)), &L->at_root) &&
      place(&at, mul3(L->groups, L->pixels, 4u * sizeof(double)), &L->at_layers) &&
      place(&at, mul3(G->ag_slots, L->pixels, sizeof(double)), &G->at_ag) &&
      place(&at, mul3(G->fg_slots, L->pixels, sizeof(double)), &G->at_fg) &&
      place(&at, L->pixels, &L->at_touched) && place(&at, L->pixels, &L->at_coverage) &&
      place(&at, L->aligned ? L->pixels : 0, &L->at_cov_aligned) && place(&at, L->aligned ? L->pixels : 0, &L->at_cov_clip) &&
      place(&at, L->aligned ? L->pixels : 0, &L->at_cov_result) &&
      place(&at, mul3(L->clips, L->pixels, 1), &L->at_clip_memory) &&
      place(&at, mul3(M->pat_levels, M->pat_stride, 1), &M->at_pat) &&
      place(&at, L->acc_bytes, &L->at_acc) &&
      place(&at, L->exact_bytes, &L->at_exact) &&
      place(&at, mul3(L->table, sizeof(Resolved), 1), &L->at_table) &&
      place(&at, mul3(L->stops, sizeof(GSHCPU_Stop), 1), &L->at_pool) &&
      place(&at, L->gradients ? TABLE_SLOTS * (GSHCPU_RENDER_GRADIENT_TABLE + 1u) * 4u * sizeof(double) : 0, &L->at_tables);
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

/** floor and ceiling of a 26.6 coordinate in whole pixels. */
static int64_t box_floor(int32_t v) {
  return v >= 0 ? (int64_t)v / 64 : -((-(int64_t)v + 63) / 64); /* widened before the negation: INT32_MIN is a coordinate */
}
static int64_t box_ceil(int32_t v) {
  return v >= 0 ? ((int64_t)v + 63) / 64 : -((-(int64_t)v) / 64);
}

/* --- the general tier of a union clip (story A3b, CG1 to CG12) ------------------------------------------------------------- */

#define UNION_NONE 0xffffu

/**
 * What the general tier derives from the members' device boxes: the pixels of each box, the groups (CG2: the connected components of
 * "the interiors of the boxes meet", each named by its least member, the members of a group a chain through `next` from `head`), and the
 * sizes the memory of the tier depends on.  The arrays are one block of the allocator the plan was made with.
 */
typedef struct Union_Plan {
  size_t n;
  GSHCPU_Rect * rect;         /* n: the pixels of each box, empty for an empty box */
  GSHGEOM_Device_Box * box;   /* n */
  uint16_t * root;            /* n: the least member of the member's group */
  uint16_t * next;            /* n: the next member of the group (ascending), or UNION_NONE */
  uint16_t * head;            /* n: for a root, the first member of its group (itself) */
  GSHCPU_Rect clip;           /* the pixels of the union met with the clips in force: what the clip's coverage holds */
  GSHCPU_Rect wide;           /* ... and in the whole render: where the tier works (CG9) */
  size_t multi;               /* the groups of two or more members */
  size_t max_parts;           /* the most members of a group */
  size_t max_width;           /* the widest rectangle of a group of several, in pixels */
  size_t max_pixels;          /* the most pixels of one */
} Union_Plan;

/**
 * CU16's relation on two device boxes: the interiors meet.  A box without width or height meets nothing, and an empty box is all zeros
 * (shape-geometry's device.h), so it has neither.
 */
static int boxes_meet(const GSHGEOM_Device_Box * a, const GSHGEOM_Device_Box * b) {
  const int32_t x0 = a->x0 > b->x0 ? a->x0 : b->x0;
  const int32_t y0 = a->y0 > b->y0 ? a->y0 : b->y0;
  const int32_t x1 = a->x1 < b->x1 ? a->x1 : b->x1;
  const int32_t y1 = a->y1 < b->y1 ? a->y1 : b->y1;
  return x0 < x1 && y0 < y1;
}

static uint16_t union_find(uint16_t * root, uint16_t i) {
  while (root[i] != i) {
    root[i] = root[root[i]];
    i = root[i];
  }
  return i;
}

/** The pixels of group @p r met with @p wide. */
static GSHCPU_Rect union_group_rect(const Union_Plan * pl, uint16_t r, GSHCPU_Rect wide) {
  GSHCPU_Rect g = {0, 0, 0, 0}; /* join ignores an empty rectangle: the group's is the join of its members' boxes */
  for (uint16_t i = pl->head[r]; i != UNION_NONE; i = pl->next[i]) {
    g = gshcpu_render_rect_join(g, pl->rect[i]);
  }
  g = gshcpu_render_rect_meet(g, wide);
  if (gshcpu_render_rect_empty(g)) {
    g.x1 = g.x0;
    g.y1 = g.y0;
  }
  return g;
}

/**
 * The plan of a union push (CG2): the boxes of the members from the one function the predicate uses (CU14), their pixel rectangles, the
 * groups, and the sizes.  With @p one_group the members are one group (the test switch).  One block from @p al, in `*block`.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_LIMIT (a member whose box cannot be formed: the push is the sink's resource limit),
 * ::GSHSCENE_ERR_OOM or ::GSHSCENE_ERR_INTERNAL.
 */
static GSHSCENE_Result union_plan(const GSHSCENE_Item * item, const GSHGEOM_Map * maps, size_t nmaps, const GSHGEOM_Map * tmap,
    GSHCPU_Rect reach, GSHCPU_Rect whole, int one_group, const GSHGEOM_Allocator * al, Union_Plan * pl, void ** block) {
  const size_t n = gshscene_resource_clip_union_count(item->resource);
  memset(pl, 0, sizeof *pl);
  *block = NULL;
  pl->n = n;
  const size_t bytes = n * (sizeof(GSHCPU_Rect) + sizeof(GSHGEOM_Device_Box) + 3u * sizeof(uint16_t)) + 64u;
  unsigned char * mem = al->malloc_fn(al->ctx, bytes);
  if (!mem) {
    return GSHSCENE_ERR_OOM;
  }
  *block = mem;
  pl->rect = (GSHCPU_Rect *)(void *)mem;
  pl->box = (GSHGEOM_Device_Box *)(void *)(mem + n * sizeof(GSHCPU_Rect));
  pl->root = (uint16_t *)(void *)(mem + n * (sizeof(GSHCPU_Rect) + sizeof(GSHGEOM_Device_Box)));
  pl->next = pl->root + n;
  pl->head = pl->next + n;
  GSHCPU_Rect bound = {0, 0, 0, 0};
  for (size_t i = 0; i < n; i++) {
    GSHSCENE_Result br = gshscene_clip_union_member_box(item, i, maps, nmaps, tmap, &pl->box[i]);
    if (br == GSHSCENE_ERR_LIMIT) {
      return GSHSCENE_ERR_LIMIT; /* an unprovable member: CU4, a missing member would draw the union wrong */
    }
    if (br != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    GSHCPU_Rect m = {0, 0, 0, 0};
    if (!pl->box[i].empty) {
      m.x0 = box_floor(pl->box[i].x0);
      m.y0 = box_floor(pl->box[i].y0);
      m.x1 = box_ceil(pl->box[i].x1);
      m.y1 = box_ceil(pl->box[i].y1);
      bound = gshcpu_render_rect_join(bound, m); /* join ignores an empty rectangle; no box at all leaves the bound empty, and so the meets */
    }
    pl->rect[i] = m;
  }
  pl->clip = gshcpu_render_rect_meet(bound, reach);
  pl->wide = gshcpu_render_rect_meet(bound, whole);
  if (gshcpu_render_rect_empty(pl->clip)) {
    pl->clip.x1 = pl->clip.x0;
    pl->clip.y1 = pl->clip.y0;
  }
  if (gshcpu_render_rect_empty(pl->wide)) {
    pl->wide.x1 = pl->wide.x0;
    pl->wide.y1 = pl->wide.y0;
  }
  pl->clip = gshcpu_render_rect_meet(pl->clip, pl->wide);
  /* The groups: a member joins the group of every member whose box its box meets; the least member names it. */
  for (size_t i = 0; i < n; i++) {
    pl->root[i] = (uint16_t)i;
  }
  for (size_t i = 0; i < n; i++) {
    for (size_t j = i + 1u; j < n; j++) {
      if (one_group ? 1 : boxes_meet(&pl->box[i], &pl->box[j])) {
        uint16_t ri = union_find(pl->root, (uint16_t)i);
        uint16_t rj = union_find(pl->root, (uint16_t)j);
        pl->root[rj] = ri; /* a group is named by whichever member this makes its root; the order the groups are made in changes nothing (their areas add) */
      }
    }
  }
  for (size_t i = 0; i < n; i++) {
    pl->root[i] = union_find(pl->root, (uint16_t)i);
    pl->head[i] = UNION_NONE;
  }
  for (size_t i = n; i-- > 0;) {
    pl->next[i] = pl->head[pl->root[i]];
    pl->head[pl->root[i]] = (uint16_t)i;
  }
  for (size_t r = 0; r < n; r++) {
    if (pl->root[r] != r || pl->next[r] == UNION_NONE) {
      continue; /* not a root, or a group of one */
    }
    size_t parts = 0;
    for (uint16_t i = pl->head[r]; i != UNION_NONE; i = pl->next[i]) {
      parts++;
    }
    const GSHCPU_Rect g = union_group_rect(pl, (uint16_t)r, pl->wide);
    const size_t width = (size_t)(g.x1 - g.x0);
    const uint64_t pixels64 = (uint64_t)width * (uint64_t)(g.y1 - g.y0);
    if (pixels64 > gshcpu_union_pixels_max) {
      return GSHSCENE_ERR_LIMIT; /* CG9: a record names a pixel of its group by a 32-bit index */
    }
    const size_t pixels = (size_t)pixels64;
    pl->multi++;
    pl->max_parts = parts > pl->max_parts ? parts : pl->max_parts;
    pl->max_width = width > pl->max_width ? width : pl->max_width;
    pl->max_pixels = pixels > pl->max_pixels ? pixels : pl->max_pixels;
  }
  return GSHSCENE_OK;
}

/**
 * The memory of the tier besides the plan (CG8), taken from one allocator in this order by the push and by the measure alike, so that
 * the caller's memory holds exactly what the push takes: the accumulator and the plane over the whole render's rectangle; and, if some
 * group has several members, the group's plane and marks (the most pixels of one), the exact step's memory, the records and pieces of the
 * kept pixels (`max_union_pieces` of each), and the arrays one pixel's step is handed.
 */
typedef struct Union_Blocks {
  int64_t * acc;
  uint16_t * plane;
  uint16_t * gplane;
  uint8_t * mark;
  unsigned char * exact;
  GSHCPU_Group_Record * records;
  GSHCPU_Piece * pieces;
  const GSHCPU_Piece ** cell_pieces;
  size_t * cell_counts;
  int64_t * cell_a_left;
  int64_t * cell_dense;
  uint8_t * cell_rules;
  uint8_t * group_rules;
  size_t piece_cap;
} Union_Blocks;

static void union_free(const GSHGEOM_Allocator * al, Union_Blocks * b) {
  void * all[] = {b->group_rules, b->cell_rules, b->cell_dense, b->cell_a_left, b->cell_counts, (void *)b->cell_pieces, b->pieces,
      b->records, b->exact, b->mark, b->gplane, b->plane, b->acc};
  for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
    if (all[i]) {
      al->free_fn(al->ctx, all[i]);
    }
  }
  memset(b, 0, sizeof *b);
}

/** @return ::GSHSCENE_OK, ::GSHSCENE_ERR_OOM, or ::GSHSCENE_ERR_LIMIT when a size does not fit. */
static GSHSCENE_Result union_alloc(const Cpu_Sink * cs, const Union_Plan * pl, const GSHGEOM_Allocator * al, Union_Blocks * b) {
  memset(b, 0, sizeof *b);
  const size_t width = (size_t)(pl->wide.x1 - pl->wide.x0);
  const size_t pixels = width * (size_t)(pl->wide.y1 - pl->wide.y0);
  size_t acc_bytes;
  size_t plane_bytes;
  if (!gcu_safe_mul_size(2u * (width + 1u), GSHCPU_RENDER_BAND_ROWS * sizeof(int64_t), &acc_bytes) ||
      !gcu_safe_mul_size(pixels ? pixels : 1u, sizeof(uint16_t), &plane_bytes)) {
    return GSHSCENE_ERR_LIMIT;
  }
  b->acc = al->malloc_fn(al->ctx, acc_bytes);
  b->plane = b->acc ? al->calloc_fn(al->ctx, pixels ? pixels : 1u, sizeof(uint16_t)) : NULL;
  if (!b->acc || !b->plane) {
    union_free(al, b);
    return GSHSCENE_ERR_OOM;
  }
  if (pl->multi == 0) {
    return GSHSCENE_OK;
  }
  b->piece_cap = cs->max_union_pieces > (uint64_t)SIZE_MAX / sizeof(GSHCPU_Group_Record) ? 0 : (size_t)cs->max_union_pieces;
  const size_t gpix = pl->max_pixels ? pl->max_pixels : 1u;
  const size_t exact_bytes = gshcpu_exact_group_bytes(pl->max_width, cs->max_exact, pl->max_parts);
  size_t records_bytes;
  size_t pieces_bytes;
  size_t exact_total;
  if (b->piece_cap == 0 || exact_bytes == SIZE_MAX || !gcu_safe_add_size(exact_bytes, 16u, &exact_total) ||
      !gcu_safe_mul_size(b->piece_cap, sizeof(GSHCPU_Group_Record), &records_bytes) ||
      !gcu_safe_mul_size(b->piece_cap, sizeof(GSHCPU_Piece), &pieces_bytes)) {
    union_free(al, b);
    return GSHSCENE_ERR_LIMIT;
  }
  b->gplane = al->malloc_fn(al->ctx, gpix * sizeof(uint16_t));
  b->mark = al->malloc_fn(al->ctx, gpix);
  b->exact = al->malloc_fn(al->ctx, exact_total);
  b->records = al->malloc_fn(al->ctx, records_bytes);
  b->pieces = al->malloc_fn(al->ctx, pieces_bytes);
  b->cell_pieces = al->malloc_fn(al->ctx, pl->max_parts * sizeof *b->cell_pieces);
  b->cell_counts = al->malloc_fn(al->ctx, pl->max_parts * sizeof *b->cell_counts);
  b->cell_a_left = al->malloc_fn(al->ctx, pl->max_parts * sizeof *b->cell_a_left);
  b->cell_dense = al->malloc_fn(al->ctx, pl->max_parts * sizeof *b->cell_dense);
  b->cell_rules = al->malloc_fn(al->ctx, pl->max_parts);
  b->group_rules = al->malloc_fn(al->ctx, pl->max_parts);
  if (!b->gplane || !b->mark || !b->exact || !b->records || !b->pieces || !b->cell_pieces || !b->cell_counts || !b->cell_a_left ||
      !b->cell_dense || !b->cell_rules || !b->group_rules) {
    union_free(al, b);
    return GSHSCENE_ERR_OOM;
  }
  return GSHSCENE_OK;
}


/* --- the geometry of the items, measured (S9) --------------------------------------------------- */

/**
 * What one fill, clip push or stroke takes of the arena (the item's geometry), at chain length @p count with the enclosing
 * instances' maps in @p maps.  Anything else takes none.  ::GSHCPU_ERR_OOM where the arena is out.  A union clip in a pattern's tile
 * (@p in_tile, CU21) is always formed by the general tier, whatever the predicate shows of its members.
 */
static GSHCPU_Result measure_one(const Cpu_Sink * cs, const GSHSCENE_Target * target, const GSHSCENE_Item * item,
    GSHGEOM_Map * maps, size_t count, GSHCPU_Arena * arena, size_t * peak, int in_tile) {
  GSHSCENE_Item hatched;
  const int is_hatch = hatch_fill_item(item, &hatched);
  if (item->kind == GSHSCENE_ITEM_STROKE || is_hatch) {
    /* A stroke that is drawn is expanded once through the arena (no cache under CALLER_MEMORY: SC7); its expansion
     * is what an item's geometry is for a fill, so the caller's memory holds the most any item takes.  A hatch fill is one (HX13). */
    if (is_hatch ? gshscene_paint_notice(item, maps, count - 1, &target->map)
                 : gshscene_stroke_notice(item, maps, count - 1, &target->map)) {
      return GSHCPU_OK; /* a stroke that draws nothing */
    }
    if (is_hatch) {
      item = &hatched;
    }
    GSHGEOM_Map composed;
    maps[count - 1] = item->map;
    if (gshgeom_map_compose_chain(maps, count, &target->map, &composed) != GSHGEOM_OK) {
      return GSHCPU_OK;
    }
    GSHCPU_Stroke st;
    gshcpu_render_arena_reset(arena);
    arena->peak = 0;
    /* A run's centerline, its glyphs' outlines and its notes are allocated here in the order the drawing takes them
     * (ST8), so the memory asked for is exact. */
    GSHCPU_Result sr = gshcpu_render_stroke_open_lines(NULL, &arena->self, &cs->geometry, cs->max_hatch_lines, item, &composed, &st);
    if (sr == GSHCPU_OK) {
      gshcpu_render_stroke_close(NULL, &st);
    }
    if (sr == GSHCPU_ERR_OOM) {
      return sr;
    }
    *peak = arena->peak > *peak ? arena->peak : *peak;
    return GSHCPU_OK;
  }
  if (item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_CLIP_PUSH) {
    return GSHCPU_OK;
  }
  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_PLANE) {
    return GSHCPU_OK; /* a plane has no geometry: it takes nothing from the arena */
  }
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH && gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
    GSHSCENE_Union_Verdict verdict;
    if (in_tile || gshscene_clip_union_disjoint(item, maps, count - 1, &target->map, &verdict) != GSHSCENE_OK ||
        !((cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint)) {
      /* CG8: the general tier's sequence: the plan, the blocks union_alloc takes, then the geometry of the member that takes the most.
       * The rectangles are the most the union can span (the region for the clips, the whole render's for the tier's work). */
      gshcpu_render_arena_reset(arena);
      arena->peak = 0;
      Union_Plan pl;
      void * block = NULL;
      GSHSCENE_Result pr = union_plan(item, maps, count - 1, &target->map, region_of(target), whole_of(target),
          (cs->tiers & GSHCPU_RENDER_FORCE_ONE_GROUP) != 0, &arena->self, &pl, &block);
      if (pr == GSHSCENE_ERR_OOM) {
        return GSHCPU_ERR_OOM;
      }
      if (pr != GSHSCENE_OK) {
        if (block) {
          arena->self.free_fn(arena->self.ctx, block);
        }
        *peak = arena->peak > *peak ? arena->peak : *peak;
        return pr == GSHSCENE_ERR_LIMIT ? GSHCPU_OK : GSHCPU_ERR_INTERNAL; /* an unprovable member: the push is refused and takes no more */
      }
      Union_Blocks blocks;
      pr = union_alloc(cs, &pl, &arena->self, &blocks);
      if (pr == GSHSCENE_ERR_OOM) {
        arena->self.free_fn(arena->self.ctx, block);
        return GSHCPU_ERR_OOM;
      }
      GSHCPU_Result result = GSHCPU_OK;
      if (pr == GSHSCENE_OK) {
        const size_t mark = arena->used;
        for (size_t i = 0; i < gshscene_resource_clip_union_count(item->resource) && result == GSHCPU_OK; i++) {
          GSHSCENE_Item member;
          GSHCPU_Item_Geometry mg;
          if (gshscene_clip_union_member_item(item, i, &member) != GSHSCENE_OK) {
            continue;
          }
          gshcpu_render_arena_rewind(arena, mark);
          GSHCPU_Result mr = gshcpu_render_geometry(&member, maps, count - 1, &target->map, &arena->self, &cs->geometry, &mg);
          gshcpu_render_geometry_release(&mg, &arena->self);
          if (mr == GSHCPU_ERR_OOM) {
            result = mr;
          }
        }
        union_free(&arena->self, &blocks);
      }
      arena->self.free_fn(arena->self.ctx, block);
      *peak = arena->peak > *peak ? arena->peak : *peak;
      return result;
    }
    /* CU20: the plane (a uint16 for each pixel of the region, the most it can span), then the geometry of the member that takes the most:
     * the sequence union_push_fast makes. */
    const GSHCPU_Rect reg = region_of(target);
    const size_t pixels = (size_t)(reg.x1 - reg.x0) * (size_t)(reg.y1 - reg.y0);
    gshcpu_render_arena_reset(arena);
    arena->peak = 0;
    void * plane = arena->self.calloc_fn(arena->self.ctx, pixels ? pixels : 1u, sizeof(uint16_t));
    if (!plane) {
      return GSHCPU_ERR_OOM;
    }
    const size_t mark = arena->used;
    for (size_t i = 0; i < gshscene_resource_clip_union_count(item->resource); i++) {
      GSHSCENE_Item member;
      GSHCPU_Item_Geometry mg;
      if (gshscene_clip_union_member_item(item, i, &member) != GSHSCENE_OK) {
        continue;
      }
      gshcpu_render_arena_rewind(arena, mark);
      GSHCPU_Result mr = gshcpu_render_geometry(&member, maps, count - 1, &target->map, &arena->self, &cs->geometry, &mg);
      gshcpu_render_geometry_release(&mg, &arena->self);
      if (mr == GSHCPU_ERR_OOM) {
        arena->self.free_fn(arena->self.ctx, plane);
        return mr;
      }
    }
    arena->self.free_fn(arena->self.ctx, plane); /* the measure really took it from the sink's allocator */
    *peak = arena->peak > *peak ? arena->peak : *peak;
    return GSHCPU_OK;
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
  return GSHCPU_OK;
}

static GSHCPU_Result measure_tile(const Cpu_Sink * cs, const GSHSCENE_Target * target, unsigned char * area, const Mask_Layout * M,
    const GSHSCENE_Item * use, GSHGEOM_Map * maps, size_t n, GSHCPU_Arena * arena, size_t * peak, size_t used, size_t base);

/** The units of the picture of @p group's mask (MC5), at the chain length @p n of the group, as measure_items measures the list's. */
static GSHCPU_Result measure_mask(const Cpu_Sink * cs, const GSHSCENE_Target * target, unsigned char * area, const Mask_Layout * M,
    const GSHSCENE_Item * group, GSHGEOM_Map * maps, size_t n, GSHCPU_Arena * arena, size_t * peak, size_t used, int in_tile) {
  GSHSCENE_Mask m;
  if (gshscene_resource_mask(group->group.soft_mask, &m) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL;
  }
  const GSHSCENE_List * picture = gshscene_resource_picture(m.picture);
  const size_t bytes = gshscene_expansion_bytes(picture);
  GSHSCENE_Expansion walk;
  if (used > M->walk || bytes > M->walk - used ||
      gshscene_expansion_init(&walk, picture, area + M->at_walk + used, bytes) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL;
  }
  maps[n - 1] = group->group.soft_mask_map; /* MK6: the chain of the mask's items ends in the group's placement */
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t m_count;
  while (gshscene_expansion_next(&walk, &item, &chain, &m_count)) {
    (void)chain;
    const size_t count = n + m_count;
    GSHCPU_Result r;
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      maps[count - 1] = item->map;
      continue;
    }
    if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {
      r = measure_mask(cs, target, area, M, item, maps, count, arena, peak, used + bytes, in_tile);
    } else {
      size_t outer = 0;
      r = measure_one(cs, target, item, maps, count, arena, &outer, in_tile);
      *peak = outer > *peak ? outer : *peak;
      if (r == GSHCPU_OK && (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
          item->paint.kind == GSHSCENE_PAINT_PATTERN) {
        r = measure_tile(cs, target, area, M, item, maps, count, arena, peak, used + bytes, outer);
      }
    }
    if (r != GSHCPU_OK) {
      return r;
    }
  }
  return GSHCPU_OK;
}

/**
 * The units of the tile of the pattern that paints @p use (PC5, PC11), at the chain length @p n of the use: each is measured under
 * the use's composed map without its translation, as the pass draws it, and the memory the pass takes of the arena for one copy of
 * a unit is bounded by the unit's geometry, the clipped copy of it (CK5: at most about five times the points) and the quadrilateral:
 * eight times the geometry and a page.  An over-estimate is a need that is never too small.
 */
static GSHCPU_Result measure_tile(const Cpu_Sink * cs, const GSHSCENE_Target * target, unsigned char * area, const Mask_Layout * M,
    const GSHSCENE_Item * use, GSHGEOM_Map * maps, size_t n, GSHCPU_Arena * arena, size_t * peak, size_t used, size_t base) {
  GSHSCENE_Pattern pt;
  if (gshscene_resource_pattern(use->paint.resource, &pt) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL;
  }
  maps[n - 1] = use->map;
  maps[n] = pt.map;
  GSHGEOM_Map composed;
  if (gshgeom_map_compose_chain(maps, n + 1, &target->map, &composed) != GSHGEOM_OK) {
    return GSHCPU_OK; /* a use that is refused draws no tile */
  }
  composed.t[0] = 0.0;
  composed.t[1] = 0.0;
  composed.t[2] = 0.0;
  GSHSCENE_Target inner = *target;
  inner.map = composed;
  const GSHSCENE_List * tile = gshscene_resource_picture(pt.tile);
  const size_t bytes = gshscene_expansion_bytes(tile);
  GSHSCENE_Expansion walk;
  if (used > M->walk || bytes > M->walk - used ||
      gshscene_expansion_init(&walk, tile, area + M->at_walk + used, bytes) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL;
  }
  GSHGEOM_Map * tmaps = maps + n + 1u;
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t m_count;
  while (gshscene_expansion_next(&walk, &item, &chain, &m_count)) {
    (void)chain;
    GSHCPU_Result r;
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      tmaps[m_count - 1] = item->map;
      continue;
    }
    if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {
      /* The items of a mask's picture in a tile are drawn in each copy, cut to its cell, like the tile's own: the same rule (CU20, CU21). */
      size_t local = 0;
      r = measure_mask(cs, &inner, area, M, item, tmaps, m_count, arena, &local, used + bytes, 1);
      if (r == GSHCPU_OK && local > 0) {
        size_t scaled;
        local = gcu_safe_mul_size(local, 8u, &scaled) ? scaled : SIZE_MAX;
        local = sat_add(sat_add(local, 4096u), base);
        *peak = local > *peak ? local : *peak;
      }
    } else {
      size_t local = 0;
      r = measure_one(cs, &inner, item, tmaps, m_count, arena, &local, 1);
      if (r == GSHCPU_OK && local > 0) {
        size_t scaled;
        local = gcu_safe_mul_size(local, 8u, &scaled) ? scaled : SIZE_MAX;
        local = sat_add(sat_add(local, 4096u), base);
        *peak = local > *peak ? local : *peak;
      }
      if (r == GSHCPU_OK && (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
          item->paint.kind == GSHSCENE_PAINT_PATTERN) {
        r = measure_tile(cs, &inner, area, M, item, tmaps, m_count, arena, peak, used + bytes, sat_add(base, local));
      }
    }
    if (r != GSHCPU_OK) {
      return r;
    }
  }
  return GSHCPU_OK;
}

/**
 * Convert every fill and clip push of the expansion of @p list once, through @p arena (reset for each), and give
 * the most one took, the items of the pictures of masks too (MC5).  @p memory holds the area of the masks, the walk and the maps
 * (at the layout's offsets from it).
 */
static GSHCPU_Result measure_items(const Cpu_Sink * cs, const GSHSCENE_List * list, const GSHSCENE_Target * target,
    const Layout * L, const Mask_Layout * M, unsigned char * memory, GSHCPU_Arena * arena, size_t * peak) {
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
    GSHCPU_Result r;
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      maps[count - 1] = item->map;
      continue;
    }
    if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {
      r = measure_mask(cs, target, memory + M->at, M, item, maps, count, arena, peak, 0, 0);
    } else {
      size_t outer = 0;
      r = measure_one(cs, target, item, maps, count, arena, &outer, 0);
      *peak = outer > *peak ? outer : *peak;
      if (r == GSHCPU_OK && (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
          item->paint.kind == GSHSCENE_PAINT_PATTERN) {
        r = measure_tile(cs, target, memory + M->at, M, item, maps, count, arena, peak, 0, outer);
      }
    }
    if (r != GSHCPU_OK) {
      return r;
    }
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
  Mask_Layout M;
  Gen_Layout G;
  GSHCPU_Result r = layout(cs, request->list, request->target, &L, &M, &G, request);
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
    r = measure_items(cs, request->list, request->target, &L, &M, base, &arena, &peak);
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
  r = measure_items(cs, request->list, request->target, &L, &M, base, &arena, &peak);
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
      !gcu_safe_add_u64(b, cs->max_deposits, &b) || !gcu_safe_add_u64(b, pixels, &b) || !gcu_safe_add_u64(b, expansion, &b)) {
    return UINT64_MAX;
  }
  uint64_t trim;
  if (!gcu_safe_mul_u64(cs->max_hatch_lines, (uint64_t)cs->elements, &trim) || !gcu_safe_add_u64(b, trim, &b)) {
    return UINT64_MAX; /* HX12: the lines of a hatch fill, each tested against the region's elements */
  }
  /* UC9: and the exact step's, over the pieces of the item's three parts (its deposits, which the cap counts, and the
   * horizontal pieces on cells' left sides, at most one for each point and each column of the widest raster), a cell of
   * an aligned stroke holding the pieces of all three. */
  uint64_t points3;
  uint64_t cell3;
  if (!gcu_safe_mul_u64((uint64_t)cs->geometry.max_points, 3u, &points3) || !gcu_safe_mul_u64(cs->max_exact, 3u, &cell3)) {
    return UINT64_MAX;
  }
  uint64_t exact = gshcpu_exact_work_bound((uint64_t)cs->max_deposits, points3, (uint64_t)cs->max_dimension, cell3, cs->max_crossings);
  if (!gcu_safe_add_u64(b, exact, &b) || !gcu_safe_add_u64(b, cs->max_image_work, &b)) {
    return UINT64_MAX; /* IC9: and one image item's texels */
  }
  /* CG11: and one union clip push, which sweeps each of its members (the points of the geometry, and the cells counted, once for a
   * member of a group of one and twice for one of a larger group, and the plane's quantizing and merging passes over the pixels) and
   * makes exact the pixels its groups share, over at most `max_deposits` pieces of each member, in cells of at most the step's pieces. */
  uint64_t members = (uint64_t)cs->union_members;
  uint64_t sweeps;
  uint64_t union_exact;
  if (!gcu_safe_mul_u64((uint64_t)cs->geometry.max_points, 2u, &sweeps) || !gcu_safe_add_u64(sweeps, 2u * cs->max_deposits, &sweeps) ||
      !gcu_safe_add_u64(sweeps, pixels, &sweeps) || !gcu_safe_mul_u64(sweeps, members, &sweeps) ||
      !gcu_safe_add_u64(sweeps, pixels, &sweeps)) {
    return UINT64_MAX;
  }
  uint64_t member_deposits;
  uint64_t member_points;
  if (!gcu_safe_mul_u64(cs->max_deposits, members, &member_deposits) || !gcu_safe_mul_u64((uint64_t)cs->geometry.max_points, members, &member_points)) {
    return UINT64_MAX;
  }
  union_exact = gshcpu_exact_work_bound(member_deposits, member_points, (uint64_t)cs->max_dimension, cs->max_exact, cs->max_crossings);
  if (!gcu_safe_add_u64(b, sweeps, &b) || !gcu_safe_add_u64(b, union_exact, &b)) {
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
  /* IC1: an image paint, every filter, every edge mode, every format, filtered in a space of its own. */
  out->image_filters = (UINT32_C(1) << GSHSCENE_IMAGE_FILTER_COUNT) - 1u;
  out->image_extends = (UINT32_C(1) << GSHSCENE_IMAGE_EXTEND_COUNT) - 1u;
  out->image_formats = GSHSCENE_IMAGE_FORMAT_ALL;
  out->flags |= GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  /* MC1: a masked group, every mode, transfer, coefficient set and space the waist defines, and both things a mask acts on.  A
   * masked group that is also non-isolated or knockout is refused first, by the flags this sink does not claim. */
  out->flags |= GSHSCENE_CAP_GROUP_SOFT_MASK;
  out->mask_native = GSHSCENE_MASK_NATIVE_ALL;
  /* KC1: a group that is non-isolated, or knockout, or both, with or without a mask, so no combination is refused. */
  out->flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT;
  /* PC1: a pattern, drawn as defined (PT1 to PT14): every field of it honored, so not PATTERN_RESAMPLED. */
  out->flags |= GSHSCENE_CAP_PATTERN;
  /* HX13: a hatch, drawn as the stroke of its trimmed lines. */
  out->flags |= GSHSCENE_CAP_HATCH;
  /* AA5: a fill or a stroke that asks for no antialiasing, covered by the centers of its pixels (list.h, AA2 and AA3), in every tier. */
  out->flags |= GSHSCENE_CAP_ANTIALIAS_NONE;
  /* CU22: a union clip whose members the predicate shows disjoint, the fast tier FT-U1; and (CG10) any union, the general tier, which
   * is a union with the capability of the disjoint ones (CU12). */
  if (cs->tiers & (GSHCPU_RENDER_TIER_UNION | GSHCPU_RENDER_TIER_UNION_GENERAL)) {
    out->flags |= GSHSCENE_CAP_CLIP_UNION;
  }
  if (cs->tiers & GSHCPU_RENDER_TIER_UNION_GENERAL) {
    out->flags |= GSHSCENE_CAP_CLIP_UNION_GENERAL;
  }
  out->cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  out->cancel_work_bound = work_bound(cs);
  out->work_measure = "cells+points+pixels+texels";
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_workspace_bytes(void * impl, const GSHSCENE_Render_Request * request, size_t * out) {
  return scene_result(need_of(impl, request, out));
}

/* --- one unit ----------------------------------------------------------------------------------- */

/** Refused while drawing (S6): the entry; under strict the render stops here, else the item's block is skipped. */
static GSHSCENE_Result refuse(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  if (cur_pat(run)) {
    return GSHSCENE_ERR_LIMIT; /* PC8: inside a tile the sink's limit refuses the use that is painted with the pattern, not the unit */
  }
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

/**
 * gshcpu_render_apply for source-over, which every fill and stroke is composed with, written out: the same operations in the
 * same order (factors 1 and 1 - source alpha, then the mix of the pixel with the coverage, then the saturation), so the same
 * doubles, without the dispatch on the operator or the call.
 */
static inline void apply_src_over(double d[4], const double s[4], double m) {
  if (m == 1.0 && s[3] == 1.0) {
    /* Full weight of an opaque source: keep is 0 and fb is 0, so o is the source itself. */
    for (int i = 0; i < 4; i++) {
      d[i] = s[i] > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION
          : s[i] < -GSHSCENE_COLOR_SATURATION ? -GSHSCENE_COLOR_SATURATION : s[i];
    }
    return;
  }
  const double fb = 1.0 - s[3];
  const double keep = 1.0 - m;
  for (int i = 0; i < 4; i++) {
    const double o = 1.0 * s[i] + fb * d[i];
    const double x = keep * d[i] + m * o;
    d[i] = x > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION
        : x < -GSHSCENE_COLOR_SATURATION ? -GSHSCENE_COLOR_SATURATION : x;
  }
}

/** Whether two concrete color spaces are the same. */
static int same_space(const GSHSCENE_Color_Space * a, const GSHSCENE_Color_Space * b) {
  return a->primaries == b->primaries && a->transfer == b->transfer && a->gamma == b->gamma;
}

/** The pixel (x, y) of a region-sized buffer. */
static size_t at(const Run * run, int64_t x, int64_t y) {
  if (run->masked) {
    const Pat_Run * q = pr(run);
    return (size_t)(y - q->org_y) * run->layout.width + (size_t)(x - q->org_x);
  }
  return (size_t)(y - run->layout.region.y0) * run->layout.width + (size_t)(x - run->layout.region.x0);
}

/** The map the items being drawn are composed with (AD-24): the target's, or inside a pattern's tile the current copy's canonical map (PC3). */
static const GSHGEOM_Map * target_map(const Run * run);

static void initial_backdrop(const Run * run, size_t i, size_t p, double out[4]);

/**
 * The backdrop an element of frame @p i's layer composites with at pixel @p p (T3): the layer itself, or in a knockout group its
 * initial backdrop (KC4).  The layer is read in place, where it was made ready (widen).
 */
static void backdrop_of(const Run * run, size_t i, size_t p, double out[4]) {
  if (gs_of(run)->frames[i].knockout) {
    initial_backdrop(run, i, p, out);
  } else {
    memcpy(out, &run->frames[i].layer[4u * p], 4u * sizeof(double));
  }
}

/**
 * KC5: the initial backdrop `L0` of frame @p i at pixel @p p, in its own space: nothing for an isolated group (N6); for a non-isolated
 * one the backdrop it has as an element of its parent (the parent's value, or in a knockout parent the parent's own initial backdrop,
 * read the same way through the parents), converted by the one prepared conversion (N3, which leaves equal spaces exactly alone).  It is
 * read where it is needed and never copied: the parent does not change while the group is open, so a read gives the bits a copy at
 * the push would have held, and it is the same function whether the layer is made ready or popped.
 */
static void initial_backdrop(const Run * run, size_t i, size_t p, double out[4]) {
  const Gen_Frame * g = &gs_of(run)->frames[i];
  if (!g->nonisolated) {
    memset(out, 0, 4u * sizeof(double));
    return;
  }
  double b[4];
  backdrop_of(run, i - 1u, p, b);
  gshcpu_render_convert_premultiplied(&g->from_parent, b, out);
}

/**
 * Make the pixels of row @p y from @p x0 to @p x1 of @p f ready (widen): a group's layer starts at `(0, 0, 0, 0)`, or at its initial
 * backdrop when it is non-isolated (KC5), its group alpha and group shape at 0 (KC3);
 * the root layer is the target (N6), its values read once, here, and its touched map clear.
 */
static void ready_span(Run * run, Frame * f, int64_t x0, int64_t x1, int64_t y) {
  if (x0 >= x1) {
    return;
  }
  if (f->bytes) {
    unsigned char * at8 = f->px8 + (size_t)(y - f->oy) * f->stride8 + 4u * (size_t)(x0 - f->ox);
    if (f != &run->frames[0]) {
      memset(at8, 0, 4u * (size_t)(x1 - x0));
    } else if (run->straight8) {
      gshcpu_direct8_premultiply_row(at8, run->pixels + (size_t)y * run->stride + 4u * (size_t)x0, (size_t)(x1 - x0));
    }
    return; /* a premultiplied root is the target itself: nothing is read or cleared */
  }
  size_t p = at(run, x0, y);
  size_t n = (size_t)(x1 - x0);
  if (f != &run->frames[0]) {
    memset(f->layer + 4u * p, 0, 4u * n * sizeof *f->layer);
    if (run->general) {
      const Gen_Frame * g = gen_of(run, f);
      if (g->ag) {
        memset(g->ag + p, 0, n * sizeof *g->ag);
      }
      if (g->fg) {
        memset(g->fg + p, 0, n * sizeof *g->fg);
      }
      if (g->nonisolated) {
        const size_t i = (size_t)(f - run->frames);
        for (size_t k = 0; k < n; k++) {
          initial_backdrop(run, i, p + k, &f->layer[4u * (p + k)]);
        }
      }
    }
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
  if (run->general && f != &run->frames[0] && gen_of(run, f)->nonisolated) {
    /* KC10: a non-isolated layer reads its parent where it is made ready, so the parent is made ready over the same pixels first. */
    widen(run, f - 1, grown);
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

/**
 * widen, and in a render whose list holds a masked group note the rectangle the whole render reaches of this layer (@p whole:
 * the item's rectangle met with the whole render's region, whatever the damage), which is what decides whether the whole render
 * would draw a masked group's mask (MC6).
 */
static void widen_w(Run * run, Frame * f, GSHCPU_Rect r, GSHCPU_Rect whole) {
  widen(run, f, r);
  if (run->masked && !gshcpu_render_rect_empty(whole)) {
    Mask_Frame * mf = &ms_of(run)->frames[f - run->frames];
    mf->whole_dirty = gshcpu_render_rect_join(mf->whole_dirty, whole);
  }
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

/** The pixels the units of this walk may reach: the region, or inside a mask pass the pop region of the masked group (MC2). */
static GSHCPU_Rect window_of(const Run * run) {
  return run->masked ? ms_of(run)->window : run->layout.region;
}

/** The pixels the clips in force in the innermost group allow (N9): the window meet their rectangles. */
static GSHCPU_Rect reach_of(const Run * run, size_t from, size_t to) {
  GSHCPU_Rect r = window_of(run);
  for (size_t i = from; i < to; i++) {
    r = gshcpu_render_rect_meet(r, run->clips[i].rect);
  }
  return r;
}

/** reach_of in the whole render (S4): the extent meet the clips' rectangles there. */
static GSHCPU_Rect whole_reach_of(const Run * run, size_t from, size_t to) {
  GSHCPU_Rect r = run->masked ? pr(run)->whole_cur : run->layout.whole;
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
static void paint_source(const Run * run, const GSHSCENE_Paint * paint, const Frame * frame, double s[4]) {
  GSHSCENE_Color color;
  if (gshscene_paint_resolve(paint, &run->foreground, run->palette, &color) != GSHSCENE_OK) {
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

static void source_of(const Run * run, const GSHSCENE_Item * item, const Frame * frame, double s[4]) {
  paint_source(run, &item->paint, frame, s);
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

/** KC4: a fill or stroke begun in a general layer, counted (its pixels take the element step). */
static void count_general_fill(Run * run, const Frame * frame) {
  if (frame->general) {
    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, general_fills));
  }
}

/**
 * What paints the spans of an item's coverage (D8, N5, N7; G6 to G12, S11): the item's paint made ready by painter_begin, then
 * each span, row by row, weighed by the clips in force and composited SRC_OVER into the layer.  A solid paint is one source;
 * a gradient's source at each pixel is G12's at the pixel's center under the chain composed by shape-geometry and inverted
 * once, converted per pixel from the interpolation space to the layer's (N3), or, under the fast fill coverage, read from a
 * table of the line.  Where G8 gives nothing the pixel is not drawn.
 */
typedef struct Painter {
  Run * run;
  Frame * frame;
  int gradient;
  int image;                 /* an image paint (IC1 to IC8): im is its sampler */
  int refused;               /* painter_begin refused the item (or the render stops under strict) */
  double src[4];             /* a solid paint's source */
  GSHCPU_Gradient gr;
  GSHCPU_Image im;
  double lut[GSHCPU_IMAGE_LUT];  /* FT-3 */
  GSHCPU_Convert conv;
  const double * table;      /* the gradient's color table, or NULL: evaluated at each pixel */
  GSHCPU_Direct8 d8;         /* fast8: a solid paint's source, and the layer it is composed into */
  const uint8_t * table8;    /* fast8: the table as premultiplied bytes, four to a point */
  int pattern;               /* a pattern paint (PC2): lf is its lattice layer */
  const Frame * lf;
  int64_t lox, loy;          /* the pixel of the lattice layer's first element (the frame of coordinates of the rectangle it was begun with) */
  int period;                /* PC9: the lattice layer holds one window of the picture: a pixel is read at rep(q - w) + w */
  GSHGEOM_Lattice_Period per;
  int64_t wx, wy;
  int has_tint;              /* PC7: uncolored; tint is the premultiplied tint, converted once */
  double tint[4];
  double alpha;              /* the paint's alpha multiplier */
} Painter;

static GSHSCENE_Result pattern_begin(Painter * p, Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count, Frame * frame,
    GSHCPU_Rect rect, GSHCPU_Rect whole);
static int pattern_pixel(const Painter * p, int64_t x, int64_t y, double s[4]);
static int pattern_read(const Painter * p, int64_t x, int64_t y, double s[4]);

/**
 * An image paint made ready (IC1 to IC9): its map composed with the chain by the one composer (AD-24), the filter chosen once
 * for the fill by IP8 (IC3), the image opened in place, the map inverted, the tint resolved and converted once.  The same
 * refusals as a gradient's: a composed map that cannot be formed or inverted, or a rectangle (the whole render's) mapped outside
 * the sampling domain, is the sink's own limit.
 */
static GSHSCENE_Result image_begin(Painter * p, Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    Frame * frame, GSHCPU_Rect rect, GSHCPU_Rect whole) {
  if (gshcpu_render_rect_empty(whole)) {
    p->refused = 1; /* nothing of it is in the whole render: nothing to paint, and nothing to report */
    return GSHSCENE_OK;
  }
  GSHSCENE_Image_Paint ip;
  if (gshscene_resource_image_paint(item->paint.resource, &ip) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHGEOM_Map composed;
  run->maps[count - 1] = item->map;
  run->maps[count] = ip.map;
  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &composed) != GSHGEOM_OK) {
    p->refused = 1;
    return refuse(run, item, chain, count);
  }
  const uint32_t filter = gshgeom_map_stretches_every_direction(&composed) ? ip.magnify : ip.minify;
  if (gshcpu_render_image_open(&p->im, ip.image, ip.window, ip.extend_x, ip.extend_y, filter, &ip.filter_space,
          gshscene_job_target_space(run->job), &frame->space) != GSHCPU_OK ||
      !gshcpu_render_image_place(&p->im, &composed) || !gshcpu_render_image_in_domain(&p->im, whole)) {
    p->refused = 1;
    return refuse(run, item, chain, count);
  }
  if (cur_pat(run) && cur_pat(run)->guard) {
    /* PC9: the tier vouches for a copy only if the whole cell passes the checks below, which the window's copies meet over less. */
    Pat_Level * pl = cur_pat(run);
    uint64_t full_bound = 0;
    uint64_t full_pixel = 0;
    if (!gshcpu_render_image_in_domain(&p->im, pl->cur_qbox)) {
      pl->abort_tier = 1;
    } else if (filter == (uint32_t)GSHSCENE_IMAGE_FILTER_AREA &&
        (!gshcpu_render_image_area_bound(&p->im, &full_pixel) || full_pixel > run->cs->max_area_pixel_work ||
            !gcu_safe_mul_u64((uint64_t)(pl->cur_qbox.x1 - pl->cur_qbox.x0), (uint64_t)(pl->cur_qbox.y1 - pl->cur_qbox.y0), &full_bound) ||
            !gcu_safe_mul_u64(full_bound, full_pixel, &full_bound) || full_bound > run->cs->max_image_work)) {
      pl->abort_tier = 1;
    }
  }
  if (filter == (uint32_t)GSHSCENE_IMAGE_FILTER_AREA) {
    /* IC9: the bound is a function of the map, the window and the rectangle only, so that no damage, cache or thread changes
     * what is refused; it is met before any pixel is drawn, over what the whole render draws of the item. */
    uint64_t pixel = 0;
    uint64_t item_bound = 0;
    const uint64_t w = (uint64_t)(whole.x1 - whole.x0);
    const uint64_t h = (uint64_t)(whole.y1 - whole.y0);
    if (!gshcpu_render_image_area_bound(&p->im, &pixel) || pixel > run->cs->max_area_pixel_work ||
        !gcu_safe_mul_u64(w, h, &item_bound) || !gcu_safe_mul_u64(item_bound, pixel, &item_bound) ||
        item_bound > run->cs->max_image_work) {
      p->refused = 1;
      return refuse(run, item, chain, count);
    }
  }
  double tint[3] = {0.0, 0.0, 0.0};
  double tint_alpha = 0.0;
  if (ip.tint) {
    GSHSCENE_Color color;
    if (gshscene_paint_resolve(ip.tint, &run->foreground, run->palette, &color) != GSHSCENE_OK) {
      memset(&color, 0, sizeof color); /* unreachable: the disposition refused it; transparent draws nothing */
    }
    GSHSCENE_Color_Space space = color.space;
    if (gshscene_color_space_is_target(&space)) {
      space = *gshscene_job_target_space(run->job);
    }
    GSHCPU_Convert c;
    gshcpu_render_convert_prepare(&c, &space, &frame->space);
    tint[0] = color.rgba[0];
    tint[1] = color.rgba[1];
    tint[2] = color.rgba[2];
    gshcpu_render_convert_straight(&c, tint, tint);
    tint_alpha = color.rgba[3];
  }
  gshcpu_render_image_source(&p->im, item->paint.rgba[3], tint, tint_alpha);
  const unsigned taken = gshcpu_render_image_tiers(&p->im, run->cs->tiers, p->lut);
  gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, images));
  if (taken & GSHCPU_RENDER_TIER_IMAGE_COPY) {
    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, image_copy));
  }
  if (taken & GSHCPU_RENDER_TIER_IMAGE_SEPARABLE) {
    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, image_separable));
  }
  if (taken & GSHCPU_RENDER_TIER_IMAGE_TABLE) {
    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, image_table));
  }
  if (!quiet_of(run)) {
    widen_w(run, frame, rect, whole);
  }
  run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
  return GSHSCENE_OK;
}

/**
 * Make the paint of @p item ready to paint @p rect (the pixels of the item that meet what it can reach; @p whole the
 * render's), widening the frame to it.  A composed map past the double range, or a rectangle (the whole render's: S4) mapped
 * outside G14's domain, is the sink's own limit (S6): @p p->refused is set and the result is refuse()'s.
 */
static GSHSCENE_Result painter_begin(Painter * p, Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    Frame * frame, GSHCPU_Rect rect, GSHCPU_Rect whole) {
  p->run = run;
  p->frame = frame;
  p->refused = 0;
  p->table = NULL;
  p->table8 = NULL;
  p->gradient = item->paint.kind == GSHSCENE_PAINT_GRADIENT;
  p->image = item->paint.kind == GSHSCENE_PAINT_IMAGE;
  p->pattern = 0;
  count_general_fill(run, frame);
  if (item->paint.kind == GSHSCENE_PAINT_PATTERN) {
    return pattern_begin(p, run, item, chain, count, frame, rect, whole);
  }
  if (p->image) {
    return image_begin(p, run, item, chain, count, frame, rect, whole);
  }
  if (!p->gradient) {
    source_of(run, item, frame, p->src);
    if (frame->bytes) {
      uint8_t c[4];
      for (int i = 0; i < 4; i++) {
        c[i] = gshcpu_render_store8(p->src[i]);
      }
      for (int i = 0; i < 3; i++) {
        c[i] = c[i] > c[3] ? c[3] : c[i]; /* premultiplied: no channel past the alpha */
      }
      p->d8.pixels = frame->px8;
      p->d8.stride = frame->stride8;
      p->d8.dx = -frame->ox;
      p->d8.dy = -frame->oy;
      gshcpu_direct8_color(&p->d8, c);
    }
    if (!quiet_of(run)) {
      widen_w(run, frame, rect, whole);
    }
    run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
    return GSHSCENE_OK;
  }
  if (gshcpu_render_rect_empty(whole)) {
    p->refused = 1; /* nothing of it is in the whole render: nothing to paint, and nothing to report */
    return GSHSCENE_OK;
  }
  GSHSCENE_Gradient g;
  const Resolved * e = NULL;
  if (gshscene_resource_gradient(item->paint.resource, &g) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_Result r = resolved_of(run, item->paint.resource, &g, &e);
  if (r != GSHSCENE_OK) {
    return r;
  }
  /* AD-24: T I_1 ... I_k M G, the gradient's map the leaf, composed by the one composer. */
  GSHGEOM_Map composed;
  run->maps[count - 1] = item->map;
  run->maps[count] = g.map;
  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &composed) != GSHGEOM_OK ||
      gshcpu_render_gradient_prepare(&p->gr, &g, run->pool + e->first, &composed, item->paint.rgba[3]) != GSHCPU_OK ||
      !gshcpu_render_gradient_in_domain(&p->gr, whole)) {
    p->refused = 1;
    return refuse(run, item, chain, count);
  }
  if (cur_pat(run) && cur_pat(run)->guard && !gshcpu_render_gradient_in_domain(&p->gr, cur_pat(run)->cur_qbox)) {
    cur_pat(run)->abort_tier = 1; /* PC9: the whole cell, which the window cannot vouch for */
  }
  gshcpu_render_convert_prepare(&p->conv, &e->space, &frame->space);
  if (!quiet_of(run)) {
    widen_w(run, frame, rect, whole);
  }
  const uint64_t area = (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
  run->work += area;
  if (run->cs->fast_fill && run->tables && !cur_pat(run)) {
    /* The fast fill coverage takes the color from a table of the line (made once for the paint, and kept for the next). */
    for (size_t k = 0; k < TABLE_SLOTS && !p->table; k++) {
      const Table_Key * key = &run->table_key[k];
      if (key->used && key->id == gshscene_resource_id(item->paint.resource) && key->alpha == item->paint.rgba[3] && key->bytes == frame->bytes &&
          key->space.primaries == frame->space.primaries && key->space.transfer == frame->space.transfer &&
          key->space.gamma == frame->space.gamma) {
        p->table = run->tables + k * (GSHCPU_RENDER_GRADIENT_TABLE + 1u) * 4u;
      }
    }
    if (!p->table && (area >= 2048u || frame->bytes)) {
      size_t k = run->table_next++ % TABLE_SLOTS;
      Table_Key * key = &run->table_key[k];
      key->used = 1;
      key->id = gshscene_resource_id(item->paint.resource);
      key->alpha = item->paint.rgba[3];
      key->bytes = frame->bytes;
      key->space = frame->space;
      double * table = run->tables + k * (GSHCPU_RENDER_GRADIENT_TABLE + 1u) * 4u;
      gshcpu_render_gradient_table(&p->gr, &p->conv, table);
      if (frame->bytes) {
        /* The table as bytes, in place: a point's four bytes are written below where its four doubles were read. */
        uint8_t * bytes = (uint8_t *)(void *)table;
        for (size_t q = 0; q <= GSHCPU_RENDER_GRADIENT_TABLE; q++) {
          uint8_t c[4];
          for (size_t i = 0; i < 4; i++) {
            c[i] = gshcpu_render_store8(table[4u * q + i]);
          }
          for (size_t i = 0; i < 3; i++) {
            c[i] = c[i] > c[3] ? c[3] : c[i];
          }
          memcpy(bytes + 4u * q, c, 4);
        }
      }
      p->table = table;
    }
  }
  if (frame->bytes && p->table) {
    p->table8 = (const uint8_t *)(const void *)p->table;
  }
  return GSHSCENE_OK;
}

/**
 * KC4: one fill's pixel into a general layer: shape @p f, the opacity source @p src, source-over, onto the layer's backdrop (itself, or
 * in a knockout group its initial backdrop), with the group alpha and group shape stepped where the layer carries them.
 */
static void general_fill(Run * run, Frame * frame, size_t p, const double * src, double f) {
  const Gen_Frame * g = gen_of(run, frame);
  double bk[4];
  const double * b = &frame->layer[4u * p];
  if (g->knockout) {
    initial_backdrop(run, (size_t)(frame - run->frames), p, bk);
    b = bk;
  }
  (void)gshcpu_render_group_step(&frame->layer[4u * p], b, g->ag ? g->ag + p : NULL, g->fg ? g->fg + p : NULL, g->knockout, src, f, f,
      GSHSCENE_COMPOSITE_SRC_OVER);
}

/** One pixel of weight @p m8 and source @p src at (x, y), in the layer. */
static inline void painter_pixel(Run * run, Frame * frame, int64_t x, int64_t y, uint32_t m8, const double * src) {
  const size_t p = at(run, x, y);
  if (frame->general) {
    general_fill(run, frame, p, src, gshcpu_k_level[m8]);
  } else {
    apply_src_over(&frame->layer[4u * p], src, gshcpu_k_level[m8]);
  }
  if (run->top == 0) {
    run->touched[p] = 1;
  }
}

/** D8: the @p len coverages @p m of row @p y from @p x weighed by every clip in force (a pixel outside a clip's rectangle weighs 0). */
static void weigh_row(const Run * run, const Frame * frame, int64_t y, int64_t x, size_t len, uint8_t * m) {
  for (size_t k = frame->clip_base; k < run->clip_count; k++) {
    const Clip * clip = &run->clips[k];
    const GSHCPU_Rect r = clip->rect;
    if (y < r.y0 || y >= r.y1 || x >= r.x1 || x + (int64_t)len <= r.x0) {
      memset(m, 0, len);
      return;
    }
    const int64_t from = x < r.x0 ? r.x0 : x;
    const int64_t to = x + (int64_t)len < r.x1 ? x + (int64_t)len : r.x1;
    if (from > x) {
      memset(m, 0, (size_t)(from - x));
    }
    if (to < x + (int64_t)len) {
      memset(m + (to - x), 0, (size_t)(x + (int64_t)len - to));
    }
    gshcpu_direct8_weigh(m + (from - x), clip->coverage + (size_t)(y - r.y0) * (size_t)(r.x1 - r.x0) + (size_t)(from - r.x0),
        (size_t)(to - from));
  }
}

/** painter_span on bytes (fast8): the coverages weighed by the clips, then composed in integers into the layer. */
static void painter_span8(Painter * p, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  Run * run = p->run;
  Frame * frame = p->frame;
  const int clipped = frame->clip_base < run->clip_count;
  if (!p->gradient && !clipped) {
    gshcpu_direct8_span(&p->d8, y, x, n, m8);
    return;
  }
  uint8_t m[256];
  uint32_t index[256];
  for (size_t i0 = 0; i0 < n; i0 += 256u) {
    const size_t len = n - i0 < 256u ? n - i0 : 256u;
    const uint8_t * cov = m8 + i0; /* (copied only to be weighed by the clips) */
    if (clipped) {
      memcpy(m, cov, len);
      weigh_row(run, frame, y, x + (int64_t)i0, len, m);
      cov = m;
    }
    if (!p->gradient) {
      gshcpu_direct8_span(&p->d8, y, x + (int64_t)i0, len, cov);
      continue;
    }
    gshcpu_render_gradient_table_row(&p->gr, (double)(int32_t)(x + (int64_t)i0) + 0.5, (double)(int32_t)y + 0.5, len, cov, index);
    gshcpu_direct8_span_table(frame->px8 + (size_t)(y - frame->oy) * frame->stride8 + 4u * (size_t)(x + (int64_t)i0 - frame->ox),
        p->table8, index, cov, len);
  }
}

/** The receiver of a span of coverages (GSHCPU_Span_Fn): @p n pixels from (@p x, @p y), a pixel of coverage 0 not drawn. */
static void painter_span(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  Painter * p = ctx;
  Run * run = p->run;
  Frame * frame = p->frame;
  if (frame->bytes) {
    painter_span8(p, y, x, n, m8);
    return;
  }
  const int clipped = frame->clip_base < run->clip_count;
  if (p->pattern) {
    /* PC2: N7 with the source of the lattice layer's pixel (PC7), for each pixel of coverage after the clips. */
    int64_t wxr = 0; /* PC9: the window column of the span's pixel i; along a row the class moves one column a pixel (LT3's reduction is the same k and shift) */
    int64_t wyr = 0;
    if (p->period && n > 0) {
      int64_t r[2];
      gshgeom_lattice_reduce(&p->per, x - p->wx, y - p->wy, r);
      wxr = r[0];
      wyr = p->wy + r[1];
    }
    for (size_t i = 0; i < n; i++) {
      const int64_t col = wxr; /* this pixel's reduced column, then the next one's */
      if (p->period) {
        wxr = wxr + 1 == p->per.p ? 0 : wxr + 1;
      }
      if (m8[i] == 0) {
        continue;
      }
      const int64_t px = x + (int64_t)i;
      uint32_t m = clipped ? weigh_clips(run, frame->clip_base, run->clip_count, m8[i], px, y) : m8[i];
      if (m == 0) {
        continue;
      }
      double s[4];
      if (p->period ? pattern_read(p, p->wx + col, wyr, s) : pattern_pixel(p, px, y, s)) {
        painter_pixel(run, frame, px, y, m, s);
      }
    }
    return;
  }
  if (p->image) {
    const uint64_t before = p->im.visits;
    for (size_t i = 0; i < n; i++) {
      if (m8[i] == 0) {
        continue;
      }
      const int64_t px = x + (int64_t)i;
      uint32_t m = clipped ? weigh_clips(run, frame->clip_base, run->clip_count, m8[i], px, y) : m8[i];
      if (m == 0) {
        continue;
      }
      double s[4];
      gshcpu_render_image_at(&p->im, (double)(int32_t)px + 0.5, (double)(int32_t)y + 0.5, s);
      painter_pixel(run, frame, px, y, m, s);
    }
    run->work += p->im.visits - before; /* the texels and cells AREA visited (IC9) */
    return;
  }
  if (!p->gradient) {
    for (size_t i = 0; i < n; i++) {
      if (m8[i] == 0) {
        /* A pixel of coverage 0 weighs 0 under any clips and is not touched: pass a run of them eight at a time. */
        while (i + 8u <= n) {
          uint64_t eight;
          memcpy(&eight, m8 + i, sizeof eight);
          if (eight != 0) {
            break;
          }
          i += 8u;
        }
        while (i < n && m8[i] == 0) {
          i++;
        }
        if (i == n) {
          return;
        }
      }
      uint32_t m = m8[i];
      if (clipped) {
        m = weigh_clips(run, frame->clip_base, run->clip_count, m, x + (int64_t)i, y);
        if (m == 0) {
          continue;
        }
      }
      painter_pixel(run, frame, x + (int64_t)i, y, m, p->src);
    }
    return;
  }
  if (p->table) {
    uint32_t index[256];
    for (size_t i0 = 0; i0 < n; i0 += 256u) {
      const size_t len = n - i0 < 256u ? n - i0 : 256u;
      size_t first = 0;
      while (first < len && m8[i0 + first] == 0) {
        first++;
      }
      if (first == len) {
        continue;
      }
      gshcpu_render_gradient_table_row(&p->gr, (double)(int32_t)(x + (int64_t)i0) + 0.5, (double)(int32_t)y + 0.5, len, m8 + i0, index);
      for (size_t i = first; i < len; i++) {
        if (m8[i0 + i] == 0 || index[i] == GSHCPU_RENDER_GRADIENT_NONE) {
          continue;
        }
        uint32_t m = m8[i0 + i];
        if (clipped) {
          m = weigh_clips(run, frame->clip_base, run->clip_count, m, x + (int64_t)(i0 + i), y);
          if (m == 0) {
            continue;
          }
        }
        painter_pixel(run, frame, x + (int64_t)(i0 + i), y, m, p->table + 4u * (size_t)index[i]);
      }
    }
    return;
  }
  for (size_t i = 0; i < n; i++) {
    if (m8[i] == 0) {
      continue;
    }
    const int64_t px = x + (int64_t)i;
    uint32_t m = clipped ? weigh_clips(run, frame->clip_base, run->clip_count, m8[i], px, y) : m8[i];
    if (m == 0) {
      continue;
    }
    double color[4];
    if (!gshcpu_render_gradient_at(&p->gr, (double)(int32_t)px + 0.5, (double)(int32_t)y + 0.5, color)) {
      continue; /* G8: outside the cone, the source is zero and src-over changes nothing */
    }
    gshcpu_render_convert_straight(&p->conv, color, color);
    const double s[4] = {color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]};
    painter_pixel(run, frame, px, y, m, s);
  }
}

/** The coverage of @p cov over @p rect painted as the item's paint says: the fill's tail, which a stroke shares (SC5). */
static GSHSCENE_Result paint_coverage(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    Frame * frame, GSHCPU_Rect rect, GSHCPU_Rect whole, const uint8_t * cov) {
  Painter p;
  GSHSCENE_Result r = painter_begin(&p, run, item, chain, count, frame, rect, whole);
  if (r != GSHSCENE_OK || p.refused) {
    return r;
  }
  const size_t w = (size_t)(rect.x1 - rect.x0);
  for (int64_t y = rect.y0; y < rect.y1; y++) {
    painter_span(&p, y, rect.x0, w, cov + (size_t)(y - rect.y0) * w);
  }
  return GSHSCENE_OK;
}

/**
 * Make the first @p extent bytes of the accumulator zero, where a sparse sweep needs them so: no more than the sweep uses (a glyph's
 * stroke does not touch the megabytes a window-tall band would), and what is zero already is not written again.  The sparse sweeps
 * leave what they used zero.
 */
static void acc_clean(Run * run, size_t extent) {
  if (extent > run->layout.acc_bytes) {
    extent = run->layout.acc_bytes;
  }
  if (run->acc_zero < extent) {
    memset((unsigned char *)(void *)run->acc + run->acc_zero, 0, extent - run->acc_zero);
    run->acc_zero = extent;
  }
}

/** A fill's paint, begun by shape() when the fast fill coverage sweeps straight into it (shape()). */
typedef struct Span_Paint {
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  Frame * frame;
  Painter painter;
  int taken;
} Span_Paint;

/**
 * The geometry and the coverage of a fill or clip push, into @p out over @p rect (the item's pixels meet what it
 * can reach), and in @p whole_rect the rectangle the whole render draws of it (its pixels meet @p whole, what it can
 * reach there).  The cell cap is met over that one, so that a damage-limited render refuses the items the whole
 * render refuses (S4); a bound from its edges skips the count where the cap cannot be met.
 * With @p sp (a fill, a stroke painted with a gradient) and the fast fill coverage, the spans of the coverage are painted as the sweep makes
 * them and @p out is left alone: @p sp->taken is set, the paint was begun (or refused), and the result is the whole of it.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_LIMIT for a resource limit (the caller refuses the item); OOM.
 */
static GSHSCENE_Result shape(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    GSHCPU_Rect reach, GSHCPU_Rect whole, uint8_t * out, GSHCPU_Rect * rect, GSHCPU_Rect * whole_rect, Span_Paint * sp) {
  GSHCPU_Item_Geometry g;
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_reset(&run->arena);
  }
  GSHCPU_Result r = gshcpu_render_geometry(item, run->maps, count - 1, target_map(run), run->item_alloc,
      &run->cs->geometry, &g);
  run->work += g.points;
  /* A run from a font: each glyph's entry (glyph.h, D7); a glyph refused under strict stops the render here (S6). */
  for (size_t i = 0; r == GSHCPU_OK && g.notes && i < g.count && !cur_pat(run); i++) {
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
          gshcpu_render_cells_bound(paths, g.count, walk, run->cs->max_deposits) <= run->cs->max_deposits) {
        walk = *rect;
      }
      if (item->kind == GSHSCENE_ITEM_FILL && item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE) {
        /* AA2: whole or nothing by the pixel's center, whichever tier the fill would take: the lean sweep and the exact cover are both
         * areas, and a center is decided exactly, so there is one coverage and it is this. */
        r = gshcpu_render_cover_center(paths, g.count, item->fill_rule, NULL, *rect, walk, out, run->acc, run->cs->max_deposits, &cells);
        run->acc_zero = 0;
        run->work += cells + (uint64_t)(rect->x1 - rect->x0) * (uint64_t)(rect->y1 - rect->y0);
      } else if (run->cs->fast_fill && (run->cs->tiers & GSHCPU_RENDER_TIER_SPARSE) && !gshcpu_render_rect_empty(*rect) &&
          !(sp && sp->item->paint.kind == GSHSCENE_PAINT_PATTERN) && rect->x1 - rect->x0 <= 65000 &&
          gshcpu_render_cells_bound(paths, g.count, walk, run->cs->max_deposits) <= run->cs->max_deposits) {
        /* The fast fill coverage: the lean cover of every path at once, into the coverage of the rectangle. */
        const size_t scratch = run->layout.acc_bytes;
        acc_clean(run, gshcpu_lean_fill_extent((size_t)(rect->x1 - rect->x0), (size_t)(rect->y1 - rect->y0), scratch));
        if (sp) {
          GSHSCENE_Result begun = painter_begin(&sp->painter, run, sp->item, sp->chain, sp->count, sp->frame, *rect, *whole_rect);
          sp->taken = 1;
          if (begun != GSHSCENE_OK || sp->painter.refused) {
            gshcpu_render_geometry_release(&g, run->item_alloc);
            return begun;
          }
          r = gshcpu_render_lean_fill_spans(paths, g.count, item->fill_rule == GSHGEOM_FILL_EVEN_ODD, 0, 0, *rect, run->acc, scratch,
              painter_span, &sp->painter);
        } else {
          r = gshcpu_render_lean_fill(paths, g.count, item->fill_rule == GSHGEOM_FILL_EVEN_ODD, 0, 0, *rect, run->acc, scratch, out);
          run->work += (uint64_t)(rect->x1 - rect->x0) * (uint64_t)(rect->y1 - rect->y0);
        }
      } else {
        r = gshcpu_render_cover_counted(paths, g.count, item->fill_rule, *rect, walk, out, run->acc, run->cs->max_deposits,
            &cells);
        run->acc_zero = 0; /* the dense sweep writes the accumulator band by band and leaves it so */
        run->work += cells;
      }
    }
  }
  gshcpu_render_geometry_release(&g, run->item_alloc);
  return scene_result(r);
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
  } else if (run->cs->fast_fill) {
    /* The fast fill coverage: every row is the same row of 255, painted as a span. */
    const size_t w = (size_t)(rect.x1 - rect.x0);
    memset(run->coverage, 0xFF, w);
    Painter p;
    GSHSCENE_Result r = painter_begin(&p, run, item, chain, count, frame, rect, whole);
    for (int64_t y = rect.y0; r == GSHSCENE_OK && !p.refused && y < rect.y1; y++) {
      painter_span(&p, y, rect.x0, w, run->coverage);
    }
    return r;
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
  Span_Paint sp = {item, chain, count, frame, {0}, 0};
  GSHSCENE_Result r = shape(run, item, chain, count, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count), run->coverage, &rect, &whole, &sp);
  if (sp.taken) {
    return r; /* painted as it was swept, or refused by its paint */
  }
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

/** The same receiver on bytes (fast8): the painter of the stroke's solid paint, the runs shifted to the destination. */
typedef struct Sparse8 {
  Painter * painter;
  int64_t dx, dy;
} Sparse8;

static void sparse8_span(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  const Sparse8 * sp = ctx;
  painter_span(sp->painter, y + sp->dy, x + sp->dx, n, m8);
}

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
    if (frame->general) {
      general_fill(run, frame, p, sp->src, gshcpu_k_level[m]);
    } else {
      apply_src_over(&frame->layer[4u * p], sp->src, gshcpu_k_level[m]);
    }
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
  /* The scratch of the first part's memory is the one the step works in whatever the part (cover.c). */
  GSHCPU_Exact_Scratch * sc = &run->exact[0].scratch;
  gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_events), sc->events);
  gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_end_evals), sc->end_evals);
  gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_crossings), sc->crossings);
  gshcpu_stroke_cache_raise(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_pieces_max), sc->pieces_max);
  gshcpu_stroke_cache_raise(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_distinct_max), sc->distinct_max);
  gshcpu_stroke_cache_raise(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_crossings_max), sc->crossings_max);
  gshcpu_stroke_cache_raise(run->cs->cache, offsetof(GSHCPU_Render_Counters, exact_winding_max), sc->wmax_max);
  sc->events = 0;
  sc->end_evals = 0;
  sc->crossings = 0;
  sc->pieces_max = 0;
  sc->distinct_max = 0;
  sc->crossings_max = 0;
  sc->wmax_max = 0;
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
  uint64_t work = points + elements * (uint64_t)GSHGEOM_STROKE_MAX_DEPTH + (path ? 0u : elements);
  if (item->style && gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_HATCH) {
    /* HX12: each line of the use is tested against the region's elements (the lines are counted whether the expansion was made or found) */
    uint64_t trim;
    if (!gcu_safe_mul_u64(s->meta.lines, elements ? elements : 1u, &trim) || !gcu_safe_add_u64(work, trim, &work)) {
      return UINT64_MAX;
    }
  }
  return work;
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
static void write_target(Run * run);

static GSHSCENE_Result stroke_item(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Frame * frame = &run->frames[run->top];
  Cpu_Sink * cs = run->cs;
  GSHGEOM_Map composed;
  run->maps[count - 1] = item->map;
  if (gshgeom_map_compose_chain(run->maps, count, target_map(run), &composed) != GSHGEOM_OK) {
    return refuse(run, item, chain, count); /* SC4: a composed map that cannot be formed is the sink's own limit */
  }
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_reset(&run->arena);
  }
  /* SC7: the cache serves a render that allocates, never one that takes its memory from the caller (S9). */
  GSHCPU_Stroke_Cache * cache = !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;
  GSHCPU_Stroke s;
  GSHCPU_Result opened =
      gshcpu_render_stroke_open_lines(cache, run->item_alloc, &cs->geometry, cs->max_hatch_lines, item, &composed, &s);
  if (opened == GSHCPU_ERR_LIMIT) {
    return refuse(run, item, chain, count);
  }
  if (opened != GSHCPU_OK) {
    return scene_result(opened);
  }
  if (item->style && gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_HATCH && s.drawn) {
    gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, hatches));
    gshcpu_stroke_cache_add(cs->cache, offsetof(GSHCPU_Render_Counters, hatch_lines), s.meta.lines);
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
  int solid = item->paint.kind != GSHSCENE_PAINT_GRADIENT && item->paint.kind != GSHSCENE_PAINT_IMAGE && item->paint.kind != GSHSCENE_PAINT_PATTERN;
  /* AA2: a stroke that asks for no antialiasing is covered by the centers of its pixels whichever tier it would take (the sweeps are areas). */
  const int center = item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE;
  if (solid && !center && cs->fast && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) && gshcpu_render_stroke_sparse_ok(&s, cs->max_deposits, NULL)) {
    /* The fast stroke coverage: the lean cover, the pixels of its runs composed like the sparse sweep's. */
    double src[4];
    source_of(run, item, frame, src);
    gshcpu_render_stroke_rects(&s, reach, wreach, &rect, &whole);
    const GSHGEOM_Flat_Path * body = gshcpu_render_stroke_cover_path(&s);
    const size_t scratch = run->layout.acc_bytes;
    acc_clean(run, gshcpu_lean_cover_extent((size_t)(rect.x1 - rect.x0), (size_t)(rect.y1 - rect.y0), scratch));
    if (run->direct8) {
      /* Strokes alone onto an 8-bit premultiplied target: composed in integers straight into its bytes. */
      uint8_t c[4];
      for (int i = 0; i < 4; i++) {
        c[i] = gshcpu_render_store8(src[i]);
      }
      for (int i = 0; i < 3; i++) {
        c[i] = c[i] > c[3] ? c[3] : c[i]; /* premultiplied: no channel past the alpha */
      }
      int queued = 0;
      r = GSHCPU_OK;
      if (c[3] != 0 && run->batch) {
        /* Queued, and drawn with the others over stripes of the rows: its outline is copied, so it is closed below as ever. */
        queued = gshcpu_batch_add(run->batch, body, s.shift_x, s.shift_y, rect, c, &r);
      }
      if (c[3] != 0 && queued == 0 && r == GSHCPU_OK) {
        GSHCPU_Direct8 d8;
        d8.pixels = run->pixels;
        d8.stride = run->stride;
        d8.dx = s.shift_x;
        d8.dy = s.shift_y;
        gshcpu_direct8_color(&d8, c);
        r = run->batch ? gshcpu_batch_flush(run->batch) : GSHCPU_OK; /* what was queued comes first */
        if (r == GSHCPU_OK) {
          r = gshcpu_render_lean_cover(body, s.shift_x, s.shift_y, rect, run->acc, scratch, run->coverage, gshcpu_direct8_span, &d8);
        }
      }
    } else if (frame->bytes) {
      Painter painter;
      const uint64_t counted = run->work;
      (void)painter_begin(&painter, run, item, chain, count, frame, rect, whole); /* a solid paint is always ready */
      run->work = counted; /* the area is added below */
      Sparse8 sp8 = {&painter, s.shift_x, s.shift_y};
      r = gshcpu_render_lean_cover(body, s.shift_x, s.shift_y, rect, run->acc, scratch, run->coverage, sparse8_span, &sp8);
    } else {
      widen_w(run, frame, rect, whole);
      count_general_fill(run, frame);
      Sparse sp = {run, frame, src, s.shift_x, s.shift_y};
      r = gshcpu_render_lean_cover(body, s.shift_x, s.shift_y, rect, run->acc, scratch, run->coverage, sparse_span, &sp);
    }
    run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
    if (r == GSHCPU_OK) {
      gshcpu_stroke_cache_count(cs->cache, s.hairline ? offsetof(GSHCPU_Render_Counters, sparse_hairline)
                                                      : offsetof(GSHCPU_Render_Counters, sparse_plain));
      gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, strokes));
    }
    gshcpu_render_stroke_close(cache, &s);
    return r == GSHCPU_OK ? GSHSCENE_OK : scene_result(r);
  }
  if (!solid && !center && item->paint.kind != GSHSCENE_PAINT_PATTERN && cs->fast_fill && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) &&
      gshcpu_render_stroke_sparse_ok(&s, cs->max_deposits, NULL)) {
    /* A stroke of a gradient with the fast coverage: the lean cover of its outline into the coverage of the rectangle, which the
     * gradient is then painted through as through a fill's. */
    if (run->batch) {
      GSHCPU_Result fr = gshcpu_batch_flush(run->batch);
      if (fr != GSHCPU_OK) {
        gshcpu_render_stroke_close(cache, &s);
        return scene_result(fr);
      }
    }
    gshcpu_render_stroke_rects(&s, reach, wreach, &rect, &whole);
    if (gshcpu_render_rect_empty(rect)) {
      rect.x1 = rect.x0;
      rect.y1 = rect.y0;
    }
    const GSHGEOM_Flat_Path * body = gshcpu_render_stroke_cover_path(&s);
    const size_t scratch = run->layout.acc_bytes;
    acc_clean(run, gshcpu_lean_fill_extent((size_t)(rect.x1 - rect.x0), (size_t)(rect.y1 - rect.y0), scratch));
    Painter painter;
    GSHSCENE_Result result = painter_begin(&painter, run, item, chain, count, frame, rect, whole);
    if (result == GSHSCENE_OK && !painter.refused) {
      r = gshcpu_render_lean_fill_spans(&body, 1, 0, s.shift_x, s.shift_y, rect, run->acc, scratch, painter_span, &painter);
      if (r != GSHCPU_OK) {
        result = scene_result(r);
      } else {
        gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, strokes));
      }
    }
    gshcpu_render_stroke_close(cache, &s);
    return result;
  }
  if (run->batch) {
    /* a stroke that is not queued reads the target through the layer: the ones queued before it come first */
    GSHCPU_Result fr = gshcpu_batch_flush(run->batch);
    if (fr != GSHCPU_OK) {
      gshcpu_render_stroke_close(cache, &s);
      return scene_result(fr);
    }
  }
  if (solid && !center && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) && gshcpu_render_stroke_sparse_ok(&s, cs->max_deposits, exact)) {
    /* FT-2 (SC8): the sweep gives only the pixels that can be nonzero; the rest the general loop would skip. */
    acc_clean(run, run->layout.acc_exact);
    gshcpu_render_stroke_rects(&s, reach, wreach, &rect, &whole);
    double src[4];
    source_of(run, item, frame, src);
    widen_w(run, frame, rect, whole);
    count_general_fill(run, frame);
    Sparse sp = {run, frame, src, s.shift_x, s.shift_y};
    GSHCPU_Rect drawn;
    GSHCPU_Rect counted;
    /* a deposit's pixel is composed when the band is swept, a while after: ask for it now (its place in the layer, from the
     * cover's frame, which is the region's moved by the stroke's shift) */
    GSHCPU_Prefetch pf = {
        (char *)frame->layer + (intptr_t)sizeof(double) * 4 *
            (((intptr_t)(sp.dy - run->layout.region.y0)) * (intptr_t)run->layout.width + (intptr_t)(sp.dx - run->layout.region.x0)),
        (int64_t)run->layout.width * (int64_t)(4 * sizeof(double)), (int64_t)(4 * sizeof(double))};
    r = gshcpu_render_stroke_cover_sparse(&s, reach, wreach, cs->max_deposits, run->acc, exact, run->coverage, sparse_span,
        &sp, &drawn, &counted, &cells, &pf);
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
  GSHCPU_Stroke_Buffers buffers = {run->coverage, run->cov_aligned, run->cov_clip, run->cov_result, center, NULL};
  r = gshcpu_render_stroke_cover(&s, reach, wreach, cs->max_deposits, run->acc,
      2u * ((size_t)run->layout.width + 1u) * GSHCPU_RENDER_BAND_ROWS, exact, (cs->tiers & GSHCPU_RENDER_TIER_JOINT) != 0, &buffers, &cov, &rect, &whole, &cells);
  run->acc_zero = 0;
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

/**
 * A stroke.  In a render whose strokes compose straight into the target's bytes, one that goes through the double layer instead (a
 * stroke past the cap of the sparse tier) is stored at its end and the layer given up, so that the next stroke finds the target
 * as the layer left it.
 */
static GSHSCENE_Result stroke(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  GSHSCENE_Result result = stroke_item(run, item, chain, count);
  if (run->direct8 && !run->fast8) {
    write_target(run);
    memset(&run->frames[0].dirty, 0, sizeof run->frames[0].dirty);
  }
  return result;
}


/**
 * The pixels a union's member can touch: the floor and the ceiling of its device box, or none when the box is empty or has no area in
 * whole pixels (a hairline on a pixel boundary, a member of one point or two).  Such a member contributes nothing to the union and is not
 * swept, measured or refused for its geometry by either tier (CG4: the general tier skips it; the fast tier must not refuse a push for it).
 * @return 1 and the rectangle, or 0.
 */
static int union_member_pixels(const GSHGEOM_Device_Box * db, GSHCPU_Rect * m) {
  if (db->empty) {
    return 0;
  }
  m->x0 = box_floor(db->x0);
  m->y0 = box_floor(db->y0);
  m->x1 = box_ceil(db->x1);
  m->y1 = box_ceil(db->y1);
  return !gshcpu_render_rect_empty(*m);
}

/**
 * CU18, CU19: a union clip.  Each member is swept by the dense cover, the magnitude fold and no final shift, into one plane of
 * `uint16` over the union's rectangle, each pixel held at 8192; when the last member is in, each plane value becomes the clip's byte
 * `min(255, s >> 5)`.  The members come from the one member item (CU14); a member refused refuses the whole push.
 */
static GSHSCENE_Result union_push_fast(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Cpu_Sink * cs = run->cs;
  Frame * frame = &run->frames[run->top];
  Clip * clip = &run->clips[run->clip_count];
  clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;
  const GSHCPU_Rect reach = reach_of(run, frame->clip_base, run->clip_count);
  const GSHCPU_Rect whole = whole_reach_of(run, frame->clip_base, run->clip_count);
  const size_t n = gshscene_resource_clip_union_count(item->resource);
  GSHCPU_Rect bound = {0, 0, 0, 0};
  int have = 0;
  for (size_t i = 0; i < n; i++) {
    GSHGEOM_Device_Box db;
    GSHSCENE_Result br = gshscene_clip_union_member_box(item, i, run->maps, count - 1, target_map(run), &db);
    if (br == GSHSCENE_ERR_LIMIT) {
      return refuse(run, item, chain, count); /* an unprovable member: the plan refused the push unless the sink is general */
    }
    if (br != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    GSHCPU_Rect m;
    if (!union_member_pixels(&db, &m)) {
      continue;
    }
    if (!have) {
      bound = m;
      have = 1;
    } else {
      bound.x0 = m.x0 < bound.x0 ? m.x0 : bound.x0;
      bound.y0 = m.y0 < bound.y0 ? m.y0 : bound.y0;
      bound.x1 = m.x1 > bound.x1 ? m.x1 : bound.x1;
      bound.y1 = m.y1 > bound.y1 ? m.y1 : bound.y1;
    }
  }
  GSHCPU_Rect rect = have ? gshcpu_render_rect_meet(bound, reach) : reach;
  GSHCPU_Rect wrect = have ? gshcpu_render_rect_meet(bound, whole) : whole;
  if (!have || gshcpu_render_rect_empty(rect)) {
    rect.x1 = rect.x0;
    rect.y1 = rect.y0;
  }
  if (!have) {
    wrect.x1 = wrect.x0;
    wrect.y1 = wrect.y0;
  }
  clip->rect = rect;
  clip->whole = wrect;
  cs->counters->union_fast++;
  if (gshcpu_render_rect_empty(wrect)) {
    run->clip_count++; /* the empty region: every pixel weighs 0 (CU4) */
    return GSHSCENE_OK;
  }
  const size_t width = (size_t)(rect.x1 - rect.x0);
  const size_t pixels = width * (size_t)(rect.y1 - rect.y0);
  const int in_arena = run->item_alloc == &run->arena.self;
  if (in_arena) {
    gshcpu_render_arena_reset(&run->arena);
  }
  uint16_t * plane = NULL;
  if (pixels > 0) {
    plane = run->item_alloc->calloc_fn(run->item_alloc->ctx, pixels, sizeof *plane); /* zero: cleared for every push */
    if (!plane) {
      return GSHSCENE_ERR_OOM;
    }
  }
  const size_t mark = in_arena ? run->arena.used : 0;
  GSHSCENE_Result result = GSHSCENE_OK;
  int refused = 0;
  for (size_t i = 0; i < n && result == GSHSCENE_OK && !refused; i++) {
    GSHGEOM_Device_Box mdb;
    GSHCPU_Rect mpx;
    if (gshscene_clip_union_member_box(item, i, run->maps, count - 1, target_map(run), &mdb) != GSHSCENE_OK ||
        !union_member_pixels(&mdb, &mpx)) {
      continue; /* the box was formed by the first loop: a member with no pixels is skipped, as the general tier skips it */
    }
    GSHSCENE_Item member;
    if (gshscene_clip_union_member_item(item, i, &member) != GSHSCENE_OK) {
      result = GSHSCENE_ERR_INTERNAL;
      break;
    }
    if (in_arena) {
      gshcpu_render_arena_rewind(&run->arena, mark);
    }
    GSHCPU_Item_Geometry g;
    GSHCPU_Result r = gshcpu_render_geometry(&member, run->maps, count - 1, target_map(run), run->item_alloc, &cs->geometry, &g);
    run->work += g.points;
    if (r == GSHCPU_OK) {
      const GSHGEOM_Flat_Path * const * paths = (const GSHGEOM_Flat_Path * const *)g.paths;
      GSHCPU_Rect mrect = gshcpu_render_rect_meet(g.box, rect);
      if (gshcpu_render_rect_empty(mrect)) {
        mrect.x1 = mrect.x0;
        mrect.y1 = mrect.y0;
      }
      GSHCPU_Rect walk = gshcpu_render_rect_meet(g.box, wrect);
      if (!gshcpu_render_rect_empty(walk)) {
        uint64_t cells = 0;
        /* As a fill's (S4): unless the bound says the cap cannot be met over the whole, count only where it is drawn. */
        if ((mrect.x0 != walk.x0 || mrect.y0 != walk.y0 || mrect.x1 != walk.x1 || mrect.y1 != walk.y1) &&
            gshcpu_render_cells_bound(paths, g.count, walk, cs->max_deposits) <= cs->max_deposits) {
          walk = mrect;
        }
        r = gshcpu_render_cover_area(paths, g.count, member.fill_rule, mrect, walk, plane, rect, run->acc, cs->max_deposits, &cells);
        run->acc_zero = 0;
        run->work += cells;
      }
      cs->counters->union_sweeps++;
    }
    gshcpu_render_geometry_release(&g, run->item_alloc);
    if (r == GSHCPU_ERR_LIMIT) {
      refused = 1;
    } else if (r != GSHCPU_OK) {
      result = scene_result(r);
    }
  }
  if (result == GSHSCENE_OK && !refused && plane) {
    for (size_t k = 0; k < pixels; k++) {
      const uint32_t level = (uint32_t)plane[k] >> 5; /* CU2: quantized once, on the sum */
      clip->coverage[k] = (uint8_t)(level > 255u ? 255u : level);
    }
    run->work += pixels;
  }
  if (plane && !in_arena) {
    run->item_alloc->free_fn(run->item_alloc->ctx, plane);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (refused) {
    return refuse(run, item, chain, count); /* a member over a cap refuses the push: a union is never drawn with one left out */
  }
  run->clip_count++;
  return GSHSCENE_OK;
}


/** The records of a group's kept pixels in the order of their pixel (a heap sort: no recursion, no allocation); the members of one pixel are together, in no order (the union of regions is the same in any). */
static int record_less(const GSHCPU_Group_Record * a, const GSHCPU_Group_Record * b) {
  return a->cell < b->cell;
}

static void sort_records(GSHCPU_Group_Record * v, size_t n) {
  for (size_t start = n / 2; start-- > 0;) {
    size_t root = start;
    for (;;) {
      size_t child = 2 * root + 1;
      if (child >= n) {
        break;
      }
      if (child + 1 < n && record_less(&v[child], &v[child + 1])) {
        child++;
      }
      if (!record_less(&v[root], &v[child])) {
        break;
      }
      GSHCPU_Group_Record t = v[root];
      v[root] = v[child];
      v[child] = t;
      root = child;
    }
  }
  for (size_t end = n; end-- > 1;) {
    GSHCPU_Group_Record t = v[0];
    v[0] = v[end];
    v[end] = t;
    size_t root = 0;
    for (;;) {
      size_t child = 2 * root + 1;
      if (child >= end) {
        break;
      }
      if (child + 1 < end && record_less(&v[child], &v[child + 1])) {
        child++;
      }
      if (!record_less(&v[root], &v[child])) {
        break;
      }
      GSHCPU_Group_Record u = v[root];
      v[root] = v[child];
      v[child] = u;
      root = child;
    }
  }
}

/**
 * Where a union's members come from (CU21): the maps of the enclosing instances and the map the members are composed with, and, for a
 * union in a pattern's tile, the cell of the copy being formed (its quadrilateral under the canonical map), to which every member's
 * outline is cut by CK1 before anything is swept.  Outside a tile `quad` is NULL and a member is its own geometry.
 */
typedef struct Union_Src {
  GSHGEOM_Map * maps;
  const GSHGEOM_Map * tmap;
  const GSHGEOM_Device_Point * quad;
} Union_Src;

/** One member's outline as the sweeps take it: the geometry, or the geometry cut to the cell. */
typedef struct Union_Member {
  GSHCPU_Item_Geometry g;
  GSHGEOM_Flat_Path ** cut;                   /* the paths cut to the cell (NULL outside a tile) */
  size_t cut_n;                               /* ... how many of them have a piece in the cell */
  const GSHGEOM_Flat_Path * const * paths;    /* what to sweep */
  size_t count;
  GSHCPU_Rect box;                            /* the pixels the outline touches (in a tile, the uncut outline's: the sweeps are met with a rectangle inside the cell's) */
} Union_Member;

static void union_member_close(Run * run, Union_Member * m) {
  for (size_t k = 0; k < m->cut_n; k++) {
    gshgeom_flat_path_destroy(m->cut[k]);
  }
  if (m->cut) {
    run->item_alloc->free_fn(run->item_alloc->ctx, m->cut);
  }
  gshcpu_render_geometry_release(&m->g, run->item_alloc);
  memset(m, 0, sizeof *m);
}

/**
 * The outline of member @p index of the push, with the work it costs counted, cut to the cell when there is one.  @p m is always safe
 * to hand to ::union_member_close afterwards.
 */
static GSHCPU_Result union_member_open(Run * run, const GSHSCENE_Item * item, size_t index, size_t count, const Union_Src * src,
    GSHSCENE_Item * member, Union_Member * m) {
  memset(m, 0, sizeof *m);
  if (gshscene_clip_union_member_item(item, index, member) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL;
  }
  GSHCPU_Result r = gshcpu_render_geometry(member, src->maps, count - 1, src->tmap, run->item_alloc, &run->cs->geometry, &m->g);
  run->work += m->g.points;
  m->paths = (const GSHGEOM_Flat_Path * const *)m->g.paths;
  m->count = m->g.count;
  m->box = m->g.box;
  if (r != GSHCPU_OK || !src->quad) {
    return r;
  }
  m->cut = run->item_alloc->calloc_fn(run->item_alloc->ctx, m->g.count ? m->g.count : 1u, sizeof *m->cut);
  if (!m->cut) {
    return GSHCPU_ERR_OOM;
  }
  m->paths = (const GSHGEOM_Flat_Path * const *)(void *)m->cut;
  m->count = 0;
  for (size_t k = 0; k < m->g.count; k++) {
    if (!m->g.paths[k]) {
      continue;
    }
    GSHGEOM_Flat_Path * piece = NULL;
    const GSHGEOM_Result gr = gshgeom_flat_path_clip_convex(m->g.paths[k], src->quad, run->item_alloc, &run->cs->geometry, &piece);
    if (gr != GSHGEOM_OK) {
      return gr == GSHGEOM_ERR_OOM ? GSHCPU_ERR_OOM : GSHCPU_ERR_LIMIT;
    }
    if (gshgeom_flat_path_subpath_count(piece) == 0) {
      gshgeom_flat_path_destroy(piece);
      continue;
    }
    m->cut[m->cut_n++] = piece;
  }
  m->count = m->cut_n;
  return GSHCPU_OK;
}

/**
 * CG7: the group named @p root (two or more members) made exact into @p b->gplane, row by row over its rectangle @p rg: a first pass
 * over the members counts their pieces in every pixel; a second adds their dense areas and keeps, for the pixels that hold two or more
 * pieces of the group in all, every member's pieces there; and then the step replaces the area of each such pixel with the exact area of
 * the union of the members' regions by their own rules (or the whole pixel, when a member without a piece in it covers it whole).
 * @p refused is set when the push is the sink's resource limit.
 */
static GSHSCENE_Result union_group(Run * run, const GSHSCENE_Item * item, size_t count, const Union_Src * src, const Union_Plan * pl,
    uint16_t root, GSHCPU_Rect rg, const Union_Blocks * b, GSHCPU_Exact * ex, GSHCPU_Group_Scratch * gs, int * refused) {
  Cpu_Sink * cs = run->cs;
  const size_t pixels = (size_t)(rg.x1 - rg.x0) * (size_t)(rg.y1 - rg.y0);
  memset(b->gplane, 0, pixels * sizeof *b->gplane);
  memset(b->mark, 0, pixels);
  GSHCPU_Group_Capture cap;
  memset(&cap, 0, sizeof cap);
  cap.records = b->records;
  cap.pieces = b->pieces;
  cap.piece_cap = b->piece_cap;
  cap.mark = b->mark;
  cap.rect = rg;
  ex->max_pieces = cs->max_exact;
  ex->max_crossings = cs->max_crossings;
  ex->eps = GSHCPU_RENDER_EXACT_EPS;
  uint32_t parts = 0;
  for (uint16_t i = pl->head[root]; i != UNION_NONE; i = pl->next[i], parts++) {
    GSHSCENE_Item member;
    if (gshscene_clip_union_member_item(item, i, &member) != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    b->group_rules[parts] = member.fill_rule == GSHGEOM_FILL_EVEN_ODD;
  }
  const int in_arena = run->item_alloc == &run->arena.self;
  const size_t mark = in_arena ? run->arena.used : 0; /* the geometry of one member is live at a time (the measure takes the most any one takes) */
  for (int pass = 0; pass < 2 && !*refused; pass++) {
    uint32_t part = 0;
    for (uint16_t i = pl->head[root]; i != UNION_NONE && !*refused; i = pl->next[i], part++) {
      if (gshcpu_render_rect_empty(pl->rect[i])) {
        continue;
      }
      GSHSCENE_Item member;
      Union_Member g;
      if (in_arena) {
        gshcpu_render_arena_rewind(&run->arena, mark);
      }
      GSHCPU_Result r = union_member_open(run, item, i, count, src, &member, &g);
      if (r == GSHCPU_OK) {
        GSHCPU_Rect mrect = gshcpu_render_rect_meet(g.box, rg);
        if (!gshcpu_render_rect_empty(mrect)) {
          uint64_t cells = 0;
          cap.part = part;
          if (pass == 0) {
            r = gshcpu_render_cover_marks(g.paths, g.count, mrect, &cap, ex, cs->max_deposits, &cells);
          } else {
            r = gshcpu_render_cover_group(g.paths, g.count, member.fill_rule, mrect, b->gplane, rg, &cap, ex, b->acc, cs->max_deposits,
                &cells);
            cs->counters->union_sweeps++;
          }
          run->work += cells;
        }
      }
      union_member_close(run, &g);
      if (r == GSHCPU_ERR_LIMIT) {
        *refused = 1;
      } else if (r != GSHCPU_OK) {
        return scene_result(r);
      }
    }
  }
  run->work += ex->work;
  ex->work = 0;
  if (*refused) {
    return GSHSCENE_OK;
  }
  GSHCPU_Group_Record * rec = cap.records;
  const size_t total = cap.record_n;
  sort_records(rec, total);
  run->work += (uint64_t)total;
  cs->counters->union_pieces += cap.piece_n;
  GSHCPU_Exact_Params params;
  memset(&params, 0, sizeof params);
  params.mode = GSHCPU_EXACT_GROUP;
  params.max_crossings = cs->max_crossings;
  params.rule_of = b->cell_rules;
  params.gscratch = gs;
  for (size_t a = 0; a < total && !*refused;) {
    size_t e = a;
    while (e < total && rec[e].cell == rec[a].cell) {
      e++;
    }
    int64_t area = 8192;
    if (!(b->mark[rec[a].cell] & GSHCPU_GROUP_FULL)) {
      GSHCPU_Exact_Cell cell;  /* a pixel with more pieces, of all the members, than the scratch holds (CG9) is the step's limit */
      memset(&cell, 0, sizeof cell);
      for (size_t t = a; t < e; t++) {
        const size_t k = t - a;
        b->cell_pieces[k] = cap.pieces + rec[t].first;
        b->cell_counts[k] = rec[t].count;
        b->cell_a_left[k] = rec[t].a_left;
        b->cell_dense[k] = rec[t].dense;
        b->cell_rules[k] = b->group_rules[rec[t].part];
      }
      cell.gpieces = b->cell_pieces;
      cell.gcount = b->cell_counts;
      cell.ga_left = b->cell_a_left;
      cell.gdense = b->cell_dense;
      params.parts = (int)(e - a);
      uint64_t spent = 0;
      int status = gshcpu_exact_cell(&params, &gs->base, &cell, &area, &spent);
      run->work += spent;
      if (status != GSHCPU_EXACT_OK) {
        if (status == GSHCPU_EXACT_IDENTITY) {
          ex->failures++;
        }
        *refused = 1; /* a cell past the crossings budget, or one the step cannot vouch for: the push is the resource limit */
        break;
      }
      cs->counters->union_exact++;
    }
    b->gplane[rec[a].cell] = (uint16_t)area; /* 0 <= area <= 8192: the step's floor of a subset of the pixel (CG6), or the whole pixel */
    a = e;
  }
  return GSHSCENE_OK;
}

/** The area of a union (CG1 to CG12) as the general tier forms it: the plan and the blocks that hold it, live until ::union_area_free. */
typedef struct Union_Area {
  Union_Plan pl;     /* the rectangles are valid after the free; the arrays are not */
  void * block;
  Union_Blocks b;    /* `b.plane` holds the area (a `uint16` per pixel of `pl.wide`, not held at 8192: CG3) */
  int live;          /* the plane is there to be read */
} Union_Area;

static void union_area_free(Run * run, Union_Area * ua) {
  if (ua->live) {
    union_free(run->item_alloc, &ua->b);
  }
  if (ua->block) {
    run->item_alloc->free_fn(run->item_alloc->ctx, ua->block);
  }
  ua->live = 0;
  ua->block = NULL;
}

/**
 * CG1 to CG12: the area of a union by the general tier, the members partitioned into groups by their boxes (CG2); a group of one is
 * swept like the fast tier's member into the shared plane, a group of several is made exact (union_group) and added.  The tier works
 * over the whole render's rectangle of the union @p whole, whatever the damage, so what it refuses never depends on the damage (S4); a
 * member over a cap, or a pixel over a limit of the exact step, refuses the union.  The members come from @p src (the render's own, or
 * a copy of a pattern's tile cut to its cell: CU21), and @p reach is where the caller reads the plane.
 *
 * @return ::GSHSCENE_OK with the plane in `ua->b.plane` over `ua->pl.wide` when `ua->live` (else the union is empty there: nothing of it
 * is in the whole render); ::GSHSCENE_ERR_LIMIT when the union is the sink's resource limit; any other result as the sweeps give it.
 * Nothing is live after a result that is not ::GSHSCENE_OK.
 */
static GSHSCENE_Result union_area(Run * run, const GSHSCENE_Item * item, size_t count, const Union_Src * src, GSHCPU_Rect reach,
    GSHCPU_Rect whole, Union_Area * ua) {
  Cpu_Sink * cs = run->cs;
  memset(ua, 0, sizeof *ua);
  const int in_arena = run->item_alloc == &run->arena.self;
  GSHSCENE_Result result = union_plan(item, src->maps, count - 1, src->tmap, reach, whole,
      (cs->tiers & GSHCPU_RENDER_FORCE_ONE_GROUP) != 0, run->item_alloc, &ua->pl, &ua->block);
  if (result != GSHSCENE_OK) {
    union_area_free(run, ua);
    return result;
  }
  Union_Plan * pl = &ua->pl;
  cs->counters->union_general++;
  if (gshcpu_render_rect_empty(pl->wide)) {
    union_area_free(run, ua); /* the empty region: every pixel weighs 0 (CU4) */
    return GSHSCENE_OK;
  }
  result = union_alloc(cs, pl, run->item_alloc, &ua->b);
  if (result != GSHSCENE_OK) {
    union_area_free(run, ua);
    return result;
  }
  ua->live = 1;
  Union_Blocks * b = &ua->b;
  GSHCPU_Exact ex;
  GSHCPU_Group_Scratch gs;
  if (pl->multi) {
    uintptr_t at = ((uintptr_t)b->exact + 15u) & ~(uintptr_t)15u;
    gshcpu_exact_group_init(&ex, &gs, (void *)at, pl->max_width, cs->max_exact, pl->max_parts);
  }
  const size_t mark = in_arena ? run->arena.used : 0;
  const GSHCPU_Rect W = pl->wide;
  const int64_t Ww = W.x1 - W.x0;
  int refused = 0;
  for (size_t r = 0; r < pl->n && result == GSHSCENE_OK && !refused; r++) {
    if (pl->root[r] != r) {
      continue;
    }
    if (in_arena) {
      gshcpu_render_arena_rewind(&run->arena, mark);
    }
    if (pl->next[r] == UNION_NONE) {
      /* A group of one: its own area, as the fast tier adds it (CG3). */
      if (gshcpu_render_rect_empty(pl->rect[r])) {
        continue;
      }
      GSHSCENE_Item member;
      Union_Member g;
      GSHCPU_Result gr = union_member_open(run, item, r, count, src, &member, &g);
      if (gr == GSHCPU_OK) {
        GSHCPU_Rect mrect = gshcpu_render_rect_meet(g.box, W);
        if (!gshcpu_render_rect_empty(mrect)) {
          uint64_t cells = 0;
          gr = gshcpu_render_cover_area(g.paths, g.count, member.fill_rule, mrect, mrect, b->plane, W, b->acc, cs->max_deposits, &cells);
          run->work += cells;
        }
        cs->counters->union_sweeps++;
      }
      union_member_close(run, &g);
      if (gr == GSHCPU_ERR_LIMIT) {
        refused = 1;
      } else if (gr != GSHCPU_OK) {
        result = scene_result(gr);
      }
      continue;
    }
    const GSHCPU_Rect rg = union_group_rect(pl, (uint16_t)r, W);
    if (gshcpu_render_rect_empty(rg)) {
      continue;
    }
    cs->counters->union_groups++;
    result = union_group(run, item, count, src, pl, (uint16_t)r, rg, b, &ex, &gs, &refused);
    if (result == GSHSCENE_OK && !refused) {
      /* The group's area, added into the plane and held at the whole pixel (CG3). */
      for (int64_t y = rg.y0; y < rg.y1; y++) {
        const uint16_t * from = b->gplane + (size_t)((y - rg.y0) * (rg.x1 - rg.x0));
        uint16_t * to = b->plane + (size_t)((y - W.y0) * Ww + (rg.x0 - W.x0));
        for (int64_t x = 0; x < rg.x1 - rg.x0; x++) {
          /* Not held at 8192: the groups' boxes are interior-disjoint, so the areas of their regions in a pixel add to at most the pixel, and the
           * plane held at 8192 by the singletons' sweeps gains at most another 8192: 16384 at most, and the quantizing below takes a level past 255 as 255. */
          to[x] = (uint16_t)((uint32_t)to[x] + from[x]);
        }
      }
      run->work += (uint64_t)(rg.x1 - rg.x0) * (uint64_t)(rg.y1 - rg.y0);
    }
  }
  if (result != GSHSCENE_OK || refused) {
    union_area_free(run, ua);
    return result != GSHSCENE_OK ? result : GSHSCENE_ERR_LIMIT; /* a member over a cap, or a pixel over a limit, refuses the union: never drawn with one left out */
  }
  return GSHSCENE_OK;
}

/**
 * A union clip drawn by the general tier: its area (union_area) quantized once into the clip's bytes (CU2).
 */
static GSHSCENE_Result union_push_general(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Frame * frame = &run->frames[run->top];
  Clip * clip = &run->clips[run->clip_count];
  clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;
  const int in_arena = run->item_alloc == &run->arena.self;
  if (in_arena) {
    gshcpu_render_arena_reset(&run->arena);
  }
  const Union_Src src = {run->maps, target_map(run), NULL};
  Union_Area ua;
  GSHSCENE_Result result = union_area(run, item, count, &src, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count), &ua);
  if (result == GSHSCENE_ERR_LIMIT) {
    return refuse(run, item, chain, count);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  clip->rect = ua.pl.clip;
  clip->whole = ua.pl.wide;
  if (ua.live) {
    const GSHCPU_Rect W = ua.pl.wide;
    const int64_t Ww = W.x1 - W.x0;
    const GSHCPU_Rect q = ua.pl.clip;
    const size_t qw = (size_t)(q.x1 - q.x0);
    for (int64_t y = q.y0; y < q.y1; y++) {
      const uint16_t * from = ua.b.plane + (size_t)((y - W.y0) * Ww + (q.x0 - W.x0));
      uint8_t * to = clip->coverage + (size_t)(y - q.y0) * qw;
      for (size_t x = 0; x < qw; x++) {
        const uint32_t level = (uint32_t)from[x] >> 5; /* CU2: quantized once, on the sum */
        to[x] = (uint8_t)(level > 255u ? 255u : level);
      }
    }
    run->work += (uint64_t)qw * (uint64_t)(q.y1 - q.y0);
    union_area_free(run, &ua);
  }
  run->clip_count++;
  return GSHSCENE_OK;
}

/** A union clip push: the fast tier for a union the predicate showed disjoint while the tier is on, else the general one (CG10). */
static GSHSCENE_Result union_push(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  return run->union_general ? union_push_general(run, item, chain, count) : union_push_fast(run, item, chain, count);
}

static GSHSCENE_Result clip_push(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
    return union_push(run, item, chain, count);
  }
  Frame * frame = &run->frames[run->top];
  Clip * clip = &run->clips[run->clip_count];
  clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;
  GSHSCENE_Result r = shape(run, item, chain, count, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count), clip->coverage, &clip->rect, &clip->whole, NULL);
  if (r == GSHSCENE_ERR_LIMIT) {
    return refuse(run, item, chain, count);
  }
  if (r != GSHSCENE_OK) {
    return r;
  }
  run->clip_count++;
  return GSHSCENE_OK;
}

static void group_push(Run * run, const GSHSCENE_Item * item, size_t index) {
  const Frame * parent = &run->frames[run->top];
  Frame * f = &run->frames[++run->top];
  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u; /* made ready as it is drawn on (widen) */
  f->group = item->group;
  f->space = gshscene_color_space_is_target(&item->group.blend_space) ? parent->space : item->group.blend_space;
  f->clip_base = run->clip_count;
  f->dirty.x0 = f->dirty.y0 = f->dirty.x1 = f->dirty.y1 = 0;
  f->bytes = run->fast8 && same_space(&f->space, &run->frames[0].space);
  f->px8 = (unsigned char *)(void *)f->layer;
  f->stride8 = 4u * run->layout.width;
  f->ox = run->layout.region.x0;
  f->oy = run->layout.region.y0;
  f->general = 0;
  if (run->general) {
    /* KC2, KC3: the group's flags decide its layer, not its parent's; a group alpha for a non-isolated group, a group shape for a group
     * with a knockout proper ancestor; the channels are the next free ones, as the groups open nest. */
    Gen_State * gs = gs_of(run);
    const Gen_Frame * pg = &gs->frames[run->top - 1u];
    Gen_Frame * g = &gs->frames[run->top];
    memset(g, 0, sizeof *g);
    g->nonisolated = item->group.isolation == (uint32_t)GSHSCENE_ISOLATION_NON_ISOLATED;
    g->knockout = item->group.knockout != 0;
    g->ko_anc = pg->ko_anc || pg->knockout;
    g->on = g->nonisolated || g->knockout || (run->cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;
    if (g->nonisolated) {
      g->ag = gs->ag + gs->ag_used++ * run->layout.pixels;
      gshcpu_render_convert_prepare(&g->from_parent, &parent->space, &f->space);
    }
    if (g->ko_anc) {
      g->fg = gs->fg + gs->fg_used++ * run->layout.pixels;
    }
    /* A plain layer under a knockout group still steps its group shape with each element (KC3), by the same step: with its backdrop
     * the layer itself the step is the plain composite's, bit for bit. */
    f->general = g->on || g->fg != NULL;
    if (g->on) {
      gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, general_layers));
    }
  }
  run->groups_open++;
  run->work += run->layout.pixels;
  if (run->masked) {
    Mask_Frame * mf = &ms_of(run)->frames[run->top];
    memset(&mf->whole_dirty, 0, sizeof mf->whole_dirty);
    mf->push = index;
  }
}

/** The pixel (x, y) of @p f's layer, in bytes (fast8). */
static unsigned char * pixel8(const Frame * f, int64_t x, int64_t y) {
  return f->px8 + (size_t)(y - f->oy) * f->stride8 + 4u * (size_t)(x - f->ox);
}

/** The premultiplied bytes @p b as doubles. */
static void decode8(const unsigned char * b, double d[4]) {
  for (int i = 0; i < 4; i++) {
    d[i] = gshcpu_k_level[b[i]];
  }
}

/** The premultiplied doubles @p d as bytes, no channel past the alpha. */
static void encode8(const double d[4], unsigned char * b) {
  unsigned char c[4];
  for (int i = 0; i < 4; i++) {
    c[i] = gshcpu_render_store8(d[i]);
  }
  for (int i = 0; i < 3; i++) {
    c[i] = c[i] > c[3] ? c[3] : c[i];
  }
  memcpy(b, c, 4);
}

/** The value of the transfer table at the straight color @p c (held to 0..1), by interpolation. */
static inline double xfer_value(const Run * run, double c) {
  if (!(c > 0.0)) {
    return (double)run->xfer[0];
  }
  if (c >= 1.0) {
    return (double)run->xfer[XFER_POINTS];
  }
  const double at_c = c * (double)XFER_POINTS;
  const int32_t i = (int32_t)at_c; /* 32-bit: no x87 (AD-17) */
  const double f = at_c - (double)i;
  return (double)run->xfer[i] + f * ((double)run->xfer[i + 1] - (double)run->xfer[i]);
}

/** Make the transfer table from @p from to @p to; 0 where one of the transfers is not the linear or the sRGB (the generic loop converts). */
static int xfer_prepare(Run * run, const GSHSCENE_Color_Space * from, const GSHSCENE_Color_Space * to) {
  if ((from->transfer != GCOL_TRANSFER_LINEAR && from->transfer != GCOL_TRANSFER_SRGB) ||
      (to->transfer != GCOL_TRANSFER_LINEAR && to->transfer != GCOL_TRANSFER_SRGB)) {
    return 0;
  }
  if (!run->xfer_ready || !same_space(from, &run->xfer_from) || !same_space(to, &run->xfer_to)) {
    for (size_t i = 0; i <= XFER_POINTS; i++) {
      run->xfer[i] = (float)gshcpu_render_encode(to, gshcpu_render_decode(from, (double)i / (double)XFER_POINTS));
    }
    run->xfer_from = *from;
    run->xfer_to = *to;
    run->xfer_ready = 1;
  }
  return 1;
}

/**
 * group_pop with byte layers (fast8): between two byte layers with no clip and no conversion, a row at a time in integers
 * (source-over) or single-precision floats (a separable blend mode); every other pair through doubles, a pixel at a time.
 */
static GSHSCENE_Result group_pop8(Run * run) {
  const Frame * f = &run->frames[run->top];
  Frame * parent = &run->frames[run->top - 1u];
  uint32_t op = f->group.composite;
  int bounded = gshcpu_render_bounded(op);
  GSHCPU_Rect region = bounded ? f->dirty : run->layout.region;
  region = gshcpu_render_rect_meet(region, reach_of(run, parent->clip_base, f->clip_base));
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);
  const double o = f->group.opacity;
  widen(run, parent, region);
  const int plain = bounded && f->bytes && parent->bytes && c.same && parent->clip_base == f->clip_base;
  if (plain && op == GSHSCENE_COMPOSITE_SRC_OVER) {
    const uint32_t m_const = o >= 1.0 ? 255u : gshcpu_render_store8(o);
    for (int64_t y = region.y0; y < region.y1; y++) {
      gshcpu_direct8_span_pixels(pixel8(parent, region.x0, y), (const uint32_t *)(const void *)pixel8(f, region.x0, y), NULL, m_const,
          (size_t)(region.x1 - region.x0));
    }
  } else if (bounded && !f->bytes && parent->bytes && !c.same && !c.matrix && parent->clip_base == f->clip_base &&
      op == GSHSCENE_COMPOSITE_SRC_OVER && xfer_prepare(run, &f->space, &parent->space)) {
    /* A double layer in another transfer of the same gamut (linear light into the target's): the colors through a table. */
    const uint32_t m_const = o >= 1.0 ? 255u : gshcpu_render_store8(o);
    uint32_t src[256];
    for (int64_t y = region.y0; y < region.y1; y++) {
      for (int64_t x0 = region.x0; x0 < region.x1; x0 += 256) {
        const size_t len = (size_t)(region.x1 - x0 < 256 ? region.x1 - x0 : 256);
        const double * in = &f->layer[4u * at(run, x0, y)];
        for (size_t i = 0; i < len; i++, in += 4) {
          unsigned char b[4] = {0, 0, 0, 0};
          const double a = in[3];
          if (a > 0.0) {
            const double inv = 1.0 / a;
            for (int k = 0; k < 3; k++) {
              b[k] = gshcpu_render_store8(xfer_value(run, in[k] * inv) * a);
            }
            b[3] = gshcpu_render_store8(a);
            for (int k = 0; k < 3; k++) {
              b[k] = b[k] > b[3] ? b[3] : b[k];
            }
          }
          memcpy(&src[i], b, 4);
        }
        gshcpu_direct8_span_pixels(pixel8(parent, x0, y), src, NULL, m_const, len);
      }
    }
  } else if (plain && gshcpu_direct8_blends(op)) {
    for (int64_t y = region.y0; y < region.y1; y++) {
      gshcpu_direct8_blend_row(op, pixel8(parent, region.x0, y), pixel8(f, region.x0, y), (size_t)(region.x1 - region.x0), (float)o);
    }
  } else {
    static const double zero[4] = {0.0, 0.0, 0.0, 0.0};
    for (int64_t y = region.y0; y < region.y1; y++) {
      for (int64_t x = region.x0; x < region.x1; x++) {
        uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);
        if (m8 == 0) {
          continue;
        }
        double in[4];
        double s[4];
        if (!inside(f->dirty, x, y)) {
          memcpy(in, zero, sizeof in);
        } else if (f->bytes) {
          decode8(pixel8(f, x, y), in);
        } else {
          memcpy(in, &f->layer[4u * at(run, x, y)], sizeof in);
        }
        gshcpu_render_convert_premultiplied(&c, in, s);
        for (int i = 0; i < 4; i++) {
          s[i] = o * s[i];
        }
        if (bounded && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0) {
          continue;
        }
        double d[4];
        double * target = d;
        if (parent->bytes) {
          decode8(pixel8(parent, x, y), d);
        } else {
          target = &parent->layer[4u * at(run, x, y)];
        }
        if (!gshcpu_render_apply(target, s, gshcpu_k_level[m8], op)) {
          return GSHSCENE_ERR_INTERNAL; /* unreachable: the planning pass refuses an operator the caps do not claim */
        }
        if (parent->bytes) {
          encode8(d, pixel8(parent, x, y));
        }
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

static GSHSCENE_Result unit(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count);

/**
 * MC2: draw the picture of the mask of the group being popped into one more layer, over the pop region @p window (the
 * pixels the pop composites) and under the chain of maps `T I_1 ... I_k Q` (MK6), and leave the layer as the innermost frame.
 *
 * The units are the picture's, walked by a second expansion and gone through as any unit is; their chain is the group's, the
 * index of its push and the unit's own in the picture (MK9), so a refusal inside the picture names it.  A unit whose rectangle
 * misses the window draws nothing (reach), and its limits are met over the whole render's region (MC6: @p whole_window is only
 * what decides, in a nested pass, whether a masked group's own mask would be drawn in the whole render).  The clips in force
 * around the group are not in force inside (MK6): the frame's clip base is the clip count here.  Cancel is checked between the
 * units, and the work of each is added to the pop's (MC7).
 */
static GSHSCENE_Result mask_pass(Run * run, const GSHCPU_Mask_Eval * me, const size_t * chain, size_t count, GSHCPU_Rect window,
    GSHCPU_Rect whole_window) {
  Mask_State * ms = ms_of(run);
  const Frame * group = &run->frames[run->top];
  Mask_Frame * mfs = ms->frames;
  GSHSCENE_Mask mask;
  if (gshscene_resource_mask(group->group.soft_mask, &mask) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  const GSHSCENE_List * picture = gshscene_resource_picture(mask.picture);
  const size_t bytes = gshscene_expansion_bytes(picture);
  GSHSCENE_Expansion walk;
  if (ms->walk_used > ms->walk_bytes || bytes > ms->walk_bytes - ms->walk_used ||
      gshscene_expansion_init(&walk, picture, ms->walk + ms->walk_used, bytes) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL; /* unreachable: the layout counts every walk a pop can have open */
  }
  /* The chain: the group's, its push's index in place of its pop's, then the item's in the picture. */
  memmove(ms->chain, chain, (count - 1u) * sizeof *chain); /* (a mask inside a mask pass names its chain in this same array) */
  ms->chain[count - 1u] = mfs[run->top].push;
  run->maps[count - 1u] = group->group.soft_mask_map;
  /* The mask's frame: an isolated layer in R that starts at (0, 0, 0, 0) (N6), made ready as the units reach it (widen). */
  Frame * f = &run->frames[++run->top];
  memset(f, 0, sizeof *f);
  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;
  f->space = me->frame_space;
  f->group.opacity = 1.0;
  f->group.composite = GSHSCENE_COMPOSITE_SRC_OVER;
  f->group.isolation = GSHSCENE_ISOLATION_ISOLATED;
  f->clip_base = run->clip_count;
  if (run->general) {
    /* The mask frame is its own isolated, non-knockout group (MK2): the groups around the masked group are not its ancestors (KC3). */
    Gen_Frame * g = &gs_of(run)->frames[run->top];
    memset(g, 0, sizeof *g);
    g->on = (run->cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;
    f->general = g->on;
  }
  memset(&mfs[run->top].whole_dirty, 0, sizeof mfs[run->top].whole_dirty);
  const GSHCPU_Rect saved_window = ms->window;
  const GSHCPU_Rect saved_whole = ms->whole_window;
  const Skip saved_skip = run->skip;
  memset(&run->skip, 0, sizeof run->skip);
  ms->window = window;
  ms->whole_window = whole_window;
  ms->walk_used += bytes;
  const GSHSCENE_Item * item;
  const size_t * inner;
  size_t depth;
  GSHSCENE_Result result = GSHSCENE_OK;
  while (result == GSHSCENE_OK && gshscene_expansion_next(&walk, &item, &inner, &depth)) {
    memcpy(ms->chain + count, inner, depth * sizeof *inner);
    run->work = 0;
    result = unit(run, item, ms->chain, count + depth);
    gshscene_job_work(run->job, run->work);
    run->work = 0;
    if (result == GSHSCENE_OK && gshscene_job_checkpoint(run->job)) {
      result = GSHSCENE_ERR_CANCELLED;
    }
  }
  ms->walk_used -= bytes;
  ms->window = saved_window;
  ms->whole_window = saved_whole;
  run->skip = saved_skip;
  return result;
}

/**
 * What the pop of a masked group keeps while it visits its pixels (MC2 to MC4, MC6 to MC8): the mask made ready, the mask frame the
 * pass drew, and which tiers were taken.
 */
typedef struct Mask_Pop {
  GSHCPU_Mask_Eval me;
  const Frame * mf;           /* the mask frame, drawn by mask_pass and released at the end of the pop */
  int outside_tier, identity_tier;
  int have_outside, took_outside, evaluated;
  double v_outside;
} Mask_Pop;

/**
 * Begin the pop of the masked group on top of the stack: make its mask ready for a parent in the group's enclosing space, and draw the
 * mask over the pop region @p region (when the whole render, @p whole, would have drawn it).  The mask frame is the one above the group
 * afterwards, and `run->top` is the group's again.
 */
static GSHSCENE_Result mask_pop_begin(Run * run, Mask_Pop * mp, const size_t * chain, size_t count, GSHCPU_Rect region,
    GSHCPU_Rect whole) {
  const size_t top = run->top;
  const Frame * f = &run->frames[top];
  const Frame * parent = &run->frames[top - 1u];
  memset(mp, 0, sizeof *mp);
  if (!gshcpu_render_mask_prepare(&mp->me, f->group.soft_mask, &parent->space)) {
    return GSHSCENE_ERR_INTERNAL; /* unreachable: the plan refused a mask the waist cannot resolve */
  }
  GSHSCENE_Result r = mask_pass(run, &mp->me, chain, count, region, whole);
  if (r != GSHSCENE_OK) {
    return r;
  }
  mp->mf = &run->frames[top + 1u];
  run->top = top; /* the mask frame is released at the end of the pop: nothing below reads another */
  mp->outside_tier = (run->cs->tiers & GSHCPU_RENDER_TIER_MASK_OUTSIDE) != 0;
  mp->identity_tier = (run->cs->tiers & GSHCPU_RENDER_TIER_MASK_IDENTITY) != 0;
  return GSHSCENE_OK;
}

/** MC3, MC8: the mask's value at pixel @p p of the pop (inside the pop region). */
static double mask_pop_value(const Run * run, Mask_Pop * mp, int64_t x, int64_t y, size_t p) {
  static const double zero[4] = {0.0, 0.0, 0.0, 0.0};
  mp->evaluated = 1;
  if (inside(mp->mf->dirty, x, y)) {
    return gshcpu_render_mask_value(&mp->me, &mp->mf->layer[4u * p], mp->identity_tier);
  }
  (void)run;
  if (mp->outside_tier) {
    /* FT-M1: the mask frame is (0, 0, 0, 0) here, so the value is MK5's, found once for the pop with the same code. */
    if (!mp->have_outside) {
      mp->v_outside = gshcpu_render_mask_value(&mp->me, zero, mp->identity_tier);
      mp->have_outside = 1;
    }
    mp->took_outside = 1;
    return mp->v_outside;
  }
  return gshcpu_render_mask_value(&mp->me, zero, mp->identity_tier);
}

/** The counters of a masked pop that visited @p region. */
static void mask_pop_end(Run * run, const Mask_Pop * mp, GSHCPU_Rect region) {
  gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, masks));
  if (!gshcpu_render_rect_empty(region)) {
    gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, mask_pixels),
        (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0));
  }
  if (!gshcpu_render_rect_empty(mp->mf->dirty)) {
    gshcpu_stroke_cache_add(run->cs->cache, offsetof(GSHCPU_Render_Counters, mask_dirty),
        (uint64_t)(mp->mf->dirty.x1 - mp->mf->dirty.x0) * (uint64_t)(mp->mf->dirty.y1 - mp->mf->dirty.y0));
  }
  if (mp->took_outside) {
    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, mask_outside));
  }
  if (mp->identity_tier && mp->me.identity && mp->evaluated) {
    gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, mask_identity));
  }
}

/**
 * The pop of a masked group (MC2 to MC4, MC6 to MC8): the mask is drawn over the pop region @p region, when the whole
 * render (@p whole) would have drawn it, and each pixel's composite is weighed by the mask's value there: the opacity
 * source becomes `o v`, or the shape `m8 / 255` becomes `(m8 / 255) v`, and nothing else is changed.
 */
static GSHSCENE_Result group_pop_masked(Run * run, const size_t * chain, size_t count, GSHCPU_Rect region, GSHCPU_Rect whole) {
  const size_t top = run->top;
  Frame * f = &run->frames[top];
  Frame * parent = &run->frames[top - 1u];
  if (gshcpu_render_rect_empty(whole)) {
    run->top--;
    run->groups_open--;
    return GSHSCENE_OK; /* the whole render composites nothing of this group, and draws no mask for it */
  }
  Mask_Pop mp;
  GSHSCENE_Result r = mask_pop_begin(run, &mp, chain, count, region, whole);
  if (r != GSHSCENE_OK) {
    return r;
  }
  const uint32_t op = f->group.composite;
  const int bounded = gshcpu_render_bounded(op);
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);
  const double o = f->group.opacity;
  static const double zero[4] = {0.0, 0.0, 0.0, 0.0};
  widen(run, parent, region);
  for (int64_t y = region.y0; y < region.y1; y++) {
    for (int64_t x = region.x0; x < region.x1; x++) {
      uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);
      if (m8 == 0) {
        continue;
      }
      size_t p = at(run, x, y);
      const double v = mask_pop_value(run, &mp, x, y, p);
      double s[4];
      gshcpu_render_convert_premultiplied(&c, inside(f->dirty, x, y) ? &f->layer[4u * p] : zero, s);
      double weight = (double)m8 / 255.0;
      if (mp.me.shape) {
        weight = weight * v; /* MC4: the shape is weighed by v, the opacity as it was */
        for (int i = 0; i < 4; i++) {
          s[i] = o * s[i];
        }
      } else {
        const double ov = o * v; /* MC4: the opacity source is o v */
        for (int i = 0; i < 4; i++) {
          s[i] = ov * s[i];
        }
      }
      if (bounded && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0) {
        continue;
      }
      if (!gshcpu_render_apply(&parent->layer[4u * p], s, weight, op)) {
        return GSHSCENE_ERR_INTERNAL; /* unreachable: the planning pass refuses an operator the caps do not claim */
      }
      if (run->top == 1u) {
        run->touched[p] = 1;
      }
    }
  }
  if (!gshcpu_render_rect_empty(region)) {
    /* The pop's pixels, and the value found for each (MC7). */
    run->work += 2u * (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);
  }
  mask_pop_end(run, &mp, region);
  run->top--;
  run->groups_open--;
  return GSHSCENE_OK;
}

/**
 * KC6: the result `G` of the group on top of the stack at pixel @p p of its dirty rectangle, in the group's space, and its group
 * shape (1 where none is carried, as KC3 says: it is only read where the parent is knockout, which carries it).
 */
static void group_result_at(const Run * run, size_t top, size_t p, double g[4], double * fg) {
  const Gen_Frame * gf = &gs_of(run)->frames[top];
  const double * l = &run->frames[top].layer[4u * p];
  double l0[4] = {0.0, 0.0, 0.0, 0.0};
  if (gf->nonisolated) {
    initial_backdrop(run, top, p, l0);
  }
  gshcpu_render_group_result(l, gf->ag ? gf->ag[p] : l[3], !gf->nonisolated, l0, g);
  *fg = gf->fg ? gf->fg[p] : 1.0;
}

/**
 * The pop where the group or its parent is a general layer (KC2, KC4 to KC6, KC10): the group's result `G` (the initial backdrop taken
 * out of a non-isolated group's layer), converted into the parent's space, weighed by the clips, the opacity and a mask's value as
 * the plain pop does, and composited as an element of the parent: by KC4 into a general parent (with the shape PDF's operators give a
 * group, `m fg`), by the plain step into any other.
 */
static GSHSCENE_Result group_pop_general(Run * run, const size_t * chain, size_t count, GSHCPU_Rect region, GSHCPU_Rect whole) {
  const size_t top = run->top;
  Frame * f = &run->frames[top];
  Frame * parent = &run->frames[top - 1u];
  const Gen_State * gs = gs_of(run);
  const Gen_Frame * pg = &gs->frames[top - 1u];
  const int masked = f->group.soft_mask != NULL;
  Mask_Pop mp;
  if (masked) {
    if (gshcpu_render_rect_empty(whole)) {
      run->top--;
      run->groups_open--;
      return GSHSCENE_OK; /* the whole render composites nothing of this group, and draws no mask for it */
    }
    GSHSCENE_Result r = mask_pop_begin(run, &mp, chain, count, region, whole);
    if (r != GSHSCENE_OK) {
      return r;
    }
  }
  const uint32_t op = f->group.composite;
  const int bounded = gshcpu_render_bounded(op);
  const int pdf = gshcpu_render_pdf_operator(op);
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);
  const double o = f->group.opacity;
  widen(run, parent, region);
  for (int64_t y = region.y0; y < region.y1; y++) {
    for (int64_t x = region.x0; x < region.x1; x++) {
      uint32_t m8 = weigh_clips(run, parent->clip_base, f->clip_base, 255u, x, y);
      if (m8 == 0) {
        continue;
      }
      const size_t p = at(run, x, y);
      const double v = masked ? mask_pop_value(run, &mp, x, y, p) : 1.0;
      double gres[4] = {0.0, 0.0, 0.0, 0.0};
      double fgc = gs->frames[top].fg ? 0.0 : 1.0; /* nothing drawn here: no shape, where it is carried */
      if (inside(f->dirty, x, y)) {
        group_result_at(run, top, p, gres, &fgc);
      }
      double s[4];
      gshcpu_render_convert_premultiplied(&c, gres, s);
      double weight = (double)m8 / 255.0;
      if (masked && mp.me.shape) {
        weight = weight * v; /* MC4 (MK7): the shape is weighed by v, the opacity as it was */
        for (int i = 0; i < 4; i++) {
          s[i] = o * s[i];
        }
      } else {
        const double ov = o * v; /* MC4: the opacity source is o v (v = 1 without a mask: o) */
        for (int i = 0; i < 4; i++) {
          s[i] = ov * s[i];
        }
      }
      if (pg->on || pg->fg) {
        double bk[4];
        const double * b = &parent->layer[4u * p];
        if (pg->knockout) {
          initial_backdrop(run, top - 1u, p, bk);
          b = bk;
        }
        /* T5: PDF's operators see the group as an object of shape `m fg`; every other operator acts on its whole clip region. */
        const int stepped = gshcpu_render_group_step(&parent->layer[4u * p], b, pg->ag ? pg->ag + p : NULL, pg->fg ? pg->fg + p : NULL,
            pg->knockout, s, weight, pdf ? weight * fgc : weight, op);
        if (stepped < 0) {
          return GSHSCENE_ERR_INTERNAL; /* unreachable: the planning pass refuses an operator the caps do not claim */
        }
        if (stepped > 0 && run->top == 1u) {
          run->touched[p] = 1;
        }
        continue;
      }
      if (bounded && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0) {
        continue;
      }
      if (!gshcpu_render_apply(&parent->layer[4u * p], s, weight, op)) {
        return GSHSCENE_ERR_INTERNAL; /* unreachable: the planning pass refuses an operator the caps do not claim */
      }
      if (run->top == 1u) {
        run->touched[p] = 1;
      }
    }
  }
  if (!gshcpu_render_rect_empty(region)) {
    run->work += (masked ? 2u : 1u) * (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);
  }
  if (masked) {
    mask_pop_end(run, &mp, region);
  }
  gshcpu_stroke_cache_count(run->cs->cache, offsetof(GSHCPU_Render_Counters, general_pops));
  run->top--;
  run->groups_open--;
  return GSHSCENE_OK;
}

static GSHSCENE_Result group_pop_plain(Run * run, const size_t * chain, size_t count) {
  if (run->fast8) {
    return group_pop8(run);
  }
  const Frame * f = &run->frames[run->top];
  Frame * parent = &run->frames[run->top - 1u];
  uint32_t op = f->group.composite;
  int bounded = gshcpu_render_bounded(op);
  /* A bounded operator changes nothing where the group drew nothing (N4); an unbounded one acts wherever it is
   * weighed above zero (N8). */
  GSHCPU_Rect region = bounded ? f->dirty : window_of(run);
  region = gshcpu_render_rect_meet(region, reach_of(run, parent->clip_base, f->clip_base));
  GSHCPU_Rect whole = {0, 0, 0, 0};
  if (run->masked) {
    /* MC6: the same in the whole render, which decides whether a mask is drawn at all, so that what a damaged render refuses
     * inside a mask is what the whole render refuses. */
    Mask_Frame * frames = ms_of(run)->frames;
    whole = bounded ? frames[run->top].whole_dirty : ms_of(run)->whole_window;
    whole = gshcpu_render_rect_meet(whole, whole_reach_of(run, parent->clip_base, f->clip_base));
    if (!gshcpu_render_rect_empty(whole)) {
      frames[run->top - 1u].whole_dirty = gshcpu_render_rect_join(frames[run->top - 1u].whole_dirty, whole);
    }
  }
  if (run->general) {
    const Gen_State * gs = gs_of(run);
    const Gen_Frame * g = &gs->frames[run->top];
    const Gen_Frame * pg = &gs->frames[run->top - 1u];
    if (g->on || g->fg || pg->on || pg->fg) {
      return group_pop_general(run, chain, count, region, whole);
    }
  }
  if (run->masked && f->group.soft_mask) {
    return group_pop_masked(run, chain, count, region, whole);
  }
  GSHCPU_Convert c;
  gshcpu_render_convert_prepare(&c, &f->space, &parent->space);
  const double o = f->group.opacity;
  /* The group's layer is zero outside its dirty rectangle (widen), where it was never made ready. */
  static const double zero[4] = {0.0, 0.0, 0.0, 0.0};
  widen(run, parent, region);
  if (bounded && c.same && parent->clip_base == f->clip_base && !gshcpu_render_rect_empty(region)) {
    /* No clips and no conversion, inside the group's dirty rectangle: a row at a time. */
    for (int64_t y = region.y0; y < region.y1; y++) {
      const size_t p0 = at(run, region.x0, y);
      gshcpu_render_composite_row(op, o, &f->layer[4u * p0], &parent->layer[4u * p0], (size_t)(region.x1 - region.x0),
          run->top == 1u ? run->touched + p0 : NULL);
    }
    run->work += (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);
    run->top--;
    run->groups_open--;
    return GSHSCENE_OK;
  }
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

/** Pop the group on top of the stack, and give back the channels it held (KC3). */
static GSHSCENE_Result group_pop(Run * run, const size_t * chain, size_t count) {
  if (!run->general) {
    return group_pop_plain(run, chain, count);
  }
  Gen_State * gs = gs_of(run);
  const Gen_Frame g = gs->frames[run->top];
  const GSHSCENE_Result r = group_pop_plain(run, chain, count);
  if (g.ag) {
    gs->ag_used--;
  }
  if (g.fg) {
    gs->fg_used--;
  }
  return r;
}

/* --- patterns (render.h, PC1 to PC8; masks-images-patterns story R2) ------------------------------------------ */

static const GSHGEOM_Map * target_map(const Run * run) {
  return cur_pat(run) ? &cur_pat(run)->base : &run->job->target.map;
}

static Pat_Level * pat_level(const Run * run, size_t k) {
  return (Pat_Level *)(void *)(pr(run)->pat_mem + k * pr(run)->pat_stride);
}

/** @p r moved by (@p dx, @p dy). */
static GSHCPU_Rect rect_moved(GSHCPU_Rect r, int64_t dx, int64_t dy) {
  r.x0 += dx;
  r.x1 += dx;
  r.y0 += dy;
  r.y1 += dy;
  return r;
}

/** Whether every coordinate of the non-empty pixel rectangle @p r is one the scan converter takes (a 26.6 coordinate of at most ::GSHCPU_RENDER_COORD_LIMIT). */
static int rect_in_range(GSHCPU_Rect r) {
  const int64_t m = GSHCPU_RENDER_COORD_LIMIT / 64;
  return r.x0 > -m && r.y0 > -m && r.x1 < m && r.y1 < m;
}

/** PC3: one copy of the lattice, as its pass draws it: the canonical map, the whole pixels it is moved by, and its cell in the canonical frame. */
typedef struct Pat_Copy {
  GSHGEOM_Map canon;
  int64_t sx, sy;
  GSHGEOM_Device_Point quad[4];  /* the cell's quadrilateral under the canonical map, in 26.6 */
  GSHCPU_Rect qbox;              /* the pixels it touches */
} Pat_Copy;

static int64_t floor_div64(int64_t v) {
  const int64_t q = v / 64;
  return v % 64 != 0 && v < 0 ? q - 1 : q;
}

/**
 * PC3: the copy @p (i, j) of the pass @p L: its canonical map and whole pixels by shape-geometry's one function (LT5: the position
 * of LT2 split by S4's function, and for integer steps the exact split), its cell's corners under the canonical map; and
 * `L->base` becomes the canonical map, what the units of the tile are composed with.
 */
static GSHSCENE_Result pat_copy_open(Pat_Level * L, int64_t i, int64_t j, Pat_Copy * c) {
  double q[8];
  if (gshgeom_lattice_quad(&L->C, &L->lat, q) != GSHGEOM_OK) {
    return GSHSCENE_ERR_LIMIT;
  }
  /* LT5: the canonical map and the whole pixels of the copy, by shape-geometry's one function (the exact split for integer steps). */
  if (gshgeom_lattice_canonical(&L->C, &L->lat, i, j, &c->canon, &c->sx, &c->sy) != GSHGEOM_OK) {
    return GSHSCENE_ERR_LIMIT; /* a shifted coordinate past the range the sink draws */
  }
  int64_t lo_x = INT64_MAX, lo_y = INT64_MAX, hi_x = INT64_MIN, hi_y = INT64_MIN;
  for (int k = 0; k < 4; k++) {
    int32_t px, py;
    if (gshgeom_round_26_6(q[2 * k] + c->canon.t[0], &px) != GSHGEOM_OK || gshgeom_round_26_6(q[2 * k + 1] + c->canon.t[1], &py) != GSHGEOM_OK) {
      return GSHSCENE_ERR_LIMIT;
    }
    c->quad[k].x = px;
    c->quad[k].y = py;
    lo_x = px < lo_x ? px : lo_x;
    hi_x = px > hi_x ? px : hi_x;
    lo_y = py < lo_y ? py : lo_y;
    hi_y = py > hi_y ? py : hi_y;
  }
  c->qbox.x0 = floor_div64(lo_x);
  c->qbox.y0 = floor_div64(lo_y);
  c->qbox.x1 = floor_div64(hi_x) + 1;
  c->qbox.y1 = floor_div64(hi_y) + 1;
  L->cur_qbox = c->qbox;
  L->base = c->canon;
  return GSHSCENE_OK;
}

typedef GSHSCENE_Result (*Pat_Copy_Fn)(Run * run, Pat_Level * L, int64_t i, int64_t j, void * ctx);

typedef struct Pat_Each {
  Run * run;
  Pat_Level * L;
  GSHSCENE_Result r;
  Pat_Copy_Fn fn;
  void * ctx;
} Pat_Each;

static int pat_each_row(void * ctx, int64_t j, int64_t lo, int64_t hi) {
  Pat_Each * e = ctx;
  for (int64_t i = lo;; i++) {
    if (gshscene_job_checkpoint(e->run->job)) {
      e->r = GSHSCENE_ERR_CANCELLED; /* PC11: before every copy */
      return 1;
    }
    e->r = e->fn(e->run, e->L, i, j, e->ctx);
    if (e->r != GSHSCENE_OK) {
      return 1;
    }
    if (i == hi) {
      return 0;
    }
  }
}

/** Call @p fn for each copy of the pass whose cell meets @p rect (LT1), in lattice order, or for the one copy of an overlapping pass. */
static GSHSCENE_Result pat_each(Run * run, Pat_Level * L, GSHCPU_Rect rect, Pat_Copy_Fn fn, void * ctx) {
  if (L->single) {
    return fn(run, L, L->ci, L->cj, ctx);
  }
  const int64_t r4[4] = {rect.x0, rect.y0, rect.x1, rect.y1};
  uint64_t n = 0;
  Pat_Each e = {run, L, GSHSCENE_OK, fn, ctx};
  const GSHGEOM_Result g = gshgeom_lattice_each(&L->C, &L->lat, r4, GSHGEOM_LATTICE_INDEX_MAX, pat_each_row, &e, &n);
  if (e.r != GSHSCENE_OK) {
    return e.r;
  }
  return g == GSHGEOM_OK ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;
}

/** PC4: the byte of an area @p a of the copies of a pixel, in 1/8192 pixel: its floor in 256ths, held at 255 (the quantization, once, of the sum). */
static inline uint32_t pat_level8(uint32_t a) {
  const uint32_t level = a >> GSHCPU_FOLD_AREA_SHIFT;
  return level > 255u ? 255u : level;
}

/* PC4: the unit's accumulator.  A pixel holds the areas of the copies that touched it (sum8), how many did, and either the one
 * copy's source or the sum of the sources, each weighed by its area. */
static inline void pat_add(Pat_Level * L, size_t p, uint32_t m, const double s[4]) {
  double * a = &L->acc[4u * p];
  const uint8_t c = L->cnt[p];
  if (c == 0) {
    memcpy(a, s, 4u * sizeof(double));
    L->sum8[p] = m;
    L->cnt[p] = 1;
    return;
  }
  if (c == 1) {
    const double m0 = (double)L->sum8[p];
    for (int i = 0; i < 4; i++) {
      a[i] = m0 * a[i] + (double)m * s[i];
    }
    L->cnt[p] = 2;
  } else {
    for (int i = 0; i < 4; i++) {
      a[i] += (double)m * s[i];
    }
    if (c < 255) {
      L->cnt[p] = (uint8_t)(c + 1);
    }
  }
  L->sum8[p] += m;
}

/**
 * PC4: the unit's element, applied to the layer: a pixel only one copy touched is N7 exactly; a pixel several touched takes the
 * shape `min(255, sum) / 255` and the mean of their sources weighted by their coverages (`m_x / sum`, so the weights are
 * renormalized above 255).
 */
static void pat_finish(Run * run, Pat_Level * L, Frame * frame) {
  const GSHCPU_Rect u = L->urect;
  if (gshcpu_render_rect_empty(u)) {
    return;
  }
  widen_w(run, frame, u, u);
  for (int64_t y = u.y0; y < u.y1; y++) {
    for (int64_t x = u.x0; x < u.x1; x++) {
      const size_t p = at(run, x, y);
      const uint8_t c = L->cnt[p];
      if (c == 0) {
        continue;
      }
      L->cnt[p] = 0;
          if (c == 1) {
        painter_pixel(run, frame, x, y, pat_level8(L->sum8[p]), &L->acc[4u * p]);
        continue;
      }
      const uint32_t total = L->sum8[p];
      double s[4];
      for (int i = 0; i < 4; i++) {
        s[i] = L->acc[4u * p + (size_t)i] / (double)total;
      }
      painter_pixel(run, frame, x, y, pat_level8(total), s);
    }
  }
  run->work += (uint64_t)(u.x1 - u.x0) * (uint64_t)(u.y1 - u.y0);
  L->urect.x1 = L->urect.x0;
  L->urect.y1 = L->urect.y0;
}

/** The source of the pixel (@p x, @p y) of a painter's frame of coordinates; 0 where the paint gives none. */
static int painter_source(Painter * p, int64_t x, int64_t y, double s[4]) {
  if (p->pattern) {
    return pattern_pixel(p, x, y, s);
  }
  if (p->image) {
    gshcpu_render_image_at(&p->im, (double)(int32_t)x + 0.5, (double)(int32_t)y + 0.5, s);
    return 1;
  }
  if (!p->gradient) {
    memcpy(s, p->src, 4u * sizeof(double));
    return 1;
  }
  double color[4];
  if (!gshcpu_render_gradient_at(&p->gr, (double)(int32_t)x + 0.5, (double)(int32_t)y + 0.5, color)) {
    return 0; /* G8 */
  }
  gshcpu_render_convert_straight(&p->conv, color, color);
  s[0] = color[0] * color[3];
  s[1] = color[1] * color[3];
  s[2] = color[2] * color[3];
  s[3] = color[3];
  return 1;
}

/** PC7, PC2: the source of a pattern paint at a pixel of its lattice layer. */
static int pattern_pixel(const Painter * p, int64_t x, int64_t y, double s[4]) {
  if (p->period) {
    int64_t r[2];
    gshgeom_lattice_reduce(&p->per, x - p->wx, y - p->wy, r); /* PC9, LT3: the window pixel congruent to (x, y) */
    return pattern_read(p, p->wx + r[0], p->wy + r[1], s);
  }
  return pattern_read(p, x, y, s);
}

/** PC7, PC2: the source of a pattern paint at the pixel (@p x, @p y) of the layer itself (the window's, once the period has reduced it). */
static int pattern_read(const Painter * p, int64_t x, int64_t y, double s[4]) {
  const Frame * f = p->lf;
  double v[4] = {0.0, 0.0, 0.0, 0.0};
  if (inside(f->dirty, x, y)) {
    memcpy(v, &f->layer[4u * ((size_t)(y - p->loy) * p->run->layout.width + (size_t)(x - p->lox))], sizeof v);
  }
  if (!p->has_tint) {
    for (int i = 0; i < 4; i++) {
      s[i] = v[i] * p->alpha;
    }
  } else {
    const double a = v[3] * p->alpha; /* only the layer's alpha is read: the tile's colors are discarded */
    for (int i = 0; i < 4; i++) {
      s[i] = p->tint[i] * a;
    }
  }
  return 1;
}

/** A flattened path of the cell's quadrilateral (a plane fill in a tile fills its cell). */
static GSHSCENE_Result quad_path(Run * run, const Pat_Copy * c, GSHGEOM_Flat_Path ** out) {
  const GSHGEOM_Flat_Subpath sub = {0, 4, 1};
  const GSHGEOM_Result r = gshgeom_flat_path_from_points(c->quad, 4, &sub, 1, run->item_alloc, &run->cs->geometry, out);
  return r == GSHGEOM_OK ? GSHSCENE_OK : r == GSHGEOM_ERR_OOM ? GSHSCENE_ERR_OOM : GSHSCENE_ERR_LIMIT;
}

/**
 * PC9: the period tier draws the copies that meet its window only, and every limit it meets is a limit of those copies over what the
 * whole render draws of each.  A copy elsewhere is the same bits shifted, so it meets the same limit unless it lies more inside the
 * whole render than the copies drawn do.  The tier therefore vouches for a copy only when no limit could be met by the copy whole:
 * its cell's pixels are in range and the cell deposits of its cut outline over the whole cell cannot pass the cap.  A copy it cannot
 * vouch for makes the pass give the window up (abort_tier) for the general tier, which decides every copy; so a limit never depends on the tier.
 */
static void period_guard_cells(Run * run, Pat_Level * L, const GSHGEOM_Flat_Path * const * paths, size_t n, const Pat_Copy * c) {
  if (!rect_in_range(c->qbox) ||
      gshcpu_render_cells_bound(paths, n, c->qbox, run->cs->max_deposits) > run->cs->max_deposits) {
    L->abort_tier = 1;
  }
}

/** PC9: the same for a stroke cut to the cell: the deposits of its parts over the cell's pixels, summed as the counting sums them. */
static void period_guard_stroke(Run * run, Pat_Level * L, const GSHCPU_Stroke * s) {
  uint64_t bound = 0;
  for (int p = GSHCPU_STROKE_BODY; p <= GSHCPU_STROKE_ALIGNED; p++) {
    bound += s->meta.bound[p] > run->cs->max_deposits ? run->cs->max_deposits + 1u : s->meta.bound[p];
  }
  if (!rect_in_range(L->cur_qbox) || bound > run->cs->max_deposits) {
    L->abort_tier = 1;
  }
}

/**
 * CU21: the coverage of a union clip in the copy @p c, in the canonical frame: the area of the union of its members (union_area, the
 * general tier's), each member's outline under the canonical map and cut to the cell's quadrilateral by CK1 as a single clip's is, over
 * the pixels the copy reaches.  The byte of a pixel is the level of its area held at 255 and, where @p rem is given, @p rem is the area
 * the byte leaves out (gshcpu_fold_split's), so that the copies of a tile add areas and not bytes (PC4).  A union never reaches across
 * the cell edge, so no copy's union is formed from another's members.  ::GSHSCENE_ERR_LIMIT is the pattern's resource limit (PC8).
 */
static GSHSCENE_Result pat_cover_union(Run * run, Pat_Level * L, const GSHSCENE_Item * item, GSHGEOM_Map * maps, size_t count,
    const Pat_Copy * c, GSHCPU_Rect * rc, GSHCPU_Rect * wc, uint8_t * out, uint8_t * rem, int * have) {
  *have = 0;
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_rewind(&run->arena, L->arena_mark);
  }
  const Union_Src src = {maps, &L->base, c->quad};
  Union_Area ua;
  const GSHSCENE_Result r = union_area(run, item, count, &src, *rc, *wc, &ua);
  if (r != GSHSCENE_OK) {
    return r;
  }
  *rc = ua.pl.clip;
  *wc = ua.pl.wide;
  if (!ua.live) {
    return GSHSCENE_OK; /* nothing of the union is in the whole render */
  }
  const GSHCPU_Rect W = ua.pl.wide;
  const int64_t Ww = W.x1 - W.x0;
  const GSHCPU_Rect q = ua.pl.clip;
  const size_t qw = (size_t)(q.x1 - q.x0);
  for (int64_t y = q.y0; y < q.y1; y++) {
    const uint16_t * from = ua.b.plane + (size_t)((y - W.y0) * Ww + (q.x0 - W.x0));
    for (size_t x = 0; x < qw; x++) {
      const uint32_t area = from[x] > 8192u ? 8192u : from[x]; /* a pixel holds no more than itself */
      const uint32_t level = area >> GSHCPU_FOLD_AREA_SHIFT;
      const uint32_t byte = level > 255u ? 255u : level;
      const size_t at_px = (size_t)(y - q.y0) * qw + x;
      out[at_px] = (uint8_t)byte;
      if (rem) {
        rem[at_px] = (uint8_t)(area - (byte << GSHCPU_FOLD_AREA_SHIFT));
      }
    }
  }
  run->work += (uint64_t)qw * (uint64_t)(q.y1 - q.y0);
  union_area_free(run, &ua);
  *have = 1;
  return GSHSCENE_OK;
}

/**
 * PC3: the coverage of the item in the copy @p c, in the canonical frame: its outline under the canonical map, each path clipped to
 * the cell's quadrilateral by CK1, scan converted over the pixels the copy reaches (the region moved by the copy's shift).  @p rc
 * is the rectangle the coverage is written for, @p wc what the whole render draws of the copy (where cells are counted).
 */
static GSHSCENE_Result pat_cover(Run * run, Pat_Level * L, const GSHSCENE_Item * item, GSHGEOM_Map * maps, size_t count, const Pat_Copy * c,
    GSHCPU_Rect * rc, GSHCPU_Rect * wc, uint8_t * out, uint8_t * rem, int * have) {
  Cpu_Sink * cs = run->cs;
  *have = 0;
  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
    return pat_cover_union(run, L, item, maps, count, c, rc, wc, out, rem, have);
  }
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_rewind(&run->arena, L->arena_mark);
  }
  GSHCPU_Item_Geometry g;
  memset(&g, 0, sizeof g);
  GSHGEOM_Flat_Path * plane = NULL;
  const GSHGEOM_Flat_Path * const * paths = NULL;
  size_t n = 0;
  GSHGEOM_Flat_Path ** clipped = NULL;
  GSHSCENE_Result result = GSHSCENE_OK;
  GSHCPU_Rect box = c->qbox;
  /* AA2: an item that asks for no antialiasing is covered by the centers of the pixels (the plane's quadrilateral is the cell itself). */
  const int center = item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE;
  const GSHGEOM_Flat_Path * mask = NULL;
  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_PLANE) {
    result = quad_path(run, c, &plane);
    if (result != GSHSCENE_OK) {
      return result;
    }
    paths = (const GSHGEOM_Flat_Path * const *)(void *)&plane;
    n = 1;
  } else {
    GSHCPU_Result r = gshcpu_render_geometry(item, maps, count - 1, &L->base, run->item_alloc, &cs->geometry, &g);
    run->work += g.points;
    if (r != GSHCPU_OK) {
      gshcpu_render_geometry_release(&g, run->item_alloc);
      return scene_result(r);
    }
    box = gshcpu_render_rect_meet(g.box, c->qbox);
    if (center) {
      /* AA2: the item's own outline, not cut (a cut point is rounded, and a center near the edge it moved could change sides); the cell
       * is the mask the centers are also tested against, so the copies of a lattice cover every center once. */
      result = quad_path(run, c, &plane);
      if (result != GSHSCENE_OK) {
        gshcpu_render_geometry_release(&g, run->item_alloc);
        return result;
      }
      mask = plane;
      clipped = run->item_alloc->calloc_fn(run->item_alloc->ctx, g.count ? g.count : 1u, sizeof *clipped);
      if (!clipped) {
        gshgeom_flat_path_destroy(plane);
        gshcpu_render_geometry_release(&g, run->item_alloc);
        return GSHSCENE_ERR_OOM;
      }
      for (size_t k = 0; k < g.count; k++) {
        if (g.paths[k]) {
          clipped[n++] = g.paths[k]; /* the geometry's own: it releases them */
        }
      }
      paths = (const GSHGEOM_Flat_Path * const *)(void *)clipped;
    } else {
    clipped = run->item_alloc->calloc_fn(run->item_alloc->ctx, g.count ? g.count : 1u, sizeof *clipped);
    if (!clipped) {
      gshcpu_render_geometry_release(&g, run->item_alloc);
      return GSHSCENE_ERR_OOM;
    }
    for (size_t k = 0; k < g.count && result == GSHSCENE_OK; k++) {
      if (!g.paths[k]) {
        continue;
      }
      GSHGEOM_Flat_Path * cut = NULL;
      const GSHGEOM_Result gr = gshgeom_flat_path_clip_convex(g.paths[k], c->quad, run->item_alloc, &cs->geometry, &cut);
      if (gr != GSHGEOM_OK) {
        result = gr == GSHGEOM_ERR_OOM ? GSHSCENE_ERR_OOM : GSHSCENE_ERR_LIMIT;
        break;
      }
      if (gshgeom_flat_path_subpath_count(cut) == 0) {
        gshgeom_flat_path_destroy(cut);
        continue;
      }
      clipped[n++] = cut;
    }
    paths = (const GSHGEOM_Flat_Path * const *)(void *)clipped;
    }
  }
  if (result == GSHSCENE_OK && n > 0 && L->guard) {
    period_guard_cells(run, L, paths, n, c);
  }
  if (result == GSHSCENE_OK && n > 0) {
    *rc = gshcpu_render_rect_meet(*rc, box);
    *wc = gshcpu_render_rect_meet(*wc, box);
    if (!gshcpu_render_rect_empty(*wc)) {
      uint64_t cells = 0;
      GSHCPU_Rect rect = *rc;
      if (gshcpu_render_rect_empty(rect)) {
        rect.x1 = rect.x0;
        rect.y1 = rect.y0;
      }
      GSHCPU_Result r;
      if (center) {
        r = gshcpu_render_cover_center(paths, n, item->fill_rule, mask, rect, *wc, out, run->acc, cs->max_deposits, &cells);
        if (r == GSHCPU_OK && rem && !gshcpu_render_rect_empty(rect)) {
          /* The area a byte leaves out (PC4): none for a pixel nothing covers; 32 of 8192 for a whole one (255 * 32 + 32). */
          const size_t pixels = (size_t)(rect.x1 - rect.x0) * (size_t)(rect.y1 - rect.y0);
          for (size_t i = 0; i < pixels; i++) {
            rem[i] = out[i] ? 32u : 0u;
          }
        }
        cells += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
      } else {
        r = rem ? gshcpu_render_cover_split(paths, n, item->fill_rule, rect, *wc, out, rem, run->acc, cs->max_deposits, &cells)
                : gshcpu_render_cover_counted(paths, n, item->fill_rule, rect, *wc, out, run->acc, cs->max_deposits, &cells);
      }
      run->acc_zero = 0;
      run->work += cells;
      result = scene_result(r);
      *have = result == GSHSCENE_OK;
    }
  }
  if (clipped) {
    for (size_t k = 0; k < n && !center; k++) {
      gshgeom_flat_path_destroy(clipped[k]);
    }
    run->item_alloc->free_fn(run->item_alloc->ctx, clipped);
  }
  if (plane) {
    gshgeom_flat_path_destroy(plane);
  }
  gshcpu_render_geometry_release(&g, run->item_alloc);
  return result;
}

static GSHSCENE_Result pat_copy_coverage(Run * run, Pat_Level * L, const GSHSCENE_Item * item, size_t count, const Pat_Copy * c,
    GSHCPU_Rect reach, GSHCPU_Rect wreach, GSHCPU_Rect * rc, GSHCPU_Rect * wc, int * have) {
  *have = 0;
  *rc = gshcpu_render_rect_meet(rect_moved(reach, -c->sx, -c->sy), c->qbox);
  *wc = gshcpu_render_rect_meet(rect_moved(wreach, -c->sx, -c->sy), c->qbox);
  if (gshcpu_render_rect_empty(*wc)) {
    return GSHSCENE_OK; /* the copy is not in the whole render: nothing to draw, nothing to count */
  }
  if (!rect_in_range(*wc)) {
    return GSHSCENE_ERR_LIMIT;
  }
  return pat_cover(run, L, item, run->maps, count, c, rc, wc, run->coverage, L->rem, have);
}

/**
 * PC4: the coverage of a pixel, @p cov and the area @p rem its byte leaves out (NULL: none), weighed by the clip's byte @p m (D8).  The
 * byte is weighed as ever, so that a pixel only one copy touches is the byte the explicit instance draws (N7), and the area the byte leaves
 * out is dropped: a clip's weight is of a byte, a clip in a tile is cut to the copy's cell, and the seam of a weighed copy is the product of
 * two coverages that no sum of areas makes whole.  Only a copy that no clip weighs adds its exact area.
 */
static inline void pat_weigh(uint8_t * cov, uint8_t * rem, uint32_t m) {
  *cov = (uint8_t)gshcpu_render_weigh(*cov, m);
  if (rem) {
    *rem = 0;
  }
}

/**
 * PC4: the weight of the unit's coverage @p cov over @p rc in the copy @p c under the clips of the tile in force in @p frame, each
 * clip made again for this copy (its outline under the copy's canonical map, cut to the cell): D8's fold with the copy's own clips.
 */
static GSHSCENE_Result pat_fold_clips(Run * run, Pat_Level * L, const Frame * frame, const Pat_Copy * c, GSHCPU_Rect rc, uint8_t * cov,
    uint8_t * rem) {
  const size_t n = (size_t)(rc.x1 - rc.x0) * (size_t)(rc.y1 - rc.y0);
  for (size_t k = frame->clip_base; k < run->clip_count; k++) {
    const Pat_Clip * pc = &L->pclips[k];
    GSHCPU_Rect r = rc;
    GSHCPU_Rect w = rc;
    int have = 0;
    memset(L->tmp, 0, n);
    GSHSCENE_Result res = pat_cover(run, L, pc->item, pc->maps, pc->count, c, &r, &w, L->tmp, NULL, &have);
    if (res != GSHSCENE_OK) {
      return res;
    }
    if (!have) {
      memset(cov, 0, n); /* the clip holds nothing of this copy here */
      if (rem) {
        memset(rem, 0, n);
      }
      return GSHSCENE_OK;
    }
    /* pat_cover wrote L->tmp over r, a rectangle inside rc; the pixels of rc outside r are 0. */
    if (r.x0 != rc.x0 || r.y0 != rc.y0 || r.x1 != rc.x1 || r.y1 != rc.y1) {
      const size_t rw = (size_t)(r.x1 - r.x0);
      const size_t cw = (size_t)(rc.x1 - rc.x0);
      for (int64_t y = rc.y0; y < rc.y1; y++) {
        for (int64_t x = rc.x0; x < rc.x1; x++) {
          const uint32_t m = (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) ? L->tmp[(size_t)(y - r.y0) * rw + (size_t)(x - r.x0)] : 0u;
          const size_t at_px = (size_t)(y - rc.y0) * cw + (size_t)(x - rc.x0);
          pat_weigh(&cov[at_px], rem ? &rem[at_px] : NULL, m);
        }
      }
    } else {
      for (size_t i = 0; i < n; i++) {
        pat_weigh(&cov[i], rem ? &rem[i] : NULL, L->tmp[i]);
      }
    }
  }
  return GSHSCENE_OK;
}

typedef struct Pat_Unit {
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  Frame * frame;
  GSHCPU_Rect reach, wreach;
} Pat_Unit;

/** The weights of the coverage @p cov of the copy @p c, folded with the clips in force (D8) and painted by the item's paint at the canonical position (PC3), added to the unit. */
static GSHSCENE_Result pat_paint_copy(Run * run, Pat_Level * L, const Pat_Unit * u, const Pat_Copy * c, GSHCPU_Rect rc, GSHCPU_Rect wc,
    uint8_t * cov, uint8_t * rem) {
  Painter p;
  if (gshcpu_render_rect_empty(rc)) {
    rc.x1 = rc.x0; /* a copy the damage does not reach: nothing to draw, in the form every painter takes (no negative width) */
    rc.y1 = rc.y0;
  }
  if (!gshcpu_render_rect_empty(rc) && u->frame->clip_base < run->clip_count) {
    const GSHSCENE_Result f = pat_fold_clips(run, L, u->frame, c, rc, cov, rem);
    if (f != GSHSCENE_OK) {
      return f;
    }
  }
  pr(run)->quiet = 1;
  pr(run)->pshx = c->sx;
  pr(run)->pshy = c->sy;
  GSHSCENE_Result r = painter_begin(&p, run, u->item, u->chain, u->count, u->frame, rc, wc);
  pr(run)->quiet = 0;
  pr(run)->pshx = 0;
  pr(run)->pshy = 0;
  if (r != GSHSCENE_OK || p.refused) {
    return r;
  }
  const size_t w = (size_t)(rc.x1 - rc.x0);
  for (int64_t y = rc.y0; y < rc.y1; y++) {
    for (int64_t x = rc.x0; x < rc.x1; x++) {
      const size_t at_px = (size_t)(y - rc.y0) * w + (size_t)(x - rc.x0);
      /* already weighed by this copy's clips (pat_fold_clips); the area of the copy here, in 1/8192 pixel (a stroke's has only its byte) */
      const uint32_t m = ((uint32_t)cov[at_px] << GSHCPU_FOLD_AREA_SHIFT) + (rem ? rem[at_px] : 0u);
      if (m == 0) {
        continue;
      }
      const int64_t dx = x + c->sx;
      const int64_t dy = y + c->sy;
      double s[4];
      if (painter_source(&p, x, y, s)) {
              pat_add(L, at(run, dx, dy), m, s);
      }
    }
  }
  if (p.image) {
    run->work += p.im.visits;
  }
  if (!gshcpu_render_rect_empty(rc)) {
    L->urect = gshcpu_render_rect_join(L->urect, rect_moved(rc, c->sx, c->sy));
    L->copies++;
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result pat_fill_copy(Run * run, Pat_Level * L, int64_t i, int64_t j, void * ctx) {
  const Pat_Unit * u = ctx;
  Pat_Copy c;
  GSHSCENE_Result r = pat_copy_open(L, i, j, &c);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHCPU_Rect rc, wc;
  int have;
  r = pat_copy_coverage(run, L, u->item, u->count, &c, u->reach, u->wreach, &rc, &wc, &have);
  if (r != GSHSCENE_OK || !have) {
    return r;
  }
  return pat_paint_copy(run, L, u, &c, rc, wc, run->coverage, L->rem);
}

/** PC4: a fill of the tile: one element over all the copies. */
static GSHSCENE_Result pat_fill(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Pat_Level * L = pr(run)->pat;
  Frame * frame = &run->frames[run->top];
  Pat_Unit u = {item, chain, count, frame, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count)};
  if (gshcpu_render_rect_empty(u.wreach)) {
    return GSHSCENE_OK; /* nothing of the unit is in the whole render */
  }
  L->urect.x1 = L->urect.x0;
  L->urect.y1 = L->urect.y0;
  /* S4: the copies of the whole render, not only the ones the damage draws, so that a limit met inside a copy (a cell cap, a gradient
   * or an image past G14's domain, a coordinate past the range) refuses the use whatever the damage; a copy the damage does not reach
   * is covered and tested over the whole render's part of it and draws nothing. */
  GSHSCENE_Result r = pat_each(run, L, u.wreach, pat_fill_copy, &u);
  L->base = L->base0;
  if (r == GSHSCENE_OK) {
    pat_finish(run, L, frame);
  }
  return r;
}

static GSHSCENE_Result pat_clip_copy(Run * run, Pat_Level * L, int64_t i, int64_t j, void * ctx) {
  const Pat_Unit * u = ctx;
  Pat_Copy c;
  GSHSCENE_Result r = pat_copy_open(L, i, j, &c);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHCPU_Rect rc, wc;
  int have;
  r = pat_copy_coverage(run, L, u->item, u->count, &c, u->reach, u->wreach, &rc, &wc, &have);
  if (r != GSHSCENE_OK || !have) {
    return r;
  }
  const size_t w = (size_t)(rc.x1 - rc.x0);
  for (int64_t y = rc.y0; y < rc.y1; y++) {
    for (int64_t x = rc.x0; x < rc.x1; x++) {
      const size_t at_px = (size_t)(y - rc.y0) * w + (size_t)(x - rc.x0);
      const uint32_t m = ((uint32_t)run->coverage[at_px] << GSHCPU_FOLD_AREA_SHIFT) + L->rem[at_px];
      if (m == 0) {
        continue;
      }
      /* PC4: a clip is the union of its copies' restricted regions: their areas add, and the byte is made once, in pat_clip_push. */
      const size_t p = at(run, x + c.sx, y + c.sy);
      if (L->cnt[p] == 0) {
        L->cnt[p] = 1;
        L->sum8[p] = m;
      } else {
        L->sum8[p] += m;
      }
    }
  }
  run->work += (uint64_t)w * (uint64_t)(rc.y1 - rc.y0);
  return GSHSCENE_OK;
}

static GSHSCENE_Result pat_clip_push(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Pat_Level * L = pr(run)->pat;
  Frame * frame = &run->frames[run->top];
  Clip * clip = &run->clips[run->clip_count];
  clip->coverage = run->clip_memory + run->clip_count * run->layout.pixels;
  clip->rect = reach_of(run, frame->clip_base, run->clip_count);
  clip->whole = whole_reach_of(run, frame->clip_base, run->clip_count);
  Pat_Unit u = {item, chain, count, frame, clip->rect, clip->whole};
  {
    /* PC4: kept, so that the unit's weight in any copy can use this clip's coverage in that copy. */
    Pat_Clip * pc = &L->pclips[run->clip_count];
    pc->item = item;
    pc->count = count;
    pc->maps = L->pmaps + run->clip_count * (run->layout.levels + 2u);
    memcpy(pc->maps, run->maps, count * sizeof *pc->maps);
  }
  const int drawn = !gshcpu_render_rect_empty(clip->rect);
  if (!drawn) {
    clip->rect.x1 = clip->rect.x0;
    clip->rect.y1 = clip->rect.y0;
  }
  if (!gshcpu_render_rect_empty(clip->whole)) {
    if (drawn) {
      memset(clip->coverage, 0, (size_t)(clip->rect.x1 - clip->rect.x0) * (size_t)(clip->rect.y1 - clip->rect.y0));
    }
    GSHSCENE_Result r = pat_each(run, L, clip->whole, pat_clip_copy, &u); /* S4: the copies of the whole render */
    L->base = L->base0;
    if (r != GSHSCENE_OK) {
      return r;
    }
  }
  if (drawn) {
    /* PC4: the union of the copies' regions, quantized once: the byte of the sum of their areas, held at 255. */
    const size_t cw = (size_t)(clip->rect.x1 - clip->rect.x0);
    for (int64_t y = clip->rect.y0; y < clip->rect.y1; y++) {
      for (int64_t x = clip->rect.x0; x < clip->rect.x1; x++) {
        const size_t p = at(run, x, y);
        if (L->cnt[p] != 0) {
          clip->coverage[(size_t)(y - clip->rect.y0) * cw + (size_t)(x - clip->rect.x0)] = (uint8_t)pat_level8(L->sum8[p]);
          L->cnt[p] = 0;
        }
      }
    }
  }
  run->clip_count++;
  return GSHSCENE_OK;
}

static GSHSCENE_Result pat_stroke_copy(Run * run, Pat_Level * L, int64_t i, int64_t j, void * ctx) {
  const Pat_Unit * u = ctx;
  const GSHSCENE_Item * item = u->item;
  Cpu_Sink * cs = run->cs;
  Pat_Copy c;
  GSHSCENE_Result result = pat_copy_open(L, i, j, &c);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHCPU_Rect reach = gshcpu_render_rect_meet(rect_moved(u->reach, -c.sx, -c.sy), c.qbox);
  GSHCPU_Rect wreach = gshcpu_render_rect_meet(rect_moved(u->wreach, -c.sx, -c.sy), c.qbox);
  if (gshcpu_render_rect_empty(wreach)) {
    return GSHSCENE_OK;
  }
  if (!rect_in_range(wreach)) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHGEOM_Map composed;
  run->maps[u->count - 1] = item->map;
  if (gshgeom_map_compose_chain(run->maps, u->count, &L->base, &composed) != GSHGEOM_OK) {
    return GSHSCENE_ERR_LIMIT;
  }
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_rewind(&run->arena, L->arena_mark);
  }
  GSHCPU_Stroke_Cache * cache = !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;
  GSHCPU_Stroke s;
  const GSHCPU_Result opened =
      gshcpu_render_stroke_open_lines(cache, run->item_alloc, &cs->geometry, cs->max_hatch_lines, item, &composed, &s);
  if (opened != GSHCPU_OK) {
    return opened == GSHCPU_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : scene_result(opened);
  }
  if (item->style && gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_HATCH && s.drawn) {
    gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, hatches));
    gshcpu_stroke_cache_add(cs->cache, offsetof(GSHCPU_Render_Counters, hatch_lines), s.meta.lines);
  }
  if (!s.drawn) {
    gshcpu_render_stroke_close(cache, &s);
    return GSHSCENE_OK;
  }
  /* PC5: the stroke is cut to the cell by CK1 (the body and the aligned part; the clip path restricts the aligned part, which is cut
   * already), so that the coverage of the two sides of a cell edge adds to the whole where a stroke ends at it. */
  /* AA2: a stroke covered by the centers of its pixels is not cut (a cut point is rounded, and a center near the edge it moved could change
   * sides): the cell is a mask the centers are also tested against, so the copies of a lattice cover every center once. */
  const int center = item->antialias == (uint32_t)GSHSCENE_ANTIALIAS_NONE;
  GSHGEOM_Flat_Path * cell = NULL;
  const GSHCPU_Result cut = center ? gshcpu_render_stroke_cell(&s, c.quad, run->item_alloc, &cs->geometry, &cell)
                                   : gshcpu_render_stroke_cut(&s, c.quad, run->item_alloc, &cs->geometry);
  if (cut != GSHCPU_OK) {
    gshcpu_render_stroke_close(cache, &s);
    return cut == GSHCPU_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : scene_result(cut);
  }
  if (!s.drawn) {
    gshgeom_flat_path_destroy(cell);
    gshcpu_render_stroke_close(cache, &s);
    return GSHSCENE_OK;
  }
  if (L->guard) {
    period_guard_stroke(run, L, &s);
  }
  run->work += expansion_work(run, item, &s);
  GSHCPU_Rect rect, whole;
  uint64_t cells = 0;
  uint8_t * cov = run->coverage;
  GSHCPU_Exact * exact = cs->tiers & GSHCPU_RENDER_TIER_EXACT ? run->exact : NULL;
  GSHCPU_Stroke_Buffers buffers = {run->coverage, run->cov_aligned, run->cov_clip, run->cov_result, center, cell};
  GSHCPU_Result r = gshcpu_render_stroke_cover(&s, reach, wreach, cs->max_deposits, run->acc,
      2u * ((size_t)run->layout.width + 1u) * GSHCPU_RENDER_BAND_ROWS, exact, (cs->tiers & GSHCPU_RENDER_TIER_JOINT) != 0, &buffers, &cov,
      &rect, &whole, &cells);
  run->acc_zero = 0;
  run->work += cells + exact_work(run);
  if (r != GSHCPU_OK) {
    gshgeom_flat_path_destroy(cell);
    gshcpu_render_stroke_close(cache, &s);
    return scene_result(r);
  }
  gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, strokes));
  result = pat_paint_copy(run, L, u, &c, rect, whole, cov, NULL); /* S4: the paint is made ready over the whole render's part, drawn or not */
  gshgeom_flat_path_destroy(cell);
  gshcpu_render_stroke_close(cache, &s);
  return result;
}

static GSHSCENE_Result pat_stroke(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  Pat_Level * L = pr(run)->pat;
  Frame * frame = &run->frames[run->top];
  Pat_Unit u = {item, chain, count, frame, reach_of(run, frame->clip_base, run->clip_count),
      whole_reach_of(run, frame->clip_base, run->clip_count)};
  if (gshcpu_render_rect_empty(u.wreach)) {
    return GSHSCENE_OK; /* nothing of the unit is in the whole render */
  }
  L->urect.x1 = L->urect.x0;
  L->urect.y1 = L->urect.y0;
  /* S4: the copies of the whole render, not only the ones the damage draws, so that a limit met inside a copy (a cell cap, a gradient
   * or an image past G14's domain, a coordinate past the range) refuses the use whatever the damage; a copy the damage does not reach
   * is covered and tested over the whole render's part of it and draws nothing. */
  GSHSCENE_Result r = pat_each(run, L, u.wreach, pat_stroke_copy, &u);
  L->base = L->base0;
  if (r == GSHSCENE_OK) {
    pat_finish(run, L, frame);
  }
  return r;
}

/** Walk the tile's units once (all the copies of the pass, or the one copy of an overlapping pass) into the lattice layer. */
static GSHSCENE_Result pat_walk(Run * run, Pat_Level * L, const GSHSCENE_List * tile) {
  Mask_State * ms = ms_of(run);
  const size_t bytes = gshscene_expansion_bytes(tile);
  GSHSCENE_Expansion walk;
  if (ms->walk_used > ms->walk_bytes || bytes > ms->walk_bytes - ms->walk_used ||
      gshscene_expansion_init(&walk, tile, ms->walk + ms->walk_used, bytes) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL; /* unreachable: the layout counts every walk a pass can have open */
  }
  ms->walk_used += bytes;
  const GSHSCENE_Item * item;
  const size_t * inner;
  size_t depth;
  GSHSCENE_Result result = GSHSCENE_OK;
  (void)L;
  while (result == GSHSCENE_OK && gshscene_expansion_next(&walk, &item, &inner, &depth)) {
    run->work = 0;
    result = unit(run, item, inner, depth);
    gshscene_job_work(run->job, run->work);
    run->work = 0;
    if (result == GSHSCENE_OK && gshscene_job_checkpoint(run->job)) {
      result = GSHSCENE_ERR_CANCELLED;
    }
  }
  ms->walk_used -= bytes;
  return result;
}

static GSHSCENE_Result pat_overlap_copy(Run * run, Pat_Level * L, int64_t i, int64_t j, void * ctx) {
  /* PC6: later copies over earlier, in lattice order: the tile drawn for this copy alone, onto the lattice layer. */
  L->single = 1;
  L->walks++;
  L->ci = i;
  L->cj = j;
  const GSHSCENE_Result r = pat_walk(run, L, ctx);
  L->single = 0;
  return r;
}

/* --- the period tier (PC9) and its cache (PC10) --------------------------------------------------------------- */

/** What a tile holds that decides the period tier and the cache key (found by walking the tile once for each pass). */
typedef struct Tile_Facts {
  int pattern;               /* a pattern paints a unit: the nested count of PC8 is of the outer copies drawn, which the window does not draw all of */
  int masked;                /* a group has a soft mask: its picture is placed once in the pass's frame, not once for each copy */
  int reads_fg;              /* a paint reads the list's foreground or palette (PC10: the key holds them) */
  int union_clip;            /* a union clip is pushed (CU21): its limits are those of a group of its members in a copy, which the window's copies do not vouch for */
} Tile_Facts;

static int paint_reads_fg(const GSHSCENE_Paint * paint) {
  return paint->kind == GSHSCENE_PAINT_FOREGROUND || paint->kind == GSHSCENE_PAINT_PALETTE;
}

/** The deepest picture an instance chain is followed to; deeper is reported as holding a pattern (the tier is not taken). */
#define TILE_FACTS_DEPTH 64u

static void tile_facts(const GSHSCENE_List * list, unsigned depth, Tile_Facts * f) {
  if (depth > TILE_FACTS_DEPTH) {
    f->pattern = 1;
    return;
  }
  const size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      break;
    }
    if (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) {
      GSHSCENE_Gradient g;
      GSHSCENE_Image_Paint ip;
      if (item->paint.kind == GSHSCENE_PAINT_PATTERN) {
        f->pattern = 1;
      } else if (paint_reads_fg(&item->paint)) {
        f->reads_fg = 1;
      } else if (item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
        if (gshscene_resource_gradient(item->paint.resource, &g) != GSHSCENE_OK) {
          f->reads_fg = 1;
        } else {
          for (size_t k = 0; k < g.stop_count && g.stops; k++) {
            f->reads_fg = f->reads_fg || paint_reads_fg(&g.stops[k].color);
          }
        }
      } else if (item->paint.kind == GSHSCENE_PAINT_IMAGE) {
        f->reads_fg = f->reads_fg ||
            gshscene_resource_image_paint(item->paint.resource, &ip) != GSHSCENE_OK || (ip.tint && paint_reads_fg(ip.tint));
      }
    } else if (item->kind == GSHSCENE_ITEM_CLIP_PUSH && gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
      f->union_clip = 1;
    } else if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {
      f->masked = 1;
    } else if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      tile_facts(gshscene_resource_picture(item->resource), depth + 1u, f);
    }
  }
}

/**
 * PC9: whether the period tier applies to the pass @p L over @p rect, and if so its window.  The step vectors are integer vectors
 * with a determinant of at most the layer's pixels (LT3), both axes repeat, the window of LT3 moved to the rectangle's corner lies in
 * the rectangle (so that its `p s` pixels are at most the layer's), and the tile holds no pattern (PC8's nested count is of the outer
 * copies drawn), no mask (its picture is placed once in the pass's frame, not once for each copy) and no union clip (CU21: the limits of
 * a union are those of the groups of its members in a copy, and a copy elsewhere may lie more inside the render than the window's).  @p facts is the tile's, found
 * here once the cheaper conditions hold (the cache key needs it too).
 */
static int period_plan(const Run * run, Pat_Level * L, const GSHSCENE_List * tile, GSHCPU_Rect rect, Tile_Facts * facts) {
  if (!(run->cs->tiers & GSHCPU_RENDER_TIER_PERIOD)) {
    return 0;
  }
  int64_t a[2];
  int64_t b[2];
  int integer = 0;
  GSHGEOM_Lattice_Period per;
  int64_t tx, ty;
  GSHGEOM_Map canon;
  if (gshgeom_lattice_integer_steps(&L->C, &L->lat, a, b, &integer) != GSHGEOM_OK || !integer ||
      gshgeom_lattice_period(a, b, &per) != GSHGEOM_OK ||
      gshgeom_lattice_canonical(&L->C, &L->lat, 0, 0, &canon, &tx, &ty) != GSHGEOM_OK) {
    return 0;
  }
  if (per.p > rect.x1 - rect.x0 || per.s > rect.y1 - rect.y0) {
    return 0;
  }
  tile_facts(tile, 0, facts);
  if (facts->pattern || facts->masked || facts->union_clip) {
    return 0;
  }
  L->per = per;
  L->wx = rect.x0;
  L->wy = rect.y0;
  L->tx = tx;
  L->ty = ty;
  L->canon0 = canon;
  return 1;
}

static uint64_t space_word(const GSHSCENE_Color_Space * s) {
  return (uint64_t)(uint32_t)s->primaries | ((uint64_t)(uint32_t)s->transfer << 32);
}

/** PC10: the key of the period raster of the pass @p L for the pattern @p resource. */
static void period_key(const Run * run, const Pat_Level * L, const GSHSCENE_Resource * resource, const Frame * frame, const Tile_Facts * facts,
    GSHCPU_Period_Key * k) {
  memset(k, 0, sizeof *k);
  k->pattern_id = gshscene_resource_id(resource);
  k->tile_id = gshscene_resource_id(L->pat.tile);
  for (int i = 0; i < 4; i++) {
    k->linear[i] = gshgeom_f64_bits(L->canon0.m[i / 2][i % 2]);
  }
  k->fraction[0] = gshgeom_f64_bits(L->canon0.t[0]);
  k->fraction[1] = gshgeom_f64_bits(L->canon0.t[1]);
  k->layer_space[0] = space_word(&frame->space);
  k->layer_space[1] = gshgeom_f64_bits(frame->space.gamma);
  const GSHSCENE_Color_Space * target = gshscene_job_target_space(run->job);
  k->target_space[0] = space_word(target);
  k->target_space[1] = gshgeom_f64_bits(target->gamma);
  if (facts->reads_fg) {
    k->reads_foreground = 1;
    for (int i = 0; i < 4; i++) {
      k->foreground[i] = gshgeom_f64_bits(run->foreground.rgba[i]);
    }
    k->foreground_space[0] = space_word(&run->foreground.space);
    k->foreground_space[1] = gshgeom_f64_bits(run->foreground.space.gamma);
    k->palette_id = run->palette ? gshscene_resource_id(run->palette) : 0u;
  }
  k->revisions[0] = GSHGEOM_STROKE_REVISION;
  k->revisions[1] = GSHGEOM_FLATTEN_REVISION;
  k->revisions[2] = GSHGEOM_CLIP_REVISION;
  k->revisions[3] = GSHGEOM_LATTICE_REVISION;
}

/**
 * The window pixel (@p u, @p v) of the class order of the raster: the pixel w = (wx + ..) of the window whose class is rep(w - t).  The
 * raster is indexed `v * p + u` by the class `(u, v) = rep(w - t)`; @p w_to_raster 1 copies the layer's window into @p raster, 0 the other way.
 */
static void period_copy(Run * run, const Pat_Level * L, const Frame * f, double * raster, int w_to_raster) {
  for (int64_t v = 0; v < L->per.s; v++) {
    for (int64_t u = 0; u < L->per.p; u++) {
      int64_t r[2];
      gshgeom_lattice_reduce(&L->per, L->wx + u - L->tx, L->wy + v - L->ty, r);
      double * cell = &raster[4u * (size_t)(r[1] * L->per.p + r[0])];
      double * px = &f->layer[4u * at(run, L->wx + u, L->wy + v)];
      if (w_to_raster) {
        memcpy(cell, px, 4u * sizeof(double));
      } else {
        memcpy(px, cell, 4u * sizeof(double));
      }
    }
  }
}

/** The pass over @p rect: the tile's units over all the copies, or (PC6) the tile once for each copy in lattice order. */
static GSHSCENE_Result pat_pass(Run * run, Pat_Level * L, const GSHSCENE_List * tile, int overlaps, GSHCPU_Rect rect) {
  if (!overlaps) {
    return pat_walk(run, L, tile);
  }
  /* PC6: the copies one at a time, in lattice order. */
  return pat_each(run, L, rect, pat_overlap_copy, (void *)(uintptr_t)tile);
}

/**
 * PC2: a pattern paint begun: the lattice layer of the use (the tile's units over the copies, drawn into an isolated layer over
 * @p rect), which painter_span then reads for each pixel of the fill's coverage.  @p rect and @p whole are in the frame of
 * coordinates the fill is in (inside a tile, the copy's canonical frame, which `pr(run)->pshx` and `pr(run)->pshy` relate to the pass's).
 */
static GSHSCENE_Result pattern_begin(Painter * p, Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count, Frame * frame,
    GSHCPU_Rect rect, GSHCPU_Rect whole) {
  Cpu_Sink * cs = run->cs;
  p->pattern = 1;
  p->period = 0;
  p->gradient = 0;
  p->image = 0;
  p->refused = 0;
  p->run = run;
  p->frame = frame;
  if (gshcpu_render_rect_empty(whole)) {
    p->refused = 1; /* nothing of it is in the whole render */
    return GSHSCENE_OK;
  }
  GSHSCENE_Pattern pt;
  if (pr(run)->pat_depth >= pr(run)->pat_levels || gshscene_resource_pattern(item->paint.resource, &pt) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  Pat_Level * L = pat_level(run, pr(run)->pat_depth);
  /* The level's memory (pat_offsets). */
  Pat_Offsets po;
  (void)pat_offsets(run->layout.pixels, run->layout.aligned, run->layout.clips, run->layout.levels, &po);
  unsigned char * lb = (unsigned char *)(void *)L;
  L->sum8 = (uint32_t *)(void *)(lb + po.at_sum8);
  L->cnt = lb + po.at_cnt;
  L->acc = (double *)(void *)(lb + po.at_acc);
  L->coverage = lb + po.at_coverage;
  L->rem = lb + po.at_rem;
  L->cov_aligned = run->layout.aligned ? lb + po.at_aligned : NULL;
  L->cov_clip = run->layout.aligned ? lb + po.at_clip : NULL;
  L->cov_result = run->layout.aligned ? lb + po.at_result : NULL;
  L->tmp = lb + po.at_tmp;
  L->pclips = (Pat_Clip *)(void *)(lb + po.at_pclips);
  L->pmaps = (GSHGEOM_Map *)(void *)(lb + po.at_pmaps);
  L->pat = pt;
  L->lat.x = pt.x;
  L->lat.y = pt.y;
  L->lat.w = pt.w;
  L->lat.h = pt.h;
  L->lat.x_step = pt.x_step;
  L->lat.y_step = pt.y_step;
  L->lat.repeat_x = pt.repeat_x;
  L->lat.repeat_y = pt.repeat_y;
  run->maps[count - 1] = item->map;
  run->maps[count] = pt.map;
  if (gshgeom_map_compose_chain(run->maps, count + 1, target_map(run), &L->C) != GSHGEOM_OK) {
    p->refused = 1;
    return refuse(run, item, chain, count);
  }
  /* PC8: the copies of the use, counted by LT1 over what the whole render draws of it, at every level, summed. */
  if (pr(run)->pat_depth == 0) {
    pr(run)->pat_total = 0;
  }
  uint64_t remaining = cs->max_pattern_cells;
  remaining = remaining == UINT64_MAX ? (uint64_t)GSHGEOM_LATTICE_INDEX_MAX : remaining;
  remaining = remaining > (uint64_t)GSHGEOM_LATTICE_INDEX_MAX ? (uint64_t)GSHGEOM_LATTICE_INDEX_MAX : remaining;
  remaining = pr(run)->pat_total >= remaining ? 0 : remaining - pr(run)->pat_total;
  const int64_t cr[4] = {whole.x0, whole.y0, whole.x1, whole.y1};
  uint64_t n = 0;
  if (gshgeom_lattice_each(&L->C, &L->lat, cr, remaining, NULL, NULL, &n) != GSHGEOM_OK) {
    p->refused = 1;
    return refuse(run, item, chain, count);
  }
  pr(run)->pat_total += n;
  if (n == 0) {
    p->refused = 1;
    return GSHSCENE_OK;
  }
  /* PC7: the tint converted once. */
  p->has_tint = pt.has_tint != 0;
  p->alpha = item->paint.rgba[3];
  if (p->has_tint) {
    paint_source(run, &pt.tint, frame, p->tint);
  }
  L->base = L->C;
  L->base.t[0] = 0.0;
  L->base.t[1] = 0.0;
  L->base.t[2] = 0.0;
  L->base0 = L->base;
  L->single = 0;
  L->walks = 0;
  L->copies = 0;
  L->arena_mark = run->item_alloc == &run->arena.self ? run->arena.used : 0;
  L->urect.x0 = L->urect.x1 = rect.x0;
  L->urect.y0 = L->urect.y1 = rect.y0;
  const int overlaps = (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);
  /* PC9: the period tier's window, when the pass is eligible: the pass then draws the window and not the use's rectangle. */
  const GSHSCENE_List * tile = gshscene_resource_picture(pt.tile);
  Tile_Facts facts;
  memset(&facts, 0, sizeof facts);
  L->guard = 0;
  L->abort_tier = 0;
  L->period = period_plan(run, L, tile, rect, &facts);
  GSHCPU_Rect prect = rect;
  if (L->period) {
    prect.x1 = prect.x0 + L->per.p;
    prect.y1 = prect.y0 + L->per.s;
  }

  /* The state the pass changes and restores. */
  Mask_State * ms = ms_of(run);
  const size_t saved_top = run->top;
  const size_t saved_clips = run->clip_count;
  const size_t saved_open = run->groups_open;
  const Skip saved_skip = run->skip;
  const GSHCPU_Rect saved_window = ms->window;
  const GSHCPU_Rect saved_ms_whole = ms->whole_window;
  const GSHCPU_Rect saved_whole_cur = pr(run)->whole_cur;
  const int64_t saved_ox = pr(run)->org_x;
  const int64_t saved_oy = pr(run)->org_y;
  GSHGEOM_Map * const saved_maps = run->maps;
  Pat_Level * const saved_pat = pr(run)->pat;
  uint8_t * const sv_cov = run->coverage;
  uint8_t * const sv_al = run->cov_aligned;
  uint8_t * const sv_cl = run->cov_clip;
  uint8_t * const sv_re = run->cov_result;
  const double sv_dummy = 0.0;
  (void)sv_dummy;

  pr(run)->org_x -= pr(run)->pshx;
  pr(run)->org_y -= pr(run)->pshy;
  p->lox = pr(run)->org_x;
  p->loy = pr(run)->org_y;
  pr(run)->whole_cur = whole;
  ms->window = prect;
  ms->whole_window = whole;
  memset(&run->skip, 0, sizeof run->skip);
  run->coverage = L->coverage;
  run->cov_aligned = L->cov_aligned;
  run->cov_clip = L->cov_clip;
  run->cov_result = L->cov_result;
  run->maps = saved_maps + count + 1u;
  pr(run)->pat = L;
  pr(run)->pat_depth++;
  const int64_t sh_x = pr(run)->pshx;
  const int64_t sh_y = pr(run)->pshy;
  const int was_quiet = pr(run)->quiet;
  pr(run)->pshx = 0;
  pr(run)->pshy = 0;
  pr(run)->quiet = 0;
  /* The lattice layer: an isolated layer in the frame's space that starts at (0, 0, 0, 0) (N6), made ready as the units reach it. */
  Frame * f = &run->frames[++run->top];
  memset(f, 0, sizeof *f);
  f->layer = run->layers + (run->top - 1u) * run->layout.pixels * 4u;
  f->space = frame->space;
  f->group.opacity = 1.0;
  f->group.composite = GSHSCENE_COMPOSITE_SRC_OVER;
  f->group.isolation = GSHSCENE_ISOLATION_ISOLATED;
  f->clip_base = run->clip_count;
  if (run->general) {
    Gen_Frame * g = &gs_of(run)->frames[run->top];
    memset(g, 0, sizeof *g);
    g->on = (cs->tiers & GSHCPU_RENDER_FORCE_GENERAL) != 0;
    f->general = g->on;
  }
  memset(&ms->frames[run->top].whole_dirty, 0, sizeof ms->frames[run->top].whole_dirty);
  L->frame = run->top;
  for (int64_t y = prect.y0; y < prect.y1; y++) {
    memset(L->cnt + at(run, prect.x0, y), 0, (size_t)(prect.x1 - prect.x0));
  }
  GSHSCENE_Result result = GSHSCENE_OK;
  /* PC10: a period raster kept for this pattern, linear part and fraction is the window, bit for bit. */
  GSHCPU_Stroke_Cache * pcache = L->period && !run->caller && (cs->tiers & GSHCPU_RENDER_TIER_CACHE) ? cs->cache : NULL;
  GSHCPU_Period_Key pkey;
  int served = 0;
  if (pcache) {
    void * entry = NULL;
    const double * raster = NULL;
    period_key(run, L, item->paint.resource, frame, &facts, &pkey);
    if (gshcpu_period_cache_acquire(pcache, &pkey, &entry, &raster)) {
      widen(run, f, prect);
      period_copy(run, L, f, (double *)(void *)raster, 0);
      gshcpu_stroke_cache_release(pcache, entry);
      served = 1;
    }
  }
  if (!served) {
    L->guard = L->period;
    result = pat_pass(run, L, tile, overlaps, L->period ? prect : whole);
    L->guard = 0;
    if (L->period && L->abort_tier && (result == GSHSCENE_OK || result == GSHSCENE_ERR_LIMIT)) {
      /* PC9: a copy the window cannot vouch for: give the window up, and draw the use's rectangle by the general tier. */
      gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, period_aborts));
      if (run->item_alloc == &run->arena.self) {
        gshcpu_render_arena_rewind(&run->arena, L->arena_mark);
      }
      L->period = 0;
      L->abort_tier = 0;
      L->copies = 0;
      ms->window = rect;
      run->top = L->frame;
      run->clip_count = f->clip_base;
      run->groups_open = saved_open;
      memset(&run->skip, 0, sizeof run->skip);
      memset(&f->dirty, 0, sizeof f->dirty);
      memset(&ms->frames[run->top].whole_dirty, 0, sizeof ms->frames[run->top].whole_dirty);
      L->urect.x0 = L->urect.x1 = rect.x0;
      L->urect.y0 = L->urect.y1 = rect.y0;
      for (int64_t y = rect.y0; y < rect.y1; y++) {
        memset(L->cnt + at(run, rect.x0, y), 0, (size_t)(rect.x1 - rect.x0));
      }
      result = pat_pass(run, L, tile, overlaps, whole);
    } else if (L->period && result == GSHSCENE_OK) {
      widen(run, f, prect); /* the window made ready whole: the pixels no unit reached are transparent */
      if (pcache && gshcpu_period_cache_fits(pcache, (size_t)(L->per.p * L->per.s))) {
        void * entry = NULL;
        double * raster = gshcpu_period_cache_make(pcache, (size_t)(L->per.p * L->per.s));
        if (!raster) {
          result = GSHSCENE_ERR_OOM;
        } else {
          period_copy(run, L, f, raster, 1);
          const int kept = gshcpu_period_cache_store(pcache, &pkey, raster, (size_t)(L->per.p * L->per.s), &entry);
          if (kept == 1 || kept == 2) {
            gshcpu_stroke_cache_release(pcache, entry);
          }
          if (kept != 1) {
            gshcpu_period_cache_free(pcache, raster);
          }
          if (kept < 0) {
            result = GSHSCENE_ERR_OOM;
          }
        }
      }
    }
  }
  if (L->period && result == GSHSCENE_OK) {
    gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, pattern_period));
  }
  /* Restore. */
  if (run->item_alloc == &run->arena.self) {
    gshcpu_render_arena_rewind(&run->arena, L->arena_mark);
  }
  gshcpu_stroke_cache_count(cs->cache, offsetof(GSHCPU_Render_Counters, patterns));
  gshcpu_stroke_cache_add(cs->cache, offsetof(GSHCPU_Render_Counters, pattern_copies), L->copies);
  pr(run)->pat = saved_pat;
  pr(run)->pat_depth--;
  run->maps = saved_maps;
  run->coverage = sv_cov;
  run->cov_aligned = sv_al;
  run->cov_clip = sv_cl;
  run->cov_result = sv_re;
  run->acc_zero = 0;
  ms->window = saved_window;
  ms->whole_window = saved_ms_whole;
  pr(run)->whole_cur = saved_whole_cur;
  pr(run)->org_x = saved_ox;
  pr(run)->org_y = saved_oy;
  run->skip = saved_skip;
  run->top = saved_top;
  run->clip_count = saved_clips;
  run->groups_open = saved_open;
  pr(run)->pshx = sh_x;
  pr(run)->pshy = sh_y;
  pr(run)->quiet = was_quiet;
  if (result == GSHSCENE_ERR_LIMIT) {
    p->refused = 1;
    return refuse(run, item, chain, count);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  p->lf = &run->frames[saved_top + 1u];
  p->period = L->period;
  if (L->period) {
    p->per = L->per;
    p->wx = L->wx;
    p->wy = L->wy;
  }
  if (!quiet_of(run)) {
    widen_w(run, frame, rect, whole);
  }
  run->work += (uint64_t)(rect.x1 - rect.x0) * (uint64_t)(rect.y1 - rect.y0);
  return GSHSCENE_OK;
}

/** Go through one unit.  @return ::GSHSCENE_OK to go on, or the result to stop with. */
static uint64_t run_tests(const Run * run) {
  return ((uint64_t)run->union_tests_hi << 32) | run->union_tests_lo;
}

/**
 * CU21: whether the unit in hand is in a walk of a pattern's tile that an earlier walk of the same pass has repeated: the tile of
 * overlapping cells (PC6) is walked once for each copy, and a union push in it is the same push each time.  The predicate's comparisons
 * are those of the push, billed once for a pass by its first walk (CU19), so that the count does not depend on how many copies the
 * pass draws.  A tile inside a tile is another pass for each copy of the outer one that paints it, as PC8 counts its copies, and is billed
 * as one.
 */
static int tile_repeat_walk(const Run * run) {
  const Pat_Level * L = cur_pat(run);
  return L && L->single && L->walks > 1;
}

static void run_tests_add(Run * run, uint64_t n) {
  const uint64_t sum = run_tests(run) + n;
  run->union_tests_lo = (uint32_t)(sum & 0xffffffffu);
  run->union_tests_hi = (uint32_t)(sum >> 32);
}

static GSHSCENE_Result unit(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  if (skip_step(&run->skip, item, count)) {
    return GSHSCENE_OK;
  }
  GSHSCENE_Disposition d = gshscene_caps_disposition(&run->job->caps, item, run->maps, count - 1,
      target_map(run), gshscene_job_target_space(run->job), run->palette);
  GSHSCENE_Glyph_Route route;
  if (d.entry_class == GSHSCENE_ENTRY_REFUSED ||
      (gshcpu_render_glyph_route(item, run->maps, count - 1, target_map(run), &route) &&
          route.entry_class == GSHSCENE_ENTRY_REFUSED)) {
    /* The planning pass reported it. */
    skip_begin(&run->skip, item, count);
    return GSHSCENE_OK;
  }
  if (item->kind == GSHSCENE_ITEM_STROKE
          ? gshscene_stroke_notice(item, run->maps, count - 1, target_map(run))
          : gshscene_paint_notice(item, run->maps, count - 1, target_map(run))) {
    return GSHSCENE_OK; /* S17, SC4: a gradient fill or a stroke that draws nothing; the plan wrote its notice */
  }
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH && gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
    /* CU19: the predicate's comparisons for every union push of the render that is not skipped, in unit order, whatever the damage
     * excludes; the push at which the running total passes the limit is the sink's resource limit. */
    GSHSCENE_Union_Verdict verdict;
    run->union_general = 1;
    if (!tile_repeat_walk(run) &&
        gshscene_clip_union_disjoint(item, run->maps, count - 1, target_map(run), &verdict) == GSHSCENE_OK) {
      /* FT-U1 draws the union when it is on and the members are provably disjoint; else the general tier (CG10), which also compares
       * every pair of boxes for its groups (CG2) and so bills `n (n - 1) / 2` comparisons more. */
      run->union_general = !((run->cs->tiers & GSHCPU_RENDER_TIER_UNION) && verdict.disjoint);
      uint64_t tests = verdict.tests;
      if (run->union_general) {
        const uint64_t n = (uint64_t)gshscene_resource_clip_union_count(item->resource);
        tests += n * (n - 1u) / 2u;
      }
      run_tests_add(run, tests);
      run->cs->counters->union_tests += tests;
      if (run->cs->max_union_tests != UINT64_MAX && run_tests(run) > run->cs->max_union_tests) {
        return refuse(run, item, chain, count);
      }
    }
  }
  switch (item->kind) {
    case GSHSCENE_ITEM_FILL: {
      GSHSCENE_Item hatched;
      if (hatch_fill_item(item, &hatched)) {
        /* HX13: drawn as a stroke of its lines; the chain, the clips and the layer are the fill's. */
        return cur_pat(run) ? pat_stroke(run, &hatched, chain, count) : stroke(run, &hatched, chain, count);
      }
      return cur_pat(run) ? pat_fill(run, item, chain, count) : fill(run, item, chain, count);
    }
    case GSHSCENE_ITEM_STROKE:
      return cur_pat(run) ? pat_stroke(run, item, chain, count) : stroke(run, item, chain, count);
    case GSHSCENE_ITEM_CLIP_PUSH:
      return cur_pat(run) ? pat_clip_push(run, item, chain, count) : clip_push(run, item, chain, count);
    case GSHSCENE_ITEM_CLIP_POP:
      run->clip_count--;
      return GSHSCENE_OK;
    case GSHSCENE_ITEM_GROUP_PUSH:
      group_push(run, item, chain[count - 1u]);
      return GSHSCENE_OK;
    case GSHSCENE_ITEM_GROUP_POP:
      return group_pop(run, chain, count);
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
  if (run->batch) {
    /* The strokes queued are drawn, whatever stopped the render: they are the prefix it reports as done. */
    GSHCPU_Result fr = gshcpu_batch_flush(run->batch);
    gshcpu_batch_destroy(run->batch);
    run->batch = NULL;
    if (fr != GSHCPU_OK && result == GSHSCENE_OK) {
      result = scene_result(fr);
    }
  }
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
  if (!run->fast8) {
    write_target(run);
  } else if (run->straight8) {
    const GSHCPU_Rect d = run->frames[0].dirty;
    for (int64_t y = d.y0; y < d.y1 && d.x0 < d.x1; y++) {
      gshcpu_direct8_store_straight_row(run->pixels + (size_t)y * run->stride + 4u * (size_t)d.x0,
          run->frames[0].px8 + (size_t)(y - run->frames[0].oy) * run->frames[0].stride8 + 4u * (size_t)(d.x0 - run->frames[0].ox),
          (size_t)(d.x1 - d.x0));
    }
  }
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
  Mask_Layout M;
  Gen_Layout G;
  GSHCPU_Result lr = layout(cs, request->list, target, &L, &M, &G, request);
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
  run->masked = M.bytes != 0;
  run->general = G.on != 0;
  if (run->general) {
    /* KC3: the state after the Frame array, and the channels (KC8) where the layout put them. */
    Gen_State * gs = gs_of(run);
    gs->ag = (double *)(void *)(base + G.at_ag);
    gs->fg = (double *)(void *)(base + G.at_fg);
    gs->ag_used = 0;
    gs->fg_used = 0;
    gs->frames = (Gen_Frame *)(void *)((unsigned char *)(void *)gs + align_up(sizeof(Gen_State)));
    memset(gs->frames, 0, (L.groups + 1u) * sizeof *gs->frames);
    gshcpu_stroke_cache_raise(cs->cache, offsetof(GSHCPU_Render_Counters, group_alpha_slots), G.ag_slots);
    gshcpu_stroke_cache_raise(cs->cache, offsetof(GSHCPU_Render_Counters, group_shape_slots), G.fg_slots);
  }
  if (run->masked) {
    /* MC5: the area after the Run (layout): the window every unit is held to starts as the region. */
    Mask_State * ms = ms_of(run);
    unsigned char * area = base + M.at;
    ms->window = L.region;
    ms->whole_window = L.whole;
    memset(&ms->P, 0, sizeof ms->P);
    ms->P.org_x = L.region.x0;
    ms->P.org_y = L.region.y0;
    ms->P.whole_cur = L.whole;
    ms->P.pat_mem = base + M.at_pat;
    ms->P.pat_levels = M.pat_levels;
    ms->P.pat_stride = M.pat_stride;
    ms->frames = (Mask_Frame *)(void *)(area + M.at_frames);
    ms->chain = (size_t *)(void *)(area + M.at_chain);
    ms->walk = area + M.at_walk;
    ms->walk_bytes = M.walk;
    ms->walk_used = 0;
    memset(ms->frames, 0, (L.groups + 1u) * sizeof *ms->frames);
  }
  if (L.strokes) {
    size_t width = (size_t)(L.whole.x1 - L.whole.x0);
    gshcpu_exact_init(&run->exact[0], base + L.at_exact, width, cs->max_exact, L.aligned ? 3 : 1);
    run->exact[0].eps = GSHCPU_RENDER_EXACT_EPS;
    run->exact[0].max_crossings = cs->max_crossings;
    if (L.aligned) {
      gshcpu_exact_init(&run->exact[1], base + L.at_exact + L.exact_second, width, cs->max_exact, 0);
      gshcpu_exact_init(&run->exact[2], base + L.at_exact + L.exact_third, width, cs->max_exact, 0);
      run->exact[1].eps = GSHCPU_RENDER_EXACT_EPS;
      run->exact[2].eps = GSHCPU_RENDER_EXACT_EPS;
      run->exact[1].max_crossings = cs->max_crossings;
      run->exact[2].max_crossings = cs->max_crossings;
    }
  }
  run->fast8 = cs->fast_fill && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) != 0 &&
      (run->format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || run->format == GSHCPU_FORMAT_RGBA8_STRAIGHT) && !cs->observer && !L.wide;
  run->straight8 = run->fast8 && run->format == GSHCPU_FORMAT_RGBA8_STRAIGHT;
  run->direct8 = cs->fast && run->format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED && !cs->observer && L.groups == 0 && L.clips == 0 &&
      !L.aligned && !L.other && !G.on && (cs->tiers & GSHCPU_RENDER_TIER_SPARSE) != 0;
  if (run->direct8 && cs->executor.start && !caller) {
    /* Several threads: the strokes are queued and drawn over stripes.  Nothing is queued without the batch (out of memory), and
     * the render goes on as one thread does. */
    run->batch = gshcpu_batch_create(&cs->alloc, &cs->executor, (size_t)(L.region.x1 - L.region.x0), run->pixels, run->stride);
  }
  run->acc_zero = 0; /* the caller's memory is not zeroed: a sparse sweep clears what it uses before its first use */
  run->caller = caller;
  run->resolved = (Resolved *)(void *)(base + L.at_table);
  run->pool = (GSHCPU_Stop *)(void *)(base + L.at_pool);
  run->tables = (double *)(void *)(base + L.at_tables);
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
  root->bytes = run->fast8;
  if (run->general && (cs->tiers & GSHCPU_RENDER_FORCE_GENERAL)) {
    gs_of(run)->frames[0].on = 1; /* KC9's gate: the root's elements take the element step too */
    root->general = 1;
  }
  if (run->straight8) {
    /* The target is straight: the root is premultiplied bytes of its own (the room of the double root), loaded as the units reach it
     * and stored, the pixels that changed, at the end. */
    root->px8 = (unsigned char *)(void *)run->root;
    root->stride8 = 4u * run->layout.width;
    root->ox = run->layout.region.x0;
    root->oy = run->layout.region.y0;
  } else {
    root->px8 = run->pixels;
    root->stride8 = run->stride;
    root->ox = 0;
    root->oy = 0;
  }
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

static GSHCPU_Result sink_make(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits, const GSHCPU_Limits * limits,
    unsigned tiers, GSHCPU_Render_Counters * counters, GSHCPU_Render_Observer observer, void * observer_ctx,
    const GSHCPU_Executor * executor, GSHSCENE_Sink ** out) {
  if (!out) {
    return GSHCPU_ERR_INVALID;
  }
  if (executor && executor->threads > GSHCPU_EXECUTOR_MAX_THREADS) {
    return GSHCPU_ERR_INVALID;
  }
  if (limits && limits->max_exact_fragments > GSHCPU_LIMITS_MAX_EXACT_FRAGMENTS) {
    return GSHCPU_ERR_INVALID; /* UC9: the step is quadratic in a pixel's pieces: a value past the ceiling is refused, not clamped */
  }
  if (limits && limits->max_union_pieces > GSHCPU_LIMITS_MAX_UNION_PIECES) {
    return GSHCPU_ERR_INVALID; /* CG9: the tier's memory is a record and a piece for each: past the ceiling is refused, not clamped */
  }
  if (limits && limits->max_exact_crossings > GSHCPU_LIMITS_MAX_EXACT_CROSSINGS) {
    return GSHCPU_ERR_INVALID; /* UC19: and in its crossings, which the budget bounds: past the ceiling is refused, not clamped */
  }
  if (limits && limits->stroke_coverage > GSHCPU_STROKE_COVERAGE_FAST) {
    return GSHCPU_ERR_INVALID;
  }
  if (limits && limits->fill_coverage > GSHCPU_FILL_COVERAGE_FAST) {
    return GSHCPU_ERR_INVALID;
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
  cs->fast = limits && limits->stroke_coverage == GSHCPU_STROKE_COVERAGE_FAST;
  cs->fast_fill = limits && limits->fill_coverage == GSHCPU_FILL_COVERAGE_FAST;
  if (executor && executor->start && executor->wait && executor->threads > 1u) {
    cs->executor = *executor;
  }
  cs->counters = counters ? counters : &cs->own;
  memset(cs->counters, 0, sizeof *cs->counters);
  cs->elements = scene_limits && scene_limits->max_resource_elements ? scene_limits->max_resource_elements
                                                                       : GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS;
  cs->max_dimension = limits && limits->max_dimension ? limits->max_dimension : GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION;
  if (cs->max_dimension > GSHCPU_MAX_DIMENSION_CEILING) {
    cs->max_dimension = GSHCPU_MAX_DIMENSION_CEILING;
  }
  cs->max_bytes = limits && limits->max_bytes ? limits->max_bytes : GSHCPU_LIMITS_DEFAULT_MAX_BYTES;
  cs->max_deposits = limits && limits->max_deposits ? limits->max_deposits : GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS;
  cs->max_exact = limits && limits->max_exact_fragments ? limits->max_exact_fragments
                                                        : GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS;
  cs->max_crossings = limits && limits->max_exact_crossings ? limits->max_exact_crossings
                                                            : GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS;
  cs->max_area_pixel_work = limits && limits->max_area_pixel_work ? limits->max_area_pixel_work
                                                                  : GSHCPU_LIMITS_DEFAULT_MAX_AREA_PIXEL_WORK;
  cs->max_image_work = limits && limits->max_image_work ? limits->max_image_work : GSHCPU_LIMITS_DEFAULT_MAX_IMAGE_WORK;
  cs->max_pattern_cells = limits && limits->max_pattern_cells ? limits->max_pattern_cells : GSHCPU_LIMITS_DEFAULT_MAX_PATTERN_CELLS;
  cs->max_hatch_lines = limits && limits->max_hatch_lines ? limits->max_hatch_lines : GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES;
  cs->max_union_tests = limits && limits->max_union_tests ? limits->max_union_tests : GSHCPU_LIMITS_DEFAULT_MAX_UNION_TESTS;
  cs->max_union_pieces = limits && limits->max_union_pieces ? limits->max_union_pieces : GSHCPU_LIMITS_DEFAULT_MAX_UNION_PIECES;
  cs->union_members = scene_limits && scene_limits->max_clip_members ? scene_limits->max_clip_members : GSHSCENE_LIMITS_DEFAULT_MAX_CLIP_MEMBERS;
  gshgeom_limits_default(&cs->geometry);
  if (limits && limits->max_points) {
    cs->geometry.max_points = limits->max_points;
  }
  if (limits) {
    cs->geometry.max_dash_steps = limits->max_dash_steps;
    if (limits->max_work) {
      cs->geometry.max_work = limits->max_work;
    }
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

GSHCPU_Result gshcpu_render_sink_create_tiered(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, unsigned tiers, GSHCPU_Render_Counters * counters, GSHCPU_Render_Observer observer,
    void * observer_ctx, GSHSCENE_Sink ** out) {
  return sink_make(alloc, scene_limits, limits, tiers, counters, observer, observer_ctx, NULL, out);
}

GSHCPU_Result gshcpu_render_sink_create_with_executor(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, unsigned tiers, GSHCPU_Render_Counters * counters, const GSHCPU_Executor * executor,
    GSHSCENE_Sink ** out) {
  return sink_make(alloc, scene_limits, limits, tiers, counters, NULL, NULL, executor, out);
}

GSHCPU_Result gshcpu_sink_create_with_executor(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, const GSHCPU_Executor * executor, GSHSCENE_Sink ** out) {
  return sink_make(alloc, scene_limits, limits, GSHCPU_RENDER_TIERS_ALL, NULL, NULL, NULL, executor, out);
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
