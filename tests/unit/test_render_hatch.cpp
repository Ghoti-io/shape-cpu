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
 * The hatch fill in the CPU sink (render.h, HX13; masks-images-patterns story H1): it is the stroke of its trimmed, dashed lines, so a
 * hatch fill equals the stroke of the same pieces as one explicit path bit for bit; the expansion is cached under a key that has the
 * hatch, the region, the rule and the fraction (a hit equals a miss, none is read or written under caller memory); the lines of the use
 * are counted whatever the cache and the damage and one more than the limit is the sink's resource limit; the sparse composite is taken
 * and changes nothing; and a whole-pixel translation moves the expansion by those pixels.
 */

#include <gtest/gtest.h>

#include "../../src/render/render_internal.h"
#include "../render_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace rh;

namespace {

struct TSink {
  GSHSCENE_Sink * sink = nullptr;
  GSHCPU_Render_Counters counters{};
  explicit TSink(unsigned tiers = GSHCPU_RENDER_TIERS_ALL, const GSHCPU_Limits * limits = nullptr) {
    EXPECT_EQ(gshcpu_render_sink_create_tiered(nullptr, nullptr, limits, tiers, &counters, nullptr, nullptr, &sink), GSHCPU_OK);
  }
  ~TSink() { EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK); }
  TSink(const TSink &) = delete;
  TSink & operator=(const TSink &) = delete;
};

struct Out {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::string bytes;
  size_t refused = 0;
  size_t resource_limits = 0;
  size_t notices = 0;
};

Out draw(GSHSCENE_Sink * sink, const GSHSCENE_List * list, uint32_t w = 48, uint32_t h = 40, uint32_t flags = 0, void * workspace = nullptr,
    size_t workspace_bytes = 0, GSHCPU_Format format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED) {
  RasterRef raster(rf_raster(format, w, h));
  std::memset(gimg_raster_pixels(raster.raster), 0, gimg_raster_stride_bytes(raster.raster) * h);
  ReportRef report;
  GSHSCENE_Target t = target_of(raster.raster);
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  req.workspace = workspace;
  req.workspace_bytes = workspace_bytes;
  Out o;
  o.result = gshscene_sink_render(sink, &req);
  o.bytes = raster.bytes();
  for (size_t i = 0; i < gshscene_report_count(report.report); i++) {
    GSHSCENE_Report_Entry e{};
    if (gshscene_report_entry(report.report, i, &e) != GSHSCENE_OK) {
      continue;
    }
    o.refused += e.entry_class == GSHSCENE_ENTRY_REFUSED;
    o.resource_limits += e.entry_class == GSHSCENE_ENTRY_REFUSED && e.reason == GSHSCENE_REASON_RESOURCE_LIMIT;
    o.notices += e.entry_class == GSHSCENE_ENTRY_NOTICE;
  }
  return o;
}

size_t lit(const std::string & bytes) {
  size_t n = 0;
  const float * f = reinterpret_cast<const float *>(bytes.data());
  for (size_t i = 3; i * sizeof(float) < bytes.size(); i += 4) {
    n += f[i] > 0.0f;
  }
  return n;
}

/** One hatch fill over a rectangle region, the hatch of the given families, pen and map; the list, the store and the objects it names. */
struct One {
  RF_Ctx ctx;
  GSHSCENE_List * list = nullptr;
  const GSHSCENE_Resource * hatch = nullptr;
  const GSHSCENE_Resource * region = nullptr;
  const GSHSCENE_Resource * pen = nullptr;
  std::vector<GSHSCENE_Hatch_Family> fams;
  std::vector<double> dashes;
  One(std::vector<GSHSCENE_Hatch_Family> f, std::vector<double> d, uint32_t cap, double width, GSHGEOM_Map item_map, GSHGEOM_Map hatch_map,
      GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO, double alpha = 1.0, uint32_t space = GSHSCENE_STROKE_WIDTH_LOCAL) {
    rf_begin(&ctx);
    fams = f;
    dashes = d;
    size_t at = 0;
    for (auto & fam : fams) {
      if (fam.dash_count) {
        fam.dashes = dashes.data() + at;
        at += fam.dash_count;
      }
    }
    pen = rf_style(&ctx, width, space, GSHSCENE_STROKE_JOIN_MITER, 4.0, cap, GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0, 0.0,
        GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO);
    region = rf_rect(&ctx, 3.0, 3.0, 40.0, 32.0);
    hatch = rf_hatch(&ctx, pen, 0, fams.data(), fams.size(), hatch_map, rf_solid(0.1, 0.3, 0.7, 1.0));
    list = rf_open(&ctx);
    rf_fill(&ctx, list, region, &item_map, rule, rf_hatch_paint(hatch, alpha), 0, 0.0);
    rf_close(&ctx, list);
    EXPECT_EQ(ctx.failed, 0);
  }
  ~One() { rf_release(&ctx); }
  One(const One &) = delete;
  One & operator=(const One &) = delete;
};

