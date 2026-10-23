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
 * The plane (color-paint story P4; shape-scene's PL2 to PL7): a fill of it covers every pixel the clips in force allow,
 * weighted by the clips alone.  The reference for each case is the fill of a path that is the clip's own region: the
 * same pixels, so an antialiased clip edge is weighted once and never twice, and a gradient is sampled at the same
 * centers under the same chain.
 */

#include <gtest/gtest.h>

#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <string>
#include <vector>

using namespace rh;

namespace {

const GSHSCENE_Resource * plane_of(Store & s) {
  const GSHSCENE_Resource * p = nullptr;
  EXPECT_EQ(gshscene_store_intern_plane(s.store, &p), GSHSCENE_OK);
  s.held.push_back(p);
  return p;
}

GSHGEOM_Path * triangle(double x0, double y0, double x1, double y1, double x2, double y2) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{x0, y0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x1, y1, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x2, y2, 0});
  gshgeom_path_close(p);
  return p;
}

GSHSCENE_Paint fg(double alpha) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_FOREGROUND;
  p.rgba[3] = alpha;
  return p;
}

struct Drawn {
  RasterRef raster;
  GSHSCENE_Result result;
  Drawn(const GSHSCENE_List * list, uint32_t w = 16, uint32_t h = 12, const GSHSCENE_Rect * damage = nullptr)
      : raster(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h)) {
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target t = target_of(raster.raster);
    t.damage = damage;
    GSHSCENE_Render_Request rq = request_of(list, &t, report.report);
    result = gshscene_sink_render(sink.sink, &rq);
    EXPECT_EQ(report.summary().by_class[GSHSCENE_ENTRY_REFUSED], 0u);
  }
};

}  // namespace

