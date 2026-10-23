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
#include <ghoti.io/shape-geometry/stroke.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/sink.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- compositing (compose.c) ------------------------------------------------------ */

/** N1: `sat(x)`, @p x clamped to `[-2^64, 2^64]` (a value inside is returned bit for bit). */
double gshcpu_render_saturate(double x);

/** N1: `sat(c / a)` for a nonzero @p a, never forming an infinity (past the bound nothing is divided). */
double gshcpu_render_unpremultiply(double c, double a);

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

/**
 * N5: `d = (1 - m) d + m op(s, d)`, componentwise; @p op a ::GSHSCENE_Composite.
 *
 * @return 1; or 0, with @p d untouched, for an operator it does not composite.  The caps claim exactly the operators
 *   it composites, so the planning pass refuses every other first and a render never meets one.
 */
int gshcpu_render_apply(double d[4], const double s[4], double m, uint32_t op);

/** N4: nonzero for an operator with `op(0, D) = D` for every `D` (every blend mode, B3). */
int gshcpu_render_bounded(uint32_t op);

/** B1: nonzero for a blend mode (13 to 27). */
int gshcpu_render_blends(uint32_t op);

/** B7 to B9: the mixing function of blend mode @p op on straight colors, each component saturated. */
void gshcpu_render_mix(uint32_t op, const double cb[3], const double cs[3], double out[3]);

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

/**
 * ::gshcpu_render_cover over @p rect, with the cell deposits counted (and @p max_cells met) over @p walk, which holds
 * @p rect (S4: the rectangle the whole render would draw of the item, so that the item's refusal does not depend on
 * the damage).  @p out is what ::gshcpu_render_cover over @p rect writes; @p cells is what it counts over @p walk.
 * @p rect may be empty (nothing is drawn, and @p walk is counted).
 */
GSHCPU_Result gshcpu_render_cover_counted(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells);

/** The memory and settings of the exact step (below). */
typedef struct GSHCPU_Exact GSHCPU_Exact;

/**
 * ::gshcpu_render_cover_counted for a stroke part under the nonzero rule, with the exact step (UC1 to UC7): where a cell
 * holds two or more pieces its coverage is the area of the union of what lies in it, not the saturating sum.  @p ex holds
 * the step's memory (its count arrays are as wide as @p walk); `NULL` is ::gshcpu_render_cover_counted.  A cell with more
 * pieces than `ex->max_pieces`, a winding past the bound, or a cell whose pieces do not reproduce the rasterizer's sums is
 * ::GSHCPU_ERR_LIMIT.
 */
GSHCPU_Result gshcpu_render_cover_exact(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, uint8_t * out, int64_t * acc, uint64_t max_cells, uint64_t * cells,
    GSHCPU_Exact * ex);

/**
 * A bound, from the edges alone, of the cell deposits of the paths over @p walk (what ::gshcpu_render_cover over
 * @p walk counts): never less than that count, and found without a pass over the cells.  It stops adding once it
 * passes @p limit (a result past @p limit says only that).  @p walk lies within ::GSHCPU_RENDER_COORD_LIMIT.
 */
uint64_t gshcpu_render_cells_bound(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHCPU_Rect walk,
    uint64_t limit);

/* --- the exact step (exact.c, cover.c; strokes story S4b, UC1 to UC11) ------------------------------------------ */

/**
 * One piece of an edge inside one cell of the accumulator, both ends in the cell's frame (26.6, `0..64`, y down), directed
 * along the contour.  A piece of nonzero height is a fragment; one of height 0 that touches the cell's left side is a flat
 * piece (UC2).
 */
typedef struct GSHCPU_Piece {
  uint8_t x0, y0, x1, y1;
} GSHCPU_Piece;

/**
 * The sign `eps` of the expansion's orientation (UC4): `w = eps W` where `W` counts an edge directed down +1 and `w` is 1
 * inside a positively wound piece.  Positive winding goes from +x toward +y around the inside (shape-geometry's orient.h),
 * so the edge on the left of a piece's inside is directed up and `W` is -1 there.
 */
#define GSHCPU_RENDER_EXACT_EPS (-1)

