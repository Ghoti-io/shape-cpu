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
 * @file
 *
 * The filter fixture set (tests/filter_fixtures.h) and its goldens (tests/corpus/filter.hash, filter-fast.hash): every scene on every
 * format is its committed hash on both coverage tiers, none refuses or approximates anything, and the scenes between them use every
 * primitive of the filter engine in every mode the standard gives it (a primitive or a mode no scene draws is a rule the cross-target
 * hash cannot see).  The probe (tools/corpus-hash.c) prints the same hashes on every target.
 */

#include <gtest/gtest.h>

#include "../filter_fixtures.h"
#include "../render_helpers.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

using namespace rh;

namespace {

std::map<std::pair<std::string, std::string>, uint64_t> committed(const char * path) {
  std::map<std::pair<std::string, std::string>, uint64_t> out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string tag, scene, format, hex;
    if (words >> tag >> scene >> format >> hex && tag == "render") {
      out[{scene, format}] = std::stoull(hex, nullptr, 16);
    }
  }
  return out;
}

/** What every filter of every scene uses: the kinds, and each kind's modes and options. */
struct Census {
  std::set<uint32_t> kinds;
  std::set<uint32_t> blend_modes, composite_ops, matrix_types, morphology_ops, noise_kinds, light_kinds_diffuse, light_kinds_specular;
  std::set<uint32_t> blur_edges, convolve_edges, x_channels, y_channels, image_renderings;
  std::set<std::pair<uint32_t, uint32_t>> transfers; // (channel, type)
  std::set<uint32_t> region_modes, filter_spaces, primitive_spaces, preserve_alpha, stitch;
  std::set<size_t> merge_inputs;
  size_t cones = 0, subregions = 0, backdrop_groups = 0, filtered_groups = 0, results_used = 0, source_alpha_used = 0;
  bool box_blur = false, gaussian_blur = false, anisotropic = false, zero_axis = false, fractional_seed = false;
  bool dil_zero = false;
};

void tally(const GSHSCENE_Filter & f, Census * c) {
  c->region_modes.insert(f.region_mode);
  c->filter_spaces.insert(f.color_space);
  for (size_t i = 0; i < f.primitive_count; i++) {
    const GSHSCENE_Filter_Primitive & p = f.primitives[i];
    c->kinds.insert(p.kind);
    c->primitive_spaces.insert(p.color_space);
    if (p.subregion_mask) {
      c->subregions++;
    }
    for (size_t k = 0; k < p.input_count; k++) {
      c->results_used += p.inputs[k].kind == GSHSCENE_FILTER_INPUT_RESULT;
      c->source_alpha_used += p.inputs[k].kind == GSHSCENE_FILTER_INPUT_SOURCE_ALPHA;
    }
    switch (p.kind) {
      case GSHSCENE_FILTER_MERGE:
        c->merge_inputs.insert(p.input_count);
        break;
      case GSHSCENE_FILTER_BLEND:
        c->blend_modes.insert(p.mode);
        break;
      case GSHSCENE_FILTER_COMPOSITE:
        c->composite_ops.insert(p.mode);
        break;
      case GSHSCENE_FILTER_COLOR_MATRIX:
        c->matrix_types.insert(p.mode);
        break;
      case GSHSCENE_FILTER_COMPONENT_TRANSFER:
        for (uint32_t k = 0; k < 4; k++) {
          c->transfers.insert({k, p.transfer[k].type});
        }
        break;
      case GSHSCENE_FILTER_GAUSSIAN_BLUR:
        c->blur_edges.insert(p.edge_mode);
        // the engine's own threshold: below a deviation of 2 the true Gaussian, from 2 the box approximation
        c->box_blur = c->box_blur || p.sx >= 2.0 || p.sy >= 2.0;
        c->gaussian_blur = c->gaussian_blur || (p.sx > 0.0 && p.sx < 2.0) || (p.sy > 0.0 && p.sy < 2.0);
        c->anisotropic = c->anisotropic || p.sx != p.sy;
        c->zero_axis = c->zero_axis || p.sx == 0.0 || p.sy == 0.0;
        break;
      case GSHSCENE_FILTER_DROP_SHADOW:
        c->box_blur = c->box_blur || p.sx >= 2.0 || p.sy >= 2.0;
        c->gaussian_blur = c->gaussian_blur || (p.sx > 0.0 && p.sx < 2.0) || (p.sy > 0.0 && p.sy < 2.0);
        break;
      case GSHSCENE_FILTER_MORPHOLOGY:
        c->morphology_ops.insert(p.mode);
        c->dil_zero = c->dil_zero || (p.sx == 0.0 && p.sy == 0.0);
        break;
      case GSHSCENE_FILTER_CONVOLVE_MATRIX:
        c->convolve_edges.insert(p.edge_mode);
        c->preserve_alpha.insert(p.preserve_alpha);
        break;
      case GSHSCENE_FILTER_DISPLACEMENT_MAP:
        c->x_channels.insert(p.x_channel);
        c->y_channels.insert(p.y_channel);
        break;
      case GSHSCENE_FILTER_TURBULENCE:
        c->noise_kinds.insert(p.mode);
        c->stitch.insert(p.stitch_tiles);
        c->fractional_seed = c->fractional_seed || p.seed != (double)(long)p.seed;
        break;
      case GSHSCENE_FILTER_DIFFUSE_LIGHTING:
        c->light_kinds_diffuse.insert(p.light.kind);
        c->cones += p.light.has_cone;
        break;
      case GSHSCENE_FILTER_SPECULAR_LIGHTING:
        c->light_kinds_specular.insert(p.light.kind);
        c->cones += p.light.has_cone;
        break;
      case GSHSCENE_FILTER_IMAGE:
        c->image_renderings.insert(p.rendering);
        break;
      default:
        break;
    }
  }
}

