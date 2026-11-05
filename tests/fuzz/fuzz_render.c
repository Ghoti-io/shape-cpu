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
 * A render through the CPU sink driven by bytes (story 16 of the shape family's
 * first milestone; CAP-15, AD-19): one entry point for libFuzzer and for the
 * replay gate (`make fuzz-replay`).
 *
 * Five header bytes choose the target's format and size, an extent and a
 * damage, the request (strict, caller memory), where the token is set, the
 * sink's limits (each small enough to be met, or the default) and the
 * allocation that fails.  The rest interns up to eight paths of degenerate
 * geometry (coordinates from a table of zeros, the 26.6 range's edges, huge and
 * tiny values, or from the bytes; self-intersecting, open, empty) and two runs
 * of glyphs from the test provider (bound with the fast tier and without), and
 * records a list of fills, clips and groups (every operator, opacity and a
 * stated blend space) under maps that scale, rotate and translate, always
 * balanced and closed.  Since color-paint story P2 a fill's paint may be a
 * gradient built from the bytes: every kind, extend and interpolation space,
 * premultiplied or not, no stops to five (unsorted, equal offsets, foreground
 * and palette stops), under gradient maps that turn, flatten (G11) or carry the
 * fill out of the sampling domain (G14, the sink's own limit), and values past
 * G14's bounds (refused at intern, the fill then solid).  N1 (Corey, 2026-10-21):
 * a solid paint's byte may give a component at `2^16` or one step past it in a
 * gamma-10 or gamma-0.1 space (past it the fill is refused, `ERR_LIMIT`; at it
 * the conversions reach the saturation), and a group may blend in gamma 0.1; a
 * float target's every stored value is then finite.  Strokes (stories S1 and S4): a fill whose rule byte has bits 1 and 2
 * set is a stroke of the shape instead, in a style made once from the next three bytes: a width (zero, a hairline, a
 * sixteenth of a pixel, wide), both width spaces, every join, cap and alignment, either alignment rule, either
 * degenerate rule, dashes of a few patterns in either scope.  The sink draws it by expansion, a stroke of a glyph run too
 * (strokes story S5: the path ::gshscene_glyph_run_centerline defines for it), and no stroke is ever refused for its
 * kind.  The render is then drawn again by a second sink with every fast tier off, and the bytes must be the same.  Masks (masks
 * story M2): with the request byte's bit 7 set, a group whose blend byte has bits 7 and 5 as 0 and 1 takes a soft mask from the
 * bytes that follow: alpha or luminance, a coefficient set, either thing it acts on, a transfer of any kind, the picture's and
 * the luminance's spaces, a backdrop, a placement (identity, a translation, a scale, a turn, a singular map), and a picture of
 * one to three fills (solid, gradient or image paints, under maps, clips and groups, and a masked group of its own to a depth
 * of two); the render's invariants then hold with masks in it, the two mask tiers off give the same bytes, and a refusal inside a
 * mask picture is the same in a damage-limited render.  A
 * list that holds a stroke of a run is also rebuilt with each such centerline replaced by its path (ST11): drawn, it is
 * the same bytes.
 *
 * After the render the sink's stated properties are checked, and a violation is
 * an `abort`:
 *
 * - the result is one the contract allows for the request, and the report's
 *   summary says the same;
 * - a successful render is deterministic: the same request again into a fresh
 *   copy of the backdrop gives the same bytes; with caller memory, the exact
 *   workspace gives the bytes of the default render, and a byte less is
 *   `ERR_LIMIT` with nothing allocated and no need stated (S9);
 * - pixels outside the extent and the damage are never touched, and inside the
 *   damage the render equals the whole render, which refuses the same items for
 *   the sink's limits (S4);
 * - a cancel at unit k stops after exactly k units;
 * - nothing leaks: the sink, the store, the list and the report take every byte
 *   from a counting allocator, which also fails the chosen call.
 *
 * The replay fails if the corpus never reaches an outcome:
 * tools/gen-fuzz-corpus.py writes it.
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>
#include <ghoti.io/shape-scene/testing/glyph_test_provider.h>

#include <ghoti.io/image/raster.h>

#include "../../src/render/render_internal.h"
#include "../support/exact_s4b.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The largest workspace the harness allocates for a caller-memory request. */
#define FUZZ_WORKSPACE_MAX ((size_t)64 << 20)

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      fprintf(stderr, "fuzz_render: invariant failed: %s (line %d)\n", #cond, __LINE__); \
      abort(); \
    } \
  } while (0)

/* --- the outcomes the corpus must reach ----------------------------------------------------------- */

enum {
  OUT_OK = 0, OUT_REFUSED_WHILE_DRAWING, OUT_STRICT, OUT_LIMIT_REGION, OUT_CANCEL_MID, OUT_OOM, OUT_CALLER_MEMORY,
  OUT_WORKSPACE_SHORT, OUT_DAMAGE, OUT_GLYPHS_FAST, OUT_GLYPHS_GENERAL, OUT_FLOAT_TARGET, OUT_NESTED_GROUP,
  OUT_GRADIENT_DRAWN, OUT_GRADIENT_NOTICE, OUT_GRADIENT_LIMIT, OUT_COLOR_LIMIT, OUT_COLOR_AT_BOUND, OUT_BLEND_DRAWN,
  OUT_BLEND_FLOAT, OUT_GENERAL_DRAWN, OUT_STROKE_RUN_DRAWN, OUT_STROKE_DRAWN, OUT_STROKE_ALIGNED, OUT_STROKE_CACHED,
  OUT_STROKE_SPARSE, OUT_STROKE_NOTICE, OUT_STROKE_EXACT, OUT_STROKE_RUN_TWIN, OUT_STROKE_DUAL,
  OUT_BUDGET_REFUSED, OUT_BUDGET_SAME, OUT_IMAGE_DRAWN, OUT_IMAGE_AREA, OUT_IMAGE_LIMIT, OUT_IMAGE_NOTICE, OUT_IMAGE_TINT,
  OUT_IMAGE_STROKE, OUT_MASK_DRAWN, OUT_MASK_LUMINANCE, OUT_MASK_NESTED, OUT_MASK_OUTSIDE, OUT_MASK_IDENTITY,
  OUT_MASK_REFUSED_INSIDE, OUT_MASK_TIERS_EQUAL, OUT_MASK_DAMAGE, OUT_MASK_CALLER_MEMORY, OUT_NON_ISOLATED, OUT_KNOCKOUT, OUT_GENERAL_MASKED, OUT_GENERAL_SHAPE,
  OUT_GENERAL_NESTED, OUT_GENERAL_CALLER_MEMORY, OUT_GENERAL_DAMAGE, OUT_GENERAL_STROKE,
  OUT_PATTERN_DRAWN, OUT_PATTERN_OVERLAP, OUT_PATTERN_PERIOD, OUT_PATTERN_TIERS_EQUAL, OUT_PATTERN_CACHED, OUT_PATTERN_NESTED,
  OUT_PATTERN_STROKE, OUT_PATTERN_LIMIT, OUT_PATTERN_CALLER_MEMORY, OUT_PATTERN_DAMAGE, OUT_PATTERN_GAVE_UP,
  OUT_HATCH_DRAWN, OUT_HATCH_DASHED, OUT_HATCH_NESTING, OUT_HATCH_CACHED, OUT_HATCH_CALLER_MEMORY, OUT_HATCH_NOTICE, OUT_HATCH_LINES_LIMIT, OUT_COUNT
};

static const char * const outcome_names[OUT_COUNT] = {"a render", "a refusal while drawing (a sink's own limit)",
    "strict, refused", "a region past the sink's limits", "cancelled between units", "a failed allocation",
    "a render in caller memory", "a workspace a byte short", "a damage-limited render equal to the whole",
    "glyphs on the fast tier", "glyphs on the general route", "a float target", "a group inside a group",
    "a render with a gradient fill", "a gradient fill that draws nothing (a notice)",
    "a gradient past G14's bounds, refused at intern", "a fill's color past N1's bound, refused",
    "a render with a color at N1's bound in a gamma space", "a render with a blend-mode group",
    "a blend-mode group on a float target", "a render with a non-isolated or knockout group composited (KC1)",
    "a render with a stroke of a glyph run drawn (ST8)", "a render with a stroke drawn (SC1)",
    "a render with an aligned stroke drawn (SC5)", "a stroke served from the cache (SC7)",
    "a stroke drawn by the sparse composite (SC8)", "a stroke that draws nothing (a notice)",
    "a stroke with cells made exact (UC3)", "a stroked run equal to the stroke of its path (ST11)",
    "a stroke drawn the same by the earlier exact step (the dual render)",
    "an item refused by a tightened crossings budget (UC19)",
    "a render under a tightened budget or piece cap that refused nothing, the same bytes as the default limits",
    "a render with an image paint drawn (IC1)", "an image drawn with the AREA filter (IC7)",
    "an image item refused for the sink's work or domain limit (IC9)", "an image fill that draws nothing (a notice)",
    "an alpha-only image drawn with a tint (IP9)", "a stroke painted with an image",
    "a render with a masked group composited (MC1)", "a luminance mask composited (MC3)", "a mask of a mask (MC2)",
    "a masked pop that took the outside value (FT-M1)", "a masked pop that skipped an identity transfer (FT-M2)",
    "an item inside a mask picture refused for a sink limit (MC6)", "a masked render equal to the render with both mask tiers off (MC8)",
    "a damage-limited render of a masked group equal to the whole render (MC6)", "a masked render in caller memory (MC5)",
    "a render with a non-isolated group (KC5)", "a render with a knockout group (KC4)", "a masked group that is non-isolated or knockout (KC10)",
    "a group shape carried under a knockout ancestor (KC3)", "general groups nested, a group alpha or group shape channel in two layers (KC8)",
    "a render with a general group in caller memory (KC8)", "a damage-limited render of a general group equal to the whole render (KC10)",
    "a stroke drawn into a general layer (KC2)",
    "a render with a pattern fill or stroke drawn (PC2)", "a pattern whose cells overlap drawn (PC6)", "a pattern drawn by the period tier (PC9)",
    "a pattern render equal to the render with the period tier off (PC9)", "a period raster served from the cache (PC10)",
    "a pattern nested in a pattern's tile (PC5)", "a stroke in a pattern's tile (PC5)", "a pattern use refused for a sink limit (PC8)",
    "a pattern render in caller memory (PC11)", "a damage-limited render of a pattern equal to the whole render (PC11)",
    "the period tier given up for the general tier (PC9)",
    "a render with a hatch fill drawn (HX13)", "a hatch with a dashed family drawn (HX6)", "a hatch of the outer or ignore nesting drawn (HX4)",
    "a hatch expansion served from the cache (HX13)", "a hatch render in caller memory (HX13)", "a hatch fill that draws nothing, noticed (HX9)",
    "a hatch refused for the lines limit (HX12)"};

static unsigned long outcomes[OUT_COUNT];

int gshcpu_fuzz_unreached(void);

int gshcpu_fuzz_unreached(void) {
  int missing = 0;
  for (int i = 0; i < OUT_COUNT; i++) {
    if (outcomes[i] == 0) {
      fprintf(stderr, "fuzz_render: the corpus never reached: %s\n", outcome_names[i]);
      missing++;
    }
  }
  return missing;
}

/* --- a counting allocator that can fail one call -------------------------------------------------- */

typedef struct Counter {
  long live;
  long calls;
  long fail_at; /* -1 never */
  int failed;
} Counter;

static int fails(Counter * c) {
  if (c->fail_at >= 0 && c->calls++ == c->fail_at) {
    c->failed = 1;
    return 1;
  }
  return 0;
}

static void * c_malloc(void * ctx, size_t n) {
  Counter * c = ctx;
  void * p = fails(c) ? NULL : malloc(n ? n : 1);
  c->live += p != NULL;
  return p;
}

static void * c_calloc(void * ctx, size_t k, size_t n) {
  Counter * c = ctx;
  void * p = fails(c) ? NULL : calloc(k ? k : 1, n ? n : 1);
  c->live += p != NULL;
  return p;
}

static void * c_realloc(void * ctx, void * old, size_t n) {
  Counter * c = ctx;
  void * p = fails(c) ? NULL : realloc(old, n ? n : 1);
  c->live += p != NULL && old == NULL;
  return p;
}

static void c_free(void * ctx, void * p) {
  ((Counter *)ctx)->live -= p != NULL;
  free(p);
}

/* --- the input ---------------------------------------------------------------------------------------- */

typedef struct Input {
  const uint8_t * data;
  size_t size;
  size_t at;
} Input;

static uint8_t next(Input * in) {
  return in->at < in->size ? in->data[in->at++] : 0;
}

/** A model coordinate: a degenerate value, or one in [-32, 96) in 1/8 pixel, around a target of at most 64. */
static double coordinate(Input * in) {
  static const double table[] = {0.0, -0.0, 0.5, 1e-9, 1e9, -1e9, 33554431.0, -33554432.0, 0x1p60, 64.0, 63.999, 1.0 / 128.0};
  const uint8_t pick = next(in);
  if (pick < 12) {
    return table[pick];
  }
  return (double)((int)(((unsigned)pick << 8 | next(in)) % 1024) - 256) / 8.0;
}

/* --- the progress callback: set the token at unit k --------------------------------------------------------- */

