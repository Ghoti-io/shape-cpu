/**
 * @file
 *
 * Runs from a font through shape-cpu by both routes (story 14a; AD-5, AD-13).  AD-5's gate in pixels: with hinting
 * off, a run on the fast tier (the test provider's device paths) renders byte for byte what the general route renders
 * under the snapped map (the same glyphs, the size snapped to the 16.16 scale, each origin rounded to 26.6, under a
 * quarter turn and its inverse so that the tag sends them to the general route), for every outline glyph of the
 * provider, ppem 8 to 72, a 2048-unit face (where every odd unit is a tie at 16 ppem) and a 1000-unit one, two
 * instances, and the origin fractions {0, 1, 16, 33, 63} on each axis; and the fast tier is seen to be taken (the
 * provider's fast count is the glyph count).  Hinting is what separates the two routes, and a run reaches the
 * report as glyph.h says.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "../render_helpers.h"

#include <cstring>
#include <vector>

using namespace rh;

namespace {

const uint32_t kFractions[5] = {0, 1, 16, 33, 63};
const uint32_t kGlyphs[6] = {1, 2, 3, 5, 6, 7};

/** One glyph placed at a whole pixel plus a fraction, in device pixels. */
struct Placed {
  uint32_t id;
  double x;
  double y;
};

/** The glyphs of one gate case: every outline glyph, each fraction pair, on a grid of cells @p cell pixels wide. */
std::vector<Placed> placements(uint32_t cell) {
  std::vector<Placed> out;
  size_t k = 0;
  for (uint32_t fy : kFractions) {
    for (uint32_t fx : kFractions) {
      uint32_t id = kGlyphs[k % 6];
      double x = 2.0 + (double)((k % 5) * cell) + fx / 64.0;
      double y = (double)cell + (double)((k / 5) * cell) + fy / 64.0;
      out.push_back({id, x, y});
      k++;
    }
  }
  return out;
}

/** A face of @p tp with the fast tier. */
const GSHSCENE_Resource * face_of(Store & s, GSHSCENE_Test_Provider * tp, int16_t coord) {
  const GSHSCENE_Resource * f = gshscene_tp_face(s.store, tp, 1, 0x71, coord);
  s.held.push_back(f);
  return f;
}

const GSHSCENE_Resource * run_of(Store & s, const GSHSCENE_Resource * face, double size, uint32_t mode,
    const std::vector<GSHSCENE_Glyph> & glyphs) {
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = size;
  run.hint_mode = mode;
  run.glyph_count = glyphs.size();
  run.glyphs = glyphs.data();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

/** Render @p list on a fresh RGBA8 raster of @p w by @p h; the report and the raster's bytes. */
std::string render(const GSHSCENE_List * list, uint32_t w, uint32_t h, GSHSCENE_Report * report,
    const GSHGEOM_Map * target_map = nullptr) {
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
  SinkRef sink;
  GSHSCENE_Target t = target_of(r.raster, target_map);
  GSHSCENE_Render_Request req = request_of(list, &t, report);
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  return r.bytes();
}

GSHGEOM_Map tagged_identity() {
  GSHGEOM_Map a;
  GSHGEOM_Map b;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &a);
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, -90.0, &b);
  gshgeom_map_compose(&a, &b, &a);
  return a;
}

}  // namespace

