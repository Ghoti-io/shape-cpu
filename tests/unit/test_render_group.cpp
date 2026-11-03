/*
 * Ghoti.io Shape CPU
 * Copyright 2026 by Corey Pennycuff
 */

/**
 * @file
 *
 * Non-isolated and knockout groups in the render (masks-images-patterns story M3; render.h, KC1 to KC11).
 *
 * - KC1, KC4 to KC6: the compositing primitives (the element step, the group's result) meet shape-scene's installed
 *   `group_vectors.h` (P3's, the four combinations of isolated and knockout), and renders meet them where the coverage is whole; the
 *   renders of `tools/gen-general-vectors.py` (a third restatement, in exact rationals, over spaces, nesting, clips and operators) meet
 *   its known answers, bit for bit where marked exact.
 * - KC3, KC8: which layers carry a group alpha and a group shape, and the memory that is exact (a render from the caller's memory
 *   allocates nothing, gives the same pixels, and one byte less is a limit).
 * - KC9: the plain path is not the general one, and the two agree: every fixture of the corpus rendered with every layer
 *   through the element step equals the normal render, and a list without these groups takes none of the general step.
 * - KC10, KC11: a masked general group, a stroke, a clipped element and a damage; the conflation of partial coverage is pinned.
 */

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/group_vectors.h>

#include "../../src/render/render_internal.h"
#include "../general_vectors.h"
#include "../render_fixtures.h"
#include "../mask_helpers.h"
#include "../vector_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

/* --- the primitives, through shape-scene's installed vectors ---------------------------------------------- */

struct Layer {
  double l[4] = {0, 0, 0, 0};
  double l0[4] = {0, 0, 0, 0};
  double ag = 0.0;
  double fg = 0.0;
  GSHSCENE_Color_Space space{};
  std::vector<double> clips;
  bool nonisolated = false;
  bool knockout = false;
  double opacity = 1.0;
  uint32_t composite = 0;
  double outer = 1.0;
};

double product(const std::vector<double> & v) {
  double p = 1.0;
  for (double x : v) {
    p *= x;
  }
  return p;
}

/** The backdrop an element of @p layer composites with (T3): the layer, or in a knockout group its initial backdrop. */
void backdrop_of(const Layer & layer, double out[4]) {
  std::memcpy(out, layer.knockout ? layer.l0 : layer.l, 4 * sizeof(double));
}

/** One element into a layer by the library's primitives; the group alpha and shape are carried in every layer here (KC3 carries fewer). */
void element(Layer & layer, const double s[4], double m, double shape, uint32_t op) {
  double bk[4];
  backdrop_of(layer, bk);
  double ag = layer.ag;
  double fg = layer.fg;
  EXPECT_GE(gshcpu_render_group_step(layer.l, bk, layer.nonisolated ? &ag : nullptr, &fg, layer.knockout ? 1 : 0, s, m, shape, op), 0);
  layer.ag = ag;
  layer.fg = fg;
}

/** T3 to T5 at one pixel with the primitives, for the items of shape-scene's group vectors. */
void evaluate(const group_vectors::VCase & c, double out[4]) {
  using namespace group_vectors;
  const GSHSCENE_Color_Space target = vh::space_of(c.target.primaries, c.target.transfer, c.target.gamma);
  std::vector<Layer> stack(1);
  std::memcpy(stack[0].l, c.backdrop, sizeof stack[0].l);
  stack[0].space = target;
  for (int i = 0; i < c.item_count; i++) {
    const VItem & it = c.items[i];
    Layer & top = stack.back();
    if (it.kind == 1) {
      double rgb[3] = {it.rgba[0], it.rgba[1], it.rgba[2]};
      GSHSCENE_Color_Space space = vh::space_of(it.space.primaries, it.space.transfer, it.space.gamma);
      if (gshscene_color_space_is_target(&space)) {
        space = target;
      }
      GSHCPU_Convert cv;
      gshcpu_render_convert_prepare(&cv, &space, &top.space);
      gshcpu_render_convert_straight(&cv, rgb, rgb);
      const double s[4] = {rgb[0] * it.rgba[3], rgb[1] * it.rgba[3], rgb[2] * it.rgba[3], it.rgba[3]};
      const double f = it.coverage * product(top.clips);
      element(top, s, f, f, GSHSCENE_COMPOSITE_SRC_OVER);
    } else if (it.kind == 2) {
      top.clips.push_back(it.coverage);
    } else if (it.kind == 3) {
      top.clips.pop_back();
    } else if (it.kind == 4) {
      Layer g;
      const GSHSCENE_Color_Space blend = vh::space_of(it.blend.primaries, it.blend.transfer, it.blend.gamma);
      g.space = gshscene_color_space_is_target(&blend) ? top.space : blend;
      g.nonisolated = it.isolation != 0;
      g.knockout = it.knockout != 0;
      g.opacity = it.opacity;
      g.composite = (uint32_t)it.composite;
      g.outer = product(top.clips);
      if (g.nonisolated) {
        double bk[4];
        backdrop_of(top, bk);
        GSHCPU_Convert cv;
        gshcpu_render_convert_prepare(&cv, &top.space, &g.space);
        gshcpu_render_convert_premultiplied(&cv, bk, g.l0);
        std::memcpy(g.l, g.l0, sizeof g.l);
      }
      stack.push_back(g);
    } else {
      Layer done = stack.back();
      stack.pop_back();
      Layer & parent = stack.back();
      double result[4];
      gshcpu_render_group_result(done.l, done.nonisolated ? done.ag : done.l[3], done.nonisolated ? 0 : 1, done.l0, result);
      GSHCPU_Convert cv;
      gshcpu_render_convert_prepare(&cv, &done.space, &parent.space);
      double s[4];
      gshcpu_render_convert_premultiplied(&cv, result, s);
      for (double & v : s) {
        v *= done.opacity;
      }
      const bool pdf = gshcpu_render_pdf_operator(done.composite) != 0;
      element(parent, s, done.outer, pdf ? done.outer * done.fg : done.outer, done.composite);
    }
  }
  std::memcpy(out, stack[0].l, 4 * sizeof(double));
}

TEST(RenderGroup, ThePrimitivesMeetThePdfGroupVectors) {
  using namespace group_vectors;
  vh::Worst worst;
  int exact = 0;
  int combos[2][2] = {{0, 0}, {0, 0}};
  for (const VCase & c : kCases) {
    double got[4];
    evaluate(c, got);
    vh::expect_pre(got, c.want, c.exact, c.name, worst);
    exact += c.exact ? 1 : 0;
    for (int i = 0; i < c.item_count; i++) {
      if (c.items[i].kind == 4) {
        combos[c.items[i].isolation ? 1 : 0][c.items[i].knockout ? 1 : 0]++;
      }
    }
  }
  EXPECT_GT(exact, 0);
  for (int a = 0; a < 2; a++) {
    for (int b = 0; b < 2; b++) {
      EXPECT_GT(combos[a][b], 0) << "a vector for each of the four combinations of isolated and knockout";
    }
  }
  std::printf("group_vectors through the primitives: %zu cases, %d exact; worst error %.3g (%s)\n", sizeof kCases / sizeof kCases[0],
      exact, worst.error, worst.where.c_str());
}

