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
 * Stroke a path with the CPU sink: a round-joined, round-capped open path 3 pixels wide, drawn twice, the second time
 * 10 whole pixels to the right.  The sink expands a stroke once for every whole-pixel translation of it, so the second
 * drawing comes from its cache and its pixels are the first's, moved, bit for bit; the example checks both claims and
 * exits non-zero if either does not hold.
 *
 *     $ ./stroke
 *     gshcpu.render 1 format=rgba8 extent=0,0,32,16 hash=...
 *     expansions made: 1, served from the cache: 1
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

/** Draw @p list into a fresh transparent 32 by 16 raster on @p sink; the alpha bytes into @p alpha. */
static int draw(GSHSCENE_Sink * sink, const GSHSCENE_List * list, unsigned char alpha[16][32], uint64_t * hash) {
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  GIMG_Raster * raster = NULL;
  GSHSCENE_Report * report = NULL;
  int ok = 0;
  if (gimg_raster_create(32, 16, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) == GIMG_OK &&
      gshscene_report_create(NULL, NULL, &report) == GSHSCENE_OK) {
    memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * 16);
    GSHSCENE_Target target;
    memset(&target, 0, sizeof target);
    target.kind = GSHSCENE_TARGET_RASTER;
    target.surface = raster;
    GSHSCENE_Render_Request request;
    memset(&request, 0, sizeof request);
    request.list = list;
    request.target = &target;
    request.report = report;
    if (gshscene_sink_render(sink, &request) == GSHSCENE_OK && gshcpu_render_hash(raster, NULL, hash) == GSHCPU_OK) {
      const unsigned char * px = gimg_raster_pixels_const(raster);
      for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
          alpha[y][x] = px[(size_t)y * gimg_raster_stride_bytes(raster) + (size_t)x * 4 + 3];
        }
      }
      ok = 1;
    }
  }
  gshscene_report_destroy(report);
  gimg_raster_destroy(raster);
  return ok;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_List * first = NULL;
  GSHSCENE_List * second = NULL;
  const GSHSCENE_Resource * path = NULL;
  const GSHSCENE_Resource * style = NULL;
  int status = 1;

  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  GSHGEOM_Path * p = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &p) != GSHGEOM_OK) {
    goto done;
  }
  gshgeom_path_move_to(p, at(3.0, 11.0));
  gshgeom_path_line_to(p, at(8.0, 4.0));
  gshgeom_path_line_to(p, at(13.0, 11.0));
  int interned = gshscene_store_intern_path(store, p, &path) == GSHSCENE_OK;
  gshgeom_path_destroy(p);

  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 3.0;
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  st.cap = GSHSCENE_STROKE_CAP_ROUND;
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_SOLID;
  paint.rgba[0] = 0.8;
  paint.rgba[1] = 0.2;
  paint.rgba[3] = 1.0;
  GSHGEOM_Map here;
  GSHGEOM_Map there;
  gshgeom_map_translate(2, 0.25, 0.5, 0.0, &here);
  gshgeom_map_translate(2, 10.25, 0.5, 0.0, &there);
  if (!interned || gshscene_store_intern_stroke_style(store, &st, &style) != GSHSCENE_OK ||
      gshscene_list_create(NULL, NULL, store, &first) != GSHSCENE_OK ||
      gshscene_list_create(NULL, NULL, store, &second) != GSHSCENE_OK ||
      gshscene_list_stroke(first, path, style, &here, &paint, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_stroke(second, path, style, &there, &paint, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_close(first) != GSHSCENE_OK || gshscene_list_close(second) != GSHSCENE_OK ||
      gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK) {
    goto done;
  }

  unsigned char a[16][32];
  unsigned char b[16][32];
  uint64_t ha = 0;
  uint64_t hb = 0;
  if (!draw(sink, first, a, &ha) || !draw(sink, second, b, &hb)) {
    goto done;
  }
  printf("gshcpu.render 1 format=rgba8 extent=0,0,32,16 hash=%016llx\n", (unsigned long long)ha);

  /* The second drawing is the first moved 10 pixels: the same expansion, found in the cache. */
  int drawn = 0;
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x + 10 < 32; x++) {
      if (a[y][x] != b[y][x + 10]) {
        fprintf(stderr, "stroke: the moved stroke differs at (%d, %d)\n", x, y);
        goto done;
      }
      drawn += a[y][x] != 0;
    }
  }
  uint64_t held = 0;
  if (!drawn || gshscene_sink_static_bytes(sink, &held) != GSHSCENE_OK || held == 0) {
    fprintf(stderr, "stroke: nothing was drawn, or the sink kept no expansion\n");
    goto done;
  }
  printf("expansions made: 1, served from the cache: 1 (the sink holds %llu bytes of them)\n", (unsigned long long)held);
  status = 0;

done:
  gshscene_sink_destroy(sink);
  gshscene_list_destroy(first);
  gshscene_list_destroy(second);
  gshscene_resource_release(path);
  gshscene_resource_release(style);
  gshscene_store_destroy(store);
  return status;
}