typedef struct Progress {
  GSHSCENE_Cancel * cancel;
  size_t at; /* 0: never */
} Progress;

static void on_progress(void * ctx, const GSHSCENE_Job * job, size_t processed) {
  (void)job;
  Progress * p = ctx;
  if (p->at && processed == p->at) {
    gshscene_cancel_set(p->cancel);
  }
}

/* --- the list ------------------------------------------------------------------------------------------- */

#define MAX_PATHS 8

typedef struct World {
  GSHSCENE_Store * store;            /* where a gradient paint is interned */
  size_t gradients;                  /* gradient fills recorded */
  size_t images;                     /* fills and strokes recorded with an image paint (IC1) */
  size_t image_areas;                /* ... whose paint can use the AREA filter */
  size_t image_tints;                /* ... of an alpha-only image */
  size_t image_strokes;              /* ... strokes */
  size_t at_bound;                   /* fills recorded with a component at N1's bound, in a gamma space */
  size_t blends;                     /* groups recorded with a blend mode (color-paint story P3) */
  const GSHSCENE_Resource * paths[MAX_PATHS];
  size_t path_count;
  const GSHSCENE_Resource * runs[2]; /* from a face with the fast tier, and from one without */
  const GSHSCENE_Resource * style;   /* a stroke style, made on first use (strokes story S1) */
  size_t strokes;                    /* strokes recorded */
  size_t run_strokes;                /* ... whose centerline is a glyph run */
  size_t aligned;                    /* ... whose style aligns */
  int aligned_style;
  double dashes[4];                  /* the style's dash lengths */
  const GSHGEOM_Allocator * alloc;   /* the harness's counting allocator: the pictures of masks are made through it */
  int masks_on;                      /* the request byte's bit 7: a group may take a mask */
  int patterns_on;                   /* a fill whose rule byte is 0xF8 or more takes a pattern from the bytes that follow (R2) */
  size_t masks;                      /* groups recorded with a mask */
  size_t mask_luminance;             /* ... of luminance */
  size_t mask_nested;                /* ... masks inside a mask's picture */
  size_t general;                    /* groups made non-isolated or knockout */
  size_t knockouts;                  /* ... of them knockout groups */
  size_t patterns;                   /* fills and strokes recorded with a pattern paint (R2) */
  size_t pattern_overlaps;           /* ... whose cells overlap */
  size_t pattern_integers;           /* ... whose step vectors are whole device pixels under the identity or a translation (PC9) */
  size_t pattern_nested;             /* ... holding a pattern in the tile */
  size_t pattern_strokes;            /* ... holding a stroke in the tile */
  size_t hatches;                    /* fills recorded with a hatch paint (H1: a rule byte of 0xF0 to 0xF7 that is not a stroke) */
  size_t hatch_dashed;               /* ... with a dashed family */
  size_t hatch_nested;               /* ... of the outer or ignore nesting */
  size_t hatch_nothing;              /* ... that draw nothing (a spacing of 0, a width of 0, a singular map) */
  const GSHSCENE_Resource * tile_style; /* the style of a tile's stroke, made on first use */
} World;

static void intern_paths(Input * in, GSHSCENE_Store * store, const GSHGEOM_Allocator * alloc, World * w) {
  const size_t count = 1 + next(in) % MAX_PATHS;
  for (size_t i = 0; i < count; i++) {
    GSHGEOM_Path * path = NULL;
    if (gshgeom_path_create(alloc, NULL, 2, &path) != GSHGEOM_OK) {
      continue;
    }
    const int elements = next(in) % 12;
    for (int e = 0; e < elements; e++) {
      /* Each draw a statement of its own (C leaves the order inside an initializer or a call's arguments
       * unspecified), so that one input means one scene to every compiler. */
      GSHGEOM_Point a = {0.0, 0.0, 0.0}, b = {0.0, 0.0, 0.0}, c = {0.0, 0.0, 0.0};
      a.x = coordinate(in);
      a.y = coordinate(in);
      b.x = coordinate(in);
      b.y = coordinate(in);
      c.x = coordinate(in);
      c.y = coordinate(in);
      switch (next(in) % 6) {
        case 0:
          gshgeom_path_move_to(path, a);
          break;
        case 1:
        case 2:
          gshgeom_path_line_to(path, a);
          break;
        case 3:
          gshgeom_path_quadratic_to(path, a, b);
          break;
        case 4:
          gshgeom_path_cubic_to(path, a, b, c);
          break;
        default:
          gshgeom_path_close(path);
          break;
      }
    }
    const GSHSCENE_Resource * r = NULL;
    if (gshscene_store_intern_path(store, path, &r) == GSHSCENE_OK) {
      w->paths[w->path_count++] = r;
    }
    gshgeom_path_destroy(path);
  }
}

static void intern_runs(Input * in, GSHSCENE_Store * store, GSHSCENE_Test_Provider * tp, World * w) {
  for (int fast = 1; fast >= 0; fast--) {
    const GSHSCENE_Resource * face = gshscene_tp_face(store, tp, fast, 0x5EED0000u + (uint64_t)fast, 0);
    if (!face) {
      continue;
    }
    GSHSCENE_Glyph glyphs[3];
    for (int g = 0; g < 3; g++) {
      glyphs[g].id = next(in) % GSHSCENE_TP_GLYPHS;
      glyphs[g].x = coordinate(in);
      glyphs[g].y = coordinate(in);
      glyphs[g].outline = NULL;
    }
    static const double sizes[] = {16.0, 12.0, 13.333, 64.0, 1.0};
    GSHSCENE_Glyph_Run run;
    memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.face = face;
    run.size = sizes[next(in) % 5];
    run.hint_mode = next(in) % 3;
    run.glyph_count = 1 + next(in) % 3;
    run.glyphs = glyphs;
    gshscene_store_intern_glyph_run(store, &run, &w->runs[fast ? 0 : 1]);
    gshscene_resource_release(face);
  }
}

static void map_of(Input * in, GSHGEOM_Map * map) {
  GSHGEOM_Map a, b;
  double u, v, w;
  switch (next(in) % 5) {
    case 0:
      gshgeom_map_identity(2, map);
      break;
    case 1:
      u = coordinate(in);
      v = coordinate(in);
      gshgeom_map_translate(2, u, v, 0.0, map);
      break;
    case 2:
      u = (double)(next(in) % 17) / 4.0;
      v = (double)(next(in) % 17) / 4.0;
      gshgeom_map_scale(2, u, v, 1.0, &a);
      u = coordinate(in);
      v = coordinate(in);
      gshgeom_map_translate(2, u, v, 0.0, &b);
      gshgeom_map_compose(&b, &a, map);
      break;
    case 3:
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, (double)next(in) * 1.5, &a);
      gshgeom_map_translate(2, 32.0, 32.0, 0.0, &b);
      gshgeom_map_compose(&b, &a, map);
      break;
    default:
      w = (double)(1 + next(in) % 4) / 64.0;
      u = coordinate(in);
      v = coordinate(in);
      gshgeom_map_design_to_model(w, u, v, map);
      break;
  }
}

/** A gradient's double from a byte: small values, fractions, and G14's bounds and one step past each. */
static double gradient_value(uint8_t b, int * beyond) {
  static const double table[8] = {0x1p64, -0x1p64, 0x1p-64, 0x1.0000000000001p64, 0x1.fffffffffffffp-65, 1e-300, 0.0, 0.5};
  if (b < 248) {
    return (double)((int)(b % 80) - 8) / (b % 3 == 0 ? 1.0 : 2.0);
  }
  double v = table[b - 248];
  double m = v < 0 ? -v : v;
  *beyond |= m != 0.0 && (m > 0x1p64 || m < 0x1p-64);
  return v;
}

static GSHSCENE_Color_Space gradient_space(uint8_t b) {
  GSHSCENE_Color_Space sp;
  memset(&sp, 0, sizeof sp);
  if (b % 4 == 1) {
    sp.primaries = GCOL_PRIMARIES_SRGB;
    sp.transfer = GCOL_TRANSFER_LINEAR;
  } else if (b % 4 == 2) {
    sp.primaries = GCOL_PRIMARIES_DISPLAY_P3;
    sp.transfer = GCOL_TRANSFER_GAMMA;
    sp.gamma = 2.2;
  } else if (b % 4 == 3) {
    sp.primaries = GCOL_PRIMARIES_SRGB;
    sp.transfer = GCOL_TRANSFER_SRGB;
  }
  return sp;
}

/**
 * A gradient from the bytes (story P2), or NULL where it is refused at intern (G14 asserted by its bound: a value
 * past it never interns).  The caller holds the reference.
 */
static const GSHSCENE_Resource * gradient_of(Input * in, GSHSCENE_Store * store) {
  int beyond = 0;
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = 1u + next(in) % 3u;
  double v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = gradient_value(next(in), &beyond);
  }
  if (g.kind == GSHSCENE_GRADIENT_LINEAR) {
    g.p0[0] = v[0], g.p0[1] = v[1], g.p1[0] = v[2], g.p1[1] = v[3];
    if (next(in) % 3 == 0) {
      g.d[0] = v[4], g.d[1] = v[5];
    }
  } else if (g.kind == GSHSCENE_GRADIENT_RADIAL) {
    g.c0[0] = v[0], g.c0[1] = v[1], g.r0 = v[2] / 4.0, g.c1[0] = v[3], g.c1[1] = v[4], g.r1 = v[5];
  } else {
    g.center[0] = v[0], g.center[1] = v[1];
    g.angle_unit = next(in) % GSHGEOM_ANGLE_UNIT_COUNT;
    g.a0 = v[2], g.a1 = v[3];
  }
  g.extend = next(in) % GSHSCENE_EXTEND_COUNT;
  g.premultiplied = next(in) & 1u;
  g.space = gradient_space(next(in));
  const uint8_t mk = next(in);
  double m[4] = {1.0, 0.0, 0.0, 1.0};
  double t[2] = {0.0, 0.0};
  switch (mk % 6) {
    case 1: /* turned and scaled */
      m[0] = 0.8, m[1] = -0.6, m[2] = 0.6, m[3] = 0.8;
      t[0] = 3.0;
      break;
    case 2: /* flat: G11 */
      m[1] = 2.0, m[2] = 0.5, m[3] = 1.0;
      break;
    case 3: /* the fill outside the sampling domain: the sink's own limit */
      m[0] = m[3] = 0x1p-64;
      t[0] = t[1] = 0x1p64;
      break;
    case 4: /* small and large, in bounds */
      m[0] = 0x1p-60, m[3] = 64.0;
      break;
    default:
      break;
  }
  if (gshgeom_map_from_matrix(2, m, t, &g.map) != GSHGEOM_OK) {
    gshgeom_map_identity(2, &g.map);
  }
  GSHSCENE_Gradient_Stop stops[5];
  memset(stops, 0, sizeof stops);
  const size_t n = next(in) % 6;
  for (size_t i = 0; i < n; i++) {
    const uint8_t ob = next(in);
    stops[i].offset = ob >= 248 ? gradient_value(ob, &beyond) : (double)(ob % 11) / 8.0 - 0.25;
    const uint8_t kind = next(in);
    stops[i].color.rgba[3] = (double)(kind % 5) / 4.0;
    if (kind % 7 == 5) {
      stops[i].color.kind = GSHSCENE_PAINT_FOREGROUND;
    } else if (kind % 7 == 6) {
      stops[i].color.kind = GSHSCENE_PAINT_PALETTE; /* the list has no palette: the plan refuses the fill (N14) */
      stops[i].color.entry = 1;
    } else {
      stops[i].color.kind = GSHSCENE_PAINT_SOLID;
      stops[i].color.rgba[0] = (double)next(in) / 255.0;
      stops[i].color.rgba[1] = (double)next(in) / 255.0;
      stops[i].color.rgba[2] = (double)(kind % 3) / 2.0;
      /* a solid stop in the target's space only in a gradient that interpolates in it (G3) */
      stops[i].color.space = gradient_space(gshscene_color_space_is_target(&g.space) ? kind / 7u : 1u + kind % 3u);
    }
  }
  g.stops = n ? stops : NULL;
  g.stop_count = n;
  /* G14's bound, from the values the gradient holds (a draw above may go unused, or be scaled). */
  beyond = 0;
  const double held[22] = {g.p0[0], g.p0[1], g.p1[0], g.p1[1], g.d[0], g.d[1], g.c0[0], g.c0[1], g.r0, g.c1[0], g.c1[1],
      g.r1, g.center[0], g.center[1], g.a0, g.a1, g.map.m[0][0], g.map.m[0][1], g.map.m[1][0], g.map.m[1][1], g.map.t[0],
      g.map.t[1]};
  for (size_t i = 0; i < 22 + n; i++) {
    double x = i < 22 ? held[i] : stops[i - 22].offset;
    double m = x < 0 ? -x : x;
    beyond |= m != 0.0 && (m > 0x1p64 || m < 0x1p-64);
  }
  const GSHSCENE_Resource * r = NULL;
  GSHSCENE_Result res = gshscene_store_intern_gradient(store, &g, &r);
  CHECK(res == GSHSCENE_OK || res == GSHSCENE_ERR_LIMIT || res == GSHSCENE_ERR_OOM);
  if (beyond) {
    CHECK(res != GSHSCENE_OK);
    outcomes[OUT_GRADIENT_LIMIT] += res == GSHSCENE_ERR_LIMIT;
  }
  return res == GSHSCENE_OK ? r : NULL;
}

