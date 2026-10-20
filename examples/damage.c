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
 * Redraw only what changed: a retained scene (a panel with a slider), a driver
 * that moves the slider's knob in an edit, the diff of the two versions, and a
 * render limited to the diff's damage rectangle.
 *
 * The application keeps one raster on screen.  After the edit it renders the
 * new version into that raster with `target.damage` set, so the sink composites
 * only the damaged pixels and leaves the rest as the last frame drew them.  The
 * example then renders the new version whole into a second raster and checks
 * the two are equal, pixel for pixel: a damage-limited render is the whole
 * render inside the damage, and touches nothing outside it.
 *
 *     $ ./damage
 *     damage: x 116..157, y 36..67 (1271 of 76800 pixels, 1.65 %)
 *     ...
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 320
#define HEIGHT 240

static int check(int failed, const char * what) {
  if (failed) {
    fprintf(stderr, "damage: %s failed\n", what);
  }
  return failed;
}

/** Intern a rectangle as a path: interning is by content, so a second equal rectangle is the same handle. */
static const GSHSCENE_Resource * rect(GSHSCENE_Store * store, double w, double h) {
  GSHGEOM_Path * path = NULL;
  const GSHSCENE_Resource * r = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &path) == GSHGEOM_OK) {
    gshgeom_path_move_to(path, (GSHGEOM_Point){0, 0, 0});
    gshgeom_path_line_to(path, (GSHGEOM_Point){w, 0, 0});
    gshgeom_path_line_to(path, (GSHGEOM_Point){w, h, 0});
    gshgeom_path_line_to(path, (GSHGEOM_Point){0, h, 0});
    gshgeom_path_close(path);
    gshscene_store_intern_path(store, path, &r);
  }
  gshgeom_path_destroy(path);
  return r;
}

/** A shape node: a resource at (x, y) in a solid color. */
static GSHSCENE_Node shape(const GSHSCENE_Resource * resource, double x, double y, double r, double g, double b) {
  GSHSCENE_Node node;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &node);
  node.resource = resource;
  gshgeom_map_translate(2, x, y, 0.0, &node.map);
  node.paint.kind = GSHSCENE_PAINT_SOLID;
  node.paint.rgba[0] = r;
  node.paint.rgba[1] = g;
  node.paint.rgba[2] = b;
  node.paint.rgba[3] = 1.0;
  return node;
}

/** Render a version's list into a raster: the whole raster, or only @p damage. */
static GSHSCENE_Result render(GSHSCENE_Sink * sink, const GSHSCENE_Version * version, GIMG_Raster * raster,
    const GSHSCENE_Rect * damage, GSHSCENE_Report * report) {
  const GSHSCENE_List * list = NULL;
  GSHSCENE_Result r = gshscene_version_list(version, &list);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  target.damage = damage;
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  return gshscene_sink_render(sink, &request);
}

/* --- the driver: given an edit, it moves the knob to a value in [0, 1], and publishes nothing --------------- */

static int drive_slider(GSHSCENE_Edit * edit, uint64_t knob, double value) {
  GSHSCENE_Node_Info info;
  if (gshscene_edit_node(edit, knob, &info) != GSHSCENE_OK) {
    return 1;
  }
  gshgeom_map_translate(2, 40.0 + 220.0 * value, 40.0, 0.0, &info.node.map);
  return gshscene_edit_set(edit, knob, &info.node) != GSHSCENE_OK;
}

