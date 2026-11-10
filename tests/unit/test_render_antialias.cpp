/**
 * @file
 *
 * Coverage by the centers of the pixels (shape-scene's list.h, AA1 to AA6) in the sink: the capability, the known answers of the
 * independent reference (tests/center_vectors.h, tools/gen-center-vectors.py) for fills under five exact maps, both rules and every tie,
 * and for strokes of straight lines and squares in the three alignments; each in every combination of the fast and the exact coverage;
 * and the rule's consequences: abutting shapes with no overlap and no gap, whole pixels covered exactly, the coverage going through
 * a clip, an opacity and a group like any other, a hatch, a pattern and a gradient drawn through it, a default item unchanged beside
 * one that asks, and the sink's limit on the work.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "../center_vectors.h"
#include "../render_helpers.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace rh;

namespace {

struct Config {
  const char * name;
  uint64_t fill;
  uint64_t stroke;
};

const Config kConfigs[] = {
    {"exact", GSHCPU_FILL_COVERAGE_EXACT, GSHCPU_STROKE_COVERAGE_EXACT},
    {"fast fill", GSHCPU_FILL_COVERAGE_FAST, GSHCPU_STROKE_COVERAGE_EXACT},
    {"fast stroke", GSHCPU_FILL_COVERAGE_EXACT, GSHCPU_STROKE_COVERAGE_FAST},
    {"fast both", GSHCPU_FILL_COVERAGE_FAST, GSHCPU_STROKE_COVERAGE_FAST},
};

/** A zeroed straight RGBA8 raster, so that the alpha of a pixel is the coverage of what was drawn on it. */
GIMG_Raster * clear_raster(uint32_t w, uint32_t h) {
  GIMG_Pixel_Format f = rf_pixel_format(GSHCPU_FORMAT_RGBA8_STRAIGHT);
  GIMG_Raster * r = nullptr;
  EXPECT_EQ(gimg_raster_create(w, h, &f, GIMG_RASTER_OWNED, nullptr, 0, &r), GIMG_OK);
  unsigned char * px = (unsigned char *)gimg_raster_pixels(r);
  for (uint32_t y = 0; y < h; y++) {
    std::memset(px + y * gimg_raster_stride_bytes(r), 0, (size_t)w * 4u);
  }
  return r;
}

/** The alpha of every pixel of @p raster, row by row. */
std::vector<uint8_t> alphas(const GIMG_Raster * raster) {
  std::vector<uint8_t> out;
  const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(raster);
  for (uint32_t y = 0; y < gimg_raster_height(raster); y++) {
    for (uint32_t x = 0; x < gimg_raster_width(raster); x++) {
      out.push_back(px[y * gimg_raster_stride_bytes(raster) + x * 4u + 3u]);
    }
  }
  return out;
}

/** Render @p list on a clear raster of 16 by 16 under @p config; the alphas, or an empty vector (with a failure) if it did not render. */
std::vector<uint8_t> draw(const GSHSCENE_List * list, const Config & config, uint32_t w = 16, uint32_t h = 16,
    GSHSCENE_Report * report = nullptr, GSHSCENE_Result * result = nullptr) {
  GIMG_Raster * raster = clear_raster(w, h);
  RasterRef keep(raster);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.fill_coverage = config.fill;
  limits.stroke_coverage = config.stroke;
  SinkRef sink(&limits);
  ReportRef own;
  GSHSCENE_Target target = target_of(raster);
  GSHSCENE_Render_Request request = request_of(list, &target, report ? report : own.report);
  const GSHSCENE_Result r = gshscene_sink_render(sink.sink, &request);
  if (result) {
    *result = r;
  } else {
    EXPECT_EQ(r, GSHSCENE_OK) << config.name;
  }
  return alphas(raster);
}

std::string as_text(const std::vector<uint8_t> & a) {
  std::string out;
  for (uint8_t v : a) {
    out += v == 255 ? '1' : v == 0 ? '0' : '?';
  }
  return out;
}

std::string picture(const std::string & rows) {
  std::string out;
  for (size_t y = 0; y * 16 < rows.size(); y++) {
    out += rows.substr(y * 16, 16) + "\n";
  }
  return out;
}

GSHSCENE_Paint black() {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_SOLID;
  p.rgba[3] = 1.0;
  return p;
}

GSHSCENE_Paint black_with(double alpha) {
  GSHSCENE_Paint p = black();
  p.rgba[3] = alpha;
  return p;
}

/** A path of a polygon given in 1/64 units, taking every contour; @p pts is `x y` pairs of the contours back to back. */
GSHGEOM_Path * polygon_path(int contours, const int * counts, const int * pts) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  size_t at = 0;
  for (int c = 0; c < contours; c++) {
    for (int k = 0; k < counts[c]; k++, at++) {
      const GSHGEOM_Point pt = rf_pt((double)pts[2 * at] / 64.0, (double)pts[2 * at + 1] / 64.0);
      if (k == 0) {
        gshgeom_path_move_to(p, pt);
      } else {
        gshgeom_path_line_to(p, pt);
      }
    }
    gshgeom_path_close(p);
  }
  return p;
}

GSHGEOM_Map map_of(const center_vectors::Map & m) {
  // x' = a x + c y + e, y' = b x + d y + f; the library's matrix is row by row
  const double mat[4] = {m.a, m.c, m.b, m.d};
  const double t[2] = {m.e, m.f};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, mat, t, &out), GSHGEOM_OK);
  return out;
}

/** The raster a case covers, as the rows of '0' and '1' the generator writes. */
struct FillCase {
  Store store;
  GSHSCENE_List * list;
  FillCase(const center_vectors::Case & c, GSHSCENE_Antialias mode) {
    GSHGEOM_Path * p = polygon_path(c.contours, c.counts, c.model);
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(store.store, p, &r), GSHSCENE_OK);
    store.held.push_back(r);
    gshgeom_path_destroy(p);
    list = store.open();
    const GSHGEOM_Map map = map_of(center_vectors::kMaps[c.map]);
    GSHSCENE_Paint paint = black();
    EXPECT_EQ(gshscene_list_fill_antialias(list, r, &map, c.rule ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO, &paint, mode, 0, 0.0),
        GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(list), GSHSCENE_OK);
  }
};

}  // namespace

// AA5: the sink says it draws the mode, so the planning pass has nothing to report for an item that asks, in either tier.
TEST(RenderAntialias, TheSinkClaimsTheCapabilityAndReportsNothingForAnItemThatAsks) {
  GSHSCENE_Sink_Caps caps;
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshcpu_sink_create(nullptr, nullptr, nullptr, &sink), GSHCPU_OK);
  GIMG_Raster * raster = clear_raster(16, 16);
  RasterRef keep(raster);
  const GSHSCENE_Target target = target_of(raster);
  ASSERT_EQ(gshscene_sink_caps(sink, &target, &caps), GSHSCENE_OK);
  EXPECT_NE(caps.flags & GSHSCENE_CAP_ANTIALIAS_NONE, 0u);
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint paint = black();
  ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(2, 2, 9, 9), nullptr, GSHGEOM_FILL_NONZERO, &paint, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  ReportRef report;
  GSHSCENE_Render_Request request = request_of(l, &target, report.report);
  EXPECT_EQ(gshscene_sink_render(sink, &request), GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_count(report.report), 0u);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

