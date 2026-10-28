/**
 * @file
 *
 * The CPU sink's limits, cancel, caller memory and allocation failure (story
 * 13b): each limit at its bound and one past it, a cancelled render's prefix of
 * paint order against the render of that prefix, the work between two checks
 * on a hostile maximal item, the exact workspace and frames that allocate
 * nothing, the bytes of it a render touches (the pixels its units reach, not
 * the region), and a failing allocator at every call of every fixture's render.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "render_helpers.h"

#include <algorithm>

using namespace rh;

namespace {

struct Outcome {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  GSHSCENE_Report_Summary summary{};
  std::vector<GSHSCENE_Report_Entry> entries;
  std::vector<std::vector<size_t>> chains;  // each entry's chain, copied (the report's are gone with it)
  std::vector<size_t> failure;               // the failure's chain, copied
};

Outcome render(GSHSCENE_Sink * sink, const GSHSCENE_List * list, GIMG_Raster * raster, uint32_t flags = 0,
    const GSHGEOM_Map * map = nullptr) {
  ReportRef report;
  GSHSCENE_Target t = target_of(raster, map);
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  Outcome r;
  r.result = gshscene_sink_render(sink, &req);
  r.summary = report.summary();
  r.entries = report.entries();
  for (const GSHSCENE_Report_Entry & e : r.entries) {
    r.chains.emplace_back(e.chain, e.chain + e.chain_count);
  }
  r.failure.assign(r.summary.failure.chain, r.summary.failure.chain + r.summary.failure.chain_count);
  for (GSHSCENE_Report_Entry & e : r.entries) {
    e.chain = nullptr;
  }
  r.summary.failure.chain = nullptr;
  return r;
}

/** The deposits and the points of item @p index of @p list over the whole of a raster's region. */
void measure(const GSHSCENE_List * list, size_t index, int64_t width, int64_t height, uint64_t * cells, size_t * points) {
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list, index, &item), GSHSCENE_OK);
  GSHGEOM_Map maps[4];
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  GSHCPU_Item_Geometry g;
  const GSHGEOM_Allocator * alloc = gshgeom_allocator_default();
  ASSERT_EQ(gshcpu_render_geometry(item, maps, 0, &identity, alloc, &limits, &g), GSHCPU_OK);
  GSHCPU_Rect region{0, 0, width, height};
  GSHCPU_Rect rect = gshcpu_render_rect_meet(g.box, region);
  std::vector<uint8_t> out((size_t)(width * height));
  std::vector<int64_t> acc(2 * (size_t)(width + 1) * GSHCPU_RENDER_BAND_ROWS);
  *cells = 0;
  ASSERT_EQ(gshcpu_render_cover((const GSHGEOM_Flat_Path * const *)g.paths, g.count, item->fill_rule, rect, out.data(),
                acc.data(), UINT64_MAX, cells),
      GSHCPU_OK);
  *points = g.points;
  gshcpu_render_geometry_release(&g, alloc);
}

/** What a render refused and where it stopped: each entry's class, reason, second and chain; the result; the stop. */
struct Refusals {
  std::vector<std::vector<size_t>> rows;
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::vector<size_t> failure;
  uint64_t work_max = 0;
  bool operator==(const Refusals & o) const {
    return rows == o.rows && result == o.result && failure == o.failure;
  }
};

/** Render @p list into @p raster with @p damage (NULL: the whole render) and @p flags; what it refused. */
Refusals render_damaged(GSHSCENE_Sink * sink, const GSHSCENE_List * list, GIMG_Raster * raster,
    const GSHSCENE_Rect * damage, uint32_t flags = 0) {
  ReportRef report;
  GSHSCENE_Target t = target_of(raster);
  t.damage = damage;
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  Refusals out;
  out.result = gshscene_sink_render(sink, &req);
  for (const GSHSCENE_Report_Entry & e : report.entries()) {
    std::vector<size_t> row{(size_t)e.entry_class, (size_t)e.reason, e.second};
    row.insert(row.end(), e.chain, e.chain + e.chain_count);
    out.rows.push_back(row);
  }
  GSHSCENE_Report_Summary sum = report.summary();
  out.failure.assign(sum.failure.chain, sum.failure.chain + sum.failure.chain_count);
  out.work_max = sum.work_max;
  return out;
}

/** The deposits of item @p index of @p list (no instances) over the pixels of its box that meet @p rect. */
uint64_t cells_in(const GSHSCENE_List * list, size_t index, GSHCPU_Rect rect) {
  const GSHSCENE_Item * item = nullptr;
  EXPECT_EQ(gshscene_list_item(list, index, &item), GSHSCENE_OK);
  GSHGEOM_Map maps[4];
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  GSHCPU_Item_Geometry g;
  const GSHGEOM_Allocator * alloc = gshgeom_allocator_default();
  EXPECT_EQ(gshcpu_render_geometry(item, maps, 0, &identity, alloc, &limits, &g), GSHCPU_OK);
  GSHCPU_Rect r = gshcpu_render_rect_meet(g.box, rect);
  uint64_t cells = 0;
  if (!gshcpu_render_rect_empty(r)) {
    std::vector<uint8_t> out((size_t)((r.x1 - r.x0) * (r.y1 - r.y0)));
    std::vector<int64_t> acc(2 * (size_t)(r.x1 - r.x0 + 1) * GSHCPU_RENDER_BAND_ROWS);
    EXPECT_EQ(gshcpu_render_cover((const GSHGEOM_Flat_Path * const *)g.paths, g.count, item->fill_rule, r, out.data(),
                  acc.data(), UINT64_MAX, &cells),
        GSHCPU_OK);
  }
  gshcpu_render_geometry_release(&g, alloc);
  return cells;
}

/** An item's box, its points, and the cell bound over its box meet @p walk (no instances). */
struct ItemFacts {
  GSHCPU_Rect box{0, 0, 0, 0};
  size_t points = 0;
  uint64_t bound = 0;
};

ItemFacts facts_of(const GSHSCENE_List * list, size_t index, GSHCPU_Rect walk) {
  const GSHSCENE_Item * item = nullptr;
  EXPECT_EQ(gshscene_list_item(list, index, &item), GSHSCENE_OK);
  GSHGEOM_Map maps[4];
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  GSHCPU_Item_Geometry g;
  const GSHGEOM_Allocator * alloc = gshgeom_allocator_default();
  EXPECT_EQ(gshcpu_render_geometry(item, maps, 0, &identity, alloc, &limits, &g), GSHCPU_OK);
  ItemFacts f;
  f.box = g.box;
  f.points = g.points;
  f.bound = gshcpu_render_cells_bound((const GSHGEOM_Flat_Path * const *)g.paths, g.count,
      gshcpu_render_rect_meet(g.box, walk), UINT64_MAX);
  gshcpu_render_geometry_release(&g, alloc);
  return f;
}