// AD-5's gate: fast(map) == general(snap(map)), byte for byte, with hinting off; and the fast tier is taken.
TEST(GlyphGate, TheFastTierIsTheGeneralRouteUnderTheSnappedMapInPixels) {
  size_t cases = 0;
  for (uint32_t upem : {2048u, 1000u}) {
    GSHSCENE_Test_Provider tp{};
    tp.upem = upem;
    tp.flags = GSHSCENE_FACE_HINTING;
    for (int16_t coord : {(int16_t)0, (int16_t)7777}) {
      for (uint32_t ppem = 8; ppem <= 72; ppem++) {
        Store s;
        const GSHSCENE_Resource * face = face_of(s, &tp, coord);
        uint32_t cell = ppem + 4u;
        std::vector<Placed> at = placements(cell);
        uint32_t w = 6u * cell;
        uint32_t h = 6u * cell + 4u;
        // The fast scene: the run at twice the size under a scale of one half, moved by a whole pixel, so the device
        // ppem is the integer and the map is not the identity.
        std::vector<GSHSCENE_Glyph> fast_glyphs;
        for (const Placed & p : at) {
          fast_glyphs.push_back({p.id, 2.0 * (p.x - 1.0), 2.0 * p.y, nullptr});
        }
        GSHGEOM_Map half;
        GSHGEOM_Map map;
        gshgeom_map_scale(2, 0.5, 0.5, 1.0, &half);
        gshgeom_map_translate(2, 1.0, 0.0, 0.0, &map);
        gshgeom_map_compose(&map, &half, &map);
        const GSHSCENE_Resource * fast_run = run_of(s, face, 2.0 * ppem, GSHSCENE_HINT_NONE, fast_glyphs);
        GSHSCENE_Glyph_Route route;
        ASSERT_EQ(gshscene_glyph_route(fast_run, &map, GSHSCENE_TARGET_RASTER, &route), GSHSCENE_OK);
        ASSERT_EQ(route.route, (uint32_t)GSHSCENE_GLYPH_FAST);
        ASSERT_EQ(route.ppem, ppem);
        // The snapped scene: the size whose scale per unit is exactly scale / 65536, each glyph at its rounded origin,
        // under the identity tagged as a quarter turn and its inverse: the general route.
        std::vector<GSHSCENE_Glyph> snapped;
        for (size_t i = 0; i < fast_glyphs.size(); i++) {
          GSHSCENE_Glyph_Origin o;
          ASSERT_EQ(gshscene_glyph_origin(fast_run, &map, i, &o), GSHSCENE_OK);
          snapped.push_back({at[i].id, (double)o.x / 64.0, (double)o.y / 64.0, nullptr});
        }
        double snapped_size = (double)route.scale * (double)upem / 65536.0;
        const GSHSCENE_Resource * general_run = run_of(s, face, snapped_size, GSHSCENE_HINT_NONE, snapped);
        GSHGEOM_Map tag = tagged_identity();
        GSHSCENE_Glyph_Route general;
        ASSERT_EQ(gshscene_glyph_route(general_run, &tag, GSHSCENE_TARGET_RASTER, &general), GSHSCENE_OK);
        ASSERT_EQ(general.route, (uint32_t)GSHSCENE_GLYPH_GENERAL);
        GSHSCENE_Paint ink = solid(0.1, 0.2, 0.3, 1.0);
        GSHSCENE_List * fl = s.open();
        ASSERT_EQ(gshscene_list_fill(fl, fast_run, &map, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0), GSHSCENE_OK);
        ASSERT_EQ(gshscene_list_close(fl), GSHSCENE_OK);
        GSHSCENE_List * gl = s.open();
        ASSERT_EQ(gshscene_list_fill(gl, general_run, &tag, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0), GSHSCENE_OK);
        ASSERT_EQ(gshscene_list_close(gl), GSHSCENE_OK);
        gshscene_tp_reset(&tp);
        ReportRef fr;
        std::string fast_pixels = render(fl, w, h, fr.report);
        EXPECT_EQ(gcu_atomic_size_load(&tp.fast_calls), fast_glyphs.size()) << "the fast tier is taken";
        EXPECT_EQ(gcu_atomic_size_load(&tp.outline_calls), 0u);
        gshscene_tp_reset(&tp);
        ReportRef gr;
        std::string general_pixels = render(gl, w, h, gr.report);
        EXPECT_EQ(gcu_atomic_size_load(&tp.fast_calls), 0u);
        EXPECT_EQ(gcu_atomic_size_load(&tp.outline_calls), snapped.size());
        ASSERT_EQ(fast_pixels, general_pixels) << "upem " << upem << " ppem " << ppem << " coordinate " << coord;
        EXPECT_EQ(gshscene_report_count(fr.report), 0u) << "mode NONE reports nothing";
        EXPECT_EQ(gshscene_report_count(gr.report), 0u);
        // Something was drawn: the comparison is not of two empty rasters.
        RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, w, h));
        ASSERT_NE(fast_pixels, fresh.bytes());
        cases++;
      }
    }
  }
  EXPECT_EQ(cases, 2u * 2u * 65u);
}