/* --- through renders ------------------------------------------------------------------------------------- */

TEST(RenderGroup, RendersMeetThePdfGroupVectorsWhereTheCoverageIsWhole) {
  using namespace group_vectors;
  vh::Worst worst;
  int run = 0;
  for (const VCase & c : kCases) {
    bool whole = true;
    for (int i = 0; i < c.item_count; i++) {
      const int k = c.items[i].kind;
      if ((k == 1 || k == 2) && c.items[i].coverage != 0.0 && c.items[i].coverage != 1.0) {
        whole = false;
      }
    }
    bool floats = true;
    for (double v : c.backdrop) {
      floats = floats && (double)(float)v == v;
    }
    if (!whole || !floats) {
      continue;
    }
    vh::Bench b;
    for (int i = 0; i < c.item_count; i++) {
      const VItem & it = c.items[i];
      b.record(it.kind, 0, it.rgba, vh::space_of(it.space.primaries, it.space.transfer, it.space.gamma), it.opacity,
          vh::space_of(it.blend.primaries, it.blend.transfer, it.blend.gamma), it.composite, it.coverage == 1.0 ? 64 : 0, 0,
          it.isolation, it.knockout);
    }
    ASSERT_EQ(gshscene_list_close(b.list), GSHSCENE_OK);
    GIMG_Raster * raster = vh::one_pixel(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    float px[4];
    for (int i = 0; i < 4; i++) {
      px[i] = (float)c.backdrop[i];
    }
    std::memcpy(gimg_raster_pixels(raster), px, sizeof px);
    vh::Seen seen = vh::render(b.list, raster, vh::space_of(c.target.primaries, c.target.transfer, c.target.gamma));
    ASSERT_GE(seen.touched, 0) << c.name;
    if (seen.touched) {
      vh::expect_pre(seen.root, c.want, c.exact, c.name, worst);
    } else {
      for (int i = 0; i < 4; i++) {
        EXPECT_EQ(c.want[i], c.backdrop[i]) << c.name;
      }
    }
    gimg_raster_destroy(raster);
    run++;
  }
  EXPECT_GE(run, 15) << "too few of the vectors reached a render";
  std::printf("group_vectors through renders: %d cases; worst error %.3g (%s)\n", run, worst.error, worst.where.c_str());
}

TEST(RenderGroup, RendersMeetTheGeneralVectorsOfTheThirdRestatement) {
  using namespace general_vectors;
  vh::Worst worst;
  int exact = 0;
  int run = 0;
  for (const GCase & c : kCases) {
    vh::Bench b;
    for (int i = 0; i < c.item_count; i++) {
      const GItem & it = c.items[i];
      b.record(it.kind, 0, it.rgba, vh::space_of(it.space.primaries, it.space.transfer, it.space.gamma), it.opacity,
          vh::space_of(it.blend.primaries, it.blend.transfer, it.blend.gamma), it.composite, it.width, 0, it.isolation, it.knockout);
    }
    ASSERT_EQ(gshscene_list_close(b.list), GSHSCENE_OK) << c.name;
    GIMG_Raster * raster = vh::one_pixel(GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED);
    float px[4];
    for (int i = 0; i < 4; i++) {
      px[i] = (float)c.backdrop[i];
      ASSERT_EQ((double)px[i], c.backdrop[i]) << c.name << ": the backdrop is a float";
    }
    std::memcpy(gimg_raster_pixels(raster), px, sizeof px);
    vh::Seen seen = vh::render(b.list, raster, vh::space_of(c.target.primaries, c.target.transfer, c.target.gamma));
    EXPECT_EQ(seen.touched == 1, c.touched) << c.name;
    if (seen.touched) {
      vh::expect_pre(seen.root, c.want, c.exact, c.name, worst);
      std::memcpy(px, gimg_raster_pixels_const(raster), sizeof px);
      for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(vh::float_store_ok(px[i], c.want[i], c.exact)) << c.name << " channel " << i << ": stored " << px[i];
      }
    }
    exact += c.exact ? 1 : 0;
    gimg_raster_destroy(raster);
    run++;
  }
  EXPECT_EQ(exact, kExactCaseCount);
  EXPECT_GE(run, 500);
  std::printf("general_vectors through renders: %d cases, %d exact; worst error %.3g (%s)\n", run, exact, worst.error,
      worst.where.c_str());
}

/* --- the lists the rest build ------------------------------------------------------------------------------- */

using mh::Draw;
using mh::Options;
using mh::Scene;

GSHSCENE_Paint paint(double r, double g, double b, double a) {
  return rh::solid(r, g, b, a);
}

/** A list built by a short program: `n` and `k` push a non-isolated and a knockout group (`N` both, `i` neither), `.` pops. */
struct Program {
  Scene scene;
  GSHSCENE_List * list = nullptr;
  explicit Program(const char * program, const GSHSCENE_Resource * instance = nullptr, uint32_t composite = GSHSCENE_COMPOSITE_SRC_OVER) {
    list = scene.open();
    double x = 1.0;
    for (const char * p = program; *p; p++) {
      if (*p == '.') {
        EXPECT_EQ(gshscene_list_group_pop(list), GSHSCENE_OK);
      } else if (*p == '@') {
        EXPECT_EQ(gshscene_list_instance(list, instance, nullptr, 0, 0.0), GSHSCENE_OK);
      } else {
        GSHSCENE_Group g = scene.group(0.8, composite);
        g.isolation = (*p == 'n' || *p == 'N') ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
        g.knockout = (*p == 'k' || *p == 'N') ? 1u : 0u;
        EXPECT_EQ(gshscene_list_group_push(list, &g, 0, 0.0), GSHSCENE_OK);
        GSHSCENE_Paint pt = paint(0.2 + 0.1 * std::fmod(x, 7.0), 0.4, 0.8, 0.6);
        EXPECT_EQ(gshscene_list_fill(list, scene.rect(x, x, x + 9.0, x + 7.0), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0), GSHSCENE_OK);
        x += 2.0;
      }
    }
    EXPECT_EQ(gshscene_list_close(list), GSHSCENE_OK);
  }
};

std::pair<uint64_t, uint64_t> slots(uint64_t alpha, uint64_t shape) {
  return {alpha, shape};
}

/** The channels the layout counted for a render of @p list: {group alpha, group shape}. */
std::pair<uint64_t, uint64_t> channels(const GSHSCENE_List * list) {
  Options o;
  o.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
  Draw d = mh::draw(list, 24, 24, o);
  EXPECT_EQ(d.result, GSHSCENE_OK);
  return {d.counters.group_alpha_slots, d.counters.group_shape_slots};
}

/* --- KC3, KC8: which layers carry what --------------------------------------------------------------------- */