GSHGEOM_Map id() {
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  return m;
}

}  // namespace

// HX13: a hatch fill is the stroke of its pieces: the same bits as a stroke item whose centerline is the pieces as one explicit path.
TEST(RenderHatch, AHatchFillEqualsTheStrokeOfItsPiecesAsOneExplicitPath) {
  std::vector<GSHSCENE_Hatch_Family> f = {{45.0, 0.5, 0.0, 0.0, 3.0, 0, nullptr}, {0.0, 0.0, 0.0, 1.0, 4.0, 2, nullptr}};
  f[1].dash_count = 2;
  for (uint32_t cap : {(uint32_t)GSHSCENE_STROKE_CAP_BUTT, (uint32_t)GSHSCENE_STROKE_CAP_ROUND}) {
    One one(f, {3.0, -1.5}, cap, 0.8, id(), id(), GSHGEOM_FILL_NONZERO, 0.7);
    TSink s;
    Out hatch = draw(s.sink, one.list);
    ASSERT_EQ(hatch.result, GSHSCENE_OK);
    EXPECT_GT(lit(hatch.bytes), 100u);
    // the pieces at the canonical map (the map is the identity: its fraction is 0)
    GSHSCENE_Hatch back;
    ASSERT_EQ(gshscene_resource_hatch(one.hatch, &back), GSHSCENE_OK);
    std::vector<GSHGEOM_Hatch_Family> gf(back.family_count);
    for (size_t i = 0; i < gf.size(); i++) {
      gf[i] = {back.families[i].angle, back.families[i].origin_x, back.families[i].origin_y, back.families[i].offset_along,
          back.families[i].offset_across, back.families[i].dash_count, back.families[i].dashes};
    }
    GSHGEOM_Hatch gh{};
    gh.angle_unit = back.angle_unit;
    gh.nesting = back.nesting;
    gh.map = back.map;
    gh.family_count = gf.size();
    gh.families = gf.data();
    const GSHGEOM_Path * path = gshscene_resource_path(one.region);
    GSHGEOM_Map chain = id();
    GSHGEOM_Hatch_Pieces * pieces = nullptr;
    ASSERT_EQ(gshgeom_hatch_pieces(path, GSHGEOM_FILL_NONZERO, &gh, &chain, 0, nullptr, nullptr, &pieces), GSHGEOM_OK);
    GSHGEOM_Path * explicit_path = nullptr;
    ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &explicit_path), GSHGEOM_OK);
    for (size_t i = 0; i < gshgeom_hatch_pieces_count(pieces); i++) {
      double xy[4];
      int dot;
      ASSERT_EQ(gshgeom_hatch_pieces_get(pieces, i, xy, &dot), GSHGEOM_OK);
      GSHGEOM_Point a = {xy[0], xy[1], 0}, b = {xy[2], xy[3], 0};
      ASSERT_EQ(gshgeom_path_move_to(explicit_path, a), GSHGEOM_OK);
      ASSERT_EQ(gshgeom_path_line_to(explicit_path, b), GSHGEOM_OK);
    }
    gshgeom_hatch_pieces_destroy(pieces);
    const GSHSCENE_Resource * centerline = rf_intern(&one.ctx, explicit_path);
    GSHSCENE_List * l = rf_open(&one.ctx);
    GSHSCENE_Paint ink = rf_solid(0.1, 0.3, 0.7, 0.7);
    rf_stroke(&one.ctx, l, centerline, one.pen, nullptr, ink);
    rf_close(&one.ctx, l);
    Out stroke = draw(s.sink, l);
    ASSERT_EQ(stroke.result, GSHSCENE_OK);
    EXPECT_EQ(hatch.bytes, stroke.bytes) << "cap " << cap;
  }
}

