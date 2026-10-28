/**
 * @file
 *
 * Stroked text in the CPU sink (strokes story S5, ST1 to ST11): a stroke of a run is a stroke of the path
 * ::gshscene_glyph_run_centerline defines, drawn by the same machinery, so a stroke of a run and a stroke of its path are
 * the same bit for bit (pixels, the test sink's outline hash, the report).  Also: one union across the run, no hinting,
 * color glyphs, the plan's refusals, the order of a fill and a stroke, the cache and its replayed notes, caller memory
 * and the exact workspace, the limits that refuse the whole item, damage, and the bounds and work the story states.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"

#include <ghoti.io/shape-scene/dump_sink.h>
#include <ghoti.io/shape-scene/glyph.h>
#include <ghoti.io/shape-scene/testing/glyph_test_provider.h>

#include <cstring>
#include <string>
#include <vector>

using namespace sh;
using stroke_gate::Params;

namespace {

/** A glyph of a run: the test provider's id and the position. */
struct G {
  uint32_t id;
  double x;
  double y;
};

/** A store, the test provider and the faces and runs made from it. */
struct Text {
  Store s;
  GSHSCENE_Test_Provider tp{};
  explicit Text(uint32_t flags = GSHSCENE_FACE_HINTING) {
    tp.upem = 2048;
    tp.flags = flags;
  }
  const GSHSCENE_Resource * face(int fast = 1) {
    const GSHSCENE_Resource * f = gshscene_tp_face(s.store, &tp, fast, 0x71, 0);
    EXPECT_NE(f, nullptr);
    s.held.push_back(f);
    return f;
  }
  const GSHSCENE_Resource * unbound() {
    GSHSCENE_Face_Desc desc{};
    desc.font_key = 0x99;
    const GSHSCENE_Resource * f = nullptr;
    EXPECT_EQ(gshscene_store_intern_face(s.store, &desc, &f), GSHSCENE_OK);
    s.held.push_back(f);
    return f;
  }
  const GSHSCENE_Resource * run(const GSHSCENE_Resource * f, double size, uint32_t mode, const std::vector<G> & g,
      uint32_t version = 0) {
    std::vector<GSHSCENE_Glyph> glyphs;
    for (const G & x : g) {
      glyphs.push_back(GSHSCENE_Glyph{x.id, x.x, x.y, nullptr});
    }
    GSHSCENE_Glyph_Run r{};
    r.source = GSHSCENE_GLYPHS_FROM_FONT;
    r.face = f;
    r.size = size;
    r.hint_mode = mode;
    r.hint_version = version;
    r.glyph_count = glyphs.size();
    r.glyphs = glyphs.empty() ? nullptr : glyphs.data();
    const GSHSCENE_Resource * out = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &r, &out), GSHSCENE_OK);
    s.held.push_back(out);
    return out;
  }
  /** A run from outlines: closed and open shapes, and a glyph with an empty outline. */
  const GSHSCENE_Resource * outlines() {
    GSHGEOM_Path * z = stroke_gate::build({{'M', {0.0, 8.0}}, {'L', {3.0, 0.0}}, {'L', {6.0, 8.0}}, {'L', {9.0, 0.0}}, {'L', {12.0, 8.0}}});
    const GSHSCENE_Resource * zr = s.path(z);
    GSHGEOM_Path * q = stroke_gate::build({{'M', {0.0, 0.0}}, {'L', {7.0, 1.0}}, {'Q', {9.0, 5.0, 5.0, 7.0}}, {'Z', {}}});
    const GSHSCENE_Resource * qr = s.path(q);
    const GSHSCENE_Resource * nothing = s.path(stroke_gate::build({}));
    GSHSCENE_Glyph glyphs[3] = {{1, 2.0, 3.0, zr}, {2, 3.0, 12.0, qr}, {3, 20.0, 9.0, nothing}};
    GSHSCENE_Glyph_Run r{};
    r.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    r.glyph_count = 3;
    r.glyphs = glyphs;
    const GSHSCENE_Resource * out = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &r, &out), GSHSCENE_OK);
    s.held.push_back(out);
    return out;
  }
  /** A run from outlines of straight lines only, closed and open, overlapping: what an aligned stroke can take at the default limits. */
  const GSHSCENE_Resource * polygons() {
    const GSHSCENE_Resource * a = s.path(stroke_gate::build({{'M', {0.0, 0.0}}, {'L', {9.0, 1.0}}, {'L', {8.0, 8.0}}, {'L', {1.0, 7.0}}, {'Z', {}}}));
    const GSHSCENE_Resource * b = s.path(stroke_gate::build({{'M', {0.0, 0.0}}, {'L', {6.0, 3.0}}, {'L', {2.0, 9.0}}}));
    GSHSCENE_Glyph glyphs[3] = {{1, 2.0, 3.0, a}, {2, 7.0, 4.0, a}, {3, 16.0, 6.0, b}};
    GSHSCENE_Glyph_Run r{};
    r.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    r.glyph_count = 3;
    r.glyphs = glyphs;
    const GSHSCENE_Resource * out = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &r, &out), GSHSCENE_OK);
    s.held.push_back(out);
    return out;
  }
  /** ST1's path for @p run, interned as a path resource of the store. */
  const GSHSCENE_Resource * centerline(const GSHSCENE_Resource * run) {
    GSHGEOM_Path * p = nullptr;
    EXPECT_EQ(gshscene_glyph_run_centerline(run, nullptr, nullptr, &p, nullptr), GSHSCENE_OK);
    return s.path(p);
  }
  const GSHSCENE_List * stroke(const GSHSCENE_Resource * centerline_res, const GSHGEOM_Stroke & g, const GSHGEOM_Map * map,
      GSHSCENE_Paint paint) {
    const GSHSCENE_Resource * style = intern_style(s, g);
    GSHSCENE_List * l = s.open();
    EXPECT_EQ(gshscene_list_stroke(l, centerline_res, style, map, &paint, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  }
};

Params pen(double width = 1.5, uint32_t join = GSHGEOM_STROKE_JOIN_ROUND, uint32_t cap = GSHGEOM_STROKE_CAP_BUTT) {
  Params p;
  p.s.width = width;
  p.s.join = join;
  p.s.cap = cap;
  return p;
}

GSHGEOM_Map rotation_about(double degrees, double tx, double ty) {
  GSHGEOM_Map r;
  GSHGEOM_Map t;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, degrees, &r);
  gshgeom_map_translate(2, tx, ty, 0.0, &t);
  gshgeom_map_compose(&t, &r, &r);
  return r;
}