TEST(RenderGroup, AGroupAlphaIsCarriedByExactlyTheNonIsolatedGroupsOpenAtOnce) {
  EXPECT_EQ(channels(Program("n.").list), slots(1, 0));
  EXPECT_EQ(channels(Program("n.n.").list), slots(1, 0)) << "siblings share their channel";
  EXPECT_EQ(channels(Program("nnn...").list), slots(3, 0));
  EXPECT_EQ(channels(Program("k.").list), slots(0, 0)) << "an isolated knockout group's alpha is its layer's (L0 = 0)";
  EXPECT_EQ(channels(Program("i.").list), slots(0, 0));
  EXPECT_EQ(channels(Program("N.").list), slots(1, 0)) << "a non-isolated knockout group carries it: L0 is the page";
}

TEST(RenderGroup, AGroupShapeIsCarriedByExactlyTheGroupsWithAKnockoutProperAncestor) {
  EXPECT_EQ(channels(Program("kn..").list), slots(1, 1));
  EXPECT_EQ(channels(Program("kii...").list), slots(0, 2)) << "through the groups between";
  EXPECT_EQ(channels(Program("kin...").list), slots(1, 2));
  EXPECT_EQ(channels(Program("kk..").list), slots(0, 1));
  EXPECT_EQ(channels(Program("ni..k.").list), slots(1, 0)) << "no knockout around either";
  EXPECT_EQ(channels(Program("k.in..").list), slots(1, 0)) << "the knockout group closed before them";
  EXPECT_EQ(channels(Program("k.nk..").list), slots(1, 0)) << "an ancestor that is not knockout";
  EXPECT_EQ(channels(Program("kn.n..").list), slots(1, 1)) << "a sibling under a knockout group reuses the channel the first gave back";
}

TEST(RenderGroup, AStackOfOpenGroupsPastAChunkOfTheLayoutsStackIsCountedAndDrawnFromEveryMemory) {
  // The layout keeps the open groups that are non-isolated or knockout on a stack of chunks (from the allocator, or from the caller's
  // workspace): 200 non-isolated groups deep, back to none, then 140 knockout ones with 100 non-isolated inside the last, so that it
  // grows past its first chunk, shrinks and grows again.
  std::string text = std::string(200, 'n') + std::string(200, '.') + std::string(140, 'k') + std::string(100, 'n') + std::string(240, '.');
  Program p(text.c_str());
  EXPECT_EQ(channels(p.list), slots(200, 239)) << "the groups under a knockout group carry a shape: 139 knockout and 100 non-isolated";
  for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_CALLER_MEMORY}) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    Options o;
    o.flags = flags;
    o.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
    o.alloc = &alloc;
    Draw d = mh::draw(p.list, 24, 24, o);
    ASSERT_EQ(d.result, GSHSCENE_OK) << flags;
    EXPECT_EQ(d.counters.group_alpha_slots, 200u);
    EXPECT_EQ(d.counters.group_shape_slots, 239u);
    EXPECT_TRUE(t.live.empty()) << "every chunk of the stack is given back: " << flags;
  }
}