/** Every filter a fixture's lists hold, through the groups that name them (a picture's inner list too). */
void census_of(const GSHSCENE_List * list, Census * c, int depth = 0) {
  ASSERT_LT(depth, 4);
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    const GSHSCENE_Item * it = nullptr;
    ASSERT_EQ(gshscene_list_item(list, i, &it), GSHSCENE_OK);
    if (it->kind == GSHSCENE_ITEM_GROUP_PUSH && it->group.filter) {
      GSHSCENE_Filter f;
      ASSERT_EQ(gshscene_resource_filter(it->group.filter, &f), GSHSCENE_OK);
      tally(f, c);
      c->filtered_groups++;
      c->backdrop_groups += it->group.filter_mode == GSHSCENE_FILTER_MODE_BACKDROP;
    }
  }
}

}  // namespace

TEST(FilterFixtures, EverySceneOnEveryFormatIsItsGoldenOnBothTiers) {
  const auto plain = committed("tests/corpus/filter.hash");
  const auto fast = committed("tests/corpus/filter-fast.hash");
  ASSERT_EQ(plain.size(), FF_FIXTURE_COUNT * 4u) << "tests/corpus/filter.hash is not read (run from the library's directory)";
  ASSERT_EQ(fast.size(), FF_FIXTURE_COUNT * 4u);
  GSHCPU_Limits fast_limits;
  gshcpu_limits_default(&fast_limits);
  fast_limits.fill_coverage = GSHCPU_FILL_COVERAGE_FAST;
  fast_limits.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
  for (size_t i = 0; i < FF_FIXTURE_COUNT; i++) {
    for (GSHCPU_Format format : RF_FORMATS) {
      const std::pair<std::string, std::string> key{FF_FIXTURES[i].name, gshcpu_format_string(format)};
      uint64_t h = 0;
      ASSERT_EQ(ff_render_with(i, format, nullptr, &h, nullptr), GSHSCENE_OK) << key.first;
      ASSERT_NE(plain.find(key), plain.end()) << key.first << " " << key.second;
      EXPECT_EQ(h, plain.at(key)) << key.first << " on " << key.second;
      ASSERT_EQ(ff_render_with(i, format, &fast_limits, &h, nullptr), GSHSCENE_OK) << key.first;
      ASSERT_NE(fast.find(key), fast.end());
      EXPECT_EQ(h, fast.at(key)) << key.first << " on " << key.second << " (fast coverages)";
    }
  }
}