/** The most pieces a cell may be allowed to hold: `GSHCPU_Limits.max_exact_fragments` is accepted up to this. */
#define GSHCPU_EXACT_FRAGMENTS_CEILING ((uint64_t)1 << 20)

/** The smallest pool of pieces a render keeps for the cells of one window (a cell holds at most `max_exact_fragments`). */
#define GSHCPU_EXACT_PIECES_MIN 4096u

/** What ::gshcpu_exact_cell reports. */
enum {
  GSHCPU_EXACT_OK = 0,
  GSHCPU_EXACT_LIMIT,    /* a winding past the internal bound (UC9): the sink's resource limit */
  GSHCPU_EXACT_IDENTITY  /* the cell's pieces do not reproduce what the rasterizer summed: a defect, refused rather than guessed */
};

/**
 * What the weight of a gap is: the overlap `(|w| - 1)^+` of one nonzero part (UC6), UC8's area of the three parts' region, or
 * (in an aligned stroke) the area of the clip's own region by its rule, where the clip alone has pieces in a cell.
 */
enum { GSHCPU_EXACT_OVERLAP = 0, GSHCPU_EXACT_UNION3 = 1, GSHCPU_EXACT_REGION = 2 };

/** A fragment oriented top to bottom: its upper end, `dx` to its lower end, its height and sign. */
typedef struct GSHCPU_Exact_Frag {
  int16_t px, py;
  int16_t dx, dy;
  int8_t s;       /* +1 when the contour goes down along it */
  uint8_t part;
  uint32_t idx;   /* its place in the sorted list: the last tie-break */
} GSHCPU_Exact_Frag;

/** A fragment as one slab sees it: `x(u) = (a + dx u) / dy` at depth `u` below the slab's top. */
typedef struct GSHCPU_Exact_Active {
  int32_t a;
  int16_t dx, dy;
  int8_t s;
  uint8_t part;
  uint32_t idx;
} GSHCPU_Exact_Active;

/** One cell's working memory, carved from a block of ::gshcpu_exact_scratch_bytes. */
typedef struct GSHCPU_Exact_Scratch {
  size_t capacity;            /* pieces of the cell, all parts */
  GSHCPU_Exact_Frag * frag;
  GSHCPU_Exact_Active * act;
  uint32_t * ord;
  uint32_t * xa;
  uint32_t * xb;
  GSHCPU_Piece * sorted;
  int64_t last_wmax;          /* the largest winding the last cell met in magnitude: read by the measuring tool, nothing else */
} GSHCPU_Exact_Scratch;

/** Bytes of scratch for cells of up to @p max_pieces pieces in each of @p parts parts (1 or 3). */
size_t gshcpu_exact_scratch_bytes(uint64_t max_pieces, int parts);

/** Carve @p s from @p memory (@p gshcpu_exact_scratch_bytes bytes). */
void gshcpu_exact_scratch_init(GSHCPU_Exact_Scratch * s, void * memory, uint64_t max_pieces, int parts);

/** What the exact step is to compute for a cell. */
typedef struct GSHCPU_Exact_Params {
  int parts;                  /* 1, or 3 (body, aligned, clip) */
  int mode;                   /* GSHCPU_EXACT_OVERLAP or GSHCPU_EXACT_UNION3 */
  int eps[3];                 /* each part's orientation sign: `w = eps W` */
  int even_odd;               /* UNION3: the clip part's rule */
  int complement;             /* UNION3: the region is the clip's complement */
} GSHCPU_Exact_Params;

/** One cell as the exact step sees it, per part: its pieces, the cover to its left and the dense area summed there. */
typedef struct GSHCPU_Exact_Cell {
  const GSHCPU_Piece * pieces[3];
  size_t count[3];
  int64_t a_left[3];          /* the sum of the covers of the cells strictly left of this one, the fold column included */
  int64_t dense[3];           /* `eps (accumulated - area)`: the dense doubled area of the part's winding, signed */
} GSHCPU_Exact_Cell;

/**
 * UC4 to UC7: the exact area of one cell.  @p out receives `2 O` in doubled units (OVERLAP: what the pieces share of the
 * dense area) or the doubled area of the region (UNION3).  @p work is incremented by the steps taken (SC11).
 *
 * @return ::GSHCPU_EXACT_OK, or one of the codes above.
 */