// Hinting is the one difference: the same scene hinted differs from the general route; HINTED reports nothing.
TEST(GlyphGate, HintingIsWhatSeparatesTheRoutes) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  tp.flags = GSHSCENE_FACE_HINTING;
  Store s;
  const GSHSCENE_Resource * face = face_of(s, &tp, 0);
  std::vector<GSHSCENE_Glyph> g = {{1, 2.25, 20.5, nullptr}, {2, 15.75, 20.0, nullptr}};
  const GSHSCENE_Resource * hinted = run_of(s, face, 16.0, GSHSCENE_HINT_FONT, g);
  const GSHSCENE_Resource * plain = run_of(s, face, 16.0, GSHSCENE_HINT_NONE, g);
  GSHSCENE_Paint ink = solid(0, 0, 0, 1);
  GSHSCENE_List * a = s.open();
  ASSERT_EQ(gshscene_list_fill(a, hinted, nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(a), GSHSCENE_OK);
  GSHSCENE_List * b = s.open();
  ASSERT_EQ(gshscene_list_fill(b, plain, nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(b), GSHSCENE_OK);
  ReportRef ra;
  ReportRef rb;
  EXPECT_NE(render(a, 40, 24, ra.report), render(b, 40, 24, rb.report));
  EXPECT_EQ(gshscene_report_count(ra.report), 0u) << "HINTED gives no entry";
}

// The routes reach the report and the target as glyph.h says, through shape-cpu.
TEST(GlyphRender, EntriesRefusalsAndTheStrictPrefix) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  tp.flags = GSHSCENE_FACE_HINTING;
  Store s;
  const GSHSCENE_Resource * face = face_of(s, &tp, 0);
  GSHSCENE_Paint ink = solid(0, 0, 0, 1);
  // A rotated run under FONT: one HINT_ROUTE entry; the same under NONE: none.
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 10.0, &turn);
  for (uint32_t mode : {(uint32_t)GSHSCENE_HINT_FONT, (uint32_t)GSHSCENE_HINT_NONE}) {
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, mode, {{1, 4, 20, nullptr}}), &turn, GSHGEOM_FILL_NONZERO,
                  &ink, 0, 0.0),
        GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    ReportRef r;
    render(l, 32, 32, r.report);
    auto e = r.entries();
    if (mode == GSHSCENE_HINT_FONT) {
      ASSERT_EQ(e.size(), 1u);
      EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_HINT_STATUS);
      EXPECT_EQ(e[0].reason, GSHSCENE_REASON_HINT_ROUTE);
    } else {
      EXPECT_EQ(e.size(), 0u);
    }
  }
  // A corrupt glyph and a glyph past the flattener's points are refused, the rest drawn; strict stops at the item.
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 2000;
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 3, 3), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, GSHSCENE_HINT_NONE, {{4, 4, 20, nullptr}, {8, 4, 20, nullptr},
                {1, 12, 20, nullptr}}), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 32, 32));
  RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 32, 32));
  SinkRef sink(&limits);
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  auto e = report.entries();
  ASSERT_EQ(e.size(), 2u); // in the report's order: the reason before the glyph
  EXPECT_EQ(e[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(e[0].second, 1u);
  EXPECT_EQ(e[1].reason, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
  EXPECT_EQ(e[1].second, 0u);
  EXPECT_NE(r.pixel(13, 17), fresh.pixel(13, 17)) << "the third glyph is drawn";
  RasterRef s2(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 32, 32));
  GSHSCENE_Target t2 = target_of(s2.raster);
  ReportRef strict;
  req = request_of(l, &t2, strict.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_REFUSED);
  GSHSCENE_Report_Summary sum = strict.summary();
  EXPECT_EQ(sum.units_done, 1u);
  ASSERT_EQ(sum.failure.chain_count, 1u);
  EXPECT_EQ(sum.failure.chain[0], 1u);
  EXPECT_NE(s2.pixel(1, 1), fresh.pixel(1, 1)) << "the prefix is drawn";
  EXPECT_EQ(s2.pixel(13, 17), fresh.pixel(13, 17)) << "the run is not";
}

// Every allocation of a render of runs from a font can fail: the render is out of memory, never a glyph refused for
// a limit it did not reach, and nothing leaks, strict or not (a glyph refused under strict stops with its paths freed).
TEST(GlyphRender, EveryAllocationCanFailAndNothingLeaks) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  tp.flags = GSHSCENE_FACE_HINTING;
  Store s;
  const GSHSCENE_Resource * face = face_of(s, &tp, 0);
  GSHSCENE_Paint ink = solid(0, 0, 0, 1);
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 10.0, &turn);
  std::vector<GSHSCENE_Glyph> g = {{1, 4, 20, nullptr}, {3, 14, 20, nullptr}, {4, 20, 20, nullptr}};
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, GSHSCENE_HINT_FONT, g), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0,
                0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, GSHSCENE_HINT_NONE, g), &turn, GSHGEOM_FILL_NONZERO, &ink, 0,
                0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_STRICT}) {
    bool finished = false;
    long ooms = 0;
    for (long k = 0; k < 100000 && !finished; k++) {
      tracker::Tracker t;
      GSHSCENE_Allocator a = tracker::allocator_for(&t);
      {
        RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 32, 32));
        SinkRef sink(nullptr, &a);
        ReportRef report;
        GSHSCENE_Target target = target_of(r.raster);
        GSHSCENE_Render_Request req = request_of(l, &target, report.report);
        req.flags = flags;
        t.fail_from = t.calls + k;
        GSHSCENE_Result rc = gshscene_sink_render(sink.sink, &req);
        t.fail_from = -1;
        if (rc == GSHSCENE_ERR_OOM) {
          ooms++;
        } else {
          ASSERT_EQ(rc, flags ? GSHSCENE_ERR_REFUSED : GSHSCENE_OK) << k;
          finished = true;
        }
        for (const GSHSCENE_Report_Entry & e : report.entries()) {
          EXPECT_NE(e.reason, GSHSCENE_REASON_RESOURCE_LIMIT) << "k " << k << ": out of memory is not a limit";
        }
      }
      EXPECT_TRUE(t.live.empty()) << "k " << k << " flags " << flags << " leaked";
    }
    EXPECT_TRUE(finished);
    EXPECT_GT(ooms, 0);
  }
}