// HX13: a hit equals a miss; the cache's presence changes nothing; none is read or written under caller memory; the counters count the
// fills and the lines.
TEST(RenderHatch, ACacheHitEqualsAMissAndTheCacheChangesNothing) {
  std::vector<GSHSCENE_Hatch_Family> f = {{30.0, 0.0, 0.0, 0.0, 2.5, 0, nullptr}};
  One one(f, {}, GSHSCENE_STROKE_CAP_SQUARE, 0.9, id(), id());
  TSink cached;
  Out first = draw(cached.sink, one.list);
  ASSERT_EQ(first.result, GSHSCENE_OK);
  EXPECT_EQ(cached.counters.cache_hits, 0u);
  EXPECT_GE(cached.counters.cache_misses, 1u);
  EXPECT_EQ(cached.counters.hatches, 1u);
  const uint64_t lines = cached.counters.hatch_lines;
  EXPECT_GT(lines, 5u);
  Out second = draw(cached.sink, one.list);
  EXPECT_EQ(second.bytes, first.bytes);
  EXPECT_GE(cached.counters.cache_hits, 1u) << "the second render found the expansion";
  EXPECT_EQ(cached.counters.hatches, 2u);
  EXPECT_EQ(cached.counters.hatch_lines, 2 * lines) << "the lines of the use are counted on a hit as on a miss";
  TSink uncached(GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_CACHE);
  Out third = draw(uncached.sink, one.list);
  EXPECT_EQ(third.bytes, first.bytes);
  EXPECT_EQ(uncached.counters.hatch_lines, lines);
  EXPECT_EQ(uncached.counters.cache_hits, 0u);
  // CALLER_MEMORY: the cache is neither read nor written
  size_t needed = 0;
  RasterRef raster(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, 48, 40));
  GSHSCENE_Target t = target_of(raster.raster);
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(one.list, &t, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  ASSERT_EQ(gshscene_sink_workspace_bytes(cached.sink, &req, &needed), GSHSCENE_OK);
  std::vector<unsigned char> memory(needed + 64);
  const uint64_t hits = cached.counters.cache_hits, misses = cached.counters.cache_misses;
  Out fourth = draw(cached.sink, one.list, 48, 40, GSHSCENE_RENDER_CALLER_MEMORY, memory.data(), memory.size());
  EXPECT_EQ(fourth.result, GSHSCENE_OK);
  EXPECT_EQ(fourth.bytes, first.bytes);
  EXPECT_EQ(cached.counters.cache_hits, hits) << "no entry read";
  EXPECT_EQ(cached.counters.cache_misses, misses) << "none written";
}