int gshcpu_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work);

/** The shape of ::gshcpu_exact_cell, for the hook below. */
typedef int (*GSHCPU_Exact_Cell_Fn)(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s,
    const GSHCPU_Exact_Cell * cell, int64_t * out, uint64_t * work);

/**
 * A test hook: when set, ::gshcpu_exact_cell calls it in place of the step.  Nothing in the library sets it; a test program
 * sets it to a second engine (the earlier step, kept in the tests) to render the same list with both and compare the pixels
 * (the dual render).  Read without a lock: set it before the renders it is to apply to, from the thread that makes them.
 */
extern GSHCPU_Exact_Cell_Fn gshcpu_exact_hook;

/**
 * The memory and the settings of the exact step inside a cover (cover.c): per accumulator cell the number of pieces,
 * a pool for the pieces of the cells of one window, and the scratch of ::gshcpu_exact_cell.
 */
struct GSHCPU_Exact {
  uint64_t max_pieces;        /* a cell with more pieces than this is refused (UC9) */
  int eps;                    /* the orientation sign of the part covered */
  uint32_t * count;           /* GSHCPU_RENDER_BAND_ROWS rows of the walk's width */
  uint32_t * slot;            /* the same: where a cell's pieces are in the pool */
  GSHCPU_Piece * pool;
  size_t pool_cap;            /* pieces */
  GSHCPU_Exact_Scratch scratch;
  uint16_t * union_k;         /* aligned strokes, the first of the three only: a cell's UC8 coverage, or 0xffff for none */
  uint16_t * clip_k;          /* ... and the clip's own coverage in a cell only it has pieces in, or 0xffff for none */
  int64_t * prefix;           /* ... and a row's running sums of cover, for each of the three parts */
  /* what happened, added to as covers run */
  uint64_t work;              /* SC11's units */
  uint64_t cells;             /* cells made exact */
  uint64_t failures;          /* IDENTITY results (a defect: the cover then refuses) */
};

/**
 * The most work (SC11's units) the exact step may do in one item whose cells are capped at @p max_cells, whose outlines
 * have at most @p max_points points and whose raster is at most @p max_dimension wide, with cells of at most
 * @p max_pieces pieces: held at `UINT64_MAX`.
 */
uint64_t gshcpu_exact_work_bound(uint64_t max_cells, uint64_t max_points, uint64_t max_dimension, uint64_t max_pieces);

/**
 * The bytes of the arrays of an ::GSHCPU_Exact for walks up to @p width cells wide and @p max_pieces.  @p parts is the
 * parts whose pieces its scratch holds at once: 1 for a part's own overlap, 3 for the cells of an aligned stroke where
 * the parts share a pixel (UC8, which also takes the arrays of its composition), 0 for none (the second and third part's
 * memory in an aligned stroke only keeps counts and pieces).
 */
size_t gshcpu_exact_bytes(size_t width, uint64_t max_pieces, int parts);

/** Set @p ex up over @p memory (::gshcpu_exact_bytes bytes, aligned to 16). */
void gshcpu_exact_init(GSHCPU_Exact * ex, void * memory, size_t width, uint64_t max_pieces, int parts);

/** The place a cell has in the pool when none of its pieces are kept. */
#define GSHCPU_EXACT_NO_SLOT UINT32_MAX

/** One part of an aligned stroke for ::gshcpu_render_cover_aligned (body, aligned, clip: the order of GSHCPU_STROKE_*). */
typedef struct GSHCPU_Aligned_Input {
  const GSHGEOM_Flat_Path * path;  /* NULL when the part is absent */
  GSHCPU_Rect walk;                /* the cells counted: holds the rectangle drawn */
  GSHGEOM_Fill_Rule rule;
  int64_t * acc;                   /* the part's accumulator, as ::gshcpu_render_cover_counted's */
  uint8_t * out;                   /* the part's coverage over the rectangle drawn, row by row */
  GSHCPU_Exact * ex;               /* the part's exact memory; the first part's holds the step's scratch for three parts */
} GSHCPU_Aligned_Input;

