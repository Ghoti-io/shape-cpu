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
 * Fill a rectangle of a 16 by 4 RGBA8 raster with a pattern, as an SVG or PDF `pattern` or CSS `background-repeat: space` would: a tile
 * (a red stripe and a blue stripe, each two pixels wide) in a cell of four pixels, repeated every six, so that two pixels of the page
 * show between one copy and the next.
 *
 * The steps are whole pixels, so the sink draws one period of the picture and reads the rest from it; the result is the same bits as
 * drawing every copy would give.  Copies are drawn as the pattern defines them: red at pixels 0 and 1 of each copy, blue at 2 and 3,
 * nothing at 4 and 5 (the page's white shows).
 *
 *     $ ./pattern
 *     row 1: 255 0 0 255 | 255 0 0 255 | 0 0 255 255 | 0 0 255 255 | 255 255 255 255 | 255 255 255 255 | 255 0 0 255 | ...
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

static const GSHSCENE_Resource * rectangle(GSHSCENE_Store * store, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * path = NULL;
  const GSHSCENE_Resource * r = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &path) != GSHGEOM_OK) {
    return NULL;
  }
  gshgeom_path_move_to(path, at(x0, y0));
  gshgeom_path_line_to(path, at(x1, y0));
  gshgeom_path_line_to(path, at(x1, y1));
  gshgeom_path_line_to(path, at(x0, y1));
  gshgeom_path_close(path);
  gshscene_store_intern_path(store, path, &r);
  gshgeom_path_destroy(path);
  return r;
}

static int fill(GSHSCENE_List * list, const GSHSCENE_Resource * path, const GSHSCENE_Paint * paint) {
  return gshscene_list_fill(list, path, NULL, GSHGEOM_FILL_NONZERO, paint, 0, 0.0) == GSHSCENE_OK;
}

static GSHSCENE_Paint solid(double r, double g, double b) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_SOLID;
  p.rgba[0] = r;
  p.rgba[1] = g;
  p.rgba[2] = b;
  p.rgba[3] = 1.0;
  return p;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * tile = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * raster = NULL;
  const GSHSCENE_Resource * picture = NULL;
  const GSHSCENE_Resource * pattern = NULL;
  int status = 1;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  const GSHSCENE_Resource * page = rectangle(store, 0, 0, 16, 4);
  const GSHSCENE_Resource * red = rectangle(store, 0, 0, 2, 4);
  const GSHSCENE_Resource * blue = rectangle(store, 2, 0, 4, 4);
  if (!page || !red || !blue || gshscene_list_create(NULL, NULL, store, &tile) != GSHSCENE_OK) {
    goto done;
  }
  /* The tile is a picture: a red stripe and a blue stripe in the cell (0, 0, 4, 4). */
  GSHSCENE_Paint r = solid(1.0, 0.0, 0.0);
  GSHSCENE_Paint b = solid(0.0, 0.0, 1.0);
  if (!fill(tile, red, &r) || !fill(tile, blue, &b) || gshscene_list_close(tile) != GSHSCENE_OK ||
      gshscene_store_intern_picture(store, tile, &picture) != GSHSCENE_OK) {
    goto done;
  }
  /* The pattern: the cell, steps of six (a gap of two pixels), repeating on both axes, under the identity. */
  GSHSCENE_Pattern p;
  gshscene_pattern_init(&p);
  p.tile = picture;
  p.x = 0.0;
  p.y = 0.0;
  p.w = 4.0;
  p.h = 4.0;
  p.x_step = 6.0;
  p.y_step = 4.0;
  p.repeat_x = 1;
  p.repeat_y = 1;
  if (gshscene_store_intern_pattern(store, &p, &pattern) != GSHSCENE_OK || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gimg_raster_create(16, 4, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK || gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK ||
      gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK) {
    goto done;
  }
  memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * 4);
  GSHSCENE_Paint white = solid(1.0, 1.0, 1.0);
  GSHSCENE_Paint pat;
  memset(&pat, 0, sizeof pat);
  pat.kind = GSHSCENE_PAINT_PATTERN;
  pat.resource = pattern;
  pat.rgba[3] = 1.0;
  if (!fill(list, page, &white) || !fill(list, page, &pat) || gshscene_list_close(list) != GSHSCENE_OK) {
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
  const unsigned char * row = (const unsigned char *)gimg_raster_pixels_const(raster) + 1 * gimg_raster_stride_bytes(raster);
  printf("row 1:");
  for (int x = 0; x < 16; x++) {
    printf("%s %u %u %u %u", x % 4 == 0 && x ? " |" : "", row[4 * x], row[4 * x + 1], row[4 * x + 2], row[4 * x + 3]);
  }
  printf("\n");
  /* Copy 0 is pixels 0 to 3 (red, red, blue, blue), the gap is 4 and 5 (the page), copy 1 starts at 6, copy 2 at 12. */
  static const unsigned expect[16][3] = {{255, 0, 0}, {255, 0, 0}, {0, 0, 255}, {0, 0, 255}, {255, 255, 255}, {255, 255, 255}, {255, 0, 0},
      {255, 0, 0}, {0, 0, 255}, {0, 0, 255}, {255, 255, 255}, {255, 255, 255}, {255, 0, 0}, {255, 0, 0}, {0, 0, 255}, {0, 0, 255}};
  for (int x = 0; x < 16; x++) {
    if (row[4 * x] != expect[x][0] || row[4 * x + 1] != expect[x][1] || row[4 * x + 2] != expect[x][2] || row[4 * x + 3] != 255) {
      fprintf(stderr, "pattern: pixel %d is not what the pattern defines\n", x);
      goto done;
    }
  }
  GSHSCENE_Report_Summary summary;
  memset(&summary, 0, sizeof summary);
  gshscene_report_summary(report, &summary);
  printf("units %zu/%zu, refused %zu\n", summary.units_done, summary.units_total, (size_t)summary.by_class[GSHSCENE_ENTRY_REFUSED]);
  status = summary.by_class[GSHSCENE_ENTRY_REFUSED] == 0 ? 0 : 1;

done:
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gimg_raster_destroy(raster);
  if (list) {
    gshscene_list_destroy(list);
  }
  if (tile) {
    gshscene_list_destroy(tile);
  }
  gshscene_resource_release(pattern);
  gshscene_resource_release(picture);
  gshscene_resource_release(blue);
  gshscene_resource_release(red);
  gshscene_resource_release(page);
  gshscene_store_destroy(store);
  return status;
}