// AA2, AA3: every case of the independent reference, in every combination of the coverage tiers.
TEST(RenderAntialias, EveryFillOfTheReferenceCoversTheCentersItSaysUnderEveryTier) {
  for (int i = 0; i < center_vectors::kCaseCount; i++) {
    const center_vectors::Case & c = center_vectors::kCases[i];
    for (const bool top_left : {true, false}) {
      FillCase fc(c, top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE);
      const char * want = top_left ? c.covered : c.covered_bottom_right;
      for (const Config & config : kConfigs) {
        const std::string got = as_text(draw(fc.list, config));
        EXPECT_EQ(got, std::string(want)) << c.name << " (" << (top_left ? "top-left, " : "bottom-right, ") << config.name << ", "
                                          << center_vectors::kMaps[c.map].name << ")\n" << picture(got) << "want\n" << picture(want);
      }
    }
  }
}

// AA2, SK5: the stroke of a straight line or a square, whose outline is a union of rectangles, in every alignment.
TEST(RenderAntialias, EveryStrokeOfTheReferenceCoversTheCentersItSaysUnderEveryTier) {
  for (int i = 0; i < center_vectors::kStrokeCount; i++) {
    const center_vectors::Stroke & s = center_vectors::kStrokes[i];
    Store store;
    GSHGEOM_Path * p = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &p);
    for (int k = 0; k < s.count; k++) {
      const GSHGEOM_Point pt = rf_pt((double)s.points[2 * k] / 64.0, (double)s.points[2 * k + 1] / 64.0);
      if (k == 0) {
        gshgeom_path_move_to(p, pt);
      } else {
        gshgeom_path_line_to(p, pt);
      }
    }
    if (s.closed) {
      gshgeom_path_close(p);
    }
    const GSHSCENE_Resource * path = nullptr;
    ASSERT_EQ(gshscene_store_intern_path(store.store, p, &path), GSHSCENE_OK);
    store.held.push_back(path);
    gshgeom_path_destroy(p);
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    st.width = (double)s.width / 64.0;
    st.cap = s.cap == 1 ? GSHSCENE_STROKE_CAP_SQUARE : GSHSCENE_STROKE_CAP_BUTT;
    st.join = GSHSCENE_STROKE_JOIN_MITER;
    st.alignment = s.align == 1 ? GSHSCENE_STROKE_ALIGN_INSIDE : s.align == 2 ? GSHSCENE_STROKE_ALIGN_OUTSIDE : GSHSCENE_STROKE_ALIGN_CENTER;
    const GSHSCENE_Resource * style = nullptr;
    ASSERT_EQ(gshscene_store_intern_stroke_style(store.store, &st, &style), GSHSCENE_OK);
    store.held.push_back(style);
    for (const bool top_left : {true, false}) {
      GSHSCENE_List * l = store.open();
      GSHSCENE_Paint paint = black();
      ASSERT_EQ(gshscene_list_stroke_antialias(l, path, style, nullptr, &paint,
                    top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
          GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      const char * want = top_left ? s.covered : s.covered_bottom_right;
      for (const Config & config : kConfigs) {
        const std::string got = as_text(draw(l, config));
        EXPECT_EQ(got, std::string(want)) << s.name << " (" << (top_left ? "top-left, " : "bottom-right, ") << config.name << ")\n"
                                          << picture(got) << "want\n" << picture(want);
      }
    }
  }
}

// AA3: a rectangle with whole-pixel corners covers exactly its pixels, and one with corners on centers covers the pixels whose centers
// are inside it by the half-open rule: under the top-left rule the left and the top edge in, the right and the bottom edge out, and under
// the bottom-right rule the other way.
namespace {
/** Whether pixel @p v is inside the half-open interval of the pixels whose centers lie on @p lo and @p hi (`[lo, hi)` top-left, `(lo, hi]` bottom-right). */
bool half_open(int v, int lo, int hi, bool top_left) { return top_left ? v >= lo && v < hi : v > lo && v <= hi; }
} // namespace

TEST(RenderAntialias, ARectangleCoversTheCentersInsideItHalfOpen) {
  for (const bool top_left : {true, false}) {
    const GSHSCENE_Antialias mode = top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE;
    for (const Config & config : kConfigs) {
      Store s;
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint paint = black();
      ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(2, 3, 7, 6), nullptr, GSHGEOM_FILL_NONZERO, &paint, mode, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(9.5, 3.5, 12.5, 6.5), nullptr, GSHGEOM_FILL_NONZERO, &paint, mode, 0, 0.0),
          GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      const std::vector<uint8_t> a = draw(l, config);
      for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
          const bool first = x >= 2 && x < 7 && y >= 3 && y < 6; // whole-pixel corners: the same under either rule
          // the centers 9.5 and 12.5 (3.5 and 6.5) are on the edges
          const bool second = half_open(x, 9, 12, top_left) && half_open(y, 3, 6, top_left);
          EXPECT_EQ(a[(size_t)y * 16 + (size_t)x], first || second ? 255 : 0) << config.name << (top_left ? " top-left " : " bottom-right ") << x << "," << y;
        }
      }
    }
  }
}

// AA3: two shapes that share an edge on the centers of a column neither overlap nor leave a gap: translucent, no pixel is darker than
// either shape alone, and the union is the whole rectangle.
TEST(RenderAntialias, ShapesThatShareAnEdgeNeitherOverlapNorLeaveAGap) {
  for (const bool top_left : {true, false}) {
    const GSHSCENE_Antialias mode = top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE;
    for (const Config & config : kConfigs) {
      Store s;
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint half = black_with(0.5);
      ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(2.5, 2.5, 7.5, 9.5), nullptr, GSHGEOM_FILL_NONZERO, &half, mode, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(7.5, 2.5, 12.5, 9.5), nullptr, GSHGEOM_FILL_NONZERO, &half, mode, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      const std::vector<uint8_t> a = draw(l, config);
      for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
          const bool in = half_open(x, 2, 12, top_left) && half_open(y, 2, 9, top_left);
          EXPECT_EQ(a[(size_t)y * 16 + (size_t)x], in ? 128 : 0)
              << config.name << (top_left ? " top-left " : " bottom-right ") << x << "," << y << " (one coverage, once)";
        }
      }
    }
  }
}

