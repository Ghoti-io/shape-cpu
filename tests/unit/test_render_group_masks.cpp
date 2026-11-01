/*
 * Ghoti.io Shape CPU
 * Copyright 2026 by Corey Pennycuff
 */

/**
 * @file
 *
 * shape-scene's mask vectors (`mask_vectors.h`, M1's: every mode, transfer kind, backdrop and acts-on under all four isolated and knockout
 * parents, and under operators of every family) through this sink's compositing primitives (masks-images-patterns story M3, KC10).  Each
 * case is evaluated at one pixel the way the sink evaluates a pop: the mask's picture drawn as its own isolated group in the mask's render
 * space, the value `v` of MK2 by `gshcpu_render_mask_value`, and MK7's one substitution into the element the pop is, composited by KC4 into
 * a parent that is general or not.  The vectors that tell `SHAPE` from `OPACITY` are the point: under a knockout parent for source-over and
 * the blend modes, and under every parent for `DEST_IN`, `SRC` and the other operators that are not bounded.
 */

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/mask_vectors.h>

#include "../../src/render/render_internal.h"
#include "../vector_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace mask_vectors;

GSHSCENE_Color_Space space_of(const VSpace & s) {
  return vh::space_of(s.primaries, s.transfer, s.gamma);
}

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
  int mask = -1;
};

double product(const std::vector<double> & v) {
  double p = 1.0;
  for (double x : v) {
    p *= x;
  }
  return p;
}

void backdrop_of(const Layer & layer, double out[4]) {
  std::memcpy(out, layer.knockout ? layer.l0 : layer.l, 4 * sizeof(double));
}

void element(Layer & layer, const double s[4], double m, double shape, uint32_t op) {
  double bk[4];
  backdrop_of(layer, bk);
  double ag = layer.ag;
  double fg = layer.fg;
  EXPECT_GE(gshcpu_render_group_step(layer.l, bk, layer.nonisolated ? &ag : nullptr, &fg, layer.knockout ? 1 : 0, s, m, shape, op), 0);
  layer.ag = ag;
  layer.fg = fg;
}

/** The masks of the vectors as resources of a store (only their definition matters to the value: their pictures are drawn by the evaluator). */
struct Masks {
  vh::Bench bench;
  std::map<int, const GSHSCENE_Resource *> made;
  const GSHSCENE_Resource * of(int index) {
    auto found = made.find(index);
    if (found != made.end()) {
      return found->second;
    }
    const VMask & m = *kMasks[index];
    GSHSCENE_List * picture = nullptr;
    EXPECT_EQ(gshscene_list_create(nullptr, nullptr, bench.store, &picture), GSHSCENE_OK);
    GSHSCENE_Paint p{};
    p.kind = GSHSCENE_PAINT_SOLID;
    p.rgba[3] = 1.0;
    EXPECT_EQ(gshscene_list_fill(picture, bench.strip(64), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(picture), GSHSCENE_OK);
    const GSHSCENE_Resource * pr = nullptr;
    EXPECT_EQ(gshscene_store_intern_picture(bench.store, picture, &pr), GSHSCENE_OK);
    gshscene_list_destroy(picture);
    bench.held.push_back(pr);
    GSHSCENE_Mask content{};
    content.picture = pr;
    content.mode = (uint32_t)m.mode;
    content.coefficients = (uint32_t)m.coefficients;
    content.space = space_of(m.space);
    content.render_space = space_of(m.render_space);
    for (int i = 0; i < 3; i++) {
      content.backdrop[i] = m.backdrop[i];
    }
    content.backdrop_space = space_of(m.backdrop_space);
    content.acts_on = (uint32_t)m.acts_on;
    std::vector<GSHSCENE_Transfer_Piece> pieces;
    content.transfer.kind = (uint32_t)m.transfer.kind;
    content.transfer.c0 = m.transfer.c0;
    content.transfer.c1 = m.transfer.c1;
    content.transfer.n = m.transfer.n;
    content.transfer.sample_count = (size_t)m.transfer.sample_count;
    content.transfer.samples = m.transfer.samples;
    for (int k = 0; k < m.transfer.piece_count; k++) {
      const VPiece & q = m.transfer.pieces[k];
      GSHSCENE_Transfer_Piece out{};
      out.lo = q.lo;
      out.hi = q.hi;
      out.e0 = q.e0;
      out.e1 = q.e1;
      out.leaf_kind = (uint32_t)q.leaf_kind;
      out.c0 = q.c0;
      out.c1 = q.c1;
      out.n = q.n;
      out.sample_count = (size_t)q.sample_count;
      out.samples = q.samples;
      pieces.push_back(out);
    }
    content.transfer.piece_count = pieces.size();
    content.transfer.pieces = pieces.data();
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_mask(bench.store, &content, &r), GSHSCENE_OK) << index;
    bench.held.push_back(r);
    made[index] = r;
    return r;
  }
};