/** The entries of a render, as comparable text. */
std::string entries_text(const Drawn & d) {
  std::string out;
  for (const GSHSCENE_Report_Entry & e : d.entries) {
    out += std::to_string(e.entry_class) + ":" + std::to_string(e.reason) + ":" + std::to_string(e.second) + ";";
  }
  return out;
}

const std::vector<G> kOverlap = {{1, 2.0, 22.0}, {2, 6.0, 22.0}, {3, 9.0, 22.0}, {2, 14.5, 22.0}};

}  // namespace

/* --- ST11: a stroke of a run is a stroke of its path --------------------------------------------------------------- */

TEST(RenderStrokeText, AStrokeOfARunAndAStrokeOfItsPathAreTheSameBitForBitInEveryFormat) {
  struct Case {
    const char * name;
    Params p;
    GSHGEOM_Map map;
  };
  std::vector<Case> cases;
  cases.push_back({"round join, butt cap", pen(1.5), translation(0.375, 0.625)});
  cases.push_back({"miter join, square cap, dashes", pen(2.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE), translation(1.0, 2.0)});
  cases.back().p.dashes = {4.0, 2.0};
  cases.push_back({"bevel join, round caps, a pattern across the path", pen(1.25, GSHGEOM_STROKE_JOIN_BEVEL, GSHGEOM_STROKE_CAP_ROUND),
      translation(0.25, 0.5)});
  cases.back().p.dashes = {3.0, 1.0, 1.0, 1.0};
  cases.back().p.s.dash_scope = GSHGEOM_STROKE_DASH_PATH;
  cases.push_back({"inside", pen(3.0), translation(0.5, 0.0)});
  cases.back().p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  cases.push_back({"outside, even-odd", pen(3.0), translation(0.0, 0.5)});
  cases.back().p.s.alignment = GSHGEOM_STROKE_ALIGN_OUTSIDE;
  cases.back().p.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
  cases.push_back({"a hairline in device width under a rotation", pen(1.0), rotation_about(23.0, 3.0, 1.0)});
  cases.back().p.s.width_space = GSHGEOM_STROKE_WIDTH_DEVICE;
  cases.push_back({"the elliptical pen", pen(2.0, GSHGEOM_STROKE_JOIN_ARCS), affine_map(1.6, 0.0, 0.0, 0.8, 1.0, 2.0)});
  Text t;
  const GSHSCENE_Resource * font = t.run(t.face(), 12.0, GSHSCENE_HINT_FONT_STRICT, kOverlap);
  const GSHSCENE_Resource * outlines = t.outlines();
  // An aligned stroke of curves, overlapping, is the dense case the default limits refuse; it takes straight lines.
  const GSHSCENE_Resource * font_lines = t.run(t.face(), 12.0, GSHSCENE_HINT_FONT, {{1, 2.0, 22.0}, {1, 9.0, 22.0}, {1, 16.0, 22.0}});
  const GSHSCENE_Resource * polygons = t.polygons();
  GSHSCENE_Paint paint = solid(0.7, 0.2, 0.1, 0.8);
  for (int pass = 0; pass < 4; pass++) {
   const GSHSCENE_Resource * run = pass == 0 ? font : pass == 1 ? outlines : pass == 2 ? font_lines : polygons;
   {
    const GSHSCENE_Resource * as_path = t.centerline(run);
    for (Case & c : cases) {
      if ((c.p.s.alignment != 0) != (pass >= 2)) {
        continue;
      }
      const GSHSCENE_List * lr = t.stroke(run, c.p.get(), &c.map, paint);
      const GSHSCENE_List * lp = t.stroke(as_path, c.p.get(), &c.map, paint);
      for (GSHCPU_Format format : {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED}) {
        for (unsigned tiers : {(unsigned)GSHCPU_RENDER_TIERS_ALL, 0u}) {
          Sink sink(tiers);
          Drawn a = render_list(sink.sink, lr, 40, 36, format);
          Drawn b = render_list(sink.sink, lp, 40, 36, format);
          ASSERT_EQ(a.result, GSHSCENE_OK) << c.name;
          ASSERT_EQ(b.result, GSHSCENE_OK) << c.name;
          EXPECT_EQ(a.hash, b.hash) << c.name << ": the stroke of a run is the stroke of its path";
          EXPECT_EQ(entries_text(a), entries_text(b)) << c.name;
          EXPECT_EQ(a.summary.by_class[GSHSCENE_ENTRY_REFUSED], 0u) << c.name << ": " << entries_text(a);
        }
      }
    }
   }
  }
}

