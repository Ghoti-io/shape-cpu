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
 * Patterns in the sink (masks-images-patterns story R2; render.h, PC1 to PC11): the claim, the canonical translation of a copy
 * (PC3), the limit on the copies of a use (PC8) against the test sink's count, a damaged render, the caller's memory, a tile that
 * repeats on one axis only, and a cell-filling background across every seam.  The oracle gate is test_render_pattern_vectors.cpp;
 * the goldens of the pattern fixtures are the fixture-wide tests'.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rh;

namespace {

/** The scene: one pattern filling a rectangle of a target (a tile of two shapes, so that every copy is told from its neighbours). */
struct Case {
  RF_Ctx ctx;
  GSHSCENE_List * list = nullptr;
  const GSHSCENE_Resource * pattern = nullptr;
  Case(double tx, double ty, double step, double x1, double y1, uint32_t rx = 1, uint32_t ry = 1, int opaque = 0) {
    rf_begin(&ctx);
    GSHSCENE_List * t = rf_open(&ctx);
    if (opaque) {
      rf_fill(&ctx, t, rf_rect(&ctx, 0, 0, step, step), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.2, 0.4, 0.8, 1.0), 0, 0.0);
    }
    rf_fill(&ctx, t, rf_rect(&ctx, 0.25 * step, 0.125 * step, 0.75 * step, 0.5 * step), nullptr, GSHGEOM_FILL_NONZERO,
        rf_solid(0.9, 0.3, 0.1, 0.9), 0, 0.0);
    GSHSCENE_Gradient_Stop stops[2] = {rf_stop(0.0, rf_solid(1, 1, 0, 1)), rf_stop(1.0, rf_solid(0, 0.2, 0.9, 1))};
    const GSHSCENE_Resource * grad = rf_gradient(&ctx, rf_linear(0, 0, step, step, 0, 0, GSHSCENE_EXTEND_PAD), stops, 2);
    rf_fill(&ctx, t, rf_rect(&ctx, 0.5 * step, 0.5 * step, 1.25 * step, 1.25 * step), nullptr, GSHGEOM_FILL_NONZERO,
        rf_gradient_paint(grad, 0.8), 0, 0.0);
    const GSHSCENE_Resource * tile = rf_picture_of(&ctx, t);
    pattern = rf_pattern(&ctx, tile, 0, 0, step, step, step, step, rx, ry, rf_move(tx, ty), nullptr);
    list = rf_open(&ctx);
    rf_fill(&ctx, list, rf_rect(&ctx, 0, 0, x1, y1), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pattern, 1.0), 0, 0.0);
    rf_close(&ctx, list);
  }
  ~Case() {
    rf_release(&ctx);
  }
  Case(const Case &) = delete;
  Case & operator=(const Case &) = delete;
};

/** Render @p list on a fresh raster of @p format (w by h); the report's summary is kept. */
struct Drawn {
  RasterRef raster;
  GSHSCENE_Result result = GSHSCENE_OK;
  GSHSCENE_Report_Summary summary{};
  std::vector<GSHSCENE_Report_Entry> entries;
  Drawn(const GSHSCENE_List * list, uint32_t w, uint32_t h, GSHCPU_Format format = GSHCPU_FORMAT_RGBA8_STRAIGHT,
      const GSHCPU_Limits * limits = nullptr, uint32_t flags = 0, const GSHSCENE_Rect * damage = nullptr, void * workspace = nullptr,
      size_t workspace_bytes = 0)
      : raster(rf_raster(format, w, h)) {
    SinkRef sink(limits);
    ReportRef report;
    unsigned char * px = (unsigned char *)gimg_raster_pixels(raster.raster);
    std::memset(px, 0, gimg_raster_stride_bytes(raster.raster) * h);
    GSHSCENE_Target t = target_of(raster.raster);
    GSHSCENE_Rect d{};
    if (damage) {
      d = *damage;
      t.damage = &d;
    }
    GSHSCENE_Render_Request req = request_of(list, &t, report.report);
    req.flags = flags;
    req.workspace = workspace;
    req.workspace_bytes = workspace_bytes;
    result = gshscene_sink_render(sink.sink, &req);
    summary = report.summary();
    entries = report.entries();
  }
};


