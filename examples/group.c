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
 * Draw a non-isolated group and a knockout group with the CPU sink on a 16 by 8 RGBA8 raster over a gray page and read a pixel of each
 * back.  The groups are PDF's: a non-isolated group's content composites with what was painted before the group, so a blend mode
 * inside it sees the page; each element of a knockout group composites with the group's initial backdrop (the page) and not with the
 * elements before it, so the second of two translucent shapes replaces the first where they overlap.
 *
 * The left half is a non-isolated group holding a multiply group of a mid gray: the product with the page, a quarter.  The right half
 * is a knockout group of a half-transparent red and then a half-transparent green over the same pixels: the red is gone, and the green
 * is half over the page.  In a plain group the red would still show through the green.
 *
 *     $ ./group
 *     non-isolated multiply (2,2) = 64 64 64 255; knockout (10,2) = 64 191 64 255
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

static int fill(GSHSCENE_List * list, const GSHSCENE_Resource * path, double r, double g, double b, double a) {
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_SOLID;
  p.rgba[0] = r;
  p.rgba[1] = g;
  p.rgba[2] = b;
  p.rgba[3] = a;
  return gshscene_list_fill(list, path, NULL, GSHGEOM_FILL_NONZERO, &p, 0, 0.0) == GSHSCENE_OK;
}

/** Push a group of @p composite that is non-isolated and, if @p knockout, knockout. */
static int push(GSHSCENE_List * list, uint32_t composite, uint32_t isolation, uint32_t knockout) {
  GSHSCENE_Group group;
  memset(&group, 0, sizeof group);
  group.opacity = 1.0;
  group.composite = composite;
  group.isolation = isolation;
  group.knockout = knockout;
  return gshscene_list_group_push(list, &group, 0, 0.0) == GSHSCENE_OK;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  GSHSCENE_List * list = NULL;
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  GIMG_Raster * raster = NULL;
  int status = 1;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  const GSHSCENE_Resource * page = rectangle(store, 0, 0, 16, 8);
  const GSHSCENE_Resource * left = rectangle(store, 0, 0, 8, 8);
  const GSHSCENE_Resource * right = rectangle(store, 8, 0, 16, 8);
  if (!page || !left || !right || gshscene_list_create(NULL, NULL, store, &list) != GSHSCENE_OK ||
      gimg_raster_create(16, 8, &format, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK ||
      gshcpu_sink_create(NULL, NULL, NULL, &sink) != GSHCPU_OK || gshscene_report_create(NULL, NULL, &report) != GSHSCENE_OK) {
    goto done;
  }
  memset(gimg_raster_pixels(raster), 0, gimg_raster_stride_bytes(raster) * 8);
  if (!fill(list, page, 0.5, 0.5, 0.5, 1.0) ||
      /* a non-isolated group around a multiply group: the multiply sees the page through the group */
      !push(list, GSHSCENE_COMPOSITE_SRC_OVER, GSHSCENE_ISOLATION_NON_ISOLATED, 0) ||
      !push(list, GSHSCENE_COMPOSITE_MULTIPLY, GSHSCENE_ISOLATION_ISOLATED, 0) || !fill(list, left, 0.5, 0.5, 0.5, 1.0) ||
      gshscene_list_group_pop(list) != GSHSCENE_OK || gshscene_list_group_pop(list) != GSHSCENE_OK ||
      /* a knockout group: the second shape replaces the first against the page */
      !push(list, GSHSCENE_COMPOSITE_SRC_OVER, GSHSCENE_ISOLATION_NON_ISOLATED, 1) || !fill(list, right, 1.0, 0.0, 0.0, 0.5) ||
      !fill(list, right, 0.0, 1.0, 0.0, 0.5) || gshscene_list_group_pop(list) != GSHSCENE_OK ||
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

  const unsigned char * row = (const unsigned char *)gimg_raster_pixels_const(raster) + 2 * gimg_raster_stride_bytes(raster);
  const unsigned char * product = row + 2 * 4;
  const unsigned char * knocked = row + 10 * 4;
  printf("non-isolated multiply (2,2) = %u %u %u %u; knockout (10,2) = %u %u %u %u\n", product[0], product[1], product[2], product[3],
      knocked[0], knocked[1], knocked[2], knocked[3]);
  /* 0.5 * 0.5 = a quarter (64 of 255); half green over the gray page is (0.25, 0.75, 0.25): the red is gone. */
  if (product[0] != 64 || product[1] != 64 || product[2] != 64 || product[3] != 255 || knocked[0] != 64 || knocked[1] != 191 ||
      knocked[2] != 64 || knocked[3] != 255) {
    fprintf(stderr, "group: the pixels are not what the groups define\n");
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
  gshscene_resource_release(right);
  gshscene_resource_release(left);
  gshscene_resource_release(page);
  gshscene_store_destroy(store);
  return status;
}