/**
 * UC8: the coverage of an aligned stroke over @p joint, the three parts covered together a band at a time, written to
 * @p result: in a cell where two of them have pieces the area of `{w_b != 0} U ({w_a != 0} & F')` with `F'` the clip's
 * region (or its complement when @p complement), exactly; in every other the three coverages composed as SC5 does
 * (::gshcpu_render_stroke_compose), each part's own overlap made exact.  The parts' own coverages are left in
 * their `out`.  The cells are counted over each part's walk and met against @p max_cells together.
 *
 * @return ::GSHCPU_OK or ::GSHCPU_ERR_LIMIT (the cell cap, the pieces of a cell, a winding, the identity).
 */
GSHCPU_Result gshcpu_render_cover_aligned(const GSHCPU_Aligned_Input in[3], GSHCPU_Rect joint, int complement,
    uint8_t * result, uint64_t max_cells, uint64_t * cells);

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

/* --- gradients (gradient.c; color-paint story P2) ------------------------------------------ */

/** The parameter a sink takes at the ends of the double's reach (P2-4): `2^431`, past every `t` G14 allows. */
#define GSHCPU_RENDER_T_LIMIT 0x1p431

/**
 * A stop of a resolved color line: what gshscene_gradient_resolve writes (its offset and straight color in the
 * interpolation space), made in place its interpolant (G4: premultiplied when the gradient is) in `rgba`.
 */
typedef GSHSCENE_Resolved_Stop GSHCPU_Stop;

/** A gradient prepared for one fill: its inverse map, its kind's constants and its color line. */
typedef struct GSHCPU_Gradient {
  uint32_t kind;
  uint32_t extend;
  uint32_t premultiplied;
  uint32_t unit;             /* sweep: the angle unit */
  int degenerate;            /* G10 by this sink's arithmetic (the plan's notice skips these first) */
  double inv[6];             /* p = L^-1 (q - t): inv[0..3] row major, inv[4..5] the composed map's translation */
  double p[2];               /* p0, c0 or the center */
  double n[2];               /* linear: n_d; radial: c1 - c0 */
  double den;                /* linear: (p1 - p0) . n_d, from its exact value */
  double r0, dr, a;          /* radial: r0, r1 - r0, |c1 - c0|^2 - (r1 - r0)^2 (zero exactly when it is) */
  double a0, a1, turn;       /* sweep: the angles and one turn in their unit */
  const GSHCPU_Stop * stops;
  size_t count;
  double o_first, o_last, period;
  double alpha;              /* the paint's alpha multiplier (G12) */
} GSHCPU_Gradient;

/** Make @p count resolved stops, in place, their interpolants (G4). */
void gshcpu_render_gradient_stops(GSHCPU_Stop * stops, size_t count, uint32_t premultiplied);

/**
 * Prepare @p g (its stops' interpolants @p stops, sorted) for a fill whose chain composes to @p composed (the target's
 * map last, the gradient's map first).
 *
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_LIMIT where the composed map's inverse is not finite or its determinant is zero in
 *   doubles (the sink's own limit, S6).
 */
GSHCPU_Result gshcpu_render_gradient_prepare(GSHCPU_Gradient * out, const GSHSCENE_Gradient * g,
    const GSHCPU_Stop * stops, const GSHGEOM_Map * composed, double alpha);

/** Nonzero if the centers of @p r's pixels map into G14's sampling domain (half of it, for the rounding between). */
int gshcpu_render_gradient_in_domain(const GSHCPU_Gradient * g, GSHCPU_Rect r);

/** The gradient-space point of the device point (@p qx, @p qy). */
void gshcpu_render_gradient_unmap(const GSHCPU_Gradient * g, double qx, double qy, double * px, double * py);

/** G7 to G9: the parameter at gradient-space point (@p px, @p py); 0 where nothing is drawn (G8's outside, G10). */
int gshcpu_render_gradient_param(const GSHCPU_Gradient * g, double px, double py, double * t);

/** G4, G5, G12: the straight color at @p t in the interpolation space, its alpha times the paint's. */
void gshcpu_render_gradient_color(const GSHCPU_Gradient * g, double t, double out[4]);

/** Both: the straight color at device point (@p qx, @p qy); 0 where nothing is drawn. */
int gshcpu_render_gradient_at(const GSHCPU_Gradient * g, double qx, double qy, double out[4]);