/** Whether the raster @p a at (x + dx, y + dy) is @p b at (x, y), byte for byte, over the pixels of [x0, x1) by [y0, y1) of @p b. */
bool shifted_equal(const RasterRef & a, const RasterRef & b, int dx, int dy, int x0, int y0, int x1, int y1, std::string * where = nullptr) {
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      if (a.pixel((uint32_t)(x + dx), (uint32_t)(y + dy)) != b.pixel((uint32_t)x, (uint32_t)y)) {
        if (where) {
          *where = std::to_string(x) + "," + std::to_string(y);
        }
        return false;
      }
    }
  }
  return true;
}

TEST(RenderPattern, ACopyIsAFunctionOfTheLinearPartAndTheFraction) {
  // PC3: the pattern at t and at t + (k, m), k and m whole, draws the same pixels shifted by (k, m), bit for bit, in every format;
  // the tile holds a translucent shape and a gradient, so that a copy that was rounded at its own translation would differ.
  for (GSHCPU_Format format : RF_FORMATS) {
    Case a(0.3, 0.45, 8.0, 80, 64);
    Case b(0.3 + 5.0, 0.45 + 3.0, 8.0, 80, 64);
    Drawn da(a.list, 80, 64, format);
    Drawn db(b.list, 80, 64, format);
    ASSERT_EQ(da.result, GSHSCENE_OK);
    ASSERT_EQ(db.result, GSHSCENE_OK);
    std::string where;
    EXPECT_TRUE(shifted_equal(da.raster, db.raster, -5, -3, 20, 20, 70, 56, &where)) << gshcpu_format_string(format) << " at " << where;
    // ... and a different fraction is not the same picture.
    Case c(0.31, 0.45, 8.0, 80, 64);
    Drawn dc(c.list, 80, 64, format);
    EXPECT_FALSE(shifted_equal(da.raster, dc.raster, 0, 0, 20, 20, 70, 56));
  }
}

TEST(RenderPattern, ACopyAtTheIndexAMillionIsTheCopyAtZero) {
  // PC3, PT5: the position of a copy is its index times the step, never an accumulation: with a step of 1/8 pixel (exact in a
  // double), a lattice moved by 125000 pixels is the same lattice 10^6 copies on, and draws the same bits.
  Case near(0.375, 0.25, 0.125, 16, 16);
  Case far(0.375 - 125000.0, 0.25 - 125000.0, 0.125, 16, 16);
  Drawn dn(near.list, 16, 16);
  Drawn df(far.list, 16, 16);
  ASSERT_EQ(dn.result, GSHSCENE_OK);
  ASSERT_EQ(df.result, GSHSCENE_OK);
  EXPECT_EQ(df.summary.by_class[GSHSCENE_ENTRY_REFUSED], 0u);
  EXPECT_EQ(dn.raster.bytes(), df.raster.bytes());
}

/** The copies LT1 counts for an abutting lattice of cells of @p step at (tx, ty) under a closed rectangle of the whole target. */
uint64_t copies_of(double tx, double ty, double step, int64_t w, int64_t h) {
  GSHGEOM_Lattice lat{0, 0, step, step, step, step, 1, 1};
  GSHGEOM_Map map;
  gshgeom_map_translate(2, tx, ty, 0.0, &map);
  const int64_t rect[4] = {0, 0, w, h};
  uint64_t n = 0;
  EXPECT_EQ(gshgeom_lattice_each(&map, &lat, rect, UINT64_MAX >> 12, nullptr, nullptr, &n), GSHGEOM_OK);
  return n;
}