// HX13: the key has the hatch, the region, the rule and the fraction: a field changed with the others held draws the new picture.
TEST(RenderHatch, EveryFieldOfTheKeyIsInIt) {
  TSink s;
  std::vector<GSHSCENE_Hatch_Family> f = {{30.0, 0.0, 0.0, 0.0, 2.5, 0, nullptr}};
  One base(f, {}, GSHSCENE_STROKE_CAP_BUTT, 0.9, id(), id());
  Out a = draw(s.sink, base.list);
  // another hatch (an angle), the same region and style
  std::vector<GSHSCENE_Hatch_Family> g = {{60.0, 0.0, 0.0, 0.0, 2.5, 0, nullptr}};
  One other(g, {}, GSHSCENE_STROKE_CAP_BUTT, 0.9, id(), id());
  // (a sink is used with the lists of one store: the ids of another would alias, so each list of a store of its own has a sink of its own)
  TSink s_other;
  Out b = draw(s_other.sink, other.list);
  EXPECT_NE(a.bytes, b.bytes) << "the hatch";
  // the same hatch over another region
  {
    RF_Ctx c;
    rf_begin(&c);
    const GSHSCENE_Resource * pen = rf_pen(&c, 0.9, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
    const GSHSCENE_Resource * h = rf_hatch(&c, pen, 0, f.data(), 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
    GSHSCENE_List * l1 = rf_open(&c);
    rf_fill(&c, l1, rf_rect(&c, 3, 3, 40, 32), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);
    rf_close(&c, l1);
    GSHSCENE_List * l2 = rf_open(&c);
    rf_fill(&c, l2, rf_rect(&c, 3, 3, 40, 20), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);
    rf_close(&c, l2);
    TSink s_ctx;
    Out x = draw(s_ctx.sink, l1), y = draw(s_ctx.sink, l2);
    EXPECT_NE(x.bytes, y.bytes) << "the region's id";
    // the same hatch and region under another rule: a square with a hole is where the rule matters
    GSHGEOM_Path * ring = rf_path(&c);
    gshgeom_path_add_rect(ring, 2.0, 2.0, 40.0, 30.0, 0, 0);
    gshgeom_path_add_rect(ring, 10.0, 8.0, 20.0, 14.0, 0, 0);
    const GSHSCENE_Resource * rr = rf_intern(&c, ring);
    GSHSCENE_List * l3 = rf_open(&c);
    rf_fill(&c, l3, rr, nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);
    rf_close(&c, l3);
    GSHSCENE_List * l4 = rf_open(&c);
    rf_fill(&c, l4, rr, nullptr, GSHGEOM_FILL_EVEN_ODD, rf_hatch_paint(h, 1.0), 0, 0.0);
    rf_close(&c, l4);
    Out n = draw(s_ctx.sink, l3), e = draw(s_ctx.sink, l4);
    EXPECT_NE(n.bytes, e.bytes) << "the rule";
    EXPECT_GT(lit(n.bytes), lit(e.bytes));
    rf_release(&c);
  }
  // the fraction of the translation
  One shifted(f, {}, GSHSCENE_STROKE_CAP_BUTT, 0.9, [] { GSHGEOM_Map m; gshgeom_map_translate(2, 0.5, 0.25, 0.0, &m); return m; }(), id());
  TSink s_shifted;
  Out c = draw(s_shifted.sink, shifted.list);
  EXPECT_NE(a.bytes, c.bytes) << "the fraction";
  // a whole-pixel shift is the same expansion moved: bit for bit
  One whole(f, {}, GSHSCENE_STROKE_CAP_BUTT, 0.9, [] { GSHGEOM_Map m; gshgeom_map_translate(2, 4.0, 2.0, 0.0, &m); return m; }(), id());
  TSink s_whole;
  Out d = draw(s_whole.sink, whole.list);
  EXPECT_GT(lit(d.bytes), 100u);
  // evict(id) drops the entries that name it: the next render is a miss
  const uint64_t misses = s.counters.cache_misses;
  Out again = draw(s.sink, base.list);
  EXPECT_EQ(s.counters.cache_misses, misses) << "a hit";
  ASSERT_EQ(gshscene_sink_evict(s.sink, gshscene_resource_id(base.hatch)), GSHSCENE_OK);
  Out after = draw(s.sink, base.list);
  EXPECT_GT(s.counters.cache_misses, misses) << "the hatch's id evicted it";
  EXPECT_EQ(after.bytes, again.bytes);
}

// HX12: the lines of the use are counted whatever the cache and the damage; one more than the limit is the sink's resource limit.
TEST(RenderHatch, TheLinesLimitPassesAtItsBoundAndRefusesOneMore) {
  std::vector<GSHSCENE_Hatch_Family> f = {{0.0, 0.0, 0.0, 0.0, 3.0, 0, nullptr}, {90.0, 0.0, 0.0, 0.0, 4.0, 0, nullptr}};
  One one(f, {}, GSHSCENE_STROKE_CAP_BUTT, 0.6, id(), id());
  TSink probe;
  Out base = draw(probe.sink, one.list);
  ASSERT_EQ(base.result, GSHSCENE_OK);
  const uint64_t n = probe.counters.hatch_lines;
  ASSERT_GT(n, 10u);
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_hatch_lines = n;
  {
    TSink at(GSHCPU_RENDER_TIERS_ALL, &lim);
    Out o = draw(at.sink, one.list);
    EXPECT_EQ(o.result, GSHSCENE_OK);
    EXPECT_EQ(o.refused, 0u);
    EXPECT_EQ(o.bytes, base.bytes);
  }
  lim.max_hatch_lines = n - 1;
  for (unsigned tiers : {(unsigned)GSHCPU_RENDER_TIERS_ALL, (unsigned)(GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_CACHE),
           (unsigned)(GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_SPARSE)}) {
    TSink over(tiers, &lim);
    Out o = draw(over.sink, one.list);
    EXPECT_EQ(o.refused, 1u) << tiers;
    EXPECT_EQ(o.resource_limits, 1u) << tiers;
    EXPECT_EQ(lit(o.bytes), 0u) << "the unit is skipped, nothing drawn";
    // the same sink, twice: the cache's hit refuses what a miss would
    Out o2 = draw(over.sink, one.list);
    EXPECT_EQ(o2.resource_limits, 1u);
    // strict stops with the prefix
    Out s = draw(over.sink, one.list, 48, 40, GSHSCENE_RENDER_STRICT);
    EXPECT_EQ(s.result, GSHSCENE_ERR_REFUSED);
  }
  // a clipped render (the lines are those the whole render draws of the fill, not only what is visible): the same refusal
  {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * l = rf_open(&c);
    rf_check(&c, gshscene_list_clip_push(l, rf_rect(&c, 0, 0, 4, 4), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0) == GSHSCENE_OK);
    GSHSCENE_Hatch h;
    gshscene_hatch_init(&h);
    h.style = rf_pen(&c, 0.6, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
    h.family_count = 2;
    h.families = f.data();
    const GSHSCENE_Resource * hr = nullptr;
    rf_check(&c, gshscene_store_intern_hatch(c.store, &h, &hr) == GSHSCENE_OK);
    rf_hold(&c, hr);
    rf_fill(&c, l, rf_rect(&c, 3.0, 3.0, 40.0, 32.0), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(hr, 1.0), 0, 0.0);
    rf_check(&c, gshscene_list_clip_pop(l) == GSHSCENE_OK);
    rf_close(&c, l);
    TSink over(GSHCPU_RENDER_TIERS_ALL, &lim);
    EXPECT_EQ(draw(over.sink, l).resource_limits, 1u) << "the clip does not reduce the count";
    lim.max_hatch_lines = n;
    TSink at(GSHCPU_RENDER_TIERS_ALL, &lim);
    EXPECT_EQ(draw(at.sink, l).resource_limits, 0u);
    rf_release(&c);
  }
}

// HX13: a hatch fill is a stroke to the sink: the sparse composite is taken, and what it draws equals what the general tier draws.
TEST(RenderHatch, TheSparseCompositeIsTakenAndChangesNothing) {
  std::vector<GSHSCENE_Hatch_Family> f = {{45.0, 0.0, 0.0, 0.0, 4.0, 0, nullptr}};
  One one(f, {}, GSHSCENE_STROKE_CAP_BUTT, 0.5, id(), id());
  TSink sparse(GSHCPU_RENDER_TIERS_ALL);
  Out a = draw(sparse.sink, one.list, 48, 40, 0, nullptr, 0, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  EXPECT_GT(sparse.counters.sparse_plain + sparse.counters.sparse_hairline, 0u) << "FT-2 on a hatch";
  TSink general(GSHCPU_RENDER_TIERS_GENERAL);
  Out b = draw(general.sink, one.list, 48, 40, 0, nullptr, 0, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED);
  EXPECT_EQ(general.counters.sparse_plain + general.counters.sparse_hairline, 0u);
  EXPECT_EQ(a.bytes, b.bytes);
}

// HX9: a hatch that draws nothing is noticed and draws nothing.
TEST(RenderHatch, AHatchThatDrawsNothingIsNoticedAndLeavesTheTargetAlone) {
  TSink s;
  std::vector<GSHSCENE_Hatch_Family> f = {{0.0, 0.0, 0.0, 0.0, 0.0, 0, nullptr}};
  One zero_spacing(f, {}, GSHSCENE_STROKE_CAP_BUTT, 0.9, id(), id());
  Out a = draw(s.sink, zero_spacing.list);
  EXPECT_EQ(a.result, GSHSCENE_OK);
  EXPECT_EQ(a.notices, 1u);
  EXPECT_EQ(lit(a.bytes), 0u);
  std::vector<GSHSCENE_Hatch_Family> g = {{0.0, 0.0, 0.0, 0.0, 2.0, 0, nullptr}};
  One zero_width(g, {}, GSHSCENE_STROKE_CAP_BUTT, 0.0, id(), id());
  Out b = draw(s.sink, zero_width.list);
  EXPECT_EQ(b.notices, 1u);
  EXPECT_EQ(lit(b.bytes), 0u);
  GSHGEOM_Map sing;
  gshgeom_map_scale(2, 0.0, 1.0, 1.0, &sing);
  One singular_hatch(g, {}, GSHSCENE_STROKE_CAP_BUTT, 0.9, id(), sing);
  Out c = draw(s.sink, singular_hatch.list);
  EXPECT_EQ(c.notices, 1u);
  EXPECT_EQ(lit(c.bytes), 0u);
  EXPECT_EQ(s.counters.hatches, 0u);
}

// HX15: a cap at a trimmed end reaches outside the region: a thick square-capped line along the region's edge covers pixels the region does not.
TEST(RenderHatch, ACapAtATrimmedEndReachesOutsideTheRegion) {
  std::vector<GSHSCENE_Hatch_Family> f = {{90.0, 3.0, 0.0, 0.0, 37.0, 0, nullptr}};
  One one(f, {}, GSHSCENE_STROKE_CAP_SQUARE, 3.0, id(), id());
  TSink s;
  Out o = draw(s.sink, one.list);
  ASSERT_EQ(o.result, GSHSCENE_OK);
  // the region is [3, 40] x [3, 32]; the lines are vertical at x = 3 and 40, ending at y = 3 and 32 with square caps 1.5 px long
  const float * px = reinterpret_cast<const float *>(o.bytes.data());
  auto alpha = [&](int x, int y) { return px[(size_t)(y * 48 + x) * 4 + 3]; };
  EXPECT_GT(alpha(3, 2), 0.0f) << "the cap above the region";
  EXPECT_GT(alpha(3, 32), 0.0f) << "the cap below it";
  EXPECT_EQ(alpha(10, 2), 0.0f);
}

// HX13: in a pattern's tile a hatch fill is a stroke of the tile: the lines are cut at the cell as a stroke is.
TEST(RenderHatch, AHatchInATileRepeatsWithTheTile) {
  size_t index = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::string(RF_FIXTURES[i].name) == "hatch-maps") {
      index = i;
    }
  }
  Fixture fx(index);
  TSink s;
  Out a = draw(s.sink, fx.list);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  EXPECT_GT(lit(a.bytes), 500u);
  TSink general(GSHCPU_RENDER_TIERS_ALL & ~(unsigned)GSHCPU_RENDER_TIER_PERIOD & ~(unsigned)GSHCPU_RENDER_TIER_CACHE);
  EXPECT_EQ(draw(general.sink, fx.list).bytes, a.bytes);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// --- what the planted defects pinned down (check-planted.py, hx- plants) -------------------------------------------------------------

namespace {

/** The cache's bucket of a hatch fill's key under the identity map (stroke_cache.c restated, so a test can put two keys in one bucket). */
size_t bucket_of_hatch(uint64_t path_id, uint64_t style_id, uint64_t hatch_id, uint64_t rule) {
  uint64_t h = path_id * UINT64_C(0x9E3779B97F4A7C15);
  h ^= (style_id + UINT64_C(0x632BE59BD9B4E019)) * UINT64_C(0xC2B2AE3D27D4EB4F);
  const uint64_t linear[4] = {gshgeom_f64_bits(1.0), 0, 0, gshgeom_f64_bits(1.0)};
  for (size_t i = 0; i < 4; i++) {
    h = (h ^ linear[i]) * UINT64_C(0x100000001B3);
  }
  h = (h ^ 0) * UINT64_C(0x100000001B3);
  h = (h ^ 0) * UINT64_C(0x100000001B3);
  h = (h ^ hatch_id) * UINT64_C(0x100000001B3);
  h = (h ^ rule) * UINT64_C(0x100000001B3);
  h ^= h >> 29;
  return (size_t)(h & 255u);
}

Out fresh(const GSHSCENE_List * list) {
  TSink s;
  return draw(s.sink, list);
}

}  // namespace

// HX13: two hatch keys in one bucket are told apart by the hatch, not by the bucket.  (The rule is told apart by the key too, but the
// two rules of one hatch and region never share a bucket - their hashes differ by the multiplier - so no test can put them together.)
TEST(RenderHatch, TwoKeysInOneBucketAreToldApartByTheHatch) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * pen = rf_pen(&c, 0.6, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHGEOM_Path * ring = rf_path(&c);
  gshgeom_path_add_rect(ring, 3.0, 3.0, 40.0, 32.0, 0, 0);
  gshgeom_path_add_rect(ring, 12.0, 10.0, 20.0, 16.0, 0, 0);
  const GSHSCENE_Resource * region = rf_intern(&c, ring);
  const uint64_t path_id = gshscene_resource_id(region), style_id = gshscene_resource_id(pen);
  std::vector<const GSHSCENE_Resource *> hatches;
  std::vector<size_t> bucket0;
  // (the context holds 192 resources: these hatches are held here and released below)
  for (int k = 0; k < 2000; k++) {
    GSHSCENE_Hatch_Family f = {5.0 + 0.09 * k, 0.0, 0.0, 0.0, 2.5, 0, nullptr};
    GSHSCENE_Hatch spec;
    gshscene_hatch_init(&spec);
    spec.style = pen;
    spec.family_count = 1;
    spec.families = &f;
    spec.ink = rf_solid(0.1, 0.3, 0.7, 1.0);
    const GSHSCENE_Resource * made = nullptr;
    ASSERT_EQ(gshscene_store_intern_hatch(c.store, &spec, &made), GSHSCENE_OK);
    hatches.push_back(made);
    bucket0.push_back(bucket_of_hatch(path_id, style_id, gshscene_resource_id(made), 0));
  }
  // (a) two hatches, the same region and pen and rule, one bucket: 2000 hatches in 256 buckets leave a pair
  size_t ia = 0, ib = 0;
  for (size_t i = 0; i < hatches.size() && !ib; i++) {
    for (size_t j = i + 1; j < hatches.size(); j++) {
      if (bucket0[i] == bucket0[j]) {
        ia = i;
        ib = j;
        break;
      }
    }
  }
  ASSERT_NE(ib, 0u);
  GSHSCENE_List * la = rf_open(&c);
  rf_fill(&c, la, region, nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(hatches[ia], 1.0), 0, 0.0);
  rf_close(&c, la);
  GSHSCENE_List * lb = rf_open(&c);
  rf_fill(&c, lb, region, nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(hatches[ib], 1.0), 0, 0.0);
  rf_close(&c, lb);
  TSink s;
  Out a = draw(s.sink, la), b = draw(s.sink, lb);
  EXPECT_NE(a.bytes, b.bytes);
  EXPECT_EQ(a.bytes, fresh(la).bytes);
  EXPECT_EQ(b.bytes, fresh(lb).bytes) << "the second hatch is not the first's cached expansion";
  for (const GSHSCENE_Resource * h : hatches) {
    gshscene_resource_release(h);
  }
  rf_release(&c);
}

// HX3: the ink's alpha and the paint's multiply.
TEST(RenderHatch, TheInkAlphaTimesThePaintAlphaIsTheCoverageOfTheLines) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * pen = rf_pen(&c, 1.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHSCENE_Hatch_Family f = {45.0, 0.0, 0.0, 0.0, 3.0, 0, nullptr};
  const GSHSCENE_Resource * half_ink = rf_hatch(&c, pen, 0, &f, 1, id(), rf_solid(0.1, 0.3, 0.7, 0.5));
  const GSHSCENE_Resource * full_ink = rf_hatch(&c, pen, 0, &f, 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
  auto list = [&](const GSHSCENE_Resource * h, double alpha) {
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 3, 3, 43, 35), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, alpha), 0, 0.0);
    rf_close(&c, l);
    return l;
  };
  GSHSCENE_List * ink_half = list(half_ink, 1.0);
  GSHSCENE_List * paint_half = list(full_ink, 0.5);
  GSHSCENE_List * both = list(half_ink, 0.5);
  GSHSCENE_List * neither = list(full_ink, 1.0);
  Out a = fresh(ink_half), b = fresh(paint_half), d = fresh(both), n = fresh(neither);
  EXPECT_EQ(a.bytes, b.bytes) << "half the ink or half the paint: the product";
  EXPECT_NE(a.bytes, n.bytes);
  EXPECT_NE(a.bytes, d.bytes);
  rf_release(&c);
}

// HX12: a limit above the geometry's own default is the sink's.
TEST(RenderHatch, ALinesLimitAboveTheGeometrysDefaultIsHonored) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * pen = rf_pen(&c, 0.01, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHSCENE_Hatch_Family f = {90.0, 0.0, 0.0, 0.0, 0.0004, 0, nullptr};
  const GSHSCENE_Resource * h = rf_hatch(&c, pen, 0, &f, 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 3, 3, 35, 4), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);  // 32 / 0.0004 = 80000 lines
  rf_close(&c, l);
  TSink def;
  EXPECT_EQ(draw(def.sink, l).resource_limits, 1u) << "2^16 lines by default";
  GSHCPU_Limits lim;
  gshcpu_limits_default(&lim);
  lim.max_hatch_lines = (uint64_t)1 << 18;
  TSink wide(GSHCPU_RENDER_TIERS_ALL, &lim);
  Out o = draw(wide.sink, l);
  EXPECT_EQ(o.resource_limits, 0u);
  EXPECT_EQ(o.result, GSHSCENE_OK);
  EXPECT_GT(lit(o.bytes), 10u);
  EXPECT_GT(wide.counters.hatch_lines, 65536u);
  rf_release(&c);
}