TEST(RenderStrokeText, ThePaintIsSampledInTheRunsSpaceSoOneGradientSpansTheRunAndAFillAndAStrokeShareIt) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 10.0, GSHSCENE_HINT_NONE, {{1, 2.0, 20.0}, {2, 14.0, 20.0}, {3, 26.0, 20.0}});
  const GSHSCENE_Resource * as_path = t.centerline(run);
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(1, 0, 0, 1)}, {1.0, solid(0, 0, 1, 1)}};
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p0[0] = 2.0;
  g.p1[0] = 36.0;
  gshgeom_map_identity(2, &g.map);
  g.stop_count = 2;
  g.stops = stops;
  const GSHSCENE_Resource * gr = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(t.s.store, &g, &gr), GSHSCENE_OK);
  t.s.held.push_back(gr);
  GSHSCENE_Paint paint{};
  paint.kind = GSHSCENE_PAINT_GRADIENT;
  paint.resource = gr;
  paint.rgba[3] = 1.0;
  Params p = pen(2.5, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  GSHGEOM_Map m = translation(0.25, 0.5);
  Sink sink;
  Drawn a = render_list(sink.sink, t.stroke(run, p.get(), &m, paint), 40, 28);
  Drawn b = render_list(sink.sink, t.stroke(as_path, p.get(), &m, paint), 40, 28);
  EXPECT_EQ(a.hash, b.hash) << "equal to the gradient stroke of the path";
  // One gradient across the run: the third glyph (x about 26 to 36) is bluer than the first (x about 2 to 8), not a
  // gradient that starts again at each glyph.
  RasterRef raster(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 40, 28));
  GSHSCENE_Target tg = target_of(raster.raster);
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(t.stroke(run, p.get(), &m, paint), &tg, report.report);
  ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
  auto reddest = [&](uint32_t x0, uint32_t x1) {
    int best = -1;
    int bluest = -1;
    for (uint32_t y = 0; y < 28; y++) {
      for (uint32_t x = x0; x < x1; x++) {
        std::string px = raster.pixel(x, y);
        if ((uint8_t)px[3] > 200) {
          best = std::max(best, (int)(uint8_t)px[0]);
          bluest = std::max(bluest, (int)(uint8_t)px[2]);
        }
      }
    }
    return std::make_pair(best, bluest);
  };
  auto first = reddest(0, 9);
  auto last = reddest(28, 40);
  EXPECT_GT(first.first, last.first) << "the first glyph is redder";
  EXPECT_GT(last.second, first.second) << "and the last bluer";
  EXPECT_GT(first.first, 200) << "the gradient starts at the first glyph and is not restarted";
  EXPECT_GT(last.second, 150);
}

/* --- ST2: one union across the run ----------------------------------------------------------------------------------- */

TEST(RenderStrokeText, GlyphsThatOverlapAreOneUnionAndNothingIsDarkerWhereTheyCross) {
  Text t;
  GSHSCENE_Paint translucent = solid(0.1, 0.2, 0.8, 0.5);
  Params p = pen(1.5, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  GSHGEOM_Map m = translation(0.375, 0.5);
  Sink sink;
  // The same glyph three times over, at one place: the union of the same region is the region, so the render is that of
  // one glyph, not three strokes composited one on another.
  const GSHSCENE_Resource * once = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 4.0, 22.0}});
  const GSHSCENE_Resource * thrice = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 4.0, 22.0}, {2, 4.0, 22.0}, {2, 4.0, 22.0}});
  Drawn one = render_list(sink.sink, t.stroke(once, p.get(), &m, translucent), 30, 28);
  Drawn three = render_list(sink.sink, t.stroke(thrice, p.get(), &m, translucent), 30, 28);
  EXPECT_EQ(three.hash, one.hash) << "no darkening where glyphs coincide";
  EXPECT_EQ(three.alpha, one.alpha);
  // Glyphs that merely overlap: no pixel is more opaque than a stroke composited once can make it (0.5), where the
  // strokes of the glyphs alone would reach 0.75 at a crossing.
  const GSHSCENE_Resource * overlapping = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 2.0, 22.0}, {2, 6.0, 22.0}, {2, 10.0, 22.0}});
  Drawn d = render_list(sink.sink, t.stroke(overlapping, p.get(), &m, translucent), 30, 28);
  uint8_t most = 0;
  for (uint8_t a : d.alpha) {
    most = std::max(most, a);
  }
  uint8_t single = 0;
  for (uint8_t a : one.alpha) {
    single = std::max(single, a);
  }
  EXPECT_EQ(most, single) << "a pixel inside the union is one coat of a half-transparent paint, however many glyphs cross there";
  // The coverage of the union, against the geometric oracle's bracket on the path and against an independent area of the
  // union, over the styles that matter to text.
  const GSHSCENE_Resource * as_path = t.centerline(overlapping);
  GSHGEOM_Path * path = nullptr;
  ASSERT_EQ(gshscene_glyph_run_centerline(overlapping, nullptr, nullptr, &path, nullptr), GSHSCENE_OK);
  struct Style {
    Params p;
    int alignment;
  };
  std::vector<Style> styles;
  styles.push_back({pen(1.5, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND), 0});
  styles.push_back({pen(3.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_BUTT), 0});
  styles.push_back({pen(2.0, GSHGEOM_STROKE_JOIN_BEVEL, GSHGEOM_STROKE_CAP_SQUARE), 0});
  styles.back().p.dashes = {3.0, 2.0};
  for (Style & st : styles) {
    st.p.s.alignment = (uint32_t)st.alignment;
    GSHSCENE_Paint white = solid(1, 1, 1, 1);
    Drawn w = render_list(sink.sink, t.stroke(as_path, st.p.get(), &m, white), 30, 30);
    Drawn run_w = render_list(sink.sink, t.stroke(overlapping, st.p.get(), &m, white), 30, 30);
    EXPECT_EQ(w.alpha, run_w.alpha);
    stroke_gate::Result br = stroke_gate::bracket(path, st.p.get(), m, st.alignment, false, 30, 30, run_w.alpha);
    EXPECT_EQ(br.violations, 0) << br.first;
    stroke_gate::UnionResult un = stroke_gate::union_gate(path, st.p.get(), m, 30, 30, run_w.alpha, false, 1024);
    EXPECT_EQ(un.violations, 0) << un.first;
  }
  gshgeom_path_destroy(path);
}