/**
 * An image paint from the bytes (masks-images-patterns story I3), or NULL where intern refuses it.  Bytes: the size (1 to 8 each
 * way), the format (layout, sample type and depth, alpha form, the image's space), the samples, then the window (whole, or a
 * sub-rectangle), the extend modes and the filters, the filter space, the map (a scale up or down, a turn, a shear, a singular
 * map, one far past the domain) and its translation, and for an alpha-only image the tint.  The caller holds the reference.
 */
static const GSHSCENE_Resource * image_paint_of(Input * in, World * w) {
  const uint8_t dims = next(in);
  const uint8_t fmt = next(in);
  const uint32_t iw = 1u + (dims & 7u);
  const uint32_t ih = 1u + ((dims >> 3) & 7u);
  static const unsigned layouts[4] = {1, 3, 4, 1};
  const unsigned layout = layouts[fmt & 3u];
  const int alpha_only = (fmt & 3u) == 3u;
  static const unsigned bit_depths[4] = {8, 12, 16, 32};
  const unsigned bits = bit_depths[(fmt >> 2) & 3u];
  GIMG_Pixel_Format format;
  memset(&format, 0, sizeof format);
  format.channel_model = layout == 1 ? GIMG_CHANNEL_GRAY : layout == 3 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
  format.channel_type = bits == 32 ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  format.layout = GIMG_LAYOUT_INTERLEAVED;
  format.channel_count = (uint8_t)layout;
  for (unsigned i = 0; i < layout; i++) {
    format.bits_per_channel[i] = (uint8_t)bits;
  }
  format.alignment = 1;
  format.alpha_form = (uint8_t)(layout == 4 ? ((fmt >> 4) & 1 ? GIMG_ALPHA_PREMULTIPLIED : GIMG_ALPHA_STRAIGHT) : GIMG_ALPHA_NONE);
  GIMG_Raster * raster = NULL;
  if (gimg_raster_create(iw, ih, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK) {
    return NULL;
  }
  unsigned char * base = (unsigned char *)gimg_raster_pixels(raster);
  const size_t stride = gimg_raster_stride_bytes(raster);
  const size_t size = bits <= 8 ? 1 : bits <= 16 ? 2 : 4;
  for (uint32_t y = 0; y < ih; y++) {
    for (uint32_t x = 0; x < iw; x++) {
      for (unsigned k = 0; k < layout; k++) {
        const uint8_t b = next(in);
        unsigned char * at = base + (size_t)y * stride + ((size_t)x * layout + k) * size;
        if (bits == 32) {
          const int is_alpha = (layout == 4 && k == 3) || alpha_only;
          const float f = is_alpha ? (float)b / 255.0f : (float)((int)(b % 64) - 8) / 16.0f; /* a float color leaves [0, 1]; alpha stays in it (IM3) */
          memcpy(at, &f, sizeof f);
        } else {
          const uint32_t top = (1u << bits) - 1u;
          const uint32_t q = (uint32_t)(((uint64_t)b * top + 127u) / 255u);
          if (size == 1) {
            *at = (unsigned char)q;
          } else {
            const uint16_t u = (uint16_t)q;
            memcpy(at, &u, sizeof u);
          }
        }
      }
    }
  }
  GSHSCENE_Image_Params params;
  memset(&params, 0, sizeof params);
  params.space = gradient_space(alpha_only ? 0 : fmt >> 5); /* an alpha-only image has no color, so no space (IM5) */
  params.flags = alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0u;
  const GSHSCENE_Resource * image = NULL;
  GSHSCENE_Result ir = gshscene_store_intern_image(w->store, raster, &params, &image);
  gimg_raster_destroy(raster);
  CHECK(ir == GSHSCENE_OK || ir == GSHSCENE_ERR_OOM || ir == GSHSCENE_ERR_LIMIT);
  const uint8_t win = next(in);
  const uint8_t modes = next(in);
  const uint8_t space_byte = next(in);
  const uint8_t map_kind = next(in);
  const uint8_t tx = next(in);
  const uint8_t ty = next(in);
  const uint8_t tint_byte = next(in);
  if (!image) {
    return NULL;
  }
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  if (win % 3 == 1) {
    ip.window[0] = win % iw;
    ip.window[1] = (win >> 2) % ih;
    ip.window[2] = ip.window[0] + 1u + (win >> 4) % (iw - ip.window[0]);
    ip.window[3] = ip.window[1] + 1u + (win >> 5) % (ih - ip.window[1]);
  }
  ip.extend_x = modes & 3u;
  ip.extend_y = (modes >> 2) & 3u;
  ip.magnify = (modes >> 4) & 3u;
  ip.minify = (modes >> 6) & 3u;
  ip.filter_space = gradient_space(space_byte);
  const double move[2] = {((double)tx - 64.0) / 4.0, ((double)ty - 64.0) / 4.0};
  double m[4] = {1.0, 0.0, 0.0, 1.0};
  switch (map_kind % 8u) {
    case 1:
      m[0] = m[3] = 4.5;
      break;
    case 2:
      m[0] = m[3] = 0.37;
      break;
    case 3:
      m[0] = 0.69282032302755092;
      m[1] = -0.4;
      m[2] = 0.4;
      m[3] = 0.69282032302755092;
      break;
    case 4:
      m[3] = 0.0; /* singular: draws nothing (IP11) */
      break;
    case 5:
      m[0] = m[3] = 0x1p-60; /* every pixel far past the domain: refused while drawing (IC9) */
      break;
    case 6:
      m[0] = 7.0;
      m[3] = 0.3;
      break;
    case 7:
      m[0] = 0.0;
      m[1] = -2.0;
      m[2] = 2.0;
      m[3] = 0.0;
      break;
    default:
      break;
  }
  GSHSCENE_Paint tint;
  memset(&tint, 0, sizeof tint);
  if (alpha_only) {
    tint.kind = tint_byte % 3u == 0 ? GSHSCENE_PAINT_SOLID : tint_byte % 3u == 1 ? GSHSCENE_PAINT_FOREGROUND : GSHSCENE_PAINT_PALETTE;
    tint.rgba[0] = 0.9;
    tint.rgba[1] = 0.3;
    tint.rgba[2] = 0.6;
    tint.rgba[3] = (double)(tint_byte % 5u + 1u) / 5.0;
    if (tint.kind != GSHSCENE_PAINT_SOLID) {
      tint.rgba[0] = tint.rgba[1] = tint.rgba[2] = 0.0;
      tint.entry = tint.kind == GSHSCENE_PAINT_PALETTE ? (tint_byte >> 4) & 1u : 0u; /* a foreground paint's entry is zero */
    }
    ip.tint = &tint;
  }
  const GSHSCENE_Resource * paint = NULL;
  const GSHSCENE_Result pr = gshgeom_map_from_matrix(2, m, move, &ip.map) == GSHGEOM_OK
      ? gshscene_store_intern_image_paint(w->store, &ip, &paint)
      : GSHSCENE_ERR_INVALID;
  gshscene_resource_release(image);
  CHECK(pr == GSHSCENE_OK || pr == GSHSCENE_ERR_OOM || pr == GSHSCENE_ERR_LIMIT);
  if (pr != GSHSCENE_OK) {
    return NULL;
  }
  w->images++;
  w->image_areas += ip.magnify == GSHSCENE_IMAGE_FILTER_AREA || ip.minify == GSHSCENE_IMAGE_FILTER_AREA;
  w->image_tints += alpha_only;
  return paint;
}

/** A fill's paint; a gradient or an image paint (held in @p held, to be released after the fill) when the bytes ask for one. */
static void paint_of(Input * in, World * w, GSHSCENE_Paint * paint, const GSHSCENE_Resource ** held) {
  memset(paint, 0, sizeof *paint);
  paint->kind = GSHSCENE_PAINT_SOLID;
  for (int i = 0; i < 4; i++) {
    paint->rgba[i] = (double)next(in) / 255.0;
  }
  const uint8_t b = next(in);
  if (b % 4 == 0) {
    paint->space.primaries = GCOL_PRIMARIES_DISPLAY_P3;
    paint->space.transfer = GCOL_TRANSFER_SRGB;
  } else if (b % 4 == 2 && (b & 0x80)) {
    /* N1: at 2^16 or one step past it, in a space whose transfer raises it to the tenth power or not. */
    static const double bound[4] = {0x1p16, -0x1p16, 0x1.0000000000001p16, -0x1.0000000000001p16};
    paint->rgba[(b >> 6) & 1] = bound[(b >> 4) & 3];
    paint->space.primaries = GCOL_PRIMARIES_SRGB;
    paint->space.transfer = GCOL_TRANSFER_GAMMA;
    paint->space.gamma = (b & 8) ? 10.0 : 0.1;
  } else if (b >= 0xF0 && (b & 3) == 3 && w->store) {
    const GSHSCENE_Resource * ip = image_paint_of(in, w);
    if (ip) {
      paint->kind = GSHSCENE_PAINT_IMAGE;
      paint->rgba[0] = paint->rgba[1] = paint->rgba[2] = 0.0;
      paint->rgba[3] = (double)(b % 5) / 4.0 + 0.0;
      paint->resource = ip;
      *held = ip;
    }
  } else if (b % 4 == 1 && w->store) {
    const GSHSCENE_Resource * g = gradient_of(in, w->store);
    if (g) {
      paint->kind = GSHSCENE_PAINT_GRADIENT;
      paint->rgba[0] = paint->rgba[1] = paint->rgba[2] = 0.0;
      paint->resource = g;
      *held = g;
      w->gradients++;
    }
  }
}

static const GSHSCENE_Resource * mask_of(Input * in, World * w, int depth);

/** A group's mask placement from a byte: identity (zero-filled), a translation, a scale, a turn about (32, 32), a singular map. */
static void placement_of(uint8_t b, GSHGEOM_Map * map) {
  GSHGEOM_Map a, c;
  switch (b % 7) {
    case 1:
      gshgeom_map_translate(2, (double)(b >> 3) - 8.0, (double)(b >> 4) - 4.0, 0.0, map);
      break;
    case 2:
      gshgeom_map_scale(2, 1.5, 0.75, 1.0, map);
      break;
    case 3:
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, (double)(b >> 3) * 4.0, &a);
      gshgeom_map_translate(2, 32.0, 32.0, 0.0, &c);
      gshgeom_map_compose(&c, &a, map);
      break;
    case 4:
      gshgeom_map_scale(2, 0.0, 1.0, 1.0, map); /* singular: every item of the picture is degenerate (MK5) */
      break;
    default:
      memset(map, 0, sizeof *map); /* the identity, as a group with no placement has it */
      break;
  }
}

/**
 * A soft mask from the bytes (masks story M2), or NULL where nothing can be made of it (the picture or the mask is refused at
 * intern: a limit, or the store out of memory).  Bytes: the picture's shape (how many fills, a clip around the last, a
 * masked group of its own), the mode, coefficient set, acts-on and backdrop, the transfer, the two spaces; then each fill's
 * path, map, paint and rule as a list's fill.  The caller holds the reference.
 */
