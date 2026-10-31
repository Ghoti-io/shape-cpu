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
 * The gate of the mask's value in whole renders (masks-images-patterns story M2; render.h, MC3 and MC4): this library's
 * `tests/mask_vectors.h` (tools/gen-mask-vectors.py, exact rationals, a third restatement of the rules that shares no
 * code with the library or with shape-scene's oracle) against masked groups drawn on a one-pixel target of each format:
 * alpha and luminance, every transfer kind, every coefficient set, the picture and the luminance in different spaces, a
 * backdrop, the outside value, what the mask acts on under every operator, a mask of a mask.
 *
 * Before the write a value is within 1e-12 of the oracle and equal to it where the case is exact (STOP-2: P3's rule,
 * carried); after it, the stored value is the oracle's rounded the same way, or the other side where the oracle's is within
 * 1e-9 of the boundary (D7).
 */

#include <gtest/gtest.h>

#include "mask_vectors.h"
#include "vector_helpers.h"

#include <cstdio>
#include <map>

using namespace vh;

namespace {

GSHSCENE_Color_Space space_of(const mask_vectors::MSpace & s) {
  return vh::space_of(s.primaries, s.transfer, s.gamma);
}

/** The case's lists, built in a store of its own: each mask once, the pictures and the masks it needs held. */
struct Built {
  Bench bench;
  std::map<int, const GSHSCENE_Resource *> masks;

  /** Record @p items into @p list: fills and clips as strips, groups with their mask. */
  void record(GSHSCENE_List * list, const mask_vectors::MItem * items, int count) {
    for (int i = 0; i < count; i++) {
      const mask_vectors::MItem & it = items[i];
      if (it.kind == 1) {
        GSHSCENE_Paint p{};
        if (it.paint == 2) {
          p.kind = GSHSCENE_PAINT_FOREGROUND;
          p.rgba[3] = it.rgba[3];
        } else {
          p.kind = GSHSCENE_PAINT_SOLID;
          std::memcpy(p.rgba, it.rgba, sizeof p.rgba);
          p.space = space_of(it.space);
        }
        ASSERT_EQ(gshscene_list_fill(list, bench.strip(it.width), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
      } else if (it.kind == 2) {
        ASSERT_EQ(gshscene_list_clip_push(list, bench.strip(it.width), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
      } else if (it.kind == 3) {
        ASSERT_EQ(gshscene_list_clip_pop(list), GSHSCENE_OK);
      } else if (it.kind == 4) {
        GSHSCENE_Group g{};
        g.opacity = it.opacity;
        g.blend_space = space_of(it.blend);
        g.composite = (uint32_t)it.composite;
        if (it.mask >= 0) {
          g.soft_mask = mask(it.mask);
        }
        ASSERT_EQ(gshscene_list_group_push(list, &g, 0, 0.0), GSHSCENE_OK);
      } else {
        ASSERT_EQ(gshscene_list_group_pop(list), GSHSCENE_OK);
      }
    }
  }

  /** The mask resource of kMasks[index]: its picture first, interned once. */
  const GSHSCENE_Resource * mask(int index) {
    auto found = masks.find(index);
    if (found != masks.end()) {
      return found->second;
    }
    const mask_vectors::MMask & m = mask_vectors::kMasks[index];
    GSHSCENE_List * picture = nullptr;
    EXPECT_EQ(gshscene_list_create(nullptr, nullptr, bench.store, &picture), GSHSCENE_OK);
    record(picture, m.items, m.item_count);
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
    content.render_space = space_of(m.render);
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
      const mask_vectors::MPiece & p = m.transfer.pieces[k];
      GSHSCENE_Transfer_Piece out{};
      out.lo = p.lo;
      out.hi = p.hi;
      out.e0 = p.e0;
      out.e1 = p.e1;
      out.leaf_kind = (uint32_t)p.leaf;
      out.c0 = p.c0;
      out.c1 = p.c1;
      out.n = p.n;
      out.sample_count = (size_t)p.sample_count;
      out.samples = p.samples;
      pieces.push_back(out);
    }
    content.transfer.piece_count = pieces.size();
    content.transfer.pieces = pieces.data();
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_mask(bench.store, &content, &r), GSHSCENE_OK) << index;
    bench.held.push_back(r);
    masks[index] = r;
    return r;
  }
};

TEST(RenderMaskVectors, MaskedGroupsMeetTheMaskVectorsOnEveryFormat) {
  using namespace mask_vectors;
  Worst worst;
  int exact = 0;
  int near = 0;
  int touched = 0;
  for (const MCase & c : kCases) {
    for (const MFormat & f : c.formats) {
      Built b;
      GSHSCENE_Color fg{};
      std::memcpy(fg.rgba, c.fg, sizeof fg.rgba);
      fg.space = space_of(c.fg_space);
      ASSERT_EQ(gshscene_list_set_foreground(b.bench.list, &fg), GSHSCENE_OK);
      b.record(b.bench.list, c.items, c.item_count);
      ASSERT_EQ(gshscene_list_close(b.bench.list), GSHSCENE_OK) << c.name;
      GSHCPU_Format format = (GSHCPU_Format)f.format;
      GIMG_Raster * raster = one_pixel(format);
      bool wide = gshcpu_render_pixel_bytes(format) == 16u;
      unsigned char * px = (unsigned char *)gimg_raster_pixels(raster);
      for (int i = 0; i < 4; i++) {
        if (wide) {
          std::memcpy(px + 4 * i, &f.backdrop[i], 4);
        } else {
          px[i] = (unsigned char)f.backdrop[i];
        }
      }
      std::string where = std::string(c.name) + " on " + gshcpu_format_string(format);
      Seen seen = render(b.bench.list, raster, space_of(c.target));
      EXPECT_EQ(seen.touched, f.touched ? 1 : 0) << where;
      expect_pre(seen.root, f.pre, f.exact, where, worst);
      exact += f.exact ? 1 : 0;
      touched += f.touched ? 1 : 0;
      for (int i = 0; i < 4; i++) {
        uint32_t got;
        if (wide) {
          std::memcpy(&got, px + 4 * i, 4);
        } else {
          got = px[i];
        }
        near += f.want[i] != f.alt[i] ? 1 : 0;
        EXPECT_TRUE(got == f.want[i] || (!f.exact && got == f.alt[i]))
            << where << " channel " << i << ": stored " << got << ", want " << f.want[i] << " (or " << f.alt[i] << ")";
      }
      gimg_raster_destroy(raster);
    }
  }
  EXPECT_EQ(exact, kExactCount);
  EXPECT_GE(touched, kCaseCount) << "the cases draw";
  std::printf("mask_vectors: %d cases on 4 formats, %d exact, %d stored values within 1e-9 of a boundary; worst error %.3g (%s)\n",
      kCaseCount, exact, near, worst.error, worst.where.c_str());
}

} // namespace

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
