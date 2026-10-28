/**
 * @file
 *
 * Strokes in the CPU sink (strokes story S4): the style conversion, the canonical translation, what draws nothing,
 * the composition of the parts, the limits, the cache, the tiers and their equality with the general tier, damage and
 * caller memory, groups, and the coverage-level bracket against the geometric oracle.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"


#include <ghoti.io/shape-scene/dump_sink.h>

#include <atomic>
#include <cmath>
#include <thread>

using namespace sh;
using stroke_gate::Ops;
using stroke_gate::Params;

namespace {

const Ops kClosed = {{'M', {3.3, 4.2}}, {'L', {19.6, 6.1}}, {'L', {9.2, 18.4}}, {'Q', {3.0, 14.0, 4.4, 9.0}}, {'Z', {}}};
const Ops kOpen = {{'M', {3.0, 3.0}}, {'L', {18.0, 4.0}}, {'L', {6.0, 12.0}}, {'Q', {14.0, 14.0, 19.0, 19.0}}};
const Ops kLens = {{'M', {12.0, 4.0}}, {'C', {20.0, 4.0, 20.0, 16.0, 12.0, 16.0}}, {'C', {4.0, 16.0, 4.0, 4.0, 12.0, 4.0}}, {'Z', {}}};
const Ops kStar = {{'M', {12.0, 2.5}}, {'L', {16.0, 19.0}}, {'L', {3.0, 8.0}}, {'L', {21.0, 8.0}}, {'L', {8.0, 19.0}}, {'Z', {}}};
const Ops kMixed = {{'M', {3.0, 3.0}}, {'L', {14.0, 3.5}}, {'L', {13.0, 13.0}}, {'L', {4.0, 12.5}}, {'Z', {}},
    {'M', {16.0, 6.0}}, {'L', {23.0, 19.0}}, {'L', {17.0, 22.0}}};
const Ops kDot = {{'M', {10.0, 10.0}}, {'L', {10.0, 10.0}}, {'M', {15.0, 15.0}}, {'Z', {}}};

Params params(double width = 3.0, uint32_t join = GSHGEOM_STROKE_JOIN_ROUND, uint32_t cap = GSHGEOM_STROKE_CAP_BUTT) {
  Params p;
  p.s.width = width;
  p.s.join = join;
  p.s.cap = cap;
  return p;
}

GSHGEOM_Map frac_map() {
  return translation(0.375, 0.625);
}

}  // namespace

/* --- SC1, SC2: the sink draws what the style says, whatever it says --------------------------------------------- */

TEST(RenderStroke, EveryValueOfEveryFieldDrawsAndNothingIsRefusedForItsStyle) {
  size_t drawn = 0;
  for (uint32_t space = 0; space < GSHGEOM_STROKE_WIDTH_SPACE_COUNT; space++) {
    for (uint32_t join = 0; join < GSHGEOM_STROKE_JOIN_COUNT; join++) {
      for (uint32_t cap = 0; cap < GSHGEOM_STROKE_CAP_COUNT; cap++) {
        for (uint32_t align = 0; align < GSHGEOM_STROKE_ALIGN_COUNT; align++) {
          for (int rule = 0; rule < 2; rule++) {
            for (int dashed = 0; dashed < 3; dashed++) {
              Params p = params(2.5, join, cap);
              p.s.width_space = space;
              p.s.alignment = align;
              p.s.alignment_rule = rule ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
              p.s.degenerate = (join + cap) % 2;
              if (dashed) {
                p.dashes = {3.0, 2.0, 1.0};
                p.s.dash_scope = dashed - 1;
                p.s.dash_offset = 0.5;
              }
              Store s;
              const GSHSCENE_List * l = one_stroke(s, kMixed, p.get(), frac_map());
              Sink sink;
              Drawn d = render_list(sink.sink, l, 28, 28);
              ASSERT_EQ(d.result, GSHSCENE_OK);
              for (const GSHSCENE_Report_Entry & e : d.entries) {
                EXPECT_NE(e.reason, GSHSCENE_REASON_STROKE_STYLE);
                EXPECT_NE(e.reason, GSHSCENE_REASON_ITEM_KIND);
              }
              EXPECT_TRUE(d.entries.empty()) << "drawn as defined: no entry";
              drawn += sink.counters.strokes;
              bool any = false;
              for (uint8_t a : d.alpha) {
                any = any || a;
              }
              EXPECT_TRUE(any) << "space " << space << " join " << join << " cap " << cap << " align " << align;
            }
          }
        }
      }
    }
  }
  EXPECT_EQ(drawn, 2u * 5u * 3u * 3u * 2u * 3u);
}

/* --- SC3: the canonical translation ------------------------------------------------------------------------------- */

TEST(RenderStroke, AWholePixelTranslationMovesThePixelsBitForBit) {
  struct Case {
    const char * what;
    const Ops * ops;
    Params p;
  };
  Params hair = params(1.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE);
  hair.s.width_space = GSHGEOM_STROKE_WIDTH_DEVICE;
  Params inside = params(3.0, GSHGEOM_STROKE_JOIN_MITER);
  inside.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  Params dashed = params(2.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  dashed.dashes = {3.0, 2.0};
  Case cases[] = {{"round joins", &kClosed, params()}, {"a hairline", &kOpen, hair}, {"inside", &kMixed, inside},
      {"dashes", &kOpen, dashed}};
  for (int sign : {1, -1}) {
  const int dx = 7 * sign;
  const int dy = -3 * sign;
  for (Case & c : cases) {
    Store s1;
    Store s2;
    Sink sink;
    Drawn a = render_list(sink.sink, one_stroke(s1, *c.ops, c.p.get(), translation(10.375, 14.625)), 64, 64);
    Drawn b = render_list(sink.sink, one_stroke(s2, *c.ops, c.p.get(), translation(10.375 + dx, 14.625 + dy)), 64, 64);
    ASSERT_EQ(a.result, GSHSCENE_OK) << c.what;
    ASSERT_EQ(b.result, GSHSCENE_OK) << c.what;
    int compared = 0;
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        int x2 = x + dx;
        int y2 = y + dy;
        if (x2 < 0 || x2 >= 64 || y2 < 0 || y2 >= 64) {
          continue;
        }
        EXPECT_EQ(a.alpha[(size_t)y * 64 + x], b.alpha[(size_t)y2 * 64 + x2]) << c.what << " at " << x << "," << y;
        compared += a.alpha[(size_t)y * 64 + x] != 0;
      }
    }
    EXPECT_GT(compared, 20) << c.what;
    // A different fraction is another expansion: another picture (and a miss, not a hit).
    Store s3;
    Drawn other = render_list(sink.sink, one_stroke(s3, *c.ops, c.p.get(), translation(10.5, 14.625)), 64, 64);
    EXPECT_NE(other.hash, a.hash) << c.what;
  }
  }
}

