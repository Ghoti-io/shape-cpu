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
 * A stroke in the CPU sink (render.h, SC1 to SC8; strokes story S4).
 *
 * A stroke is a fill with another coverage.  Which polygon, in which frame, and how its parts become one 8-bit
 * coverage is all this file decides; everything after the coverage (the clips, the layers, the operators, the paint
 * and its source) is the fill's, in sink.c.  The polygon itself is `shape-geometry`'s one expansion: nothing here
 * offsets, joins, caps or dashes anything (a second implementation of the stroke definition is the drift the
 * expansion exists to prevent).
 *
 * - **The frame (SC3).**  The composed map is split into its translation's whole pixels and a fraction in `[0, 1)`
 *   (`gshgeom_stroke_canonical_map`, the one function every sink that wants these bits calls); the expansion is made
 *   at the map with the fraction only, and the outline is covered moved by the whole pixels, which moves the
 *   rectangle covered the other way and copies nothing.  So the outline is a function of the centerline, the style,
 *   the linear part and the fraction, whatever the whole-pixel translation is: that is what lets a cache serve a
 *   translated stroke and a hit be the miss bit for bit.
 * - **The parts (SC5).**  The expansion returns up to three paths: the body, the aligned part and the clip path.
 *   Each is covered as a fill is (story 9's accumulation, 8 bits): `b` and `a` under nonzero, `c` under the clip's
 *   rule over the aligned part's rectangle.  The composition is SK5 as amended (E9), `k = k_b + k_a c' - k_b k_a c'`,
 *   in D8's integer arithmetic.
 * - **A run (ST1, ST8).**  A centerline that is a glyph run is the path ::gshscene_glyph_run_centerline defines, made on
 *   a miss only, expanded as any path is and cached under the run's resource id with the notes of its glyphs and the
 *   elements it holds, which a hit replays: a hit equals a miss in the report as well as in the pixels.
 * - **The limits (SC6).**  A part past `max_points`, the depth cap, a coordinate that the shift takes past
 *   ::GSHCPU_RENDER_COORD_LIMIT and deposits past `max_cells` (summed over the parts, counted over what the whole
 *   render draws of the item) are the sink's resource limit.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/shape-geometry/hash.h>
#include <ghoti.io/shape-scene/glyph.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/stroke.h>

#include <stddef.h>
#include <string.h>

/** What the cache accounts besides an outline's points and subpaths: a header for each part and for the outline. */
#define PART_OVERHEAD 64u

/** The pixel holding a 26.6 coordinate: the floor of @p v / 64, for either sign. */
static int64_t pixel_floor(int64_t v) {
  int64_t q = v / 64;
  return v % 64 != 0 && v < 0 ? q - 1 : q;
}

static const GSHGEOM_Flat_Path * part_of(const GSHGEOM_Stroke_Outline * o, int part) {
  switch (part) {
    case GSHCPU_STROKE_BODY:
      return gshgeom_stroke_outline_body(o);
    case GSHCPU_STROKE_ALIGNED:
      return gshgeom_stroke_outline_aligned(o);
    default:
      return gshgeom_stroke_outline_clip(o);
  }
}

/** What is known of @p o: each part's points and the extent of its 26.6 coordinates, and what a cache holds for it. */
static void meta_of(const GSHGEOM_Stroke_Outline * o, GSHCPU_Stroke_Meta * m) {
  memset(m, 0, sizeof *m);
  size_t bytes = PART_OVERHEAD;
  for (int p = 0; p < GSHCPU_STROKE_PARTS; p++) {
    const GSHGEOM_Flat_Path * path = part_of(o, p);
    if (!path) {
      continue;
    }
    m->present[p] = 1;
    size_t n = gshgeom_flat_path_point_count(path);
    const GSHGEOM_Device_Point * points = gshgeom_flat_path_points(path);
    m->points[p] = n;
    for (size_t i = 0; i < n; i++) {
      int64_t x = points[i].x;
      int64_t y = points[i].y;
      if (i == 0 || x < m->lo_x[p]) {
        m->lo_x[p] = x;
      }
      if (i == 0 || y < m->lo_y[p]) {
        m->lo_y[p] = y;
      }
      if (i == 0 || x > m->hi_x[p]) {
        m->hi_x[p] = x;
      }
      if (i == 0 || y > m->hi_y[p]) {
        m->hi_y[p] = y;
      }
    }
    bytes += PART_OVERHEAD + n * sizeof(GSHGEOM_Device_Point) + gshgeom_flat_path_subpath_count(path) * sizeof(GSHGEOM_Flat_Subpath);
  }
  m->bytes = bytes;
}

static GSHCPU_Rect empty_rect(void) {
  GSHCPU_Rect r;
  r.x0 = r.y0 = r.x1 = r.y1 = 0;
  return r;
}

/** The notes of a run's glyphs that have an entry, in glyph order, as a stroke's own records (NULL and 0 for none). */
static GSHCPU_Result collect_notes(const GSHGEOM_Allocator * alloc, const GSHSCENE_Glyph_Note * notes, size_t count,
    GSHCPU_Run_Note ** out, size_t * out_count) {
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    n += notes[i].entry_class != 0;
  }
  *out = NULL;
  *out_count = n;
  if (n == 0) {
    return GSHCPU_OK;
  }
  GSHCPU_Run_Note * records = alloc->malloc_fn(alloc->ctx, n * sizeof *records);
  if (!records) {
    return GSHCPU_ERR_OOM;
  }
  size_t at = 0;
  for (size_t i = 0; i < count; i++) {
    if (notes[i].entry_class) {
      records[at].index = i;
      records[at].entry_class = notes[i].entry_class;
      records[at].reason = notes[i].reason;
      at++;
    }
  }
  *out = records;
  return GSHCPU_OK;
}