/* --- strokes (stroke.c, stroke_cache.c; strokes story S4) -------------------------------------- */

/** Tiers a test can switch off to force the general tier (render.h, SC8).  Every tier is on by default. */
#define GSHCPU_RENDER_TIER_CACHE 1u   /* FT-1: the cached expansion */
#define GSHCPU_RENDER_TIER_SPARSE 2u  /* FT-2: the sparse composite of a solid single-part stroke */
/**
 * UC1 to UC11: the exact union coverage of a stroke.  Not an optimization (a render without it is the sum of partial
 * coverages that credits an overlap twice, the stopgap F4 bounded): the control of the gates, which show the bracket and
 * the union gate fail on the build that has it off.
 */
#define GSHCPU_RENDER_TIER_EXACT 4u
/**
 * UC8: the three parts of an aligned stroke covered together, a pixel two of them have pieces in the area of the region.
 * Off (with the exact step on), each part is exact for its own overlap and SC5's formula composes them, which treats the
 * three as independent: the control of the gate that holds a thin aligned stroke to the union.
 */
#define GSHCPU_RENDER_TIER_JOINT 8u
#define GSHCPU_RENDER_TIERS_ALL \
  (GSHCPU_RENDER_TIER_CACHE | GSHCPU_RENDER_TIER_SPARSE | GSHCPU_RENDER_TIER_EXACT | GSHCPU_RENDER_TIER_JOINT)
/** What the general tier is: the coverage (the exact step and UC8), none of the fast tiers. */
#define GSHCPU_RENDER_TIERS_GENERAL (GSHCPU_RENDER_TIER_EXACT | GSHCPU_RENDER_TIER_JOINT)

/**
 * What a sink counted (render.h, SC7, SC8): how often each tier was taken.  A test reads it after the renders it
 * made, to show a tier taken; the counts are kept under the cache's lock.
 */
typedef struct GSHCPU_Render_Counters {
  uint64_t strokes;          /* strokes drawn (their expansion drew something) */
  uint64_t cache_hits;       /* FT-1: an expansion found in the cache */
  uint64_t cache_misses;     /* an expansion made because the cache had none */
  uint64_t cache_kept;       /* ... and kept */
  uint64_t cache_evictions;  /* entries dropped to make room, by an eviction or by the sink's destruction */
  uint64_t sparse_hairline;  /* FT-2 for a solid hairline (device width 1) */
  uint64_t sparse_plain;     /* FT-2 for any other solid stroke */
  uint64_t exact_cells;      /* cells the exact step made exact (UC3), whichever part or parts */
  uint64_t exact_failures;   /* cells whose pieces did not reproduce the rasterizer's sums (UC4): a defect, refused */
} GSHCPU_Render_Counters;

/** What identifies an expansion in the cache (SC7). */
typedef struct GSHCPU_Stroke_Key {
  uint64_t path_id;          /* the centerline's resource id */
  uint64_t style_id;         /* the style's */
  uint64_t linear[4];        /* the bit patterns of the canonical map's linear part, row major */
  uint64_t fraction[2];      /* ... and of its translation, in [0, 1) */
  int32_t revision;          /* GSHGEOM_STROKE_REVISION */
} GSHCPU_Stroke_Key;

/** The parts of an outline, in the order of its accessors. */
enum { GSHCPU_STROKE_BODY = 0, GSHCPU_STROKE_ALIGNED = 1, GSHCPU_STROKE_CLIP = 2, GSHCPU_STROKE_PARTS = 3 };

/** What became of one glyph of a stroked run that has an entry (ST1, ST8): its index and the note ::gshscene_glyph_run_centerline gave. */
typedef struct GSHCPU_Run_Note {
  uint64_t index;
  uint32_t entry_class;
  uint32_t reason;
} GSHCPU_Run_Note;

/**
 * What is known of an outline without a pass over it: each part's points and the extent of its 26.6 coordinates, and for
 * a stroke of a run (ST8) what the construction of its centerline said (the glyphs' notes, and the elements of the
 * centerline: the glyph outlines' elements that went into it), which a hit replays so that it equals a miss in the report.
 */