// AA4: the coverage goes through the clip, the opacity, the group and the operator as any coverage does; a default item beside it is
// antialiased as ever (a half-covered pixel), in the same picture.
TEST(RenderAntialias, TheCoverageGoesWhereAnyCoverageGoesAndADefaultItemBesideItIsAntialiased) {
  for (const Config & config : kConfigs) {
    Store s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint paint = black();
    GSHSCENE_Paint half = black_with(0.5);
    GSHSCENE_Group g;
    std::memset(&g, 0, sizeof g);
    g.opacity = 0.5;
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(1.5, 1.5, 5.5, 5.5), nullptr, GSHGEOM_FILL_NONZERO, &paint, GSHSCENE_ANTIALIAS_NONE_TOP_LEFT, 0,
                  0.0),
        GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    // a crisp fill clipped by a crisp-edged rectangle: the clip's edge is its own (antialiased) and sits on whole pixels here
    ASSERT_EQ(gshscene_list_clip_push(l, s.rect(8, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(6.5, 8.5, 13.5, 12.5), nullptr, GSHGEOM_FILL_NONZERO, &half, GSHSCENE_ANTIALIAS_NONE_TOP_LEFT,
                  0, 0.0),
        GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    // a default fill whose edge is at a half pixel is half covered there
    ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 12, 3.5, 14), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    const std::vector<uint8_t> a = draw(l, config);
    auto at = [&](int x, int y) { return (int)a[(size_t)y * 16 + (size_t)x]; };
    EXPECT_EQ(at(1, 1), 128) << config.name << ": a crisp fill at the group's opacity";
    EXPECT_EQ(at(4, 4), 128);
    EXPECT_EQ(at(5, 4), 0) << "center 5.5 is on the right edge";
    EXPECT_EQ(at(0, 1), 0);
    EXPECT_EQ(at(8, 9), 128) << "a half-alpha crisp fill inside the clip";
    EXPECT_EQ(at(13, 9), 0) << "the right edge at 13.5";
    EXPECT_EQ(at(7, 9), 0) << "the clip takes what is left of x = 8";
    EXPECT_EQ(at(1, 12), 255);
    EXPECT_EQ(at(2, 12), 255);
    EXPECT_NE(at(3, 12), 255) << "the default fill keeps its half pixel";
    EXPECT_GT(at(3, 12), 0);
  }
}