TEST(RenderPattern, TheCopiesOfAUseAreLimitedAtTheCountAndAnAtMostPassesOneMoreRefuses) {
  // PC8: the number of copies, counted over what the whole render draws of the use, is at most max_pattern_cells; one more is
  // RESOURCE_LIMIT, the use is skipped (strict stops, with the target untouched), and the count is the one the lattice enumeration
  // makes, which is the test sink's `copies=`.
  Case c(0.0, 0.0, 4.0, 16, 16);
  const uint64_t n = copies_of(0.0, 0.0, 4.0, 16, 16);
  EXPECT_EQ(n, 36u) << "a cell that only touches the rectangle is a copy: the lattice has the cells -1 to 4 on each axis";
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_pattern_cells = n;
  Drawn at(c.list, 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, &limits, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(at.result, GSHSCENE_OK);
  EXPECT_EQ(at.summary.by_class[GSHSCENE_ENTRY_REFUSED], 0u);
  Drawn open(c.list, 16, 16);
  EXPECT_EQ(at.raster.bytes(), open.raster.bytes()) << "the default limit draws the same";
  limits.max_pattern_cells = n - 1;
  Drawn over(c.list, 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, &limits, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(over.result, GSHSCENE_ERR_REFUSED);
  ASSERT_EQ(over.entries.size(), 1u);
  EXPECT_EQ(over.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(over.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  RasterRef blank(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 16, 16));
  Drawn skipped(c.list, 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, &limits, 0);
  EXPECT_EQ(skipped.result, GSHSCENE_OK);
  EXPECT_EQ(skipped.entries.size(), 1u);
  const std::string untouched = std::string(16 * 4, '\0');
  EXPECT_EQ(skipped.raster.bytes().substr(0, 16 * 4), untouched) << "the use is skipped";
  // The count is of the use, not of what is drawn: the same refusal under a damage.
  GSHSCENE_Rect damage{4, 4, 8, 8};
  Drawn damaged(c.list, 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, &limits, GSHSCENE_RENDER_STRICT, &damage);
  EXPECT_EQ(damaged.result, GSHSCENE_ERR_REFUSED);
  // A step far below a pixel under a full-target fill is refused promptly, not drawn.
  Case tiny(0.0, 0.0, 1.0 / 1024.0, 16, 16);
  Drawn t(tiny.list, 16, 16);
  EXPECT_EQ(t.result, GSHSCENE_OK);
  EXPECT_EQ(t.entries.size(), 1u);
  EXPECT_EQ(t.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT) << "256 Mi copies are past the default";
}

TEST(RenderPattern, ADamagedRenderIsTheWholeRenderInsideTheDamage) {
  // PC11: a pixel inside the damage is the pixel of the whole render, and nothing outside it is touched.
  for (GSHCPU_Format format : RF_FORMATS) {
    Case c(0.3, 0.45, 7.0, 48, 40, 1, 1, 1);
    Drawn whole(c.list, 48, 40, format);
    GSHSCENE_Rect damage{9, 11, 31, 29};
    Drawn part(c.list, 48, 40, format, nullptr, 0, &damage);
    ASSERT_EQ(whole.result, GSHSCENE_OK);
    ASSERT_EQ(part.result, GSHSCENE_OK);
    for (int y = 0; y < 40; y++) {
      for (int x = 0; x < 48; x++) {
        const bool in = x >= 9 && x < 31 && y >= 11 && y < 29;
        if (in) {
          EXPECT_EQ(part.raster.pixel((uint32_t)x, (uint32_t)y), whole.raster.pixel((uint32_t)x, (uint32_t)y)) << x << "," << y;
        } else {
          EXPECT_EQ(part.raster.pixel((uint32_t)x, (uint32_t)y), std::string(gimg_raster_bytes_per_pixel(gimg_raster_format(part.raster.raster)), '\0'));
        }
      }
    }
  }
}

TEST(RenderPattern, TheCallersMemoryDrawsTheSamePixelsAndOneByteLessIsALimit) {
  // PC11: the caller's block is the size reported, nothing is allocated, and the pixels are the same; the tile holds a stroke and a nested pattern.
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "pattern-", 8) != 0) {
      continue;
    }
    Fixture fx(i);
    SinkRef sink;
    ReportRef report;
    RasterRef plain(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target tp = target_of(plain.raster, &fx.ctx.target_map);
    GSHSCENE_Render_Request rp = request_of(fx.list, &tp, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &rp), GSHSCENE_OK) << RF_FIXTURES[i].name;
    RasterRef own(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target to = target_of(own.raster, &fx.ctx.target_map);
    GSHSCENE_Render_Request ro = request_of(fx.list, &to, report.report);
    ro.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t need = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &ro, &need), GSHSCENE_OK) << RF_FIXTURES[i].name;
    std::vector<unsigned char> memory(need + 64);
    ro.workspace = memory.data();
    ro.workspace_bytes = need;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &ro), GSHSCENE_OK) << RF_FIXTURES[i].name;
    EXPECT_EQ(own.bytes(), plain.bytes()) << RF_FIXTURES[i].name;
    ro.workspace_bytes = need - 1;
    RasterRef less(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target tl = target_of(less.raster, &fx.ctx.target_map);
    ro.target = &tl;
    EXPECT_EQ(gshscene_sink_render(sink.sink, &ro), GSHSCENE_ERR_LIMIT) << RF_FIXTURES[i].name;
  }
}

