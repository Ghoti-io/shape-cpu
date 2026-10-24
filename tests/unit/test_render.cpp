/**
 * @file
 *
 * The CPU sink (story 13b): its caps per target and format, the fixture set's
 * committed goldens on every format, damage and extents, readback, the report,
 * glyph runs, the same drawing as an immediate and a retained list, and the
 * render hash and dump.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "render_helpers.h"

#include <cmath>
#include <random>

using namespace rh;

namespace {

GSHSCENE_Sink_Caps caps_for(GSHSCENE_Sink * sink, GIMG_Raster * raster, GSHSCENE_Result want = GSHSCENE_OK) {
  GSHSCENE_Target t = target_of(raster);
  GSHSCENE_Sink_Caps caps{};
  EXPECT_EQ(gshscene_sink_caps(sink, &t, &caps), want);
  return caps;
}

GIMG_Raster * raster_of(const GIMG_Pixel_Format & f, uint32_t w = 4, uint32_t h = 4) {
  GIMG_Raster * r = nullptr;
  if (gimg_raster_create(w, h, &f, GIMG_RASTER_OWNED, nullptr, 0, &r) != GIMG_OK) {
    return nullptr;
  }
  return r;
}

} // namespace

/* --- caps (S2) ----------------------------------------------------------------------------------- */

TEST(RenderCaps, EachFormatItWritesHasTheSameCaps) {
  SinkRef sink;
  for (GSHCPU_Format format : RF_FORMATS) {
    RasterRef r(rf_raster(format, 4, 4));
    ASSERT_NE(r.raster, nullptr);
    EXPECT_EQ(gshcpu_format_of(r.raster), format);
    GSHSCENE_Sink_Caps caps = caps_for(sink.sink, r.raster);
    // The seven kinds the sink draws, named: not "every kind below the enum's end", which would claim a kind shape-scene
    // adds later and hand the sink an item it has no drawing for.
    EXPECT_EQ(caps.item_kinds, (1u << GSHSCENE_ITEM_FILL) | (1u << GSHSCENE_ITEM_CLIP_PUSH) |
        (1u << GSHSCENE_ITEM_CLIP_POP) | (1u << GSHSCENE_ITEM_GROUP_PUSH) | (1u << GSHSCENE_ITEM_GROUP_POP) |
        (1u << GSHSCENE_ITEM_INSTANCE) | (1u << GSHSCENE_ITEM_STROKE)) << "the seven kinds it draws, and no bit past them";
    EXPECT_EQ(caps.item_kinds & ~0xfeu, 0u) << "no kind from 8 on";
    EXPECT_EQ(caps.stroke_native, 0u) << "a stroke is never drawn as a stroke: every feature is met by expansion (SC1)";
    EXPECT_EQ(caps.composites, (1u << GSHSCENE_COMPOSITE_COUNT) - 1u)
        << "the twelve Porter-Duff operators, plus and the fifteen blend modes (color-paint story P3)";
    EXPECT_EQ(caps.flags & (GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT | GSHSCENE_CAP_GROUP_SOFT_MASK), 0u)
        << "PDF's group fields are refused (S19) until they are composited";
    // The caps claim exactly what the compositor composites (color-paint story P3-12): an operator the caps claim
    // that apply does not know would be drawn as nothing, and one apply knows that the caps leave out is refused.
    for (uint32_t op = 0; op < 32u; op++) {
      double d[4] = {0.25, 0.5, 0.75, 0.5};
      const double s[4] = {0.125, 0.25, 0.0, 0.5};
      EXPECT_EQ((caps.composites >> op) & 1u, (uint32_t)gshcpu_render_apply(d, s, 0.5, op)) << "operator " << op;
    }
    EXPECT_EQ(caps.flags, GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_CALLER_MEMORY | GSHSCENE_CAP_CLIP_AA |
        GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_READBACK | GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_GRADIENT_SPACES |
        GSHSCENE_CAP_STROKE_EXPAND | GSHSCENE_CAP_STROKE_RUNS);
    EXPECT_NE(caps.flags & GSHSCENE_CAP_STROKE_RUNS, 0u) << "a stroked run is the stroke of its centerline (strokes story S5, ST8)";
    EXPECT_EQ(caps.gradient_kinds, (1u << GSHSCENE_GRADIENT_LINEAR) | (1u << GSHSCENE_GRADIENT_RADIAL) |
        (1u << GSHSCENE_GRADIENT_SWEEP)) << "every gradient kind (color-paint story P2)";
    EXPECT_EQ(caps.cancel_unit, (uint32_t)GSHSCENE_CANCEL_BETWEEN_UNITS);
    EXPECT_STREQ(caps.work_measure, "cells+points+pixels");
    EXPECT_EQ(caps.cancel_work_bound, 3 * (uint64_t)GSHGEOM_LIMITS_DEFAULT_MAX_POINTS + GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS +
        2 * (uint64_t)GSHCPU_LIMITS_DEFAULT_MAX_BYTES +
        (uint64_t)GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS * (GSHGEOM_STROKE_MAX_DEPTH + 1) +
        gshcpu_exact_work_bound(GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS, 3 * (uint64_t)GSHGEOM_LIMITS_DEFAULT_MAX_POINTS,
            GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION, 3 * GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS,
            GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS))
        << "SC11: an aligned stroke's three parts, its cells, two passes over the pixels, the expansion's steps and the exact step's (UC9)";
  }
}

