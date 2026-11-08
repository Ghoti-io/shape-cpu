/**
 * @file
 *
 * The rule of coverage by the centers of the pixels (shape-scene's list.h, AA2 and AA3) held against a brute-force evaluation of it:
 * for random fills (lines, quadratics and cubics, one to three contours, both rules, an exact map and a general one, a glyph run of
 * several paths) and random strokes (open and closed, every cap and join, dashes, local and device widths, a general map), the pixels
 * the sink covers in each combination of the fast and the exact coverage are the pixels whose centers the rule puts inside the
 * item's flattened device outline.  The reference shares the geometry (the sink's own flattener and expansion, which are not what is
 * under test) and nothing of the way a center is decided: it tests every center against every edge with one cross product.  The
 * rule's exact statement and its vectors, which share no code at all, are tests/center_vectors.h.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "../render_helpers.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace rh;

namespace {

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed) {}
  uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
  double unit() { return (double)(next() >> 11) / (double)(1ull << 53); }
  double range(double lo, double hi) { return lo + (hi - lo) * unit(); }
  int below(int n) { return (int)(next() % (uint64_t)n); }
};

const int kSize = 24;

struct Config {
  uint64_t fill, stroke;
};
const Config kConfigs[] = {
    {GSHCPU_FILL_COVERAGE_EXACT, GSHCPU_STROKE_COVERAGE_EXACT},
    {GSHCPU_FILL_COVERAGE_FAST, GSHCPU_STROKE_COVERAGE_FAST},
};

/**
 * AA2 literally, with one cross product for each edge and center: whether the center @p (cx, cy) is inside the contours of @p paths
 * (every point shifted by (-sx, -sy) pixels first), by the rule.  The numbers are small (a point is under 2^20 units), so `int64` holds them.
 */
bool inside(const std::vector<const GSHGEOM_Flat_Path *> & paths, int64_t cx, int64_t cy, GSHGEOM_Fill_Rule rule) {
  int64_t winding = 0;
  for (const GSHGEOM_Flat_Path * path : paths) {
    const GSHGEOM_Device_Point * pts = gshgeom_flat_path_points(path);
    for (size_t s = 0; s < gshgeom_flat_path_subpath_count(path); s++) {
      GSHGEOM_Flat_Subpath sub;
      EXPECT_EQ(gshgeom_flat_path_subpath(path, s, &sub), GSHGEOM_OK);
      for (size_t k = 0; k < sub.count; k++) {
        const GSHGEOM_Device_Point a = pts[sub.first + (k ? k - 1 : sub.count - 1)];
        const GSHGEOM_Device_Point b = pts[sub.first + k];
        const bool below_a = (int64_t)a.y <= cy;
        const bool below_b = (int64_t)b.y <= cy;
        if (below_a == below_b) {
          continue;
        }
        // x_e = a.x + (cy - a.y) (b.x - a.x) / (b.y - a.y) > cx, with the sign of the denominator folded in
        const int64_t dy = (int64_t)b.y - a.y;
        const int64_t lhs = (int64_t)a.x * dy + (cy - a.y) * ((int64_t)b.x - a.x);
        const int64_t rhs = cx * dy;
        const bool counts = dy > 0 ? lhs > rhs : lhs < rhs;
        if (counts) {
          winding += dy > 0 ? 1 : -1;
        }
      }
    }
  }
  return rule == GSHGEOM_FILL_EVEN_ODD ? (winding & 1) != 0 : winding != 0;
}

GIMG_Raster * clear_raster() {
  GIMG_Pixel_Format f = rf_pixel_format(GSHCPU_FORMAT_RGBA8_STRAIGHT);
  GIMG_Raster * r = nullptr;
  EXPECT_EQ(gimg_raster_create(kSize, kSize, &f, GIMG_RASTER_OWNED, nullptr, 0, &r), GIMG_OK);
  unsigned char * px = (unsigned char *)gimg_raster_pixels(r);
  for (int y = 0; y < kSize; y++) {
    std::memset(px + (size_t)y * gimg_raster_stride_bytes(r), 0, (size_t)kSize * 4u);
  }
  return r;
}

std::vector<uint8_t> render(const GSHSCENE_List * list, const Config & config) {
  GIMG_Raster * raster = clear_raster();
  RasterRef keep(raster);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.fill_coverage = config.fill;
  limits.stroke_coverage = config.stroke;
  SinkRef sink(&limits);
  ReportRef report;
  GSHSCENE_Target target = target_of(raster);
  GSHSCENE_Render_Request request = request_of(list, &target, report.report);
  EXPECT_EQ(gshscene_sink_render(sink.sink, &request), GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_count(report.report), 0u);
  std::vector<uint8_t> out;
  const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(raster);
  for (int y = 0; y < kSize; y++) {
    for (int x = 0; x < kSize; x++) {
      out.push_back(px[(size_t)y * gimg_raster_stride_bytes(raster) + (size_t)x * 4u + 3u]);
    }
  }
  return out;
}