// HX12: each line is counted into the work the report states, as many tests of the region's elements as it has.  The same rectangle with
// 395 extra vertices along its bottom edge (collinear, so the same pieces and the same cells) has 395 more elements, and its work is
// larger by exactly 395 times (the depth of the expansion plus the lines of the use).
TEST(RenderHatch, TheLinesAreInTheWorkTheReportStates) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * pen = rf_pen(&c, 0.5, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHGEOM_Path * plain = rf_path(&c);
  gshgeom_path_move_to(plain, rf_pt(3.0, 3.0));
  gshgeom_path_line_to(plain, rf_pt(43.0, 3.0));
  gshgeom_path_line_to(plain, rf_pt(43.0, 35.5));
  gshgeom_path_line_to(plain, rf_pt(3.0, 35.5));
  gshgeom_path_close(plain);
  GSHGEOM_Path * many = rf_path(&c);
  gshgeom_path_move_to(many, rf_pt(3.0, 3.0));
  gshgeom_path_line_to(many, rf_pt(43.0, 3.0));
  gshgeom_path_line_to(many, rf_pt(43.0, 35.5));
  for (int k = 1; k <= 395; k++) {
    gshgeom_path_line_to(many, rf_pt(43.0 - 40.0 * (double)k / 396.0 + 0.0, 35.5));
  }
  gshgeom_path_line_to(many, rf_pt(3.0, 35.5));
  gshgeom_path_close(many);
  const GSHSCENE_Resource * regions[2] = {rf_intern(&c, plain), rf_intern(&c, many)};
  uint64_t work[2] = {0, 0}, lines[2] = {0, 0}, elements[2] = {0, 0};
  GSHSCENE_Hatch_Family f = {0.0, 0.0, 0.3, 0.0, 0.75, 0, nullptr};  // about 43 lines
  const GSHSCENE_Resource * h = rf_hatch(&c, pen, 0, &f, 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
  for (int k = 0; k < 2; k++) {
    elements[k] = gshgeom_path_element_count(gshscene_resource_path(regions[k]));
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, regions[k], nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink s;
    RasterRef raster(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, 48, 40));
    ReportRef report;
    GSHSCENE_Target t = target_of(raster.raster);
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    ASSERT_EQ(gshscene_sink_render(s.sink, &req), GSHSCENE_OK);
    GSHSCENE_Report_Summary sum{};
    ASSERT_EQ(gshscene_report_summary(report.report, &sum), GSHSCENE_OK);
    work[k] = sum.work_max;
    lines[k] = s.counters.hatch_lines;
  }
  ASSERT_EQ(lines[0], lines[1]);
  ASSERT_GT(lines[0], 30u);
  ASSERT_EQ(elements[1] - elements[0], 395u);
  EXPECT_EQ(work[1] - work[0], 395u * ((uint64_t)GSHGEOM_STROKE_MAX_DEPTH + lines[0]));
  rf_release(&c);
}