// AA2: the work is counted like any coverage's, and a render past the cap is the sink's resource limit, the item refused and reported,
// whatever the damage.
TEST(RenderAntialias, AnItemPastTheDepositCapIsTheSinksLimitAndNothingIsDrawn) {
  Store s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint paint = black();
  // a zigzag of 120 edges, each across all sixteen rows: 1920 crossings of a center line
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, rf_pt(1.0, 0.0));
  for (int i = 1; i <= 120; i++) {
    gshgeom_path_line_to(p, rf_pt(1.0 + 14.0 * (double)i / 120.0, i % 2 ? 16.0 : 0.0));
  }
  gshgeom_path_close(p);
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(s.store, p, &r), GSHSCENE_OK);
  s.held.push_back(r);
  gshgeom_path_destroy(p);
  ASSERT_EQ(gshscene_list_fill_antialias(l, r, nullptr, GSHGEOM_FILL_NONZERO, &paint, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  for (uint64_t cap : {UINT64_C(5), UINT64_C(1000), UINT64_C(1919), UINT64_C(0)}) {
    GIMG_Raster * raster = clear_raster(16, 16);
    RasterRef keep(raster);
    GSHCPU_Limits limits;
    gshcpu_limits_default(&limits);
    limits.max_deposits = cap ? cap : UINT64_MAX;
    SinkRef sink(&limits);
    ReportRef report;
    GSHSCENE_Target target = target_of(raster);
    GSHSCENE_Render_Request request = request_of(l, &target, report.report);
    ASSERT_EQ(gshscene_sink_render(sink.sink, &request), GSHSCENE_OK);
    const std::vector<GSHSCENE_Report_Entry> e = report.entries();
    const std::vector<uint8_t> a = alphas(raster);
    size_t covered = 0;
    for (uint8_t v : a) {
      covered += v != 0;
    }
    if (cap == 0) {
      EXPECT_TRUE(e.empty());
      EXPECT_GT(covered, 0u);
    } else {
      ASSERT_EQ(e.size(), 1u) << cap;
      EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_REFUSED);
      EXPECT_EQ(e[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
      EXPECT_EQ(covered, 0u) << "a refused item draws nothing, " << cap;
    }
  }
}

namespace {

/** The picture of one opaque black cell: what a pattern of opaque cells paints is whatever coverage it is given. */
const GSHSCENE_Resource * black_cell(RF_Ctx * c) {
  GSHSCENE_List * t = rf_open(c);
  rf_fill(c, t, rf_rect(c, 0, 0, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
  return rf_picture_of(c, t);
}

/** The alphas of @p list on a 32 by 32 clear raster, with an optional damage; fails the test if it did not render. */
std::vector<uint8_t> draw32(const GSHSCENE_List * list, const Config & config, const GSHSCENE_Rect * damage = nullptr,
    GSHSCENE_Report_Summary * summary = nullptr, uint64_t max_deposits = 0, size_t * entries = nullptr) {
  GIMG_Raster * raster = clear_raster(32, 32);
  RasterRef keep(raster);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.fill_coverage = config.fill;
  limits.stroke_coverage = config.stroke;
  if (max_deposits) {
    limits.max_deposits = max_deposits;
  }
  SinkRef sink(&limits);
  ReportRef report;
  GSHSCENE_Target target = target_of(raster);
  target.damage = damage;
  GSHSCENE_Render_Request request = request_of(list, &target, report.report);
  EXPECT_EQ(gshscene_sink_render(sink.sink, &request), GSHSCENE_OK) << config.name;
  if (summary) {
    *summary = report.summary();
  }
  if (entries) {
    *entries = report.entries().size();
  }
  return alphas(raster);
}

}  // namespace

// AC4: a fill painted with a pattern of opaque cells draws exactly what the same fill painted with a solid color draws, at a paint alpha
// that shows a pixel painted twice or not at all: each center is in one cell.  The cells abut at a fraction and under a turn.
TEST(RenderAntialias, AFillPaintedWithAPatternOfOpaqueCellsIsTheSolidFillItsCoverageMakes) {
  for (const Config & config : kConfigs) {
    for (int lattice = 0; lattice < 3; lattice++) {
      RF_Ctx ctx;
      rf_begin(&ctx);
      const GSHSCENE_Resource * tile = black_cell(&ctx);
      GSHGEOM_Map map = lattice == 0 ? rf_move(0.3, 0.45) : lattice == 1 ? rf_move(0.0, 0.0) : rf_move(0.0, 0.0);
      if (lattice == 2) {
        gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 17.0, &map);
      }
      const GSHSCENE_Resource * pat = rf_pattern(&ctx, tile, 0, 0, 6, 6, 6, 6, 1, 1, map, nullptr);
      const GSHSCENE_Resource * region = rf_star(&ctx, 15.5, 16.0, 13.0);
      GSHSCENE_List * with_pattern = rf_open(&ctx);
      rf_fill_crisp(&ctx, with_pattern, region, nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 0.5), 0, 0.0);
      rf_close(&ctx, with_pattern);
      GSHSCENE_List * solid = rf_open(&ctx);
      rf_fill_crisp(&ctx, solid, region, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
      rf_close(&ctx, solid);
      const std::vector<uint8_t> a = draw32(with_pattern, config);
      const std::vector<uint8_t> b = draw32(solid, config);
      size_t covered = 0;
      for (uint8_t v : b) {
        covered += v != 0;
        EXPECT_TRUE(v == 0 || v == 128) << "a crisp fill at half alpha is 0 or 128, never a sum";
      }
      EXPECT_GT(covered, 100u);
      EXPECT_EQ(a, b) << config.name << " lattice " << lattice;
      rf_release(&ctx);
    }
  }
}

// AC4: the same for a stroke, with an aligned one among them (the clip part is not met with the cell).
TEST(RenderAntialias, AStrokePaintedWithAPatternOfOpaqueCellsIsTheSolidStrokeItsCoverageMakes) {
  for (const Config & config : kConfigs) {
    for (uint32_t align : {GSHSCENE_STROKE_ALIGN_CENTER, GSHSCENE_STROKE_ALIGN_INSIDE, GSHSCENE_STROKE_ALIGN_OUTSIDE}) {
      RF_Ctx ctx;
      rf_begin(&ctx);
      const GSHSCENE_Resource * tile = black_cell(&ctx);
      const GSHSCENE_Resource * pat = rf_pattern(&ctx, tile, 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.3, 0.45), nullptr);
      const GSHSCENE_Resource * path = rf_blob(&ctx, 6.0, 8.0, 7.0);
      const GSHSCENE_Resource * pen = rf_style(&ctx, 3.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_ROUND, 4.0,
          GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, align, GSHGEOM_FILL_NONZERO);
      GSHSCENE_List * with_pattern = rf_open(&ctx);
      rf_stroke_crisp(&ctx, with_pattern, path, pen, nullptr, rf_pattern_paint(pat, 0.5));
      rf_close(&ctx, with_pattern);
      GSHSCENE_List * solid = rf_open(&ctx);
      rf_stroke_crisp(&ctx, solid, path, pen, nullptr, rf_solid(0, 0, 0, 0.5));
      rf_close(&ctx, solid);
      const std::vector<uint8_t> a = draw32(with_pattern, config);
      const std::vector<uint8_t> b = draw32(solid, config);
      size_t covered = 0;
      for (uint8_t v : b) {
        covered += v != 0;
        EXPECT_TRUE(v == 0 || v == 128);
      }
      EXPECT_GT(covered, 50u) << align;
      EXPECT_EQ(a, b) << config.name << " alignment " << align;
      rf_release(&ctx);
    }
  }
}

// AC4: a pattern whose cells overlap (a step smaller than the cell) draws its tile under each copy's own cell; the coverage of the item
// is still decided once per center, and an opaque tile gives the same alphas.
TEST(RenderAntialias, ThePatternPaintAndTheGradientPaintDrawThroughTheCoverageUnderEveryTier) {
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * region = rf_blob(&ctx, 3.0, 5.0, 8.0);
    const GSHSCENE_Gradient_Stop opaque[2] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
    GSHSCENE_List * grad = rf_open(&ctx);
    rf_fill_crisp(&ctx, grad, region, nullptr, GSHGEOM_FILL_NONZERO,
        rf_gradient_paint(rf_gradient(&ctx, rf_linear(0, 0, 30, 0, 0, 0, GSHSCENE_EXTEND_PAD), opaque, 2), 1.0), 0, 0.0);
    rf_close(&ctx, grad);
    GSHSCENE_List * solid = rf_open(&ctx);
    rf_fill_crisp(&ctx, solid, region, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
    rf_close(&ctx, solid);
    const std::vector<uint8_t> a = draw32(grad, config);
    const std::vector<uint8_t> b = draw32(solid, config);
    EXPECT_EQ(a, b) << config.name << ": an opaque gradient through the coverage";
    // overlapping cells: the tile is drawn by each copy over its own cell
    const GSHSCENE_Resource * pat = rf_pattern(&ctx, black_cell(&ctx), 0, 0, 6, 6, 4.5, 4.5, 1, 1, rf_move(0.25, 0.5), nullptr);
    GSHSCENE_List * over = rf_open(&ctx);
    rf_fill_crisp(&ctx, over, region, nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
    rf_close(&ctx, over);
    EXPECT_EQ(draw32(over, config), b) << config.name << ": overlapping cells of an opaque tile";
    rf_release(&ctx);
  }
}

// HX13: a hatch fill is the stroke of its lines and takes the fill's mode: crisp lines are whole or nothing and are the same lines in
// every tier, and without the mode the lines have partial pixels.
TEST(RenderAntialias, AHatchFillTakesTheModeOfTheFillThatCarriesIt) {
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * pen = rf_pen(&ctx, 0.7, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT);
    GSHSCENE_Hatch_Family fam[1] = {{45.0, 0.0, 0.0, 0.0, 2.0, 0, nullptr}};
    const GSHSCENE_Resource * hatch = rf_hatch(&ctx, pen, 0, fam, 1, rf_move(0.0, 0.0), rf_solid(0, 0, 0, 1));
    const GSHSCENE_Resource * region = rf_rect(&ctx, 3.0, 3.0, 29.0, 27.0);
    GSHSCENE_List * crisp = rf_open(&ctx);
    rf_fill_crisp(&ctx, crisp, region, nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(hatch, 1.0), 0, 0.0);
    rf_close(&ctx, crisp);
    GSHSCENE_List * smooth = rf_open(&ctx);
    rf_fill(&ctx, smooth, region, nullptr, GSHGEOM_FILL_NONZERO, rf_hatch_paint(hatch, 1.0), 0, 0.0);
    rf_close(&ctx, smooth);
    size_t covered = 0;
    for (uint8_t v : draw32(crisp, config)) {
      EXPECT_TRUE(v == 0 || v == 255) << config.name << ": " << (int)v;
      covered += v != 0;
    }
    EXPECT_GT(covered, 40u);
    size_t partial = 0;
    for (uint8_t v : draw32(smooth, config)) {
      partial += v != 0 && v != 255;
    }
    EXPECT_GT(partial, 20u) << "the same hatch without the mode is antialiased";
    rf_release(&ctx);
  }
}

// AA4: a crisp item inside a group that blends and a clip block composes like any coverage: the group's result is the crisp coverage at the
// group's opacity, and the clip multiplies it by the clip's own antialiased edge.
TEST(RenderAntialias, ACrispFillInAClipBlockAtAFractionalEdgeIsWeighedByTheClipsOwnCoverage) {
  for (const Config & config : kConfigs) {
    Store s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint paint = black();
    ASSERT_EQ(gshscene_list_clip_push(l, s.rect(0, 0, 8.5, 32), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(2, 2, 20, 10), nullptr, GSHGEOM_FILL_NONZERO, &paint, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
        GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    const std::vector<uint8_t> a = draw32(l, config);
    auto at = [&](int x, int y) { return (int)a[(size_t)y * 32 + (size_t)x]; };
    EXPECT_EQ(at(7, 5), 255) << config.name;
    EXPECT_EQ(at(8, 5), 128) << "the clip covers half of the pixel; the crisp coverage of the fill is whole";
    EXPECT_EQ(at(9, 5), 0);
    EXPECT_EQ(at(5, 1), 0);
    EXPECT_EQ(at(5, 2), 255);
    EXPECT_EQ(at(5, 9), 255);
    EXPECT_EQ(at(5, 10), 0);
  }
}

// AC5: the cap of deposits is a table: a rule that straddles 1920 center lines passes at 1920 and is refused at 1919, in every tier and
// whatever the damage (S4: the count is over what the whole render draws, so a damage that holds none of the rows changes nothing).
TEST(RenderAntialias, TheDepositCapIsExactAndDoesNotDependOnTheDamage) {
  RF_Ctx ctx;
  rf_begin(&ctx);
  GSHGEOM_Path * p = rf_path(&ctx);
  gshgeom_path_move_to(p, rf_pt(1.0, 0.0));
  for (int i = 1; i <= 120; i++) {
    gshgeom_path_line_to(p, rf_pt(1.0 + 14.0 * (double)i / 120.0, i % 2 ? 16.0 : 0.0));
  }
  gshgeom_path_close(p);
  const GSHSCENE_Resource * zig = rf_intern(&ctx, p);
  GSHSCENE_List * l = rf_open(&ctx);
  rf_fill_crisp(&ctx, l, zig, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
  rf_close(&ctx, l);
  const GSHSCENE_Rect damage = {20, 20, 24, 24}; // holds none of the zigzag's rows
  for (const Config & config : kConfigs) {
    GSHSCENE_Report_Summary sum{};
    size_t entries = 0;
    const std::vector<uint8_t> whole = draw32(l, config, nullptr, &sum, 1920, &entries);
    EXPECT_EQ(entries, 0u) << "1920 deposits at a cap of 1920: " << config.name;
    size_t covered = 0;
    for (uint8_t v : whole) {
      covered += v != 0;
    }
    EXPECT_GT(covered, 20u);
    draw32(l, config, &damage, &sum, 1920, &entries);
    EXPECT_EQ(entries, 0u) << "a damage that holds none of it draws nothing and refuses nothing";
    const std::vector<uint8_t> refused = draw32(l, config, nullptr, &sum, 1919, &entries);
    EXPECT_EQ(entries, 1u) << "1920 deposits at a cap of 1919";
    for (uint8_t v : refused) {
      EXPECT_EQ(v, 0);
    }
    draw32(l, config, &damage, &sum, 1919, &entries);
    EXPECT_EQ(entries, 1u) << "the refusal does not depend on the damage";
  }
  rf_release(&ctx);
}

// AC5: the work of a crisp fill is its points and its crossings of the center lines (the deposits of the other coverage for a rectangle,
// whose two vertical edges cross six cells) and the pixels of its rectangle; so the same rectangle crisp costs the pixels more than
// antialiased (36), and a cancel's bound (the sink's work bound) is not passed.
TEST(RenderAntialias, TheWorkOfACrispFillIsItsPointsItsCrossingsAndItsPixels) {
  for (const Config & config : kConfigs) {
    Store s;
    uint64_t work[2] = {0, 0};
    for (int crisp = 0; crisp < 2; crisp++) {
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint paint = black();
      ASSERT_EQ(gshscene_list_fill_antialias(l, s.rect(2, 2, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &paint,
                    crisp ? GSHSCENE_ANTIALIAS_NONE : GSHSCENE_ANTIALIAS_AREA, 0, 0.0),
          GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      GSHSCENE_Report_Summary sum{};
      GIMG_Raster * raster = clear_raster(16, 16);
      RasterRef keep(raster);
      GSHCPU_Limits limits;
      gshcpu_limits_default(&limits);
      limits.fill_coverage = GSHCPU_FILL_COVERAGE_EXACT; // the dense coverage is the one the sum compares with
      limits.stroke_coverage = config.stroke;
      SinkRef sink(&limits);
      ReportRef report;
      GSHSCENE_Target target = target_of(raster);
      GSHSCENE_Render_Request request = request_of(l, &target, report.report);
      ASSERT_EQ(gshscene_sink_render(sink.sink, &request), GSHSCENE_OK);
      sum = report.summary();
      work[crisp] = sum.work_max;
    }
    // 4 points, 12 crossings of center lines (two vertical edges over six rows), 36 pixels, and the paint's own pass over the 36 pixels
    EXPECT_EQ(work[1], 4u + 12u + 36u + 36u) << config.name;
    EXPECT_GT(work[1], work[0]) << "the antialiased fill's deposits are cells, and a rectangle's are more than its crossings";
  }
}

// AC4: the items of a pattern's tile are covered per copy: a crisp item that spills over the cell is covered in its own cell only, so a
// translucent crisp rectangle three times the cell's reach is 128 everywhere and never a sum, and crisp strokes (a line, and the three
// alignments of a ring) the same.
TEST(RenderAntialias, TheCrispItemsOfATileAreCoveredInTheirOwnCellsOnly) {
  for (const bool top_left : {true, false}) {
  rf_crisp_mode = top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE;
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * region = rf_rect(&ctx, 3.0, 3.0, 28.0, 27.5);
    auto render_tile = [&](GSHSCENE_List * tile) {
      rf_close(&ctx, tile);
      const GSHSCENE_Resource * pat = rf_pattern(&ctx, rf_picture_of(&ctx, tile), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.3, 0.45), nullptr);
      GSHSCENE_List * l = rf_open(&ctx);
      rf_fill_crisp(&ctx, l, region, nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
      rf_close(&ctx, l);
      return draw32(l, config);
    };
    // a bar that spills sideways: its edges are at fractions, so the area coverage would have partial rows, and a copy not held to its cell would add
    GSHSCENE_List * t1 = rf_open(&ctx);
    rf_fill_crisp(&ctx, t1, rf_rect(&ctx, -3, 1.25, 9, 4.75), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
    const std::vector<uint8_t> a = render_tile(t1);
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 32; x++) {
        // the region's centers, 3.5 .. 27.5 in x and 3.5 .. 26.5 in y, and the rows whose centers (at .5 + 6 k) lie in [1.7, 5.2): 2, 3 and 4
        const bool in = x >= 3 && x < 28 && y >= 3 && y < (top_left ? 27 : 28) && (y % 6 == 2 || y % 6 == 3 || y % 6 == 4);
        EXPECT_EQ((int)a[(size_t)y * 32 + (size_t)x], in ? 128 : 0) << config.name << " " << x << "," << y;
      }
    }
    // a line across the cell edges: continuous, half alpha once (its rows are those whose centers are within a unit of 3.45)
    GSHSCENE_List * t2 = rf_open(&ctx);
    static const double line[] = {-6.0, 3.0, 4.0, 3.0};
    GSHGEOM_Map shifted = rf_move(4.0, 0.0); // a whole-pixel translation: the stroke's outline is in a frame moved by it
    rf_stroke_crisp(&ctx, t2, rf_polyline(&ctx, line, 2, 0), rf_pen(&ctx, 2.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), &shifted,
        rf_solid(0, 0, 0, 0.5));
    const std::vector<uint8_t> b = render_tile(t2);
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 32; x++) {
        const bool in = x >= 3 && x < 28 && y >= 3 && y < (top_left ? 27 : 28) && (y % 6 == 2 || y % 6 == 3); // the line repeats every cell row
        EXPECT_EQ((int)b[(size_t)y * 32 + (size_t)x], in ? 128 : 0) << config.name << " line " << x << "," << y;
      }
    }
    // rings that spill, in each alignment: whole or nothing at half alpha, and some covered
    for (uint32_t align : {GSHSCENE_STROKE_ALIGN_CENTER, GSHSCENE_STROKE_ALIGN_INSIDE, GSHSCENE_STROKE_ALIGN_OUTSIDE}) {
      GSHSCENE_List * t3 = rf_open(&ctx);
      static const double ring[] = {-1.0, -1.0, 7.0, -1.0, 7.0, 7.0, -1.0, 7.0};
      rf_stroke_crisp(&ctx, t3, rf_polyline(&ctx, ring, 4, 1),
          rf_style(&ctx, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_DEGENERATE_DRAW,
              nullptr, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH, align, GSHGEOM_FILL_NONZERO),
          nullptr, rf_solid(0, 0, 0, 0.5));
      size_t covered = 0;
      for (uint8_t v : render_tile(t3)) {
        EXPECT_TRUE(v == 0 || v == 128) << config.name << " alignment " << align << ": " << (int)v;
        covered += v != 0;
      }
      // a copy draws in its own cell only: the centered ring's bands and the outside ones lie outside the cell they are of, and inside's do not
      if (align == GSHSCENE_STROKE_ALIGN_INSIDE) {
        EXPECT_GT(covered, 20u);
      } else {
        EXPECT_EQ(covered, 0u) << align;
      }
    }
    rf_release(&ctx);
  }
  }
  rf_crisp_mode = GSHSCENE_ANTIALIAS_NONE;
}

// AC5 under each tie rule: a zigzag whose points are on center lines at one end (y = 0.5) and between them at the other (y = 16), so each of
// its 120 edges straddles 16 center lines under the top-left rule (the centers of rows 0 to 15) and 15 under the bottom-right rule (rows 1
// to 15): the cap is exact for each, and the rule that counts is the item's.
TEST(RenderAntialias, TheDepositCapIsExactUnderEachTieRule) {
  for (const bool top_left : {true, false}) {
    rf_crisp_mode = top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE;
    const size_t deposits = top_left ? 1920 : 1800;
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHGEOM_Path * p = rf_path(&ctx);
    gshgeom_path_move_to(p, rf_pt(1.0, 0.5));
    for (int i = 1; i <= 120; i++) {
      gshgeom_path_line_to(p, rf_pt(1.0 + 14.0 * (double)i / 120.0, i % 2 ? 16.0 : 0.5));
    }
    gshgeom_path_close(p);
    const GSHSCENE_Resource * zig = rf_intern(&ctx, p);
    GSHSCENE_List * l = rf_open(&ctx);
    rf_fill_crisp(&ctx, l, zig, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
    rf_close(&ctx, l);
    for (const Config & config : kConfigs) {
      GSHSCENE_Report_Summary sum{};
      size_t entries = 0;
      draw32(l, config, nullptr, &sum, deposits, &entries);
      EXPECT_EQ(entries, 0u) << (top_left ? "top-left " : "bottom-right ") << deposits << " deposits at a cap of " << deposits << ": " << config.name;
      draw32(l, config, nullptr, &sum, deposits - 1, &entries);
      EXPECT_EQ(entries, 1u) << (top_left ? "top-left " : "bottom-right ") << deposits << " deposits at a cap of " << deposits - 1 << ": " << config.name;
    }
    rf_release(&ctx);
  }
  rf_crisp_mode = GSHSCENE_ANTIALIAS_NONE;
}

// AC4 on a lattice whose cell edges are on the centers of the pixels: each copy is held to its cell by the same rule as the item, so every
// center is covered once, by the copy it belongs to.  A translucent crisp square that is the cell itself is 128 everywhere in the region
// (neither a gap on the edges the rule leaves out nor a sum on the ones it takes), and a crisp line across the cell edges is the rows the
// rule gives, under each tie rule, whichever the rule of the cell.
TEST(RenderAntialias, TheCrispItemsOfATileOnCenterEdgesAreCoveredOncePerCenterUnderEachTieRule) {
  for (const bool top_left : {true, false}) {
    rf_crisp_mode = top_left ? GSHSCENE_ANTIALIAS_NONE_TOP_LEFT : GSHSCENE_ANTIALIAS_NONE;
    for (const Config & config : kConfigs) {
      RF_Ctx ctx;
      rf_begin(&ctx);
      const GSHSCENE_Resource * region = rf_rect(&ctx, 3.0, 3.0, 28.0, 27.0);
      auto render_tile = [&](GSHSCENE_List * tile) {
        rf_close(&ctx, tile);
        const GSHSCENE_Resource * pat = rf_pattern(&ctx, rf_picture_of(&ctx, tile), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.5, 0.5), nullptr);
        GSHSCENE_List * l = rf_open(&ctx);
        rf_fill_crisp(&ctx, l, region, nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
        rf_close(&ctx, l);
        return draw32(l, config);
      };
      GSHSCENE_List * t1 = rf_open(&ctx);
      rf_fill_crisp(&ctx, t1, rf_rect(&ctx, 0, 0, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
      const std::vector<uint8_t> a = render_tile(t1);
      for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
          const bool in = x >= 3 && x < 28 && y >= 3 && y < 27;
          EXPECT_EQ((int)a[(size_t)y * 32 + (size_t)x], in ? 128 : 0)
              << config.name << (top_left ? " top-left " : " bottom-right ") << "square " << x << "," << y;
        }
      }
      // a line across the cell edges, width 2 at y = 3 in the cell: its edges are at 2 and 4, so at 2.5 and 4.5 in the picture (centers)
      GSHSCENE_List * t2 = rf_open(&ctx);
      static const double line[] = {-6.0, 3.0, 12.0, 3.0};
      rf_stroke_crisp(&ctx, t2, rf_polyline(&ctx, line, 2, 0), rf_pen(&ctx, 2.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr,
          rf_solid(0, 0, 0, 0.5));
      const std::vector<uint8_t> b = render_tile(t2);
      for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
          // rows 2 and 3 of each cell (centers 2.5 and 3.5) under the top-left rule, 3 and 4 (3.5 and 4.5) under the bottom-right one
          const int r = (y + 6 - 0) % 6;
          const bool row = top_left ? (r == 2 || r == 3) : (r == 3 || r == 4);
          const bool in = x >= 3 && x < 28 && y >= 3 && y < 27 && row;
          EXPECT_EQ((int)b[(size_t)y * 32 + (size_t)x], in ? 128 : 0)
              << config.name << (top_left ? " top-left " : " bottom-right ") << "line " << x << "," << y;
        }
      }
      // a bar across the cell edges drawn as a fill: the same rows, so that a fill's tie rule is the item's and not the cell's
      GSHSCENE_List * t3 = rf_open(&ctx);
      rf_fill_crisp(&ctx, t3, rf_rect(&ctx, -6, 2, 12, 4), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
      const std::vector<uint8_t> c3 = render_tile(t3);
      for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
          const int r = y % 6;
          const bool row = top_left ? (r == 2 || r == 3) : (r == 3 || r == 4);
          const bool in = x >= 3 && x < 28 && y >= 3 && y < 27 && row;
          EXPECT_EQ((int)c3[(size_t)y * 32 + (size_t)x], in ? 128 : 0)
              << config.name << (top_left ? " top-left " : " bottom-right ") << "bar " << x << "," << y;
        }
      }
      rf_release(&ctx);
    }
  }
  rf_crisp_mode = GSHSCENE_ANTIALIAS_NONE;
}

// AC4: under a turn the cell is a quadrilateral inside a larger box, so the box does not hold a copy to its cell.  An item that spills over
// one side of the cell only (a rectangle, a line, and the inside of a ring) is covered in the cell it is of and nowhere else: a pixel is
// covered exactly when its center, taken back into the pattern's plane, lies in the item's own place in its cell.  (Two copies that cover a
// pixel are summed and saturate, so a spill that falls where the next copy has its own item shows nothing; this one falls where it has none.)
TEST(RenderAntialias, TheCrispItemsOfATurnedTileAreCoveredInTheirOwnCellsOnly) {
  const double degrees = 30.0;
  const double c = std::cos(degrees * 3.14159265358979323846 / 180.0);
  const double s = std::sin(degrees * 3.14159265358979323846 / 180.0);
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * region = rf_rect(&ctx, 2.0, 2.0, 30.0, 30.0);
    GSHGEOM_Map turn;
    ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, degrees, &turn), GSHGEOM_OK);
    for (int which = 0; which < 4; which++) {
      GSHSCENE_List * t = rf_open(&ctx);
      auto box = [](double lu, double lv, double a, double b, double c2, double d) { return lu >= a && lu < c2 && lv >= b && lv < d; };
      std::function<bool(double, double)> place; // the item's own place, in the plane of a cell of 6 by 6, whose reach passes it on the right
      if (which == 0) {
        place = [&](double lu, double lv) { return box(lu, lv, 4.0, 1.0, 6.0, 5.0); };
        rf_fill_crisp(&ctx, t, rf_rect(&ctx, 4.0, 1.0, 14.0, 5.0), nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
      } else if (which == 1) {
        place = [&](double lu, double lv) { return box(lu, lv, 4.0, 2.0, 6.0, 4.0); };
        static const double line[] = {4.0, 3.0, 14.0, 3.0};
        rf_stroke_crisp(&ctx, t, rf_polyline(&ctx, line, 2, 0), rf_pen(&ctx, 2.0, GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_CAP_BUTT), nullptr,
            rf_solid(0, 0, 0, 0.5));
      } else {
        // a ring whose band of width 2 lies inside it (which 2) or outside it (which 3): the aligned part of the stroke
        const bool inside = which == 2;
        static const double inner[] = {3.0, 1.0, 15.0, 1.0, 15.0, 6.0, 3.0, 6.0};
        static const double outer[] = {4.0, 2.0, 8.0, 2.0, 8.0, 4.0, 4.0, 4.0};
        place = inside ? std::function<bool(double, double)>([&](double lu, double lv) { return box(lu, lv, 3.0, 1.0, 15.0, 6.0) && !box(lu, lv, 5.0, 3.0, 13.0, 4.0); })
                       : std::function<bool(double, double)>([&](double lu, double lv) { return box(lu, lv, 2.0, 0.0, 10.0, 6.0) && !box(lu, lv, 4.0, 2.0, 8.0, 4.0); });
        rf_stroke_crisp(&ctx, t, rf_polyline(&ctx, inside ? inner : outer, 4, 1),
            rf_style(&ctx, 2.0, GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_JOIN_MITER, 4.0, GSHSCENE_STROKE_CAP_BUTT,
                GSHSCENE_STROKE_DEGENERATE_DRAW, nullptr, 0, 0.0, GSHSCENE_STROKE_DASH_PER_SUBPATH,
                inside ? GSHSCENE_STROKE_ALIGN_INSIDE : GSHSCENE_STROKE_ALIGN_OUTSIDE, GSHGEOM_FILL_NONZERO),
            nullptr, rf_solid(0, 0, 0, 0.5));
      }
      rf_close(&ctx, t);
      const GSHSCENE_Resource * pat = rf_pattern(&ctx, rf_picture_of(&ctx, t), 0, 0, 6, 6, 6, 6, 1, 1, turn, nullptr);
      GSHSCENE_List * l = rf_open(&ctx);
      rf_fill_crisp(&ctx, l, region, nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
      rf_close(&ctx, l);
      const std::vector<uint8_t> got = draw32(l, config);
      size_t covered = 0, wrong = 0, skipped = 0;
      for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
          const double px = x + 0.5, py = y + 0.5;
          const double u = c * px + s * py, v = -s * px + c * py; // the pixel's center in the pattern's plane
          auto at = [&](double du, double dv) {
            const double a = u + du, b = v + dv;
            return place(a - 6.0 * std::floor(a / 6.0), b - 6.0 * std::floor(b / 6.0));
          };
          const double e = 0.03; // a center this close to an edge is decided by the rounding of the outline to 1/64 (and of the map), not by the rule
          const bool in = at(0, 0);
          if (at(e, 0) != in || at(-e, 0) != in || at(0, e) != in || at(0, -e) != in || at(e, e) != in || at(-e, -e) != in || at(e, -e) != in ||
              at(-e, e) != in) {
            skipped++;
            continue;
          }
          const int want = in && x >= 2 && x < 30 && y >= 2 && y < 30 ? 128 : 0;
          wrong += (int)got[(size_t)y * 32 + (size_t)x] != want;
          covered += got[(size_t)y * 32 + (size_t)x] != 0;
        }
      }
      EXPECT_GT(covered, 20u) << config.name << " item " << which;
      EXPECT_LT(skipped, 40u) << config.name << " item " << which << ": the reference must decide nearly every pixel";
      EXPECT_EQ(wrong, 0u) << config.name << " item " << which;
    }
    rf_release(&ctx);
  }
}

