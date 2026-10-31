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
 * Draw two masked groups with the CPU sink on a 16 by 8 RGBA8 raster and read a pixel of each back.  A mask is a resource of the
 * scene's store: a picture (a list that is drawn as its own isolated layer), a mode (the picture's alpha, or its luminance), a
 * transfer, and what the value acts on.  The group names it, and its pop is weighed at each pixel by the mask's value there.
 *
 * The left group is red under an alpha mask whose picture covers its left half: whole inside the picture, and outside it the
 * mask's value is the transfer of nothing, 0.  The right group is blue under a luminance mask, placed over it by the group's
 * mask map, whose picture is a mid gray: a value of one half, so half the group is drawn.
 *
 *     $ ./mask
 *     alpha (1,1) = 255 0 0 255, (6,1) = 0 0 0 0; luminance (12,1) = 0 0 255 128
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

/** A mask of @p mode over a picture of one fill of @p path in @p paint; @p picture receives the picture's reference too. */
static const GSHSCENE_Resource * mask_of(GSHSCENE_Store * store, const GSHSCENE_Resource * path, GSHSCENE_Paint paint, uint32_t mode,
    const GSHSCENE_Resource ** picture) {
  GSHSCENE_List * inner = NULL;
  const GSHSCENE_Resource * mask = NULL;
  *picture = NULL;
  if (gshscene_list_create(NULL, NULL, store, &inner) != GSHSCENE_OK) {
    return NULL;
  }
  if (gshscene_list_fill(inner, path, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) == GSHSCENE_OK && gshscene_list_close(inner) == GSHSCENE_OK &&
      gshscene_store_intern_picture(store, inner, picture) == GSHSCENE_OK) {
    GSHSCENE_Mask content;
    memset(&content, 0, sizeof content);
    content.picture = *picture;
    content.mode = mode;
    if (gshscene_store_intern_mask(store, &content, &mask) != GSHSCENE_OK) {
      mask = NULL;
    }
  }
  gshscene_list_destroy(inner);
  return mask;
}

/** A masked group of one fill of @p path in @p paint, the mask placed by @p placement (NULL: where it is drawn). */
static int masked_fill(GSHSCENE_List * list, const GSHSCENE_Resource * mask, const GSHGEOM_Map * placement, const GSHSCENE_Resource * path,
    GSHSCENE_Paint paint) {
  GSHSCENE_Group group;
  memset(&group, 0, sizeof group);
  group.opacity = 1.0;
  group.soft_mask = mask;
  if (placement) {
    group.soft_mask_map = *placement;
  }
  return gshscene_list_group_push(list, &group, 0, 0.0) == GSHSCENE_OK &&
      gshscene_list_fill(list, path, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) == GSHSCENE_OK &&
      gshscene_list_group_pop(list) == GSHSCENE_OK;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * raster = NULL;
  const GSHSCENE_Resource * held[8];
  size_t held_count = 0;
  int status = 1;
  memset(held, 0, sizeof held);
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  const GSHSCENE_Resource * all_left = rectangle(store, 0, 0, 8, 8);
  const GSHSCENE_Resource * all_right = rectangle(store, 8, 0, 16, 8);
  const GSHSCENE_Resource * half = rectangle(store, 0, 0, 4, 8);
  const GSHSCENE_Resource * square = rectangle(store, 0, 0, 8, 8);
  const GSHSCENE_Resource * alpha_picture = NULL;
  const GSHSCENE_Resource * gray_picture = NULL;
  const GSHSCENE_Resource * alpha = mask_of(store, half, solid(1.0, 1.0, 1.0), GSHSCENE_MASK_ALPHA, &alpha_picture);
  const GSHSCENE_Resource * luminance = mask_of(store, square, solid(0.5, 0.5, 0.5), GSHSCENE_MASK_LUMINANCE, &gray_picture);
  const GSHSCENE_Resource * resources[8] = {all_left, all_right, half, square, alpha_picture, gray_picture, alpha, luminance};
  for (size_t i = 0; i < 8; i++) {
    if (!resources[i]) {
      goto done;
    }
    held[held_count++] = resources[i];
  }
  GSHGEOM_Map over_right;
  if (gshgeom_map_translate(2, 8.0, 0.0, 0.0, &over_right) != GSHGEOM_OK || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gimg_raster_create(16, 8, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK ||
      gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK || gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK) {
    goto done;
  }
  memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * 8);
  if (!masked_fill(list, alpha, NULL, all_left, solid(1.0, 0.0, 0.0)) ||
      !masked_fill(list, luminance, &over_right, all_right, solid(0.0, 0.0, 1.0)) || gshscene_list_close(list) != GSHSCENE_OK) {
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
  const unsigned char * in = row + 1 * 4;
  const unsigned char * out = row + 6 * 4;
  const unsigned char * half_gray = row + 12 * 4;
  printf("alpha (1,1) = %u %u %u %u, (6,1) = %u %u %u %u; luminance (12,1) = %u %u %u %u\n", in[0], in[1], in[2], in[3], out[0], out[1],
      out[2], out[3], half_gray[0], half_gray[1], half_gray[2], half_gray[3]);
  /* Inside the alpha mask's picture the group is whole; outside, the mask's value is 0.  A gray of one half has the luminance one
   * half (the coefficients sum to 1), so the blue group is drawn at half its alpha, 128 of 255. */
  if (in[0] != 255 || in[3] != 255 || out[0] != 0 || out[3] != 0 || half_gray[0] != 0 || half_gray[2] != 255 || half_gray[3] < 127 ||
      half_gray[3] > 129) {
    fprintf(stderr, "mask: the pixels are not what the masks define\n");
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
  if (list) {
    gshscene_list_destroy(list);
  }
  for (size_t i = 0; i < held_count; i++) {
    gshscene_resource_release(held[i]);
  }
  gshscene_store_destroy(store);
  return status;
}
