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
 * The sink's internals, shared by its source files and read by its tests (which
 * link the static archive, where a hidden symbol resolves): the compositing
 * primitives (compose.c), the clipped scan converter (cover.c), the geometry of
 * an item (geometry.c), the workspace arena (arena.c) and the render (sink.c).
 * Nothing here is part of the library's interface.
 */

#ifndef GHOTI_IO_GSHCPU_RENDER_INTERNAL_H
#define GHOTI_IO_GSHCPU_RENDER_INTERNAL_H

#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-cpu/core.h>
#include <ghoti.io/shape-cpu/render.h>

#include <ghoti.io/shape-geometry/flatten.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/sink.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- compositing (compose.c) ------------------------------------------------------ */

/** N2: an encoded component to linear light, in @p space (a concrete space). */
double gshcpu_render_decode(const GSHSCENE_Color_Space * space, double x);

/** N2: linear light to an encoded component. */
double gshcpu_render_encode(const GSHSCENE_Color_Space * space, double y);

/** A 3 by 3 matrix, row major (a struct, so that a const one can be passed in C17). */
typedef struct GSHCPU_Mat3 {
  double m[3][3];
} GSHCPU_Mat3;

/** N3: the matrix taking linear RGB of a gamut to XYZ (`GCOL_PRIMARIES_SRGB` to `_BT2020`). */
void gshcpu_render_rgb_to_xyz(int primaries, GSHCPU_Mat3 * out);

/** A conversion between two concrete spaces, prepared once (N3). */
typedef struct GSHCPU_Convert {
  int same;                 /* the spaces are equal: nothing is done */
  int matrix;               /* the gamuts differ: the two matrices apply */
  GSHSCENE_Color_Space from;
  GSHSCENE_Color_Space to;
  GSHCPU_Mat3 to_xyz;       /* the source gamut's */
  GSHCPU_Mat3 from_xyz;     /* the inverse of the destination gamut's */
} GSHCPU_Convert;

/** Prepare the conversion from @p from to @p to (both concrete). */
void gshcpu_render_convert_prepare(GSHCPU_Convert * c, const GSHSCENE_Color_Space * from, const GSHSCENE_Color_Space * to);

/** N3: a straight color.  @p in and @p out may be the same array. */
void gshcpu_render_convert_straight(const GSHCPU_Convert * c, const double in[3], double out[3]);

/** N3: a premultiplied value (un-premultiplied, converted, premultiplied again; alpha 0 gives 0). */
void gshcpu_render_convert_premultiplied(const GSHCPU_Convert * c, const double in[4], double out[4]);

/** N5: `d = (1 - m) d + m op(s, d)`, componentwise; @p op a ::GSHSCENE_Composite. */
void gshcpu_render_apply(double d[4], const double s[4], double m, uint32_t op);

/** N4: nonzero for an operator with `op(0, D) = D` for every `D`. */
int gshcpu_render_bounded(uint32_t op);

/** D8: the weight of a coverage under one more clip, `(m8 * c8 + 127) / 255`. */
uint32_t gshcpu_render_weigh(uint32_t m8, uint32_t c8);

/** The write to 8 bits: `floor(v * 255 + 1/2)` clamped to `[0, 255]`. */
uint8_t gshcpu_render_store8(double v);

/** The write to `float`: rounded once. */
float gshcpu_render_store_float(double v);

/**
 * Read pixel @p x of an 8-bit or float row of @p format as a premultiplied value.
 */
void gshcpu_render_load(GSHCPU_Format format, const unsigned char * row, size_t x, double out[4]);

/** Write the premultiplied value @p v as pixel @p x of a row of @p format. */
void gshcpu_render_store(GSHCPU_Format format, unsigned char * row, size_t x, const double v[4]);

/** Bytes of a pixel of @p format (4 or 16). */
size_t gshcpu_render_pixel_bytes(GSHCPU_Format format);

/* --- the clipped scan converter (cover.c) ------------------------------------------ */

/** A rectangle of whole pixels, half open (the same as a `GSHSCENE_Rect`). */
typedef GSHSCENE_Rect GSHCPU_Rect;

/** Nonzero if @p r is empty. */
int gshcpu_render_rect_empty(GSHCPU_Rect r);

/** The intersection. */
GSHCPU_Rect gshcpu_render_rect_meet(GSHCPU_Rect a, GSHCPU_Rect b);

/** The union (either may be empty). */
GSHCPU_Rect gshcpu_render_rect_join(GSHCPU_Rect a, GSHCPU_Rect b);

/** The most a 26.6 coordinate of an item may be in magnitude: 2^24 pixels. */
#define GSHCPU_RENDER_COORD_LIMIT ((int64_t)1 << 30)