typedef struct GSHCPU_Stroke_Meta {
  int present[GSHCPU_STROKE_PARTS];
  uint64_t points[GSHCPU_STROKE_PARTS];
  int64_t lo_x[GSHCPU_STROKE_PARTS], lo_y[GSHCPU_STROKE_PARTS], hi_x[GSHCPU_STROKE_PARTS], hi_y[GSHCPU_STROKE_PARTS];
  size_t bytes;              /* what the cache accounts for it (stroke_cache.c) */
  uint64_t run_elements;     /* a run: the elements of its centerline (0 for a path) */
  size_t note_count;         /* a run: the glyphs that have an entry */
  const GSHCPU_Run_Note * notes; /* ... and their notes; the cache entry's own, valid while pinned (else the stroke's) */
} GSHCPU_Stroke_Meta;

/** A receiver of the pixels one row of a sparse sweep covers: `n` coverages from pixel @p x (in the paths' frame). */
typedef void (*GSHCPU_Span_Fn)(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8);

/** The cache of stroke expansions (SC7). */
typedef struct GSHCPU_Stroke_Cache GSHCPU_Stroke_Cache;

/**
 * A cache bounded to @p bound bytes (0 is the default, 1 keeps nothing, `UINT64_MAX` no cap), its counters in
 * @p counters.  Entries are made from @p alloc.  @return ::GSHCPU_OK or ::GSHCPU_ERR_OOM.
 */
GSHCPU_Result gshcpu_stroke_cache_create(const GSHGEOM_Allocator * alloc, uint64_t bound,
    GSHCPU_Render_Counters * counters, GSHCPU_Stroke_Cache ** out);

/** Free every entry (no render is in flight) and the cache. */
void gshcpu_stroke_cache_destroy(GSHCPU_Stroke_Cache * cache);

/** The bytes held by the entries in the table (static_bytes, S9). */
uint64_t gshcpu_stroke_cache_bytes(GSHCPU_Stroke_Cache * cache);

/** Drop every entry that names resource @p id as its centerline or its style; one in use goes at its last release. */
void gshcpu_stroke_cache_evict(GSHCPU_Stroke_Cache * cache, uint64_t id);

/**
 * Find the expansion for @p key and pin it.  @return 1 and the entry, its outline and its meta; 0 when there is none.
 * A hit counts.
 */
int gshcpu_stroke_cache_acquire(GSHCPU_Stroke_Cache * cache, const GSHCPU_Stroke_Key * key, void ** entry,
    GSHGEOM_Stroke_Outline ** outline, GSHCPU_Stroke_Meta * meta);

/**
 * Offer a fresh expansion for @p key.  @return 1 when the cache took it (it owns @p outline now, which is pinned until
 * ::gshcpu_stroke_cache_release); 2 when an entry for the key was there already (that one is pinned, @p entry and
 * @p outline are it, and the caller still owns, and must free, its own); 0 when it was not kept (larger than the
 * bound, or nothing could be dropped to make room): the caller keeps it; -1 when the entry could not be allocated
 * (out of memory: the caller still owns the outline).  A miss counts.
 */
int gshcpu_stroke_cache_store(GSHCPU_Stroke_Cache * cache, const GSHCPU_Stroke_Key * key, GSHGEOM_Stroke_Outline * outline,
    const GSHCPU_Stroke_Meta * meta, void ** entry, GSHGEOM_Stroke_Outline ** held);

/** Unpin an entry; the last release of one that was dropped frees it. */
void gshcpu_stroke_cache_release(GSHCPU_Stroke_Cache * cache, void * entry);

/** Count a tier taken (stroke.c): one of the counters of ::GSHCPU_Render_Counters, by offset. */
void gshcpu_stroke_cache_count(GSHCPU_Stroke_Cache * cache, size_t offset);

/** Add @p amount to one of the counters, by offset. */
void gshcpu_stroke_cache_add(GSHCPU_Stroke_Cache * cache, size_t offset, uint64_t amount);

