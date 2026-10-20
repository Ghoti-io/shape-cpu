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
 * A game's frame loop that allocates nothing per frame.
 *
 * Shapes are interned once, before the loop.  Each frame records a transient
 * list in immediate mode (no IDs, no versions), resets it for the next frame
 * (which keeps its memory), and renders it with `GSHSCENE_RENDER_CALLER_MEMORY`
 * into a workspace the game sized once with `gshscene_sink_workspace_bytes`.
 * Every library object is given one counting allocator, and from the second
 * frame on it must not be called at all.  The last frame is checked against
 * the same list rendered the default way: caller memory changes where the
 * scratch lives, never the pixels.
 *
 *     $ ./frames
 *     frame 1: ... allocator calls (setup), a workspace of ... bytes
 *     frames 2..60: 0 allocator calls
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 256
#define HEIGHT 144
#define FRAMES 60

/* --- an allocator that counts its calls: the game's own, or a probe like this one --------------------------- */

static unsigned long g_calls = 0;
static void * count_malloc(void * ctx, size_t n) {
  (void)ctx;
  g_calls++;
  return malloc(n ? n : 1);
}
static void * count_calloc(void * ctx, size_t c, size_t n) {
  (void)ctx;
  g_calls++;
  return calloc(c ? c : 1, n ? n : 1);
}
static void * count_realloc(void * ctx, void * p, size_t n) {
  (void)ctx;
  g_calls++;
  return realloc(p, n ? n : 1);
}
static void count_free(void * ctx, void * p) {
  (void)ctx;
  if (p) {
    g_calls++;
  }
  free(p);
}
static const GSHCPU_Allocator counting = {NULL, count_malloc, count_calloc, count_realloc, count_free};

/** A circle of radius 1 about the origin: four exact quarter arcs. */
static const GSHSCENE_Resource * unit_circle(GSHSCENE_Store * store) {
  static const double w[3] = {1.0, 0.70710678118654752440, 1.0};
  GSHGEOM_Path * path = NULL;
  const GSHSCENE_Resource * r = NULL;
  if (gshgeom_path_create(&counting, NULL, 2, &path) == GSHGEOM_OK) {
    gshgeom_path_move_to(path, (GSHGEOM_Point){1, 0, 0});
    gshgeom_path_rational_quadratic_to(path, (GSHGEOM_Point){1, 1, 0}, (GSHGEOM_Point){0, 1, 0}, w);
    gshgeom_path_rational_quadratic_to(path, (GSHGEOM_Point){-1, 1, 0}, (GSHGEOM_Point){-1, 0, 0}, w);
    gshgeom_path_rational_quadratic_to(path, (GSHGEOM_Point){-1, -1, 0}, (GSHGEOM_Point){0, -1, 0}, w);
    gshgeom_path_rational_quadratic_to(path, (GSHGEOM_Point){1, -1, 0}, (GSHGEOM_Point){1, 0, 0}, w);
    gshgeom_path_close(path);
    gshscene_store_intern_path(store, path, &r);
  }
  gshgeom_path_destroy(path);
  return r;
}

static GSHSCENE_Paint solid(double r, double g, double b, double a) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_SOLID;
  p.rgba[0] = r;
  p.rgba[1] = g;
  p.rgba[2] = b;
  p.rgba[3] = a;
  return p;
}

/** Record frame @p t: a background, a few balls moving across, and a half-transparent HUD bar in a group. */
static int record(GSHSCENE_List * list, const GSHSCENE_Resource * circle, int t) {
  GSHSCENE_Paint sky = solid(0.08, 0.1, 0.18, 1.0), ball = solid(0.95, 0.6, 0.1, 1.0), hud = solid(1.0, 1.0, 1.0, 1.0);
  GSHGEOM_Map at;
  int failed = 0;
  gshgeom_map_scale(2, 400.0, 400.0, 1.0, &at); /* a circle much larger than the screen: the background */
  failed |= gshscene_list_fill(list, circle, &at, GSHGEOM_FILL_NONZERO, &sky, 0, 0.0) != GSHSCENE_OK;
  for (int i = 0; i < 5; i++) {
    double x = (double)((t * (3 + i) + 40 * i) % WIDTH), y = 30.0 + 20.0 * i;
    GSHGEOM_Map scale, move;
    gshgeom_map_scale(2, 8.0, 8.0, 1.0, &scale);
    gshgeom_map_translate(2, x, y, 0.0, &move);
    gshgeom_map_compose(&move, &scale, &at);
    failed |= gshscene_list_fill(list, circle, &at, GSHGEOM_FILL_NONZERO, &ball, 0, 0.0) != GSHSCENE_OK;
  }
  GSHSCENE_Group group;
  memset(&group, 0, sizeof group);
  group.opacity = 0.4;
  failed |= gshscene_list_group_push(list, &group, 0, 0.0) != GSHSCENE_OK;
  GSHGEOM_Map bar;
  gshgeom_map_from_matrix(2, (const double[]){60.0, 0.0, 0.0, 6.0}, (const double[]){WIDTH / 2.0, HEIGHT - 10.0}, &bar);
  failed |= gshscene_list_fill(list, circle, &bar, GSHGEOM_FILL_NONZERO, &hud, 0, 0.0) != GSHSCENE_OK;
  failed |= gshscene_list_group_pop(list) != GSHSCENE_OK;
  failed |= gshscene_list_close(list) != GSHSCENE_OK;
  return failed;
}