int main(void) {
  int failed = 0;
  GSHSCENE_Scene * scene = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * screen = NULL;
  GIMG_Raster * whole = NULL;
  GSHSCENE_Edit * edit = NULL;
  const GSHSCENE_Version * v1 = NULL;
  const GSHSCENE_Version * v2 = NULL;
  GSHSCENE_Diff * diff = NULL;

  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  if (gshscene_scene_create(NULL, NULL, NULL, &scene) != GSHSCENE_OK || gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK ||
      gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK ||
      gimg_raster_create(WIDTH, HEIGHT, &format, GIMG_RASTER_OWNED, NULL, 0, &screen) != GIMG_OK ||
      gimg_raster_create(WIDTH, HEIGHT, &format, GIMG_RASTER_OWNED, NULL, 0, &whole) != GIMG_OK) {
    return check(1, "setup");
  }

  /* The first version: a panel, a slider track and its knob.  The application keeps the knob's ID for the driver. */
  GSHSCENE_Store * store = gshscene_scene_store(scene);
  const GSHSCENE_Resource * panel = rect(store, WIDTH - 20, HEIGHT - 20);
  const GSHSCENE_Resource * track = rect(store, 240, 4);
  const GSHSCENE_Resource * knob = rect(store, 20, 20);
  uint64_t knob_id = 0;
  failed |= check(gshscene_scene_open_edit(scene, NULL, &edit) != GSHSCENE_OK, "open edit");
  GSHSCENE_Node n = shape(panel, 10, 10, 0.93, 0.93, 0.95);
  failed |= check(gshscene_edit_add(edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, NULL, NULL) != GSHSCENE_OK, "panel");
  n = shape(track, 40, 48, 0.6, 0.6, 0.65);
  failed |= check(gshscene_edit_add(edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, NULL, NULL) != GSHSCENE_OK, "track");
  n = shape(knob, 40, 40, 0.15, 0.4, 0.85);
  failed |= check(gshscene_edit_add(edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, NULL, &knob_id) != GSHSCENE_OK, "knob");
  failed |= check(gshscene_scene_publish(scene, edit, NULL, &v1) != GSHSCENE_OK, "publish");
  gshscene_edit_destroy(edit);
  edit = NULL;
  failed |= check(failed || render(sink, v1, screen, NULL, report) != GSHSCENE_OK, "first frame");

  /* The driver moves the knob in an edit the application opens on v1; only the application publishes. */
  failed |= check(gshscene_scene_open_edit(scene, v1, &edit) != GSHSCENE_OK || drive_slider(edit, knob_id, 0.35),
      "driver");
  failed |= check(failed || gshscene_scene_publish(scene, edit, NULL, &v2) != GSHSCENE_OK, "publish");

  /* What can differ on screen: the diff's damage under the target's map (here the identity, NULL). */
  failed |= check(failed || gshscene_diff(v1, v2, NULL, NULL, &diff) != GSHSCENE_OK, "diff");
  GSHSCENE_Damage damage;
  memset(&damage, 0, sizeof damage);
  failed |= check(failed || gshscene_diff_damage(diff, &damage) != GSHSCENE_OK, "damage");
  if (!failed) {
    if (damage.unbounded || damage.empty) {
      return check(1, "a bounded, non-empty damage");
    }
    int64_t area = (damage.rect.x1 - damage.rect.x0) * (damage.rect.y1 - damage.rect.y0);
    printf("changed nodes: %zu\n", gshscene_diff_count(diff));
    printf("damage: x %" PRId64 "..%" PRId64 ", y %" PRId64 "..%" PRId64 " (%" PRId64 " of %d pixels, %.2f %%)\n",
        damage.rect.x0, damage.rect.x1, damage.rect.y0, damage.rect.y1, area, WIDTH * HEIGHT,
        100.0 * (double)area / (WIDTH * HEIGHT));
  }

  /* The next frame redraws only the damage, on top of the last frame; the reference redraws everything. */
  failed |= check(failed || render(sink, v2, screen, &damage.rect, report) != GSHSCENE_OK, "damage-limited frame");
  failed |= check(failed || render(sink, v2, whole, NULL, report) != GSHSCENE_OK, "whole frame");

  GSHSCENE_Rect all = {0, 0, WIDTH, HEIGHT};
  uint64_t h_screen = 0, h_whole = 0;
  failed |= check(failed || gshcpu_render_hash(screen, &all, &h_screen) != GSHCPU_OK ||
                      gshcpu_render_hash(whole, &all, &h_whole) != GSHCPU_OK,
      "hash");
  printf("screen after the damage-limited frame %016" PRIx64 "\nwhole render of the new version   %016" PRIx64 "\n",
      h_screen, h_whole);
  failed |= check(!failed && h_screen != h_whole, "the damage-limited frame equals the whole render");

  gshscene_diff_destroy(diff);
  gshscene_edit_destroy(edit);
  gshscene_version_release(v2);
  gshscene_version_release(v1);
  gshscene_resource_release(knob);
  gshscene_resource_release(track);
  gshscene_resource_release(panel);
  gimg_raster_destroy(whole);
  gimg_raster_destroy(screen);
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gshscene_scene_destroy(scene);
  return failed;
}