TEST(RenderCaps, AKindPastTheEightItKnowsIsRefused) {
  // Every kind after the placeholder: the one predicate of the planning pass refuses each, ITEM_KIND, so the sink never
  // meets one it has no drawing for.  The placeholder (8) is not a kind it draws and is not refused for it: the plan
  // reports it whatever the caps claim (PH3), as its own reason.
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 4, 4));
  ASSERT_NE(r.raster, nullptr);
  GSHSCENE_Sink_Caps caps = caps_for(sink.sink, r.raster);
  const GSHSCENE_Color_Space srgb = {GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0};
  for (uint32_t k = 9; k < 40; k++) {
    GSHSCENE_Item item{};
    item.kind = (GSHSCENE_Item_Kind)k;
    GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, &item, nullptr, 0, nullptr, &srgb, nullptr);
    EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED) << k;
    EXPECT_EQ(d.reason, GSHSCENE_REASON_ITEM_KIND) << k;
  }
  EXPECT_EQ(caps.item_kinds & (1u << GSHSCENE_ITEM_PLACEHOLDER), 0u) << "no sink claims the placeholder";
  GSHSCENE_Item placeholder{};
  placeholder.kind = GSHSCENE_ITEM_PLACEHOLDER;
  placeholder.placeholder.source = GSHSCENE_PLACEHOLDER_SOURCE_COLR;
  placeholder.placeholder.code = GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED;
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, &placeholder, nullptr, 0, nullptr, &srgb, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PLACEHOLDER);
  EXPECT_EQ(d.detail, (uint64_t)((GSHSCENE_PLACEHOLDER_SOURCE_COLR << 16) | GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED));
}

TEST(RenderCaps, TheWorkBoundIsTheWorstUnitsSumOfTheLimits) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 100;
  limits.max_deposits = 200;
  limits.max_bytes = 300;
  SinkRef sink(&limits);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 4, 4));
  GSHSCENE_Limits scene_limits{};
  scene_limits.max_resource_elements = 10;
  GSHSCENE_Sink * small = nullptr;
  ASSERT_EQ(gshcpu_sink_create(nullptr, &scene_limits, &limits, &small), GSHCPU_OK);
  // An aligned stroke: 3 parts of 100 points, 200 cells, two passes over 300 pixels, 10 elements to the depth cap, and
  // the exact step over the pieces its parts can hold (200 deposits and a horizontal piece for each point and column).
  EXPECT_EQ(caps_for(small, r.raster).cancel_work_bound, 300u + 200u + 600u + 10u * (GSHGEOM_STROKE_MAX_DEPTH + 1) +
      gshcpu_exact_work_bound(200u, 300u, GSHCPU_LIMITS_DEFAULT_MAX_DIMENSION, 3 * GSHCPU_LIMITS_DEFAULT_MAX_EXACT_FRAGMENTS,
            GSHCPU_LIMITS_DEFAULT_MAX_EXACT_CROSSINGS));
  EXPECT_EQ(gshscene_sink_destroy(small), GSHSCENE_OK);
  limits.max_deposits = SIZE_MAX;
  limits.max_points = SIZE_MAX;
  SinkRef wide(&limits);
  EXPECT_EQ(caps_for(wide.sink, r.raster).cancel_work_bound, UINT64_MAX) << "saturates";
}