GSHGEOM_Point rp(Rng & r) {
  return rf_pt(r.range(-3.0, (double)kSize + 3.0), r.range(-3.0, (double)kSize + 3.0));
}

/** A random path of one to three contours of lines, quadratics and cubics; every contour closed or left open. */
GSHGEOM_Path * random_path(Rng & r, int max_contours, bool allow_open) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  const int contours = 1 + r.below(max_contours);
  for (int c = 0; c < contours; c++) {
    gshgeom_path_move_to(p, rp(r));
    const int segs = 2 + r.below(5);
    for (int s = 0; s < segs; s++) {
      switch (r.below(3)) {
        case 0: gshgeom_path_line_to(p, rp(r)); break;
        case 1: gshgeom_path_quadratic_to(p, rp(r), rp(r)); break;
        default: gshgeom_path_cubic_to(p, rp(r), rp(r), rp(r)); break;
      }
    }
    if (!allow_open || r.below(3) != 0) {
      gshgeom_path_close(p);
    }
  }
  return p;
}

/** A map: the identity, a translation by a whole number of 64ths, or a general one (a turn, a scale, a skew, a fractional shift). */
GSHGEOM_Map random_map(Rng & r, int kind) {
  GSHGEOM_Map m;
  if (kind == 0) {
    gshgeom_map_identity(2, &m);
  } else if (kind == 1) {
    gshgeom_map_translate(2, (double)r.below(64) / 64.0 + (double)r.below(3), (double)r.below(64) / 64.0, 0.0, &m);
  } else {
    const double a = r.range(0.0, 6.28318), sx = r.range(0.6, 1.4), sy = r.range(0.6, 1.4), k = r.range(-0.3, 0.3);
    const double mat[4] = {sx * std::cos(a), sx * std::sin(a), -sy * std::sin(a) + k, sy * std::cos(a)};
    const double t[2] = {r.range(-2.0, 2.0), r.range(-2.0, 2.0)};
    EXPECT_EQ(gshgeom_map_from_matrix(2, mat, t, &m), GSHGEOM_OK);
  }
  return m;
}

}  // namespace