/* --- ST3, ST4: no hinting, no color ---------------------------------------------------------------------------------- */

TEST(RenderStrokeText, AStrokeIsNeverHintedNeverRoutedAndNeverReadsTheColorTables) {
  GSHSCENE_Paint paint = solid(0.9, 0.3, 0.1, 1.0);
  Params p = pen(1.0);
  GSHGEOM_Map aligned = translation(2.0, 1.0); // a grid-aligned map at an integer ppem: a fill takes the fast tier here
  std::string reference;
  for (uint32_t mode : {(uint32_t)GSHSCENE_HINT_NONE, (uint32_t)GSHSCENE_HINT_FONT, (uint32_t)GSHSCENE_HINT_FONT_STRICT}) {
    for (uint32_t flags : {(uint32_t)GSHSCENE_FACE_HINTING, (uint32_t)(GSHSCENE_FACE_HINTING | GSHSCENE_FACE_COLOR), 0u}) {
      Text t(flags);
      const GSHSCENE_Resource * run = t.run(t.face(), 16.0, mode, {{1, 1.0, 20.0}, {2, 9.0, 20.0}}, 77);
      Sink sink;
      gshscene_tp_reset(&t.tp);
      Drawn d = render_list(sink.sink, t.stroke(run, p.get(), &aligned, paint), 28, 26);
      ASSERT_EQ(d.result, GSHSCENE_OK);
      EXPECT_EQ(d.entries.size(), 0u) << "no HINT_* entry for a stroke, in any mode";
      EXPECT_EQ(gcu_atomic_size_load(&t.tp.fast_calls), 0u) << "the fast tier is not called";
      EXPECT_GT(gcu_atomic_size_load(&t.tp.outline_calls), 0u);
      if (reference.empty()) {
        reference = std::to_string(d.hash);
      }
      EXPECT_EQ(std::to_string(d.hash), reference) << "mode " << mode << " flags " << flags << ": the same pixels";
    }
  }
  // A fill of the same run at the same size is hinted (the fast tier) and a stroke of it is not: that is X3's note.
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 16.0, GSHSCENE_HINT_FONT, {{1, 1.0, 20.0}});
  GSHSCENE_List * l = t.s.open();
  EXPECT_EQ(gshscene_list_fill(l, run, &aligned, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
  gshscene_list_close(l);
  Sink sink;
  gshscene_tp_reset(&t.tp);
  render_list(sink.sink, l, 28, 26);
  EXPECT_EQ(gcu_atomic_size_load(&t.tp.fast_calls), 1u);
}

/* --- ST5: the plan -------------------------------------------------------------------------------------------------- */

TEST(RenderStrokeText, AnUnboundFaceIsRefusedByThePlanBeforeTheTargetIsTouchedAndWhatDrawsNothingIsNot) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.unbound(), 12.0, GSHSCENE_HINT_FONT_STRICT, {{1, 1.0, 10.0}});
  GSHSCENE_Paint paint = solid(1, 1, 1, 1);
  Params p = pen(2.0);
  GSHGEOM_Map id = translation(0, 0);
  const GSHSCENE_List * l = t.stroke(run, p.get(), &id, paint);
  Sink sink;
  Drawn d = render_list(sink.sink, l, 16, 16);
  EXPECT_EQ(d.result, GSHSCENE_OK);
  ASSERT_EQ(d.entries.size(), 1u);
  EXPECT_EQ(d.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_FACE_UNBOUND);
  for (uint8_t a : d.alpha) {
    EXPECT_EQ(a, 0);
  }
  Drawn strict = render_list(sink.sink, l, 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
  // Width 0 and a singular map on the same run: the notice alone, strict OK.
  Params zero = pen(0.0);
  Drawn z = render_list(sink.sink, t.stroke(run, zero.get(), &id, paint), 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(z.result, GSHSCENE_OK);
  ASSERT_EQ(z.entries.size(), 1u);
  EXPECT_EQ(z.entries[0].entry_class, GSHSCENE_ENTRY_NOTICE);
}

TEST(RenderStrokeText, ASingularMapAndANoOutlineRunDrawNothingWithNoEntryOrOneNotice) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 12.0, GSHSCENE_HINT_NONE, {{1, 1.0, 10.0}});
  const GSHSCENE_Resource * spaces = t.run(t.face(), 12.0, GSHSCENE_HINT_NONE, {{0, 1.0, 10.0}, {0, 8.0, 10.0}});
  GSHSCENE_Paint paint = solid(1, 1, 1, 1);
  Params p = pen(2.0);
  GSHGEOM_Map flat = affine_map(0.0, 0.0, 0.0, 1.0, 3.0, 3.0);
  GSHGEOM_Map id = translation(0, 0);
  Sink sink;
  Drawn a = render_list(sink.sink, t.stroke(run, p.get(), &flat, paint), 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(a.result, GSHSCENE_OK);
  ASSERT_EQ(a.entries.size(), 1u);
  EXPECT_EQ(a.entries[0].reason, GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  Drawn b = render_list(sink.sink, t.stroke(spaces, p.get(), &id, paint), 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(b.result, GSHSCENE_OK);
  EXPECT_EQ(b.entries.size(), 0u) << "a run of spaces draws nothing, as a fill of it does, with no entry";
  for (uint8_t v : b.alpha) {
    EXPECT_EQ(v, 0);
  }
}

TEST(RenderStrokeText, ARunOfGlyphsTheProviderCannotGiveDrawsNothingAndReportsEachOfThem) {
  Text t;
  // Glyphs 4 (corrupt) and 9 (unavailable) only: the centerline is empty, nothing is drawn, and each glyph has its note.
  const GSHSCENE_Resource * run = t.run(t.face(), 12.0, GSHSCENE_HINT_NONE, {{4, 1.0, 10.0}, {0, 5.0, 10.0}, {9, 9.0, 10.0}});
  GSHSCENE_Paint paint = solid(1, 1, 1, 1);
  Params p = pen(2.0);
  GSHGEOM_Map m = translation(0.5, 0.5);
  const GSHSCENE_List * l = t.stroke(run, p.get(), &m, paint);
  Sink sink;
  for (int pass = 0; pass < 2; pass++) {
    Drawn d = render_list(sink.sink, l, 16, 16);
    EXPECT_EQ(d.result, GSHSCENE_OK) << pass;
    ASSERT_EQ(d.entries.size(), 2u) << pass << ": a miss and then a hit say the same";
    EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
    EXPECT_EQ(d.entries[0].second, 0u);
    EXPECT_EQ(d.entries[1].second, 2u);
    for (uint8_t a : d.alpha) {
      EXPECT_EQ(a, 0) << pass;
    }
  }
  EXPECT_EQ(sink.counters.cache_hits, 1u);
  Drawn strict = render_list(sink.sink, l, 16, 16, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
}

/* --- ST7: all the fills of a chunk, then all its strokes -------------------------------------------------------------- */

TEST(RenderStrokeText, FillThenStrokeOfARunIsEveryFillBeforeAnyStrokeNeverGlyphByGlyph) {
  Text t;
  // Two glyphs close enough that the stroke of the first reaches the second's fill.
  const std::vector<G> two = {{2, 1.0, 22.0}, {2, 9.0, 22.0}};
  const GSHSCENE_Resource * run = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, two);
  const GSHSCENE_Resource * g1 = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {two[0]});
  const GSHSCENE_Resource * g2 = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {two[1]});
  const GSHSCENE_Resource * style = intern_style(t.s, pen(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND).get());
  GSHSCENE_Paint fill_paint = solid(0.9, 0.8, 0.1, 1.0);
  GSHSCENE_Paint stroke_paint = solid(0.1, 0.1, 0.6, 1.0);
  auto fill = [&](GSHSCENE_List * l, const GSHSCENE_Resource * r) {
    EXPECT_EQ(gshscene_list_fill(l, r, nullptr, GSHGEOM_FILL_NONZERO, &fill_paint, 0, 0.0), GSHSCENE_OK);
  };
  auto stroke = [&](GSHSCENE_List * l, const GSHSCENE_Resource * r) {
    EXPECT_EQ(gshscene_list_stroke(l, r, style, nullptr, &stroke_paint, 0, 0.0), GSHSCENE_OK);
  };
  GSHSCENE_List * chunk = t.s.open();   // the format's order: the chunk's fill, then its stroke
  fill(chunk, run);
  stroke(chunk, run);
  gshscene_list_close(chunk);
  GSHSCENE_List * fills_first = t.s.open(); // the same, spelled as the per-glyph fills and then the one stroke
  fill(fills_first, g1);
  fill(fills_first, g2);
  stroke(fills_first, run);
  gshscene_list_close(fills_first);
  GSHSCENE_List * per_glyph = t.s.open();   // glyph by glyph: what a chunk is not
  fill(per_glyph, g1);
  stroke(per_glyph, g1);
  fill(per_glyph, g2);
  stroke(per_glyph, g2);
  gshscene_list_close(per_glyph);
  GSHSCENE_List * reverse = t.s.open();     // the stroke first (paint-order: stroke)
  stroke(reverse, run);
  fill(reverse, run);
  gshscene_list_close(reverse);
  Sink sink;
  Drawn a = render_list(sink.sink, chunk, 30, 28);
  Drawn b = render_list(sink.sink, fills_first, 30, 28);
  Drawn c = render_list(sink.sink, per_glyph, 30, 28);
  Drawn d = render_list(sink.sink, reverse, 30, 28);
  EXPECT_EQ(a.hash, b.hash);
  EXPECT_NE(a.hash, c.hash) << "a stroke over a neighbour's fill: interleaving is visible";
  EXPECT_NE(a.hash, d.hash) << "the order of the two items is the format's";
}

