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
 * Stroke a glyph run: a line of two "glyphs" (outlines placed side by side, here close enough to overlap) stroked in a
 * translucent color.  The run's centerline is one path made of its glyphs' outlines, so the stroke is one union across the
 * run: where the two strokes cross the color is no deeper than anywhere else in it.  The example checks that, and that
 * stroking the path `gshscene_glyph_run_centerline` makes for the run gives the same pixels as stroking the run.
 *
 *     $ ./stroke-run
 *     gshcpu.render 1 format=rgba8 extent=0,0,40,20 hash=...
 *     the stroke of the run is the stroke of its path; where the glyphs cross it is no deeper
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

#define W 40
#define H 20

/** Draw @p list into a fresh transparent raster on @p sink; the alpha bytes into @p alpha. */
static int draw(GSHSCENE_Sink * sink, const GSHSCENE_List * list, unsigned char alpha[H][W], uint64_t * hash) {
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  GIMG_Raster * raster = NULL;
  GSHSCENE_Report * report = NULL;
  int ok = 0;
  if (gimg_raster_create(W, H, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) == GIMG_OK &&
      gshscene_report_create(NULL, NULL, &report) == GSHSCENE_OK) {
    memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * H);
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
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
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

/** A list with one stroke of @p centerline, translucent, round joins. */
static GSHSCENE_List * stroke_list(GSHSCENE_Store * store, const GSHSCENE_Resource * centerline, const GSHSCENE_Resource * style) {
  GSHSCENE_List * list = NULL;
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_SOLID;
  paint.rgba[0] = 0.8;
  paint.rgba[1] = 0.2;
  paint.rgba[3] = 0.5;
  if (gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gshscene_list_stroke(list, centerline, style, NULL, &paint, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_close(list) != GSHSCENE_OK) {
    gshscene_list_destroy(list);
    return NULL;
  }
  return list;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_Sink * sink = NULL;
  const GSHSCENE_Resource * z = NULL;
  const GSHSCENE_Resource * run = NULL;
  const GSHSCENE_Resource * single = NULL;
  const GSHSCENE_Resource * centerline = NULL;
  const GSHSCENE_Resource * style = NULL;
  GSHSCENE_List * by_run = NULL;
  GSHSCENE_List * by_one = NULL;
  GSHSCENE_List * by_path = NULL;
  GSHGEOM_Path * path = NULL;
  int status = 1;

  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  /* A "glyph": a Z, an open contour of three lines, in its own space. */
  GSHGEOM_Path * outline = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &outline) != GSHGEOM_OK) {
    goto done;
  }
  gshgeom_path_move_to(outline, at(0.0, 0.0));
  gshgeom_path_line_to(outline, at(10.0, 0.0));
  gshgeom_path_line_to(outline, at(0.0, 12.0));
  gshgeom_path_line_to(outline, at(10.0, 12.0));
  int interned = gshscene_store_intern_path(store, outline, &z) == GSHSCENE_OK;
  gshgeom_path_destroy(outline);
  if (!interned) {
    goto done;
  }
  /* Two of them, the second 6 units from the first: their strokes cross. */
  GSHSCENE_Glyph glyphs[2] = {{1, 6.0, 4.0, z}, {2, 12.0, 4.0, z}};
  GSHSCENE_Glyph_Run text;
  memset(&text, 0, sizeof text);
  text.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  text.glyph_count = 2;
  text.glyphs = glyphs;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 3.0;
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  st.cap = GSHSCENE_STROKE_CAP_ROUND;
  GSHSCENE_Glyph_Run first = text;
  first.glyph_count = 1;
  if (gshscene_store_intern_glyph_run(store, &text, &run) != GSHSCENE_OK ||
      gshscene_store_intern_glyph_run(store, &first, &single) != GSHSCENE_OK ||
      gshscene_glyph_run_centerline(run, NULL, NULL, &path, NULL) != GSHSCENE_OK ||
      gshscene_store_intern_path(store, path, &centerline) != GSHSCENE_OK ||
      gshscene_store_intern_stroke_style(store, &st, &style) != GSHSCENE_OK ||
      !(by_run = stroke_list(store, run, style)) || !(by_path = stroke_list(store, centerline, style)) ||
      !(by_one = stroke_list(store, single, style)) ||
      gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK) {
    goto done;
  }
  unsigned char a[H][W];
  unsigned char b[H][W];
  uint64_t ha = 0;
  uint64_t hb = 0;
  if (!draw(sink, by_run, a, &ha) || !draw(sink, by_path, b, &hb)) {
    goto done;
  }
  printf("gshcpu.render 1 format=rgba8 extent=0,0,%d,%d hash=%016llx\n", W, H, (unsigned long long)ha);
  if (ha != hb || memcmp(a, b, sizeof a) != 0) {
    fprintf(stderr, "stroke-run: the stroke of the run is not the stroke of its path\n");
    goto done;
  }
  /* One coat of a half-transparent paint is as deep as it gets: the deepest pixel of the two glyphs' stroke is that of one. */
  unsigned char c[H][W];
  uint64_t hc = 0;
  if (!draw(sink, by_one, c, &hc)) {
    goto done;
  }
  int deepest = 0;
  int deepest_one = 0;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      deepest = a[y][x] > deepest ? a[y][x] : deepest;
      deepest_one = c[y][x] > deepest_one ? c[y][x] : deepest_one;
    }
  }
  if (deepest != deepest_one || deepest == 0) {
    fprintf(stderr, "stroke-run: the strokes were composited one on another (deepest alpha %d, one glyph's %d)\n", deepest, deepest_one);
    goto done;
  }
  printf("the stroke of the run is the stroke of its path; where the glyphs cross it is no deeper (alpha %d)\n", deepest);
  status = 0;

done:
  gshscene_sink_destroy(sink);
  gshscene_list_destroy(by_run);
  gshscene_list_destroy(by_one);
  gshscene_list_destroy(by_path);
  gshgeom_path_destroy(path);
  gshscene_resource_release(centerline);
  gshscene_resource_release(single);
  gshscene_resource_release(run);
  gshscene_resource_release(z);
  gshscene_resource_release(style);
  gshscene_store_destroy(store);
  return status;
}