// A clip's edge inside a pixel is weighed once: the plane under a clip is the clip's own region filled, pixel for pixel.
TEST(RenderPlane, UnderAClipItIsTheFillOfTheClipsRegion) {
  Store s;
  const GSHSCENE_Resource * plane = plane_of(s);
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  struct Region {
    const char * name;
    const GSHSCENE_Resource * shape;
  } regions[] = {
      {"an edge half way into a pixel", s.rect(1.0, 2.0, 8.5, 9.25)},
      {"a triangle", s.path(triangle(1.5, 1.25, 14.75, 3.5, 6.0, 10.5))},
  };
  for (const Region & r : regions) {
    GSHSCENE_List * clipped = s.open();
    ASSERT_EQ(gshscene_list_clip_push(clipped, r.shape, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(clipped, plane, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(clipped), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(clipped), GSHSCENE_OK);
    GSHSCENE_List * filled = s.open();
    ASSERT_EQ(gshscene_list_fill(filled, r.shape, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(filled), GSHSCENE_OK);
    Drawn a(clipped);
    Drawn b(filled);
    ASSERT_EQ(a.result, GSHSCENE_OK) << r.name;
    EXPECT_EQ(a.raster.bytes(), b.raster.bytes()) << r.name;
    // The edge pixel is partly covered (so the test sees an antialiased edge), and since the plane's pixels equal the
    // path fill's above, its coverage is the clip's once: a coverage weighed twice would be the square, a different byte.
    if (std::string(r.name).find("half") != std::string::npos) {
      std::string edge = a.raster.pixel(8, 5);
      EXPECT_GT((unsigned char)edge[3], 0);
      EXPECT_LT((unsigned char)edge[3], 255);
    }
  }
}

// Two clips weigh a pixel by both: the plane is the intersection filled.
TEST(RenderPlane, UnderTwoClipsItIsTheirIntersection) {
  Store s;
  const GSHSCENE_Resource * plane = plane_of(s);
  GSHSCENE_Paint blue = solid(0, 0, 1, 1);
  GSHSCENE_List * nested = s.open();
  ASSERT_EQ(gshscene_list_clip_push(nested, s.rect(1.5, 1.5, 12.0, 10.0), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(nested, s.rect(4.0, 0.0, 15.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(nested, plane, nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(nested), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(nested), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(nested), GSHSCENE_OK);
  // The same two clips around a fill of a rectangle the size of the target.
  GSHSCENE_List * around = s.open();
  ASSERT_EQ(gshscene_list_clip_push(around, s.rect(1.5, 1.5, 12.0, 10.0), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(around, s.rect(4.0, 0.0, 15.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(around, s.rect(-1, -1, 17, 13), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(around), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(around), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(around), GSHSCENE_OK);
  Drawn a(nested);
  Drawn b(around);
  EXPECT_EQ(a.raster.bytes(), b.raster.bytes());
}

// With no clip it is every pixel of the extent; with a damage, exactly the damage.
TEST(RenderPlane, WithNoClipItIsTheWholeExtentAndAdamageLimitsIt) {
  Store s;
  const GSHSCENE_Resource * plane = plane_of(s);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint p = solid(0, 1, 0, 1);
  ASSERT_EQ(gshscene_list_fill(l, plane, nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Drawn whole(l);
  RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
  for (uint32_t y = 0; y < 12; y++) {
    for (uint32_t x = 0; x < 16; x++) {
      ASSERT_NE(whole.raster.pixel(x, y), fresh.pixel(x, y)) << x << "," << y;
      ASSERT_EQ(whole.raster.pixel(x, y), whole.raster.pixel(0, 0));
    }
  }
  const GSHSCENE_Rect damage{3, 2, 11, 9};
  Drawn part(l, 16, 12, &damage);
  for (uint32_t y = 0; y < 12; y++) {
    for (uint32_t x = 0; x < 16; x++) {
      bool inside = x >= 3 && x < 11 && y >= 2 && y < 9;
      ASSERT_EQ(part.raster.pixel(x, y), inside ? whole.raster.pixel(x, y) : fresh.pixel(x, y)) << x << "," << y;
    }
  }
}

// The item's map is the paint's alone: a gradient plane is sampled at the pixel centers under the item's map composed
// in front of the gradient's, as a fill of any shape that covers the target is.
TEST(RenderPlane, AGradientIsSampledAsUnderAnyFillAndTheMapMovesOnlyThePaint) {
  Store s;
  const GSHSCENE_Resource * plane = plane_of(s);
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 16;
  gshgeom_map_identity(2, &g.map);
  GSHSCENE_Gradient_Stop stops[2] = {
      {0.0, solid(1, 0, 0, 1)},
      {1.0, solid(0, 0, 1, 1)},
  };
  g.stops = stops;
  g.stop_count = 2;
  const GSHSCENE_Resource * grad = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(s.store, &g, &grad), GSHSCENE_OK);
  s.held.push_back(grad);
  GSHSCENE_Paint paint{};
  paint.kind = GSHSCENE_PAINT_GRADIENT;
  paint.gradient = grad;
  paint.rgba[3] = 0.75;
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 4.0, 0.0, 0.0, &move);
  for (const GSHGEOM_Map * map : {static_cast<const GSHGEOM_Map *>(nullptr), static_cast<const GSHGEOM_Map *>(&move)}) {
    GSHSCENE_List * plane_list = s.open();
    ASSERT_EQ(gshscene_list_fill(plane_list, plane, map, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(plane_list), GSHSCENE_OK);
    GSHSCENE_List * rect_list = s.open();
    ASSERT_EQ(gshscene_list_fill(rect_list, s.rect(0, 0, 16, 12), map, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(rect_list), GSHSCENE_OK);
    Drawn a(plane_list);
    Drawn b(rect_list);
    if (!map) {
      EXPECT_EQ(a.raster.bytes(), b.raster.bytes());
    } else {
      // Moved by 4: the rectangle's own region moves with the item map, the plane's does not.
      for (uint32_t y = 0; y < 12; y++) {
        for (uint32_t x = 4; x < 16; x++) {
          EXPECT_EQ(a.raster.pixel(x, y), b.raster.pixel(x, y)) << x << "," << y << ": the same paint where both cover";
        }
        for (uint32_t x = 0; x < 4; x++) {
          RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 12));
          EXPECT_EQ(b.raster.pixel(x, y), fresh.pixel(x, y)) << "the moved rectangle leaves its left edge";
          EXPECT_NE(a.raster.pixel(x, y), fresh.pixel(x, y)) << "the plane is not moved: it still covers the pixel";
        }
      }
    }
  }
}

// A group's opacity and the foreground reach a plane as they reach any fill.
TEST(RenderPlane, InAGroupItIsComposedAsAnyFillIs) {
  Store s;
  const GSHSCENE_Resource * plane = plane_of(s);
  GSHSCENE_Group group{};
  group.opacity = 0.5;
  group.ordering = GSHSCENE_ORDER_FLAT;
  GSHSCENE_Paint paint = fg(1.0);
  GSHSCENE_List * a = s.open();
  ASSERT_EQ(gshscene_list_group_push(a, &group, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(a, plane, nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(a), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(a), GSHSCENE_OK);
  GSHSCENE_List * b = s.open();
  ASSERT_EQ(gshscene_list_group_push(b, &group, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(b, s.rect(-1, -1, 17, 13), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(b), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(b), GSHSCENE_OK);
  Drawn x(a);
  Drawn y(b);
  EXPECT_EQ(x.raster.bytes(), y.raster.bytes());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