static const GSHSCENE_Resource * mask_of(Input * in, World * w, int depth) {
  const uint8_t shape = next(in);
  const uint8_t mode = next(in);
  const uint8_t transfer = next(in);
  const uint8_t spaces = next(in);
  GSHSCENE_List * picture_list = NULL;
  if (!w->store || !w->path_count || gshscene_list_create(w->alloc, NULL, w->store, &picture_list) != GSHSCENE_OK) {
    return NULL;
  }
  const size_t fills = 1u + shape % 3u;
  int nested = 0;
  for (size_t i = 0; i < fills; i++) {
    GSHGEOM_Map map;
    GSHSCENE_Paint paint;
    const GSHSCENE_Resource * held = NULL;
    const GSHSCENE_Resource * path = w->paths[next(in) % w->path_count];
    map_of(in, &map);
    paint_of(in, w, &paint, &held);
    int past = 0;
    for (int k = 0; k < 3; k++) {
      past |= !(paint.rgba[k] >= -0x1p16 && paint.rgba[k] <= 0x1p16);
    }
    const uint8_t rule = next(in);
    const GSHSCENE_Resource * inner = NULL;
    const int wrap = depth < 2 && (shape & 0x20) && i == 0;
    const int clipped = (shape & 0x40) && i + 1 == fills;
    int group_open = 0;
    int clip_open = 0;
    if (wrap) {
      inner = mask_of(in, w, depth + 1);
      GSHSCENE_Group group;
      memset(&group, 0, sizeof group);
      group.opacity = (double)(1 + rule % 4) / 4.0;
      group.soft_mask = inner;
      group_open = gshscene_list_group_push(picture_list, &group, 0, 0.0) == GSHSCENE_OK;
      nested += group_open && inner != NULL;
    }
    if (clipped) {
      clip_open = gshscene_list_clip_push(picture_list, path, &map, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK;
    }
    GSHSCENE_Result fr = past ? GSHSCENE_ERR_LIMIT
                              : gshscene_list_fill(picture_list, path, &map, rule & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
    w->gradients += held != NULL && paint.kind == GSHSCENE_PAINT_GRADIENT && fr == GSHSCENE_OK;
    w->images += held != NULL && paint.kind == GSHSCENE_PAINT_IMAGE && fr == GSHSCENE_OK;
    gshscene_resource_release(held);
    if (clip_open) {
      CHECK(gshscene_list_clip_pop(picture_list) == GSHSCENE_OK);
    }
    if (group_open) {
      CHECK(gshscene_list_group_pop(picture_list) == GSHSCENE_OK);
    }
    gshscene_resource_release(inner);
  }
  const GSHSCENE_Result closed = gshscene_list_close(picture_list);
  CHECK(closed == GSHSCENE_OK || closed == GSHSCENE_ERR_OOM);
  if (closed != GSHSCENE_OK) {
    gshscene_list_destroy(picture_list);
    return NULL;
  }
  const GSHSCENE_Resource * picture = NULL;
  GSHSCENE_Result pr = gshscene_store_intern_picture(w->store, picture_list, &picture);
  gshscene_list_destroy(picture_list);
  CHECK(pr == GSHSCENE_OK || pr == GSHSCENE_ERR_OOM || pr == GSHSCENE_ERR_LIMIT);
  if (pr != GSHSCENE_OK) {
    return NULL;
  }
  GSHSCENE_Mask m;
  memset(&m, 0, sizeof m);
  m.picture = picture;
  m.acts_on = (mode >> 3) & 1u;
  const int luminance = mode & 1u;
  m.mode = luminance ? GSHSCENE_MASK_LUMINANCE : GSHSCENE_MASK_ALPHA;
  if (luminance) {
    m.coefficients = (mode >> 1) & 3u;
    m.space = gradient_space((uint8_t)(spaces >> 4));
    static const double backdrops[8][3] = {{0, 0, 0}, {0.5, 0.5, 0.5}, {1, 1, 1}, {0.25, 0.5, 1.0}, {0.9, 0.1, 0.2}, {0, 0, 0}, {0.5, 0.5, 0.5}, {2, 0.125, 0}};
    memcpy(m.backdrop, backdrops[(mode >> 4) & 7u], sizeof m.backdrop);
    if ((mode >> 4 & 7u) == 7u) {
      m.backdrop_space = gradient_space((uint8_t)(spaces >> 4 ^ 2));
    }
  }
  m.render_space = gradient_space(spaces);
  double samples[4];
  GSHSCENE_Transfer_Piece pieces[2];
  switch (transfer & 3u) {
    case 1: {
      static const double exponents[4] = {0.0, 0.5, 1.0, 2.0};
      m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      m.transfer.c0 = (double)((transfer >> 2) & 3u) / 4.0;
      m.transfer.c1 = 1.0 - (double)((transfer >> 4) & 3u) / 8.0;
      m.transfer.n = exponents[(transfer >> 6) & 3u];
      break;
    }
    case 2:
      m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
      m.transfer.sample_count = 2u + (transfer >> 2) % 3u;
      for (size_t i = 0; i < m.transfer.sample_count; i++) {
        samples[i] = (double)next(in) / 255.0;
      }
      m.transfer.samples = samples;
      break;
    case 3: {
      const double cut = (double)(1 + (transfer >> 2) % 3u) / 4.0;
      memset(pieces, 0, sizeof pieces);
      pieces[0].lo = 0.0;
      pieces[0].hi = cut;
      pieces[0].e0 = 0.0;
      pieces[0].e1 = 1.0;
      pieces[0].leaf_kind = GSHSCENE_TRANSFER_IDENTITY;
      pieces[1].lo = cut;
      pieces[1].hi = 1.0;
      pieces[1].e0 = 1.0;
      pieces[1].e1 = 0.25;
      pieces[1].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      pieces[1].c0 = 0.0;
      pieces[1].c1 = 1.0;
      pieces[1].n = (transfer >> 5) & 1u ? 2.0 : 0.5;
      m.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
      m.transfer.piece_count = 2;
      m.transfer.pieces = pieces;
      break;
    }
    default:
      break;
  }
  const GSHSCENE_Resource * mask = NULL;
  GSHSCENE_Result mr = gshscene_store_intern_mask(w->store, &m, &mask);
  gshscene_resource_release(picture);
  CHECK(mr == GSHSCENE_OK || mr == GSHSCENE_ERR_OOM || mr == GSHSCENE_ERR_LIMIT || mr == GSHSCENE_ERR_INVALID);
  if (mr != GSHSCENE_OK) {
    return NULL;
  }
  w->mask_luminance += luminance;
  w->mask_nested += nested > 0;
  return mask;
}

/**
 * A pattern from the bytes (masks-images-patterns story R2), or NULL where nothing can be made of it.  Bytes: the configuration (the
 * tile's fills, a stroke, a clip, a group, a gradient or image paint, a nested pattern, a tint), the cell and the steps (from tables
 * that give abutting, gapped, overlapping, whole-pixel and fractional lattices), the repeats and a map (identity, a translation by a
 * fraction or by whole pixels or very far, a scale, an integer shear and turn, a quarter turn, a singular map); then the tile's fills as a
 * list's.  A nest is as deep as @p depth allows.  The caller holds the reference.
 */
static const GSHSCENE_Resource * pattern_of(Input * in, World * w, int depth) {
  static const double sizes[8] = {1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 2.5, 5.5};
  static const double steps[10] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 12.0, 0.75, 2.5};
  const uint8_t cfg = next(in);
  const uint8_t geom = next(in);
  const uint8_t maps = next(in);
  const uint8_t tint = next(in);
  GSHSCENE_List * tile_list = NULL;
  if (!w->store || !w->path_count || gshscene_list_create(w->alloc, NULL, w->store, &tile_list) != GSHSCENE_OK) {
    return NULL;
  }
  const size_t fills = 1u + cfg % 3u;
  int holds_pattern = 0;
  int holds_stroke = 0;
  for (size_t i = 0; i < fills; i++) {
    GSHGEOM_Map map;
    GSHSCENE_Paint paint;
    const GSHSCENE_Resource * held = NULL;
    const GSHSCENE_Resource * path = w->paths[next(in) % w->path_count];
    map_of(in, &map);
    paint_of(in, w, &paint, &held);
    int past = 0;
    for (int k = 0; k < 3; k++) {
      past |= !(paint.rgba[k] >= -0x1p16 && paint.rgba[k] <= 0x1p16);
    }
    const uint8_t rule = next(in);
    const GSHSCENE_Resource * inner = NULL;
    if (depth < 1 && (cfg & 0x40) && i == 0) {
      inner = pattern_of(in, w, depth + 1);
      if (inner) {
        gshscene_resource_release(held);
        memset(&paint, 0, sizeof paint);
        paint.kind = GSHSCENE_PAINT_PATTERN;
        paint.resource = inner;
        paint.rgba[3] = 1.0;
        held = NULL;
        holds_pattern = 1;
      }
    }
    int group_open = 0;
    int clip_open = 0;
    if ((cfg & 0x20) && i == 0) {
      GSHSCENE_Group group;
      memset(&group, 0, sizeof group);
      group.opacity = (double)(1 + rule % 4) / 4.0;
      group.composite = (rule >> 2) & 1 ? GSHSCENE_COMPOSITE_MULTIPLY : GSHSCENE_COMPOSITE_SRC_OVER;
      group.isolation = (rule >> 3) & 1 ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
      group_open = gshscene_list_group_push(tile_list, &group, 0, 0.0) == GSHSCENE_OK;
    }
    if ((cfg & 0x80) && i + 1 == fills) {
      clip_open = gshscene_list_clip_push(tile_list, path, &map, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK;
    }
    GSHSCENE_Result fr;
    if (past) {
      fr = GSHSCENE_ERR_LIMIT;
    } else if ((cfg & 0x10) && i == 0 && !inner) {
      if (!w->tile_style) {
        GSHSCENE_Stroke_Style st;
        gshscene_stroke_style_init(&st);
        st.width = (cfg >> 2) & 1 ? 0.5 : 1.75;
        st.cap = (cfg >> 3) & 1 ? GSHSCENE_STROKE_CAP_ROUND : GSHSCENE_STROKE_CAP_BUTT;
        const GSHSCENE_Result sr = gshscene_store_intern_stroke_style(w->store, &st, &w->tile_style);
        CHECK(sr == GSHSCENE_OK || sr == GSHSCENE_ERR_OOM || sr == GSHSCENE_ERR_LIMIT);
      }
      fr = w->tile_style ? gshscene_list_stroke(tile_list, path, w->tile_style, &map, &paint, 0, 0.0) : GSHSCENE_ERR_OOM;
      holds_stroke += fr == GSHSCENE_OK;
    } else {
      fr = gshscene_list_fill(tile_list, path, &map, rule & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
    }
    w->gradients += held != NULL && paint.kind == GSHSCENE_PAINT_GRADIENT && fr == GSHSCENE_OK;
    w->images += held != NULL && paint.kind == GSHSCENE_PAINT_IMAGE && fr == GSHSCENE_OK;
    gshscene_resource_release(held);
    gshscene_resource_release(inner);
    if (clip_open) {
      CHECK(gshscene_list_clip_pop(tile_list) == GSHSCENE_OK);
    }
    if (group_open) {
      CHECK(gshscene_list_group_pop(tile_list) == GSHSCENE_OK);
    }
  }
  const GSHSCENE_Result closed = gshscene_list_close(tile_list);
  CHECK(closed == GSHSCENE_OK || closed == GSHSCENE_ERR_OOM);
  if (closed != GSHSCENE_OK) {
    gshscene_list_destroy(tile_list);
    return NULL;
  }
  const GSHSCENE_Resource * picture = NULL;
  GSHSCENE_Result pr = gshscene_store_intern_picture(w->store, tile_list, &picture);
  gshscene_list_destroy(tile_list);
  CHECK(pr == GSHSCENE_OK || pr == GSHSCENE_ERR_OOM || pr == GSHSCENE_ERR_LIMIT);
  if (pr != GSHSCENE_OK) {
    return NULL;
  }
  GSHSCENE_Pattern pt;
  gshscene_pattern_init(&pt);
  pt.tile = picture;
  pt.x = (geom >> 6) & 1 ? -0.5 : 0.0;
  pt.y = (geom >> 7) & 1 ? 0.25 : 0.0;
  pt.w = sizes[geom & 7];
  pt.h = sizes[(geom >> 3) & 7];
  pt.repeat_x = (tint & 0x40) ? 0u : 1u;
  pt.repeat_y = (tint & 0x80) ? 0u : 1u;
  pt.x_step = steps[maps % 10];
  pt.y_step = steps[(maps >> 4) % 10];
  if (cfg % 5 == 0) {
    pt.x_step = pt.w; /* abutting */
  }
  if (cfg % 7 == 0) {
    pt.y_step = pt.h;
  }
  GSHGEOM_Map m;
  switch (tint % 8) {
    case 1:
      gshgeom_map_translate(2, 0.3, 0.45, 0.0, &m);
      break;
    case 2:
      gshgeom_map_translate(2, 5.0, 3.0, 0.0, &m);
      break;
    case 3:
      gshgeom_map_scale(2, 2.0, 2.0, 1.0, &m);
      break;
    case 4: {
      const double sh[4] = {1.0, 0.0, 1.0, 1.0};
      const double tt[2] = {0.25, 0.75};
      CHECK(gshgeom_map_from_matrix(2, sh, tt, &m) == GSHGEOM_OK);
      break;
    }
    case 5: {
      const double rot[4] = {3.0, -4.0, 4.0, 3.0};
      const double tt[2] = {0.5, 0.5};
      CHECK(gshgeom_map_from_matrix(2, rot, tt, &m) == GSHGEOM_OK);
      break;
    }
    case 6:
      gshgeom_map_translate(2, -4000.3, 3999.7, 0.0, &m);
      break;
    case 7:
      gshgeom_map_scale(2, 0.0, 1.0, 1.0, &m); /* singular */
      break;
    default:
      gshgeom_map_identity(2, &m);
      break;
  }
  pt.map = m;
  GSHSCENE_Paint tint_paint;
  memset(&tint_paint, 0, sizeof tint_paint);
  if ((tint & 0x30) == 0x30) {
    tint_paint.kind = GSHSCENE_PAINT_SOLID;
    tint_paint.rgba[0] = 0.2;
    tint_paint.rgba[1] = 0.6;
    tint_paint.rgba[2] = 0.9;
    tint_paint.rgba[3] = 0.8;
    pt.has_tint = 1;
    pt.tint = tint_paint;
  }
  const GSHSCENE_Resource * pattern = NULL;
  const GSHSCENE_Result r = gshscene_store_intern_pattern(w->store, &pt, &pattern);
  gshscene_resource_release(picture);
  CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_OOM || r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_UNSUPPORTED);
  if (r != GSHSCENE_OK) {
    return NULL;
  }
  w->patterns++;
  w->pattern_overlaps += (pt.repeat_x && pt.x_step < pt.w) || (pt.repeat_y && pt.y_step < pt.h);
  w->pattern_integers += pt.repeat_x && pt.repeat_y && (tint % 8) <= 5 && pt.x_step == (double)(int)pt.x_step && pt.y_step == (double)(int)pt.y_step;
  w->pattern_nested += holds_pattern;
  w->pattern_strokes += holds_stroke > 0;
  return pattern;
}

/**
 * A hatch from the bytes (masks-images-patterns story H1), or NULL where nothing can be made of it.  Bytes: the nesting and the number of
 * families (nesting = b % 3, 1 + (b >> 2) % 2 families), then for each family its angle (table index), spacing (table, with a 0 and a
 * negative), stagger (table) and dash pattern (table, with none, dots and a pattern that has no pen-down element), then the pen (a table
 * of widths, the width space, the cap) and the hatch map (identity, a stretch, a rotation, a singular map).
 */
static const GSHSCENE_Resource * hatch_of(Input * in, World * w) {
  static const double angles[8] = {0.0, 90.0, 45.0, 30.0, 135.0, 180.0, 17.0, 1.0e6};
  static const double spacings[8] = {2.0, 3.5, 0.0, 1.25, -2.0, 7.0, 0.5, 5.0};
  static const double staggers[4] = {0.0, 0.75, -1.0, 2.5};
  static const double patterns[7][4] = {{0, 0, 0, 0}, {3, -1, 0, 0}, {0, -2, 0, 0}, {2, -1, 0.5, -1}, {-1, -2, 0, 0}, {1.5, 0, 0, 0}, {0, -1, 1, 0}};
  static const unsigned counts[7] = {0, 2, 2, 4, 2, 1, 3};
  const uint8_t head = next(in);
  const size_t nf = 1 + (head >> 2) % 2u;
  GSHSCENE_Hatch_Family fam[2];
  for (size_t i = 0; i < nf; i++) {
    const uint8_t a = next(in);
    const uint8_t b = next(in);
    const unsigned p = (b >> 4) % 7u;
    fam[i].angle = angles[a & 7];
    fam[i].origin_x = (double)(int)((a >> 3) & 7) / 2.0;
    fam[i].origin_y = (double)(int)((a >> 6) & 3);
    fam[i].offset_along = staggers[b & 3];
    fam[i].offset_across = spacings[(b >> 2) & 7];
    fam[i].dash_count = counts[p];
    fam[i].dashes = counts[p] ? patterns[p] : NULL;
  }
  const uint8_t pen = next(in);
  const uint8_t mp = next(in);
  static const double widths[8] = {1.0, 0.0, 0.5, 2.5, 0.0625, 6.0, 1.5, 0.75};
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = widths[pen & 7];
  st.width_space = (pen >> 3) & 1;
  st.cap = (pen >> 4) % 3u;
  st.join = (pen >> 6) % 3u;
  const GSHSCENE_Resource * style = NULL;
  GSHSCENE_Result sr = gshscene_store_intern_stroke_style(w->store, &st, &style);
  CHECK(sr == GSHSCENE_OK || sr == GSHSCENE_ERR_OOM || sr == GSHSCENE_ERR_LIMIT);
  if (sr != GSHSCENE_OK) {
    return NULL;
  }
  GSHSCENE_Hatch h;
  gshscene_hatch_init(&h);
  h.style = style;
  h.nesting = head % 3u;
  h.family_count = nf;
  h.families = fam;
  switch (mp & 3u) {
    case 1:
      gshgeom_map_scale(2, 1.0, 1.0 + (double)(mp >> 4 & 3) / 2.0, 1.0, &h.map);
      break;
    case 2:
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, (double)(mp >> 2 & 31) * 3.0, &h.map);
      break;
    case 3:
      gshgeom_map_scale(2, 0.0, 1.0, 1.0, &h.map);
      break;
    default:
      break;
  }
  h.ink.rgba[0] = (double)(mp >> 4 & 3) / 3.0;
  const GSHSCENE_Resource * hatch = NULL;
  GSHSCENE_Result r = gshscene_store_intern_hatch(w->store, &h, &hatch);
  gshscene_resource_release(style);
  CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_OOM || r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_UNSUPPORTED);
  if (r != GSHSCENE_OK) {
    return NULL;
  }
  w->hatches++;
  for (size_t i = 0; i < nf; i++) {
    w->hatch_dashed += fam[i].dash_count != 0 && fam[i].dashes[0] > 0.0;
  }
  w->hatch_nested += h.nesting != 0;
  w->hatch_nothing += st.width == 0.0 || (mp & 3u) == 3u;
  return hatch;
}