TEST(RenderPattern, ABackgroundThatFillsItsCellCoversEverySeamOnce) {
  // PT6, PC4: where cells abut, a cell-filling background has coverage 255 on every pixel of the lattice, across every edge, however
  // the lattice sits; compositing the copies one after another would leave 1 - (1 - a)(1 - b) there.
  const double fractions[5][2] = {{0.0, 0.0}, {0.5, 0.5}, {0.3, 0.45}, {0.01, 0.99}, {0.7, 0.2}};
  for (const auto & f : fractions) {
    Case c(f[0], f[1], 5.5, 60, 48, 1, 1, 1);
    Drawn d(c.list, 60, 48);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    for (int y = 0; y < 48; y++) {
      for (int x = 0; x < 60; x++) {
        const unsigned char a = (unsigned char)d.raster.pixel((uint32_t)x, (uint32_t)y)[3];
        EXPECT_EQ(a, 255) << "at " << x << "," << y << " fraction " << f[0] << "," << f[1];
      }
    }
  }
}

TEST(RenderPattern, ARepeatOfZeroDrawsTheIndexZeroCopiesOnly) {
  // PT1: repeat_x 0 draws the column of index 0 only; the copies of that axis beyond it are not there, nor are they counted.
  Case c(10.0, 0.0, 8.0, 48, 40, 0, 1, 1);
  Drawn d(c.list, 48, 40);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  for (int y = 0; y < 40; y++) {
    for (int x = 0; x < 48; x++) {
      const unsigned char a = (unsigned char)d.raster.pixel((uint32_t)x, (uint32_t)y)[3];
      if (x < 10 || x >= 18) {
        EXPECT_EQ(a, 0) << x << "," << y;
      } else if (x >= 11 && x < 17) {
        EXPECT_EQ(a, 255) << x << "," << y;
      }
    }
  }
}

TEST(RenderPattern, ThePatternFixturesAreDrawnWithNothingRefused) {
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "pattern-", 8) != 0) {
      continue;
    }
    Fixture fx(i);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster, &fx.ctx.target_map);
    GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << RF_FIXTURES[i].name;
    EXPECT_EQ(report.summary().by_class[GSHSCENE_ENTRY_REFUSED], 0u) << RF_FIXTURES[i].name;
  }
}

TEST(RenderPattern, DumpFixtures) {
  const char * dir = std::getenv("PATTERN_PPM_DIR");
  if (!dir || !*dir) {
    GTEST_SKIP() << "PATTERN_PPM_DIR is not set";
  }
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "pattern-", 8) != 0) {
      continue;
    }
    GIMG_Raster * r = nullptr;
    uint64_t h = 0;
    ASSERT_EQ(rf_render(i, GSHCPU_FORMAT_RGBA8_STRAIGHT, &h, &r), GSHSCENE_OK) << RF_FIXTURES[i].name;
    std::string path = std::string(dir) + "/" + RF_FIXTURES[i].name + ".ppm";
    FILE * f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fprintf(f, "P6\n%u %u\n255\n", RF_WIDTH, RF_HEIGHT);
    const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(r);
    for (uint32_t y = 0; y < RF_HEIGHT; y++) {
      for (uint32_t x = 0; x < RF_WIDTH; x++) {
        const unsigned char * p = px + y * gimg_raster_stride_bytes(r) + x * 4u;
        unsigned char rgb[3] = {p[0], p[1], p[2]};
        std::fwrite(rgb, 1, 3, f);
      }
    }
    std::fclose(f);
    gimg_raster_destroy(r);
  }
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
