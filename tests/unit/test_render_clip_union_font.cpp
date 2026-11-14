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
 * A glyph run from a font as a member of a clip union and as an inner clip (shape-scene's clip_union.h, CU24): its region is what a plain
 * clip push of the run draws, so a font member beside a member that does not meet it is that clip's bytes, a clipped font member against
 * the same region made by nested clips, the bounds a union has (it covers each member and no more than their sum), the entries a part
 * makes naming it, the refusals, the caller's memory exactly measured, and every allocation failing.
 */

#include <gtest/gtest.h>

#include "../clipunion_helpers.h"

#include <ghoti.io/shape-scene/testing/glyph_test_provider.h>

#include <algorithm>
#include <vector>

using namespace cuh;

namespace {

constexpr uint32_t kW = 48;
constexpr uint32_t kH = 40;

/** A scene with a test provider's face and runs of it. */
struct FontScene {
  Scene s;
  GSHSCENE_Test_Provider tp{};
  const GSHSCENE_Resource * face = nullptr;
  FontScene() {
    tp.upem = 2048;
    tp.flags = GSHSCENE_FACE_HINTING;
    face = gshscene_tp_face(s.store, &tp, 1, 0x71, 0);
    s.held.push_back(face);
  }
  const GSHSCENE_Resource * run(std::vector<GSHSCENE_Glyph> glyphs, double size = 20.0, uint32_t mode = GSHSCENE_HINT_NONE) {
    GSHSCENE_Glyph_Run r{};
    r.source = GSHSCENE_GLYPHS_FROM_FONT;
    r.face = face;
    r.size = size;
    r.hint_mode = mode;
    r.glyph_count = glyphs.size();
    r.glyphs = glyphs.data();
    const GSHSCENE_Resource * out = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &r, &out), GSHSCENE_OK);
    s.held.push_back(out);
    return out;
  }
  /** A list that clips a white fill of the grid by @p clip (a union or a plain resource). */
  GSHSCENE_List * clipped(const GSHSCENE_Resource * clip) {
    return clipped_fill(s, clip, kW, kH);
  }
  /** A list with two plain clips, nested, around the fill. */
  GSHSCENE_List * nested(const GSHSCENE_Resource * outer, const GSHSCENE_Resource * inner) {
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint wh = white();
    EXPECT_EQ(gshscene_list_clip_push(l, outer, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_clip_push(l, inner, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, (double)kW, (double)kH), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  }
  /** The union of @p members as a resource. */
  const GSHSCENE_Resource * united(const std::vector<GSHSCENE_Clip_Member> & members) {
    return make_union(s, members);
  }
  size_t member_index(const GSHSCENE_Resource * u, const GSHSCENE_Resource * r) {
    for (size_t i = 0; i < gshscene_resource_clip_union_count(u); i++) {
      GSHSCENE_Clip_Member m{};
      gshscene_resource_clip_union_member(u, i, &m);
      if (m.resource == r) {
        return i;
      }
    }
    ADD_FAILURE() << "no such member";
    return 0;
  }
};

GSHSCENE_Clip_Inner inner_of(const GSHSCENE_Resource * r) {
  GSHSCENE_Clip_Inner in{};
  in.resource = r;
  in.rule = GSHGEOM_FILL_NONZERO;
  return in;
}

GSHSCENE_Clip_Member clipped_member(const GSHSCENE_Resource * r, const GSHSCENE_Clip_Inner * inner, size_t count) {
  GSHSCENE_Clip_Member m = member(r);
  m.inner = inner;
  m.inner_count = count;
  return m;
}

Options general_only() {
  Options o;
  o.tiers = GSHCPU_RENDER_TIERS_ALL & ~GSHCPU_RENDER_TIER_UNION;
  return o;
}

Options one_group() {
  Options o = general_only();
  o.tiers |= GSHCPU_RENDER_FORCE_ONE_GROUP;
  return o;
}

size_t inked(const Draw & d) {
  size_t n = 0;
  for (uint8_t a : alphas(d)) {
    n += a != 0;
  }
  return n;
}

}  // namespace

// A font member beside a member that does not meet it: each pixel has one member's area, so the union is the byte of that member's own
// plain clip.  The font member is never the fast tier's (its box is not the store's to form), and its entries are the plain run's.
TEST(RenderClipUnionFont, AFontMemberBesideAnotherThatDoesNotMeetItIsTheBytesOfThePlainClips) {
  FontScene f;
  const GSHSCENE_Resource * text = f.run({{1, 4.0, 30.0, nullptr}, {2, 16.0, 30.0, nullptr}});
  const GSHSCENE_Resource * box = f.s.rect(33.25, 5.5, 44.0, 14.75);
  const GSHSCENE_Resource * u = f.united({member(text), member(box)});
  const Draw by_default = draw(f.clipped(u), kW, kH);
  const Draw forced = draw(f.clipped(u), kW, kH, general_only());
  const Draw one = draw(f.clipped(u), kW, kH, one_group());
  const Draw a = draw(f.clipped(text), kW, kH);
  const Draw b = draw(f.clipped(box), kW, kH);
  ASSERT_EQ(by_default.result, GSHSCENE_OK);
  ASSERT_EQ(forced.result, GSHSCENE_OK);
  ASSERT_EQ(one.result, GSHSCENE_OK);
  EXPECT_EQ(by_default.counters.union_fast, 0u) << "a font member is unprovable";
  EXPECT_EQ(by_default.counters.union_general, 1u);
  EXPECT_EQ(by_default.count(GSHSCENE_ENTRY_REFUSED), 0u);
  EXPECT_GT(inked(a), 50u);
  EXPECT_GT(inked(b), 50u);
  for (uint32_t y = 0; y < kH; y++) {
    for (uint32_t x = 0; x < kW; x++) {
      const uint8_t pa = a.at8(x, y, 3), pb = b.at8(x, y, 3);
      ASSERT_TRUE(pa == 0 || pb == 0) << "the members meet at (" << x << ", " << y << ")";
      EXPECT_EQ(by_default.at8(x, y, 3), std::max(pa, pb)) << "(" << x << ", " << y << ")";
      EXPECT_EQ(forced.at8(x, y, 3), by_default.at8(x, y, 3)) << "(" << x << ", " << y << ")";
      EXPECT_EQ(one.at8(x, y, 3), by_default.at8(x, y, 3)) << "(" << x << ", " << y << ")";
    }
  }
}

// A union is at least each of its members and at most their sum, whatever the groups (CG2): two overlapping font members.
TEST(RenderClipUnionFont, OverlappingFontMembersCoverEachAndNoMoreThanTheirSum) {
  FontScene f;
  const GSHSCENE_Resource * one_glyph = f.run({{2, 6.0, 30.0, nullptr}});
  const GSHSCENE_Resource * shifted = f.run({{2, 9.0, 27.0, nullptr}});
  const GSHSCENE_Resource * u = f.united({member(one_glyph), member(shifted)});
  const Draw d = draw(f.clipped(u), kW, kH);
  const Draw a = draw(f.clipped(one_glyph), kW, kH);
  const Draw b = draw(f.clipped(shifted), kW, kH);
  const Draw g = draw(f.clipped(u), kW, kH, one_group());
  ASSERT_EQ(d.result, GSHSCENE_OK);
  size_t overlap = 0;
  for (uint32_t y = 0; y < kH; y++) {
    for (uint32_t x = 0; x < kW; x++) {
      const unsigned pa = a.at8(x, y, 3), pb = b.at8(x, y, 3), pu = d.at8(x, y, 3);
      EXPECT_GE(pu, std::max(pa, pb)) << "(" << x << ", " << y << ")";
      EXPECT_LE(pu, std::min(255u, pa + pb + 1u)) << "(" << x << ", " << y << ")"; // two floors of areas can be one under the floor of the sum
      EXPECT_EQ(g.at8(x, y, 3), pu) << "one group, (" << x << ", " << y << ")";
      overlap += pa && pb;
    }
  }
  EXPECT_GT(overlap, 10u) << "the members overlap";
}

// A clipped font member is the same region as nested clips when the inner clip is aligned to the pixels (nested coverage is a product,
// exact where one factor is 0 or 255).
TEST(RenderClipUnionFont, AFontMemberWithAnInnerClipIsTheNestedClipsRegion) {
  FontScene f;
  const GSHSCENE_Resource * text = f.run({{1, 4.0, 30.0, nullptr}, {2, 16.0, 30.0, nullptr}});
  const GSHSCENE_Resource * cut = f.s.rect(0, 0, 14, 40);  // the first glyph's pixels and a part of the second's
  const GSHSCENE_Clip_Inner in = inner_of(cut);
  const GSHSCENE_Resource * u = f.united({clipped_member(text, &in, 1)});
  const Draw d = draw(f.clipped(u), kW, kH);
  const Draw n = draw(f.nested(text, cut), kW, kH);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  ASSERT_EQ(n.result, GSHSCENE_OK);
  EXPECT_EQ(d.counters.union_general, 1u);
  EXPECT_GT(inked(d), 50u);
  EXPECT_LT(inked(d), inked(draw(f.clipped(text), kW, kH)));
  EXPECT_EQ(alphas(d), alphas(n));
}

// A font run as the inner clip of a path member: the same region again.
TEST(RenderClipUnionFont, AFontRunAsAnInnerClipIsTheNestedClipsRegion) {
  FontScene f;
  const GSHSCENE_Resource * text = f.run({{1, 4.0, 30.0, nullptr}, {2, 16.0, 30.0, nullptr}});
  const GSHSCENE_Resource * band = f.s.rect(0, 20, 48, 40);  // the lower half, aligned
  const GSHSCENE_Clip_Inner in = inner_of(text);
  const GSHSCENE_Resource * u = f.united({clipped_member(band, &in, 1)});
  const Draw d = draw(f.clipped(u), kW, kH);
  const Draw n = draw(f.nested(band, text), kW, kH);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  EXPECT_GT(inked(d), 50u);
  EXPECT_EQ(alphas(d), alphas(n));
  // and with a second member the union is the two regions' union
  const GSHSCENE_Resource * box = f.s.rect(33.25, 5.5, 44.0, 14.75);
  const GSHSCENE_Resource * u2 = f.united({clipped_member(band, &in, 1), member(box)});
  const Draw d2 = draw(f.clipped(u2), kW, kH);
  const Draw b = draw(f.clipped(box), kW, kH);
  ASSERT_EQ(d2.result, GSHSCENE_OK);
  for (uint32_t y = 0; y < kH; y++) {
    for (uint32_t x = 0; x < kW; x++) {
      ASSERT_TRUE(n.at8(x, y, 3) == 0 || b.at8(x, y, 3) == 0);
      EXPECT_EQ(d2.at8(x, y, 3), std::max(n.at8(x, y, 3), b.at8(x, y, 3))) << "(" << x << ", " << y << ")";
    }
  }
}

// The entries of a part: an unavailable glyph, named by the part and its glyph; a refused glyph under strict stops at the push.
TEST(RenderClipUnionFont, AGlyphTheProviderCannotGiveIsReportedWithItsPartAndTheRestIsDrawn) {
  FontScene f;
  const GSHSCENE_Resource * text = f.run({{1, 4.0, 30.0, nullptr}, {9, 12.0, 30.0, nullptr}, {2, 20.0, 30.0, nullptr}});
  const GSHSCENE_Resource * band = f.s.rect(0, 0, 48, 12);
  const GSHSCENE_Clip_Inner in = inner_of(text);
  for (int as_inner = 0; as_inner < 2; as_inner++) {
    const GSHSCENE_Resource * u = as_inner ? f.united({clipped_member(f.s.rect(0, 0, 48, 40), &in, 1), member(band)})
                                           : f.united({member(text), member(band)});
    const Draw d = draw(f.clipped(u), kW, kH);
    ASSERT_EQ(d.result, GSHSCENE_OK);
    size_t found = 0;
    for (const GSHSCENE_Report_Entry & e : d.entries) {
      if (e.reason == GSHSCENE_REASON_GLYPH_UNAVAILABLE) {
        found++;
        EXPECT_EQ(e.second, 1u);
        EXPECT_EQ(e.member, as_inner ? GSHSCENE_REPORT_INNER(f.member_index(u, f.s.rect(0, 0, 48, 40)), 0)
                                     : GSHSCENE_REPORT_MEMBER(f.member_index(u, text)));
        EXPECT_EQ(e.chain_count, 1u);
      }
    }
    EXPECT_EQ(found, 1u) << as_inner;
    EXPECT_GT(inked(d), 50u) << "the rest of the run is drawn";
  }
  // strict: the refused glyph (the program of glyph 5 fails under FONT_STRICT) stops the render at the push
  const GSHSCENE_Resource * strict_text = f.run({{1, 4.0, 30.0, nullptr}, {5, 12.0, 30.0, nullptr}}, 20.0, GSHSCENE_HINT_FONT_STRICT);
  const GSHSCENE_Resource * u = f.united({member(strict_text), member(band)});
  Options o;
  o.flags = GSHSCENE_RENDER_STRICT;
  const Draw d = draw(f.clipped(u), kW, kH, o);
  EXPECT_EQ(d.result, GSHSCENE_ERR_REFUSED);
  size_t refused = 0;
  for (const GSHSCENE_Report_Entry & e : d.entries) {
    refused += e.member != 0 && e.entry_class == GSHSCENE_ENTRY_REFUSED && e.second == 1u;
  }
  EXPECT_EQ(refused, 1u);
}

// A part whose face is unbound refuses the push (its block is skipped: nothing is drawn); a sink with the font capability off would too.
TEST(RenderClipUnionFont, APartWithAnUnboundFaceRefusesThePushNamingIt) {
  FontScene f;
  GSHSCENE_Face_Desc desc{};
  desc.font_key = 0x99;
  const GSHSCENE_Resource * bare = nullptr;
  ASSERT_EQ(gshscene_store_intern_face(f.s.store, &desc, &bare), GSHSCENE_OK);
  f.s.held.push_back(bare);
  GSHSCENE_Glyph_Run r{};
  GSHSCENE_Glyph g{1, 4.0, 30.0, nullptr};
  r.source = GSHSCENE_GLYPHS_FROM_FONT;
  r.face = bare;
  r.size = 20.0;
  r.glyph_count = 1;
  r.glyphs = &g;
  const GSHSCENE_Resource * text = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(f.s.store, &r, &text), GSHSCENE_OK);
  f.s.held.push_back(text);
  const GSHSCENE_Resource * u = f.united({member(text), member(f.s.rect(30, 5, 40, 15))});
  const Draw d = draw(f.clipped(u), kW, kH);
  ASSERT_EQ(d.result, GSHSCENE_OK);
  ASSERT_EQ(d.count(GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_FACE_UNBOUND), 1u);
  for (const GSHSCENE_Report_Entry & e : d.entries) {
    if (e.reason == GSHSCENE_REASON_FACE_UNBOUND) {
      EXPECT_EQ(e.member, GSHSCENE_REPORT_MEMBER(f.member_index(u, text)));
    }
  }
  EXPECT_EQ(inked(d), 0u) << "the block of a refused push is skipped";
}