void run(Masks & masks, std::vector<Layer> & stack, const GSHSCENE_Color_Space & target, const VItem * items, int count);

/**
 * What a paint of the zero (target) space means inside a mask's picture when the mask's render space R is not the target's.  The sink
 * converts it from the render's target (every paint is: `source_of` reads the job's target space), and shape-scene's oracle, which
 * draws the picture as a render of its own whose target is R, reads it as already in R.  They agree wherever R is the target's space and
 * the picture's paints name no other, and the installed vectors hold 24 cases where they do not.  Which reading is the waist's is
 * not settled by MK2 ("drawn as its own isolated group in R"), so the test holds both: the sink's reading against the vectors where
 * the readings agree, the oracle's against every vector, and the number of vectors on which they differ.
 */
bool g_zero_space_is_r = false;

/** MK2: the mask's value at the pixel for a masked group popped into a layer of space @p parent; fills @p acts_on_shape. */
double mask_value(Masks & masks, int index, const GSHSCENE_Color_Space & parent, const GSHSCENE_Color_Space & target, bool * acts_on_shape) {
  GSHCPU_Mask_Eval me;
  EXPECT_EQ(gshcpu_render_mask_prepare(&me, masks.of(index), &parent), 1);
  *acts_on_shape = me.shape != 0;
  std::vector<Layer> stack(1);
  stack[0].space = me.frame_space; // the mask's picture is its own isolated, non-knockout group in the render space (MK2)
  run(masks, stack, g_zero_space_is_r ? me.frame_space : target, kMasks[index]->items, kMasks[index]->item_count);
  return gshcpu_render_mask_value(&me, stack[0].l, 0);
}

void run(Masks & masks, std::vector<Layer> & stack, const GSHSCENE_Color_Space & target, const VItem * items, int count) {
  for (int i = 0; i < count; i++) {
    const VItem & it = items[i];
    Layer & top = stack.back();
    if (it.kind == 0) {
      continue; // an empty picture
    }
    if (it.kind == 1) {
      double rgb[3] = {it.rgba[0], it.rgba[1], it.rgba[2]};
      GSHSCENE_Color_Space space = space_of(it.space);
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
      const GSHSCENE_Color_Space blend = space_of(it.blend);
      g.space = gshscene_color_space_is_target(&blend) ? top.space : blend;
      g.nonisolated = it.isolation != 0;
      g.knockout = it.knockout != 0;
      g.opacity = it.opacity;
      g.composite = (uint32_t)it.composite;
      g.outer = product(top.clips);
      g.mask = it.mask;
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
      double weight = done.outer;
      double v = 1.0;
      bool shape = false;
      if (done.mask >= 0) {
        v = mask_value(masks, done.mask, parent.space, target, &shape);
      }
      if (shape) {
        weight = weight * v; // MK7: the shape is weighed by v
        for (double & x : s) {
          x = done.opacity * x;
        }
      } else {
        const double ov = done.opacity * v; // MK7: the opacity source is o v
        for (double & x : s) {
          x = ov * x;
        }
      }
      const bool pdf = gshcpu_render_pdf_operator(done.composite) != 0;
      element(parent, s, weight, pdf ? weight * done.fg : weight, done.composite);
    }
  }
}