/** A stroke ready to cover: its outline (pinned in the cache, or owned), the whole-pixel shift and its meta (SC3). */
typedef struct GSHCPU_Stroke {
  GSHGEOM_Stroke_Outline * outline;
  GSHGEOM_Stroke_Outline * owned;  /* the outline to destroy at the end; NULL when the cache holds it */
  void * pin;                      /* the cache entry to release; NULL when none */
  int drawn;                       /* the outline's status is DRAWN: parts to cover */
  int64_t shift_x, shift_y;        /* whole pixels (SC3) */
  GSHCPU_Stroke_Meta meta;
  GSHCPU_Rect box[GSHCPU_STROKE_PARTS]; /* each part's pixels, shifted (empty when absent) */
  int hairline;                    /* a solid device width of 1 (FT-2's detection) */
  int sparse_ok;                   /* the style and paint admit FT-2 (the outline has one part is decided from the parts) */
  const GSHGEOM_Allocator * alloc; /* the allocator of `notes` */
  GSHCPU_Run_Note * notes;         /* a run: the glyph notes of its centerline (ST8), the stroke's own copy; NULL for none */
  size_t note_count;
  uint64_t run_elements;           /* a run: the elements of its centerline */
} GSHCPU_Stroke;

/**
 * SC2 to SC7: expand @p item's centerline under @p composed (the chain composed, in pixels) at the canonical
 * translation, from the cache (@p cache not NULL) or made from @p alloc.  @p limits' `max_points` bounds each part.
 *
 * A centerline that is a run is ST1's path, made by ::gshscene_glyph_run_centerline on a miss only and cached with its
 * glyphs' notes under the run's resource id.  The notes are the stroke's own on return (`notes`, even when nothing is
 * drawn) and ::gshcpu_render_stroke_close frees them, so a caller always closes a stroke that opened.
 *
 * @return ::GSHCPU_OK (a status other than DRAWN leaves `drawn` 0: nothing to draw); ::GSHCPU_ERR_LIMIT (a part past
 *   `max_points`, the depth cap, a shifted coordinate past ::GSHCPU_RENDER_COORD_LIMIT: the sink's resource limit);
 *   ::GSHCPU_ERR_OOM; ::GSHCPU_ERR_INTERNAL.  Nothing is held on failure.
 */
GSHCPU_Result gshcpu_render_stroke_open(GSHCPU_Stroke_Cache * cache, const GSHGEOM_Allocator * alloc,
    const GSHGEOM_Limits * limits, const GSHSCENE_Item * item, const GSHGEOM_Map * composed, GSHCPU_Stroke * out);

/** Release what ::gshcpu_render_stroke_open holds. */
void gshcpu_render_stroke_close(GSHCPU_Stroke_Cache * cache, GSHCPU_Stroke * s);

/** The arrays a stroke's coverage is built in: the region's pixels each; the last three only for an aligned stroke. */
typedef struct GSHCPU_Stroke_Buffers {
  uint8_t * body;
  uint8_t * aligned;
  uint8_t * clip;
  uint8_t * result;
} GSHCPU_Stroke_Buffers;

/**
 * SC5, SC6: the coverage of @p s composed to one 8-bit `k` over the pixels it can reach.
 *
 * @param reach The pixels the clips in force allow, in the drawn region; @p whole the same in the whole render (S4).
 * @param acc The cell accumulators: one of @p acc_stride elements, or three in a row when the stroke aligns (UC8).
 * @param ex The exact step's memory, NULL for none (the stopgap build): three in a row when the stroke aligns.
 * @param joint Whether an aligned stroke is covered by UC8 (nonzero) or composed from its parts by SC5's formula.
 * @param k Receives the array holding the composed coverage (`body`, or `result` for an aligned stroke), row by row over @p rect.
 * @param rect Receives the pixels `k` is for (reach meet the parts' boxes); @p whole_rect the rectangle of the whole render.
 * @param cells Receives the cell deposits counted, over what the whole render draws of the item.
 * @return ::GSHCPU_OK; ::GSHCPU_ERR_LIMIT when the deposits of all its parts pass @p max_cells.
 */
GSHCPU_Result gshcpu_render_stroke_cover(const GSHCPU_Stroke * s, GSHCPU_Rect reach, GSHCPU_Rect whole,
    uint64_t max_cells, int64_t * acc, size_t acc_stride, GSHCPU_Exact * ex, int joint, const GSHCPU_Stroke_Buffers * buffers,
    uint8_t ** k, GSHCPU_Rect * rect, GSHCPU_Rect * whole_rect, uint64_t * cells);