/** Record the list: fills, clips and groups, closed balanced.  @return the deepest group nesting reached. */
static int record(Input * in, GSHSCENE_List * list, World * w) {
  int clips = 0, groups = 0, open[16], depth = 0, deepest = 0;
  const int items = next(in) % 24;
  for (int i = 0; i < items; i++) {
    const uint8_t op = next(in) % 8;
    GSHGEOM_Map map;
    GSHSCENE_Paint paint;
    const GSHSCENE_Resource * r = NULL;
    const uint8_t pick = next(in);
    if (pick % 4 == 3 && (w->runs[0] || w->runs[1])) {
      r = w->runs[pick / 4 % 2] ? w->runs[pick / 4 % 2] : w->runs[0] ? w->runs[0] : w->runs[1];
    } else if (w->path_count) {
      r = w->paths[pick % w->path_count];
    }
    if (op <= 3 && r) {
      map_of(in, &map);
      const GSHSCENE_Resource * held = NULL;
      paint_of(in, w, &paint, &held);
      int past = 0;
      for (int k = 0; k < 3; k++) {
        past |= !(paint.rgba[k] >= -0x1p16 && paint.rgba[k] <= 0x1p16);
      }
      /* The rule byte's bits 1 and 2 both set: a stroke of the shape instead (strokes stories S1 and S5). */
      const uint8_t rule = next(in);
      const GSHSCENE_Resource * pat = NULL;
      if ((rule & 0xF8) == 0xF8 && w->patterns_on) {
        /* R2: the paint is a pattern from the bytes that follow (a fill's rule 0xF8 to 0xFF; a stroke of it when bits 1 and 2 are set). */
        pat = pattern_of(in, w, 0);
        if (pat) {
          gshscene_resource_release(held);
          held = NULL;
          memset(&paint, 0, sizeof paint);
          paint.kind = GSHSCENE_PAINT_PATTERN;
          paint.resource = pat;
          paint.rgba[3] = (double)(1 + (rule >> 1) % 4) / 4.0;
          past = 0;
        }
      }
      if ((rule & 0xF8) == 0xF0 && (rule & 6) != 6 && w->patterns_on) {
        /* H1: the paint is a hatch from the bytes that follow (a fill's rule 0xF0, 0xF1, 0xF8 not: 0xF8 is a pattern). */
        const GSHSCENE_Resource * hat = hatch_of(in, w);
        if (hat) {
          gshscene_resource_release(held);
          held = hat;
          memset(&paint, 0, sizeof paint);
          paint.kind = GSHSCENE_PAINT_HATCH;
          paint.resource = hat;
          paint.rgba[3] = (double)(1 + (rule >> 1) % 4) / 4.0;
          past = 0;
        }
      }
      if ((rule & 6) == 6 && !w->style) {
        static const double widths[8] = {1.0, 0.0, 2.5, 6.0, 0.0625, 17.0, 1.5, 3.0};
        static const double patterns[4][2] = {{0.0, 0.0}, {3.0, 2.0}, {1.0, 1.0}, {0.0, 2.0}};
        const uint8_t a = next(in);
        const uint8_t b = next(in);
        const uint8_t c = next(in);
        GSHSCENE_Stroke_Style st;
        gshscene_stroke_style_init(&st);
        st.width = widths[a & 7];
        st.width_space = (a >> 3) & 1;
        st.join = b % 5;
        st.cap = (b >> 3) % 3;
        st.degenerate = (b >> 5) & 1;
        st.alignment = c % 3;
        st.alignment_rule = (c >> 2) & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
        st.miter_limit = (c >> 6) & 1 ? 1.0 : 4.0;
        w->dashes[0] = patterns[(c >> 3) & 3][0];
        w->dashes[1] = patterns[(c >> 3) & 3][1];
        if ((c >> 3) & 3) {
          st.dash_count = 2;
          st.dashes = w->dashes;
          st.dash_offset = (c >> 7) & 1 ? 0.5 : 0.0;
          st.dash_scope = (c >> 5) & 1;
        }
        GSHSCENE_Result sr = gshscene_store_intern_stroke_style(w->store, &st, &w->style);
        CHECK(sr == GSHSCENE_OK || sr == GSHSCENE_ERR_OOM || sr == GSHSCENE_ERR_LIMIT);
        w->aligned_style = st.alignment != 0;
      }
      GSHSCENE_Result fr = (rule & 6) == 6
          ? (w->style ? gshscene_list_stroke(list, r, w->style, &map, &paint, 0, 0.0) : GSHSCENE_ERR_OOM)
          : gshscene_list_fill(list, r, &map, rule & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
      w->strokes += (rule & 6) == 6 && fr == GSHSCENE_OK;
      w->run_strokes += (rule & 6) == 6 && fr == GSHSCENE_OK && gshscene_resource_kind(r) == GSHSCENE_RESOURCE_GLYPH_RUN;
      w->aligned += (rule & 6) == 6 && fr == GSHSCENE_OK && w->aligned_style;
      w->image_strokes += (rule & 6) == 6 && fr == GSHSCENE_OK && paint.kind == GSHSCENE_PAINT_IMAGE;
      if (past) {
        CHECK(fr == GSHSCENE_ERR_LIMIT || fr == GSHSCENE_ERR_OOM); /* N1: never recorded */
        outcomes[OUT_COLOR_LIMIT] += fr == GSHSCENE_ERR_LIMIT;
      } else if (fr == GSHSCENE_OK && paint.space.transfer == GCOL_TRANSFER_GAMMA &&
                 (paint.rgba[0] == 0x1p16 || paint.rgba[0] == -0x1p16 || paint.rgba[1] == 0x1p16 ||
                     paint.rgba[1] == -0x1p16)) {
        w->at_bound++;
      }
      gshscene_resource_release(held); /* the list holds its own reference */
      gshscene_resource_release(pat);
    } else if (op == 4 && r && depth < 16) {
      map_of(in, &map);
      if (gshscene_list_clip_push(list, r, &map, next(in) & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, 0, 0.0) ==
          GSHSCENE_OK) {
        open[depth++] = 0;
        clips++;
      }
    } else if (op == 5 && depth < 16) {
      GSHSCENE_Group group;
      memset(&group, 0, sizeof group);
      group.opacity = (double)(next(in) % 5) / 4.0;
      group.composite = next(in) % GSHSCENE_COMPOSITE_COUNT;
      const uint8_t blend = next(in);
      if (blend % 3 == 0) {
        group.blend_space.primaries = GCOL_PRIMARIES_SRGB;
        group.blend_space.transfer = GCOL_TRANSFER_LINEAR;
      } else if (blend % 3 == 1 && (blend & 0x40)) {
        /* N1: a gamma-0.1 group, whose encode raises a fill's color to the tenth power. */
        group.blend_space.primaries = GCOL_PRIMARIES_SRGB;
        group.blend_space.transfer = GCOL_TRANSFER_GAMMA;
        group.blend_space.gamma = 0.1;
      }
      /* T1, KC1: PDF's group fields, which this sink composites: bit 0x80 makes the group general, 0x20 then makes it a knockout group
       * (isolated) and 0x10 beside it a knockout group that is also non-isolated; without 0x20 it is non-isolated. */
      if (blend & 0x80) {
        group.knockout = (blend & 0x20) != 0;
        if (!(blend & 0x20) || (blend & 0x10)) {
          group.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
        }
        w->general += 1;
        w->knockouts += group.knockout;
      }
      const GSHSCENE_Resource * soft = NULL;
      if (w->masks_on && ((blend & 0xA0) == 0x20 || (blend & 0x88) == 0x88)) {
        soft = mask_of(in, w, 0);
        if (soft) {
          group.soft_mask = soft;
          placement_of(next(in), &group.soft_mask_map);
        }
      }
      GSHSCENE_Result gr = gshscene_list_group_push(list, &group, 0, 0.0);
      gshscene_resource_release(soft); /* the list holds its own reference */
      if (gr == GSHSCENE_OK) {
        w->masks += soft != NULL;
        open[depth++] = 1;
        groups++;
        w->blends += group.composite >= GSHSCENE_COMPOSITE_SCREEN;
        int nested = 0;
        for (int k = 0; k < depth; k++) {
          nested += open[k];
        }
        deepest = nested > deepest ? nested : deepest;
      }
    } else if (depth > 0) {
      depth--;
      CHECK((open[depth] ? gshscene_list_group_pop(list) : gshscene_list_clip_pop(list)) == GSHSCENE_OK);
    }
  }
  while (depth > 0) {
    depth--;
    CHECK((open[depth] ? gshscene_list_group_pop(list) : gshscene_list_clip_pop(list)) == GSHSCENE_OK);
  }
  (void)clips;
  (void)groups;
  CHECK(gshscene_list_close(list) == GSHSCENE_OK);
  return deepest;
}

/**
 * The list again with the centerline of every stroke that is a glyph run replaced by the path
 * ::gshscene_glyph_run_centerline gives for it (interned in @p store): a stroke of a run and a stroke of its path are the same
 * (ST11).  NULL when a path cannot be made or an item cannot be recorded (out of memory, a limit): nothing is compared then.
 * The paths are held by the list.
 */
static GSHSCENE_List * twin_of(const GSHSCENE_List * list, GSHSCENE_Store * store, const GSHGEOM_Allocator * alloc) {
  GSHSCENE_List * twin = NULL;
  if (gshscene_list_create(alloc, NULL, store, &twin) != GSHSCENE_OK) {
    return NULL;
  }
  const size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    CHECK(gshscene_list_item(list, i, &item) == GSHSCENE_OK);
    GSHSCENE_Result r = GSHSCENE_OK;
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
        r = gshscene_list_fill(twin, item->resource, &item->map, item->fill_rule, &item->paint, item->layer, item->depth);
        break;
      case GSHSCENE_ITEM_STROKE: {
        const GSHSCENE_Resource * centerline = item->resource;
        const GSHSCENE_Resource * made = NULL;
        if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_GLYPH_RUN) {
          GSHGEOM_Path * path = NULL;
          r = gshscene_glyph_run_centerline(item->resource, alloc, NULL, &path, NULL);
          if (r == GSHSCENE_OK) {
            r = gshscene_store_intern_path(store, path, &made);
            gshgeom_path_destroy(path);
            centerline = made;
          }
        }
        if (r == GSHSCENE_OK) {
          r = gshscene_list_stroke(twin, centerline, item->style, &item->map, &item->paint, item->layer, item->depth);
        }
        gshscene_resource_release(made);
        break;
      }
      case GSHSCENE_ITEM_CLIP_PUSH:
        r = gshscene_list_clip_push(twin, item->resource, &item->map, item->fill_rule, item->layer, item->depth);
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        r = gshscene_list_clip_pop(twin);
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        r = gshscene_list_group_push(twin, &item->group, item->layer, item->depth);
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        r = gshscene_list_group_pop(twin);
        break;
      default:
        r = GSHSCENE_ERR_UNSUPPORTED;
        break;
    }
    if (r != GSHSCENE_OK) {
      gshscene_list_destroy(twin);
      return NULL;
    }
  }
  if (gshscene_list_close(twin) != GSHSCENE_OK) {
    gshscene_list_destroy(twin);
    return NULL;
  }
  return twin;
}