// AC4: a crisp item of a tile is not cut to its cell (a cut point is rounded to 1/64, which moves a slanted edge a little, and a center that
// lies exactly on the edge, a tie, would change sides).  The tile's pixels are those of the item drawn alone: a triangle whose slanted edges
// pass exactly through centers, and cross the cell's sides at points that are not multiples of 1/64, so a cut would decide every one of
// those centers anew.
TEST(RenderAntialias, ACrispItemOfATileWhoseEdgeCrossesTheCellIsDecidedAsIfItWereNotCut) {
  struct Tri {
    double p[6];
  };
  // slope 1/3 through (0.5, 0.5) and (3.5, 1.5); its mirror; and slope 3 (steep) through the same centers swapped
  const Tri tris[] = {
      {{0.5, 0.5, 12.5, 4.5, 0.5, 12.5}},
      {{12.5, 0.5, 0.5, 4.5, 12.5, 12.5}},
      {{0.5, 0.5, 4.5, 12.5, 12.5, 0.5}},
      {{0.5, 12.5, 4.5, 0.5, 12.5, 12.5}},
  };
  for (const Config & config : kConfigs) {
    for (size_t k = 0; k < sizeof tris / sizeof tris[0]; k++) {
      RF_Ctx ctx;
      rf_begin(&ctx);
      const GSHSCENE_Resource * tri = rf_polyline(&ctx, tris[k].p, 3, 1);
      GSHSCENE_List * alone = rf_open(&ctx);
      rf_fill_crisp(&ctx, alone, tri, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
      rf_close(&ctx, alone);
      const std::vector<uint8_t> want = draw32(alone, config);
      GSHSCENE_List * tile = rf_open(&ctx);
      rf_fill_crisp(&ctx, tile, tri, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0.5), 0, 0.0);
      rf_close(&ctx, tile);
      const GSHSCENE_Resource * pat = rf_pattern(&ctx, rf_picture_of(&ctx, tile), 0, 0, 6, 6, 6, 6, 1, 1, rf_move(0.0, 0.0), nullptr);
      GSHSCENE_List * l = rf_open(&ctx);
      rf_fill_crisp(&ctx, l, rf_rect(&ctx, 0.0, 0.0, 6.0, 6.0), nullptr, GSHGEOM_FILL_NONZERO, rf_pattern_paint(pat, 1.0), 0, 0.0);
      rf_close(&ctx, l);
      const std::vector<uint8_t> got = draw32(l, config);
      size_t covered = 0, wrong = 0;
      for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 6; x++) {
          wrong += got[(size_t)y * 32 + (size_t)x] != want[(size_t)y * 32 + (size_t)x];
          covered += want[(size_t)y * 32 + (size_t)x] != 0;
        }
      }
      EXPECT_GT(covered, 6u) << config.name << " triangle " << k;
      EXPECT_EQ(wrong, 0u) << config.name << " triangle " << k;
      rf_release(&ctx);
    }
  }
}