TEST(RenderCaps, EveryOtherFormatAndTargetIsUnsupported) {
  SinkRef sink;
  std::vector<GIMG_Pixel_Format> refused;
  GIMG_Pixel_Format f = rf_pixel_format(GSHCPU_FORMAT_RGBA8_STRAIGHT);
  GIMG_Pixel_Format g = f;
  g.channel_model = GIMG_CHANNEL_GRAY;
  g.channel_count = 1;
  g.alpha_form = GIMG_ALPHA_NONE;
  std::memset(g.bits_per_channel, 0, sizeof g.bits_per_channel);
  g.bits_per_channel[0] = 8;
  refused.push_back(g);
  g = f;
  g.channel_model = GIMG_CHANNEL_RGB;
  g.channel_count = 3;
  g.alpha_form = GIMG_ALPHA_NONE;
  g.bits_per_channel[3] = 0;
  refused.push_back(g);
  g = f;
  for (int i = 0; i < 4; i++) {
    g.bits_per_channel[i] = 16;
  }
  refused.push_back(g); // RGBA16
  g = f;
  g.bits_per_channel[2] = 16;
  refused.push_back(g); // a channel of another width
  g = f;
  for (int i = 0; i < 4; i++) {
    g.bits_per_channel[i] = 12;
  }
  refused.push_back(g); // RGBA12
  g = f;
  g.layout = GIMG_LAYOUT_PLANAR;
  refused.push_back(g);
  g = f;
  g.channel_model = GIMG_CHANNEL_CMYK;
  g.alpha_form = GIMG_ALPHA_NONE;
  refused.push_back(g);
  g = f;
  g.channel_type = GIMG_CHANNEL_UINT;
  refused.push_back(g);
  g = rf_pixel_format(GSHCPU_FORMAT_RGBAF32_STRAIGHT);
  g.bits_per_channel[3] = 8;
  refused.push_back(g); // float with a narrow alpha (not creatable: skipped if image refuses it)
  int tried = 0;
  for (size_t i = 0; i < refused.size(); i++) {
    RasterRef r(raster_of(refused[i]));
    if (!r.raster) {
      continue;
    }
    tried++;
    EXPECT_EQ(gshcpu_format_of(r.raster), (GSHCPU_Format)0) << i;
    caps_for(sink.sink, r.raster, GSHSCENE_ERR_UNSUPPORTED);
    Store s;
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_UNSUPPORTED) << i;
    uint64_t h;
    EXPECT_EQ(gshcpu_render_hash(r.raster, nullptr, &h), GSHCPU_ERR_UNSUPPORTED) << i;
  }
  EXPECT_GE(tried, 7);
  // A stream: a document, not pixels.
  GSHSCENE_Writer writer{};
  writer.write = [](void *, const void *, size_t) { return GSHSCENE_OK; };
  GSHSCENE_Target stream{};
  stream.kind = GSHSCENE_TARGET_STREAM;
  stream.surface = &writer;
  stream.unit = GSHSCENE_UNIT_MILLIMETER;
  GSHSCENE_Sink_Caps caps{};
  EXPECT_EQ(gshscene_sink_caps(sink.sink, &stream, &caps), GSHSCENE_ERR_UNSUPPORTED);
  EXPECT_EQ(gshcpu_format_of(nullptr), (GSHCPU_Format)0);
}

TEST(RenderCaps, FormatNames) {
  EXPECT_STREQ(gshcpu_format_string(GSHCPU_FORMAT_RGBA8_STRAIGHT), "rgba8");
  EXPECT_STREQ(gshcpu_format_string(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED), "rgba8-premultiplied");
  EXPECT_STREQ(gshcpu_format_string(GSHCPU_FORMAT_RGBAF32_STRAIGHT), "rgbaf32");
  EXPECT_STREQ(gshcpu_format_string(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED), "rgbaf32-premultiplied");
  EXPECT_STREQ(gshcpu_format_string(GSHCPU_FORMAT_COUNT), "unknown format");
  EXPECT_STREQ(gshcpu_format_string((GSHCPU_Format)0), "unknown format");
  EXPECT_EQ((int)GSHCPU_FORMAT_RGBA8_STRAIGHT, 1);
  EXPECT_EQ((int)GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 2);
  EXPECT_EQ((int)GSHCPU_FORMAT_RGBAF32_STRAIGHT, 3);
  EXPECT_EQ((int)GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, 4);
}

TEST(RenderSink, CreateChecksItsArgumentsAndNamesItself) {
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, nullptr, nullptr), GSHCPU_ERR_INVALID);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_cache_bytes = GSHCPU_LIMITS_CACHE_OFF;
  ASSERT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &sink), GSHCPU_OK) << "every value of the cache field is one";
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  sink = nullptr;
  ASSERT_EQ(gshcpu_sink_create(nullptr, nullptr, nullptr, &sink), GSHCPU_OK);
  EXPECT_STREQ(gshscene_sink_name(sink), "shape-cpu");
  EXPECT_TRUE(gshcpu_sink_is(sink));
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  EXPECT_FALSE(gshcpu_sink_is(nullptr));
  GSHSCENE_Sink * dump = nullptr;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, nullptr, &dump), GSHSCENE_OK);
  EXPECT_FALSE(gshcpu_sink_is(dump));
  EXPECT_EQ(gshscene_sink_destroy(dump), GSHSCENE_OK);
}

