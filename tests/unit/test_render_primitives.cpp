/**
 * @file
 *
 * A primitive tag never changes what is drawn (shape-scene's primitive.h, PR9): a tagged path and the untagged resource of the same geometry
 * render bit for bit alike, for every item kind that takes a path, every fill rule, a matrix of maps, and every stroke join, cap and dash.
 * The renderer reads no tag (tools/check-tag-ignored.sh greps its sources); this is the other half, that what it draws for a tagged path is
 * what it draws for the path.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "../stroke_helpers.h"

#include <ghoti.io/shape-scene/primitive.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sh;

namespace {

struct Subject {
  std::string name;
  GSHSCENE_Primitive prim;
};

GSHSCENE_Primitive rect(int fx, int fy) {
  GSHSCENE_Primitive p{};
  p.kind = GSHSCENE_PRIMITIVE_RECT;
  p.flip_x = fx;
  p.flip_y = fy;
  p.x = 6.3;
  p.y = 5.7;
  p.w = 21.4;
  p.h = 13.9;
  return p;
}

GSHSCENE_Primitive ellipse(int fx, int fy) {
  GSHSCENE_Primitive p{};
  p.kind = GSHSCENE_PRIMITIVE_ELLIPSE;
  p.flip_x = fx;
  p.flip_y = fy;
  p.cx = 22.6;
  p.cy = 19.1;
  p.rx = 14.3;
  p.ry = 9.7;
  return p;
}

std::vector<Subject> subjects() {
  std::vector<Subject> out;
  static const char * const names[4] = {"none", "flip_x", "flip_y", "both"};
  for (int f = 0; f < 4; f++) {
    out.push_back({std::string("rect ") + names[f], rect(f & 1, f >> 1)});
  }
  for (int f = 0; f < 4; f++) {
    out.push_back({std::string("ellipse ") + names[f], ellipse(f & 1, f >> 1)});
  }
  GSHSCENE_Primitive rr{};
  rr.kind = GSHSCENE_PRIMITIVE_ROUNDED_RECT;
  rr.x = 5.25;
  rr.y = 6.5;
  rr.w = 30.5;
  rr.h = 24.75;
  const GSHGEOM_Corner c[4] = {{6.5, 4.25}, {3.0, 3.0}, {8.0, 5.5}, {0.0, 0.0}};
  for (int i = 0; i < 4; i++) {
    rr.r[i] = c[i];
  }
  out.push_back({"rounded rect", rr});
  for (int f = 1; f < 4; f++) {
    GSHSCENE_Primitive flipped = rr;
    flipped.flip_x = f & 1;
    flipped.flip_y = f >> 1;
    out.push_back({f == 1 ? "rounded rect flip_x" : f == 2 ? "rounded rect flip_y" : "rounded rect both flips", flipped});
  }
  GSHSCENE_Primitive circle = ellipse(0, 0);
  circle.rx = 11.5;
  circle.ry = 11.5;
  out.push_back({"circle", circle});
  return out;
}

struct Look {
  const char * name;
  GSHGEOM_Map map;
};

std::vector<Look> looks() {
  std::vector<Look> out;
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  out.push_back({"identity", m});
  out.push_back({"fractional translation", translation(0.375, 0.625)});
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &m);
  out.push_back({"rotation", m});
  out.push_back({"skew", affine_map(1.0, 0.3, 0.2, 1.0, 1.5, -0.5)});
  out.push_back({"mirror", affine_map(-1.0, 0.0, 0.0, 1.0, 44.0, 0.0)});
  out.push_back({"stretch", affine_map(1.3, 0.0, 0.0, 0.7, 0.25, 1.0)});
  return out;
}

struct Pair {
  Store s;
  const GSHSCENE_Resource * tagged = nullptr;
  const GSHSCENE_Resource * plain = nullptr;
  explicit Pair(const GSHSCENE_Primitive & p) {
    EXPECT_EQ(gshscene_store_intern_primitive(s.store, &p, &tagged), GSHSCENE_OK);
    s.held.push_back(tagged);
    GSHGEOM_Path * copy = nullptr;
    EXPECT_EQ(gshgeom_path_copy(gshscene_resource_path(tagged), nullptr, nullptr, &copy), GSHGEOM_OK);
    plain = s.path(copy);
    EXPECT_NE(plain, tagged);
    GSHSCENE_Primitive back;
    EXPECT_EQ(gshscene_resource_primitive(plain, &back), GSHSCENE_OK);
    EXPECT_EQ(back.kind, GSHSCENE_PRIMITIVE_NONE);
  }
};

enum Kind { FILL_NONZERO, FILL_EVEN_ODD, CLIP, STROKE_MITER, STROKE_ROUND_DASHED, STROKE_SQUARE_DASHED, KIND_COUNT };
const char * const kind_names[KIND_COUNT] = {"fill nonzero", "fill even-odd", "clip", "stroke, miter, butt",
    "stroke, round, round caps, dashed", "stroke, bevel, square caps, dashed with an offset"};

const GSHSCENE_List * list_of(Pair & p, const GSHSCENE_Resource * shape, Kind kind, const GSHGEOM_Map & map) {
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  GSHSCENE_Paint blue = solid(0.2, 0.4, 1.0, 0.75);
  GSHSCENE_List * l = p.s.open();
  switch (kind) {
    case FILL_NONZERO:
    case FILL_EVEN_ODD:
      EXPECT_EQ(gshscene_list_fill(l, shape, &map, kind == FILL_NONZERO ? GSHGEOM_FILL_NONZERO : GSHGEOM_FILL_EVEN_ODD, &white, 0, 0.0),
          GSHSCENE_OK);
      break;
    case CLIP: {
      EXPECT_EQ(gshscene_list_clip_push(l, shape, &map, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_fill(l, p.s.rect(0, 0, 48, 48), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
      EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
      break;
    }
    default: {
      GSHGEOM_Stroke g;
      gshgeom_stroke_init(&g);
      static const double dashes[2] = {4.0, 2.5};
      g.width = kind == STROKE_MITER ? 2.5 : 3.0;
      if (kind == STROKE_MITER) {
        g.join = GSHGEOM_STROKE_JOIN_MITER;
        g.cap = GSHGEOM_STROKE_CAP_BUTT;
      } else {
        g.join = kind == STROKE_ROUND_DASHED ? GSHGEOM_STROKE_JOIN_ROUND : GSHGEOM_STROKE_JOIN_BEVEL;
        g.cap = kind == STROKE_ROUND_DASHED ? GSHGEOM_STROKE_CAP_ROUND : GSHGEOM_STROKE_CAP_SQUARE;
        g.dash_count = 2;
        g.dashes = dashes;
        g.dash_offset = kind == STROKE_SQUARE_DASHED ? 1.25 : 0.0;
      }
      const GSHSCENE_Resource * style = intern_style(p.s, g);
      EXPECT_EQ(gshscene_list_stroke(l, shape, style, &map, &white, 0, 0.0), GSHSCENE_OK);
      break;
    }
  }
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

}  // namespace

TEST(RenderPrimitives, ATaggedPathAndItsUntaggedCopyRenderBitForBit) {
  size_t renders = 0, drawn = 0;
  for (const Subject & subject : subjects()) {
    Pair p(subject.prim);
    // A sink caches by resource id, and every store counts ids from 1: one sink for one store, or a later store's strokes are an
    // earlier one's.
    Sink sink;
    for (const Look & look : looks()) {
      for (int k = 0; k < KIND_COUNT; k++) {
        const GSHSCENE_List * a = list_of(p, p.tagged, (Kind)k, look.map);
        const GSHSCENE_List * b = list_of(p, p.plain, (Kind)k, look.map);
        Drawn da = render_list(sink.sink, a, 48, 48);
        Drawn db = render_list(sink.sink, b, 48, 48);
        ASSERT_EQ(da.result, GSHSCENE_OK) << subject.name << ", " << look.name << ", " << kind_names[k];
        ASSERT_EQ(db.result, GSHSCENE_OK);
        EXPECT_EQ(da.hash, db.hash) << subject.name << ", " << look.name << ", " << kind_names[k];
        EXPECT_EQ(da.alpha, db.alpha) << subject.name << ", " << look.name << ", " << kind_names[k];
        EXPECT_EQ(da.entries.size(), db.entries.size());
        renders += 2;
        for (uint8_t v : da.alpha) {
          if (v) {
            drawn++;
            break;
          }
        }
      }
    }
  }
  EXPECT_EQ(renders, 13u * 6u * 6u * 2u);
  EXPECT_GE(drawn, 13u * 6u * 6u * 9u / 10u) << "the matrix draws something almost everywhere, so equality is not a blank against a blank";
}

TEST(RenderPrimitives, TheFlipsMoveADashedStrokeAndAFillIsTheRegionToWithinOneLevel) {
  // A tagged rect under each flip pair is the same region.  The pairs of equal flips turn clockwise and the others counter-clockwise, and
  // a fill is the region to within one level of coverage: the sink rounds a corner's cell by the way round (a finding, recorded in
  // shape-cpu's design notes: four pixels of the rectangle below differ by one level), so the pairs of one way round are bit for bit
  // equal and the two ways round are within a level.  A dash begins at the start corner and follows the turn, so each pair moves it.
  uint64_t clockwise = 0, counter = 0;
  std::vector<uint8_t> first_cw, first_ccw;
  std::vector<uint64_t> dashed;
  for (int f = 0; f < 4; f++) {
    Pair p(rect(f & 1, f >> 1));
    Sink sink;
    Drawn fill = render_list(sink.sink, list_of(p, p.tagged, FILL_NONZERO, translation(0.0, 0.0)), 48, 48);
    Drawn dash = render_list(sink.sink, list_of(p, p.tagged, STROKE_ROUND_DASHED, translation(0.0, 0.0)), 48, 48);
    ASSERT_EQ(fill.result, GSHSCENE_OK);
    const bool ccw = (f == 1 || f == 2);
    uint64_t & seen = ccw ? counter : clockwise;
    std::vector<uint8_t> & alpha = ccw ? first_ccw : first_cw;
    if (!seen) {
      seen = fill.hash;
      alpha = fill.alpha;
    }
    EXPECT_EQ(fill.hash, seen) << "flips " << f << ": the same way round is the same bits";
    dashed.push_back(dash.hash);
  }
  ASSERT_EQ(first_cw.size(), first_ccw.size());
  int most = 0;
  for (size_t i = 0; i < first_cw.size(); i++) {
    most = std::max(most, std::abs((int)first_cw[i] - (int)first_ccw[i]));
  }
  EXPECT_LE(most, 1) << "and the two ways round are within one level";
  for (int a = 0; a < 4; a++) {
    for (int b = a + 1; b < 4; b++) {
      EXPECT_NE(dashed[a], dashed[b]) << "flips " << a << " and " << b << ": the dashes begin at the start corner and turn the way the flips say";
    }
  }
}

TEST(RenderPrimitives, ADashBeginsWhereChromiumsDoesForTheCanvasSignCasesOfRectAndEllipse) {
  // Recorded from a headless Chrome 156 run (conventions only: where a path starts and which way it turns, never a pixel value):
  // Canvas strokes a 4 px line with one 40 px dash of a pattern whose gap is 1e5, on a 300 by 200
  // canvas, and the bounding box of the pixels with alpha above 128 is the first 40 px of the path.  rect(40, 30, 200, 120) with each
  // sign case, and ellipse(140, 100, 90, 60) from angle 0 and pi either way round.  The tolerance is one pixel (the rasterizers anti-alias
  // the dash's ends differently).
  struct Expect {
    const char * what;
    GSHSCENE_Primitive prim;
    int box[4];
  };
  GSHSCENE_Primitive r = rect(0, 0);
  r.x = 40;
  r.y = 30;
  r.w = 200;
  r.h = 120;
  GSHSCENE_Primitive e = ellipse(0, 0);
  e.cx = 140;
  e.cy = 100;
  e.rx = 90;
  e.ry = 60;
  auto with = [](GSHSCENE_Primitive p, int fx, int fy) {
    p.flip_x = fx;
    p.flip_y = fy;
    return p;
  };
  const Expect cases[] = {
      {"rect(x, y, w, h)", with(r, 0, 0), {40, 28, 79, 31}},
      {"rect(x + w, y, -w, h)", with(r, 1, 0), {200, 28, 239, 31}},
      {"rect(x, y + h, w, -h)", with(r, 0, 1), {40, 148, 79, 151}},
      {"rect(x + w, y + h, -w, -h)", with(r, 1, 1), {200, 148, 239, 151}},
      {"ellipse from 0 clockwise", with(e, 0, 0), {212, 100, 231, 135}},
      {"ellipse from 0 counter-clockwise", with(e, 0, 1), {212, 64, 231, 99}},
      {"ellipse from pi clockwise", with(e, 1, 1), {48, 64, 67, 99}},
      {"ellipse from pi counter-clockwise", with(e, 1, 0), {48, 100, 67, 135}},
  };
  for (const Expect & c : cases) {
    Pair p(c.prim);
    Sink sink;
    GSHGEOM_Stroke g;
    gshgeom_stroke_init(&g);
    static const double dashes[2] = {40.0, 1e5};
    g.width = 4.0;
    g.join = GSHGEOM_STROKE_JOIN_MITER;
    g.cap = GSHGEOM_STROKE_CAP_BUTT;
    g.dash_count = 2;
    g.dashes = dashes;
    const GSHSCENE_Resource * style = intern_style(p.s, g);
    GSHSCENE_Paint white = solid(1, 1, 1, 1);
    GSHSCENE_List * l = p.s.open();
    ASSERT_EQ(gshscene_list_stroke(l, p.tagged, style, nullptr, &white, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Drawn d = render_list(sink.sink, l, 300, 200);
    ASSERT_EQ(d.result, GSHSCENE_OK) << c.what;
    int x0 = 1000, y0 = 1000, x1 = -1, y1 = -1;
    for (int y = 0; y < 200; y++) {
      for (int x = 0; x < 300; x++) {
        if (d.alpha[(size_t)y * 300 + (size_t)x] > 128) {
          x0 = std::min(x0, x);
          y0 = std::min(y0, y);
          x1 = std::max(x1, x);
          y1 = std::max(y1, y);
        }
      }
    }
    EXPECT_NEAR(x0, c.box[0], 1) << c.what;
    EXPECT_NEAR(y0, c.box[1], 1) << c.what;
    EXPECT_NEAR(x1, c.box[2], 1) << c.what;
    EXPECT_NEAR(y1, c.box[3], 1) << c.what;
  }
}

TEST(RenderPrimitives, ARoundedRectDashBeginsWhereChromiumsRoundRectDoesForEachSignCase) {
  // Canvas roundRect(x, y, w, h, [10, 20, 30, 40]) with the box (40, 30, 200, 120) and each sign case, a 2 px line with one 4 px dash and
  // a gap of 1e5 at lineDashOffset 0, -10 and -50 (a headless Chrome 156 run; the first dash is 4 px of the path from 0, 10 and 50 px
  // along it): the bounding box of the pixels above alpha 128.  The radii go to the mirrored corners and, for one negative size, the path
  // starts after the top-left radius of the box and runs counter-clockwise.  Firefox draws one negative size from the other end of the top
  // edge and is not what this follows.  The tolerance is one pixel.
  struct Row {
    int fx, fy;
    int box[3][4];
  };
  const Row rows[] = {
      {0, 0, {{50, 29, 53, 30}, {60, 29, 63, 30}, {100, 29, 103, 30}}},
      {1, 0, {{56, 29, 59, 30}, {47, 32, 49, 34}, {39, 69, 40, 72}}},
      {0, 1, {{76, 29, 79, 30}, {66, 30, 69, 32}, {40, 57, 42, 60}}},
      {1, 1, {{70, 29, 73, 30}, {80, 29, 83, 30}, {120, 29, 123, 30}}},
  };
  const double offsets[3] = {0.0, -10.0, -50.0};
  for (const Row & row : rows) {
    GSHSCENE_Primitive rr{};
    rr.kind = GSHSCENE_PRIMITIVE_ROUNDED_RECT;
    rr.flip_x = row.fx;
    rr.flip_y = row.fy;
    rr.x = 40;
    rr.y = 30;
    rr.w = 200;
    rr.h = 120;
    const double radii[4] = {10, 20, 30, 40};
    for (int i = 0; i < 4; i++) {
      rr.r[i].rx = radii[i];
      rr.r[i].ry = radii[i];
    }
    for (int k = 0; k < 3; k++) {
      Pair p(rr);
      Sink sink;
      GSHGEOM_Stroke g;
      gshgeom_stroke_init(&g);
      static const double dashes[2] = {4.0, 1e5};
      g.width = 2.0;
      g.join = GSHGEOM_STROKE_JOIN_MITER;
      g.cap = GSHGEOM_STROKE_CAP_BUTT;
      g.dash_count = 2;
      g.dashes = dashes;
      g.dash_offset = offsets[k];
      const GSHSCENE_Resource * style = intern_style(p.s, g);
      GSHSCENE_Paint white = solid(1, 1, 1, 1);
      GSHSCENE_List * l = p.s.open();
      ASSERT_EQ(gshscene_list_stroke(l, p.tagged, style, nullptr, &white, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      Drawn d = render_list(sink.sink, l, 300, 200);
      ASSERT_EQ(d.result, GSHSCENE_OK);
      int x0 = 1000, y0 = 1000, x1 = -1, y1 = -1;
      for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 300; x++) {
          if (d.alpha[(size_t)y * 300 + (size_t)x] > 128) {
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
          }
        }
      }
      EXPECT_NEAR(x0, row.box[k][0], 1) << row.fx << row.fy << " offset " << offsets[k];
      EXPECT_NEAR(y0, row.box[k][1], 1) << row.fx << row.fy << " offset " << offsets[k];
      EXPECT_NEAR(x1, row.box[k][2], 1) << row.fx << row.fy << " offset " << offsets[k];
      EXPECT_NEAR(y1, row.box[k][3], 1) << row.fx << row.fy << " offset " << offsets[k];
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