int main(void) {
  int failed = 0;
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * screen = NULL;
  GIMG_Raster * check_raster = NULL;
  void * workspace = NULL;

  /* Setup, once: everything that allocates. */
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  if (gshscene_store_create(&counting, NULL, &store) != GSHSCENE_OK ||
      gshscene_list_create(&counting, NULL, store, &list) != GSHSCENE_OK ||
      gshcpu_sink_create(&counting, NULL, NULL, &sink) != GSHCPU_OK ||
      gshscene_report_create(&counting, NULL, &report) != GSHSCENE_OK ||
      gimg_raster_create(WIDTH, HEIGHT, &format, GIMG_RASTER_OWNED, NULL, 0, &screen) != GIMG_OK ||
      gimg_raster_create(WIDTH, HEIGHT, &format, GIMG_RASTER_OWNED, NULL, 0, &check_raster) != GIMG_OK) {
    fprintf(stderr, "frames: setup failed\n");
    return 1;
  }
  const GSHSCENE_Resource * circle = unit_circle(store);
  if (!circle) {
    fprintf(stderr, "frames: circle failed\n");
    return 1;
  }

  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = screen;
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  request.flags = GSHSCENE_RENDER_CALLER_MEMORY;

  unsigned long after_first = 0;
  size_t need = 0;
  for (int t = 0; t < FRAMES && !failed; t++) {
    unsigned long before = g_calls;
    failed |= gshscene_list_reset(list) != GSHSCENE_OK || record(list, circle, t);
    if (!workspace) {
      /* The exact need, asked once: the frames all draw the same shapes over the same region. */
      failed |= gshscene_sink_workspace_bytes(sink, &request, &need) != GSHSCENE_OK;
      workspace = malloc(need);
      request.workspace = workspace;
      request.workspace_bytes = need;
      failed |= workspace == NULL;
    }
    GSHSCENE_Result r = failed ? GSHSCENE_ERR_INVALID : gshscene_sink_render(sink, &request);
    if (r != GSHSCENE_OK) {
      fprintf(stderr, "frames: frame %d: %s\n", t + 1, gshscene_result_string(r));
      failed = 1;
    }
    if (t == 0) {
      printf("frame 1: %lu allocator calls (setup), a workspace of %zu bytes\n", g_calls - before, need);
    } else {
      after_first += g_calls - before;
    }
  }
  printf("frames 2..%d: %lu allocator calls\n", FRAMES, after_first);
  failed |= after_first != 0;

  /* The last frame again, the default way (the sink's own memory), into a second raster: the same pixels. */
  GSHSCENE_Rect all = {0, 0, WIDTH, HEIGHT};
  uint64_t h_frame = 0, h_default = 0;
  target.surface = check_raster;
  request.flags = 0;
  request.workspace = NULL;
  request.workspace_bytes = 0;
  failed |= gshscene_sink_render(sink, &request) != GSHSCENE_OK;
  failed |= gshcpu_render_hash(screen, &all, &h_frame) != GSHCPU_OK || gshcpu_render_hash(check_raster, &all, &h_default) != GSHCPU_OK;
  printf("last frame %016" PRIx64 ", rendered the default way %016" PRIx64 "\n", h_frame, h_default);
  failed |= h_frame != h_default;

  free(workspace);
  gshscene_resource_release(circle);
  gimg_raster_destroy(check_raster);
  gimg_raster_destroy(screen);
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gshscene_list_destroy(list);
  gshscene_store_destroy(store);
  if (failed) {
    fprintf(stderr, "frames: FAILED\n");
  }
  return failed;
}