/**
 * The coverage of the union of @p count flattened paths (their windings add) inside @p rect, by @p rule: what
 * gshcpu_coverage_fill gives for those pixels, wherever the paths lie.
 *
 * @param out `width * height` bytes of @p rect, row by row; written in full.
 * @param acc The cell accumulator: `2 * (width + 1) * GSHCPU_RENDER_BAND_ROWS` `int64_t`.
 * @param max_cells The most cell deposits (a crossing of one cell with a nonzero height) allowed.
 * @param cells The deposits made, counted until the limit.
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_LIMIT when a deposit would pass @p max_cells (@p out is then not complete).
 */
GSHCPU_Result gshcpu_render_cover(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells);

/* --- the workspace arena (arena.c) ------------------------------------------------- */

/**
 * A bump allocator over caller memory, or a measure of one over another allocator (`measure` set): every block
 * takes a 16-byte header and its size rounded up to 16; a free is kept; a realloc of the last block grows it in
 * place.  The measure gives the bytes the same calls take from an arena of unbounded size, which is the size that
 * makes an arena reproduce them exactly.
 */
typedef struct GSHCPU_Arena {
  unsigned char * base;     /* aligned to 16; NULL in a measure */
  size_t capacity;
  size_t used;
  size_t peak;
  void * last;              /* the last block handed out (its payload) */
  size_t last_offset;       /* where its header starts */
  const GSHGEOM_Allocator * measure; /* a measure: real memory comes from here */
  GSHGEOM_Allocator self;   /* this arena as an allocator */
} GSHCPU_Arena;

/** An arena over @p bytes at @p memory (aligned to 16 by the caller). */
void gshcpu_render_arena_init(GSHCPU_Arena * arena, void * memory, size_t bytes);

/** A measure over @p real. */
void gshcpu_render_arena_measure(GSHCPU_Arena * arena, const GSHGEOM_Allocator * real);

/** Start again from empty (the peak is kept). */
void gshcpu_render_arena_reset(GSHCPU_Arena * arena);

/* --- the geometry of one item (geometry.c) ------------------------------------------- */

/** What became of one glyph of a run from a font (glyph.h): its entry, if it has one. */
typedef struct GSHCPU_Glyph_Note {
  uint32_t entry_class; /* 0, HINT_STATUS or REFUSED */
  uint32_t reason;
} GSHCPU_Glyph_Note;

/** An item's flattened paths in device space. */
typedef struct GSHCPU_Item_Geometry {
  GSHGEOM_Flat_Path ** paths; /* one per path or glyph; NULL entries are none */
  size_t count;
  size_t points;              /* their points */
  GSHCPU_Rect box;            /* the pixels their points touch (empty for none) */
  GSHCPU_Glyph_Note * notes;  /* a run from a font: one per glyph (count of them); else NULL */
} GSHCPU_Item_Geometry;

/**
 * Convert and flatten the resource of a fill or clip push: @p chain holds the maps of the enclosing instances, room
 * for two more, and @p count of them are set (the item's map is added here, and a glyph's translation).  A run
 * from a font goes by its route (glyph.h): the fast tier's device path from the face's provider, or the unhinted
 * outline under the glyph's map; what became of each glyph is in `notes`, and a refused glyph has no path.
 *
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_OOM; ::GSHCPU_ERR_LIMIT for anything else that stops it (the sink's resource
 *   limits: a map, a coordinate, the points); on failure @p out holds what must still be released.
 */
GSHCPU_Result gshcpu_render_geometry(const GSHSCENE_Item * item, GSHGEOM_Map * chain, size_t count,
    const GSHGEOM_Map * target, const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits,
    GSHCPU_Item_Geometry * out);

/** The route of a fill or clip push of a run from a font under the maps of @p chain (as above); 0 for any other item. */
int gshcpu_render_glyph_route(const GSHSCENE_Item * item, GSHGEOM_Map * chain, size_t count, const GSHGEOM_Map * target,
    GSHSCENE_Glyph_Route * out);

/** Release what ::gshcpu_render_geometry made. */
void gshcpu_render_geometry_release(GSHCPU_Item_Geometry * g, const GSHGEOM_Allocator * alloc);

/* --- the sink (sink.c) ---------------------------------------------------------------- */

/**
 * A test's view of a render, called just before the write with the root layer (premultiplied doubles, four a
 * pixel, row by row over @p region) and the touched map: how the gate reads the values before their one rounding.
 */
typedef void (*GSHCPU_Render_Observer)(void * ctx, GSHCPU_Rect region, const double * root, const uint8_t * touched);

/** ::gshcpu_sink_create with an observer of every render (for tests). */
GSHCPU_Result gshcpu_render_sink_create_observed(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, GSHCPU_Render_Observer observer, void * observer_ctx, GSHSCENE_Sink ** out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_RENDER_INTERNAL_H */