/* --- ST8: the cache, its replayed notes, caller memory and the exact workspace ---------------------------------------------- */

TEST(RenderStrokeText, AHitIsTheMissBitForBitIncludingTheNotesAndAReflowedRunMissesAndDrawsTheSame) {
  Text t;
  // Glyph 4 is corrupt: its note (the second glyph, index 1) is reported by the miss and, replayed, by the hit.
  const GSHSCENE_Resource * run = t.run(t.face(), 12.0, GSHSCENE_HINT_NONE, {{1, 1.0, 10.0}, {4, 7.0, 10.0}, {2, 12.0, 10.0}});
  GSHSCENE_Paint paint = solid(0.2, 0.3, 0.9, 0.8);
  Params p = pen(1.25, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  GSHGEOM_Map m = translation(0.5, 0.25);
  const GSHSCENE_List * l = t.stroke(run, p.get(), &m, paint);
  Sink sink;
  Drawn miss = render_list(sink.sink, l, 30, 20);
  EXPECT_EQ(sink.counters.cache_misses, 1u);
  Drawn hit = render_list(sink.sink, l, 30, 20);
  EXPECT_EQ(sink.counters.cache_hits, 1u);
  EXPECT_EQ(hit.hash, miss.hash);
  EXPECT_EQ(entries_text(hit), entries_text(miss));
  ASSERT_EQ(miss.entries.size(), 1u);
  EXPECT_EQ(miss.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(miss.entries[0].reason, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
  EXPECT_EQ(miss.entries[0].second, 1u);
  EXPECT_EQ(hit.summary.work_max, miss.summary.work_max) << "a hit counts what a miss counts (SC11)";
  // A whole-pixel shift is a hit too, and the notes come with it.
  GSHGEOM_Map shifted = translation(3.5, 1.25);
  Drawn moved = render_list(sink.sink, t.stroke(run, p.get(), &shifted, paint), 30, 20);
  EXPECT_EQ(sink.counters.cache_hits, 2u);
  EXPECT_EQ(entries_text(moved), entries_text(miss));
  // Strict stops at the refused glyph on a hit as on a miss, before anything is drawn for the item.
  Drawn strict = render_list(sink.sink, l, 30, 20, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
  for (uint8_t a : strict.alpha) {
    EXPECT_EQ(a, 0) << "the finished prefix is nothing";
  }
  // A reflowed run (the third glyph moved: another resource, another id) misses, and draws the stroke of its own path.
  const GSHSCENE_Resource * reflowed = t.run(t.face(), 12.0, GSHSCENE_HINT_NONE, {{1, 1.0, 10.0}, {4, 7.0, 10.0}, {2, 12.5, 10.0}});
  uint64_t misses = sink.counters.cache_misses;
  Drawn r = render_list(sink.sink, t.stroke(reflowed, p.get(), &m, paint), 30, 20);
  EXPECT_EQ(sink.counters.cache_misses, misses + 1);
  Drawn rp = render_list(sink.sink, t.stroke(t.centerline(reflowed), p.get(), &m, paint), 30, 20);
  EXPECT_EQ(r.hash, rp.hash);
  // A run that differs only in what hinting asks has its own id and the same pixels (ST3).
  const GSHSCENE_Resource * strict_run = t.run(t.face(), 12.0, GSHSCENE_HINT_FONT_STRICT, {{1, 1.0, 10.0}, {4, 7.0, 10.0}, {2, 12.0, 10.0}});
  misses = sink.counters.cache_misses;
  Drawn s = render_list(sink.sink, t.stroke(strict_run, p.get(), &m, paint), 30, 20);
  EXPECT_EQ(sink.counters.cache_misses, misses + 1);
  EXPECT_EQ(s.hash, miss.hash);
  EXPECT_EQ(entries_text(s), entries_text(miss));
}

TEST(RenderStrokeText, CallerMemoryAllocatesNothingReadsNoEntryAndItsWorkspaceIsExact) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 1.0, 20.0}, {4, 8.0, 20.0}, {1, 11.0, 20.0}, {3, 18.0, 20.0}});
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(1, 0, 0, 1)}, {1.0, solid(0, 0, 1, 0.5)}};
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 24.0;
  gshgeom_map_identity(2, &g.map);
  g.stop_count = 2;
  g.stops = stops;
  const GSHSCENE_Resource * gr = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(t.s.store, &g, &gr), GSHSCENE_OK);
  t.s.held.push_back(gr);
  GSHSCENE_Paint paint{};
  paint.kind = GSHSCENE_PAINT_GRADIENT;
  paint.resource = gr;
  paint.rgba[3] = 1.0;
  for (int aligned = 0; aligned < 2; aligned++) {
    Params p = pen(2.5, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE);
    if (aligned) {
      p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
    }
    GSHGEOM_Map m = translation(0.375, 0.625);
    const GSHSCENE_List * l = t.stroke(run, p.get(), &m, paint);
    tracker::Tracker tr;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&tr);
    Sink sink(GSHCPU_RENDER_TIERS_ALL, nullptr, &alloc);
    Drawn plain = render_list(sink.sink, l, 34, 28);
    ASSERT_EQ(plain.result, GSHSCENE_OK);
    RasterRef probe(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 34, 28));
    GSHSCENE_Target target = target_of(probe.raster);
    ReportRef report;
    GSHSCENE_Render_Request req = request_of(l, &target, report.report);
    size_t need = 0;
    ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
    std::vector<unsigned char> memory(need + 16);
    const long calls = tr.calls;
    const uint64_t hits = sink.counters.cache_hits;
    const uint64_t misses = sink.counters.cache_misses;
    Drawn caller = render_list(sink.sink, l, 34, 28, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr, nullptr,
        memory.data(), need);
    ASSERT_EQ(caller.result, GSHSCENE_OK) << aligned;
    EXPECT_EQ(tr.calls, calls) << "no allocation by the sink during a caller-memory render (S9)";
    EXPECT_EQ(sink.counters.cache_hits, hits) << "the cache is not read";
    EXPECT_EQ(sink.counters.cache_misses, misses) << "and not written";
    EXPECT_EQ(caller.hash, plain.hash);
    EXPECT_EQ(entries_text(caller), entries_text(plain)) << "the notes are the same";
    Drawn short_one = render_list(sink.sink, l, 34, 28, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr,
        nullptr, memory.data(), need - 1);
    EXPECT_EQ(short_one.result, GSHSCENE_ERR_LIMIT) << "one byte less is a limit";
    EXPECT_EQ(tr.calls, calls);
  }
}