GSHCPU_Result gshcpu_render_stroke_open(GSHCPU_Stroke_Cache * cache, const GSHGEOM_Allocator * alloc,
    const GSHGEOM_Limits * limits, const GSHSCENE_Item * item, const GSHGEOM_Map * composed, GSHCPU_Stroke * out) {
  memset(out, 0, sizeof *out);
  /* SC2: the style, converted row by row by shape-scene's one conversion. */
  GSHGEOM_Stroke params;
  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);
  GSHSCENE_Glyph_Run glyphs;
  memset(&glyphs, 0, sizeof glyphs);
  int is_run = !path && gshscene_resource_glyph_run(item->resource, &glyphs) == GSHSCENE_OK;
  if ((!path && !is_run) || gshscene_resource_stroke_geometry(item->style, &params) != GSHSCENE_OK) {
    return GSHCPU_ERR_INTERNAL; /* a style the store holds passes the check; a run the plan cannot draw is refused by it */
  }
  /* SC3: the canonical map. */
  GSHGEOM_Map canon;
  int64_t sx = 0;
  int64_t sy = 0;
  GSHGEOM_Result g = gshgeom_stroke_canonical_map(composed, &canon, &sx, &sy);
  if (g != GSHGEOM_OK) {
    return g == GSHGEOM_ERR_LIMIT ? GSHCPU_ERR_LIMIT : GSHCPU_ERR_INTERNAL;
  }
  GSHCPU_Stroke_Key key;
  memset(&key, 0, sizeof key);
  key.path_id = gshscene_resource_id(item->resource);
  key.style_id = gshscene_resource_id(item->style);
  for (int i = 0; i < 4; i++) {
    key.linear[i] = gshgeom_f64_bits(canon.m[i / 2][i % 2]);
  }
  key.fraction[0] = gshgeom_f64_bits(canon.t[0]);
  key.fraction[1] = gshgeom_f64_bits(canon.t[1]);
  key.revision = GSHGEOM_STROKE_REVISION;

  GSHGEOM_Stroke_Outline * outline = NULL;
  GSHCPU_Stroke s;
  memset(&s, 0, sizeof s);
  s.alloc = alloc;
  GSHCPU_Run_Note * made = NULL; /* a miss: the notes of the centerline just made (this stroke's own) */
  if (!cache || !gshcpu_stroke_cache_acquire(cache, &key, &s.pin, &outline, &s.meta)) {
    /* SC7: a miss.  The expansion is made outside every lock, from the allocator given. */
    GSHGEOM_Path * built = NULL;
    memset(&s.meta, 0, sizeof s.meta);
    if (is_run) {
      /* ST1: the one function that defines a run's centerline; the whole run is one region, so a path past the limits
       * is the item's resource limit (SC6), while a glyph left out is a note. */
      GSHSCENE_Glyph_Note * notes = NULL;
      if (glyphs.glyph_count > 0) {
        notes = alloc->calloc_fn(alloc->ctx, glyphs.glyph_count, sizeof *notes);
        if (!notes) {
          return GSHCPU_ERR_OOM;
        }
      }
      GSHSCENE_Result cr = gshscene_glyph_run_centerline(item->resource, alloc, limits, &built, notes);
      GSHCPU_Result nr = GSHCPU_OK;
      if (cr == GSHSCENE_OK) {
        nr = collect_notes(alloc, notes, glyphs.glyph_count, &made, &s.meta.note_count);
      }
      if (notes) {
        alloc->free_fn(alloc->ctx, notes);
      }
      if (cr != GSHSCENE_OK) {
        return cr == GSHSCENE_ERR_OOM ? GSHCPU_ERR_OOM : cr == GSHSCENE_ERR_LIMIT ? GSHCPU_ERR_LIMIT : GSHCPU_ERR_INTERNAL;
      }
      if (nr != GSHCPU_OK) {
        gshgeom_path_destroy(built);
        return nr;
      }
      s.meta.run_elements = gshgeom_path_element_count(built);
      s.meta.notes = made;
      path = built;
    }
    uint64_t run_elements = s.meta.run_elements;
    size_t note_count = s.meta.note_count;
    g = gshgeom_stroke_expand(path, &params, &canon, alloc, limits, &outline);
    gshgeom_path_destroy(built);
    if (g != GSHGEOM_OK) {
      if (made) {
        alloc->free_fn(alloc->ctx, made);
      }
      return g == GSHGEOM_ERR_OOM ? GSHCPU_ERR_OOM : g == GSHGEOM_ERR_LIMIT ? GSHCPU_ERR_LIMIT : GSHCPU_ERR_INTERNAL;
    }
    meta_of(outline, &s.meta);
    s.meta.run_elements = run_elements;
    s.meta.note_count = note_count;
    s.meta.notes = made;
    s.meta.bytes += note_count * sizeof(GSHCPU_Run_Note);
    s.owned = outline;
    s.notes = made;
    s.note_count = note_count;
    if (cache) {
      void * entry = NULL;
      GSHGEOM_Stroke_Outline * held = NULL;
      int kept = gshcpu_stroke_cache_store(cache, &key, outline, &s.meta, &entry, &held);
      if (kept < 0) {
        gshcpu_render_stroke_close(cache, &s); /* destroys the outline it still owns, and the notes */
        return GSHCPU_ERR_OOM;
      }
      if (kept == 1) {
        s.pin = entry;
        s.owned = NULL;
      } else if (kept == 2) {
        gshgeom_stroke_outline_destroy(outline); /* another render stored the same expansion first: use that one */
        s.pin = entry;
        s.owned = NULL;
        outline = held;
      }
    }
  } else if (s.meta.note_count > 0) {
    /* A hit replays the notes of the miss (ST8): the stroke's own copy, so that they outlive the pin. */
    GSHCPU_Run_Note * copy = alloc->malloc_fn(alloc->ctx, s.meta.note_count * sizeof *copy);
    if (!copy) {
      gshcpu_render_stroke_close(cache, &s);
      return GSHCPU_ERR_OOM;
    }
    memcpy(copy, s.meta.notes, s.meta.note_count * sizeof *copy);
    s.notes = copy;
    s.note_count = s.meta.note_count;
  }
  s.run_elements = s.meta.run_elements;
  s.outline = outline;
  s.shift_x = sx;
  s.shift_y = sy;
  /* A hit refuses what a miss would (SC7): a part past max_points. */
  GSHCPU_Result r = GSHCPU_OK;
  for (int p = 0; p < GSHCPU_STROKE_PARTS && r == GSHCPU_OK; p++) {
    if (s.meta.present[p] && limits && s.meta.points[p] > (uint64_t)limits->max_points && limits->max_points != 0) {
      r = GSHCPU_ERR_LIMIT;
    }
  }
  GSHGEOM_Stroke_Status status = gshgeom_stroke_outline_status(outline);
  if (r == GSHCPU_OK && status == GSHGEOM_STROKE_DRAWN) {
    /* SC3: the shifted coordinates are the sink's resource limit past 2^24 pixels; the boxes are the pixels touched. */
    for (int p = 0; p < GSHCPU_STROKE_PARTS && r == GSHCPU_OK; p++) {
      s.box[p] = empty_rect();
      if (!s.meta.present[p] || s.meta.points[p] == 0) {
        continue;
      }
      int64_t x0 = s.meta.lo_x[p] + 64 * sx;
      int64_t y0 = s.meta.lo_y[p] + 64 * sy;
      int64_t x1 = s.meta.hi_x[p] + 64 * sx;
      int64_t y1 = s.meta.hi_y[p] + 64 * sy;
      if (x0 < -GSHCPU_RENDER_COORD_LIMIT || y0 < -GSHCPU_RENDER_COORD_LIMIT || x1 > GSHCPU_RENDER_COORD_LIMIT ||
          y1 > GSHCPU_RENDER_COORD_LIMIT) {
        r = GSHCPU_ERR_LIMIT;
        break;
      }
      s.box[p].x0 = pixel_floor(x0);
      s.box[p].y0 = pixel_floor(y0);
      s.box[p].x1 = -pixel_floor(-x1);
      s.box[p].y1 = -pixel_floor(-y1);
    }
  }
  if (r != GSHCPU_OK) {
    gshcpu_render_stroke_close(cache, &s);
    memset(out, 0, sizeof *out);
    return r;
  }
  if (status != GSHGEOM_STROKE_DRAWN) {
    /* A status other than DRAWN draws nothing and is not an error (SC4's defense); the notes of a run still stand. */
    s.drawn = 0;
    if (s.pin) {
      gshcpu_stroke_cache_release(cache, s.pin);
    }
    if (s.owned) {
      gshgeom_stroke_outline_destroy(s.owned);
    }
    s.pin = NULL;
    s.owned = NULL;
    s.outline = NULL;
    memset(&s.meta, 0, sizeof s.meta);
    *out = s;
    return GSHCPU_OK;
  }
  s.drawn = 1;
  s.hairline = params.width_space == (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE && params.width == 1.0;
  *out = s;
  return GSHCPU_OK;
}