TEST(FilterFixtures, NoSceneRefusesOrApproximatesAnythingAndEveryFilterIsDrawn) {
  for (size_t i = 0; i < FF_FIXTURE_COUNT; i++) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHSCENE_List * list = FF_FIXTURES[i].build(&ctx);
    ASSERT_EQ(ctx.failed, 0) << FF_FIXTURES[i].name;
    RasterRef r(rf_raster(GSHCPU_FORMAT_RGBA8_STRAIGHT, RF_WIDTH, RF_HEIGHT));
    SinkRef sink;
    ReportRef report;
    GSHSCENE_Target t = target_of(r.raster, &ctx.target_map);
    GSHSCENE_Render_Request req = request_of(list, &t, report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    ASSERT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK) << FF_FIXTURES[i].name;
    const GSHSCENE_Report_Summary sum = report.summary();
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_REFUSED], 0u) << FF_FIXTURES[i].name;
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_APPROXIMATED], 0u) << FF_FIXTURES[i].name;
    EXPECT_EQ(sum.units_done, sum.units_total) << FF_FIXTURES[i].name;
    EXPECT_EQ(gshscene_report_count(report.report), 0u) << FF_FIXTURES[i].name << " reports something";
    rf_release(&ctx);
  }
}

TEST(FilterFixtures, EveryPrimitiveAndEveryModeIsDrawnBySomeScene) {
  Census c;
  size_t scenes_with_filters = 0;
  for (size_t i = 0; i < FF_FIXTURE_COUNT; i++) {
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHSCENE_List * list = FF_FIXTURES[i].build(&ctx);
    ASSERT_EQ(ctx.failed, 0);
    const size_t before = c.filtered_groups;
    census_of(list, &c);
    // a picture's own list: its items are in the picture resource, found through the held resources
    for (size_t h = 0; h < ctx.held_count; h++) {
      if (gshscene_resource_kind(ctx.held[h]) == GSHSCENE_RESOURCE_PICTURE) {
        const GSHSCENE_List * inner = gshscene_resource_picture(ctx.held[h]);
        ASSERT_NE(inner, nullptr);
        census_of(inner, &c, 1);
      }
    }
    scenes_with_filters += c.filtered_groups > before;
    rf_release(&ctx);
  }
  EXPECT_EQ(scenes_with_filters, FF_FIXTURE_COUNT) << "a scene without a filter";

  for (uint32_t k = GSHSCENE_FILTER_OFFSET; k < GSHSCENE_FILTER_KIND_COUNT; k++) {
    EXPECT_TRUE(c.kinds.count(k)) << "no scene draws primitive kind " << k;
  }
  EXPECT_EQ(c.blend_modes.size(), 16u) << "the sixteen blend modes of the standard";
  EXPECT_TRUE(c.blend_modes.count(GSHSCENE_COMPOSITE_SRC_OVER));
  for (uint32_t m = GSHSCENE_COMPOSITE_SCREEN; m < GSHSCENE_COMPOSITE_COUNT; m++) {
    EXPECT_TRUE(c.blend_modes.count(m)) << "blend mode " << m;
  }
  for (uint32_t m = 0; m < GSHSCENE_FILTER_COMPOSITE_COUNT; m++) {
    EXPECT_TRUE(c.composite_ops.count(m)) << "composite operator " << m;
  }
  for (uint32_t m = 0; m < GSHSCENE_FILTER_MATRIX_COUNT; m++) {
    EXPECT_TRUE(c.matrix_types.count(m)) << "color matrix type " << m;
  }
  for (uint32_t ch = 0; ch < 4; ch++) {
    for (uint32_t t = 0; t < GSHSCENE_FILTER_TRANSFER_COUNT; t++) {
      EXPECT_TRUE(c.transfers.count({ch, t})) << "transfer function " << t << " on channel " << ch;
    }
  }
  for (uint32_t e = 0; e < GSHSCENE_FILTER_EDGE_COUNT; e++) {
    EXPECT_TRUE(c.blur_edges.count(e)) << "blur edge mode " << e;
    EXPECT_TRUE(c.convolve_edges.count(e)) << "convolve edge mode " << e;
  }
  EXPECT_TRUE(c.gaussian_blur) << "no true Gaussian (a deviation below 2)";
  EXPECT_TRUE(c.box_blur) << "no box approximation (a deviation from 2)";
  EXPECT_TRUE(c.anisotropic);
  EXPECT_TRUE(c.zero_axis);
  EXPECT_EQ(c.morphology_ops.size(), (size_t)GSHSCENE_FILTER_MORPHOLOGY_COUNT);
  EXPECT_TRUE(c.dil_zero) << "a morphology of radius zero";
  EXPECT_EQ(c.preserve_alpha.size(), 2u);
  for (uint32_t ch = 0; ch < GSHSCENE_FILTER_CHANNEL_COUNT; ch++) {
    EXPECT_TRUE(c.x_channels.count(ch)) << "displacement x channel " << ch;
    EXPECT_TRUE(c.y_channels.count(ch)) << "displacement y channel " << ch;
  }
  EXPECT_EQ(c.noise_kinds.size(), (size_t)GSHSCENE_FILTER_NOISE_COUNT);
  EXPECT_EQ(c.stitch.size(), 2u);
  EXPECT_TRUE(c.fractional_seed);
  for (uint32_t k = 0; k < GSHSCENE_FILTER_LIGHT_COUNT; k++) {
    EXPECT_TRUE(c.light_kinds_diffuse.count(k)) << "diffuse light " << k;
    EXPECT_TRUE(c.light_kinds_specular.count(k)) << "specular light " << k;
  }
  EXPECT_GE(c.cones, 4u) << "a spot with a cone under both lightings";
  EXPECT_TRUE(c.image_renderings.count(GSHSCENE_IMAGE_FILTER_NEAREST));
  EXPECT_EQ(c.region_modes.size(), 2u) << "an explicit and an automatic region";
  EXPECT_EQ(c.filter_spaces.size(), 2u) << "linearRGB and sRGB filters";
  EXPECT_TRUE(c.primitive_spaces.count(GSHSCENE_FILTER_SPACE_INHERIT));
  EXPECT_TRUE(c.primitive_spaces.count(GSHSCENE_FILTER_SPACE_SRGB)) << "a primitive that overrides the filter's space";
  EXPECT_TRUE(c.merge_inputs.count(2) && c.merge_inputs.count(3) && c.merge_inputs.count(4));
  EXPECT_GT(c.subregions, 0u);
  EXPECT_GT(c.results_used, 0u);
  EXPECT_GT(c.source_alpha_used, 0u);
  EXPECT_GT(c.backdrop_groups, 0u) << "a backdrop filter";
}

TEST(FilterFixtures, TheMapScenesUseTheRouteTheyAreNamedFor) {
  // the turned and sheared maps are not axis aligned (the canonical-space route); the quarter turn, the mirror and the scale are
  const char * names[5] = {"filter-map-rotate", "filter-map-skew", "filter-map-scale", "filter-map-quarter", "filter-map-mirror"};
  const bool canonical[5] = {true, true, false, false, false};
  for (size_t n = 0; n < 5; n++) {
    size_t index = FF_FIXTURE_COUNT;
    for (size_t i = 0; i < FF_FIXTURE_COUNT; i++) {
      if (std::string(FF_FIXTURES[i].name) == names[n]) {
        index = i;
      }
    }
    ASSERT_LT(index, FF_FIXTURE_COUNT) << names[n];
    RF_Ctx ctx;
    rf_begin(&ctx);
    GSHSCENE_List * list = FF_FIXTURES[index].build(&ctx);
    ASSERT_EQ(ctx.failed, 0);
    EXPECT_EQ(gshscene_filter_axis_aligned(&ctx.target_map) == 0, canonical[n]) << names[n];
    (void)list;
    rf_release(&ctx);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