/* --- SC6, ST8: a limit refuses the whole item -------------------------------------------------------------------------------- */

TEST(RenderStrokeText, ALimitOfTheRunRefusesTheWholeItemAndNeverStrokesPartOfIt) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 1.0, 20.0}, {3, 12.0, 20.0}});
  GSHSCENE_Paint paint = solid(1, 1, 1, 1);
  Params p = pen(2.0);
  GSHGEOM_Map m = translation(0.25, 0.5);
  const GSHSCENE_List * l = t.stroke(run, p.get(), &m, paint);
  {
    GSHCPU_Limits limits{};
    limits.max_points = 40; // past the points of a part
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
    Drawn d = render_list(sink.sink, l, 30, 24);
    EXPECT_EQ(d.result, GSHSCENE_OK);
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
    EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
    for (uint8_t a : d.alpha) {
      EXPECT_EQ(a, 0) << "none of the run is drawn";
    }
    Drawn strict = render_list(sink.sink, l, 30, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
    EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
  }
  {
    GSHCPU_Limits limits{};
    limits.max_deposits = 30; // cells past the cap
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
    Drawn d = render_list(sink.sink, l, 30, 24);
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  }
  {
    // A coordinate past the sink's range after the whole-pixel shift.
    GSHGEOM_Map far = translation(3e8, 0.0);
    Sink sink;
    Drawn d = render_list(sink.sink, t.stroke(run, p.get(), &far, paint), 30, 24);
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  }
  {
    // A glyph past the geometry's bounds is that glyph's note, never the item's: the rest of the run is stroked.
    GSHSCENE_Paint white = solid(1, 1, 1, 1);
    const GSHSCENE_Resource * mixed = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 1.0, 20.0}, {9, 9.0, 20.0}, {3, 12.0, 20.0}});
    Sink sink;
    Drawn d = render_list(sink.sink, t.stroke(mixed, p.get(), &m, white), 30, 24);
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
    EXPECT_EQ(d.entries[0].second, 1u);
    bool any = false;
    for (uint8_t a : d.alpha) {
      any = any || a != 0;
    }
    EXPECT_TRUE(any) << "the others are drawn";
  }
}