void gshcpu_render_stroke_close(GSHCPU_Stroke_Cache * cache, GSHCPU_Stroke * s) {
  if (s->pin) {
    gshcpu_stroke_cache_release(cache, s->pin);
  }
  if (s->owned) {
    gshgeom_stroke_outline_destroy(s->owned);
  }
  if (s->notes) {
    s->alloc->free_fn(s->alloc->ctx, s->notes);
  }
  s->pin = NULL;
  s->owned = NULL;
  s->outline = NULL;
  s->notes = NULL;
  s->note_count = 0;
}

/* --- SC5: the composition ------------------------------------------------------------------------------- */

uint32_t gshcpu_render_stroke_compose(uint32_t b, uint32_t a, uint32_t c, int complement) {
  uint32_t cp = complement ? 255u - c : c;
  uint32_t t = gshcpu_render_weigh(a, cp); /* D8: (a * c' + 127) / 255 */
  return 255u - ((255u - b) * (255u - t) + 127u) / 255u;
}

/** @p r met with @p reach, an empty result made a zero-size rectangle at its corner (as a fill's is). */
static GSHCPU_Rect reach_of(GSHCPU_Rect box, GSHCPU_Rect reach) {
  GSHCPU_Rect r = gshcpu_render_rect_meet(box, reach);
  if (gshcpu_render_rect_empty(r)) {
    r.x1 = r.x0;
    r.y1 = r.y0;
  }
  return r;
}