/** A diamond of radius @p r about (@p cx, @p cy): every edge crosses columns and rows alike. */
const GSHSCENE_Resource * diamond(Store & s, double cx, double cy, double r) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{cx, cy - r, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{cx + r, cy + 0.25, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{cx + 0.5, cy + r, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{cx - r, cy - 0.25, 0});
  gshgeom_path_close(p);
  return s.path(p);
}

/** A zigzag of @p n points across the raster (lines only, so the flattened path holds exactly n points). */
const GSHSCENE_Resource * zigzag(Store & s, int n, double w, double h) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  for (int i = 0; i < n; i++) {
    GSHGEOM_Point q{w * (double)i / (double)(n - 1), (i % 2) ? h - 0.5 : 0.5, 0};
    if (i == 0) {
      gshgeom_path_move_to(p, q);
    } else {
      gshgeom_path_line_to(p, q);
    }
  }
  gshgeom_path_close(p);
  return s.path(p);
}

} // namespace

/* --- the region's limits -------------------------------------------------------------------------- */

TEST(RenderLimits, TheRegionIsBoundedByMaxDimensionAndMaxBytes) {
  Fixture fx(0);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_dimension = 16;
  SinkRef sink(&limits);
  for (int64_t extra : {0, 1}) {
    for (int axis = 0; axis < 2; axis++) {
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      ReportRef report;
      GSHSCENE_Target t = target_of(r.raster);
      t.extent = axis ? GSHSCENE_Rect{3, 2, 19, 18 + extra} : GSHSCENE_Rect{3, 2, 19 + extra, 18};
      GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
      GSHSCENE_Result got = gshscene_sink_render(sink.sink, &req);
      if (extra) {
        EXPECT_EQ(got, GSHSCENE_ERR_LIMIT) << axis;
        EXPECT_EQ(r.bytes(), fresh.bytes());
        EXPECT_EQ(report.summary().units_processed, 0u);
      } else {
        EXPECT_EQ(got, GSHSCENE_OK) << axis;
      }
    }
  }
  // The same through a damage rectangle: the region is what is drawn.
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  ReportRef report;
  GSHSCENE_Rect damage{0, 0, 16, 16};
  GSHSCENE_Target t = target_of(r.raster);
  t.damage = &damage;
  GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  // A max_dimension past the ceiling is the ceiling.
  limits.max_dimension = SIZE_MAX;
  SinkRef wide(&limits);
  t.damage = nullptr;
  req.target = &t;
  EXPECT_EQ(gshscene_sink_render(wide.sink, &req), GSHSCENE_OK);

  gshcpu_limits_default(&limits);
  limits.max_bytes = 16 * 16;
  SinkRef small(&limits);
  for (int64_t extra : {0, 1}) {
    RasterRef q(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target tq = target_of(q.raster);
    tq.extent = extra ? GSHSCENE_Rect{0, 0, 257, 1} : GSHSCENE_Rect{0, 0, 16, 16};
    RasterRef wide_raster(extra ? rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 257, 1) : nullptr);
    if (extra) {
      tq.surface = wide_raster.raster;
    }
    GSHSCENE_Render_Request rq = request_of(fx.list, &tq, report.report);
    EXPECT_EQ(gshscene_sink_render(small.sink, &rq), extra ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK);
    size_t need = 0;
    rq.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    EXPECT_EQ(gshscene_sink_workspace_bytes(small.sink, &rq, &need), extra ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK);
  }
}

/* --- an item's limits: refused while drawing (S6) -------------------------------------------------------- */

