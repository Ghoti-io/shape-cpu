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
 * balanced and closed.
 *
 * After the render the sink's stated properties are checked, and a violation is
 * an `abort`:
 *
 * - the result is one the contract allows for the request, and the report's
 *   summary says the same;
 * - a successful render is deterministic: the same request again into a fresh
 *   copy of the backdrop gives the same bytes; with caller memory, the exact
 *   workspace gives the bytes of the default render, and a byte less is
 *   `ERR_LIMIT`;
 * - pixels outside the extent and the damage are never touched, and inside the
 *   damage the render equals the whole render;
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  OUT_COUNT
};

static const char * const outcome_names[OUT_COUNT] = {"a render", "a refusal while drawing (a sink's own limit)",
    "strict, refused", "a region past the sink's limits", "cancelled between units", "a failed allocation",
    "a render in caller memory", "a workspace a byte short", "a damage-limited render equal to the whole",
    "glyphs on the fast tier", "glyphs on the general route", "a float target", "a group inside a group"};

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
  const GSHSCENE_Resource * paths[MAX_PATHS];
  size_t path_count;
  const GSHSCENE_Resource * runs[2]; /* from a face with the fast tier, and from one without */
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
      GSHGEOM_Point a = {coordinate(in), coordinate(in), 0.0};
      GSHGEOM_Point b = {coordinate(in), coordinate(in), 0.0};
      GSHGEOM_Point c = {coordinate(in), coordinate(in), 0.0};
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
  switch (next(in) % 5) {
    case 0:
      gshgeom_map_identity(2, map);
      break;
    case 1:
      gshgeom_map_translate(2, coordinate(in), coordinate(in), 0.0, map);
      break;
    case 2:
      gshgeom_map_scale(2, (double)(next(in) % 17) / 4.0, (double)(next(in) % 17) / 4.0, 1.0, &a);
      gshgeom_map_translate(2, coordinate(in), coordinate(in), 0.0, &b);
      gshgeom_map_compose(&b, &a, map);
      break;
    case 3:
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, (double)next(in) * 1.5, &a);
      gshgeom_map_translate(2, 32.0, 32.0, 0.0, &b);
      gshgeom_map_compose(&b, &a, map);
      break;
    default:
      gshgeom_map_design_to_model((double)(1 + next(in) % 4) / 64.0, coordinate(in), coordinate(in), map);
      break;
  }
}

static void paint_of(Input * in, GSHSCENE_Paint * paint) {
  memset(paint, 0, sizeof *paint);
  paint->kind = GSHSCENE_PAINT_SOLID;
  for (int i = 0; i < 4; i++) {
    paint->rgba[i] = (double)next(in) / 255.0;
  }
  if (next(in) % 4 == 0) {
    paint->space.primaries = GCOL_PRIMARIES_DISPLAY_P3;
    paint->space.transfer = GCOL_TRANSFER_SRGB;
  }
}

/** Record the list: fills, clips and groups, closed balanced.  @return the deepest group nesting reached. */
static int record(Input * in, GSHSCENE_List * list, const World * w) {
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
      paint_of(in, &paint);
      gshscene_list_fill(list, r, &map, next(in) & 1 ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
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
      if (next(in) % 3 == 0) {
        group.blend_space.primaries = GCOL_PRIMARIES_SRGB;
        group.blend_space.transfer = GCOL_TRANSFER_LINEAR;
      }
      if (gshscene_list_group_push(list, &group, 0, 0.0) == GSHSCENE_OK) {
        open[depth++] = 1;
        groups++;
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

/* --- the render and its checks -------------------------------------------------------------------------- */

static size_t raster_bytes(const GIMG_Raster * r) {
  return gimg_raster_stride_bytes(r) * gimg_raster_height(r);
}

static int same_bytes(const GIMG_Raster * a, const GIMG_Raster * b) {
  CHECK(raster_bytes(a) == raster_bytes(b));
  return memcmp(gimg_raster_pixels_const(a), gimg_raster_pixels_const(b), raster_bytes(a)) == 0;
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
      limits.max_cells = 1 + (limit_byte >> 3) % 16;
      break;
    case 3:
      limits.max_dimension = 1 + (limit_byte >> 3) % 16;
      break;
    case 4:
      limits.max_bytes = 1 + (limit_byte >> 3) % 64;
      break;
    default:
      break;
  }

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

  GSHCPU_Result cr = gshcpu_sink_create(&sink_alloc, NULL, &limits, &sink);
  CHECK(cr == GSHCPU_OK || cr == GSHCPU_ERR_OOM);
  if (gshscene_store_create(&alloc, NULL, &store) != GSHSCENE_OK || !sink ||
      gshscene_report_create(&alloc, NULL, &report) != GSHSCENE_OK ||
      gshscene_list_create(&alloc, NULL, store, &list) != GSHSCENE_OK) {
    outcomes[OUT_OOM]++;
    goto done;
  }
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
    /* A byte short first: refused, the target as it was. */
    if (need > 0 && (request_byte & 16)) {
      workspace = malloc(need);
      request.workspace = workspace;
      request.workspace_bytes = need - 1;
      CHECK(workspace != NULL);
      GSHSCENE_Result shortr = gshscene_sink_render(sink, &request);
      /* The sink sizes the need of a short workspace over its own allocator (render/sink.c, need_of), so a failure
       * planted there is ERR_OOM rather than ERR_LIMIT. */
      CHECK(shortr == GSHSCENE_ERR_LIMIT || (shortr == GSHSCENE_ERR_OOM && sink_counter.failed));
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
        /* Inside the damage the damage-limited render is the whole render. */
        if (request_byte & 8) {
          memcpy(gimg_raster_pixels(again), gimg_raster_pixels_const(backdrop), raster_bytes(backdrop));
          target.damage = NULL;
          r2 = gshscene_sink_render(sink, &request);
          /* The whole region can be past a region limit the damage's was within, and under strict an item can meet
           * a per-item limit (cells, points) outside the damage that it never reached inside it. */
          CHECK(r2 == GSHSCENE_OK || r2 == GSHSCENE_ERR_OOM || (r2 == GSHSCENE_ERR_LIMIT && (limit_byte & 7) >= 3) ||
                (r2 == GSHSCENE_ERR_REFUSED && (request.flags & GSHSCENE_RENDER_STRICT)));
          if (r2 == GSHSCENE_OK) {
            for (int64_t y = damage.y0; y < damage.y1 && y < (int64_t)height; y++) {
              for (int64_t x = damage.x0; x < damage.x1 && x < (int64_t)width; x++) {
                CHECK(same_pixel(raster, again, (uint32_t)x, (uint32_t)y, bpp));
              }
            }
            outcomes[OUT_DAMAGE]++;
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
  gshscene_report_destroy(report);
  if (sink) {
    gshscene_sink_destroy(sink);
  }
  gshscene_store_destroy(store);
  CHECK(counter.live == 0 && sink_counter.live == 0);
  return 0;
}