static GSHCPU_Rect moved(GSHCPU_Rect r, int64_t dx, int64_t dy) {
  r.x0 -= dx;
  r.y0 -= dy;
  r.x1 -= dx;
  r.y1 -= dy;
  return r;
}

static int same_rect(GSHCPU_Rect a, GSHCPU_Rect b) {
  return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

/** One part's pixels: the drawn rectangle and the one the whole render draws. */
typedef struct Part {
  const GSHGEOM_Flat_Path * path;
  GSHCPU_Rect rect;
  GSHCPU_Rect walk;
} Part;

static Part part_rects(const GSHCPU_Stroke * s, int p, GSHCPU_Rect reach, GSHCPU_Rect whole) {
  Part part;
  part.path = part_of(s->outline, p);
  part.rect = reach_of(s->box[p], reach);
  part.walk = gshcpu_render_rect_meet(s->box[p], whole);
  if (!part.path || gshcpu_render_rect_empty(s->box[p])) {
    part.path = NULL;
    part.rect = empty_rect();
    part.walk = empty_rect();
  }
  return part;
}

/**
 * Whether a cell of part @p p could hold more pieces than @p ex allows: an edge has at most one piece in a cell, and an
 * outline has fewer edges than twice its points (one for each point and one closing line for each subpath), so one of
 * at most half that many points cannot.  A function of the outline alone, so that whether a stroke is refused does not
 * depend on the damage.
 */
static int may_exceed(const GSHCPU_Stroke * s, int p, const GSHCPU_Exact * ex) {
  return ex && s->meta.points[p] + s->meta.points[p] > ex->max_pieces;
}

/**
 * The cap decided over what the whole render draws (S4), as sink.c's `shape` decides it for a fill: when the edges
 * alone bound the deposits of every part by the cap, count only where it is drawn; else count the whole while drawing
 * the part.
 */
static int count_whole(const Part * parts, size_t n, uint64_t max_cells) {
  uint64_t bound = 0;
  for (size_t i = 0; i < n; i++) {
    if (!parts[i].path || gshcpu_render_rect_empty(parts[i].walk)) {
      continue;
    }
    bound += gshcpu_render_cells_bound(&parts[i].path, 1, parts[i].walk, max_cells);
    if (bound > max_cells) {
      return 1;
    }
  }
  return 0;
}

GSHCPU_Result gshcpu_render_stroke_cover(const GSHCPU_Stroke * s, GSHCPU_Rect reach, GSHCPU_Rect whole,
    uint64_t max_cells, int64_t * acc, size_t acc_stride, GSHCPU_Exact * ex, int joint_tier, const GSHCPU_Stroke_Buffers * buffers, uint8_t ** k,
    GSHCPU_Rect * rect, GSHCPU_Rect * whole_rect, uint64_t * cells) {
  Part body = part_rects(s, GSHCPU_STROKE_BODY, reach, whole);
  Part aligned = part_rects(s, GSHCPU_STROKE_ALIGNED, reach, whole);
  Part clip = part_rects(s, GSHCPU_STROKE_CLIP, reach, whole);
  clip.rect = aligned.rect; /* c over the aligned part's rectangle (SC5) */
  clip.walk = aligned.walk;
  if (!aligned.path) {
    clip.path = NULL;
  }
  Part parts[3] = {body, aligned, clip};
  int whole_count = count_whole(parts, 3, max_cells);
  *whole_rect = gshcpu_render_rect_join(body.walk, aligned.walk);
  /* The walk of each part: the whole render's, unless the caps cannot be met anywhere (the cells' by the edges alone, the
   * pieces of a cell by the outline's points) and then only where it is drawn. */
  GSHCPU_Rect walks[3];
  for (int p = 0; p < 3; p++) {
    walks[p] = parts[p].walk;
    GSHCPU_Exact * exact = p == GSHCPU_STROKE_CLIP ? NULL : ex;
    if (!same_rect(parts[p].rect, walks[p]) && !whole_count && !may_exceed(s, p, p == GSHCPU_STROKE_CLIP ? NULL : exact)) {
      walks[p] = parts[p].rect; /* the caps cannot be met anywhere: count where it is drawn */
    }
  }
  int complement = gshgeom_stroke_outline_clip_complement(s->outline);
  if (aligned.path && ex && joint_tier) {
    /* UC8: the three parts together, so that a pixel two of them have pieces in is the area of the region and not the
     * composition of three coverages that treats them as independent. */
    GSHCPU_Rect joint = gshcpu_render_rect_join(body.rect, aligned.rect);
    if (gshcpu_render_rect_empty(joint)) {
      joint.x1 = joint.x0;
      joint.y1 = joint.y0;
    }
    GSHCPU_Rect moved_joint = moved(joint, s->shift_x, s->shift_y);
    GSHCPU_Aligned_Input in[3];
    for (int p = 0; p < 3; p++) {
      in[p].path = parts[p].path && !gshcpu_render_rect_empty(parts[p].walk) ? parts[p].path : NULL;
      in[p].walk = gshcpu_render_rect_join(moved(walks[p], s->shift_x, s->shift_y), moved_joint);
      if (gshcpu_render_rect_empty(moved_joint)) {
        in[p].walk = moved(walks[p], s->shift_x, s->shift_y);
      }
      in[p].rule = p == GSHCPU_STROKE_CLIP ? gshgeom_stroke_outline_clip_rule(s->outline) : GSHGEOM_FILL_NONZERO;
      in[p].acc = acc + (size_t)p * acc_stride;
      in[p].out = p == 0 ? buffers->body : p == 1 ? buffers->aligned : buffers->clip;
      in[p].ex = ex + p;
    }
    GSHCPU_Result r = gshcpu_render_cover_aligned(in, moved_joint, complement, buffers->result, max_cells, cells);
    *rect = joint;
    *k = buffers->result;
    return r;
  }
  uint64_t total = 0;
  for (int p = 0; p < 3; p++) {
    Part * part = &parts[p];
    if (!part->path || gshcpu_render_rect_empty(part->walk)) {
      continue;
    }
    uint8_t * out = p == 0 ? buffers->body : p == 1 ? buffers->aligned : buffers->clip;
    GSHGEOM_Fill_Rule rule = p == 2 ? gshgeom_stroke_outline_clip_rule(s->outline) : GSHGEOM_FILL_NONZERO;
    uint64_t used = 0;
    GSHCPU_Result r = gshcpu_render_cover_exact(&part->path, 1, rule, moved(part->rect, s->shift_x, s->shift_y),
        moved(walks[p], s->shift_x, s->shift_y), out, acc, max_cells - total, &used, p == GSHCPU_STROKE_CLIP ? NULL : ex);
    total += used;
    if (r != GSHCPU_OK) {
      *cells = total;
      return r;
    }
  }
  *cells = total;
  if (!aligned.path) {
    *rect = body.rect;
    *k = buffers->body;
    return GSHCPU_OK;
  }
  /* SC5: the three coverages composed over the pixels either part reaches (the stopgap build, without the exact step). */
  GSHCPU_Rect joint = gshcpu_render_rect_join(body.rect, aligned.rect);
  if (gshcpu_render_rect_empty(joint)) {
    joint.x1 = joint.x0;
    joint.y1 = joint.y0;
  }
  size_t bw = (size_t)(body.rect.x1 - body.rect.x0);
  size_t aw = (size_t)(aligned.rect.x1 - aligned.rect.x0);
  for (int64_t y = joint.y0; y < joint.y1; y++) {
    for (int64_t x = joint.x0; x < joint.x1; x++) {
      uint32_t b = 0;
      uint32_t a = 0;
      uint32_t c = 0;
      if (x >= body.rect.x0 && x < body.rect.x1 && y >= body.rect.y0 && y < body.rect.y1) {
        b = buffers->body[(size_t)(y - body.rect.y0) * bw + (size_t)(x - body.rect.x0)];
      }
      if (x >= aligned.rect.x0 && x < aligned.rect.x1 && y >= aligned.rect.y0 && y < aligned.rect.y1) {
        size_t at = (size_t)(y - aligned.rect.y0) * aw + (size_t)(x - aligned.rect.x0);
        a = buffers->aligned[at];
        c = buffers->clip[at];
      }
      buffers->result[(size_t)(y - joint.y0) * (size_t)(joint.x1 - joint.x0) + (size_t)(x - joint.x0)] =
          (uint8_t)gshcpu_render_stroke_compose(b, a, c, complement);
    }
  }
  *rect = joint;
  *k = buffers->result;
  return GSHCPU_OK;
}

/* --- FT-2: the sparse composite --------------------------------------------------------------------------------- */

int gshcpu_render_stroke_sparse_ok(const GSHCPU_Stroke * s, uint64_t max_cells) {
  if (!s->drawn || s->meta.present[GSHCPU_STROKE_ALIGNED]) {
    return 0;
  }
  const GSHGEOM_Flat_Path * path = part_of(s->outline, GSHCPU_STROKE_BODY);
  GSHCPU_Rect box = s->box[GSHCPU_STROKE_BODY];
  if (!path || gshcpu_render_rect_empty(box)) {
    return 0;
  }
  /* The rectangle is the outline's own, the pixels its points touch, never the damage or the clips: whether a stroke
   * takes the tier is a function of the stroke alone.  The bound is over that whole rectangle, so it is never below
   * the deposits any render counts, and a stroke whose bound is within the cap cannot be refused for its cells. */
  uint64_t pixels = (uint64_t)(box.x1 - box.x0) * (uint64_t)(box.y1 - box.y0);
  uint64_t limit = pixels / 16u;
  if (limit > max_cells) {
    limit = max_cells;
  }
  return gshcpu_render_cells_bound(&path, 1, moved(box, s->shift_x, s->shift_y), limit) <= limit;
}

void gshcpu_render_stroke_rects(const GSHCPU_Stroke * s, GSHCPU_Rect reach, GSHCPU_Rect whole, GSHCPU_Rect * rect,
    GSHCPU_Rect * whole_rect) {
  Part body = part_rects(s, GSHCPU_STROKE_BODY, reach, whole);
  Part aligned = part_rects(s, GSHCPU_STROKE_ALIGNED, reach, whole);
  *whole_rect = gshcpu_render_rect_join(body.walk, aligned.walk);
  if (!aligned.path) {
    *rect = body.rect;
    return;
  }
  *rect = gshcpu_render_rect_join(body.rect, aligned.rect);
  if (gshcpu_render_rect_empty(*rect)) {
    rect->x1 = rect->x0;
    rect->y1 = rect->y0;
  }
}

GSHCPU_Result gshcpu_render_stroke_cover_sparse(const GSHCPU_Stroke * s, GSHCPU_Rect reach, GSHCPU_Rect whole,
    uint64_t max_cells, int64_t * acc, GSHCPU_Exact * ex, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx,
    GSHCPU_Rect * rect, GSHCPU_Rect * whole_rect, uint64_t * cells) {
  Part body = part_rects(s, GSHCPU_STROKE_BODY, reach, whole);
  *rect = body.rect;
  *whole_rect = body.walk;
  *cells = 0;
  if (!body.path || gshcpu_render_rect_empty(body.walk)) {
    return GSHCPU_OK;
  }
  GSHCPU_Rect walk = body.walk;
  if (!same_rect(body.rect, walk) && !count_whole(&body, 1, max_cells) && !may_exceed(s, GSHCPU_STROKE_BODY, ex)) {
    walk = body.rect;
  }
  return gshcpu_render_cover_sparse(&body.path, 1, GSHGEOM_FILL_NONZERO, moved(body.rect, s->shift_x, s->shift_y),
      moved(walk, s->shift_x, s->shift_y), acc, line, max_cells, cells, fn, ctx, ex);
}
