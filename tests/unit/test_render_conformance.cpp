/**
 * @file
 *
 * shape-scene's sink conformance suite (story 13a, S1 to S16), run unchanged
 * against shape-cpu (story 13b).  The suite is the text shape-scene installs
 * (<ghoti.io/shape-scene/testing/sink_conformance.h>), so it is the very suite
 * the test sink passes; every expectation is read from shape-cpu's own caps.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/shape-scene/testing/sink_conformance.h>

#include <ghoti.io/shape-cpu/shape-cpu.h>

namespace {

GSHSCENE_Sink * make_cpu(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Sink * sink = nullptr;
  if (gshcpu_sink_create(alloc, limits, nullptr, &sink) != GSHCPU_OK) {
    return nullptr;
  }
  return sink;
}

/** shape-cpu with its cell cap at @p level (S4's refusal rule): the cap is the limit an item can meet in part. */
GSHSCENE_Sink * make_cpu_limited(const GSHSCENE_Allocator * alloc, unsigned level) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_deposits = level;
  GSHSCENE_Sink * sink = nullptr;
  if (gshcpu_sink_create(alloc, nullptr, &limits, &sink) != GSHCPU_OK) {
    return nullptr;
  }
  return sink;
}

const sinkconf::Fixture kCpu{"shape_cpu", make_cpu, {GSHSCENE_TARGET_RASTER}, nullptr, make_cpu_limited};

} // namespace

INSTANTIATE_TEST_SUITE_P(ShapeCpu, SinkConformance, ::testing::Values(&kCpu));

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