TEST(RenderLimits, CellsAndPointsPassAtTheirBoundAndAreRefusedOnePast) {
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_Paint blue = solid(0, 0, 1, 1);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 1, 9, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 64, 40.0, 30.0), nullptr, GSHGEOM_FILL_EVEN_ODD, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(30, 30, 40, 38), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  uint64_t cells = 0;
  size_t points = 0;
  measure(l, 1, RF_WIDTH, RF_HEIGHT, &cells, &points);
  ASSERT_EQ(points, 64u);
  uint64_t rect_cells = 0;
  size_t rect_points = 0;
  measure(l, 0, RF_WIDTH, RF_HEIGHT, &rect_cells, &rect_points);
  ASSERT_LT(rect_cells, cells);
  ASSERT_GT(cells, 100u);

  // The same list without the item, and the prefix before it: what best effort and strict leave.
  GSHSCENE_List * without = s.open();
  GSHSCENE_List * before = s.open();
  for (GSHSCENE_List * m : {without, before}) {
    ASSERT_EQ(gshscene_list_fill(m, s.rect(1, 1, 9, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_fill(without, s.rect(30, 30, 40, 38), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(without), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(before), GSHSCENE_OK);
  RasterRef golden(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef skipped(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef prefixed(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  {
    SinkRef sink;
    ASSERT_EQ(render(sink.sink, l, golden.raster).result, GSHSCENE_OK);
    ASSERT_EQ(render(sink.sink, without, skipped.raster).result, GSHSCENE_OK);
    ASSERT_EQ(render(sink.sink, before, prefixed.raster).result, GSHSCENE_OK);
  }
  for (int which = 0; which < 2; which++) {
    for (uint64_t less : {0, 1}) {
      GSHCPU_Limits limits;
      gshcpu_limits_default(&limits);
      if (which == 0) {
        limits.max_deposits = (size_t)(cells - less);
      } else {
        limits.max_points = points - (size_t)less;
      }
      SinkRef sink(&limits);
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      Outcome run = render(sink.sink, l, r.raster);
      EXPECT_EQ(run.result, GSHSCENE_OK) << which << " " << less;
      if (!less) {
        EXPECT_EQ(r.bytes(), golden.bytes()) << which;
        EXPECT_TRUE(run.entries.empty());
        continue;
      }
      // Best effort: the item is skipped with an entry, the rest is drawn.
      ASSERT_EQ(run.entries.size(), 1u) << which;
      EXPECT_EQ(run.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
      EXPECT_EQ(run.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
      EXPECT_EQ(run.chains[0], std::vector<size_t>{1});
      EXPECT_EQ(run.summary.units_done, 3u);
      EXPECT_EQ(r.bytes(), skipped.bytes());
      EXPECT_NE(r.bytes(), golden.bytes());
      // Strict: the render stops at the item, the finished prefix in the target, the item named.
      RasterRef q(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      Outcome strict = render(sink.sink, l, q.raster, GSHSCENE_RENDER_STRICT);
      EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
      EXPECT_EQ(strict.summary.units_processed, 1u);
      EXPECT_EQ(strict.summary.units_done, 1u);
      EXPECT_EQ(strict.failure, std::vector<size_t>{1});
      EXPECT_EQ(q.bytes(), prefixed.bytes()) << "the prefix is in the target, and nothing after the refusal";
      EXPECT_NE(q.bytes(), fresh.bytes());
      ASSERT_EQ(strict.entries.size(), 1u);
      EXPECT_EQ(strict.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
    }
  }
}

TEST(RenderLimits, ARefusedClipSkipsItsBlockAndARefusedGroupItemTheGroupGoesOn) {
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_Group g{};
  g.opacity = 0.5;
  ASSERT_EQ(gshscene_list_clip_push(l, zigzag(s, 40, 46, 38), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 48, 40), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 40, 46, 38), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(2, 2, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 39;
  SinkRef sink(&limits);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  Outcome run = render(sink.sink, l, r.raster);
  EXPECT_EQ(run.result, GSHSCENE_OK);
  ASSERT_EQ(run.entries.size(), 2u);
  EXPECT_EQ(run.chains[0], std::vector<size_t>{0});
  EXPECT_EQ(run.chains[1], std::vector<size_t>{4});
  EXPECT_EQ(r.pixel(20, 30), fresh.pixel(20, 30)) << "the fill inside the refused clip is skipped with it";
  EXPECT_NE(r.pixel(3, 3), fresh.pixel(3, 3)) << "the group goes on after its refused fill";
  EXPECT_EQ(run.summary.units_processed, 7u);
}

TEST(RenderLimits, TheRegionLiesWithinTwoToThe24PixelsOfTheOrigin) {
  // A raster one pixel past the ceiling (64 MiB): its last columns are drawn up to the ceiling and not past it.
  const uint32_t width = (1u << 24) + 1u;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, width, 1));
  ASSERT_NE(r.raster, nullptr);
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(16777210.0, 0, 16777216.0, 1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  SinkRef sink;
  for (int64_t past : {0, 1}) {
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster);
    t.extent = GSHSCENE_Rect{(1 << 24) - 4 + past, 0, (1 << 24) + past, 1};
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    EXPECT_EQ(gshscene_sink_render(sink.sink, &req), past ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK) << past;
  }
  EXPECT_EQ(r.pixel((1u << 24) - 1u, 0), std::string("\xff\0\0\xff", 4));
  // And down: a raster one pixel taller than the ceiling.
  RasterRef tall(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 1, (1u << 24) + 1u));
  ASSERT_NE(tall.raster, nullptr);
  for (int64_t past : {0, 1}) {
    ReportRef report;
    GSHSCENE_Target t = target_of(tall.raster);
    t.extent = GSHSCENE_Rect{0, (1 << 24) - 4 + past, 1, (1 << 24) + past};
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    EXPECT_EQ(gshscene_sink_render(sink.sink, &req), past ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK) << past;
  }
}

TEST(RenderLimits, ACoordinateMoreThanTwoToThe24PixelsOutIsRefused) {
  const double far = 16777216.0;
  for (int side = 0; side < 4; side++) {
    for (double past : {0.0, 1.0}) {
      Store s;
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint red = solid(1, 0, 0, 1);
      double x0 = 2;
      double y0 = 2;
      double x1 = 10;
      double y1 = 10;
      if (side == 0) {
        x1 = far + past;
      } else if (side == 1) {
        y1 = far + past;
      } else if (side == 2) {
        x0 = -far - past;
      } else {
        y0 = -far - past;
      }
      ASSERT_EQ(gshscene_list_fill(l, s.rect(x0, y0, x1, y1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      SinkRef sink;
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, 12, 12));
      Outcome run = render(sink.sink, l, r.raster);
      EXPECT_EQ(run.result, GSHSCENE_OK);
      EXPECT_EQ(run.entries.size(), past ? 1u : 0u) << side;
      if (!past) {
        // Drawn: the pixel inside is the paint, through the folded columns and the skipped rows.
        float px[4];
        std::memcpy(px, (const unsigned char *)gimg_raster_pixels_const(r.raster) + 5 * gimg_raster_stride_bytes(r.raster) + 5 * 16,
            sizeof px);
        EXPECT_EQ(px[0], 1.0f) << side;
        EXPECT_EQ(px[3], 1.0f) << side;
      }
    }
  }
}

TEST(RenderLimits, AHostileMaximalItemDoesItsWorkWithinTheDeclaredBound) {
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 4096, 48.0, 40.0), nullptr, GSHGEOM_FILL_EVEN_ODD, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  uint64_t cells = 0;
  size_t points = 0;
  measure(l, 0, RF_WIDTH, RF_HEIGHT, &cells, &points);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = points;
  limits.max_deposits = (size_t)cells;
  limits.max_bytes = RF_WIDTH * RF_HEIGHT;
  SinkRef sink(&limits);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Sink_Caps caps{};
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
  // SC11: the worst unit is an aligned stroke: three parts' points, the deposits of all, two passes over the pixels, and
  // the expansion's steps for every element a resource may hold, and one image item's texels (IC9).
  EXPECT_EQ(caps.cancel_work_bound, 3 * points + cells + 2 * RF_WIDTH * RF_HEIGHT +
      (uint64_t)GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS * (GSHGEOM_STROKE_MAX_DEPTH + 1) +
      gshcpu_exact_work_bound(cells, 3 * points, GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION, 3 * GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS,
      GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS) + GSHCPU_LIMITS_DEFAULT_MAX_IMAGE_WORK);
  Outcome run = render(sink.sink, l, r.raster);
  ASSERT_EQ(run.result, GSHSCENE_OK);
  EXPECT_TRUE(run.entries.empty());
  EXPECT_LE(run.summary.work_max, caps.cancel_work_bound);
  EXPECT_GE(run.summary.work_max, points + cells) << "the item's points and cells are counted";
  EXPECT_EQ(run.summary.work_max, points + cells + RF_WIDTH * RF_HEIGHT) << "and every pixel of its box, the raster here";
}

TEST(RenderLimits, TheSinksMemoryDoesNotDependOnTheCapOfDeposits) {
  // max_deposits counts work: the sink adds each crossing straight into a dense accumulator for a band of rows and stores no
  // cell, so the memory it asks for is the same whatever the cap is, and a render that makes more deposits than a small
  // cap would allow is refused by the cap and not by the memory.
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 4096, 48.0, 40.0), nullptr, GSHGEOM_FILL_EVEN_ODD, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  uint64_t cells = 0;
  size_t points = 0;
  measure(l, 0, RF_WIDTH, RF_HEIGHT, &cells, &points);
  ASSERT_GT(cells, 1000u) << "the list makes many deposits";
  size_t need[3] = {0, 0, 0};
  const uint64_t caps[3] = {1, GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS, UINT64_MAX};
  for (int i = 0; i < 3; i++) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = caps[i];
    SinkRef sink(&limits);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need[i]), GSHSCENE_OK);
  }
  EXPECT_EQ(need[0], need[1]);
  EXPECT_EQ(need[1], need[2]);
  GSHCPU_Limits one;
  gshcpu_limits_default(&one);
  one.max_deposits = 1;
  SinkRef sink(&one);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  EXPECT_FALSE(render(sink.sink, l, r.raster).entries.empty()) << "a cap of one deposit refuses the item";
}

TEST(RenderLimits, ARefusedBlockSkipsItsNestedBlocksAndEndsAtItsOwnPop) {
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_Paint blue = solid(0, 0, 1, 1);
  GSHSCENE_Group g{};
  g.opacity = 1.0;
  ASSERT_EQ(gshscene_list_clip_push(l, zigzag(s, 40, 46, 38), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l, s.rect(0, 0, 48, 40), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 20, 20), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(20, 20, 30, 30), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(30, 0, 40, 10), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(40, 30, 48, 40), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHSCENE_List * after = s.open();
  ASSERT_EQ(gshscene_list_fill(after, s.rect(40, 30, 48, 40), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(after), GSHSCENE_OK);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 39;
  SinkRef sink(&limits);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef want(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  Outcome run = render(sink.sink, l, r.raster);
  ASSERT_EQ(render(sink.sink, after, want.raster).result, GSHSCENE_OK);
  EXPECT_EQ(run.result, GSHSCENE_OK);
  ASSERT_EQ(run.entries.size(), 1u);
  EXPECT_EQ(r.bytes(), want.bytes()) << "everything in the refused clip's block is skipped, and nothing after it";
  EXPECT_EQ(run.summary.units_processed, gshscene_list_count(l));
  EXPECT_EQ(run.summary.units_done, gshscene_list_count(l));
}

TEST(RenderLimits, DeepInNestedPicturesTheChainNamesTheItem) {
  // A glyph run (two maps more than its item: its glyph's translation) in a picture instanced by a picture.
  Store s;
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_Store * own = c.store;
  c.store = s.store;
  GSHSCENE_Glyph glyphs[2] = {{82, 0.0, 0.0, rf_glyph(&c, 0)}, {89, 13.0, 0.0, rf_glyph(&c, 1)}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * text = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &text), GSHSCENE_OK);
  s.held.push_back(text);
  GSHSCENE_Paint ink = solid(0.2, 0.1, 0.6, 1.0);
  GSHGEOM_Map one;
  GSHGEOM_Map two;
  GSHGEOM_Map three;
  gshgeom_map_translate(2, 1.0, 2.0, 0.0, &one);
  gshgeom_map_translate(2, 3.0, 4.0, 0.0, &two);
  gshgeom_map_translate(2, 5.0, 6.0, 0.0, &three);
  GSHSCENE_List * inner = s.open();
  ASSERT_EQ(gshscene_list_fill(inner, s.rect(0, 0, 2, 2), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(inner, text, &three, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(inner, zigzag(s, 400, 30, 20), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner), GSHSCENE_OK);
  const GSHSCENE_Resource * p1 = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner, &p1), GSHSCENE_OK);
  s.held.push_back(p1);
  GSHSCENE_List * middle = s.open();
  ASSERT_EQ(gshscene_list_instance(middle, p1, &two, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(middle), GSHSCENE_OK);
  const GSHSCENE_Resource * p2 = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, middle, &p2), GSHSCENE_OK);
  s.held.push_back(p2);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, s.rect(40, 30, 44, 34), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_instance(l, p2, &one, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  // The same drawing, flat: the translations are whole numbers, so composed they are exact.
  GSHGEOM_Map at_glyphs;
  gshgeom_map_translate(2, 9.0, 12.0, 0.0, &at_glyphs);
  GSHGEOM_Map at_square;
  gshgeom_map_translate(2, 4.0, 6.0, 0.0, &at_square);
  GSHSCENE_List * flat = s.open();
  ASSERT_EQ(gshscene_list_fill(flat, s.rect(40, 30, 44, 34), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(flat, s.rect(0, 0, 2, 2), &at_square, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(flat, text, &at_glyphs, GSHGEOM_FILL_NONZERO, &ink, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(flat), GSHSCENE_OK);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 399; // the zigzag, last in the inner picture, is refused; every glyph's outline passes
  SinkRef sink(&limits);
  RasterRef a(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef b(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  Outcome got = render(sink.sink, l, a.raster);
  ASSERT_EQ(render(sink.sink, flat, b.raster).result, GSHSCENE_OK);
  EXPECT_EQ(got.result, GSHSCENE_OK);
  EXPECT_EQ(a.bytes(), b.bytes()) << "the nested pictures draw as the flat list";
  ASSERT_EQ(got.chains.size(), 1u);
  EXPECT_EQ(got.chains[0], (std::vector<size_t>{1, 0, 2}));
  RasterRef q(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  Outcome strict = render(sink.sink, l, q.raster, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(strict.failure, (std::vector<size_t>{1, 0, 2}));
  EXPECT_EQ(q.bytes(), b.bytes()) << "everything before the refusal is in the target";
  EXPECT_STREQ(strict.summary.failure.reason, "strict: refused while drawing (the sink's resource limit, or a glyph)");
  for (size_t i = 0; i < c.held_count; i++) {
    gshscene_resource_release(c.held[i]);
  }
  gshscene_store_destroy(own);
}

TEST(RenderLimits, TheWorkOfAUnitIsItsPointsCellsAndPixels) {
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  // One small fill: its box is 2..7 by 3..7 (rounded out), 20 pixels.
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, s.rect(2.5, 3.5, 6.25, 7.0), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  uint64_t cells = 0;
  size_t points = 0;
  measure(l, 0, RF_WIDTH, RF_HEIGHT, &cells, &points);
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  Outcome o = render(sink.sink, l, r.raster);
  ASSERT_EQ(o.result, GSHSCENE_OK);
  EXPECT_EQ(o.summary.work_max, points + cells + 20u);
  // A box with a negative corner: the floor rounds out, not toward zero.
  GSHSCENE_List * n = s.open();
  ASSERT_EQ(gshscene_list_fill(n, s.rect(-1.5, -2.25, 3.5, 2.0), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(n), GSHSCENE_OK);
  measure(n, 0, RF_WIDTH, RF_HEIGHT, &cells, &points);
  o = render(sink.sink, n, r.raster);
  EXPECT_EQ(o.summary.work_max, points + cells + 4u * 2u) << "the box meets the region: 0..4 by 0..2";
  // A group: its push clears a layer of the region; each unit's work is its own, not the sum so far.
  GSHSCENE_Group g{};
  g.opacity = 0.5;
  GSHSCENE_List * m = s.open();
  ASSERT_EQ(gshscene_list_group_push(m, &g, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(m, s.rect(2.5, 3.5, 6.25, 7.0), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(m), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(m), GSHSCENE_OK);
  o = render(sink.sink, m, r.raster);
  EXPECT_EQ(o.summary.work_max, (uint64_t)RF_WIDTH * RF_HEIGHT);
}

TEST(RenderLimits, ACellCapRefusesTheSameItemsWithAndWithoutTheDamage) {
  // S4: whether an item is refused for the cell cap does not depend on the damage; the cap is met over what the whole
  // render draws of the item (the extent, and the clips' reach in it).  Story 16's soak found a damage-limited render
  // counting only the cells inside the damage: under a cap between the two counts an item across the damage's edge,
  // or wholly outside it, was refused by the whole render and drawn (or passed over) by the damage-limited one.
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 64, 40.0, 30.0), nullptr, GSHGEOM_FILL_EVEN_ODD, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(36.5, 28.5, 47.5, 39.5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l, s.rect(4, 4, 44, 36), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 48, 40), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(12, 10, 20, 16), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Rect damage{10, 8, 30, 24};
  const GSHCPU_Rect region{0, 0, RF_WIDTH, RF_HEIGHT};
  const uint64_t across = cells_in(l, 0, region);     // the zigzag, across the damage
  const uint64_t inside = cells_in(l, 0, damage);
  const uint64_t outside = cells_in(l, 1, region);    // the rectangle wholly outside the damage
  ASSERT_LT(inside + 10, across) << "most of the zigzag's cells lie outside the damage";
  ASSERT_EQ(cells_in(l, 1, damage), 0u);
  ASSERT_GT(outside, 10u);
  ASSERT_LT(outside, inside);
  // Caps where a count inside the damage and one over the whole differ: each item refused by the whole render is
  // refused by the damage-limited one, under best effort and under strict; inside the damage the pixels are the
  // whole render's, outside it the target's.
  size_t refusing = 0;
  for (uint64_t cap : {outside - 1, outside, inside, (across + inside) / 2, across - 1, across}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = (size_t)cap;
    SinkRef sink(&limits);
    for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_STRICT}) {
      RasterRef whole(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      RasterRef part(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      Refusals w = render_damaged(sink.sink, l, whole.raster, nullptr, flags);
      Refusals p = render_damaged(sink.sink, l, part.raster, &damage, flags);
      EXPECT_TRUE(w == p) << "cap " << cap << (flags ? " strict" : "") << ": the whole render " << w.result << " with "
                          << w.rows.size() << " entries, the damage-limited one " << p.result << " with "
                          << p.rows.size();
      refusing += !w.rows.empty();
      for (uint32_t y = 0; y < RF_HEIGHT; y++) {
        for (uint32_t x = 0; x < RF_WIDTH; x++) {
          bool in = (int64_t)x >= damage.x0 && (int64_t)x < damage.x1 && (int64_t)y >= damage.y0 && (int64_t)y < damage.y1;
          ASSERT_EQ(part.pixel(x, y), in ? whole.pixel(x, y) : fresh.pixel(x, y))
              << "cap " << cap << (flags ? " strict" : "") << " at " << x << "," << y;
        }
      }
    }
  }
  EXPECT_GE(refusing, 8u);
  // The item wholly outside the damage is counted where the cap could be met, and costs no cells where it cannot:
  // at the default cap the bound from its edges settles it.
  GSHSCENE_List * one = s.open();
  ASSERT_EQ(gshscene_list_fill(one, s.rect(36.5, 28.5, 47.5, 39.5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(one), GSHSCENE_OK);
  uint64_t cells = 0;
  size_t points = 0;
  measure(one, 0, RF_WIDTH, RF_HEIGHT, &cells, &points);
  ASSERT_EQ(cells, outside);
  {
    SinkRef sink;
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    Refusals o = render_damaged(sink.sink, one, r.raster, &damage);
    EXPECT_EQ(o.result, GSHSCENE_OK);
    EXPECT_EQ(o.work_max, points) << "outside the damage, under a cap it cannot meet: its points and nothing else";
  }
  for (uint64_t cap : {outside, outside - 1}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = (size_t)cap;
    SinkRef sink(&limits);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    Refusals o = render_damaged(sink.sink, one, r.raster, &damage);
    EXPECT_EQ(o.result, GSHSCENE_OK);
    EXPECT_EQ(o.rows.size(), cap == outside ? 0u : 1u) << cap;
    EXPECT_EQ(o.work_max, points + cap) << "near the cap its cells are counted, to the cap";
  }
}

TEST(RenderLimits, ADamageCountsOnlyTheCellsItDrawsWhereTheCapCannotBeMet) {
  // Where the bound from an item's edges says the cap cannot be met over the whole render's rectangle, a damage-limited
  // render counts the cells where it draws and no more: the work of an item cut by the damage on one side (each side
  // in turn) is its points, the cells inside the damage and the pixels drawn.  Where the bound reaches the cap, the
  // whole rectangle is counted (and the item is still drawn, its cells being within the bound).
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 0.5);
  const GSHSCENE_Rect damage{10, 8, 30, 24};
  const GSHCPU_Rect region{0, 0, RF_WIDTH, RF_HEIGHT};
  struct Cut {
    const char * side;
    double cx, cy;
  } cuts[] = {{"left", 11, 16}, {"top", 20, 9.5}, {"right", 28.5, 16}, {"bottom", 20, 22.5}};
  for (const Cut & c : cuts) {
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_fill(l, diamond(s, c.cx, c.cy, 5.0), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    ItemFacts f = facts_of(l, 0, region);
    GSHCPU_Rect drawn = gshcpu_render_rect_meet(f.box, damage);
    int cut = (f.box.x0 < damage.x0) + (f.box.y0 < damage.y0) + (f.box.x1 > damage.x1) + (f.box.y1 > damage.y1);
    ASSERT_EQ(cut, 1) << c.side << ": cut on one side only";
    const uint64_t inside = cells_in(l, 0, damage);
    const uint64_t whole = cells_in(l, 0, region);
    ASSERT_NE(inside, whole) << c.side;
    const uint64_t pixels = (uint64_t)(drawn.x1 - drawn.x0) * (uint64_t)(drawn.y1 - drawn.y0);
    {
      SinkRef sink;
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      Refusals o = render_damaged(sink.sink, l, r.raster, &damage);
      EXPECT_EQ(o.result, GSHSCENE_OK);
      EXPECT_EQ(o.work_max, f.points + inside + pixels) << c.side << ": the default cap";
    }
    for (uint64_t cap : {f.bound, f.bound - 1}) {
      GSHCPU_Limits limits;
      gshcpu_limits_default(&limits);
      limits.max_deposits = (size_t)cap;
      SinkRef sink(&limits);
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      Refusals o = render_damaged(sink.sink, l, r.raster, &damage);
      EXPECT_EQ(o.result, GSHSCENE_OK);
      EXPECT_TRUE(o.rows.empty()) << c.side << " cap " << cap;
      EXPECT_EQ(o.work_max, f.points + (cap == f.bound ? inside : whole) + pixels)
          << c.side << ": a cap the bound " << (cap == f.bound ? "reaches" : "passes");
    }
  }
}

TEST(RenderLimits, AnItemUnderAClipMeetsTheCapOverTheClipsReach) {
  // The cells of an item are met over what the whole render draws of it, which a clip narrows (S4's rectangle is the
  // extent meet the clips' reach, not the extent): under a cap of exactly the cells inside the clip the item is drawn,
  // in the whole render and in a damage-limited one alike; one less and both refuse it.
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_clip_push(l, s.rect(10.5, 10.5, 19.5, 19.5), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 64, 40.0, 30.0), nullptr, GSHGEOM_FILL_EVEN_ODD, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHCPU_Rect region{0, 0, RF_WIDTH, RF_HEIGHT};
  const GSHCPU_Rect clip = facts_of(l, 0, region).box;
  const uint64_t under = cells_in(l, 1, clip);
  ASSERT_LT(under + 20, cells_in(l, 1, region));
  ASSERT_GT(cells_in(l, 0, region), 0u);
  ASSERT_LT(cells_in(l, 0, region), under) << "the clip itself is within every cap here";
  const GSHSCENE_Rect damage{12, 12, 16, 30};
  for (uint64_t cap : {under, under - 1}) {
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = (size_t)cap;
    SinkRef sink(&limits);
    RasterRef whole(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    RasterRef part(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    Refusals w = render_damaged(sink.sink, l, whole.raster, nullptr);
    Refusals p = render_damaged(sink.sink, l, part.raster, &damage);
    EXPECT_EQ(w.rows.size(), cap == under ? 0u : 1u) << cap;
    EXPECT_TRUE(w == p) << cap;
  }
}

TEST(RenderMemory, TheNeedIsTheBlockAndTheLargestItemNotTheirSum) {
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  const GSHSCENE_Resource * small = s.rect(1, 1, 5, 5);
  const GSHSCENE_Resource * large = zigzag(s, 300, 40, 30);
  GSHSCENE_List * both = s.open();
  GSHSCENE_List * one = s.open();
  GSHSCENE_List * two = s.open();
  for (auto lr : {std::make_pair(both, small), std::make_pair(both, large), std::make_pair(one, small), std::make_pair(two, large)}) {
    ASSERT_EQ(gshscene_list_fill(lr.first, lr.second, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  }
  for (GSHSCENE_List * l : {both, one, two}) {
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  }
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  size_t need[3] = {0, 0, 0};
  GSHSCENE_List * lists[3] = {both, one, two};
  for (int i = 0; i < 3; i++) {
    GSHSCENE_Render_Request req = request_of(lists[i], &t, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need[i]), GSHSCENE_OK);
  }
  EXPECT_LT(need[1], need[2]);
  EXPECT_EQ(need[0], std::max(need[1], need[2]));
  // The layers and clip coverages are counted by the most open at once, not by how many there are.
  GSHSCENE_Group g{};
  g.opacity = 0.5;
  GSHSCENE_List * one_each = s.open();
  GSHSCENE_List * three_each = s.open();
  for (int k = 0; k < 3; k++) {
    for (GSHSCENE_List * l : {one_each, three_each}) {
      if (l == one_each && k > 0) {
        continue;
      }
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_clip_push(l, small, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(l, small, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
  }
  ASSERT_EQ(gshscene_list_close(one_each), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(three_each), GSHSCENE_OK);
  size_t a = 0;
  size_t b = 0;
  GSHSCENE_Render_Request ra = request_of(one_each, &t, report.report);
  ra.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  GSHSCENE_Render_Request rb = ra;
  rb.list = three_each;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &ra, &a), GSHSCENE_OK);
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &rb, &b), GSHSCENE_OK);
  EXPECT_EQ(a, b);
  EXPECT_GT(a, need[1] + (size_t)RF_WIDTH * RF_HEIGHT * 33u) << "a group's layer and a clip's coverage over the region";
}

TEST(RenderMemory, AListThatNeedsNoItemMemoryRendersInItsExactWorkspaceWithoutAllocating) {
  // A caller-memory render whose items take nothing from the arena (an empty list, here) needs exactly the fixed
  // block, and in a workspace of exactly that the sink sizes the render inside it and allocates nothing (S9). Story
  // 16's fuzz replay found it measuring over its own allocator instead: the test was "more than the block", not "at
  // least the block". The control is a list with an item, whose need is past the block.
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 0.5);
  GSHSCENE_List * empty = s.open();
  GSHSCENE_List * one = s.open();
  ASSERT_EQ(gshscene_list_fill(one, s.rect(1, 1, 5, 5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(empty), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(one), GSHSCENE_OK);
  for (GSHSCENE_List * list : {empty, one}) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    SinkRef sink(nullptr, &alloc);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    ReportRef report;
    GSHSCENE_Target target = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(list, &target, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t need = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
    std::vector<unsigned char> workspace(need);
    req.workspace = workspace.data();
    req.workspace_bytes = need;
    const long before = t.calls;
    EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    EXPECT_EQ(t.calls, before) << (list == empty ? "the empty list" : "the list with an item");
  }
}

TEST(RenderMemory, AShortWorkspaceIsALimitWithoutAnyAllocation) {
  // S9 is absolute: a CALLER_MEMORY render whose workspace is too small (for the block, or for an item's geometry), or
  // that brings none, is ERR_LIMIT without one allocator call, by the sink or the report, and with the target
  // untouched; it states no need, and the query (not a render) gives it exactly.  Before 2026-10-21 the sink
  // measured over its own allocator to put the need in the summary (story 16, Implementation Notes 7).
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 0.5);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 1, 5, 5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, zigzag(s, 300, 40, 30), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    SinkRef sink(nullptr, &alloc);
    ReportRef report(&alloc);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target target = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(l, &target, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t need = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
    ASSERT_GT(need, (size_t)RF_WIDTH * RF_HEIGHT * 32u);
    // The block: the need of the same render with no item (the walk and the layers do not depend on the items here).
    GSHSCENE_List * none = s.open();
    ASSERT_EQ(gshscene_list_close(none), GSHSCENE_OK);
    GSHSCENE_Render_Request empty = request_of(none, &target, report.report);
    empty.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t block = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &empty, &block), GSHSCENE_OK);
    ASSERT_LT(block + 64, need);
    std::vector<unsigned char> workspace(need + 1);
    // None; a byte; far short of the block; a byte short of the block; the block alone (short for any item); a byte
    // short of the need.  Nothing past the bytes given is written.
    for (size_t bytes : {(size_t)0, (size_t)1, need / 4, block - 1, block, need - 1}) {
      for (int failing = 0; failing < 2; failing++) {
        std::fill(workspace.begin(), workspace.end(), (unsigned char)0xc5);
        req.workspace = bytes ? workspace.data() + 1 : nullptr;
        req.workspace_bytes = bytes;
        const long calls = t.calls;
        t.fail_from = failing ? t.calls : -1;
        t.persistent = failing != 0;
        EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << bytes << (failing ? " failing" : "");
        t.fail_from = -1;
        t.persistent = false;
        EXPECT_EQ(t.calls, calls) << bytes << " of " << need << " bytes: the render allocated";
        EXPECT_EQ(r.bytes(), fresh.bytes()) << bytes;
        GSHSCENE_Report_Summary sum = report.summary();
        EXPECT_EQ(sum.workspace_needed, 0u) << bytes;
        EXPECT_NE(std::string(sum.failure.reason).find("gshscene_sink_workspace_bytes"), std::string::npos)
            << sum.failure.reason;
        EXPECT_EQ(workspace[0], 0xc5);
        for (size_t i = 1 + bytes; i < workspace.size(); i++) {
          ASSERT_EQ(workspace[i], 0xc5) << bytes << " bytes given: written at " << i - 1;
        }
      }
      size_t again = 0;
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &again), GSHSCENE_OK);
      EXPECT_EQ(again, need) << bytes << ": the query with the short workspace gives the exact need";
    }
    req.workspace = workspace.data() + 1;
    req.workspace_bytes = need;
    EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    EXPECT_EQ(report.summary().workspace_needed, need);
  }
  EXPECT_TRUE(t.live.empty());
}

/* --- what a render touches ---------------------------------------------------------------------------------- */

namespace {

/** Bytes of a workspace of a known fill that a CALLER_MEMORY render of @p list on @p raster changed. */
size_t workspace_written(const GSHSCENE_List * list, GIMG_Raster * raster, size_t * need) {
  SinkRef sink;
  ReportRef report;
  GSHSCENE_Target t = target_of(raster);
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  *need = 0;
  EXPECT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, need), GSHSCENE_OK);
  const unsigned char fill = 0xa5;
  std::vector<unsigned char> workspace(*need, fill);
  req.workspace = workspace.data();
  req.workspace_bytes = *need;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  return (size_t)std::count_if(workspace.begin(), workspace.end(), [fill](unsigned char b) { return b != fill; });
}

} // namespace

TEST(RenderMemory, ARenderTouchesThePixelsItsUnitsReachNotTheRegion) {
  // A render pays for the pixels its units reach (Frame, widen in sink.c): an empty list on a large target writes
  // kilobytes of its workspace at most, and so does a small item, alone, in a group, or in a group with an
  // unbounded operator under a small clip.  The region here is 1024 by 1024, so the block is 34 MB and more; reading
  // the target into the root layer, clearing the touched map or clearing a group's layer over the region (story
  // 13b's sink did all three on every render) writes megabytes.  Counted in bytes, not time.
  const uint32_t w = 1024;
  const uint32_t h = 1024;
  const size_t bound = 64u * 1024u;
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 0.5);
  const GSHSCENE_Resource * item = s.rect(700.25, 600.5, 708.75, 606.25);
  const GSHSCENE_Resource * clip = s.rect(690, 590, 720, 620);
  GSHSCENE_Group over{};
  over.opacity = 0.5;
  GSHSCENE_Group in{};
  in.opacity = 1.0;
  in.composite = GSHSCENE_COMPOSITE_SRC_IN;
  ASSERT_FALSE(gshcpu_render_bounded(in.composite));
  GSHSCENE_List * empty = s.open();
  GSHSCENE_List * one = s.open();
  GSHSCENE_List * grouped = s.open();
  GSHSCENE_List * unbounded = s.open();
  GSHSCENE_List * around = s.open();
  ASSERT_EQ(gshscene_list_fill(one, item, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(grouped, &over, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(grouped, &over, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(grouped, item, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(grouped), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(grouped), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(unbounded, clip, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(unbounded, &in, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(unbounded, item, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(unbounded), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(unbounded), GSHSCENE_OK);
  // Items that widen what is ready on every side of what already is, on the root and in a group (widen's four spans).
  const GSHSCENE_Resource * sides[] = {item, s.rect(690.5, 603.5, 695.5, 604.5), s.rect(712.5, 602.5, 715.5, 605.5),
      s.rect(705.5, 595.5, 707.5, 598.5), s.rect(704.5, 610.5, 707.5, 612.5)};
  for (int level = 0; level < 2; level++) {
    if (level == 1) {
      ASSERT_EQ(gshscene_list_group_push(around, &over, 0, 0), GSHSCENE_OK);
    }
    for (const GSHSCENE_Resource * side : sides) {
      ASSERT_EQ(gshscene_list_fill(around, side, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
    }
    if (level == 1) {
      ASSERT_EQ(gshscene_list_group_pop(around), GSHSCENE_OK);
    }
  }
  struct Case {
    const char * name;
    GSHSCENE_List * list;
    GSHCPU_Rect reach; // where the target may change
  } cases[] = {
      {"empty", empty, {0, 0, 0, 0}},
      {"one small item", one, {700, 600, 709, 607}},
      {"a small item in two groups", grouped, {700, 600, 709, 607}},
      {"a small item in an unbounded group under a small clip", unbounded, {690, 590, 720, 620}},
      {"small items around it, on the root and in a group", around, {690, 595, 716, 613}},
  };
  for (const Case & c : cases) {
    ASSERT_EQ(gshscene_list_close(c.list), GSHSCENE_OK);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
    RasterRef before(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
    size_t need = 0;
    size_t written = workspace_written(c.list, r.raster, &need);
    EXPECT_GT(need, (size_t)w * h * 34u) << c.name << ": the block covers the region";
    EXPECT_LE(written, bound) << c.name << ": " << written << " bytes of a " << need << "-byte workspace written";
    size_t changed = 0;
    size_t outside = 0;
    const unsigned char * now = (const unsigned char *)gimg_raster_pixels_const(r.raster);
    const unsigned char * was = (const unsigned char *)gimg_raster_pixels_const(before.raster);
    size_t stride = gimg_raster_stride_bytes(r.raster);
    ASSERT_EQ(stride, gimg_raster_stride_bytes(before.raster));
    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        if (std::memcmp(now + y * stride + 4u * x, was + y * stride + 4u * x, 4) != 0) {
          changed++;
          outside += (x < c.reach.x0 || x >= c.reach.x1 || y < c.reach.y0 || y >= c.reach.y1) ? 1 : 0;
        }
      }
    }
    EXPECT_EQ(outside, 0u) << c.name;
    EXPECT_EQ(changed == 0, c.list == empty) << c.name;
    // And the pixels are the sink's own allocation's (S12): nothing was read from memory the render had not made
    // ready, which a workspace of 0xa5 bytes would show.
    SinkRef plain;
    RasterRef q(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
    EXPECT_EQ(render(plain.sink, c.list, q.raster).result, GSHSCENE_OK) << c.name;
    EXPECT_TRUE(q.bytes() == r.bytes()) << c.name;
  }
}

TEST(RenderMemory, AnUnboundedGroupsPopVisitsTheRowsOfItsClipsReachAndNoMore) {
  // A group with an unbounded operator acts wherever the clips around it weigh above zero (N8), and its pop visits
  // the rows of those clips' reach.  The row just below the reach is outside the clip, but a clip's coverage is kept
  // compact over its own rectangle in memory the clips at its level share, so reading one row past it reads what an
  // earlier, larger clip left there.  Here that is a whole row of 255: the first clip covers the target and the
  // second, the rows 2 to 4 of it, reuses its memory.  A pop that went one row too far would weigh row 5 fully and
  // apply its operator (SRC, with the group's empty source) there, clearing the red backdrop below the clip.
  const uint32_t w = 8;
  const uint32_t h = 8;
  Store s;
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  GSHSCENE_Paint blue = solid(0, 0, 1, 1);
  const GSHSCENE_Resource * all = s.rect(0, 0, w, h);
  const GSHSCENE_Resource * band = s.rect(0, 2, w, 5);
  GSHSCENE_Group src{};
  src.opacity = 1.0;
  src.composite = GSHSCENE_COMPOSITE_SRC;
  ASSERT_FALSE(gshcpu_render_bounded(src.composite));
  GSHSCENE_List * list = s.open();
  ASSERT_EQ(gshscene_list_clip_push(list, all, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(list, all, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(list, band, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(list, &src, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(list, band, nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(list), GSHSCENE_OK);
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
  ASSERT_EQ(render(sink.sink, list, r.raster).result, GSHSCENE_OK);
  const std::string redpx("\xff\x00\x00\xff", 4);
  const std::string bluepx("\x00\x00\xff\xff", 4);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      EXPECT_EQ(r.pixel(x, y), (y >= 2 && y < 5) ? bluepx : redpx) << "at " << x << ", " << y;
    }
  }
}

/* --- cancel (S7) -------------------------------------------------------------------------------------------- */

namespace {
struct CancelAt {
  GSHSCENE_Cancel * cancel;
  size_t at;
};
void cancel_at(void * ctx, const GSHSCENE_Job *, size_t units) {
  CancelAt * c = (CancelAt *)ctx;
  if (units == c->at) {
    gshscene_cancel_set(c->cancel);
  }
}
} // namespace

TEST(RenderCancel, ACancelledRenderHoldsTheRenderOfThePrefixOfPaintOrder) {
  int compared = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Fixture fx(i);
    if (has_instance(fx.list)) {
      continue;
    }
    size_t total = gshscene_list_count(fx.list);
    for (size_t k = 1; k < total; k++) {
      SinkRef sink;
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      ReportRef report;
      GSHSCENE_Cancel cancel;
      gshscene_cancel_init(&cancel);
      CancelAt at{&cancel, k};
      GSHSCENE_Target t = target_of(r.raster, &fx.ctx.target_map);
      GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
      req.cancel = &cancel;
      req.progress = cancel_at;
      req.progress_ctx = &at;
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_CANCELLED);
      GSHSCENE_Report_Summary sum = report.summary();
      EXPECT_STREQ(sum.failure.reason, "cancelled between units");
      EXPECT_EQ(sum.units_processed, k);
      EXPECT_LE(sum.units_done, k);
      // What the target holds is the render of the units done, and nothing of an open group.
      GSHSCENE_List * p = prefix(fx.list, fx.ctx.store, sum.units_done);
      RasterRef want(rf_raster(GSHCPU_FORMAT_RGBAF32_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      GSHSCENE_Target wt = target_of(want.raster, &fx.ctx.target_map);
      ReportRef wr;
      GSHSCENE_Render_Request wq = request_of(p, &wt, wr.report);
      ASSERT_EQ(gshscene_sink_render(sink.sink, &wq), GSHSCENE_OK);
      EXPECT_EQ(r.bytes(), want.bytes()) << RF_FIXTURES[i].name << " cancelled at " << k << " (done " << sum.units_done << ")";
      gshscene_list_destroy(p);
      compared++;
    }
  }
  EXPECT_GT(compared, 60);
}

/* --- caller memory (S9) ------------------------------------------------------------------------------------ */

TEST(RenderMemory, TheExactWorkspaceDrawsTheGoldenAndFramesAllocateNothing) {
  auto want = goldens();
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Fixture fx(i);
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      SinkRef sink(nullptr, &alloc);
      ReportRef report(&alloc);
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
      GSHSCENE_Target target = target_of(r.raster, &fx.ctx.target_map);
      GSHSCENE_Render_Request req = request_of(fx.list, &target, report.report);
      req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
      size_t need = 0;
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
      ASSERT_GT(need, (size_t)RF_WIDTH * RF_HEIGHT * 32u);
      std::vector<unsigned char> workspace(need + 1 + 4096, 0xc5);
      // One byte less: a limit, the target untouched.
      req.workspace = workspace.data() + 1;
      req.workspace_bytes = need - 1;
      RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
      EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << RF_FIXTURES[i].name;
      EXPECT_EQ(report.summary().workspace_needed, 0u) << "a short workspace states no need (S9)";
      EXPECT_EQ(r.bytes(), fresh.bytes());
      // Sized with the workspace given: the same need, and nothing allocated to find it.
      req.workspace_bytes = need;
      long before = t.calls;
      size_t again = 0;
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &again), GSHSCENE_OK);
      EXPECT_EQ(again, need);
      EXPECT_EQ(t.calls, before) << "sizing in the caller's workspace allocated";
      for (int frame = 0; frame < 4; frame++) {
        RasterRef out(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
        GSHSCENE_Target ft = target_of(out.raster, &fx.ctx.target_map);
        req.target = &ft;
        long calls = t.calls;
        ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << RF_FIXTURES[i].name;
        if (frame > 0) {
          EXPECT_EQ(t.calls, calls) << RF_FIXTURES[i].name << ": frame " << frame << " allocated";
        }
        EXPECT_EQ(out.hash(), (want[{RF_FIXTURES[i].name, "rgba8-premultiplied"}])) << RF_FIXTURES[i].name;
        for (size_t b = need + 1; b < workspace.size(); b++) {
          ASSERT_EQ(workspace[b], 0xc5) << "past the workspace, at " << b - 1;
        }
        EXPECT_EQ(report.summary().workspace_needed, need);
      }
    }
    EXPECT_TRUE(t.live.empty()) << RF_FIXTURES[i].name;
  }
}

TEST(RenderMemory, TheNeedDoesNotDependOnTheWorkspacesAddress) {
  Fixture fx(9); // glyphs: geometry from rational arcs
  SinkRef sink;
  ReportRef report;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  GSHSCENE_Target target = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(fx.list, &target, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> workspace(need + 64);
  uint64_t first = 0;
  for (size_t offset = 0; offset < 16; offset++) {
    req.workspace = workspace.data() + offset;
    req.workspace_bytes = need;
    size_t n = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &n), GSHSCENE_OK);
    EXPECT_EQ(n, need) << offset;
    RasterRef out(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target ft = target_of(out.raster);
    req.target = &ft;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << offset;
    if (offset == 0) {
      first = out.hash();
    }
    EXPECT_EQ(out.hash(), first) << offset;
    req.target = &target;
  }
}

/* --- allocation failure ---------------------------------------------------------------------------------------- */

TEST(RenderMemory, EveryAllocationOfEveryFixturesRenderCanFail) {
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Fixture fx(i);
    bool reached = false;
    int ooms = 0;
    int named = 0; // failures inside a unit, which the failure's chain names
    for (long fail = 0; fail < 2000 && !reached; fail++) {
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      {
        SinkRef sink(nullptr, &alloc);
        RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
        ReportRef report;
        GSHSCENE_Target target = target_of(r.raster, &fx.ctx.target_map);
        GSHSCENE_Render_Request req = request_of(fx.list, &target, report.report);
        t.fail_from = t.calls + fail;
        GSHSCENE_Result got = gshscene_sink_render(sink.sink, &req);
        t.fail_from = -1;
        if (got == GSHSCENE_OK) {
          reached = true;
          EXPECT_FALSE(t.failed);
        } else {
          EXPECT_EQ(got, GSHSCENE_ERR_OOM) << RF_FIXTURES[i].name << " fail " << fail;
          GSHSCENE_Report_Summary sum = report.summary();
          EXPECT_EQ(sum.result, GSHSCENE_ERR_OOM);
          if (sum.failure.chain_count) {
            named++;
            if (!has_instance(fx.list)) {
              ASSERT_EQ(sum.failure.chain_count, 1u);
              EXPECT_EQ(sum.failure.chain[0], sum.units_processed) << "the unit that failed is the next one";
            }
            EXPECT_STREQ(sum.failure.reason, "out of memory while drawing");
          }
          ooms++;
        }
      }
      EXPECT_TRUE(t.live.empty()) << RF_FIXTURES[i].name << " fail " << fail << " leaked";
    }
    EXPECT_TRUE(reached) << RF_FIXTURES[i].name;
    EXPECT_GT(ooms, 1) << RF_FIXTURES[i].name;
    EXPECT_GT(named, 0) << RF_FIXTURES[i].name << ": no failure inside a unit was seen";
  }
}

TEST(RenderMemory, SizingCanFailAtEveryAllocation) {
  Fixture fx(8);
  bool reached = false;
  for (long fail = 0; fail < 400 && !reached; fail++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      SinkRef sink(nullptr, &alloc);
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
      ReportRef report;
      GSHSCENE_Target target = target_of(r.raster);
      GSHSCENE_Render_Request req = request_of(fx.list, &target, report.report);
      req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
      size_t need = 0;
      t.fail_from = t.calls + fail;
      GSHSCENE_Result got = gshscene_sink_workspace_bytes(sink.sink, &req, &need);
      t.fail_from = -1;
      if (got == GSHSCENE_OK) {
        reached = true;
      } else {
        EXPECT_EQ(got, GSHSCENE_ERR_OOM) << fail;
      }
    }
    EXPECT_TRUE(t.live.empty()) << fail;
  }
  EXPECT_TRUE(reached);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
