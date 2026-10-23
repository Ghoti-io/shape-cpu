/**
 * @file
 *
 * The dual render (strokes story S4c, C1 and C4): the same list drawn twice, once with the library's exact step and once
 * with the step of strokes story S4b kept in the tests (tests/support/exact_s4b.c), and the two targets compared byte for
 * byte.  The earlier step's value is the exact area less the roundings of its own procedure, so the two agree wherever that
 * error does not reach a level boundary, which is every cell of the committed fixtures; a difference here is a change of the
 * area's value in a cell the goldens commit, and stops the story that made it.  The gate is shown to see: a step that adds
 * one to every cell it handles changes the fixtures, and a step that adds one only to cells of at least 512 raw pieces
 * (the dense cells, which no fixture holds) changes none.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"

#include "../support/exact_s4b.h"

using namespace sh;

namespace {

/** The hook set for the life of the object; the library never sets it. */
struct HookGuard {
  explicit HookGuard(GSHCPU_Exact_Cell_Fn fn) {
    gshcpu_exact_hook = fn;
  }
  ~HookGuard() {
    gshcpu_exact_hook = nullptr;
  }
  HookGuard(const HookGuard &) = delete;
  HookGuard & operator=(const HookGuard &) = delete;
};

/** The earlier step with one added to the value of every cell it handles. */
int plus_one_everywhere(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  int r = gshcpu_s4b_exact_cell(params, s, cell, out, work);
  if (r == GSHCPU_EXACT_OK) {
    *out += 1;
  }
  return r;
}

/** ... only in a cell of at least 512 raw pieces in a part. */
int plus_one_in_dense_cells(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  int r = gshcpu_s4b_exact_cell(params, s, cell, out, work);
  size_t most = 0;
  for (int p = 0; p < params->parts; p++) {
    most = cell->count[p] > most ? cell->count[p] : most;
  }
  if (r == GSHCPU_EXACT_OK && most >= 512) {
    *out += 1;
  }
  return r;
}

struct Both {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::string bytes;
  GSHSCENE_Report_Summary summary{};
};

Both draw(size_t index, GSHCPU_Format format) {
  Fixture fx(index);
  Sink sink;
  RasterRef r(rf_raster(format, RF_WIDTH, RF_HEIGHT));
  ReportRef report;
  GSHSCENE_Target t = target_of(r.raster, &fx.ctx.target_map);
  GSHSCENE_Render_Request req = request_of(fx.list, &t, report.report);
  Both b;
  b.result = gshscene_sink_render(sink.sink, &req);
  b.bytes = r.bytes();
  b.summary = report.summary();
  b.summary.failure.chain = nullptr;
  return b;
}

const GSHCPU_Format kFormats[] = {GSHCPU_FORMAT_RGBA8_STRAIGHT, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED,
    GSHCPU_FORMAT_RGBAF32_STRAIGHT, GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED};

/** The number of (fixture, format) pairs whose target differs between the library's step and the hook @p fn. */
size_t differing(GSHCPU_Exact_Cell_Fn fn, size_t * compared) {
  size_t different = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    for (GSHCPU_Format format : kFormats) {
      Both a = draw(i, format);
      Both b;
      {
        HookGuard guard(fn);
        b = draw(i, format);
      }
      *compared += 1;
      bool same = a.result == b.result && a.bytes == b.bytes && a.summary.failure.reason == b.summary.failure.reason;
      if (!same) {
        different++;
        ADD_FAILURE() << RF_FIXTURES[i].name << " on " << gshcpu_format_string(format) << " differs between the steps"
                      << " (result " << a.result << " and " << b.result << ")";
      }
    }
  }
  return different;
}

}  // namespace

TEST(RenderDual, EveryFixtureOnEveryFormatIsTheSameByteForByteWithEitherStep) {
  size_t compared = 0;
  EXPECT_EQ(differing(gshcpu_s4b_exact_cell, &compared), 0u);
  EXPECT_EQ(compared, RF_FIXTURE_COUNT * 4u);
}

TEST(RenderDual, TheGateSeesAStepThatMovesEveryCellAndIsBlindToOneThatMovesOnlyCellsNoFixtureHolds) {
  // The control: one added to every cell changes the fixtures that have an exact cell; the same added only to cells of at
  // least 512 raw pieces (the largest fixture cell holds fewer) changes none, so a clean dual render of the fixtures says
  // nothing of dense cells, which is why the dense population is a gate of its own (test_render_dense.cpp).
  size_t compared = 0;
  size_t moved = 0;
  for (size_t i = 0; i < RF_FIXTURE_COUNT; i++) {
    Both a = draw(i, GSHCPU_FORMAT_RGBA8_STRAIGHT);
    Both b;
    {
      HookGuard guard(plus_one_everywhere);
      b = draw(i, GSHCPU_FORMAT_RGBA8_STRAIGHT);
    }
    Both c;
    {
      HookGuard guard(plus_one_in_dense_cells);
      c = draw(i, GSHCPU_FORMAT_RGBA8_STRAIGHT);
    }
    compared++;
    moved += a.bytes != b.bytes;
    EXPECT_EQ(a.bytes, c.bytes) << RF_FIXTURES[i].name << ": no fixture has a cell of 512 raw pieces";
  }
  EXPECT_GT(moved, 0u) << "the dual render could not see a step that adds one to every cell";
  EXPECT_EQ(compared, RF_FIXTURE_COUNT);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