TEST(RenderStroke, ADifferentFractionOrLinearPartIsACacheMissAndAWholePixelShiftIsAHit) {
  Store s;
  Sink sink;
  Params p = params();
  const GSHSCENE_Resource * path = s.path(stroke_gate::build(kClosed));
  const GSHSCENE_Resource * style = intern_style(s, p.get());
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  auto list_at = [&](const GSHGEOM_Map & m) {
    GSHSCENE_List * l = s.open();
    EXPECT_EQ(gshscene_list_stroke(l, path, style, &m, &white, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  };
  render_list(sink.sink, list_at(translation(2.25, 3.5)), 48, 48);
  EXPECT_EQ(sink.counters.cache_misses, 1u);
  EXPECT_EQ(sink.counters.cache_hits, 0u);
  render_list(sink.sink, list_at(translation(12.25, 8.5)), 48, 48);
  EXPECT_EQ(sink.counters.cache_hits, 1u) << "the same fraction: the whole pixels are the draw's";
  render_list(sink.sink, list_at(translation(2.5, 3.5)), 48, 48);
  EXPECT_EQ(sink.counters.cache_misses, 2u) << "another fraction";
  render_list(sink.sink, list_at(affine_map(1.0, 0.25, 0.0, 1.0, 2.25, 3.5)), 48, 48);
  EXPECT_EQ(sink.counters.cache_misses, 3u) << "another linear part";
  render_list(sink.sink, list_at(translation(2.25, 3.5)), 48, 48);
  EXPECT_EQ(sink.counters.cache_hits, 2u);
  render_list(sink.sink, list_at(translation(2.25, 3.75)), 48, 48);
  EXPECT_EQ(sink.counters.cache_misses, 4u) << "another fraction in y alone";
  render_list(sink.sink, list_at(translation(2.25, 3.5)), 48, 48);
  EXPECT_EQ(sink.counters.cache_hits, 3u);
}

/* --- SC10: the test sink's outline= is the very outline that is drawn --------------------------------------------- */

TEST(RenderStroke, TheTestSinksOutlineHashIsTheOutlineTheSinkDraws) {
  Params styles[3] = {params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND), params(1.5), params(4.0)};
  styles[1].s.width_space = GSHGEOM_STROKE_WIDTH_DEVICE;
  styles[2].s.alignment = GSHGEOM_STROKE_ALIGN_OUTSIDE;
  styles[2].dashes = {4.0, 1.0};
  const GSHGEOM_Map maps[3] = {translation(3.375, 2.25), affine_map(1.5, 0.25, -0.5, 1.25, 4.75, 1.5), translation(-3.0, 9.0)};
  for (int i = 0; i < 3; i++) {
    Store s;
    const GSHSCENE_List * l = one_stroke(s, kMixed, styles[i].get(), maps[i]);
    // The test sink's K record, on a raster target of pixels.
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
    GSHSCENE_Target t = target_of(raster.raster);
    GSHSCENE_Render_Request req = request_of(l, &t, report.report);
    ASSERT_EQ(gshscene_sink_render(dump, &req), GSHSCENE_OK);
    gshscene_sink_destroy(dump);
    size_t at = text.find("outline=");
    ASSERT_NE(at, std::string::npos) << text;
    // The outline shape-cpu draws: the stroke opened as the sink opens it.
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

/* --- SC4: what draws nothing --------------------------------------------------------------------------------------- */

TEST(RenderStroke, ASingularChainAndAZeroWidthAndADegenerateGradientDrawNothingWithOneNoticeAndStrictStaysOk) {
  struct Case {
    const char* what;
    GSHSCENE_Reason reason;
  };
  const GSHGEOM_Map tiny = affine_map(0x1p-600, 0, 0, 0x1p-600, 0, 0);
  const GSHGEOM_Map flat = affine_map(0, 0, 0, 1, 0, 0);
  for (int variant = 0; variant < 5; variant++) {
    Store s;
    Params p = params();
    GSHGEOM_Map item_map = translation(2, 2);
    GSHGEOM_Map target = translation(0, 0);
    GSHSCENE_Paint paint = solid(1, 1, 1, 1);
    GSHSCENE_Reason want = GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
    const GSHSCENE_Resource * gradient = nullptr;
    switch (variant) {
      case 0:
        item_map = flat;
        break;
      case 1:
        item_map = tiny;
        target = tiny; // each factor invertible, the product underflows to a singular map (SK9 as amended)
        break;
      case 2:
        p.s.width = 0.0;
        want = GSHSCENE_REASON_STROKE_ZERO_WIDTH;
        break;
      case 3: {
        GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(0, 0, 0, 1)}, {1.0, solid(1, 1, 1, 1)}};
        GSHSCENE_Gradient g{};
        g.kind = GSHSCENE_GRADIENT_LINEAR; // p0 = p1 = 0: degenerate (G10)
        gshgeom_map_identity(2, &g.map);
        g.stop_count = 2;
        g.stops = stops;
        ASSERT_EQ(gshscene_store_intern_gradient(s.store, &g, &gradient), GSHSCENE_OK);
        s.held.push_back(gradient);
        paint = GSHSCENE_Paint{};
        paint.kind = GSHSCENE_PAINT_GRADIENT;
        paint.resource = gradient;
        paint.rgba[3] = 1.0;
        want = GSHSCENE_REASON_GRADIENT_DEGENERATE;
        break;
      }
      case 4: { // a flat instance map
        want = GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
        break;
      }
    }
    const GSHSCENE_Resource * path = s.path(stroke_gate::build(kClosed));
    const GSHSCENE_Resource * style = intern_style(s, p.get());
    GSHSCENE_List * l = s.open();
    if (variant == 4) {
      GSHSCENE_List * pic = s.open();
      ASSERT_EQ(gshscene_list_stroke(pic, path, style, nullptr, &paint, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(pic), GSHSCENE_OK);
      const GSHSCENE_Resource * picture = nullptr;
      ASSERT_EQ(gshscene_store_intern_picture(s.store, pic, &picture), GSHSCENE_OK);
      s.held.push_back(picture);
      ASSERT_EQ(gshscene_list_instance(l, picture, &flat, 0, 0.0), GSHSCENE_OK);
    } else {
      ASSERT_EQ(gshscene_list_stroke(l, path, style, &item_map, &paint, 0, 0.0), GSHSCENE_OK) << "variant " << variant;
    }
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_STRICT}) {
      Sink sink;
      Drawn d = render_list(sink.sink, l, 24, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, flags, &target);
      EXPECT_EQ(d.result, GSHSCENE_OK) << variant << ": a notice never fails strict";
      for (uint8_t a : d.alpha) {
        ASSERT_EQ(a, 0) << variant;
      }
      ASSERT_EQ(d.entries.size(), 1u) << variant << ": the plan wrote one notice, the sink none";
      EXPECT_EQ(d.entries[0].entry_class, GSHSCENE_ENTRY_NOTICE);
      EXPECT_EQ(d.entries[0].reason, want) << variant;
      EXPECT_EQ(sink.counters.strokes, 0u);
    }
  }
}

/* --- SC5: the composition ------------------------------------------------------------------------------------------ */

TEST(RenderStroke, TheCompositionIsTheUnionFormulaInD8sArithmeticAndExactAtBothEnds) {
  // k = 255 - ((255 - b)(255 - t) + 127) / 255 with t = (a c' + 127) / 255: restated against the rational union
  // k_b + k_a c' - k_b k_a c' (E9), over every b, a and c.
  long worst_hundredths = 0;
  for (int complement = 0; complement < 2; complement++) {
    for (uint32_t b = 0; b < 256; b++) {
      for (uint32_t a = 0; a < 256; a += (a < 4 || a > 251 ? 1 : 3)) {
        for (uint32_t c = 0; c < 256; c += (c < 4 || c > 251 ? 1 : 5)) {
          uint32_t k = gshcpu_render_stroke_compose(b, a, c, complement);
          uint32_t cp = complement ? 255 - c : c;
          ASSERT_LE(k, 255u);
          ASSERT_GE(k, b) << "never below the body";
          if (a == 0 || cp == 0) {
            ASSERT_EQ(k, b) << "no aligned part: the body, bit for bit";
          }
          if (b == 0) {
            ASSERT_EQ(k, (a * cp + 127) / 255) << "no body: the weighed aligned part";
          }
          if (b == 255) {
            ASSERT_EQ(k, 255u);
          }
          if (a == 255 && cp == 255) {
            ASSERT_EQ(k, 255u) << "the aligned part wholly in: the union is full";
          }
          const uint32_t t = (a * cp + 127) / 255;
          ASSERT_EQ(k, 255u - ((255u - b) * (255u - t) + 127u) / 255u) << "the formula itself, in D8's arithmetic: " << b << " " << a << " " << c;
          double kb = b / 255.0;
          double kat = a / 255.0 * cp / 255.0;
          double exact = (kb + kat - kb * kat) * 255.0;
          long diff = (long)std::lround(std::fabs((double)k - exact) * 100.0);
          worst_hundredths = std::max(worst_hundredths, diff);
        }
      }
    }
  }
  EXPECT_LE(worst_hundredths, 150) << "within a unit and a half of the rational union (two roundings)";
  // Monotone in each coverage.
  for (uint32_t b = 0; b + 1 < 256; b++) {
    EXPECT_LE(gshcpu_render_stroke_compose(b, 120, 200, 0), gshcpu_render_stroke_compose(b + 1, 120, 200, 0));
    EXPECT_LE(gshcpu_render_stroke_compose(40, b, 200, 1), gshcpu_render_stroke_compose(40, b + 1, 200, 1));
    EXPECT_GE(gshcpu_render_stroke_compose(40, 200, b, 1), gshcpu_render_stroke_compose(40, 200, b + 1, 1))
        << "the complement reverses the clip's direction";
  }
}

