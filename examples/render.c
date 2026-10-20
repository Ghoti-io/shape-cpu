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
 * Render a list with the CPU sink: a triangle and a half-transparent square in
 * an opacity group, under a clip, on a 16 by 12 RGBA8 raster; then print the
 * render's hash and the report.
 *
 *     $ ./render
 *     gshcpu.render 1 format=rgba8 extent=0,0,16,12 hash=...
 *     gshscene.report 1 result=0 units=6/6/6 ...
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static GSHGEOM_Point at(double x, double y) {
  GSHGEOM_Point p = {x, y, 0.0};
  return p;
}

/** Intern a closed polygon. */
static const GSHSCENE_Resource * polygon(GSHSCENE_Store * store, const double * xy, int n) {
  GSHGEOM_Path * path = NULL;
  const GSHSCENE_Resource * r = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &path) != GSHGEOM_OK) {
    return NULL;
  }
  gshgeom_path_move_to(path, at(xy[0], xy[1]));
  for (int i = 1; i < n; i++) {
    gshgeom_path_line_to(path, at(xy[2 * i], xy[2 * i + 1]));
  }
  gshgeom_path_close(path);
  gshscene_store_intern_path(store, path, &r);
  gshgeom_path_destroy(path);
  return r;
}

int main(void) {
  static const double triangle[] = {1.5, 10.5, 8.0, 0.75, 14.5, 10.5};
  static const double square[] = {6.25, 3.0, 15.0, 3.0, 15.0, 11.5, 6.25, 11.5};
  static const double window[] = {0.0, 2.0, 16.0, 2.0, 16.0, 12.0, 0.0, 12.0};
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * raster = NULL;
  const GSHSCENE_Resource * shapes[3] = {NULL, NULL, NULL};
  int status = 1;

  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  shapes[0] = polygon(store, triangle, 3);
  shapes[1] = polygon(store, square, 4);
  shapes[2] = polygon(store, window, 4);
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  if (!shapes[0] || !shapes[1] || !shapes[2] || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gimg_raster_create(16, 12, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK ||
      gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK ||
      gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK) {
    goto done;
  }

  /* The list: a clip, and inside it a group of opacity 0.75 holding the two fills. */
  GSHSCENE_Paint orange;
  memset(&orange, 0, sizeof orange);
  orange.kind = GSHSCENE_PAINT_SOLID;
  orange.rgba[0] = 1.0;
  orange.rgba[1] = 0.55;
  orange.rgba[3] = 1.0;
  GSHSCENE_Paint blue = orange;
  blue.rgba[0] = 0.1;
  blue.rgba[1] = 0.3;
  blue.rgba[2] = 0.9;
  blue.rgba[3] = 0.5;
  GSHSCENE_Group group;
  memset(&group, 0, sizeof group);
  group.opacity = 0.75;
  if (gshscene_list_clip_push(list, shapes[2], NULL, GSHGEOM_FILL_NONZERO, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_group_push(list, &group, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_fill(list, shapes[0], NULL, GSHGEOM_FILL_NONZERO, &orange, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_fill(list, shapes[1], NULL, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_group_pop(list) != GSHSCENE_OK || gshscene_list_clip_pop(list) != GSHSCENE_OK ||
      gshscene_list_close(list) != GSHSCENE_OK) {
    goto done;
  }

  /* The target: the raster, whole; the request: everything else zero (best effort, the sink's own memory). */
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  GSHSCENE_Result result = gshscene_sink_render(sink, &request);
  if (result != GSHSCENE_OK) {
    fprintf(stderr, "render: %s\n", gshscene_result_string(result));
    goto done;
  }
  GSHSCENE_Rect all = {0, 0, 16, 12};
  uint64_t hash = 0;
  if (gshcpu_render_hash(raster, &all, &hash) != GSHCPU_OK) {
    goto done;
  }
  printf("gshcpu.render 1 format=%s extent=0,0,16,12 hash=%016llx\n", gshcpu_format_string(gshcpu_format_of(raster)),
      (unsigned long long)hash);
  status = gshscene_report_dump(report, stdout) == GSHSCENE_OK ? 0 : 1;

done:
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gimg_raster_destroy(raster);
  gshscene_list_destroy(list);
  for (int i = 0; i < 3; i++) {
    gshscene_resource_release(shapes[i]);
  }
  gshscene_store_destroy(store);
  return status;
}
