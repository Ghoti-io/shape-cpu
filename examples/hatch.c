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
 * A hatched fill: a rectangle with a hole, hatched by a dashed family at 45 degrees and a solid one at 135, in the stroke of a round pen,
 * drawn into an RGBA8 raster.  It checks that a pixel on a dash is painted, a pixel in a gap is not, and the hole stays empty under the
 * outermost-ring nesting; it exits non-zero otherwise.
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>

#include <ghoti.io/image/raster.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static int alpha_at(const GIMG_Raster * r, unsigned x, unsigned y) {
  const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(r);
  return px[y * gimg_raster_stride_bytes(r) + x * 4u + 3u];
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * raster = NULL;
  const GSHSCENE_Resource * region = NULL;
  const GSHSCENE_Resource * style = NULL;
  const GSHSCENE_Resource * hatch = NULL;
  int status = 1;
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  GSHGEOM_Path * path = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &path) != GSHGEOM_OK || gshgeom_path_add_rect(path, 4, 4, 56, 40, 0, 0) != GSHGEOM_OK ||
      gshgeom_path_add_rect(path, 22, 16, 20, 16, 1, 0) != GSHGEOM_OK || gshscene_store_intern_path(store, path, &region) != GSHSCENE_OK) {
    gshgeom_path_destroy(path);
    goto done;
  }
  gshgeom_path_destroy(path);
  GSHSCENE_Stroke_Style pen;
  gshscene_stroke_style_init(&pen);
  pen.width = 1.5;
  pen.cap = GSHSCENE_STROKE_CAP_ROUND;
  static const double dashes[2] = {5.0, -2.0};
  GSHSCENE_Hatch_Family families[2] = {{45.0, 0.0, 0.0, 0.0, 4.0, 2, dashes}, {135.0, 0.0, 0.0, 0.0, 6.0, 0, NULL}};
  GSHSCENE_Hatch spec;
  gshscene_hatch_init(&spec);
  if (gshscene_store_intern_stroke_style(store, &pen, &style) != GSHSCENE_OK) {
    goto done;
  }
  spec.style = style;
  spec.nesting = GSHSCENE_HATCH_NESTING_OUTER; /* DXF's outer style: the hole stays empty */
  spec.family_count = 2;
  spec.families = families;
  spec.ink.rgba[0] = 0.1;
  spec.ink.rgba[1] = 0.2;
  spec.ink.rgba[2] = 0.6;
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_HATCH;
  paint.rgba[3] = 1.0;
  if (gshscene_store_intern_hatch(store, &spec, &hatch) != GSHSCENE_OK || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gimg_raster_create(64, 48, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK || gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK ||
      gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK) {
    goto done;
  }
  paint.resource = hatch;
  memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * 48u);
  if (gshscene_list_fill(list, region, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) != GSHSCENE_OK || gshscene_list_close(list) != GSHSCENE_OK) {
    goto done;
  }
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  if (gshscene_sink_render(sink, &request) != GSHSCENE_OK) {
    goto done;
  }
  int painted = 0;
  for (unsigned y = 0; y < 48; y++) {
    for (unsigned x = 0; x < 64; x++) {
      painted += alpha_at(raster, x, y) > 0;
    }
  }
  int hole_clear = 1;
  for (unsigned y = 19; y < 29; y++) {
    for (unsigned x = 25; x < 39; x++) {
      hole_clear = hole_clear && alpha_at(raster, x, y) == 0;
    }
  }
  if (painted < 200 || painted > 64 * 48 * 2 / 3 || !hole_clear) {
    fprintf(stderr, "hatch: %d pixels painted, hole clear %d\n", painted, hole_clear);
    goto done;
  }
  printf("hatch: %d pixels hatched, the hole empty\n", painted);
  status = 0;

done:
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gimg_raster_destroy(raster);
  gshscene_list_destroy(list);
  gshscene_resource_release(hatch);
  gshscene_resource_release(style);
  gshscene_resource_release(region);
  gshscene_store_destroy(store);
  return status;
}
