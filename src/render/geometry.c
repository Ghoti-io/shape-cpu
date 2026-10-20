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
 * The geometry of one fill or clip push (S16, AD-12, AD-24): its maps composed
 * root to leaf with the target's last, its path (or each glyph's outline, moved
 * to the glyph's position) made a device path and flattened, the definition's
 * way, after the map.  A run from a font goes by its route (AD-5, story 14a):
 * the fast tier's device path from the face's provider, moved by the whole
 * pixel, or the unhinted outline under the glyph's map, rounded after it.  Everything is allocated from the allocator given: the
 * sink's, or the workspace's arena.  The order of the calls is the same whether
 * the allocator is the arena or its measure (arena.c).
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-scene/glyph.h>
#include <ghoti.io/shape-scene/resource.h>

#include <string.h>

/** A `shape-geometry` failure as the sink's: running out of memory, or a limit it cannot draw past. */
static GSHCPU_Result from_geometry(GSHGEOM_Result r) {
  if (r == GSHGEOM_OK) {
    return GSHCPU_OK;
  }
  return r == GSHGEOM_ERR_OOM ? GSHCPU_ERR_OOM : GSHCPU_ERR_LIMIT;
}

/** The pixel holding a 26.6 coordinate: the floor of @p v / 64, for either sign. */
static int64_t pixel_floor(int64_t v) {
  int64_t q = v / 64;
  return v % 64 != 0 && v < 0 ? q - 1 : q;
}

int gshcpu_render_glyph_route(const GSHSCENE_Item * item, GSHGEOM_Map * chain, size_t count, const GSHGEOM_Map * target,
    GSHSCENE_Glyph_Route * out) {
  GSHSCENE_Glyph_Run run;
  if ((item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_CLIP_PUSH) ||
      gshscene_resource_glyph_run(item->resource, &run) != GSHSCENE_OK || run.source != GSHSCENE_GLYPHS_FROM_FONT) {
    return 0;
  }
  chain[count] = item->map;
  GSHGEOM_Map map;
  return gshgeom_map_compose_chain(chain, count + 1, target, &map) == GSHGEOM_OK &&
         gshscene_glyph_route(item->resource, &map, GSHSCENE_TARGET_RASTER, out) == GSHSCENE_OK;
}

/** A glyph refused as the sink's own limit. */
static void limit_note(GSHCPU_Glyph_Note * note) {
  note->entry_class = GSHSCENE_ENTRY_REFUSED;
  note->reason = GSHSCENE_REASON_RESOURCE_LIMIT;
}

/**
 * Glyph @p i of a run from a font: its device path by the route, flattened into @p out (NULL when the glyph is
 * refused or has no outline), and its note.  @return ::GSHCPU_OK or ::GSHCPU_ERR_OOM.
 */
static GSHCPU_Result font_glyph(const GSHSCENE_Item * item, const GSHGEOM_Map * map, const GSHSCENE_Glyph_Route * route,
    size_t i, const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits, GSHGEOM_Flat_Path ** out,
    GSHCPU_Glyph_Note * note) {
  GSHGEOM_Device_Path * device = NULL;
  GSHSCENE_Glyph_Note gn;
  memset(&gn, 0, sizeof gn);
  GSHSCENE_Result r;
  if (route->route == GSHSCENE_GLYPH_FAST) {
    GSHSCENE_Glyph_Origin origin;
    r = gshscene_glyph_origin(item->resource, map, i, &origin);
    if (r == GSHSCENE_OK) {
      r = gshscene_glyph_device_path(item->resource, route, i, &origin, alloc, limits, &device, &gn);
    }
  } else {
    GSHGEOM_Map glyph_map;
    GSHGEOM_Path * outline = NULL;
    r = gshscene_glyph_map(item->resource, map, i, &glyph_map);
    if (r == GSHSCENE_OK) {
      r = gshscene_glyph_outline(item->resource, i, alloc, limits, &outline, &gn);
    }
    if (r == GSHSCENE_OK && outline) {
      GSHGEOM_Result g = gshgeom_device_path_from_model(outline, &glyph_map, alloc, limits, &device);
      r = g == GSHGEOM_OK ? GSHSCENE_OK : g == GSHGEOM_ERR_OOM ? GSHSCENE_ERR_OOM : GSHSCENE_ERR_LIMIT;
    }
    gshgeom_path_destroy(outline);
  }
  note->entry_class = gn.entry_class;
  note->reason = gn.reason;
  if (r == GSHSCENE_ERR_OOM) {
    gshgeom_device_path_destroy(device);
    return GSHCPU_ERR_OOM;
  }
  if (r != GSHSCENE_OK) {
    /* An origin past the 26.6 range, a glyph map past shape-geometry's bounds, a device path past them. */
    gshgeom_device_path_destroy(device);
    limit_note(note);
    return GSHCPU_OK;
  }
  if (device) {
    GSHGEOM_Result g = gshgeom_device_path_flatten(device, alloc, limits, out);
    gshgeom_device_path_destroy(device);
    if (g == GSHGEOM_ERR_OOM) {
      return GSHCPU_ERR_OOM;
    }
    if (g != GSHGEOM_OK) {
      *out = NULL;
      limit_note(note); /* past the flattener's points */
    }
  }
  return GSHCPU_OK;
}