/**
 * FT-2 (SC8): whether @p s qualifies for the sparse composite: a single part, and `cells_bound` of its rectangle at
 * most a sixteenth of that rectangle's pixels and at most @p max_cells (so the cap cannot be met and the stroke cannot
 * be refused for its cells).  A function of the outline, the shift and the cap only, never of the damage or the clips.
 */
int gshcpu_render_stroke_sparse_ok(const GSHCPU_Stroke * s, uint64_t max_cells);

/** The pixels ::gshcpu_render_stroke_cover would cover (@p rect) and the whole render's (@p whole_rect), without covering. */
void gshcpu_render_stroke_rects(const GSHCPU_Stroke * s, GSHCPU_Rect reach, GSHCPU_Rect whole, GSHCPU_Rect * rect,
    GSHCPU_Rect * whole_rect);

/**
 * FT-2: ::gshcpu_render_stroke_cover for a qualifying stroke, handing each row's coverages to @p fn (in the destination's
 * frame) instead of building `k`.  @p acc must be all zero and is left so; @p line is `rect`'s width bytes.
 */
GSHCPU_Result gshcpu_render_stroke_cover_sparse(const GSHCPU_Stroke * s, GSHCPU_Rect reach, GSHCPU_Rect whole,
    uint64_t max_cells, int64_t * acc, GSHCPU_Exact * ex, uint8_t * line, GSHCPU_Span_Fn fn, void * ctx,
    GSHCPU_Rect * rect, GSHCPU_Rect * whole_rect, uint64_t * cells);

/**
 * SC5's composition of three coverages, one pixel: `t = (a * c' + 127) / 255` with `c'` the clip's coverage or
 * its complement, then `k = 255 - ((255 - b) * (255 - t) + 127) / 255`.
 */
uint32_t gshcpu_render_stroke_compose(uint32_t b, uint32_t a, uint32_t c, int complement);

/**
 * FT-2: what ::gshcpu_render_cover_counted computes, handed to @p fn a row at a time and only where it can be
 * nonzero, with the cells counted as it counts them.  @p acc must be all zero on entry and is left so.  @p line holds
 * `rect`'s width bytes.  @return as ::gshcpu_render_cover_counted.
 */
GSHCPU_Result gshcpu_render_cover_sparse(const GSHGEOM_Flat_Path * const * paths, size_t count, GSHGEOM_Fill_Rule rule,
    GSHCPU_Rect rect, GSHCPU_Rect walk, int64_t * acc, uint8_t * line, uint64_t max_cells, uint64_t * cells,
    GSHCPU_Span_Fn fn, void * ctx, GSHCPU_Exact * ex);

/* --- the sink (sink.c) ---------------------------------------------------------------- */

/**
 * A test's view of a render, called just before the write with the root layer (premultiplied doubles, four a
 * pixel, row by row over @p region) and the touched map: how the gate reads the values before their one rounding.
 */
typedef void (*GSHCPU_Render_Observer)(void * ctx, GSHCPU_Rect region, const double * root, const uint8_t * touched);

/** ::gshcpu_sink_create with an observer of every render (for tests). */
GSHCPU_Result gshcpu_render_sink_create_observed(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, GSHCPU_Render_Observer observer, void * observer_ctx, GSHSCENE_Sink ** out);

/**
 * ::gshcpu_sink_create with the tiers named by @p tiers on (`GSHCPU_RENDER_TIER_*`; the rest are forced off, which makes
 * the general tier the oracle of the others), an observer if not NULL, and the sink's counters written to
 * @p counters if not NULL (it must outlive the sink).
 */
GSHCPU_Result gshcpu_render_sink_create_tiered(const GSHCPU_Allocator * alloc, const GSHSCENE_Limits * scene_limits,
    const GSHCPU_Limits * limits, unsigned tiers, GSHCPU_Render_Counters * counters, GSHCPU_Render_Observer observer,
    void * observer_ctx, GSHSCENE_Sink ** out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_RENDER_INTERNAL_H */
