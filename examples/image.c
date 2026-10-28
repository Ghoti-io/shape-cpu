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
 * Draw an image with the CPU sink: a 2 by 2 image of four colors, magnified by four twice on a 16 by 8 RGBA8 raster, once
 * with `NEAREST` (each texel a block) and once with `LINEAR` (the blocks blend at their boundary), and read two pixels back to
 * check what each filter does.  The image is a resource of the scene's store, an image paint says how a fill samples it (its
 * map, edge modes and filters), and a fill whose paint it is draws the window of the plane the fill covers.
 *
 *     $ ./image
 *     nearest (5,1) = 0 255 0 255; linear (11,1) = 191 64 0 255 ...
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

/** An image paint of @p image magnified by four and placed at (@p x, 0), sampled with @p filter. */
static const GSHSCENE_Resource * magnified(GSHSCENE_Store * store, const GSHSCENE_Resource * image, double x, uint32_t filter) {
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  const double scale[4] = {4.0, 0.0, 0.0, 4.0};
  const double move[2] = {x, 0.0};
  if (gshgeom_map_from_matrix(2, scale, move, &ip.map) != GSHGEOM_OK) {
    return NULL;
  }
  ip.magnify = filter;
  const GSHSCENE_Resource * r = NULL;
  return gshscene_store_intern_image_paint(store, &ip, &r) == GSHSCENE_OK ? r : NULL;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * pixels = NULL;
  GIMG_Raster * raster = NULL;
  const GSHSCENE_Resource * held[5] = {NULL, NULL, NULL, NULL, NULL}; /* the image, the two rectangles and the two paints */
  int status = 1;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }

  /* The image: red and green above blue and white, 8 bits, straight alpha (the store copies the samples). */
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  static const unsigned char texels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
  const GSHSCENE_Resource * image = NULL;
  if (gimg_raster_create(2, 2, &format, GIMG_RASTER_OWNED, NULL, 0, &pixels) != GIMG_OK) {
    goto done;
  }
  for (uint32_t y = 0; y < 2; y++) {
    memcpy((unsigned char *)gimg_raster_pixels(pixels) + (size_t)y * gimg_raster_stride_bytes(pixels), texels + 8 * y, 8);
  }
  GSHSCENE_Image_Params params;
  memset(&params, 0, sizeof params); /* the zero space is the target's */
  if (gshscene_store_intern_image(store, pixels, &params, &image) != GSHSCENE_OK) {
    goto done;
  }
  held[0] = image;
  const GSHSCENE_Resource * left = held[1] = rectangle(store, 0, 0, 8, 8);
  const GSHSCENE_Resource * right = held[2] = rectangle(store, 8, 0, 16, 8);
  const GSHSCENE_Resource * nearest = held[3] = magnified(store, image, 0.0, GSHSCENE_IMAGE_FILTER_NEAREST);
  const GSHSCENE_Resource * linear = held[4] = magnified(store, image, 8.0, GSHSCENE_IMAGE_FILTER_LINEAR);
  if (!left || !right || !nearest || !linear || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gimg_raster_create(16, 8, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK ||
      gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK ||
      gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK) {
    goto done;
  }
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_IMAGE;
  paint.rgba[3] = 1.0; /* the alpha multiplier */
  paint.resource = nearest;
  if (gshscene_list_fill(list, left, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) != GSHSCENE_OK) {
    goto done;
  }
  paint.resource = linear;
  if (gshscene_list_fill(list, right, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) != GSHSCENE_OK ||
      gshscene_list_close(list) != GSHSCENE_OK) {
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

  /* NEAREST: pixel (5, 1) is inside the block of the texel (1, 0), green, whole.  LINEAR: pixel (11, 1) of the second copy lies
   * between the centers of the red and the green texel, so it is a blend of the two with no blue. */
  const unsigned char * a = (const unsigned char *)gimg_raster_pixels_const(raster) + 1 * gimg_raster_stride_bytes(raster) + 5 * 4;
  const unsigned char * b = (const unsigned char *)gimg_raster_pixels_const(raster) + 1 * gimg_raster_stride_bytes(raster) + 11 * 4;
  printf("nearest (5,1) = %u %u %u %u; linear (11,1) = %u %u %u %u\n", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
  if (a[0] != 0 || a[1] != 255 || a[2] != 0 || a[3] != 255 || b[0] == 0 || b[1] == 0 || b[2] != 0 || b[3] != 255) {
    fprintf(stderr, "image: the pixels are not what the filters define\n");
    goto done;
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
  gimg_raster_destroy(pixels);
  if (list) {
    gshscene_list_destroy(list);
  }
  for (int i = 0; i < 5; i++) {
    if (held[i]) {
      gshscene_resource_release(held[i]);
    }
  }
  gshscene_store_destroy(store);
  return status;
}
