/**
 * @file
 *
 * The CPU sink's limits, cancel, caller memory and allocation failure (story
 * 13b): each limit at its bound and one past it, a cancelled render's prefix of
 * paint order against the render of that prefix, the work between two checks
 * on a hostile maximal item, the exact workspace and frames that allocate
 * nothing, and a failing allocator at every call of every fixture's render.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "render_helpers.h"

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
    tq.extent = GSHSCENE_Rect{0, 0, 16, 16 + extra};
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
        limits.max_cells = (size_t)(cells - less);
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
  limits.max_cells = (size_t)cells;
  limits.max_bytes = RF_WIDTH * RF_HEIGHT;
  SinkRef sink(&limits);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Sink_Caps caps{};
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
  EXPECT_EQ(caps.cancel_work_bound, points + cells + RF_WIDTH * RF_HEIGHT);
  Outcome run = render(sink.sink, l, r.raster);
  ASSERT_EQ(run.result, GSHSCENE_OK);
  EXPECT_TRUE(run.entries.empty());
  EXPECT_LE(run.summary.work_max, caps.cancel_work_bound);
  EXPECT_GE(run.summary.work_max, points + cells) << "the item's points and cells are counted";
  EXPECT_EQ(run.summary.work_max, points + cells + RF_WIDTH * RF_HEIGHT) << "and every pixel of its box, the raster here";
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
      // One byte less: a limit, naming the need, the target untouched.
      req.workspace = workspace.data() + 1;
      req.workspace_bytes = need - 1;
      RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
      EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << RF_FIXTURES[i].name;
      EXPECT_EQ(report.summary().workspace_needed, need);
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
          EXPECT_EQ(report.summary().result, GSHSCENE_ERR_OOM);
          ooms++;
        }
      }
      EXPECT_TRUE(t.live.empty()) << RF_FIXTURES[i].name << " fail " << fail << " leaked";
    }
    EXPECT_TRUE(reached) << RF_FIXTURES[i].name;
    EXPECT_GT(ooms, 1) << RF_FIXTURES[i].name;
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