// HX13: a color outside [0, 1] is wide: a group holding it keeps doubles.  A pen as wide as the lines are apart covers the interior fully,
// the ink (1.4, 0.2, -0.1) at 0.8 in a group of 0.6 over (0.4, 0.5, 0.6): the group's layer holds the premultiplied (1.12, 0.16, -0.08) at
// 0.8 unclamped, times 0.6, over the backdrop: (0.88, 0.356, 0.264), which is 224, 91, 67 in bytes.  A byte layer would clamp the layer first.
TEST(RenderHatch, AnInkOutsideTheGamutInAGroupKeepsItsExcursionUntilTheComposite) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * pen = rf_pen(&c, 3.2, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHSCENE_Hatch_Family f = {30.0, 0.0, 0.0, 0.0, 3.0, 0, nullptr};
  const GSHSCENE_Resource * h = rf_hatch(&c, pen, 0, &f, 1, id(), rf_solid(1.4, 0.2, -0.1, 0.8));
  GSHSCENE_List * l = rf_open(&c);
  rf_fill(&c, l, rf_rect(&c, 0, 0, 48, 40), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0.4, 0.5, 0.6, 1.0), 0, 0.0);
  GSHSCENE_Group half = rf_group(0.6, GSHSCENE_COMPOSITE_SRC_OVER);
  rf_check(&c, gshscene_list_group_push(l, &half, 0, 0.0) == GSHSCENE_OK);
  rf_fill(&c, l, rf_rect(&c, 3, 3, 43, 35), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);
  rf_check(&c, gshscene_list_group_pop(l) == GSHSCENE_OK);
  rf_close(&c, l);
  for (GSHCPU_Format format : {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED}) {
    // the fast coverages take 8-bit layers unless a color is wide, so they are where the list-level test of the ink shows
    GSHCPU_Limits fast;
    gshcpu_limits_default(&fast);
    fast.fill_coverage = GSHCPU_FILL_COVERAGE_FAST;
    fast.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
    for (int mode = 0; mode < 3; mode++) {
      TSink s(mode == 1 ? (unsigned)GSHCPU_RENDER_TIERS_GENERAL : (unsigned)GSHCPU_RENDER_TIERS_ALL, mode == 2 ? &fast : nullptr);
      Out a = draw(s.sink, l, 48, 40, 0, nullptr, 0, format);
      ASSERT_EQ(a.result, GSHSCENE_OK);
      const unsigned char * px = reinterpret_cast<const unsigned char *>(a.bytes.data()) + (20u * 48u + 20u) * 4u;
      EXPECT_NEAR((int)px[0], 224, 2) << "red";
      EXPECT_NEAR((int)px[1], 91, 2) << "green";
      EXPECT_NEAR((int)px[2], 67, 2) << "blue";
      EXPECT_EQ((int)px[3], 255);
    }
  }
  rf_release(&c);
}