TEST(RenderStroke, AnAlignedStrokeCoversExactlyTheBandItNamesOnPixelAlignedSquares) {
  // A square of whole pixels, so that every pixel is wholly in or wholly out: width 4 inside is the 4 pixels within the
  // boundary, width 4 outside the 4 pixels beyond it (the miter joins fill the outer corners); even-odd and nonzero
  // clips differ inside a hole of a path of two nested squares.
  const Ops square = {{'M', {8.0, 8.0}}, {'L', {24.0, 8.0}}, {'L', {24.0, 24.0}}, {'L', {8.0, 24.0}}, {'Z', {}}};
  const uint32_t n = 32;
  unsigned tiers = GSHCPU_RENDER_TIERS_ALL;
  auto band = [&](uint32_t alignment, GSHGEOM_Fill_Rule rule, const Ops & ops, double width) {
    Params p = params(width, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_BUTT);
    p.s.alignment = alignment;
    p.s.alignment_rule = rule;
    Store st;
    Sink sink(tiers);
    Drawn d = render_list(sink.sink, one_stroke(st, ops, p.get(), translation(0.0, 0.0)), n, n);
    EXPECT_EQ(d.result, GSHSCENE_OK);
    return d.alpha;
  };
  // The union of the pieces (the exact step) and the build without it (F4's sum, the gates' control) draw the same
  // bands where nothing overlaps.
  for (unsigned t : {(unsigned)GSHCPU_RENDER_TIERS_ALL, (unsigned)(GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_EXACT)}) {
    tiers = t;
    auto at = [&](const std::vector<uint8_t> & a, uint32_t x, uint32_t y) { return (unsigned)a[y * n + x]; };
    // The edges of a band meet a pixel's edge only to within the expansion's tolerance, so a pixel is judged where it and
    // its eight neighbors agree on being in the band or out of it: wholly covered (at least 250) or all but clear (at most 8).
    auto judge = [&](const std::vector<uint8_t> & a, const char * what, auto in_band) {
      ASSERT_EQ(a.size(), (size_t)n * n);
      size_t covered = 0;
      size_t clear = 0;
      for (uint32_t y = 1; y + 1 < n; y++) {
        for (uint32_t x = 1; x + 1 < n; x++) {
          int sum = 0;
          for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
              sum += in_band(x + dx, y + dy) ? 1 : 0;
            }
          }
          if (sum == 9) {
            EXPECT_GE(at(a, x, y), 250u) << what << " " << x << " " << y;
            covered++;
          } else if (sum == 0) {
            EXPECT_LE(at(a, x, y), 8u) << what << " " << x << " " << y;
            clear++;
          }
        }
      }
      EXPECT_GT(covered, 50u) << what;
      EXPECT_GT(clear, 100u) << what;
    };
    auto in_square = [](uint32_t x, uint32_t y) { return x >= 8 && x < 24 && y >= 8 && y < 24; };
    judge(band(GSHGEOM_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO, square, 4.0), "inside", [&](uint32_t x, uint32_t y) {
      return in_square(x, y) && (x < 12 || x >= 20 || y < 12 || y >= 20);
    });
    judge(band(GSHGEOM_STROKE_ALIGN_OUTSIDE, GSHGEOM_FILL_NONZERO, square, 4.0), "outside", [&](uint32_t x, uint32_t y) {
      return !in_square(x, y) && x >= 4 && x < 28 && y >= 4 && y < 28;
    });
    // An open line beside the square is a centered stroke of the body part, drawn with the aligned ring of the square.
    const Ops mixed = {{'M', {8.0, 8.0}}, {'L', {24.0, 8.0}}, {'L', {24.0, 24.0}}, {'L', {8.0, 24.0}}, {'Z', {}},
        {'M', {2.0, 4.0}}, {'L', {2.0, 28.0}}};
    judge(band(GSHGEOM_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO, mixed, 4.0), "mixed", [&](uint32_t x, uint32_t y) {
      return (in_square(x, y) && (x < 12 || x >= 20 || y < 12 || y >= 20)) || (x < 4 && y >= 4 && y < 28);
    });
    // Two nested squares wound the same way: nonzero fills the inner one, even-odd leaves it a hole.
    const Ops nested = {{'M', {4.0, 4.0}}, {'L', {28.0, 4.0}}, {'L', {28.0, 28.0}}, {'L', {4.0, 28.0}}, {'Z', {}},
        {'M', {10.0, 10.0}}, {'L', {22.0, 10.0}}, {'L', {22.0, 22.0}}, {'L', {10.0, 22.0}}, {'Z', {}}};
    std::vector<uint8_t> nonzero = band(GSHGEOM_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_NONZERO, nested, 2.0);
    std::vector<uint8_t> evenodd = band(GSHGEOM_STROKE_ALIGN_INSIDE, GSHGEOM_FILL_EVEN_ODD, nested, 2.0);
    EXPECT_EQ(at(nonzero, 11, 16), 255u) << "the pixel just inside the inner square: inside the nonzero fill";
    EXPECT_EQ(at(evenodd, 11, 16), 0u) << "the same pixel is in the even-odd hole";
    EXPECT_EQ(at(nonzero, 9, 16), 255u);
    EXPECT_EQ(at(evenodd, 9, 16), 255u) << "and the pixel just outside the inner square is in both";
    EXPECT_EQ(at(nonzero, 5, 16), 255u);
    EXPECT_EQ(at(evenodd, 5, 16), 255u);
    EXPECT_EQ(at(nonzero, 16, 16), 0u);

  }
}