TEST(RenderSink, EveryAllocationOfCreateCanFail) {
  for (long fail = 0; fail < 20; fail++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    t.fail_from = fail;
    GSHSCENE_Sink * sink = nullptr;
    GSHCPU_Result r = gshcpu_sink_create(&alloc, nullptr, nullptr, &sink);
    if (r == GSHCPU_OK) {
      EXPECT_FALSE(t.failed);
      EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
      EXPECT_TRUE(t.live.empty());
      return;
    }
    EXPECT_EQ(r, GSHCPU_ERR_OOM) << fail;
    EXPECT_TRUE(t.live.empty()) << fail;
  }
  ADD_FAILURE() << "create never succeeded";
}

/* --- the fixture set and its goldens ---------------------------------------------------------------- */

TEST(RenderFixtures, EveryFixtureOnEveryFormatIsItsGolden) {
  auto want = goldens();
  ASSERT_EQ(want.size(), RF_FIXTURE_COUNT * 4u) << "tests/corpus/render.hash is not read (run from the library's directory)";
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    for (GSHCPU_Format format : RF_FORMATS) {
      uint64_t h = 0;
      ASSERT_EQ(rf_render(i, format, &h, nullptr), GSHSCENE_OK) << RF_FIXTURES[i].name;
      auto it = want.find({RF_FIXTURES[i].name, gshcpu_format_string(format)});
      ASSERT_NE(it, want.end());
      EXPECT_EQ(h, it->second) << RF_FIXTURES[i].name << " on " << gshcpu_format_string(format);
    }
  }
}

TEST(RenderFixtures, TheReportForwardsTiesAndRefusesNothing) {
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Fixture fx(i);
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster, &fx.ctx.target_map);
    GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << RF_FIXTURES[i].name;
    GSHSCENE_Report_Summary sum = report.summary();
    EXPECT_EQ(sum.units_total, gshscene_list_expansion(fx.list));
    EXPECT_EQ(sum.units_done, sum.units_total);
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_REFUSED], 0u);
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_APPROXIMATED], 0u);
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_RESOLVED_AMBIGUITY], gshscene_list_tie_count(fx.list)) << RF_FIXTURES[i].name;
    EXPECT_GT(sum.work_max, 0u);
    EXPECT_GT(sum.frame_bytes, 0u);
  }
}