// The caller's memory is exactly what the render takes (CG8, CU24): a font member forms its box from its outlines in the plan, in the
// measure and in the draw alike.
TEST(RenderClipUnionFont, TheCallersMemoryIsExactlyMeasuredForAFontMember) {
  FontScene f;
  const GSHSCENE_Resource * text = f.run({{1, 4.0, 30.0, nullptr}, {2, 16.0, 30.0, nullptr}});
  const GSHSCENE_Resource * cut = f.s.rect(0, 0, 14, 40);
  const GSHSCENE_Clip_Inner in = inner_of(cut);
  const GSHSCENE_Resource * u = f.united({clipped_member(text, &in, 1), member(f.s.rect(33.25, 5.5, 44.0, 14.75))});
  GSHSCENE_List * l = f.clipped(u);
  const Draw plain = draw(l, kW, kH);
  Options o;
  o.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  const Draw mem = draw(l, kW, kH, o);
  ASSERT_EQ(plain.result, GSHSCENE_OK);
  ASSERT_EQ(mem.result, GSHSCENE_OK);
  EXPECT_GT(mem.need, 0u);
  EXPECT_EQ(mem.bytes, plain.bytes);
}

// Every allocation of a render of a union with font parts can fail: the render is out of memory, and nothing leaks.
TEST(RenderClipUnionFont, EveryAllocationCanFailAndNothingLeaks) {
  FontScene f;
  const GSHSCENE_Resource * text = f.run({{1, 4.0, 30.0, nullptr}, {9, 12.0, 30.0, nullptr}, {2, 20.0, 30.0, nullptr}});
  const GSHSCENE_Resource * cut = f.s.rect(0, 0, 30, 40);
  const GSHSCENE_Clip_Inner in = inner_of(cut);
  const GSHSCENE_Resource * u = f.united({clipped_member(text, &in, 1), member(f.s.rect(33.25, 5.5, 44.0, 14.75))});
  GSHSCENE_List * l = f.clipped(u);
  bool finished = false;
  long ooms = 0;
  for (long k = 0; k < 100000 && !finished; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    {
      RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, kW, kH));
      SinkRef sink(nullptr, &a);
      ReportRef report;
      GSHSCENE_Target target = target_of(r.raster);
      GSHSCENE_Render_Request req = request_of(l, &target, report.report);
      t.fail_from = t.calls + k;
      const GSHSCENE_Result rc = gshscene_sink_render(sink.sink, &req);
      t.fail_from = -1;
      if (rc == GSHSCENE_ERR_OOM) {
        ooms++;
      } else {
        ASSERT_EQ(rc, GSHSCENE_OK) << k;
        finished = true;
      }
      for (const GSHSCENE_Report_Entry & e : report.entries()) {
        EXPECT_NE(e.reason, GSHSCENE_REASON_RESOURCE_LIMIT) << "k " << k << ": out of memory is not a limit";
      }
    }
    EXPECT_TRUE(t.live.empty()) << "k " << k << " leaked";
  }
  EXPECT_TRUE(finished);
  EXPECT_GT(ooms, 0);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