// Every pixel of a random fill is the rule's answer, in the two tiers; the fills are of several paths (a run), several contours, both rules.
TEST(RenderAntialiasDiff, RandomFillsCoverTheCentersTheRuleSaysUnderEveryTier) {
  Rng r(0xA11A5ED);
  size_t covered = 0;
  size_t cases = 0;
  for (int i = 0; i < 160; i++) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHSCENE_List * list = rf_open(&ctx);
    const GSHGEOM_Fill_Rule rule = r.below(2) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
    const GSHGEOM_Map map = random_map(r, r.below(3));
    const GSHSCENE_Resource * resource = nullptr;
    const bool run = r.below(5) == 0;
    if (run) {
      // a run of three outlines at offsets: the windings of its glyphs add
      const GSHSCENE_Resource * outlines[3];
      for (auto & o : outlines) {
        GSHGEOM_Path * p = random_path(r, 2, false);
        o = rf_intern(&ctx, p);
      }
      GSHSCENE_Glyph glyphs[3] = {{1, 0.0, 0.0, outlines[0]}, {2, r.range(-2.0, 2.0), r.range(-2.0, 2.0), outlines[1]},
          {3, r.range(-2.0, 2.0), r.range(-2.0, 2.0), outlines[2]}};
      GSHSCENE_Glyph_Run gr{};
      gr.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
      gr.glyph_count = 3;
      gr.glyphs = glyphs;
      const GSHSCENE_Resource * rr = nullptr;
      ASSERT_EQ(gshscene_store_intern_glyph_run(ctx.store, &gr, &rr), GSHSCENE_OK);
      resource = rf_hold(&ctx, rr);
    } else {
      resource = rf_intern(&ctx, random_path(r, 3, true));
    }
    GSHSCENE_Paint paint = rf_solid(0, 0, 0, 1);
    ASSERT_EQ(gshscene_list_fill_antialias(list, resource, &map, rule, &paint, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
    rf_close(&ctx, list);
    // the reference: the sink's own device outline of the item
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
    GSHGEOM_Map chain[2];
    GSHGEOM_Map identity;
    gshgeom_map_identity(2, &identity);
    GSHGEOM_Limits geometry;
    gshgeom_limits_default(&geometry);
    GSHCPU_Item_Geometry g;
    std::memset(&g, 0, sizeof g);
    ASSERT_EQ(gshcpu_render_geometry(item, chain, 0, &identity, gshgeom_allocator_default(), &geometry, &g), GSHCPU_OK);
    std::vector<const GSHGEOM_Flat_Path *> paths;
    for (size_t k = 0; k < g.count; k++) {
      if (g.paths[k]) {
        paths.push_back(g.paths[k]);
      }
    }
    std::vector<uint8_t> want;
    for (int y = 0; y < kSize; y++) {
      for (int x = 0; x < kSize; x++) {
        want.push_back(inside(paths, 64 * x + 32, 64 * y + 32, rule) ? 255 : 0);
      }
    }
    gshcpu_render_geometry_release(&g, gshgeom_allocator_default());
    for (const Config & config : kConfigs) {
      const std::vector<uint8_t> got = render(list, config);
      size_t differ = 0;
      for (size_t k = 0; k < got.size(); k++) {
        differ += got[k] != want[k];
      }
      EXPECT_EQ(differ, 0u) << "fill " << i << (run ? " (a run)" : "") << " rule " << (int)rule << " fill coverage " << config.fill;
    }
    for (uint8_t v : want) {
      covered += v != 0;
    }
    cases++;
    rf_release(&ctx);
  }
  EXPECT_GT(covered, cases * 20) << "the cases cover something: the comparison is not between two empty pictures";
}

// The same for strokes: the pixels of the stroke's outline (a centered one: its one part) whose centers the nonzero rule puts inside.
TEST(RenderAntialiasDiff, RandomStrokesCoverTheCentersTheRuleSaysUnderEveryTier) {
  Rng r(0x57A0CE5);
  size_t covered = 0;
  size_t drawn = 0;
  for (int i = 0; i < 160; i++) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHSCENE_List * list = rf_open(&ctx);
    const GSHSCENE_Resource * path = rf_intern(&ctx, random_path(r, 2, true));
    const uint32_t joins[5] = {GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_JOIN_BEVEL,
        GSHSCENE_STROKE_JOIN_MITER_CLIP, GSHSCENE_STROKE_JOIN_ARCS};
    const uint32_t caps[3] = {GSHSCENE_STROKE_CAP_BUTT, GSHSCENE_STROKE_CAP_ROUND, GSHSCENE_STROKE_CAP_SQUARE};
    double dashes[4] = {r.range(0.5, 4.0), r.range(0.3, 3.0), r.range(0.5, 4.0), r.range(0.3, 3.0)};
    const bool dashed = r.below(3) == 0;
    const GSHSCENE_Resource * style = rf_style(&ctx, r.range(0.4, 5.0), r.below(4) == 0 ? GSHSCENE_STROKE_WIDTH_DEVICE : GSHSCENE_STROKE_WIDTH_LOCAL,
        joins[r.below(5)], r.range(1.0, 5.0), caps[r.below(3)], GSHSCENE_STROKE_DEGENERATE_DRAW, dashed ? dashes : nullptr, dashed ? 4 : 0,
        r.range(0.0, 3.0), GSHSCENE_STROKE_DASH_PER_SUBPATH, GSHSCENE_STROKE_ALIGN_CENTER, GSHGEOM_FILL_NONZERO);
    const GSHGEOM_Map map = random_map(r, r.below(3));
    GSHSCENE_Paint paint = rf_solid(0, 0, 0, 1);
    ASSERT_EQ(gshscene_list_stroke_antialias(list, path, style, &map, &paint, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
    rf_close(&ctx, list);
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
    GSHGEOM_Limits geometry;
    gshgeom_limits_default(&geometry);
    GSHCPU_Stroke s;
    std::memset(&s, 0, sizeof s);
    GSHGEOM_Map composed = item->map;
    const GSHCPU_Result opened = gshcpu_render_stroke_open_lines(nullptr, gshgeom_allocator_default(), &geometry, 0, item, &composed, &s);
    ASSERT_EQ(opened, GSHCPU_OK);
    std::vector<uint8_t> want(kSize * kSize, 0);
    if (s.drawn) {
      ASSERT_FALSE(s.meta.present[GSHCPU_STROKE_ALIGNED]) << "a centered stroke has one part";
      std::vector<const GSHGEOM_Flat_Path *> paths;
      paths.push_back(gshcpu_render_stroke_cover_path(&s));
      for (int y = 0; y < kSize; y++) {
        for (int x = 0; x < kSize; x++) {
          // the outline is in the canonical frame: the device's moved back by the whole pixels of the shift
          want[(size_t)y * kSize + (size_t)x] =
              inside(paths, 64 * (x - s.shift_x) + 32, 64 * (y - s.shift_y) + 32, GSHGEOM_FILL_NONZERO) ? 255 : 0;
        }
      }
      drawn++;
    }
    gshcpu_render_stroke_close(nullptr, &s);
    for (const Config & config : kConfigs) {
      const std::vector<uint8_t> got = render(list, config);
      size_t differ = 0;
      for (size_t k = 0; k < got.size(); k++) {
        differ += got[k] != want[k];
      }
      EXPECT_EQ(differ, 0u) << "stroke " << i << " stroke coverage " << config.stroke;
    }
    for (uint8_t v : want) {
      covered += v != 0;
    }
    rf_release(&ctx);
  }
  EXPECT_GT(drawn, 100u);
  EXPECT_GT(covered, 160u * 10u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