// FT-2 and the lean sweeps assume an accumulator they left clean: a crisp fill between two antialiased ones must leave it so.
TEST(RenderAntialias, ACrispFillBetweenTwoAntialiasedOnesLeavesTheAccumulatorTheNextOneNeeds) {
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * a = rf_star(&ctx, 8.0, 8.0, 7.0);
    const GSHSCENE_Resource * b = rf_star(&ctx, 8.0, 16.0, 7.0);
    const GSHSCENE_Resource * c = rf_blob(&ctx, 18.0, 4.0, 5.0);
    auto last_alone = [&]() {
      GSHSCENE_List * l = rf_open(&ctx);
      rf_fill(&ctx, l, c, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
      rf_close(&ctx, l);
      return draw32(l, config);
    };
    GSHSCENE_List * l = rf_open(&ctx);
    rf_fill(&ctx, l, a, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
    rf_fill_crisp(&ctx, l, b, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
    rf_fill(&ctx, l, c, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
    rf_close(&ctx, l);
    const std::vector<uint8_t> all = draw32(l, config);
    const std::vector<uint8_t> alone = last_alone();
    size_t partial = 0;
    for (int y = 0; y < 32; y++) {
      for (int x = 16; x < 32; x++) { // the third fill's columns: the first two lie left of 16
        EXPECT_EQ(all[(size_t)y * 32 + (size_t)x], alone[(size_t)y * 32 + (size_t)x]) << config.name << " " << x << "," << y;
        partial += alone[(size_t)y * 32 + (size_t)x] != 0 && alone[(size_t)y * 32 + (size_t)x] != 255;
      }
    }
    EXPECT_GT(partial, 4u);
    rf_release(&ctx);
  }
}

// The same, where the next fill lies on the cells the crisp one wrote: its leftover (a crisp fill's accumulator is not zero when it is done,
// and a fast sweep that finds the first bytes zero does not clear them) would be read as area.  The crisp fill is invisible, so that the
// picture is that of the other two alone.
TEST(RenderAntialias, ACrispFillLeavesTheAccumulatorToTheNextFillOnTheSameCells) {
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * big = rf_star(&ctx, 8.0, 8.0, 7.0);
    const GSHSCENE_Resource * same = rf_star(&ctx, 8.0, 20.0, 7.0);
    auto draw = [&](bool with_crisp) {
      GSHSCENE_List * l = rf_open(&ctx);
      rf_fill(&ctx, l, big, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
      if (with_crisp) {
        rf_fill_crisp(&ctx, l, same, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 0), 0, 0.0);
      }
      rf_fill(&ctx, l, same, nullptr, GSHGEOM_FILL_NONZERO, rf_solid(0, 0, 0, 1), 0, 0.0);
      rf_close(&ctx, l);
      return draw32(l, config);
    };
    const std::vector<uint8_t> without = draw(false);
    size_t partial = 0;
    for (uint8_t v : without) {
      partial += v != 0 && v != 255;
    }
    EXPECT_GT(partial, 8u) << config.name;
    EXPECT_EQ(draw(true), without) << config.name;
    rf_release(&ctx);
  }
}

// A crisp stroke painted with a gradient takes none of the fast tiers either: it is the solid crisp stroke.
TEST(RenderAntialias, ACrispStrokePaintedWithAGradientIsTheSolidCrispStroke) {
  for (const Config & config : kConfigs) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    const GSHSCENE_Resource * path = rf_star(&ctx, 15.0, 16.0, 12.0);
    const GSHSCENE_Resource * pen = rf_pen(&ctx, 1.75, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_CAP_BUTT);
    const GSHSCENE_Gradient_Stop two[2] = {rf_stop(0.0, rf_solid(0.9, 0.1, 0.1, 1.0)), rf_stop(1.0, rf_solid(0.1, 0.2, 0.9, 1.0))};
    GSHSCENE_List * g = rf_open(&ctx);
    rf_stroke_crisp(&ctx, g, path, pen, nullptr, rf_gradient_paint(rf_gradient(&ctx, rf_linear(0, 0, 30, 0, 0, 0, GSHSCENE_EXTEND_PAD), two, 2), 1.0));
    rf_close(&ctx, g);
    GSHSCENE_List * s = rf_open(&ctx);
    rf_stroke_crisp(&ctx, s, path, pen, nullptr, rf_solid(0, 0, 0, 1));
    rf_close(&ctx, s);
    EXPECT_EQ(draw32(g, config), draw32(s, config)) << config.name;
    rf_release(&ctx);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