/* --- the render and its checks -------------------------------------------------------------------------- */

static size_t raster_bytes(const GIMG_Raster * r) {
  return gimg_raster_stride_bytes(r) * gimg_raster_height(r);
}

static int same_bytes(const GIMG_Raster * a, const GIMG_Raster * b) {
  CHECK(raster_bytes(a) == raster_bytes(b));
  return memcmp(gimg_raster_pixels_const(a), gimg_raster_pixels_const(b), raster_bytes(a)) == 0;
}

/**
 * Whether every sample of @p a is within @p levels of 255 of @p b's: 8-bit samples (four to a pixel of @p bpp = 4) or
 * 32-bit floats (@p bpp = 16).  The two engines of the dual render differ by a doubled unit of area where the exact area is a
 * tie, which a coverage of 255 levels shows as at most one level, and a paint scales that by at most its own range.
 */
static int near_samples(const GIMG_Raster * a, const GIMG_Raster * b, size_t bpp, int levels) {
  CHECK(raster_bytes(a) == raster_bytes(b));
  const uint8_t * pa = (const uint8_t *)gimg_raster_pixels_const(a);
  const uint8_t * pb = (const uint8_t *)gimg_raster_pixels_const(b);
  const size_t stride = gimg_raster_stride_bytes(a);
  const size_t row = (size_t)gimg_raster_width(a) * bpp;
  for (uint32_t y = 0; y < gimg_raster_height(a); y++) {
    for (size_t i = 0; i < row; i += bpp == 16 ? 4 : 1) {
      if (bpp == 16) {
        float fa, fb;
        memcpy(&fa, pa + y * stride + i, sizeof fa);
        memcpy(&fb, pb + y * stride + i, sizeof fb);
        float d = fa > fb ? fa - fb : fb - fa;
        if (!(d <= (float)levels / 255.0f)) {
          return 0;
        }
      } else {
        int d = (int)pa[y * stride + i] - (int)pb[y * stride + i];
        if ((d < 0 ? -d : d) > levels) {
          return 0;
        }
      }
    }
  }
  return 1;
}

static GIMG_Raster * copy_of(const GIMG_Raster * src, const GIMG_Pixel_Format * format) {
  GIMG_Raster * out = NULL;
  if (gimg_raster_create(gimg_raster_width(src), gimg_raster_height(src), format, GIMG_RASTER_OWNED, NULL, 0, &out) !=
      GIMG_OK) {
    return NULL;
  }
  memcpy(gimg_raster_pixels(out), gimg_raster_pixels_const(src), raster_bytes(src));
  return out;
}

/** Nonzero if every byte of pixel (x, y) is equal in @p a and @p b. */
static int same_pixel(const GIMG_Raster * a, const GIMG_Raster * b, uint32_t x, uint32_t y, size_t bpp) {
  const size_t at = (size_t)y * gimg_raster_stride_bytes(a) + (size_t)x * bpp;
  return memcmp((const uint8_t *)gimg_raster_pixels_const(a) + at, (const uint8_t *)gimg_raster_pixels_const(b) + at, bpp) == 0;
}

/** The pixel format the sink writes, by its index (straight and premultiplied, 8-bit and float). */
static GIMG_Pixel_Format format_of(int index) {
  GIMG_Pixel_Format f;
  memset(&f, 0, sizeof f);
  f.channel_model = GIMG_CHANNEL_RGBA;
  f.layout = GIMG_LAYOUT_INTERLEAVED;
  f.channel_count = 4;
  const int wide = index >= 2;
  f.channel_type = wide ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  for (int i = 0; i < 4; i++) {
    f.bits_per_channel[i] = (uint8_t)(wide ? 32 : 8);
  }
  f.alpha_form = (uint8_t)(index & 1 ? GIMG_ALPHA_PREMULTIPLIED : GIMG_ALPHA_STRAIGHT);
  return f;
}

/** FNV-1a 64 of the report's entries (class, reason, chain, second): what was refused, approximated and tied. */
static uint64_t entries_hash(const GSHSCENE_Report * report) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e;
    CHECK(gshscene_report_entry(report, i, &e) == GSHSCENE_OK);
    uint64_t words[3] = {(uint64_t)e.entry_class, (uint64_t)e.reason, (uint64_t)e.second};
    for (size_t k = 0; k < 3 + e.chain_count; k++) {
      uint64_t w = k < 3 ? words[k] : (uint64_t)e.chain[k - 3];
      for (int b = 0; b < 8; b++) {
        h = (h ^ (uint8_t)(w >> (8 * b))) * 0x100000001b3ull;
      }
    }
    h = (h ^ 0xff) * 0x100000001b3ull; /* the entry's end */
  }
  return h;
}

