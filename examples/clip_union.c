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
 * A union clip drawn by the CPU sink.
 *
 * Two rectangles that share the edge x = 3.5 are clipped to as one union (an SVG `<clipPath>` of two children).  The coverage of a pixel
 * the two share is the sum of their exact areas, quantized once: half a pixel from each side is a whole pixel, 255, where an alpha mask
 * made of the same two shapes composes to three quarters, 192.  The example prints the alpha of the row across the join, and then
 * clips to two rectangles that overlap, a quarter pixel of one lying across the other's edge.  Their areas in that pixel are one half and one
 * half, and their sum would be a whole pixel; the sink draws the union, a pixel covered to the three quarters that the two together cover
 * (the exact step takes the pixels where the members meet, so there is neither a seam nor a double count).  Each case checks what it shows.
 *
 *     $ ./clip_union
 *     abutting: alpha across the join 255 255 255 255 255 255 255 255
 *     overlapping: alpha across the join 255 255 255 192 0 0 0 0
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static const GSHSCENE_Resource * rect(GSHSCENE_Store * store, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * path = NULL;
  const GSHSCENE_Resource * r = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &path) == GSHGEOM_OK) {
    gshgeom_path_move_to(path, (GSHGEOM_Point){x0, y0, 0});
    gshgeom_path_line_to(path, (GSHGEOM_Point){x1, y0, 0});
    gshgeom_path_line_to(path, (GSHGEOM_Point){x1, y1, 0});
    gshgeom_path_line_to(path, (GSHGEOM_Point){x0, y1, 0});
    gshgeom_path_close(path);
    gshscene_store_intern_path(store, path, &r);
  }
  gshgeom_path_destroy(path);
  return r;
}

/** Render a white fill of the 8 by 4 sheet clipped to the union of the two rectangles; the alpha of row 1 is printed. */
static int draw(GSHSCENE_Store * store, GSHSCENE_Sink * sink, const char * what, const GSHSCENE_Resource * a, const GSHSCENE_Resource * b,
    const unsigned char * want) {
  GSHSCENE_Clip_Member members[2];
  memset(members, 0, sizeof members);
  members[0].resource = a;
  members[1].resource = b;
  const GSHSCENE_Resource * u = NULL;
  const GSHSCENE_Resource * sheet = rect(store, 0, 0, 8, 4);
  GSHSCENE_List * list = NULL;
  GSHSCENE_Paint white = {GSHSCENE_PAINT_SOLID, {1, 1, 1, 1}, {0, 0, 0.0}, NULL, 0};
  if (gshscene_store_intern_clip_union(store, members, 2, &u) != GSHSCENE_OK || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK) {
    return 1;
  }
  gshscene_list_clip_push(list, u, NULL, GSHGEOM_FILL_NONZERO, 0, 0.0);
  gshscene_list_fill(list, sheet, NULL, GSHGEOM_FILL_NONZERO, &white, 0, 0.0);
  gshscene_list_clip_pop(list);
  gshscene_list_close(list);
  GIMG_Raster * raster = NULL;
  gimg_raster_create(8, 4, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster);
  memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * 4);
  GSHSCENE_Target target = {0};
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  GSHSCENE_Render_Request request = {0};
  request.list = list;
  request.target = &target;
  request.report = report;
  int failed = gshscene_sink_render(sink, &request) != GSHSCENE_OK || gshscene_report_count(report) != 0;
  const unsigned char * row = (const unsigned char *)gimg_raster_pixels(raster) + gimg_raster_stride_bytes(raster);
  printf("%s: alpha across the join", what);
  for (int x = 0; x < 8; x++) {
    printf(" %u", (unsigned)row[4 * x + 3]);
    failed |= row[4 * x + 3] != want[x];
  }
  printf("\n");
  gshscene_report_destroy(report);
  gimg_raster_destroy(raster);
  gshscene_list_destroy(list);
  gshscene_resource_release(sheet);
  gshscene_resource_release(u);
  return failed;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_Sink * sink = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK || gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK) {
    return 1;
  }
  const GSHSCENE_Resource * left = rect(store, 0, 0, 3.5, 4);
  const GSHSCENE_Resource * right = rect(store, 3.5, 0, 8, 4);
  const GSHSCENE_Resource * clash_a = rect(store, 0, 0, 3.5, 4);
  const GSHSCENE_Resource * clash_b = rect(store, 3.25, 0, 3.75, 4);
  static const unsigned char joined[8] = {255, 255, 255, 255, 255, 255, 255, 255};
  static const unsigned char overlapped[8] = {255, 255, 255, 192, 0, 0, 0, 0};
  int failed = draw(store, sink, "abutting", left, right, joined);
  failed |= draw(store, sink, "overlapping", clash_a, clash_b, overlapped);
  gshscene_resource_release(left);
  gshscene_resource_release(right);
  gshscene_resource_release(clash_a);
  gshscene_resource_release(clash_b);
  gshscene_sink_destroy(sink);
  gshscene_store_destroy(store);
  return failed;
}