/** Flatten @p path under the composed map of @p chain[0 .. count). */
static GSHCPU_Result one(const GSHGEOM_Path * path, const GSHGEOM_Map * chain, size_t count, const GSHGEOM_Map * target,
    const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits, GSHGEOM_Flat_Path ** out) {
  GSHGEOM_Map map;
  GSHGEOM_Result r = gshgeom_map_compose_chain(chain, count, target, &map);
  if (r != GSHGEOM_OK) {
    return from_geometry(r);
  }
  GSHGEOM_Device_Path * device = NULL;
  r = gshgeom_device_path_from_model(path, &map, alloc, limits, &device);
  if (r != GSHGEOM_OK) {
    return from_geometry(r);
  }
  r = gshgeom_device_path_flatten(device, alloc, limits, out);
  gshgeom_device_path_destroy(device);
  return from_geometry(r);
}

GSHCPU_Result gshcpu_render_geometry(const GSHSCENE_Item * item, GSHGEOM_Map * chain, size_t count,
    const GSHGEOM_Map * target, const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits,
    GSHCPU_Item_Geometry * out) {
  memset(out, 0, sizeof *out);
  chain[count] = item->map;
  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  size_t n = 1;
  GSHSCENE_Glyph_Route route;
  GSHGEOM_Map map;
  int font = 0;
  if (!path) {
    if (gshscene_resource_glyph_run(item->resource, &run) != GSHSCENE_OK) {
      return GSHCPU_ERR_LIMIT;
    }
    if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {
      /* The route, as the plan decided it; a run it refuses is skipped before it gets here. */
      GSHGEOM_Result t = gshgeom_map_compose_chain(chain, count + 1, target, &map);
      if (t != GSHGEOM_OK) {
        return from_geometry(t);
      }
      if (gshscene_glyph_route(item->resource, &map, GSHSCENE_TARGET_RASTER, &route) != GSHSCENE_OK ||
          route.entry_class == GSHSCENE_ENTRY_REFUSED) {
        return GSHCPU_ERR_INTERNAL;
      }
      font = 1;
    }
    n = run.glyph_count;
  }
  if (n > 0) {
    out->paths = alloc->calloc_fn(alloc->ctx, n, sizeof *out->paths);
    if (!out->paths) {
      return GSHCPU_ERR_OOM;
    }
    if (font) {
      out->notes = alloc->calloc_fn(alloc->ctx, n, sizeof *out->notes);
      if (!out->notes) {
        return GSHCPU_ERR_OOM;
      }
    }
  }
  out->count = n;
  for (size_t i = 0; i < n; i++) {
    GSHCPU_Result r;
    if (font) {
      r = font_glyph(item, &map, &route, i, alloc, limits, &out->paths[i], &out->notes[i]);
    } else if (path) {
      r = one(path, chain, count + 1, target, alloc, limits, &out->paths[i]);
    } else {
      const GSHGEOM_Path * outline = gshscene_resource_path(run.glyphs[i].outline);
      if (!outline) {
        continue; /* a glyph with no outline draws nothing */
      }
      GSHGEOM_Result t = gshgeom_map_translate(2, run.glyphs[i].x, run.glyphs[i].y, 0.0, &chain[count + 1]);
      r = t != GSHGEOM_OK ? from_geometry(t) : one(outline, chain, count + 2, target, alloc, limits, &out->paths[i]);
    }
    if (r != GSHCPU_OK) {
      return r;
    }
  }
  /* The points, their range (render.h: within 2^24 pixels of the origin) and the pixels they touch. */
  int64_t x0 = 0;
  int64_t y0 = 0;
  int64_t x1 = 0;
  int64_t y1 = 0;
  int any = 0;
  for (size_t i = 0; i < n; i++) {
    if (!out->paths[i]) {
      continue;
    }
    const GSHGEOM_Device_Point * points = gshgeom_flat_path_points(out->paths[i]);
    size_t m = gshgeom_flat_path_point_count(out->paths[i]);
    out->points += m;
    for (size_t k = 0; k < m; k++) {
      int64_t x = points[k].x;
      int64_t y = points[k].y;
      if (x < -GSHCPU_RENDER_COORD_LIMIT || x > GSHCPU_RENDER_COORD_LIMIT || y < -GSHCPU_RENDER_COORD_LIMIT ||
          y > GSHCPU_RENDER_COORD_LIMIT) {
        return GSHCPU_ERR_LIMIT;
      }
      if (!any || x < x0) {
        x0 = x;
      }
      if (!any || y < y0) {
        y0 = y;
      }
      if (!any || x > x1) {
        x1 = x;
      }
      if (!any || y > y1) {
        y1 = y;
      }
      any = 1;
    }
  }
  if (any) {
    /* Whole pixels, rounded out. */
    out->box.x0 = pixel_floor(x0);
    out->box.y0 = pixel_floor(y0);
    out->box.x1 = -pixel_floor(-x1);
    out->box.y1 = -pixel_floor(-y1);
  }
  return GSHCPU_OK;
}

void gshcpu_render_geometry_release(GSHCPU_Item_Geometry * g, const GSHGEOM_Allocator * alloc) {
  if (g->paths) {
    for (size_t i = 0; i < g->count; i++) {
      gshgeom_flat_path_destroy(g->paths[i]);
    }
    alloc->free_fn(alloc->ctx, g->paths);
  }
  if (g->notes) {
    alloc->free_fn(alloc->ctx, g->notes);
  }
  memset(g, 0, sizeof *g);
}
