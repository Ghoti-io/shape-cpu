/**
 * @file
 *
 * The fast fill coverage (GSHCPU_FILL_COVERAGE_FAST, lean.c) against the exact one on every fixture of the sink's tests: fills
 * under both rules, clips, groups, gradients, glyph outlines, and strokes with a gradient.  The fast coverage differs only where
 * pieces of an outline overlap and by the rounding of a level, so a fixture's picture must stay within a few levels of the
 * exact one on the whole, and the render must succeed as the exact one does.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "render_helpers.h"

#include <algorithm>
#include <cstdlib>

using namespace rh;

namespace {

struct Picture {
  std::vector<uint8_t> bytes;
  GSHSCENE_Result result = GSHSCENE_OK;
};

Picture draw(size_t index, uint64_t fill, uint64_t stroke, GSHCPU_Format format = GSHCPU_FORMAT_RGBA8_PREMULTIPLIED) {
  Picture out;
  Fixture fx(index);
  GIMG_Raster * raster = rf_raster(format, RF_WIDTH, RF_HEIGHT);
  EXPECT_NE(raster, nullptr);
  RasterRef keep(raster);
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.fill_coverage = fill;
  limits.stroke_coverage = stroke;
  SinkRef sink(&limits);
  ReportRef report;
  GSHSCENE_Target target = target_of(raster, &fx.ctx.target_map);
  GSHSCENE_Render_Request request = request_of(fx.list, &target, report.report);
  out.result = gshscene_sink_render(sink.sink, &request);
  const std::string s = keep.bytes();
  out.bytes.assign(s.begin(), s.end());
  return out;
}

}  // namespace

void every_fixture_stays_close(GSHCPU_Format format) {
  size_t differing = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Picture exact = draw(i, GSHCPU_FILL_COVERAGE_EXACT, GSHCPU_STROKE_COVERAGE_EXACT, format);
    Picture fast = draw(i, GSHCPU_FILL_COVERAGE_FAST, GSHCPU_STROKE_COVERAGE_FAST, format);
    ASSERT_EQ(exact.bytes.size(), fast.bytes.size()) << RF_FIXTURES[i].name;
    EXPECT_EQ(exact.result, fast.result) << RF_FIXTURES[i].name;
    double sum = 0;
    double steps = 0;      // the levels of the bytes that differ by a step of the paint
    size_t step_bytes = 0;
    int worst = 0;
    for (size_t k = 0; k < exact.bytes.size(); k++) {
      int d = std::abs((int)exact.bytes[k] - (int)fast.bytes[k]);
      sum += d;
      if (d > 64) {
        steps += d;
        step_bytes++;
      }
      worst = std::max(worst, d);
    }
    const double mean = exact.bytes.empty() ? 0.0 : sum / (double)exact.bytes.size();
    // A pixel exactly on a hard step of a gradient (an angle the arctangent only approximates, an offset of equal stops) may
    // land on the other side of it, which is a step, not a rounding: those bytes are held to a fraction of the picture and
    // the rest of it to the mean.
    EXPECT_LT((sum - steps) / (double)exact.bytes.size(), 1.5) << RF_FIXTURES[i].name << " (worst " << worst << ")";
    EXPECT_LT(step_bytes, exact.bytes.size() / 50) << RF_FIXTURES[i].name << " (mean " << mean << ")";
    differing += sum > 0 ? 1 : 0;
  }
  EXPECT_GT(differing, RF_FIXTURE_COUNT / 2) << "the fast coverage was taken: its rounding shows in most pictures";
}

TEST(RenderFastFill, EveryFixtureStaysWithinAFewLevelsOfTheExactCoverage) {
  every_fixture_stays_close(GSHCPU_FORMAT_RGBA8_PREMULTIPLIED);
}

TEST(RenderFastFill, EveryFixtureStaysWithinAFewLevelsOnAStraightTarget) {
  every_fixture_stays_close(GSHCPU_FORMAT_RGBA8_STRAIGHT);
}

TEST(RenderFastFill, TheFillCoverageIsRefusedPastTheDefinedValues) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  EXPECT_EQ(limits.fill_coverage, GSHCPU_FILL_COVERAGE_EXACT);
  limits.fill_coverage = GSHCPU_FILL_COVERAGE_FAST + 1;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_sink_create(nullptr, nullptr, &limits, &sink), GSHCPU_ERR_INVALID);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