/* --- damage ---------------------------------------------------------------------------------------------------------------- */

TEST(RenderStrokeText, InsideADamageTheRenderIsTheWholeRenderOverAWholeRunAndOverOneGlyph) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 1.0, 20.0}, {3, 12.0, 20.0}, {2, 24.0, 20.0}});
  GSHSCENE_Paint paint = solid(0.8, 0.2, 0.2, 0.7);
  Params p = pen(2.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  p.dashes = {3.0, 1.5};
  GSHGEOM_Map m = translation(0.25, 0.75);
  const GSHSCENE_List * l = t.stroke(run, p.get(), &m, paint);
  Sink sink;
  Drawn whole = render_list(sink.sink, l, 40, 24);
  const GSHSCENE_Rect damages[3] = {{6, 4, 30, 20}, {0, 0, 11, 24}, {11, 9, 24, 24}};
  for (const GSHSCENE_Rect & r : damages) {
    Drawn part = render_list(sink.sink, l, 40, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, &r);
    ASSERT_EQ(part.result, GSHSCENE_OK);
    for (int y = r.y0; y < r.y1; y++) {
      for (int x = r.x0; x < r.x1; x++) {
        EXPECT_EQ(part.alpha[(size_t)y * 40 + (size_t)x], whole.alpha[(size_t)y * 40 + (size_t)x]) << x << "," << y;
      }
    }
  }
}

/* --- SC10, ST10: the test sink's outline hash is the outline this sink draws ------------------------------------------------- */

TEST(RenderStrokeText, TheTestSinksOutlineHashOfARunIsTheOutlineTheSinkDraws) {
  Text t;
  const GSHSCENE_Resource * font = t.run(t.face(), 12.0, GSHSCENE_HINT_FONT_STRICT, {{1, 2.0, 18.0}, {2, 8.0, 18.0}, {3, 15.0, 18.0}});
  const GSHSCENE_Resource * outlines = t.outlines();
  Params styles[2] = {pen(2.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND), pen(1.5)};
  styles[1].s.alignment = GSHGEOM_STROKE_ALIGN_OUTSIDE;
  styles[1].dashes = {4.0, 1.0};
  const GSHGEOM_Map maps[2] = {translation(3.375, 2.25), affine_map(1.5, 0.25, -0.5, 1.25, 4.75, 1.5)};
  GSHSCENE_Paint paint = solid(0.5, 0.5, 0.5, 1.0);
  for (const GSHSCENE_Resource * run : {font, outlines}) {
    for (int i = 0; i < 2; i++) {
      const GSHSCENE_List * l = t.stroke(run, styles[i].get(), &maps[i], paint);
      std::string text;
      GSHSCENE_Writer log{[](void * ctx, const void * data, size_t n) {
        static_cast<std::string *>(ctx)->append(static_cast<const char *>(data), n);
        return GSHSCENE_OK;
      }, &text};
      GSHSCENE_Dump_Sink_Config cfg;
      gshscene_dump_sink_config_default(&cfg);
      cfg.log = &log;
      GSHSCENE_Sink * dump = nullptr;
      ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &cfg, &dump), GSHSCENE_OK);
      RasterRef raster(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 4, 4));
      ReportRef report;
      GSHSCENE_Target tg = target_of(raster.raster);
      GSHSCENE_Render_Request req = request_of(l, &tg, report.report);
      ASSERT_EQ(gshscene_sink_render(dump, &req), GSHSCENE_OK);
      gshscene_sink_destroy(dump);
      size_t at = text.find("outline=");
      ASSERT_NE(at, std::string::npos) << text;
      const GSHSCENE_Item * item = nullptr;
      ASSERT_EQ(gshscene_list_item(l, 0, &item), GSHSCENE_OK);
      GSHCPU_Stroke st;
      GSHGEOM_Limits limits;
      gshgeom_limits_default(&limits);
      ASSERT_EQ(gshcpu_render_stroke_open(nullptr, gshgeom_allocator_default(), &limits, item, &maps[i], &st), GSHCPU_OK);
      char want[40];
      std::snprintf(want, sizeof want, "outline=%016llx", (unsigned long long)gshgeom_stroke_outline_hash(st.outline));
      EXPECT_EQ(text.substr(at, 24), want) << i;
      gshcpu_render_stroke_close(nullptr, &st);
    }
  }
}