TEST(RenderStroke, ACenteredStrokeIsAFillOfItsBodyWhereNothingOverlapsAndTheUnionWhereItDoes) {
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE);
  const GSHGEOM_Map map = affine_map(1.2, 0.3, -0.2, 0.9, 2.375, 3.625);
  Store s;
  Sink sink;
  // With the exact step off (the stopgap build of F4, the control of the gates) the coverage is the saturating sum of the
  // pieces' areas: the body's fill, bit for bit.  With it on, the pieces' union (UC1): never more than the sum by more
  // than the rounding of a cut (an edge that goes up is cut from its upper end), and less where pieces overlap.
  Sink stopgap(GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_EXACT);
  Drawn stroked = render_list(stopgap.sink, one_stroke(s, kOpen, p.get(), map), 32, 32);
  Drawn unioned = render_list(sink.sink, one_stroke(s, kOpen, p.get(), map), 32, 32);
  // The body, expanded as the sink does (the canonical map, moved by the whole pixels) and filled as a path.
  GSHGEOM_Path * path = stroke_gate::build(kOpen);
  GSHGEOM_Map canon;
  int64_t sx = 0;
  int64_t sy = 0;
  ASSERT_EQ(gshgeom_stroke_canonical_map(&map, &canon, &sx, &sy), GSHGEOM_OK);
  GSHGEOM_Stroke_Outline * o = nullptr;
  ASSERT_EQ(gshgeom_stroke_expand(path, &p.get(), &canon, nullptr, nullptr, &o), GSHGEOM_OK);
  const GSHGEOM_Flat_Path * body = gshgeom_stroke_outline_body(o);
  ASSERT_NE(body, nullptr);
  GSHGEOM_Path * fillable = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &fillable), GSHGEOM_OK);
  const GSHGEOM_Device_Point * pts = gshgeom_flat_path_points(body);
  for (size_t sub = 0; sub < gshgeom_flat_path_subpath_count(body); sub++) {
    GSHGEOM_Flat_Subpath fs;
    ASSERT_EQ(gshgeom_flat_path_subpath(body, sub, &fs), GSHGEOM_OK);
    for (size_t k = 0; k < fs.count; k++) {
      GSHGEOM_Point q = {(double)pts[fs.first + k].x / 64.0 + (double)sx, (double)pts[fs.first + k].y / 64.0 + (double)sy, 0.0};
      k == 0 ? gshgeom_path_move_to(fillable, q) : gshgeom_path_line_to(fillable, q);
    }
    gshgeom_path_close(fillable);
  }
  Store s2;
  const GSHSCENE_Resource * res = s2.path(fillable);
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  GSHSCENE_List * l = s2.open();
  ASSERT_EQ(gshscene_list_fill(l, res, nullptr, GSHGEOM_FILL_NONZERO, &white, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Drawn filled = render_list(sink.sink, l, 32, 32);
  EXPECT_EQ(stroked.alpha, filled.alpha) << "k = b, bit for bit what a fill of the body gives, without the exact step";
  EXPECT_EQ(stroked.hash, filled.hash);
  size_t less = 0;
  for (size_t i = 0; i < filled.alpha.size(); i++) {
    EXPECT_LE(unioned.alpha[i], filled.alpha[i] + 1) << "the union is never more than the sum, pixel " << i;
    less += unioned.alpha[i] + 1 < filled.alpha[i];
  }
  EXPECT_GT(less, 0u) << "where the body's pieces overlap the union is less than the sum";
  gshgeom_stroke_outline_destroy(o);
  gshgeom_path_destroy(path);
}

/* --- SC6: limits ---------------------------------------------------------------------------------------------------- */

TEST(RenderStrokeLimits, APartPastMaxPointsIsTheSinksResourceLimit) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_points = 8;
  Store s;
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  const GSHSCENE_List * l = one_stroke(s, kOpen, p.get(), frac_map());
  Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
  Drawn d = render_list(sink.sink, l, 24, 24);
  EXPECT_EQ(d.result, GSHSCENE_OK);
  ASSERT_EQ(d.entries.size(), 1u);
  EXPECT_EQ(d.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  for (uint8_t a : d.alpha) {
    EXPECT_EQ(a, 0);
  }
  Drawn strict = render_list(sink.sink, l, 24, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
}

TEST(RenderStrokeLimits, CellsPastMaxCellsAreTheSinksLimitWholeOrDamaged) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_deposits = 6;
  Store s;
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  const GSHSCENE_List * l = one_stroke(s, kClosed, p.get(), frac_map());
  Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
  Drawn whole = render_list(sink.sink, l, 24, 24);
  ASSERT_EQ(whole.entries.size(), 1u);
  EXPECT_EQ(whole.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  // A damage over a part of it refuses what the whole render refuses (S4), however few cells the damage holds.
  GSHSCENE_Rect damage = {10, 10, 14, 14};
  Drawn part = render_list(sink.sink, l, 24, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, &damage);
  ASSERT_EQ(part.entries.size(), 1u);
  EXPECT_EQ(part.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  // The cap met exactly by the sum over the parts: with it raised by the parts' total it passes, one less it does not.
  GSHCPU_Limits roomy = limits;
  roomy.max_deposits = GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS;
  Sink big(GSHCPU_RENDER_TIERS_ALL, &roomy);
  Drawn ok = render_list(big.sink, l, 24, 24);
  EXPECT_TRUE(ok.entries.empty());
  uint64_t used = ok.summary.work_max; // an upper bound of the cells
  (void)used;
  size_t lo = 1;
  size_t hi = GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS;
  while (lo < hi) { // the least cap that draws: the sum over the three parts, no more and no less
    size_t mid = lo + (hi - lo) / 2;
    GSHCPU_Limits probe = limits;
    probe.max_deposits = mid;
    Sink s2(GSHCPU_RENDER_TIERS_ALL, &probe);
    Drawn d = render_list(s2.sink, l, 24, 24);
    (d.entries.empty() ? hi : lo) = d.entries.empty() ? mid : mid + 1;
  }
  GSHCPU_Limits at = limits;
  at.max_deposits = lo;
  Sink exact(GSHCPU_RENDER_TIERS_ALL, &at);
  Drawn at_cap = render_list(exact.sink, l, 24, 24);
  EXPECT_TRUE(at_cap.entries.empty());
  EXPECT_EQ(at_cap.alpha, ok.alpha);
  at.max_deposits = lo - 1;
  Sink under(GSHCPU_RENDER_TIERS_ALL, &at);
  EXPECT_FALSE(render_list(under.sink, l, 24, 24).entries.empty());
  EXPECT_GT(lo, 3u) << "the cells of three parts, not of one";
}

TEST(RenderStrokeLimits, ADamageRefusesWhatTheWholeRenderRefusesWhereverTheStrokeIs) {
  // The same stroke at the origin and moved by whole pixels: a damage over a corner of it must still be refused when the whole
  // render is, because the cap is decided over the cells of the whole stroke and not over the cells the damage holds.  The cap
  // is the least that draws the whole stroke, less one, so that the damage's own cells are far below it.  The decision reads
  // the stroke's outline, which is in the canonical frame, and so must read the rectangle in that frame, which is the device's
  // moved back by the whole pixels the stroke was shifted: it once read it unmoved, found no cell of the outline in it for a
  // shifted stroke, and let the damage count only its own cells.
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  auto placed = [](double shift) { return affine_map(6.0, 0.0, 0.0, 6.0, shift + 0.375, shift / 2 + 0.625); };
  // the cap is the least that draws the whole stroke where it is: a half-pixel phase (an odd shift) puts the pieces on other
  // cells, and so it needs a few deposits fewer
  auto least = [&](double shift) {
    size_t lo = 1;
    size_t hi = GSHCPU_LIMITS_DEFAULT_MAX_DEPOSITS;
    Store s;
    const GSHSCENE_List * l = one_stroke(s, kClosed, p.get(), placed(shift));
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      GSHCPU_Limits probe;
      gshcpu_limits_default(&probe);
      probe.max_deposits = mid;
      Sink sink(GSHCPU_RENDER_TIERS_ALL, &probe);
      Drawn d = render_list(sink.sink, l, 600, 600);
      (d.entries.empty() ? hi : lo) = d.entries.empty() ? mid : mid + 1;
    }
    return lo;
  };
  ASSERT_GT(least(0.0), 200u);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  for (double shift : {0.0, 7.0, 40.0, 130.0}) {
    limits.max_deposits = least(shift) - 1;
    Store s;
    const GSHSCENE_List * l = one_stroke(s, kClosed, p.get(), placed(shift));
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
    Drawn whole = render_list(sink.sink, l, 600, 600);
    ASSERT_EQ(whole.entries.size(), 1u) << shift;
    EXPECT_EQ(whole.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
    int64_t x = (int64_t)shift + 60;
    int64_t y = (int64_t)(shift / 2) + 70;
    GSHSCENE_Rect damage = {x, y, x + 3, y + 3};
    Drawn part = render_list(sink.sink, l, 600, 600, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, &damage);
    ASSERT_EQ(part.entries.size(), 1u) << "shift " << shift;
    EXPECT_EQ(part.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT) << "shift " << shift;
  }
}

TEST(RenderStrokeLimits, TheBoundOfADrawnStrokeReadFromTheCacheIsTheOneMeasuredAgain) {
  // The bound of the cells of a part over its own box is made once, when the outline is; a render whose walk is that box reads it.
  // With the check on, every such read is compared with a bound measured afresh, whatever the shift, the damage and the cap.
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  gshcpu_exact_check = 1;
  unsigned long cached = gshcpu_bound_cached;
  unsigned long wrong = gshcpu_bound_mismatches;
  for (double shift : {0.0, 7.0, 130.0}) {
    for (uint64_t cap : {(uint64_t)40, (uint64_t)3000, (uint64_t)1 << 20}) {
      for (int aligned = 0; aligned < 2; aligned++) {
        Params q = p;
        q.s.alignment = aligned ? GSHGEOM_STROKE_ALIGN_INSIDE : GSHGEOM_STROKE_ALIGN_CENTER;
        GSHCPU_Limits limits;
        gshcpu_limits_default(&limits);
        limits.max_deposits = cap;
        Store s;
        const GSHSCENE_List * l = one_stroke(s, kClosed, q.get(), translation(shift + 0.375, shift / 2 + 0.625));
        Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
        render_list(sink.sink, l, 200, 200);
        render_list(sink.sink, l, 200, 200); // a hit
        GSHSCENE_Rect damage = {(int64_t)shift + 8, (int64_t)(shift / 2) + 8, (int64_t)shift + 16, (int64_t)(shift / 2) + 16};
        render_list(sink.sink, l, 200, 200, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, &damage);
      }
    }
  }
  gshcpu_exact_check = 0;
  EXPECT_GT(gshcpu_bound_cached, cached) << "the cached bound is read";
  EXPECT_EQ(gshcpu_bound_mismatches, wrong) << "and is the one measured again";
}

TEST(RenderStrokeLimits, AShiftedCoordinatePastTheRangeAndAnUnformableMapAreTheSinksLimit) {
  Store s;
  Params p = params();
  Sink sink;
  // 2^25 pixels away: past 2^24.
  Drawn far = render_list(sink.sink, one_stroke(s, kClosed, p.get(), translation(33554432.0 + 0.5, 0.25)), 24, 24);
  ASSERT_EQ(far.entries.size(), 1u);
  EXPECT_EQ(far.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  // and in each of the other three directions: the box of a part is checked on all four sides
  for (const GSHGEOM_Map & away : {translation(0.5, 33554432.0 + 0.25), translation(-33554432.0 - 0.5, 0.25),
           translation(0.5, -33554432.0 - 0.25)}) {
    Store sa;
    Drawn d = render_list(sink.sink, one_stroke(sa, kClosed, p.get(), away), 24, 24);
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  }
  // A composed map past the map bound: the product of two maps each at the bound.
  const GSHGEOM_Map huge = affine_map(0x1p99, 0, 0, 0x1p99, 0, 0);
  Store s2;
  Drawn big = render_list(sink.sink, one_stroke(s2, kClosed, p.get(), huge), 24, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, &huge);
  ASSERT_EQ(big.entries.size(), 1u) << "not a notice: the product cannot be formed";
  EXPECT_EQ(big.entries[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(big.entries[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
}

/* --- SC7: the cache --------------------------------------------------------------------------------------------------- */

TEST(RenderStrokeCache, AHitIsTheMissBitForBitAndTheMemoryHeldIsReportedAndBounded) {
  Store s;
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  p.s.alignment = GSHGEOM_STROKE_ALIGN_OUTSIDE;
  const GSHSCENE_List * l = one_stroke(s, kMixed, p.get(), frac_map());
  Sink sink;
  Drawn miss = render_list(sink.sink, l, 28, 28);
  EXPECT_EQ(sink.counters.cache_misses, 1u);
  EXPECT_EQ(sink.counters.cache_kept, 1u);
  uint64_t held = 0;
  ASSERT_EQ(gshscene_sink_static_bytes(sink.sink, &held), GSHSCENE_OK);
  EXPECT_GT(held, 0u);
  Drawn hit = render_list(sink.sink, l, 28, 28);
  EXPECT_EQ(sink.counters.cache_hits, 1u);
  EXPECT_EQ(hit.alpha, miss.alpha);
  EXPECT_EQ(hit.hash, miss.hash);
  EXPECT_EQ(hit.summary.work_max, miss.summary.work_max) << "the report does not depend on the cache";
  uint64_t after = 0;
  ASSERT_EQ(gshscene_sink_static_bytes(sink.sink, &after), GSHSCENE_OK);
  EXPECT_EQ(after, held) << "a hit stores nothing";
  // The summary states what the sink holds across frames.
  EXPECT_EQ(hit.summary.static_bytes, held);
  // The same with the cache off: the same pixels.
  Sink off(GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_CACHE);
  Drawn uncached = render_list(off.sink, l, 28, 28);
  EXPECT_EQ(uncached.alpha, miss.alpha);
  EXPECT_EQ(off.counters.cache_misses + off.counters.cache_hits, 0u);
}

TEST(RenderStrokeCache, EvictByIdAndDestructionFree) {
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    Store s;
    Params p = params();
    Params q = params(2.0);
    const GSHSCENE_Resource * path = s.path(stroke_gate::build(kClosed));
    const GSHSCENE_Resource * path2 = s.path(stroke_gate::build(kOpen));
    const GSHSCENE_Resource * a = intern_style(s, p.get());
    const GSHSCENE_Resource * b = intern_style(s, q.get());
    GSHSCENE_Paint white = solid(1, 1, 1, 1);
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_stroke(l, path, a, nullptr, &white, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_stroke(l, path2, a, nullptr, &white, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_stroke(l, path, b, nullptr, &white, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Sink sink(GSHCPU_RENDER_TIERS_ALL, nullptr, &alloc);
    render_list(sink.sink, l, 28, 28);
    uint64_t held = 0;
    gshscene_sink_static_bytes(sink.sink, &held);
    EXPECT_EQ(sink.counters.cache_kept, 3u);
    // Evicting the first path drops the two entries that name it, and no more.
    ASSERT_EQ(gshscene_sink_evict(sink.sink, gshscene_resource_id(path)), GSHSCENE_OK);
    EXPECT_EQ(sink.counters.cache_evictions, 2u);
    uint64_t some = 0;
    gshscene_sink_static_bytes(sink.sink, &some);
    EXPECT_GT(some, 0u);
    EXPECT_LT(some, held);
    // Evicting the style the survivor names drops it; an id nothing names is fine.
    ASSERT_EQ(gshscene_sink_evict(sink.sink, gshscene_resource_id(a)), GSHSCENE_OK);
    ASSERT_EQ(gshscene_sink_evict(sink.sink, 987654321u), GSHSCENE_OK);
    uint64_t none = 1;
    gshscene_sink_static_bytes(sink.sink, &none);
    EXPECT_EQ(none, 0u);
    // A render after an eviction misses again and the pixels are the same.
    Drawn again = render_list(sink.sink, l, 28, 28);
    EXPECT_EQ(sink.counters.cache_kept, 6u);
    EXPECT_EQ(again.result, GSHSCENE_OK);
    // Destruction (the Sink's) frees what is still held.
  }
  EXPECT_TRUE(t.live.empty()) << "every byte the sink took, the cache's included, is given back";
  EXPECT_FALSE(t.failed);
}

TEST(RenderStrokeCache, TheBoundHoldsLeastRecentlyUsedFirstAndAnEntryLargerThanItIsUsedButNotKept) {
  Params p = params(2.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  Store s;
  const GSHSCENE_Resource * style = intern_style(s, p.get());
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  // Twelve distinct strokes (a fraction each): each is one entry of the same size.
  std::vector<GSHSCENE_List *> lists;
  const GSHSCENE_Resource * path = s.path(stroke_gate::build(kClosed));
  for (int i = 0; i < 12; i++) {
    GSHGEOM_Map m = translation(1.0 + i * 0.0625, 1.5);
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_stroke(l, path, style, &m, &white, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    lists.push_back(l);
  }
  Sink unbounded;
  render_list(unbounded.sink, lists[0], 28, 28);
  uint64_t one = 0;
  gshscene_sink_static_bytes(unbounded.sink, &one);
  ASSERT_GT(one, 0u);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_cache_bytes = one * 3 + one / 2; // room for three
  Sink bounded(GSHCPU_RENDER_TIERS_ALL, &limits);
  for (GSHSCENE_List * l : lists) {
    render_list(bounded.sink, l, 28, 28);
    uint64_t held = 0;
    gshscene_sink_static_bytes(bounded.sink, &held);
    EXPECT_LE(held, limits.max_cache_bytes);
  }
  EXPECT_GE(bounded.counters.cache_evictions, 8u) << "twelve entries through room for three or four";
  EXPECT_LE(bounded.counters.cache_evictions, 9u);
  // The most recent three are the ones kept: rendering them again hits, the oldest misses.
  for (size_t i = 9; i < 12; i++) {
    render_list(bounded.sink, lists[i], 28, 28);
  }
  EXPECT_EQ(bounded.counters.cache_hits, 3u);
  render_list(bounded.sink, lists[0], 28, 28);
  EXPECT_EQ(bounded.counters.cache_hits, 3u);
  // A bound of 1 keeps nothing; the stroke is drawn all the same, and the pixels are the same.
  limits.max_cache_bytes = GSHCPU_LIMITS_CACHE_OFF;
  Sink nothing(GSHCPU_RENDER_TIERS_ALL, &limits);
  Drawn a = render_list(nothing.sink, lists[5], 28, 28);
  Drawn b = render_list(unbounded.sink, lists[5], 28, 28);
  EXPECT_EQ(a.alpha, b.alpha);
  uint64_t zero = 7;
  gshscene_sink_static_bytes(nothing.sink, &zero);
  EXPECT_EQ(zero, 0u);
  EXPECT_EQ(nothing.counters.cache_kept, 0u);
  // An entry larger than the bound is used for its render and not kept, and nothing is evicted to make room for it.
  limits.max_cache_bytes = one / 2;
  Sink small(GSHCPU_RENDER_TIERS_ALL, &limits);
  EXPECT_EQ(render_list(small.sink, lists[1], 28, 28).alpha, render_list(unbounded.sink, lists[1], 28, 28).alpha);
  EXPECT_EQ(small.counters.cache_kept, 0u);
  EXPECT_EQ(small.counters.cache_evictions, 0u);
}

TEST(RenderStrokeCache, CallerMemoryNeitherReadsNorWritesTheCacheAndAllocatesNothing) {
  Store s;
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_SQUARE);
  p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  const GSHSCENE_List * l = one_stroke(s, kMixed, p.get(), frac_map());
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  Sink sink(GSHCPU_RENDER_TIERS_ALL, nullptr, &alloc);
  Drawn plain = render_list(sink.sink, l, 28, 28);
  ASSERT_EQ(plain.result, GSHSCENE_OK);
  EXPECT_EQ(sink.counters.cache_kept, 1u);
  // The exact workspace, and the render in it: the same pixels, no allocator call, no cache read.
  RasterRef probe(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 28, 28));
  GSHSCENE_Target target = target_of(probe.raster);
  ReportRef report;
  GSHSCENE_Render_Request req = request_of(l, &target, report.report);
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  std::vector<unsigned char> memory(need + 16);
  const long calls = t.calls;
  const uint64_t hits = sink.counters.cache_hits;
  Drawn caller = render_list(sink.sink, l, 28, 28, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr,
      nullptr, memory.data(), need);
  ASSERT_EQ(caller.result, GSHSCENE_OK);
  EXPECT_EQ(t.calls, calls) << "no allocation by the sink during a caller-memory render (S9)";
  EXPECT_EQ(sink.counters.cache_hits, hits) << "the cache is not read";
  EXPECT_EQ(caller.alpha, plain.alpha);
  // One byte less is a limit, with nothing allocated.
  Drawn short_one = render_list(sink.sink, l, 28, 28, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr,
      nullptr, memory.data(), need - 1);
  EXPECT_EQ(short_one.result, GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(t.calls, calls);
  // A caller-memory render alone writes nothing into the cache.
  Sink fresh(GSHCPU_RENDER_TIERS_ALL);
  render_list(fresh.sink, l, 28, 28, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_CALLER_MEMORY, nullptr, nullptr,
      memory.data(), need);
  EXPECT_EQ(fresh.counters.cache_misses + fresh.counters.cache_kept, 0u);
}

TEST(RenderStrokeCache, RendersOnOtherThreadsAndEvictionsMayOverlap) {
  Store s;
  Params p = params(2.5, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  const GSHSCENE_Resource * path = s.path(stroke_gate::build(kMixed));
  const GSHSCENE_Resource * style = intern_style(s, p.get());
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  GSHSCENE_List * l = s.open();
  for (int i = 0; i < 4; i++) {
    GSHGEOM_Map m = translation(0.25 * i, 0.5);
    ASSERT_EQ(gshscene_list_stroke(l, path, style, &m, &white, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Sink sink;
  Drawn want = render_list(sink.sink, l, 28, 28);
  std::atomic<int> bad{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; t++) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 25; i++) {
        Drawn d = render_list(sink.sink, l, 28, 28);
        if (d.hash != want.hash) {
          bad++;
        }
        if (t == 0 && i % 5 == 0) {
          gshscene_sink_evict(sink.sink, gshscene_resource_id(path));
        }
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  EXPECT_EQ(bad.load(), 0) << "a render pinned its entries: the eviction cannot change a picture";
}

/* --- SC8: the tiers --------------------------------------------------------------------------------------------------- */

TEST(RenderStrokeTiers, EachTierIsTakenAndBitEqualToTheGeneralTier) {
  // A long thin stroke is where the sparse composite is taken: its rectangle is large against its few deposits (an
  // axis-parallel line deposits at its ends only; a long slanted one, a few cells in each of its many rows).
  const Ops flat = {{'M', {-40.0, 5.5}}, {'L', {100.0, 5.5}}};
  const Ops slant = {{'M', {-50.0, -50.0}}, {'L', {150.0, 150.0}}};
  struct Case {
    const char * what;
    const Ops * ops;
    Params p;
    GSHGEOM_Map map;
    bool hairline;
  };
  Params hair = params(1.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_BUTT);
  hair.s.width_space = GSHGEOM_STROKE_WIDTH_DEVICE;
  // (butt caps: a square or round cap is a piece of its own, with edges of its own, and the bound counts them all)
  Params plain = params(2.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_BUTT);
  Case cases[] = {{"hairline", &flat, hair, translation(0.0, 0.0), true}, {"hairline, slanted", &slant, hair, frac_map(), true},
      {"plain", &flat, plain, translation(0.0, 0.25), false}, {"plain, slanted", &slant, plain, frac_map(), false}};
  GSHCPU_Format formats[] = {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_FORMAT_RGBAF32_STRAIGHT,
      GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED};
  for (Case & c : cases) {
    for (GSHCPU_Format format : formats) {
      Store s;
      const GSHSCENE_List * l = one_stroke(s, *c.ops, c.p.get(), c.map);
      Sink tiered;
      Sink general(GSHCPU_RENDER_TIERS_GENERAL); // the general tier: the exact step is the coverage, not a tier
      Drawn general_draw = render_list(general.sink, l, 48, 24, format);
      Drawn first = render_list(tiered.sink, l, 48, 24, format);
      Drawn second = render_list(tiered.sink, l, 48, 24, format);
      EXPECT_EQ(first.hash, general_draw.hash) << c.what;
      EXPECT_EQ(second.hash, general_draw.hash) << c.what << ": from the cache";
      EXPECT_EQ(first.summary.work_max, general_draw.summary.work_max) << "the report does not depend on the tier";
      EXPECT_EQ(tiered.counters.cache_hits, 1u) << c.what;
      EXPECT_EQ(general.counters.sparse_hairline + general.counters.sparse_plain + general.counters.cache_hits, 0u);
      if (c.hairline) {
        EXPECT_EQ(tiered.counters.sparse_hairline, 2u) << c.what << ": taken";
        EXPECT_EQ(tiered.counters.sparse_plain, 0u);
      } else {
        EXPECT_EQ(tiered.counters.sparse_plain, 2u) << c.what << ": taken";
        EXPECT_EQ(tiered.counters.sparse_hairline, 0u);
      }
      bool any = false;
      for (uint8_t a : first.alpha) {
        any = any || a;
      }
      if (format == GSHCPU_FORMAT_RGBA8_STRAIGHT) {
        EXPECT_TRUE(any) << c.what << " draws";
      }
    }
  }
  // A ring, whose rectangle is mostly its own deposits, and a near-horizontal curve, whose every column deposits, take the
  // sparse path too (the sweep gives the pixels that can be nonzero, however many that is); a gradient or an aligned stroke
  // is never sparse.
  Store s;
  Params ring = params(2.0, GSHGEOM_STROKE_JOIN_ROUND);
  Sink tiered;
  render_list(tiered.sink, one_stroke(s, kLens, ring.get(), frac_map()), 24, 24);
  const Ops wire = {{'M', {-40.0, 12.0}}, {'Q', {30.0, 14.0, 100.0, 12.5}}};
  render_list(tiered.sink, one_stroke(s, wire, ring.get(), frac_map()), 48, 24);
  Params aligned = params(2.0, GSHGEOM_STROKE_JOIN_MITER);
  aligned.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  const Ops big_ring = {{'M', {-50.0, -50.0}}, {'L', {150.0, 150.0}}, {'L', {150.0, -50.0}}, {'Z', {}}};
  render_list(tiered.sink, one_stroke(s, big_ring, aligned.get(), translation(0.0, 0.0)), 48, 24);
  EXPECT_EQ(tiered.counters.sparse_plain + tiered.counters.sparse_hairline, 2u);
  EXPECT_EQ(tiered.counters.strokes, 3u);
}

namespace {

/** Fixture @p index drawn twice by @p sink onto the fixture backdrop of @p format; the hash of the second. */
uint64_t draw_fixture_twice(GSHSCENE_Sink * sink, size_t index, GSHCPU_Format format, uint64_t * first_hash = nullptr) {
  Fixture fx(index);
  uint64_t last = 0;
  for (int pass = 0; pass < 2; pass++) {
    RasterRef r(rf_raster(format, RF_WIDTH, RF_HEIGHT));
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster, &fx.ctx.target_map);
    GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
    EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK) << RF_FIXTURES[index].name;
    last = r.hash();
    if (pass == 0 && first_hash) {
      *first_hash = last;
    }
  }
  return last;
}

}  // namespace

TEST(RenderStrokeTiers, OnlyAWidthOfOneInDeviceUnitsIsAHairline) {
  struct Case {
    uint32_t space;
    double width;
    bool hairline;
  } cases[] = {{GSHGEOM_STROKE_WIDTH_DEVICE, 1.0, true}, {GSHGEOM_STROKE_WIDTH_LOCAL, 1.0, false},
      {GSHGEOM_STROKE_WIDTH_DEVICE, 1.25, false}, {GSHGEOM_STROKE_WIDTH_DEVICE, 0.5, false},
      {GSHGEOM_STROKE_WIDTH_LOCAL, 2.0, false}};
  for (const Case & c : cases) {
    Params p = params(c.width, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_BUTT);
    p.s.width_space = c.space;
    Store s;
    Sink sink;
    const Ops thin = {{'M', {-50.0, -50.0}}, {'L', {150.0, 150.0}}};   // long and thin: where the sparse composite is taken
    Drawn d = render_list(sink.sink, one_stroke(s, thin, p.get(), frac_map()), 48, 24);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_EQ(sink.counters.sparse_hairline, c.hairline ? 1u : 0u) << c.space << " " << c.width;
    EXPECT_EQ(sink.counters.sparse_plain, c.hairline ? 0u : 1u) << c.space << " " << c.width;
  }
}

TEST(RenderStrokeTiers, AStrokeIsSparseWhereverItsDepositsFitTheCapAndNeverWhenAligned) {
  // A diagonal across a small canvas deposits in about every row and column of its rectangle: far more than a sixteenth
  // of its pixels, though well under a half, and the sweep draws it as the dense one does.
  {
    Params p = params(2.0, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_BUTT);
    const Ops diagonal = {{'M', {2.0, 2.0}}, {'L', {22.0, 22.0}}};
    Store s;
    Sink tiered;
    Sink general(GSHCPU_RENDER_TIERS_GENERAL);
    const GSHSCENE_List * l = one_stroke(s, diagonal, p.get(), frac_map());
    EXPECT_EQ(render_list(tiered.sink, l, 24, 24).hash, render_list(general.sink, l, 24, 24).hash);
    EXPECT_EQ(tiered.counters.sparse_plain + tiered.counters.sparse_hairline, 1u) << "sparse: the tier takes it";
  }
  // A long thin triangle stroked inside, beside a long open line: the body alone would be sparse by its deposits, but a
  // stroke with an aligned part never is.
  {
    Params p = params(0.5, GSHGEOM_STROKE_JOIN_MITER, GSHGEOM_STROKE_CAP_BUTT);
    p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
    // (with a long open line beside it, so that the body part is there for the sparse tier to take)
    const Ops sliver = {{'M', {-50.0, -60.0}}, {'L', {150.0, 140.0}}, {'M', {-50.0, -50.0}}, {'L', {150.0, 150.0}},
        {'L', {150.0, 148.0}}, {'Z', {}}};
    Store s;
    Sink tiered;
    Sink general(GSHCPU_RENDER_TIERS_GENERAL);
    const GSHSCENE_List * l = one_stroke(s, sliver, p.get(), frac_map());
    EXPECT_EQ(render_list(tiered.sink, l, 48, 24).hash, render_list(general.sink, l, 48, 24).hash);
    EXPECT_EQ(tiered.counters.sparse_plain + tiered.counters.sparse_hairline, 0u);
  }
}

TEST(RenderStrokeTiers, EveryStrokeFixtureIsBitEqualToTheGeneralTierOnEveryFormatAndEachTierIsTakenOverThem) {
  GSHCPU_Format formats[] = {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_FORMAT_RGBAF32_STRAIGHT,
      GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED};
  auto want = goldens();
  GSHCPU_Render_Counters total{};
  size_t fixtures = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "strokes-", 8) != 0) {
      continue;
    }
    fixtures++;
    for (GSHCPU_Format format : formats) {
      Sink tiered;
      Sink general(GSHCPU_RENDER_TIERS_GENERAL); // the general tier: the exact step is the coverage, not a tier
      uint64_t first = 0;
      uint64_t second = draw_fixture_twice(tiered.sink, i, format, &first);
      uint64_t oracle = draw_fixture_twice(general.sink, i, format);
      auto it = want.find({RF_FIXTURES[i].name, gshcpu_format_string(format)});
      ASSERT_NE(it, want.end()) << RF_FIXTURES[i].name;
      EXPECT_EQ(first, it->second) << RF_FIXTURES[i].name << " " << gshcpu_format_string(format) << ": the golden";
      EXPECT_EQ(second, it->second) << RF_FIXTURES[i].name << ": the second drawing, from the cache";
      EXPECT_EQ(oracle, it->second) << RF_FIXTURES[i].name << ": the general tier";
      EXPECT_EQ(general.counters.cache_hits + general.counters.sparse_hairline + general.counters.sparse_plain, 0u);
      total.cache_hits += tiered.counters.cache_hits;
      total.cache_misses += tiered.counters.cache_misses;
      total.sparse_hairline += tiered.counters.sparse_hairline;
      total.sparse_plain += tiered.counters.sparse_plain;
      total.strokes += tiered.counters.strokes;
    }
  }
  EXPECT_EQ(fixtures, 14u) << "the eight of S4, S4b's four and S5's two";
  EXPECT_GT(total.cache_hits, 0u) << "FT-1 is taken";
  EXPECT_GT(total.sparse_hairline, 0u) << "FT-2 is taken for a solid hairline";
  EXPECT_GT(total.sparse_plain, 0u) << "FT-2 is taken for a plain solid stroke";
  std::printf("stroke tiers over the fixtures: %llu strokes drawn, %llu expansions made, %llu served from the cache, "
              "%llu hairlines and %llu plain strokes by the sparse composite\n",
      (unsigned long long)total.strokes, (unsigned long long)total.cache_misses, (unsigned long long)total.cache_hits,
      (unsigned long long)total.sparse_hairline, (unsigned long long)total.sparse_plain);
}

/* --- S4, S12: damage, groups, refused groups ----------------------------------------------------------------------------- */

TEST(RenderStroke, InsideADamageTheRenderIsTheWholeRenderAlignedOrNot) {
  Params plain = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  Params inside = params(3.0, GSHGEOM_STROKE_JOIN_MITER);
  inside.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  Params outside = inside;
  outside.s.alignment = GSHGEOM_STROKE_ALIGN_OUTSIDE;
  Params* all[3] = {&plain, &inside, &outside};
  for (Params * p : all) {
    Store s;
    const GSHSCENE_List * l = one_stroke(s, kMixed, p->get(), frac_map());
    Sink sink;
    Drawn whole = render_list(sink.sink, l, 28, 28);
    GSHSCENE_Rect damage = {5, 4, 17, 13};
    Drawn part = render_list(sink.sink, l, 28, 28, GSHCPU_FORMAT_RGBA8_STRAIGHT, 0, nullptr, &damage);
    int compared = 0;
    for (int y = 0; y < 28; y++) {
      for (int x = 0; x < 28; x++) {
        bool in = x >= damage.x0 && x < damage.x1 && y >= damage.y0 && y < damage.y1;
        if (in) {
          EXPECT_EQ(part.alpha[(size_t)y * 28 + x], whole.alpha[(size_t)y * 28 + x]) << x << "," << y;
          compared += whole.alpha[(size_t)y * 28 + x] != 0;
        } else {
          EXPECT_EQ(part.alpha[(size_t)y * 28 + x], 0) << "outside the damage nothing is touched";
        }
      }
    }
    EXPECT_GT(compared, 5);
  }
}

TEST(RenderStroke, AStrokeInAnIsolatedGroupCompositesSrcOverAndTheGroupsModeActsAtItsPop) {
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  Store s;
  const GSHSCENE_Resource * path = s.path(stroke_gate::build(kOpen));
  const GSHSCENE_Resource * style = intern_style(s, p.get());
  GSHSCENE_Paint orange = solid(1, 0.5, 0, 0.75);
  auto list_with = [&](GSHSCENE_Group g, bool grouped) {
    GSHSCENE_List * l = s.open();
    if (grouped) {
      EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_stroke(l, path, style, nullptr, &orange, 0, 0.0), GSHSCENE_OK);
    if (grouped) {
      EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  };
  Sink sink;
  GSHSCENE_Group plain = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  Drawn bare = render_list(sink.sink, list_with(plain, false), 24, 24);
  Drawn grouped = render_list(sink.sink, list_with(plain, true), 24, 24);
  EXPECT_EQ(grouped.alpha, bare.alpha) << "an isolated SRC_OVER group at opacity 1 changes nothing";
  GSHSCENE_Group faded = rf_group(0.5, GSHSCENE_COMPOSITE_SRC_OVER);
  Drawn half = render_list(sink.sink, list_with(faded, true), 24, 24);
  for (size_t i = 0; i < half.alpha.size(); i++) {
    EXPECT_NEAR(half.alpha[i], bare.alpha[i] / 2.0, 1.0) << i;
  }
  // A blend mode acts at the pop, on the stroke's layer: on a transparent backdrop the result is the layer itself.
  GSHSCENE_Group multiply = rf_group(1.0, GSHSCENE_COMPOSITE_MULTIPLY);
  Drawn blended = render_list(sink.sink, list_with(multiply, true), 24, 24);
  EXPECT_EQ(blended.alpha, bare.alpha);
  // A knockout, non-isolated or soft-masked group is refused by this sink as before, and the stroke inside is skipped.
  GSHSCENE_Group knock = rf_group(1.0, GSHSCENE_COMPOSITE_SRC_OVER);
  knock.knockout = 1;
  Drawn refused = render_list(sink.sink, list_with(knock, true), 24, 24);
  ASSERT_EQ(refused.entries.size(), 1u);
  EXPECT_EQ(refused.entries[0].reason, GSHSCENE_REASON_GROUP_KNOCKOUT);
  for (uint8_t a : refused.alpha) {
    EXPECT_EQ(a, 0);
  }
  Drawn strict = render_list(sink.sink, list_with(knock, true), 24, 24, GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHSCENE_RENDER_STRICT);
  EXPECT_EQ(strict.result, GSHSCENE_ERR_REFUSED);
}

TEST(RenderStroke, AStrokeOfAGlyphRunIsDrawnAsTheStrokeOfItsCenterline) {
  // Strokes story S5: this sink says STROKE_RUNS, so a stroke of a run is planned in and drawn (test_render_stroke_text.cpp
  // holds the rest); here the one-glyph run of a square draws its square's stroke.
  Store s;
  Params p = params();
  const GSHSCENE_Resource * style = intern_style(s, p.get());
  GSHSCENE_Glyph glyphs[1] = {{1, 0.0, 0.0, s.rect(2, 2, 12, 12)}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 1;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
  s.held.push_back(r);
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_stroke(l, r, style, nullptr, &white, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHSCENE_List * lp = s.open();
  ASSERT_EQ(gshscene_list_stroke(lp, s.rect(2, 2, 12, 12), style, nullptr, &white, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(lp), GSHSCENE_OK);
  Sink sink;
  Drawn d = render_list(sink.sink, l, 16, 16);
  EXPECT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.entries.size(), 0u);
  Drawn dp = render_list(sink.sink, lp, 16, 16);
  EXPECT_EQ(d.hash, dp.hash);
  bool any = false;
  for (uint8_t a : d.alpha) {
    any = any || a != 0;
  }
  EXPECT_TRUE(any);
}

/* --- S7, SC11: the work of a stroke is within the declared bound --------------------------------------------------------- */

TEST(RenderStroke, AnAlignedStrokesWorkIsWithinTheDeclaredBoundAndCountsItsExpansion) {
  Params p = params(3.0, GSHGEOM_STROKE_JOIN_ROUND, GSHGEOM_STROKE_CAP_ROUND);
  p.s.alignment = GSHGEOM_STROKE_ALIGN_INSIDE;
  Store s;
  const GSHSCENE_List * l = one_stroke(s, kMixed, p.get(), frac_map());
  Sink sink;
  RasterRef probe(transparent(GSHCPU_FORMAT_RGBA8_STRAIGHT, 28, 28));
  GSHSCENE_Target t = target_of(probe.raster);
  GSHSCENE_Sink_Caps caps{};
  ASSERT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_OK);
  Drawn d = render_list(sink.sink, l, 28, 28);
  EXPECT_LE(d.summary.work_max, caps.cancel_work_bound);
  EXPECT_GE(d.summary.work_max, 28u * 28u) << "at least the pixels of the paint pass";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