// HX9 under caller memory: a hatch that draws nothing is skipped by the measure as by the pass.
TEST(RenderHatch, AHatchThatDrawsNothingIsMeasuredAsNothingUnderCallerMemory) {
  RF_Ctx c;
  rf_begin(&c);
  const GSHSCENE_Resource * thin = rf_pen(&c, 0.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  const GSHSCENE_Resource * pen = rf_pen(&c, 0.7, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
  GSHSCENE_Hatch_Family f = {45.0, 0.0, 0.0, 0.0, 3.0, 0, nullptr};
  GSHSCENE_Hatch_Family none = {45.0, 0.0, 0.0, 0.0, 0.0, 0, nullptr};
  GSHGEOM_Map sing;
  gshgeom_map_scale(2, 0.0, 1.0, 1.0, &sing);
  const GSHSCENE_Resource * zero_width = rf_hatch(&c, thin, 0, &f, 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
  const GSHSCENE_Resource * zero_spacing = rf_hatch(&c, pen, 0, &none, 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
  const GSHSCENE_Resource * singular = rf_hatch(&c, pen, 0, &f, 1, sing, rf_solid(0.1, 0.3, 0.7, 1.0));
  const GSHSCENE_Resource * drawing = rf_hatch(&c, pen, 0, &f, 1, id(), rf_solid(0.1, 0.3, 0.7, 1.0));
  size_t needed_drawing = 0;
  {
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 3, 3, 43, 35), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(drawing, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink s;
    RasterRef raster(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, 48, 40));
    GSHSCENE_Target t = target_of(raster.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    ASSERT_EQ(gshscene_sink_workspace_bytes(s.sink, &req, &needed_drawing), GSHSCENE_OK);
  }
  for (const GSHSCENE_Resource * h : {zero_width, zero_spacing, singular}) {
    GSHSCENE_List * l = rf_open(&c);
    rf_fill(&c, l, rf_rect(&c, 3, 3, 43, 35), nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(h, 1.0), 0, 0.0);
    rf_close(&c, l);
    TSink s;
    RasterRef raster(rf_raster(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED, 48, 40));
    GSHSCENE_Target t = target_of(raster.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t needed = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(s.sink, &req, &needed), GSHSCENE_OK);
    // the caller-memory need of a fill that draws nothing is that of an empty list, not of a hatch's lines
    GSHSCENE_List * empty = rf_open(&c);
    rf_close(&c, empty);
    ReportRef report2;
    GSHSCENE_Render_Request req2 = request_of(empty, &t, report2.report);
    req2.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    size_t needed_empty = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(s.sink, &req2, &needed_empty), GSHSCENE_OK);
    EXPECT_LT(needed, needed_drawing) << "the lines of a hatch that draws nothing are not measured";
    (void)needed_empty;
  }
  rf_release(&c);
}