// An origin past the 26.6 range refuses its glyph alone.
TEST(GlyphRender, AnOriginPastTheRangeRefusesItsGlyph) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  Store s;
  const GSHSCENE_Resource * face = face_of(s, &tp, 0);
  GSHSCENE_Paint ink = solid(0, 0, 0, 1);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, GSHSCENE_HINT_NONE, {{1, 4e7, 20, nullptr}, {1, 4, 20, nullptr}}),
                nullptr, GSHGEOM_FILL_NONZERO, &ink, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  ReportRef report;
  render(l, 16, 24, report.report);
  auto e = report.entries();
  ASSERT_EQ(e.size(), 1u);
  EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(e[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(e[0].second, 0u);
}

// Caller memory: the provider's paths come from the workspace, sized exactly, and the render allocates nothing.
TEST(GlyphRender, CallerMemoryCoversTheProvidersPaths) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  Store s;
  const GSHSCENE_Resource * face = face_of(s, &tp, 0);
  GSHSCENE_Paint ink = solid(0, 0, 0, 1);
  GSHGEOM_Map turn;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 10.0, &turn);
  GSHSCENE_List * l = s.open();
  std::vector<GSHSCENE_Glyph> g = {{1, 4, 20, nullptr}, {2, 14, 20, nullptr}, {3, 22, 20, nullptr}};
  ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, GSHSCENE_HINT_NONE, g), nullptr, GSHGEOM_FILL_NONZERO, &ink, 0,
                0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, run_of(s, face, 12.0, GSHSCENE_HINT_NONE, g), &turn, GSHGEOM_FILL_NONZERO, &ink, 0,
                0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  ReportRef plain_report;
  std::string plain = render(l, 32, 32, plain_report.report);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 32, 32));
  SinkRef sink;
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> workspace(need + 16);
  req.workspace = workspace.data();
  req.workspace_bytes = need;
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(r.bytes(), plain);
  req.workspace_bytes = need - 1;
  ReportRef small;
  req.report = small.report;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