/** The vectors met by the primitives under the reading of a paint's zero space @p zero_is_r; the cases that are not, by name. */
int meet(bool zero_is_r, std::vector<std::string> * missed, int * shape_cases, int * knockout_parent, int * unbounded, vh::Worst * worst) {
  g_zero_space_is_r = zero_is_r;
  int exact = 0;
  for (const VCase & c : kCases) {
    Masks masks;
    const GSHSCENE_Color_Space target = space_of(c.target);
    std::vector<Layer> stack(1);
    std::memcpy(stack[0].l, c.backdrop, sizeof stack[0].l);
    stack[0].space = target;
    run(masks, stack, target, c.items, c.item_count);
    EXPECT_EQ(stack.size(), 1u) << c.name;
    bool close = true;
    for (int i = 0; i < 4; i++) {
      close = close && std::fabs(stack[0].l[i] - c.want[i]) <= 1e-12 * std::max(1.0, std::fabs(c.want[i]));
    }
    if (!close) {
      missed->push_back(c.name);
    } else if (worst) {
      vh::expect_pre(stack[0].l, c.want, c.exact, c.name, *worst);
      exact += c.exact ? 1 : 0;
    }
    const std::string name = c.name;
    if (shape_cases) {
      *shape_cases += name.find("SHAPE") != std::string::npos ? 1 : 0;
      *knockout_parent += name.find("knockout parent") != std::string::npos && name.find("not knockout parent") == std::string::npos ? 1 : 0;
      *unbounded += name.find("dest-in") != std::string::npos || name.find("src,") != std::string::npos ? 1 : 0;
    }
  }
  return exact;
}

TEST(RenderGroupMasks, ThePrimitivesMeetShapeScenesMaskVectorsUnderEveryParent) {
  vh::Worst worst;
  std::vector<std::string> missed;
  int shape_cases = 0;
  int knockout_parent = 0;
  int unbounded = 0;
  // Under the oracle's reading of a picture's paints every vector is met: the group step, the result, MK7's substitution into the
  // element and MK2's value are the waist's, under each of the four parents and every operator of the vectors.
  meet(true, &missed, &shape_cases, &knockout_parent, &unbounded, nullptr);
  EXPECT_TRUE(missed.empty()) << missed.size() << " of the vectors are not met, the first: " << (missed.empty() ? "" : missed[0]);
  // The values themselves, bit for bit where the vector says so.
  std::vector<std::string> again;
  const int exact = meet(true, &again, nullptr, nullptr, nullptr, &worst);
  EXPECT_EQ(exact, kExactCaseCount);
  EXPECT_GT(shape_cases, 90) << "the vectors that act on the shape";
  EXPECT_GT(knockout_parent, 50) << "under a knockout parent";
  std::printf("mask_vectors (shape-scene's) through the primitives: %d cases, %d exact; worst error %.3g (%s); %d acting on the shape, %d under a "
              "knockout parent, %d of an unbounded operator\n", kCaseCount, exact, worst.error, worst.where.c_str(), shape_cases, knockout_parent, unbounded);
}

TEST(RenderGroupMasks, TheSinksReadingOfAPaintsZeroSpaceInAMaskPictureDiffersFromTheOraclesOnlyWhereTheRenderSpaceIsNotTheTargets) {
  std::vector<std::string> missed;
  meet(false, &missed, nullptr, nullptr, nullptr, nullptr);
  std::printf("the sink's reading of a paint's zero space in a mask picture differs from the oracle's on %zu of %d vectors\n", missed.size(), kCaseCount);
  // Pinned so that a decision either way is seen: 24 vectors, every one a case whose render space is another than the target's.
  EXPECT_EQ(missed.size(), 24u);
  for (const std::string & name : missed) {
    EXPECT_TRUE(name.find("R = ") != std::string::npos || name.find("linear sRGB") != std::string::npos) << name;
  }
}

}  // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