TEST(RenderGroup, TheCountsRecurseIntoInstancesAndStartAgainInMaskPictures) {
  Scene s;
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Group g = s.group();
  g.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
  ASSERT_EQ(gshscene_list_group_push(pl, &g, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint pt = paint(0.9, 0.2, 0.2, 0.8);
  ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(pl), GSHSCENE_OK);
  const GSHSCENE_Resource * picture = s.picture(pl);
  // The picture instanced inside a knockout group: its group has a knockout ancestor (KC3), as if it were written there.
  {
    GSHSCENE_List * l = s.open();
    GSHSCENE_Group k = s.group();
    k.knockout = 1;
    ASSERT_EQ(gshscene_list_group_push(l, &k, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_instance(l, picture, nullptr, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    EXPECT_EQ(channels(l), slots(1, 1));
  }
  // The same picture instanced inside a non-isolated group: its group alpha is the second open at once.
  {
    GSHSCENE_List * l = s.open();
    GSHSCENE_Group n = s.group();
    n.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
    ASSERT_EQ(gshscene_list_group_push(l, &n, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_instance(l, picture, nullptr, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    EXPECT_EQ(channels(l), slots(2, 0));
  }
  // A mask whose picture holds a knockout group that holds a plain one (a group shape in the picture), the masked group itself
  // under a knockout group (a group shape of its own): two shapes are open at the masked group's pop.
  {
    GSHSCENE_List * inner = s.open();
    GSHSCENE_Group k = s.group();
    k.knockout = 1;
    ASSERT_EQ(gshscene_list_group_push(inner, &k, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group plain = s.group();
    ASSERT_EQ(gshscene_list_group_push(inner, &plain, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(inner, s.rect(0, 0, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(inner), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(inner), GSHSCENE_OK);
    GSHSCENE_Mask m{};
    m.picture = s.picture(inner);
    m.mode = GSHSCENE_MASK_ALPHA;
    const GSHSCENE_Resource * mask = s.mask(m);
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_group_push(l, &k, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group masked = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
    ASSERT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    EXPECT_EQ(channels(l), slots(0, 2));
  }
  // The same picture as the picture of a mask of a knockout group: the mask's picture is its own isolated group, so the
  // knockout group around the masked one is not its ancestor (MK2), and the two channels are counted open at the pop.
  {
    GSHSCENE_Mask m{};
    m.picture = picture;
    m.mode = GSHSCENE_MASK_ALPHA;
    const GSHSCENE_Resource * mask = s.mask(m);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Group k = s.group();
    k.knockout = 1;
    k.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
    ASSERT_EQ(gshscene_list_group_push(l, &k, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group masked = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
    ASSERT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    // open at the masked group's pop: the knockout non-isolated group (its alpha) and the masked group (a group shape: it has a
    // knockout ancestor); in the picture, one non-isolated group with no knockout ancestor (an alpha, no shape).
    EXPECT_EQ(channels(l), slots(2, 1));
  }
}

/* --- a pixel, in the doubles: the definitions the story states -------------------------------------------- */

Draw one_pixel_draw(const GSHSCENE_List * list, const Options & o = Options()) {
  Options f = o;
  f.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
  return mh::draw(list, 8, 8, f);
}

TEST(RenderGroup, ANonIsolatedGroupBlendsWithThePageAndAnIsolatedOneWithItsOwnLayer) {
  // Over an opaque page of (0.5, 0.5, 0.5) a multiply group of one fill of (0.5, 0.5, 0.5) at alpha 1: the non-isolated group sees
  // the page through it and multiplies (0.25); the isolated group has nothing under the fill and shows it as it is (0.5).
  for (int ni = 0; ni < 2; ni++) {
    Scene s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint page = paint(0.5, 0.5, 0.5, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &page, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group g = s.group();
    g.isolation = ni ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group inner = s.group(1.0, GSHSCENE_COMPOSITE_MULTIPLY);
    ASSERT_EQ(gshscene_list_group_push(l, &inner, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &page, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Draw d = one_pixel_draw(l);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    EXPECT_FLOAT_EQ(d.atf(3, 3, 0), ni ? 0.25f : 0.5f) << ni;
    EXPECT_FLOAT_EQ(d.atf(3, 3, 3), 1.0f);
    EXPECT_EQ(d.counters.general_layers, ni ? 1u : 0u);
  }
}

TEST(RenderGroup, InAKnockoutGroupTheSecondShapeReplacesTheFirstAgainstTheInitialBackdrop) {
  // Two translucent fills of the whole pixel over a blue page, in a plain group and in a knockout one.  Plain: the second composites over
  // the first.  Knockout (non-isolated): each composites with the page, so the second replaces the first.
  for (int knockout = 0; knockout < 2; knockout++) {
    Scene s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint page = paint(0.0, 0.0, 1.0, 1.0);
    GSHSCENE_Paint a = paint(1.0, 0.0, 0.0, 0.5);
    GSHSCENE_Paint b = paint(0.0, 1.0, 0.0, 0.5);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &page, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group g = s.group();
    g.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
    g.knockout = (uint32_t)knockout;
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &a, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &b, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Draw d = one_pixel_draw(l);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    if (knockout) {
      EXPECT_FLOAT_EQ(d.atf(3, 3, 0), 0.0f) << "the red is gone";
      EXPECT_FLOAT_EQ(d.atf(3, 3, 1), 0.5f);
      EXPECT_FLOAT_EQ(d.atf(3, 3, 2), 0.5f);
    } else {
      EXPECT_FLOAT_EQ(d.atf(3, 3, 0), 0.25f);
      EXPECT_FLOAT_EQ(d.atf(3, 3, 1), 0.5f);
      EXPECT_FLOAT_EQ(d.atf(3, 3, 2), 0.25f);
    }
    EXPECT_FLOAT_EQ(d.atf(3, 3, 3), 1.0f);
  }
}

// KC11: a knockout group is defined on shape, and shape is coverage: two shapes that abut in a pixel, each covering half of it, leave a
// quarter of the initial backdrop there, where an exact union would leave none.  Pinned so that a fix is seen.
TEST(RenderGroup, ConflationLeavesAQuarterOfTheBackdropWhereTwoHalvesAbut) {
  for (int variant = 0; variant < 2; variant++) {
    Scene s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint page = paint(0.0, 0.0, 1.0, 1.0);
    GSHSCENE_Paint red = paint(1.0, 0.0, 0.0, 1.0);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &page, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Group g = s.group();
    g.isolation = variant ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
    g.knockout = 1;
    ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 3.5, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(3.5, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Draw d = one_pixel_draw(l);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    const double half = 128.0 / 255.0; // the 8-bit weight of half a pixel (D8)
    const double red_part = 1.0 - (1.0 - half) * (1.0 - half);
    EXPECT_NEAR(d.atf(3, 3, 0), red_part, 1e-6) << variant;
    EXPECT_NEAR(d.atf(3, 3, 2), 1.0 - red_part, 1e-6) << variant << ": about a quarter of the page shows through";
    EXPECT_LT(red_part, 0.76);
    EXPECT_FLOAT_EQ(d.atf(1, 3, 0), 1.0f) << "whole pixels are whole";
    EXPECT_FLOAT_EQ(d.atf(6, 3, 0), 1.0f);
  }
}

// MK7 under a knockout parent: a mask that acts on the shape and one that acts on the opacity are different there, and the same under
// an isolated parent, for a bounded operator that is affine in the source (shape-scene's mask vectors hold the numbers; this holds them
// through a render).
TEST(RenderGroup, AShapeMaskAndAnOpacityMaskDifferUnderAKnockoutParentOnlyForSourceOver) {
  for (int parent_knockout = 0; parent_knockout < 2; parent_knockout++) {
    Draw by[2];
    for (uint32_t acts = 0; acts < 2; acts++) {
      Scene s;
      GSHSCENE_Paint half = paint(1.0, 1.0, 1.0, 0.5);
      const GSHSCENE_Resource * mask = s.alpha_mask(s.picture_of(s.rect(0, 0, 8, 8), half), acts);
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint page = paint(0.0, 0.0, 1.0, 1.0);
      GSHSCENE_Paint back = paint(0.0, 1.0, 0.0, 1.0);
      GSHSCENE_Paint red = paint(1.0, 0.0, 0.0, 1.0);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &page, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group outer = s.group();
      outer.knockout = (uint32_t)parent_knockout;
      outer.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
      ASSERT_EQ(gshscene_list_group_push(l, &outer, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &back, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group masked = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
      ASSERT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      by[acts] = one_pixel_draw(l);
      ASSERT_EQ(by[acts].result, GSHSCENE_OK);
      EXPECT_EQ(by[acts].count(GSHSCENE_ENTRY_REFUSED), 0u);
    }
    bool differ = false;
    for (int c = 0; c < 4; c++) {
      differ = differ || by[0].atf(3, 3, c) != by[1].atf(3, 3, c);
    }
    EXPECT_EQ(differ, parent_knockout == 1) << "the two inputs of T3 differ only where the backdrop is not the layer";
    if (parent_knockout) {
      // Opacity: the red at half over the page (0.5 red, 0.5 blue); the element's shape is the group's, 1, so the green is knocked out.
      EXPECT_NEAR(by[0].atf(3, 3, 0), 0.5, 1e-6);
      EXPECT_NEAR(by[0].atf(3, 3, 1), 0.0, 1e-6);
      // Shape: the red at full opacity with shape 1/2: half the red over the page, and half of what the layer held beyond the page (the green).
      EXPECT_NEAR(by[1].atf(3, 3, 0), 0.5, 1e-6);
      EXPECT_NEAR(by[1].atf(3, 3, 1), 0.5, 1e-6) << "half the layer's own value stays: the green the knockout leaves of the layer";
      EXPECT_NEAR(by[1].atf(3, 3, 2), 0.0, 1e-6) << "and none of the page: half is red, half is the layer";
    }
  }
}

/* --- KC2: a stroke is one element, and equals a fill of the same coverage --------------------------------------- */

TEST(RenderGroup, AStrokeIsOneElementOfAKnockoutGroupAndEqualsTheFillOfItsOutline) {
  for (int ni = 0; ni < 2; ni++) {
    Draw by[2];
    for (int stroked = 0; stroked < 2; stroked++) {
      Scene s;
      GSHSCENE_List * l = s.open();
      GSHSCENE_Paint page = paint(0.2, 0.7, 0.3, 1.0);
      GSHSCENE_Paint red = paint(0.9, 0.1, 0.1, 0.7);
      GSHSCENE_Paint blue = paint(0.1, 0.1, 0.9, 0.6);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 16, 16), nullptr, GSHGEOM_FILL_NONZERO, &page, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group g = s.group();
      g.knockout = 1;
      g.isolation = ni ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 1, 12, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
      if (stroked) {
        // A butt-capped horizontal line of width 4.5 along y = 6.25 from x = 3.25 to x = 14.75: the rectangle [3.25, 14.75] x [4, 8.5].
        GSHGEOM_Path * p = nullptr;
        gshgeom_path_create(nullptr, nullptr, 2, &p);
        gshgeom_path_move_to(p, GSHGEOM_Point{3.25, 6.25, 0});
        gshgeom_path_line_to(p, GSHGEOM_Point{14.75, 6.25, 0});
        const GSHSCENE_Resource * line = s.path(p);
        GSHSCENE_Stroke_Style st;
        gshscene_stroke_style_init(&st);
        st.width = 4.5;
        st.cap = GSHSCENE_STROKE_CAP_BUTT;
        const GSHSCENE_Resource * style = nullptr;
        ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
        s.held.push_back(style);
        ASSERT_EQ(gshscene_list_stroke(l, line, style, nullptr, &blue, 0, 0.0), GSHSCENE_OK);
      } else {
        ASSERT_EQ(gshscene_list_fill(l, s.rect(3.25, 4.0, 14.75, 8.5), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
      }
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
      Options o;
      o.format = GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
      by[stroked] = mh::draw(l, 16, 16, o);
      ASSERT_EQ(by[stroked].result, GSHSCENE_OK);
      EXPECT_GT(by[stroked].counters.general_fills, 0u);
    }
    EXPECT_EQ(by[0].bytes, by[1].bytes) << "a stroke is one element whose shape is its coverage: " << (ni ? "non-isolated" : "isolated");
  }
}

/* --- KC10: damage ------------------------------------------------------------------------------------------ */

size_t first_general_fixture() {
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "group-", 6) == 0) {
      return i;
    }
  }
  return RF_FIXTURE_COUNT;
}

/** One past the last group fixture: the pattern fixtures that follow hold groups only inside a tile, and are the pattern tests'. */
size_t end_general_fixture() {
  for (size_t i = first_general_fixture(); i < RF_FIXTURE_COUNT; i++) {
    if (std::strncmp(RF_FIXTURES[i].name, "group-", 6) != 0) {
      return i;
    }
  }
  return RF_FIXTURE_COUNT;
}

/** Fixture @p index drawn with tiers @p tiers, an optional @p damage and an optional counting allocator, onto a raster of @p format. */
struct Fixture {
  int result = -1;
  std::string bytes;
  GSHCPU_Render_Counters counters{};
  size_t need = 0;
};

Fixture draw_fixture(size_t index, GSHCPU_Format format, unsigned tiers, const GSHSCENE_Rect * damage = nullptr) {
  Fixture out;
  RF_Ctx c;
  rf_begin(&c);
  GSHSCENE_List * list = RF_FIXTURES[index].build(&c);
  GIMG_Raster * raster = rf_raster(format, RF_WIDTH, RF_HEIGHT);
  GSHSCENE_Sink * sink = nullptr;
  GSHSCENE_Report * report = nullptr;
  if (!c.failed && list && raster && gshcpu_render_sink_create_tiered(nullptr, nullptr, nullptr, tiers, &out.counters, nullptr, nullptr, &sink) == GSHCPU_OK &&
      gshscene_report_create(nullptr, nullptr, &report) == GSHSCENE_OK) {
    GSHSCENE_Target target;
    std::memset(&target, 0, sizeof target);
    target.kind = GSHSCENE_TARGET_RASTER;
    target.surface = raster;
    target.map = c.target_map;
    target.damage = damage;
    GSHSCENE_Render_Request request;
    std::memset(&request, 0, sizeof request);
    request.list = list;
    request.target = &target;
    request.report = report;
    gshscene_sink_workspace_bytes(sink, &request, &out.need);
    out.result = (int)gshscene_sink_render(sink, &request);
    out.bytes.assign((const char *)gimg_raster_pixels_const(raster), gimg_raster_stride_bytes(raster) * RF_HEIGHT);
  }
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  rf_release(&c);
  gimg_raster_destroy(raster);
  return out;
}

TEST(RenderGroup, InsideADamageTheRenderOfAGeneralGroupEqualsTheWholeRender) {
  const size_t first = first_general_fixture();
  ASSERT_LT(first, RF_FIXTURE_COUNT);
  const GSHSCENE_Rect damages[3] = {{5, 4, 29, 21}, {0, 0, 7, 40}, {20, 15, 48, 40}};
  for (size_t i = first; i < end_general_fixture(); i++) {
    for (GSHCPU_Format format : {GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_FORMAT_RGBAF32_STRAIGHT}) {
      Fixture whole = draw_fixture(i, format, GSHCPU_RENDER_TIERS_ALL);
      ASSERT_EQ(whole.result, (int)GSHSCENE_OK) << RF_FIXTURES[i].name;
      const size_t bpp = gshcpu_render_pixel_bytes(format);
      for (const GSHSCENE_Rect & dm : damages) {
        Fixture part = draw_fixture(i, format, GSHCPU_RENDER_TIERS_ALL, &dm);
        ASSERT_EQ(part.result, (int)GSHSCENE_OK) << RF_FIXTURES[i].name;
        const size_t stride = bpp * RF_WIDTH;
        for (int64_t y = dm.y0; y < dm.y1; y++) {
          for (int64_t x = dm.x0; x < dm.x1; x++) {
            ASSERT_EQ(part.bytes.compare((size_t)y * stride + (size_t)x * bpp, bpp, whole.bytes, (size_t)y * stride + (size_t)x * bpp, bpp), 0)
                << RF_FIXTURES[i].name << " at " << x << "," << y;
          }
        }
      }
    }
  }
}

/* --- KC8: memory is exact ---------------------------------------------------------------------------------- */

TEST(RenderGroup, TheMemoryOfGeneralGroupsIsExactAndNeverAllocated) {
  const size_t first = first_general_fixture();
  for (size_t i = first; i < end_general_fixture(); i++) {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * list = RF_FIXTURES[i].build(&c);
    ASSERT_TRUE(!c.failed && list) << RF_FIXTURES[i].name;
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      mh::SinkRef sink(nullptr, &alloc);
      mh::ReportRef report(&alloc);
      mh::RasterRef ref(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
      GSHSCENE_Target target = mh::target_of(ref.raster);
      GSHSCENE_Render_Request req = mh::request_of(list, &target, report.report);
      size_t need = 0;
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK) << RF_FIXTURES[i].name;
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << RF_FIXTURES[i].name;
      req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
      std::vector<unsigned char> workspace(need + 1 + 4096, 0xc5);
      req.workspace = workspace.data() + 1;
      req.workspace_bytes = need - 1;
      mh::RasterRef fresh(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
      mh::RasterRef out(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
      GSHSCENE_Target short_target = mh::target_of(out.raster);
      req.target = &short_target;
      EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT) << "one byte short: " << RF_FIXTURES[i].name;
      EXPECT_EQ(out.bytes(), fresh.bytes());
      req.workspace_bytes = need;
      size_t again = 0;
      const long before = t.calls;
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &again), GSHSCENE_OK);
      EXPECT_EQ(again, need) << RF_FIXTURES[i].name;
      EXPECT_EQ(t.calls, before) << "sizing in the caller's workspace allocated: " << RF_FIXTURES[i].name;
      for (int frame = 0; frame < 2; frame++) {
        mh::RasterRef drawn(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
        GSHSCENE_Target ft = mh::target_of(drawn.raster);
        req.target = &ft;
        const long calls = t.calls;
        ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << RF_FIXTURES[i].name;
        if (frame > 0) {
          EXPECT_EQ(t.calls, calls) << "a render from the caller's memory allocated: " << RF_FIXTURES[i].name;
        }
        mh::RasterRef plain(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, RF_WIDTH, RF_HEIGHT));
        GSHSCENE_Target pt = mh::target_of(plain.raster);
        GSHSCENE_Render_Request normal = mh::request_of(list, &pt, report.report);
        ASSERT_EQ(gshscene_sink_render(sink.sink, &normal), GSHSCENE_OK);
        EXPECT_EQ(drawn.bytes(), plain.bytes()) << "the same pixels as from the sink's own memory: " << RF_FIXTURES[i].name;
        for (size_t b = need + 1; b < workspace.size(); b++) {
          ASSERT_EQ(workspace[b], 0xc5) << "written past the caller's memory: " << RF_FIXTURES[i].name;
        }
        ASSERT_EQ(workspace[0], 0xc5) << "written before the caller's memory";
      }
    }
    EXPECT_TRUE(t.live.empty()) << RF_FIXTURES[i].name;
    rf_release(&c);
  }
}

/* --- KC9: the default path ------------------------------------------------------------------------------------ */

TEST(RenderGroup, EveryFixtureRenderedThroughTheElementStepEqualsTheNormalRender) {
  // Every layer of every fixture (the root's too) composites by KC4, and agrees with the plain step: exactly where the arithmetic is
  // exact in doubles (the two are the same function in the reals, not bit for bit), else within a rounding of the one write.
  size_t identical = 0;
  size_t total = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    for (GSHCPU_Format format : RF_FORMATS) {
      Fixture normal = draw_fixture(i, format, GSHCPU_RENDER_TIERS_ALL);
      Fixture forced = draw_fixture(i, format, GSHCPU_RENDER_TIERS_ALL | GSHCPU_RENDER_FORCE_GENERAL);
      ASSERT_EQ(normal.result, forced.result) << RF_FIXTURES[i].name;
      EXPECT_GT(forced.counters.general_fills, 0u) << RF_FIXTURES[i].name << ": the switch routes the fills through the element step";
      EXPECT_GE(forced.counters.general_fills, normal.counters.general_fills) << RF_FIXTURES[i].name;
      ASSERT_EQ(normal.bytes.size(), forced.bytes.size());
      total++;
      if (normal.bytes == forced.bytes) {
        identical++;
        continue;
      }
      if (format == GSHCPU_FORMAT_RGBAF32_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED) {
        for (size_t b = 0; b + 4 <= normal.bytes.size(); b += 4) {
          float a;
          float f;
          std::memcpy(&a, normal.bytes.data() + b, 4);
          std::memcpy(&f, forced.bytes.data() + b, 4);
          ASSERT_LE(std::fabs((double)a - (double)f), 1e-6 * std::max(1.0, std::fabs((double)a))) << RF_FIXTURES[i].name << " byte " << b;
        }
      } else {
        for (size_t b = 0; b < normal.bytes.size(); b++) {
          const int d = std::abs((int)(unsigned char)normal.bytes[b] - (int)(unsigned char)forced.bytes[b]);
          ASSERT_LE(d, 1) << RF_FIXTURES[i].name << " byte " << b;
        }
      }
    }
  }
  std::printf("the forced element step: %zu of %zu renders (fixture and format) are the normal render's bytes\n", identical, total);
  EXPECT_GT(identical, total / 2) << "the two are mostly the same doubles";
}

TEST(RenderGroup, AListWithoutNonIsolatedOrKnockoutGroupsTakesNoneOfTheGeneralStep) {
  const size_t first = first_general_fixture();
  ASSERT_GT(first, 40u);
  for (size_t i = 0; i < first; i++) {
    Fixture f = draw_fixture(i, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_RENDER_TIERS_ALL);
    EXPECT_EQ(f.counters.general_layers, 0u) << RF_FIXTURES[i].name;
    EXPECT_EQ(f.counters.general_fills, 0u) << RF_FIXTURES[i].name;
    EXPECT_EQ(f.counters.general_pops, 0u) << RF_FIXTURES[i].name;
    EXPECT_EQ(f.counters.group_alpha_slots, 0u) << RF_FIXTURES[i].name;
    EXPECT_EQ(f.counters.group_shape_slots, 0u) << RF_FIXTURES[i].name;
  }
  for (size_t i = first; i < end_general_fixture(); i++) {
    Fixture f = draw_fixture(i, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_RENDER_TIERS_ALL);
    EXPECT_GT(f.counters.general_layers, 0u) << RF_FIXTURES[i].name << " uses the groups it is named for";
    EXPECT_GT(f.counters.general_pops, 0u) << RF_FIXTURES[i].name;
  }
}

TEST(RenderGroup, AnIsolatedPlainGroupIsNotRoutedThroughTheElementStepInAListThatHoldsGeneralGroups) {
  // A plain group beside a knockout one: only the second is a general layer (KC2: a group's own flags decide its layer).
  Program p("i.k.");
  Options o;
  Draw d = mh::draw(p.list, 24, 24, o);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.general_layers, 1u);
  EXPECT_EQ(d.counters.general_fills, 1u) << "the knockout group's fill";
  EXPECT_EQ(d.counters.general_pops, 1u) << "its pop";
  Program q("i.i.");
  Draw e = mh::draw(q.list, 24, 24, o);
  EXPECT_EQ(e.counters.general_layers, 0u);
  EXPECT_EQ(e.counters.general_pops, 0u);
}

/* --- KC8: what a list of plain groups does not take, and a workspace too short for the stack ---------------------- */

TEST(RenderGroup, AListOfPlainGroupsTakesNoRoomForTheStackOfOpenGroups) {
  // The stack of open groups takes its first chunk from the allocator when the first non-isolated or knockout group is pushed, and
  // never for a plain one: sizing a list of one plain group allocates as often as sizing the same fills outside it.
  long calls[2] = {0, 0};
  for (int grouped = 0; grouped < 2; grouped++) {
    Scene s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint pt = paint(0.9, 0.2, 0.2, 0.8);
    GSHSCENE_Group g = s.group();
    if (grouped) {
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    }
    for (int i = 0; i < 3; i++) {
      ASSERT_EQ(gshscene_list_fill(l, s.rect(i, i, i + 6.0, i + 6.0), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0), GSHSCENE_OK);
    }
    if (grouped) {
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      mh::SinkRef sink(nullptr, &alloc);
      mh::ReportRef report(&alloc);
      mh::RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 16, 16));
      GSHSCENE_Target target = mh::target_of(r.raster);
      GSHSCENE_Render_Request req = mh::request_of(l, &target, report.report);
      size_t need = 0;
      const long before = t.calls;
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
      calls[grouped] = t.calls - before;
    }
  }
  EXPECT_EQ(calls[0], calls[1]);
}

TEST(RenderGroup, AWorkspaceTooShortForTheStackIsALimitAndNeverAnAllocationFailure) {
  Program p("nk..");
  mh::SinkRef sink;
  mh::ReportRef report;
  mh::RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 16, 16));
  GSHSCENE_Target target = mh::target_of(r.raster);
  GSHSCENE_Render_Request req = mh::request_of(p.list, &target, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  std::vector<unsigned char> small(64, 0xc5);
  req.workspace = small.data();
  req.workspace_bytes = small.size();
  EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_LIMIT);
  for (unsigned char b : small) {
    ASSERT_EQ(b, 0xc5) << "nothing is written to a workspace the render refuses";
  }
  size_t need = 0;
  EXPECT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_ERR_LIMIT) << "no room to size itself in: a limit, as for any list";
}

/* --- KC2: a stroke alone in a general layer takes the element step on every route ------------------------------- */

TEST(RenderGroup, AStrokeAloneInAGeneralLayerIsDrawnByTheElementStepWhicheverTierDrawsIt) {
  GSHCPU_Limits fast{};
  fast.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
  for (int variant = 0; variant < 3; variant++) {
    Scene s;
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint blue = paint(0.1, 0.1, 0.9, 0.6);
    const bool group = variant < 2;
    GSHSCENE_Group g = s.group();
    g.knockout = 1;
    if (group) {
      ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    }
    GSHGEOM_Path * p = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &p);
    gshgeom_path_move_to(p, GSHGEOM_Point{2.25, 6.25, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{13.75, 6.25, 0});
    const GSHSCENE_Resource * line = s.path(p);
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    st.width = 3.5;
    st.cap = GSHSCENE_STROKE_CAP_BUTT;
    const GSHSCENE_Resource * style = nullptr;
    ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
    s.held.push_back(style);
    ASSERT_EQ(gshscene_list_stroke(l, line, style, nullptr, &blue, 0, 0.0), GSHSCENE_OK);
    if (group) {
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Options o;
    o.format = GSHCPU_FORMAT_RGBA8_PREMULTIPLIED;
    if (variant == 1) {
      o.limits = &fast; // the fast stroke coverage's own composite
    }
    if (variant == 2) {
      o.limits = &fast; // and with every layer through the element step (KC9's switch), where no stroke may go straight to the bytes
      o.tiers = GSHCPU_RENDER_TIERS_ALL | GSHCPU_RENDER_FORCE_GENERAL;
    }
    Draw d = mh::draw(l, 16, 16, o);
    ASSERT_EQ(d.result, GSHSCENE_OK) << variant;
    EXPECT_EQ(d.counters.strokes, 1u) << variant;
    EXPECT_GE(d.counters.general_fills, 1u) << "the stroke's pixels take the element step: " << variant;
  }
}

// The fast fill coverage composites into byte layers where it can; a list that holds a general layer is composited in doubles, as one
// that holds a masked group or an image paint is (the layers carry channels no byte layer holds).  Each general fixture drawn with the
// fast coverage on an 8-bit target stays within the few levels the fast coverage differs from the exact one at an edge, and a
// knockout or non-isolated group drawn as a plain one would be off by a color.
TEST(RenderGroup, AGeneralGroupUnderTheFastFillCoverageIsDrawnInDoublesAndNotInBytes) {
  GSHCPU_Limits fast{};
  fast.fill_coverage = GSHCPU_FILL_COVERAGE_FAST;
  fast.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
  const size_t first = first_general_fixture();
  int worst = 0;
  for (size_t i = first; i < end_general_fixture(); i++) {
    RF_Ctx c;
    rf_begin(&c);
    GSHSCENE_List * list = RF_FIXTURES[i].build(&c);
    ASSERT_TRUE(!c.failed && list) << RF_FIXTURES[i].name;
    Draw exact;
    Draw quick;
    for (int fastest = 0; fastest < 2; fastest++) {
      Options o;
      o.backdrop = true;
      o.limits = fastest ? &fast : nullptr;
      (fastest ? quick : exact) = mh::draw(list, RF_WIDTH, RF_HEIGHT, o);
      ASSERT_EQ((fastest ? quick : exact).result, GSHSCENE_OK) << RF_FIXTURES[i].name;
    }
    EXPECT_GT(quick.counters.general_layers, 0u) << RF_FIXTURES[i].name;
    EXPECT_GT(quick.counters.general_pops, 0u) << RF_FIXTURES[i].name << ": the pops took the element step, not the byte layers' composite";
    for (size_t b = 0; b < exact.bytes.size(); b++) {
      const int d = std::abs((int)(unsigned char)exact.bytes[b] - (int)(unsigned char)quick.bytes[b]);
      worst = std::max(worst, d);
      ASSERT_LE(d, 64) << RF_FIXTURES[i].name << " byte " << b << ": a general layer drawn as a plain one is off by a color";
    }
    rf_release(&c);
  }
  std::printf("general fixtures under the fast fill coverage: worst difference from the exact coverage %d levels\n", worst);
}

/* --- what the survivors of the generated plants asked for ----------------------------------------------------------- */

TEST(RenderGroup, OnlyTheGeneralLayersAreCountedAsGeneral) {
  Program p("i.i.k.");
  Draw d = mh::draw(p.list, 24, 24);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.general_layers, 1u) << "two plain groups and one knockout group";
  EXPECT_EQ(d.counters.general_fills, 1u);
  EXPECT_EQ(d.counters.general_pops, 1u);
}

/** A masked group of one fill at (x, y), its mask the picture of a group of a plain circle fill; @p general makes the masked group non-isolated. */
const GSHSCENE_Resource * mask_of_a_picture_with_a_group(Scene & s, int index) {
  GSHSCENE_List * pl = s.open();
  GSHSCENE_Group g = s.group();
  EXPECT_EQ(gshscene_list_group_push(pl, &g, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint white = paint(1, 1, 1, 0.8);
  EXPECT_EQ(gshscene_list_fill(pl, s.rect(index, 0, index + 8.0, 8), nullptr, GSHGEOM_FILL_NONZERO, &white, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_group_pop(pl), GSHSCENE_OK);
  return s.alpha_mask(s.picture(pl));
}

TEST(RenderGroup, AMaskFrameDoesNotInheritTheRecordOfAGroupThatEndedAtItsDepth) {
  // A knockout group that ends, then a masked group at the same depth whose picture holds a group: the frame the mask is drawn in is its
  // own isolated group, whatever a group before it left at that depth, so the picture's group carries no group shape (the layout counted
  // none) and the masked part of the picture is drawn as it is without the knockout group before it.
  Draw by[2];
  for (int before = 0; before < 2; before++) {
    Scene s;
    const GSHSCENE_Resource * mask = mask_of_a_picture_with_a_group(s, 0);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint red = paint(0.9, 0.1, 0.1, 0.9);
    if (before) {
      // A plain group around a knockout group, both ended: the knockout group's record is the one at the depth the mask's frame will take.
      GSHSCENE_Group outer = s.group();
      ASSERT_EQ(gshscene_list_group_push(l, &outer, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Group k = s.group();
      k.knockout = 1;
      ASSERT_EQ(gshscene_list_group_push(l, &k, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 0, 6, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    }
    GSHSCENE_Group masked = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
    GSHGEOM_Map at;
    gshgeom_map_translate(2, 12.0, 12.0, 0.0, &at);
    masked.soft_mask_map = at;
    ASSERT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(10, 10, 22, 22), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    Options o;
    o.format = GSHCPU_FORMAT_RGBA8_STRAIGHT; // a straight target written back where the touched map says it was drawn: a flag set wrongly shows
    o.backdrop = true;
    by[before] = mh::draw(l, 24, 24, o);
    ASSERT_EQ(by[before].result, GSHSCENE_OK);
    EXPECT_EQ(by[before].counters.group_shape_slots, 0u) << "no group shape is carried: the knockout group has ended";
  }
  for (uint32_t y = 8; y < 24; y++) {
    for (uint32_t x = 8; x < 24; x++) {
      for (int c = 0; c < 4; c++) {
        ASSERT_EQ(by[0].at8(x, y, c), by[1].at8(x, y, c)) << x << "," << y;
      }
    }
  }
}

TEST(RenderGroup, AMaskedNonIsolatedGroupThatDrawsNothingIsAUnitDoneAndItsMaskIsNeverDrawn) {
  Scene s;
  const GSHSCENE_Resource * mask = mask_of_a_picture_with_a_group(s, 0);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Group g = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
  g.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
  ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  GSHSCENE_Paint red = paint(0.9, 0.1, 0.1, 0.9);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(2, 2, 9, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Draw d = mh::draw(l, 24, 24);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.masks, 0u) << "nothing of the group is composited, and no mask is drawn for it";
  EXPECT_EQ(d.summary.units_done, d.summary.units_total) << "the group is closed, so the fill after it is a unit done";
}

TEST(RenderGroup, AMaskedGeneralGroupIsCountedAsAMaskedPopAndEveryMaskedFixtureDrawsItsMask) {
  size_t index = RF_FIXTURE_COUNT;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    if (std::strcmp(RF_FIXTURES[i].name, "group-masked") == 0) {
      index = i;
    }
  }
  ASSERT_LT(index, RF_FIXTURE_COUNT);
  Fixture f = draw_fixture(index, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, GSHCPU_RENDER_TIERS_ALL);
  ASSERT_EQ(f.result, (int)GSHSCENE_OK);
  EXPECT_EQ(f.counters.masks, 4u) << "three masked general groups and one whose mask picture is a knockout group";
  EXPECT_GT(f.counters.mask_pixels, 0u);
  EXPECT_EQ(f.counters.general_pops, 4u);
}

TEST(RenderGroup, AnAllocationThatFailsWhileTheLayoutCountsIsAnAllocationFailureAndNeverALimit) {
  // The stack of open groups is the one allocation of the layout that only a general group makes; a mask's picture holding such a group
  // makes it inside the count of the picture.  Failing each allocation of a render in turn gives the render or an out-of-memory.
  for (int variant = 0; variant < 2; variant++) {
    Scene s;
    GSHSCENE_List * l = nullptr;
    std::unique_ptr<Program> plain;
    if (variant == 0) {
      plain.reset(new Program("nk.."));
      l = plain->list;
    } else {
      GSHSCENE_List * pl = s.open();
      GSHSCENE_Group k = s.group();
      k.knockout = 1;
      ASSERT_EQ(gshscene_list_group_push(pl, &k, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Paint white = paint(1, 1, 1, 0.8);
      ASSERT_EQ(gshscene_list_fill(pl, s.rect(0, 0, 8, 8), nullptr, GSHGEOM_FILL_NONZERO, &white, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(pl), GSHSCENE_OK);
      const GSHSCENE_Resource * mask = s.alpha_mask(s.picture(pl));
      l = s.open();
      GSHSCENE_Group masked = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask);
      ASSERT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
      GSHSCENE_Paint red = paint(0.9, 0.1, 0.1, 0.9);
      ASSERT_EQ(gshscene_list_fill(l, s.rect(1, 1, 7, 7), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
      ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    }
    long total = 0;
    {
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      mh::SinkRef sink(nullptr, &alloc);
      mh::ReportRef report(&alloc);
      mh::RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 16, 16));
      GSHSCENE_Target target = mh::target_of(r.raster);
      GSHSCENE_Render_Request req = mh::request_of(l, &target, report.report);
      const long before = t.calls;
      ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
      total = t.calls - before;
    }
    ASSERT_GT(total, 0);
    for (long fail = 0; fail < total; fail++) {
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      mh::SinkRef sink(nullptr, &alloc);
      mh::ReportRef report(&alloc);
      mh::RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED, 16, 16));
      GSHSCENE_Target target = mh::target_of(r.raster);
      GSHSCENE_Render_Request req = mh::request_of(l, &target, report.report);
      t.fail_from = t.calls + fail;
      const GSHSCENE_Result got = gshscene_sink_render(sink.sink, &req);
      EXPECT_TRUE(got == GSHSCENE_OK || got == GSHSCENE_ERR_OOM) << "variant " << variant << ", allocation " << fail << " failing gave " << (int)got;
    }
  }
}

TEST(RenderGroup, TheStackOfOpenGroupsAndTheMemoOfMaskPicturesShareTheCallersMemoryWithoutOverlapping) {
  // A non-isolated group (the stack takes its first chunk from the caller's memory) around 40 masked groups of 40 different pictures (the
  // memo of the pictures grows past its first table, from the same memory): the render from the caller's memory equals the render from the
  // sink's own, so neither wrote over the other.
  Scene s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Group n = s.group();
  n.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
  ASSERT_EQ(gshscene_list_group_push(l, &n, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint red = paint(0.9, 0.1, 0.1, 0.9);
  for (int i = 0; i < 40; i++) {
    GSHSCENE_Group masked = s.group(1.0, GSHSCENE_COMPOSITE_SRC_OVER, mask_of_a_picture_with_a_group(s, i));
    ASSERT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l, s.rect(i % 8, (i / 8) * 3, i % 8 + 4.0, (i / 8) * 3 + 3.0), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  // A second non-isolated group after the first: one channel again, if the stack gave the first group back whole.
  ASSERT_EQ(gshscene_list_group_push(l, &n, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.rect(0, 12, 6, 15), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Options normal;
  Options caller;
  caller.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  Draw a = mh::draw(l, 16, 16, normal);
  Draw b = mh::draw(l, 16, 16, caller);
  ASSERT_EQ(a.result, GSHSCENE_OK);
  ASSERT_EQ(b.result, GSHSCENE_OK);
  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_GE(a.counters.mask_pictures, 40u);
  EXPECT_EQ(a.counters.group_alpha_slots, 1u);
  EXPECT_EQ(b.counters.group_alpha_slots, 1u) << "the stack's chunk and the memo's tables are not the same bytes";
}

}  // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