/* UC19's budgets the harness draws: small ones that a pixel of a few overlapping pieces passes, and some that it does not. */
static uint64_t crossings_budget(unsigned index) {
  static const uint64_t table[12] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 64, 256, 1024};
  return table[index % 12];
}

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  Input in = {data, size, 0};
  const uint8_t target_byte = next(&in);
  const uint8_t request_byte = next(&in);
  const uint8_t cancel_byte = next(&in);
  const uint8_t limit_byte = next(&in);
  const uint8_t fail_byte = next(&in);

  /* The sink has a counter of its own, so that S9 (no allocation by the sink during a caller-memory render) can be
   * told from the report's and the store's own growth; the chosen call fails in one of the two. */
  Counter counter = {0, 0, (limit_byte & 128) && !(limit_byte & 64) ? (long)fail_byte : -1, 0};
  Counter sink_counter = {0, 0, (limit_byte & 128) && (limit_byte & 64) ? (long)fail_byte : -1, 0};
  const GSHGEOM_Allocator alloc = {&counter, c_malloc, c_calloc, c_realloc, c_free};
  const GSHGEOM_Allocator sink_alloc = {&sink_counter, c_malloc, c_calloc, c_realloc, c_free};
  GSHSCENE_Test_Provider tp;
  memset(&tp, 0, sizeof tp);
  tp.upem = 1000;
  tp.flags = GSHSCENE_FACE_HINTING;

  const GIMG_Pixel_Format pixel_format = format_of(target_byte & 3);
  const GIMG_Pixel_Format * format = &pixel_format;
  static const uint32_t dims[4] = {1, 7, 33, 64};
  const uint32_t width = dims[(target_byte >> 2) & 3], height = dims[(target_byte >> 4) & 3];

  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  switch (limit_byte & 7) {
    case 1:
      limits.max_points = 2 + (limit_byte >> 3) % 16;
      break;
    case 2:
      limits.max_deposits = 1 + (limit_byte >> 3) % 16;
      break;
    case 3:
      limits.max_dimension = 1 + (limit_byte >> 3) % 16;
      break;
    case 4:
      limits.max_bytes = 1 + (limit_byte >> 3) % 64;
      break;
    case 5: /* UC9: the pieces of a stroke part in one pixel */
      limits.max_exact_fragments = 2 + (limit_byte >> 3) % 24;
      break;
    case 6: /* UC19: the crossings of a stroke part in one pixel, a table from one to a thousand */
      limits.max_exact_crossings = crossings_budget(limit_byte >> 3);
      break;
    case 7: /* both at once: the pieces of a pixel and its crossings */
      limits.max_exact_fragments = 2 + (limit_byte >> 3) % 24;
      limits.max_exact_crossings = crossings_budget(cancel_byte >> 3);
      break;
    default:
      if (limit_byte >= 0x80) {
        /* IC9: the work of an image's AREA filter, a pixel's and an item's, from a few visits to a few thousand. */
        limits.max_area_pixel_work = 1 + (limit_byte >> 3) % 64 * 3;
        limits.max_image_work = 1 + (limit_byte >> 5) % 4 * 1500;
      }
      break;
  }
  if ((limit_byte & 7) == 0 && (limit_byte & 0x38) != 0 && limit_byte < 0x80) {
    limits.max_pattern_cells = 1 + (uint64_t)((limit_byte >> 3) & 7) * 6u; /* PC8: a few copies to a few dozen */
  }
  if ((limit_byte & 7) == 0 && limit_byte < 0x80 && (limit_byte & 0x40) != 0) {
    limits.max_hatch_lines = 1 + (uint64_t)((limit_byte >> 3) & 7) * 3u; /* HX12: a few lines to a couple of dozen */
  }
  const int tightened = (limit_byte & 7) >= 5; /* a limit of the exact step (UC9, UC19) is lowered */

  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * backdrop = NULL;
  GIMG_Raster * raster = NULL;
  GIMG_Raster * again = NULL;
  void * workspace = NULL;
  World world;
  memset(&world, 0, sizeof world);

  GSHCPU_Render_Counters counters;
  GSHCPU_Result cr = gshcpu_render_sink_create_tiered(&sink_alloc, NULL, &limits, GSHCPU_RENDER_TIERS_ALL, &counters, NULL,
      NULL, &sink);
  CHECK(cr == GSHCPU_OK || cr == GSHCPU_ERR_OOM);
  if (gshscene_store_create(&alloc, NULL, &store) != GSHSCENE_OK || !sink ||
      gshscene_report_create(&alloc, NULL, &report) != GSHSCENE_OK ||
      gshscene_list_create(&alloc, NULL, store, &list) != GSHSCENE_OK) {
    outcomes[OUT_OOM]++;
    goto done;
  }
  world.store = store;
  world.alloc = &alloc;
  world.masks_on = (request_byte & 0x80) != 0;
  world.patterns_on = 1;
  intern_paths(&in, store, &alloc, &world);
  intern_runs(&in, store, &tp, &world);
  if (record(&in, list, &world) >= 2) {
    outcomes[OUT_NESTED_GROUP]++;
  }

  /* A varied backdrop, so that an untouched pixel can be told from a written one. */
  if (gimg_raster_create(width, height, format, GIMG_RASTER_OWNED, NULL, 0, &backdrop) != GIMG_OK) {
    goto done;
  }
  {
    const size_t stride = gimg_raster_stride_bytes(backdrop);
    uint8_t * bytes = gimg_raster_pixels(backdrop);
    const int is_float = (target_byte & 3) >= 2;
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width * 4; x++) {
        if (is_float) {
          ((float *)(bytes + (size_t)y * stride))[x] = (float)((x * 7 + y * 13) % 17) / 16.0f;
        } else {
          bytes[(size_t)y * stride + x] = (uint8_t)(x * 37 + y * 101);
        }
      }
    }
    outcomes[OUT_FLOAT_TARGET] += is_float;
  }
  raster = copy_of(backdrop, format);
  if (!raster) {
    goto done;
  }

  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Rect damage = {0, 0, 0, 0};
  if (request_byte & 4) {
    target.extent.x0 = request_byte >> 5 & 1;
    target.extent.y0 = request_byte >> 6 & 1;
    target.extent.x1 = width;
    target.extent.y1 = height;
  }
  if (request_byte & 8) {
    damage.x0 = width / 4;
    damage.y0 = height / 3;
    damage.x1 = width / 4 + 1 + width / 2;
    damage.y1 = height / 3 + 1 + height / 3;
    target.damage = &damage;
  }

  /* An extent that starts at the raster's edge is empty, which the contract refuses (S4). */
  const int empty_extent = (request_byte & 4) && ((uint32_t)target.extent.x0 >= width || (uint32_t)target.extent.y0 >= height);

  GSHSCENE_Cancel cancel;
  gshscene_cancel_init(&cancel);
  Progress progress = {&cancel, cancel_byte % 8};
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  request.flags = (request_byte & 1 ? GSHSCENE_RENDER_STRICT : 0) | (request_byte & 2 ? GSHSCENE_RENDER_CALLER_MEMORY : 0);
  request.cancel = progress.at ? &cancel : NULL;
  request.progress = progress.at ? on_progress : NULL;
  request.progress_ctx = &progress;

  size_t need = 0;
  if (request.flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    GSHSCENE_Result q = gshscene_sink_workspace_bytes(sink, &request, &need);
    CHECK(q == GSHSCENE_OK || q == GSHSCENE_ERR_LIMIT || q == GSHSCENE_ERR_OOM || (q == GSHSCENE_ERR_INVALID && empty_extent));
    if (q != GSHSCENE_OK) {
      outcomes[OUT_OOM] += q == GSHSCENE_ERR_OOM;
      goto done;
    }
    /* The caller memory a request needs under the default limits can be gigabytes (a 480-byte input asked for 1.7 GB, over libFuzzer's
     * 2 GB resident limit, and rendering into it took half a minute): the exact-size and one-byte-short checks below are made on
     * the requests whose workspace is a bounded size, which the limits the header bytes choose give in most inputs. */
    if (need > FUZZ_WORKSPACE_MAX) {
      goto done;
    }
    /* A byte short first: refused, the target as it was. */
    if (need > 0 && (request_byte & 16)) {
      workspace = malloc(need);
      request.workspace = workspace;
      request.workspace_bytes = need - 1;
      CHECK(workspace != NULL);
      const long sink_calls = sink_counter.calls;
      const long calls = counter.calls;
      GSHSCENE_Result shortr = gshscene_sink_render(sink, &request);
      /* S9: a short workspace is a limit with nothing allocated, by the sink or the report, and no need stated. */
      CHECK(shortr == GSHSCENE_ERR_LIMIT);
      CHECK(sink_counter.calls == sink_calls && counter.calls == calls);
      GSHSCENE_Report_Summary short_summary;
      CHECK(gshscene_report_summary(report, &short_summary) == GSHSCENE_OK);
      CHECK(short_summary.workspace_needed == 0);
      CHECK(same_bytes(raster, backdrop));
      outcomes[OUT_WORKSPACE_SHORT]++;
      free(workspace);
      workspace = NULL;
    }
    workspace = malloc(need ? need : 1);
    request.workspace = workspace;
    request.workspace_bytes = need;
    CHECK(workspace != NULL);
  }

  const long calls_before = sink_counter.calls;
  GSHSCENE_Result r = gshscene_sink_render(sink, &request);
  if (r == GSHSCENE_ERR_INVALID) {
    CHECK(empty_extent); /* nothing begun: the report is not the render's */
    CHECK(same_bytes(raster, backdrop));
    goto done;
  }
  CHECK(!empty_extent);
  GSHSCENE_Report_Summary summary;
  CHECK(gshscene_report_summary(report, &summary) == GSHSCENE_OK);
  CHECK(summary.result == r);
  CHECK(summary.units_done <= summary.units_processed && summary.units_processed <= summary.units_total);
  CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_REFUSED || r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_CANCELLED ||
        r == GSHSCENE_ERR_OOM);
  CHECK(r != GSHSCENE_ERR_REFUSED || (request.flags & GSHSCENE_RENDER_STRICT));
  if (request.flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    CHECK(sink_counter.calls == calls_before); /* S9: the sink allocates nothing during the render */
  }
  outcomes[OUT_OOM] += r == GSHSCENE_ERR_OOM;
  outcomes[OUT_STRICT] += r == GSHSCENE_ERR_REFUSED;
  if (r == GSHSCENE_ERR_LIMIT) {
    outcomes[OUT_LIMIT_REGION]++;
    CHECK(same_bytes(raster, backdrop));
  }
  if (r == GSHSCENE_ERR_CANCELLED) {
    outcomes[OUT_CANCEL_MID] += progress.at > 0;
    CHECK(summary.units_processed == progress.at);
  }
  outcomes[OUT_REFUSED_WHILE_DRAWING] += summary.by_class[GSHSCENE_ENTRY_REFUSED] > 0 && r != GSHSCENE_ERR_OOM;
  outcomes[OUT_GRADIENT_DRAWN] += world.gradients > 0 && r == GSHSCENE_OK;
  outcomes[OUT_IMAGE_DRAWN] += world.images > 0 && r == GSHSCENE_OK;
  outcomes[OUT_IMAGE_AREA] += world.image_areas > 0 && r == GSHSCENE_OK;
  outcomes[OUT_IMAGE_TINT] += world.image_tints > 0 && r == GSHSCENE_OK;
  outcomes[OUT_IMAGE_STROKE] += world.image_strokes > 0 && r == GSHSCENE_OK;
  outcomes[OUT_COLOR_AT_BOUND] += world.at_bound > 0 && r == GSHSCENE_OK;
  outcomes[OUT_BLEND_DRAWN] += world.blends > 0 && r == GSHSCENE_OK;
  unsigned limit_refused = 0; /* items refused for a resource limit of the sink */
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e;
    CHECK(gshscene_report_entry(report, i, &e) == GSHSCENE_OK);
    limit_refused += e.reason == GSHSCENE_REASON_RESOURCE_LIMIT;
    /* KC1: this sink claims non-isolated and knockout groups, so neither reason is ever given. */
    CHECK(e.reason != GSHSCENE_REASON_GROUP_ISOLATION && e.reason != GSHSCENE_REASON_GROUP_KNOCKOUT);
    /* MC1: this sink claims the soft-mask flag and every mask feature, so neither reason is ever given. */
    CHECK(e.reason != GSHSCENE_REASON_SOFT_MASK && e.reason != GSHSCENE_REASON_MASK_FEATURE);
    if (e.reason == GSHSCENE_REASON_RESOURCE_LIMIT && e.chain_count >= 2) {
      /* MC6: an item inside a mask's picture refused for a limit has the chain [the group's push, the item, ...]. */
      const GSHSCENE_Item * top = NULL;
      if (gshscene_list_item(list, e.chain[0], &top) == GSHSCENE_OK && top->kind == GSHSCENE_ITEM_GROUP_PUSH && top->group.soft_mask) {
        outcomes[OUT_MASK_REFUSED_INSIDE]++;
      }
    }
    /* The sink claims the seven kinds it draws: nothing is refused for its kind, and a stroke is never refused for its
     * style (every feature is met by expansion, SC1). */
    CHECK(e.reason != GSHSCENE_REASON_ITEM_KIND && e.reason != GSHSCENE_REASON_STROKE_STYLE);
    /* This sink strokes runs (ST8): the plan never refuses a stroke for its centerline. */
    CHECK(e.reason != GSHSCENE_REASON_STROKE_CENTERLINE);
    if (world.images > 0 && e.chain_count > 0) {
      const GSHSCENE_Item * item = NULL;
      if (gshscene_list_item(list, e.chain[0], &item) == GSHSCENE_OK && (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
          item->paint.kind == GSHSCENE_PAINT_IMAGE) {
        outcomes[OUT_IMAGE_LIMIT] += e.reason == GSHSCENE_REASON_RESOURCE_LIMIT;
        outcomes[OUT_IMAGE_NOTICE] += e.entry_class == GSHSCENE_ENTRY_NOTICE;
      }
    }
    if (world.hatches > 0 && e.chain_count > 0) {
      const GSHSCENE_Item * item = NULL;
      if (gshscene_list_item(list, e.chain[0], &item) == GSHSCENE_OK && item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_HATCH) {
        outcomes[OUT_HATCH_NOTICE] += e.entry_class == GSHSCENE_ENTRY_NOTICE;
        outcomes[OUT_HATCH_LINES_LIMIT] += e.reason == GSHSCENE_REASON_RESOURCE_LIMIT && limits.max_hatch_lines < GSHCPU_LIMITS_DEFAULT_MAX_HATCH_LINES;
      }
    }
    if (e.entry_class == GSHSCENE_ENTRY_NOTICE && world.strokes > 0) {
      const GSHSCENE_Item * item = NULL;
      if (gshscene_list_item(list, e.chain[0], &item) == GSHSCENE_OK && item->kind == GSHSCENE_ITEM_STROKE) {
        outcomes[OUT_STROKE_NOTICE]++;
      }
    }
  }
  if (r == GSHSCENE_OK && (target_byte & 3) >= 2) {
    /* No NaN reaches a store (B9, N1): every float the render wrote is a number. */
    const size_t stride = gimg_raster_stride_bytes(raster);
    const uint8_t * bytes = gimg_raster_pixels_const(raster);
    for (uint32_t y = 0; y < height; y++) {
      const float * row = (const float *)(bytes + (size_t)y * stride);
      for (uint32_t x = 0; x < width * 4; x++) {
        CHECK(row[x] == row[x]);
      }
    }
    outcomes[OUT_BLEND_FLOAT] += world.blends > 0;
  }
  if ((target_byte & 3) >= 2) {
    /* N1: every value a float target holds after a render is finite (the backdrop is, and the saturation keeps it). */
    const size_t stride = gimg_raster_stride_bytes(raster);
    const uint8_t * bytes = gimg_raster_pixels_const(raster);
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width * 4; x++) {
        float v;
        memcpy(&v, bytes + (size_t)y * stride + (size_t)x * sizeof v, sizeof v);
        CHECK(v - v == 0.0f);
      }
    }
  }
  outcomes[OUT_GRADIENT_NOTICE] += summary.by_class[GSHSCENE_ENTRY_NOTICE] > 0;
  if (gcu_atomic_size_load(&tp.fast_calls) > 0) {
    outcomes[OUT_GLYPHS_FAST] += r == GSHSCENE_OK;
  }
  if (gcu_atomic_size_load(&tp.outline_calls) > 0) {
    outcomes[OUT_GLYPHS_GENERAL] += r == GSHSCENE_OK;
  }

  /* Outside the extent and the damage nothing is touched (a raster's pixels are 4 channels). */
  const size_t bpp = (target_byte & 3) >= 2 ? 16 : 4;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      int inside = 1;
      if (request_byte & 4) {
        inside &= (int64_t)x >= target.extent.x0 && (int64_t)y >= target.extent.y0;
      }
      if (request_byte & 8) {
        inside &= (int64_t)x >= damage.x0 && (int64_t)x < damage.x1 && (int64_t)y >= damage.y0 && (int64_t)y < damage.y1;
      }
      if (!inside) {
        CHECK(same_pixel(raster, backdrop, x, y, bpp));
      }
    }
  }

  outcomes[OUT_MASK_DRAWN] += r == GSHSCENE_OK && counters.masks > 0;
  outcomes[OUT_MASK_LUMINANCE] += r == GSHSCENE_OK && counters.masks > 0 && world.mask_luminance > 0;
  outcomes[OUT_MASK_NESTED] += r == GSHSCENE_OK && counters.masks > 0 && world.mask_nested > 0;
  outcomes[OUT_MASK_OUTSIDE] += counters.mask_outside > 0;
  outcomes[OUT_MASK_IDENTITY] += counters.mask_identity > 0;
  outcomes[OUT_MASK_CALLER_MEMORY] += r == GSHSCENE_OK && counters.masks > 0 && (request.flags & GSHSCENE_RENDER_CALLER_MEMORY);
  outcomes[OUT_GENERAL_DRAWN] += r == GSHSCENE_OK && counters.general_layers > 0;
  outcomes[OUT_NON_ISOLATED] += r == GSHSCENE_OK && counters.group_alpha_slots > 0;
  outcomes[OUT_KNOCKOUT] += r == GSHSCENE_OK && counters.general_layers > 0 && world.knockouts > 0;
  outcomes[OUT_GENERAL_MASKED] += r == GSHSCENE_OK && counters.general_pops > 0 && counters.masks > 0;
  outcomes[OUT_GENERAL_SHAPE] += r == GSHSCENE_OK && counters.group_shape_slots > 0;
  outcomes[OUT_GENERAL_NESTED] += r == GSHSCENE_OK && (counters.group_alpha_slots > 1 || counters.group_shape_slots > 1);
  outcomes[OUT_GENERAL_CALLER_MEMORY] += r == GSHSCENE_OK && counters.general_layers > 0 && (request.flags & GSHSCENE_RENDER_CALLER_MEMORY);
  outcomes[OUT_GENERAL_STROKE] += r == GSHSCENE_OK && counters.general_fills > 0 && counters.strokes > 0;
  outcomes[OUT_STROKE_DRAWN] += r == GSHSCENE_OK && counters.strokes > 0;
  outcomes[OUT_STROKE_RUN_DRAWN] += r == GSHSCENE_OK && counters.strokes > 0 && world.run_strokes > 0;
  outcomes[OUT_STROKE_ALIGNED] += r == GSHSCENE_OK && counters.strokes > 0 && world.aligned > 0;
  outcomes[OUT_STROKE_SPARSE] += counters.sparse_hairline + counters.sparse_plain > 0;
  outcomes[OUT_STROKE_EXACT] += counters.exact_cells > 0;
  outcomes[OUT_BUDGET_REFUSED] += (limit_byte & 7) >= 6 && limit_refused > 0;
  outcomes[OUT_PATTERN_DRAWN] += r == GSHSCENE_OK && counters.patterns > 0;
  outcomes[OUT_PATTERN_OVERLAP] += r == GSHSCENE_OK && counters.patterns > 0 && world.pattern_overlaps > 0;
  outcomes[OUT_PATTERN_PERIOD] += r == GSHSCENE_OK && counters.pattern_period > 0;
  outcomes[OUT_PATTERN_GAVE_UP] += counters.period_aborts > 0;
  outcomes[OUT_PATTERN_NESTED] += r == GSHSCENE_OK && counters.patterns > 1 && world.pattern_nested > 0;
  outcomes[OUT_PATTERN_STROKE] += r == GSHSCENE_OK && counters.patterns > 0 && world.pattern_strokes > 0;
  outcomes[OUT_PATTERN_LIMIT] += world.patterns > 0 && (limit_byte & 7) == 0 && limit_byte < 0x80 && (limit_byte & 0x38) != 0 && limit_refused > 0;
  outcomes[OUT_PATTERN_CALLER_MEMORY] += r == GSHSCENE_OK && counters.patterns > 0 && (request.flags & GSHSCENE_RENDER_CALLER_MEMORY);
  outcomes[OUT_HATCH_DRAWN] += r == GSHSCENE_OK && counters.hatches > 0;
  outcomes[OUT_HATCH_DASHED] += r == GSHSCENE_OK && counters.hatches > 0 && world.hatch_dashed > 0;
  outcomes[OUT_HATCH_NESTING] += r == GSHSCENE_OK && counters.hatches > 0 && world.hatch_nested > 0;
  outcomes[OUT_HATCH_CACHED] += counters.hatches > 0 && counters.cache_hits > 0;
  outcomes[OUT_HATCH_CALLER_MEMORY] += r == GSHSCENE_OK && counters.hatches > 0 && (request.flags & GSHSCENE_RENDER_CALLER_MEMORY);
  /* UC4: the pieces of every cell the exact step made reproduce what the rasterizer summed, whatever the geometry. */
  CHECK(counters.exact_failures == 0);
  const uint64_t first_entries = entries_hash(report);
  if (r == GSHSCENE_OK) {
    outcomes[OUT_OK]++;
    outcomes[OUT_CALLER_MEMORY] += (request.flags & GSHSCENE_RENDER_CALLER_MEMORY) != 0;
    /* The same request again, into a fresh copy of the backdrop, without the token: the same bytes (S12). */
    again = copy_of(backdrop, format);
    if (again) {
      target.surface = again;
      request.cancel = NULL;
      request.progress = NULL;
      request.flags &= ~GSHSCENE_RENDER_CALLER_MEMORY;
      request.workspace = NULL;
      request.workspace_bytes = 0;
      GSHSCENE_Result r2 = gshscene_sink_render(sink, &request);
      CHECK(r2 == GSHSCENE_OK || r2 == GSHSCENE_ERR_OOM);
      if (r2 == GSHSCENE_OK) {
#ifdef GSHCPU_FUZZ_PLANT
        ((uint8_t *)gimg_raster_pixels(again))[0] ^= 1; /* the replay gate's plant: the check below must see it */
#endif
        CHECK(same_bytes(raster, again));
        outcomes[OUT_STROKE_CACHED] += counters.cache_hits > 0;
        /* UC9 and UC19: a render that refused no item for a resource limit is the render the default limits make, whatever
         * the piece cap and the crossings budget were lowered to (a limit refuses; it never changes what it lets through). */
        if (tightened && counters.strokes > 0 && limit_refused == 0) {
          GSHCPU_Limits roomy;
          gshcpu_limits_default(&roomy);
          GSHSCENE_Sink * wide = NULL;
          GIMG_Raster * wide_out = copy_of(backdrop, format);
          if (wide_out && gshcpu_render_sink_create_tiered(NULL, NULL, &roomy, GSHCPU_RENDER_TIERS_ALL, NULL, NULL, NULL, &wide) == GSHCPU_OK) {
            target.surface = wide_out;
            GSHSCENE_Result r6 = gshscene_sink_render(wide, &request);
            CHECK(r6 == GSHSCENE_OK || r6 == GSHSCENE_ERR_OOM);
            if (r6 == GSHSCENE_OK) {
              CHECK(same_bytes(raster, wide_out));
              outcomes[OUT_BUDGET_SAME] += (limit_byte & 7) >= 6;
            }
            target.surface = again;
          }
          gshscene_sink_destroy(wide);
          gimg_raster_destroy(wide_out);
        }
        /* The general tier is the oracle of the fast ones (SC8): a second sink with them off (the exact step stays: it is the
         * coverage, not a tier) gives the same bytes. */
        if (counters.strokes > 0) {
          GSHSCENE_Sink * general = NULL;
          GIMG_Raster * other = copy_of(backdrop, format);
          if (other && gshcpu_render_sink_create_tiered(NULL, NULL, &limits, GSHCPU_RENDER_TIERS_GENERAL, NULL, NULL, NULL, &general) == GSHCPU_OK) {
            target.surface = other;
            GSHSCENE_Result r3 = gshscene_sink_render(general, &request);
            CHECK(r3 == GSHSCENE_OK || r3 == GSHSCENE_ERR_OOM);
            if (r3 == GSHSCENE_OK) {
              CHECK(same_bytes(raster, other));
            }
            target.surface = again;
          }
          gshscene_sink_destroy(general);
          gimg_raster_destroy(other);
          /* The dual render (strokes story S4c, C4): the earlier step, kept in the tests, as a second engine.  Where it renders
           * the list it draws the same bytes; it is exact to a bound that grows with the pieces of a cell, so a cell it is not
           * (see the call's comment) is left to the exact references of the unit tests. */
          GSHCPU_Limits lim = limits;
          GSHSCENE_Sink * dual = NULL;
          GIMG_Raster * twin_out = copy_of(backdrop, format);
          if (twin_out && gshcpu_render_sink_create_tiered(NULL, NULL, &lim, GSHCPU_RENDER_TIERS_ALL, NULL, NULL, NULL, &dual) == GSHCPU_OK) {
            target.surface = twin_out;
            gshcpu_exact_hook = gshcpu_s4b_exact_cell;
            GSHSCENE_Result r5 = gshscene_sink_render(dual, &request);
            gshcpu_exact_hook = NULL;
            CHECK(r5 == GSHSCENE_OK || r5 == GSHSCENE_ERR_OOM || r5 == GSHSCENE_ERR_LIMIT);
            /* Where it is exact enough: no pixel holds more pieces of a part than it was written for and no winding is past its
             * 255 (the library's own cells are then the exact area, the earlier step's within a unit of it). */
            /* The earlier step has no crossings budget (the hook replaces the step that counts them), so where the budget
             * refused an item it drew what the render left out; the engines are compared where nothing was refused. */
            if (r5 == GSHSCENE_OK && counters.exact_pieces_max <= 512 && counters.exact_winding_max <= 255 &&
                ((limit_byte & 7) < 6 || limit_refused == 0)) {
              CHECK(near_samples(raster, twin_out, bpp, 2));
              if (getenv("GSHCPU_FUZZ_STRICT_DUAL")) {
                CHECK(same_bytes(raster, twin_out)); /* the committed seeds draw the same bytes with either engine */
              }
              outcomes[OUT_STROKE_DUAL]++;
            }
            target.surface = again;
          }
          gshscene_sink_destroy(dual);
          gimg_raster_destroy(twin_out);
        }
        /* PC9, PC10: a render that drew a pattern is the render with the period tier off (and the cache: a second, third render of the
         * same sink was a hit), bit for bit, with the same report. */
        outcomes[OUT_PATTERN_CACHED] += counters.period_hits > 0;
        if (counters.patterns > 0) {
          GSHSCENE_Sink * general = NULL;
          GIMG_Raster * other = copy_of(backdrop, format);
          GSHCPU_Render_Counters general_counters;
          if (other && gshcpu_render_sink_create_tiered(NULL, NULL, &limits,
                  GSHCPU_RENDER_TIERS_ALL & ~(unsigned)(GSHCPU_RENDER_TIER_PERIOD | GSHCPU_RENDER_TIER_CACHE), &general_counters, NULL, NULL,
                  &general) == GSHCPU_OK) {
            target.surface = other;
            GSHSCENE_Result rp = gshscene_sink_render(general, &request);
            CHECK(rp == GSHSCENE_OK || rp == GSHSCENE_ERR_OOM);
            if (rp == GSHSCENE_OK) {
              CHECK(same_bytes(raster, other));
              CHECK(entries_hash(report) == first_entries);
              CHECK(general_counters.pattern_period == 0 && general_counters.period_hits == 0);
              outcomes[OUT_PATTERN_TIERS_EQUAL]++;
            }
            target.surface = again;
          }
          gshscene_sink_destroy(general);
          gimg_raster_destroy(other);
        }
        /* MC8: a render that composited a masked group is the render with both mask tiers off, bit for bit. */
        if (counters.masks > 0) {
          GSHSCENE_Sink * general = NULL;
          GIMG_Raster * other = copy_of(backdrop, format);
          GSHCPU_Render_Counters general_counters;
          if (other && gshcpu_render_sink_create_tiered(NULL, NULL, &limits,
                  GSHCPU_RENDER_TIERS_ALL & ~(unsigned)(GSHCPU_RENDER_TIER_MASK_OUTSIDE | GSHCPU_RENDER_TIER_MASK_IDENTITY),
                  &general_counters, NULL, NULL, &general) == GSHCPU_OK) {
            target.surface = other;
            GSHSCENE_Result rm = gshscene_sink_render(general, &request);
            CHECK(rm == GSHSCENE_OK || rm == GSHSCENE_ERR_OOM);
            if (rm == GSHSCENE_OK) {
              CHECK(same_bytes(raster, other));
              CHECK(general_counters.mask_outside == 0 && general_counters.mask_identity == 0);
              outcomes[OUT_MASK_TIERS_EQUAL]++;
            }
            target.surface = again;
          }
          gshscene_sink_destroy(general);
          gimg_raster_destroy(other);
        }
        /* ST11: the list with every stroked run replaced by the stroke of its path draws the same bytes. */
        if (world.run_strokes > 0 && counters.strokes > 0 && !(request_byte & 8)) {
          GSHSCENE_List * twin = twin_of(list, store, &alloc);
          GIMG_Raster * other = twin ? copy_of(backdrop, format) : NULL;
          if (other) {
            GSHSCENE_Render_Request tr = request;
            tr.list = twin;
            target.surface = other;
            GSHSCENE_Result r4 = gshscene_sink_render(sink, &tr);
            CHECK(r4 == GSHSCENE_OK || r4 == GSHSCENE_ERR_OOM || r4 == GSHSCENE_ERR_LIMIT);
            if (r4 == GSHSCENE_OK) {
              CHECK(same_bytes(again, other));
              outcomes[OUT_STROKE_RUN_TWIN]++;
            }
            target.surface = again;
          }
          gimg_raster_destroy(other);
          gshscene_list_destroy(twin);
        }
        /* Inside the damage the damage-limited render is the whole render. */
        if (request_byte & 8) {
          memcpy(gimg_raster_pixels(again), gimg_raster_pixels_const(backdrop), raster_bytes(backdrop));
          target.damage = NULL;
          r2 = gshscene_sink_render(sink, &request);
          /* The whole region can be past a region limit (dimension, bytes) the damage's was within.  A per-item limit
           * (cells, points) is met over the whole render's region whatever the damage (S4), so the whole render
           * refuses the items the damage-limited one did, and is not refused where it was not. */
          CHECK(r2 == GSHSCENE_OK || r2 == GSHSCENE_ERR_OOM || (r2 == GSHSCENE_ERR_LIMIT && (limit_byte & 7) >= 3));
          if (r2 == GSHSCENE_OK) {
            CHECK(entries_hash(report) == first_entries);
            for (int64_t y = damage.y0; y < damage.y1 && y < (int64_t)height; y++) {
              for (int64_t x = damage.x0; x < damage.x1 && x < (int64_t)width; x++) {
                CHECK(same_pixel(raster, again, (uint32_t)x, (uint32_t)y, bpp));
              }
            }
            outcomes[OUT_DAMAGE]++;
            outcomes[OUT_PATTERN_DAMAGE] += counters.patterns > 0;
            outcomes[OUT_MASK_DAMAGE] += counters.masks > 0;
            outcomes[OUT_GENERAL_DAMAGE] += counters.general_layers > 0;
          }
        }
      }
    }
  }

done:
  free(workspace);
  gimg_raster_destroy(again);
  gimg_raster_destroy(raster);
  gimg_raster_destroy(backdrop);
  gshscene_list_destroy(list);
  for (size_t i = 0; i < world.path_count; i++) {
    gshscene_resource_release(world.paths[i]);
  }
  gshscene_resource_release(world.runs[0]);
  gshscene_resource_release(world.runs[1]);
  gshscene_resource_release(world.style);
  gshscene_resource_release(world.tile_style);
  gshscene_report_destroy(report);
  if (sink) {
    gshscene_sink_destroy(sink);
  }
  gshscene_store_destroy(store);
  CHECK(counter.live == 0 && sink_counter.live == 0);
  return 0;
}