/* --- ST9: bounds and work ------------------------------------------------------------------------------------------------- */

TEST(RenderStrokeText, TheExpansionLiesWithinTheStyleBoundsOfTheGlyphsAndTheWorkIsWithinTheDeclaredBound) {
  Text t;
  const GSHSCENE_Resource * run = t.run(t.face(), 14.0, GSHSCENE_HINT_NONE, {{2, 2.0, 20.0}, {3, 14.0, 22.0}, {1, 26.0, 18.0}});
  GSHSCENE_Paint paint = solid(0.1, 0.1, 0.1, 1.0);
  Params p = pen(3.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE);
  p.s.alignment = GSHGEOM_STROKE_ALIGN_OUTSIDE;
  GSHGEOM_Map m = translation(0.375, 0.5);
  const GSHSCENE_List * l = t.stroke(run, p.get(), &m, paint);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l, 0, &item), GSHSCENE_OK);
  // SK10: the centerline's box (the glyphs' outlines placed) outset by the style's reach, in pixels.
  GSHGEOM_Path * path = nullptr;
  ASSERT_EQ(gshscene_glyph_run_centerline(run, nullptr, nullptr, &path, nullptr), GSHSCENE_OK);
  GSHGEOM_Bounds box;
  ASSERT_EQ(gshgeom_path_bounds(path, &box), GSHGEOM_OK);
  gshgeom_path_destroy(path);
  GSHGEOM_Bounds reach;
  ASSERT_EQ(gshscene_stroke_bounds(item->style, &box, &m, GSHSCENE_UNIT_PIXEL, &reach), GSHSCENE_OK);
  GSHCPU_Stroke st;
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  ASSERT_EQ(gshcpu_render_stroke_open(nullptr, gshgeom_allocator_default(), &limits, item, &m, &st), GSHCPU_OK);
  ASSERT_TRUE(st.drawn);
  for (int part = 0; part < GSHCPU_STROKE_PARTS; part++) {
    if (!st.meta.present[part] || st.meta.points[part] == 0) {
      continue;
    }
    // The outline's 26.6 coordinates are at the canonical map; add the whole-pixel shift back.
    double x0 = (double)(st.meta.lo_x[part] + 64 * st.shift_x) / 64.0;
    double y0 = (double)(st.meta.lo_y[part] + 64 * st.shift_y) / 64.0;
    double x1 = (double)(st.meta.hi_x[part] + 64 * st.shift_x) / 64.0;
    double y1 = (double)(st.meta.hi_y[part] + 64 * st.shift_y) / 64.0;
    const double tol = 1.0 / 16.0 + 1.0 / 64.0;
    EXPECT_GE(x0, reach.min.x - tol) << part;
    EXPECT_GE(y0, reach.min.y - tol) << part;
    EXPECT_LE(x1, reach.max.x + tol) << part;
    EXPECT_LE(y1, reach.max.y + tol) << part;
  }
  gshcpu_render_stroke_close(nullptr, &st);
  // The work: the path's stroke counts the same, plus the elements of the glyphs' outlines for the run (ST9).
  const GSHSCENE_Resource * as_path = t.centerline(run);
  Sink sink;
  RasterRef probe(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 40, 30));
  GSHSCENE_Target tg = target_of(probe.raster);
  GSHSCENE_Sink_Caps caps{};
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &tg, &caps), GSHSCENE_OK);
  EXPECT_NE(caps.flags & GSHSCENE_CAP_STROKE_RUNS, 0u) << "this sink strokes runs";
  Drawn a = render_list(sink.sink, l, 40, 30);
  Drawn b = render_list(sink.sink, t.stroke(as_path, p.get(), &m, paint), 40, 30);
  EXPECT_LE(a.summary.work_max, caps.cancel_work_bound);
  GSHGEOM_Path * counted = nullptr;
  ASSERT_EQ(gshscene_glyph_run_centerline(run, nullptr, nullptr, &counted, nullptr), GSHSCENE_OK);
  EXPECT_EQ(a.summary.work_max, b.summary.work_max + gshgeom_path_element_count(counted))
      << "the elements of the glyphs' outlines are counted once";
  gshgeom_path_destroy(counted);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