/** The pixels of the backdrop that a fixture draws over are the fresh raster's. */
TEST(RenderFixtures, APixelNoUnitTouchedIsNotWritten) {
  // An empty list writes nothing, on every format; a straight pixel of alpha 0 keeps its color bytes.
  for (GSHCPU_Format format : RF_FORMATS) {
    Store s;
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    RasterRef r(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    RasterRef fresh(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster);
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    EXPECT_EQ(r.bytes(), fresh.bytes()) << gshcpu_format_string(format);
    // A fill with no area, and one wholly outside, touch nothing either.
    GSHSCENE_List * m = s.open();
    GSHSCENE_Paint red = solid(1, 0, 0, 1);
    ASSERT_EQ(gshscene_list_fill(m, s.rect(3, 3, 3, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(m, s.rect(60, 3, 70, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(m), GSHSCENE_OK);
    req.list = m;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
    EXPECT_EQ(r.bytes(), fresh.bytes()) << gshcpu_format_string(format);
    // A box wholly outside the region is no work: the bound holds.
    GSHSCENE_Sink_Caps caps = caps_for(sink.sink, r.raster);
    GSHSCENE_Report_Summary sum{};
    gshscene_report_summary(report.report, &sum);
    EXPECT_LE(sum.work_max, caps.cancel_work_bound);
    EXPECT_LT(sum.work_max, 100u) << "an empty box has no pixels";
    EXPECT_STREQ(sum.failure.reason, "") << "a render that succeeded states no failure";
  }
}

/* --- damage and extent (S4) ----------------------------------------------------------------------------- */

TEST(RenderDamage, InsideEqualsTheWholeRenderAndOutsideIsUntouched) {
  std::mt19937 rng(1313);
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Fixture fx(i);
    for (GSHCPU_Format format : RF_FORMATS) {
      SinkRef sink;
      ReportRef report;
      RasterRef whole(rf_raster(format, RF_WIDTH, RF_HEIGHT));
      GSHSCENE_Target t = target_of(whole.raster, &fx.ctx.target_map);
      GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
      for (int round = 0; round < 3; round++) {
        int64_t x0 = (int64_t)(rng() % RF_WIDTH);
        int64_t y0 = (int64_t)(rng() % RF_HEIGHT);
        GSHSCENE_Rect damage{x0, y0, x0 + 1 + (int64_t)(rng() % (RF_WIDTH - x0)), y0 + 1 + (int64_t)(rng() % (RF_HEIGHT - y0))};
        if (round == 2) {
          damage = GSHSCENE_Rect{-5, -7, 100, 3}; // a damage reaching past the extent is clipped to it
        }
        RasterRef part(rf_raster(format, RF_WIDTH, RF_HEIGHT));
        RasterRef fresh(rf_raster(format, RF_WIDTH, RF_HEIGHT));
        GSHSCENE_Target pt = target_of(part.raster, &fx.ctx.target_map);
        pt.damage = &damage;
        req.target = &pt;
        ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
        for (uint32_t y = 0; y < RF_HEIGHT; y++) {
          for (uint32_t x = 0; x < RF_WIDTH; x++) {
            bool inside = (int64_t)x >= damage.x0 && (int64_t)x < damage.x1 && (int64_t)y >= damage.y0 && (int64_t)y < damage.y1;
            ASSERT_EQ(part.pixel(x, y), inside ? whole.pixel(x, y) : fresh.pixel(x, y))
                << RF_FIXTURES[i].name << " " << gshcpu_format_string(format) << " at " << x << "," << y;
          }
        }
        // The same rectangle as the extent draws the same.
        GSHSCENE_Rect clipped{std::max<int64_t>(damage.x0, 0), std::max<int64_t>(damage.y0, 0),
            std::min<int64_t>(damage.x1, RF_WIDTH), std::min<int64_t>(damage.y1, RF_HEIGHT)};
        RasterRef ext(rf_raster(format, RF_WIDTH, RF_HEIGHT));
        GSHSCENE_Target et = target_of(ext.raster, &fx.ctx.target_map);
        et.extent = clipped;
        req.target = &et;
        ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
        EXPECT_EQ(ext.bytes(), part.bytes()) << RF_FIXTURES[i].name;
        req.target = &t;
      }
    }
  }
}

TEST(RenderDamage, ADamageOutsideTheExtentDrawsNothing) {
  Fixture fx(0);
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  SinkRef sink;
  ReportRef report;
  GSHSCENE_Rect damage{10, 10, 20, 20};
  GSHSCENE_Target t = target_of(r.raster);
  t.extent = GSHSCENE_Rect{0, 0, 8, 8};
  t.damage = &damage;
  GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(r.bytes(), fresh.bytes());
  EXPECT_EQ(report.summary().units_done, gshscene_list_expansion(fx.list));
}

/* --- readback (S15) ------------------------------------------------------------------------------------- */

TEST(RenderReadback, ACompletedRenderReadsBackItsExtentOnEveryFormat) {
  Fixture fx(8); // the picture
  for (GSHCPU_Format format : RF_FORMATS) {
    SinkRef sink;
    ReportRef report;
    RasterRef r(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    GSHSCENE_Target t = target_of(r.raster);
    t.extent = GSHSCENE_Rect{5, 3, 41, 37};
    GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
    GSHSCENE_Job job{};
    ASSERT_EQ(gshscene_sink_begin(sink.sink, &req, &job), GSHSCENE_OK);
    RasterRef out(raster_of(rf_pixel_format(format), 36, 34));
    ASSERT_EQ(gshscene_sink_readback(sink.sink, &job, out.raster), GSHSCENE_OK);
    for (uint32_t y = 0; y < 34; y++) {
      for (uint32_t x = 0; x < 36; x++) {
        ASSERT_EQ(out.pixel(x, y), r.pixel(x + 5, y + 3)) << gshcpu_format_string(format);
      }
    }
    GSHSCENE_Rect e{5, 3, 41, 37};
    uint64_t a = 0;
    uint64_t b = 0;
    ASSERT_EQ(gshcpu_render_hash(r.raster, &e, &a), GSHCPU_OK);
    ASSERT_EQ(gshcpu_render_hash(out.raster, nullptr, &b), GSHCPU_OK);
    EXPECT_NE(a, b) << "the extent is hashed: a moved copy is another render";
  }
}

/* --- glyph runs -------------------------------------------------------------------------------------------- */

TEST(RenderGlyphs, ARunOfOutlinesDrawsAsOneShapeOfItsGlyphs) {
  // The two glyphs of the fixture, as one run and as one path holding both outlines moved by whole pixels.
  Store s;
  GSHGEOM_Path * both = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &both), GSHGEOM_OK);
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_Store * own = c.store;
  c.store = s.store; // intern into the test's store
  const GSHSCENE_Resource * o = rf_glyph(&c, 0);
  const GSHSCENE_Resource * v = rf_glyph(&c, 1);
  GSHSCENE_Glyph glyphs[2] = {{82, 0.0, 0.0, o}, {89, 13.0, 1.0, v}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
  s.held.push_back(r);
  // The same contours, written into one path.
  for (int g = 0; g < 2; g++) {
    const GSHGEOM_Path * p = gshscene_resource_path(glyphs[g].outline);
    for (size_t e = 0; e < gshgeom_path_element_count(p); e++) {
      GSHGEOM_Element el;
      ASSERT_EQ(gshgeom_path_element(p, e, &el), GSHGEOM_OK);
      GSHGEOM_Point q[4];
      for (unsigned k = 0; k < 4; k++) {
        q[k] = GSHGEOM_Point{el.curve.p[k].x + glyphs[g].x, el.curve.p[k].y + glyphs[g].y, 0};
      }
      switch (el.verb) {
        case GSHGEOM_VERB_MOVE: gshgeom_path_move_to(both, q[0]); break;
        case GSHGEOM_VERB_LINE: gshgeom_path_line_to(both, q[1]); break;
        case GSHGEOM_VERB_QUADRATIC: gshgeom_path_quadratic_to(both, q[1], q[2]); break;
        case GSHGEOM_VERB_CUBIC: gshgeom_path_cubic_to(both, q[1], q[2], q[3]); break;
        case GSHGEOM_VERB_RATIONAL_QUADRATIC: gshgeom_path_rational_quadratic_to(both, q[1], q[2], el.curve.w); break;
        case GSHGEOM_VERB_RATIONAL_CUBIC: gshgeom_path_rational_cubic_to(both, q[1], q[2], q[3], el.curve.w); break;
        case GSHGEOM_VERB_CLOSE: gshgeom_path_close(both); break;
        default: FAIL() << "verb"; break;
      }
    }
  }
  const GSHSCENE_Resource * path = s.path(both);
  for (GSHGEOM_Fill_Rule rule : {GSHGEOM_FILL_NONZERO, GSHGEOM_FILL_EVEN_ODD}) {
    GSHSCENE_Paint ink = solid(0.1, 0.1, 0.4, 1.0);
    GSHGEOM_Map place;
    gshgeom_map_translate(2, 4.0, 6.0, 0.0, &place);
    GSHSCENE_List * a = s.open();
    ASSERT_EQ(gshscene_list_fill(a, r, &place, rule, &ink, 0, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(a), GSHSCENE_OK);
    GSHSCENE_List * b = s.open();
    ASSERT_EQ(gshscene_list_fill(b, path, &place, rule, &ink, 0, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(b), GSHSCENE_OK);
    RasterRef ra(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    RasterRef rb(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target ta = target_of(ra.raster);
    GSHSCENE_Target tb = target_of(rb.raster);
    GSHSCENE_Render_Request qa = request_of(a, &ta, report.report);
    GSHSCENE_Render_Request qb = request_of(b, &tb, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &qa), GSHSCENE_OK);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &qb), GSHSCENE_OK);
    EXPECT_EQ(ra.bytes(), rb.bytes()) << "rule " << rule;
  }
  // rf_release would destroy the test's store: release the two outlines by hand instead, and the context's own store.
  for (size_t i = 0; i < c.held_count; i++) {
    gshscene_resource_release(c.held[i]);
  }
  gshscene_store_destroy(own);
}

TEST(RenderGlyphs, ARunOfAnUnboundFaceIsRefusedByThePlan) {
  // Story 14a: shape-cpu draws runs from a font, through their face's provider; a face with none is refused.
  Store s;
  GSHSCENE_Face_Desc desc{};
  desc.font_key = 0x1234;
  const GSHSCENE_Resource * face = nullptr;
  ASSERT_EQ(gshscene_store_intern_face(s.store, &desc, &face), GSHSCENE_OK);
  s.held.push_back(face);
  GSHSCENE_Glyph glyphs[1] = {{36, 2.0, 30.0, nullptr}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = 12.0;
  run.glyph_count = 1;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * font_run = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &font_run), GSHSCENE_OK);
  s.held.push_back(font_run);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 1, 9, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, font_run, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l, font_run, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(20, 1, 29, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  SinkRef sink;
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster);
  GSHSCENE_Render_Request req = request_of(l, &t, report.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(r.bytes(), fresh.bytes()) << "strict: the plan refused before the sink was called";
  req.flags = 0;
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  auto e = report.entries();
  ASSERT_EQ(e.size(), 2u);
  EXPECT_EQ(e[0].reason, GSHSCENE_REASON_FACE_UNBOUND);
  EXPECT_EQ(e[1].reason, GSHSCENE_REASON_FACE_UNBOUND);
  // The first fill is drawn, the clip block (and the fill in it) is skipped.
  EXPECT_NE(r.pixel(4, 4), fresh.pixel(4, 4));
  EXPECT_EQ(r.pixel(24, 4), fresh.pixel(24, 4));
}

/* --- the same drawing, immediate and retained ------------------------------------------------------------ */

TEST(RenderRetained, AVersionsListRendersAsTheRecordedListOfTheSameDrawing) {
  GSHSCENE_Scene * scene = nullptr;
  ASSERT_EQ(gshscene_scene_create(nullptr, nullptr, nullptr, &scene), GSHSCENE_OK);
  GSHSCENE_Store * store = gshscene_scene_store(scene);
  std::vector<const GSHSCENE_Resource *> held;
  const GSHSCENE_Resource * a = rect(store, held, 2, 2, 20, 18);
  const GSHSCENE_Resource * b = rect(store, held, 10, 8, 30, 30);
  const GSHSCENE_Resource * clip = rect(store, held, 4.5, 3.25, 26.75, 24.5);
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_Store * own = c.store;
  c.store = store;
  const GSHSCENE_Resource * star = rf_star(&c, 30.0, 22.0, 12.0);
  GSHSCENE_Edit * edit = nullptr;
  ASSERT_EQ(gshscene_scene_open_edit(scene, nullptr, &edit), GSHSCENE_OK);
  GSHSCENE_Node an;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &an);
  an.resource = a;
  an.paint = solid(0.9, 0.3, 0.1, 1.0);
  uint64_t id = 0;
  ASSERT_EQ(gshscene_edit_add(edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &an, nullptr, &id), GSHSCENE_OK);
  GSHSCENE_Node gn;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &gn);
  gn.opacity = 0.5;
  gn.clip = clip;
  gn.clip_rule = GSHGEOM_FILL_EVEN_ODD;
  gn.blend_space.primaries = GCOL_PRIMARIES_SRGB;
  gn.blend_space.transfer = GCOL_TRANSFER_LINEAR;
  gshgeom_map_translate(2, 1.5, 0.75, 0.0, &gn.map);
  uint64_t g = 0;
  ASSERT_EQ(gshscene_edit_add(edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &gn, nullptr, &g), GSHSCENE_OK);
  GSHSCENE_Node bn;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &bn);
  bn.resource = b;
  bn.paint = solid(0.1, 0.4, 0.9, 0.8);
  ASSERT_EQ(gshscene_edit_add(edit, g, GSHSCENE_APPEND, &bn, nullptr, &id), GSHSCENE_OK);
  GSHSCENE_Node sn;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &sn);
  sn.resource = star;
  sn.fill_rule = GSHGEOM_FILL_EVEN_ODD;
  sn.paint.kind = GSHSCENE_PAINT_FOREGROUND;
  sn.paint.rgba[0] = sn.paint.rgba[1] = sn.paint.rgba[2] = 0.0;
  sn.paint.rgba[3] = 0.75;
  ASSERT_EQ(gshscene_edit_add(edit, g, GSHSCENE_APPEND, &sn, nullptr, &id), GSHSCENE_OK);
  GSHSCENE_Color fg;
  gshscene_color_init(&fg);
  fg.rgba[1] = 0.6;
  ASSERT_EQ(gshscene_edit_set_foreground(edit, &fg), GSHSCENE_OK);
  const GSHSCENE_Version * version = nullptr;
  ASSERT_EQ(gshscene_scene_publish(scene, edit, nullptr, &version), GSHSCENE_OK);
  gshscene_edit_destroy(edit);
  const GSHSCENE_List * emitted = nullptr;
  ASSERT_EQ(gshscene_version_list(version, &emitted), GSHSCENE_OK);

  // The same drawing, recorded.
  GSHSCENE_List * recorded = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, store, &recorded), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_set_foreground(recorded, &fg), GSHSCENE_OK);
  GSHSCENE_Paint pa = an.paint;
  ASSERT_EQ(gshscene_list_fill(recorded, a, nullptr, GSHGEOM_FILL_NONZERO, &pa, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(recorded, clip, &gn.map, GSHGEOM_FILL_EVEN_ODD, 0, 0), GSHSCENE_OK);
  GSHSCENE_Group grp{};
  grp.opacity = 0.5;
  grp.blend_space = gn.blend_space;
  ASSERT_EQ(gshscene_list_group_push(recorded, &grp, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(recorded, b, &gn.map, GSHGEOM_FILL_NONZERO, &bn.paint, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(recorded, star, &gn.map, GSHGEOM_FILL_EVEN_ODD, &sn.paint, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(recorded), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(recorded), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(recorded), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_equal(emitted, recorded), 1);

  for (GSHCPU_Format format : RF_FORMATS) {
    RasterRef ra(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    RasterRef rb(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    RasterRef fresh(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target ta = target_of(ra.raster);
    GSHSCENE_Target tb = target_of(rb.raster);
    GSHSCENE_Render_Request qa = request_of(emitted, &ta, report.report);
    GSHSCENE_Render_Request qb = request_of(recorded, &tb, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &qa), GSHSCENE_OK);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &qb), GSHSCENE_OK);
    EXPECT_EQ(ra.bytes(), rb.bytes()) << gshcpu_format_string(format);
    EXPECT_NE(ra.bytes(), fresh.bytes());
  }
  gshscene_list_destroy(recorded);
  gshscene_version_release(version);
  for (const GSHSCENE_Resource * r : held) {
    gshscene_resource_release(r);
  }
  for (size_t i = 0; i < c.held_count; i++) {
    gshscene_resource_release(c.held[i]);
  }
  gshscene_scene_destroy(scene);
  gshscene_store_destroy(own);
}

/* --- the render hash and dump ------------------------------------------------------------------------------- */

TEST(RenderHash, BadArgumentsAndFailingStreams) {
  RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, 6, 5));
  uint64_t h = 0;
  EXPECT_EQ(gshcpu_render_hash(nullptr, nullptr, &h), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_render_hash(r.raster, nullptr, nullptr), GSHCPU_ERR_INVALID);
  for (GSHSCENE_Rect e : {GSHSCENE_Rect{0, 0, 0, 5}, GSHSCENE_Rect{0, 0, 6, 0}, GSHSCENE_Rect{-1, 0, 6, 5},
           GSHSCENE_Rect{0, -1, 6, 5}, GSHSCENE_Rect{0, 0, 7, 5}, GSHSCENE_Rect{0, 0, 6, 6}, GSHSCENE_Rect{3, 0, 3, 5},
           GSHSCENE_Rect{0, 2, 6, 2}}) {
    EXPECT_EQ(gshcpu_render_hash(r.raster, &e, &h), GSHCPU_ERR_INVALID) << e.x0 << "," << e.y0 << "," << e.x1 << "," << e.y1;
    EXPECT_EQ(gshcpu_render_dump(r.raster, &e, stdout), GSHCPU_ERR_INVALID);
  }
  GSHSCENE_Rect all{0, 0, 6, 5};
  uint64_t a = 0;
  ASSERT_EQ(gshcpu_render_hash(r.raster, &all, &a), GSHCPU_OK);
  ASSERT_EQ(gshcpu_render_hash(r.raster, nullptr, &h), GSHCPU_OK);
  EXPECT_EQ(a, h) << "NULL is the whole raster";
  EXPECT_EQ(gshcpu_render_dump(r.raster, nullptr, nullptr), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_render_dump(nullptr, nullptr, stdout), GSHCPU_ERR_INVALID);
  FILE * full = std::fopen("/dev/full", "w");
  if (!full) {
    GTEST_SKIP() << "no /dev/full on this host";
  }
  EXPECT_EQ(gshcpu_render_dump(r.raster, nullptr, full), GSHCPU_ERR_IO);
  std::fclose(full);
  // Unbuffered: each write fails where it is made, at every line of the dump.
  for (int rows = 0; rows <= 5; rows++) {
    full = std::fopen("/dev/full", "w");
    ASSERT_NE(full, nullptr);
    std::setvbuf(full, nullptr, _IONBF, 0);
    GSHSCENE_Rect e{0, 0, 6, rows ? rows : 1};
    EXPECT_EQ(gshcpu_render_dump(r.raster, &e, full), GSHCPU_ERR_IO) << rows;
    std::fclose(full);
  }
  FILE * ro = std::fopen("/dev/null", "r");
  ASSERT_NE(ro, nullptr);
  EXPECT_EQ(gshcpu_render_dump(r.raster, nullptr, ro), GSHCPU_ERR_IO);
  std::fclose(ro);
}

TEST(RenderHash, EveryChannelAndThePlaceOfTheExtentAreHashed) {
  for (GSHCPU_Format format : RF_FORMATS) {
    RasterRef r(rf_raster(format, 5, 4));
    uint64_t base = r.hash();
    size_t bytes = gshcpu_render_pixel_bytes(format);
    unsigned char * px = (unsigned char *)gimg_raster_pixels(r.raster);
    for (size_t b = 0; b < bytes; b++) {
      px[b] ^= 0x40;
      EXPECT_NE(r.hash(), base) << gshcpu_format_string(format) << " byte " << b;
      px[b] ^= 0x40;
    }
    GSHSCENE_Rect e1{0, 0, 2, 2};
    GSHSCENE_Rect e2{0, 0, 2, 3};
    uint64_t h1 = 0;
    uint64_t h2 = 0;
    ASSERT_EQ(gshcpu_render_hash(r.raster, &e1, &h1), GSHCPU_OK);
    ASSERT_EQ(gshcpu_render_hash(r.raster, &e2, &h2), GSHCPU_OK);
    EXPECT_NE(h1, h2);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
