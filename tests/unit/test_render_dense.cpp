/**
 * @file
 *
 * The sweep of the exact step on dense cells (strokes story S4c, UC13 to UC20): equal copies as one fragment, the slab's
 * order carried and repaired, the meetings in a heap, the area accumulated at the meetings by the exact antiderivative, the
 * checks, the crossings budget, and the winding that is no longer a reason to refuse.  Every cell of the populations here is
 * measured against instruments that know nothing of the sweep (tests/dense_cells.h: the area from all the pairwise
 * meetings in `long double`, the meetings by testing every pair) and, where the earlier step is exact enough, against it.
 * The step runs with its self-checks on throughout (`gshcpu_exact_check`): the order of every slab sorted at its end, and
 * the meetings it made equal to the meetings the count found.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "../dense_cells.h"
#include "../dense_vectors.h"

#include "../support/exact_s4b.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

#include <time.h>

using namespace dense;

namespace {

/** The scratch of the step for cells of up to @p cap pieces in each of @p parts parts. */
struct Step {
  std::vector<unsigned char> memory;
  GSHCPU_Exact_Scratch scratch;
  explicit Step(size_t cap, int parts = 3) {
    memory.resize(gshcpu_exact_scratch_bytes(cap, parts) + 16);
    gshcpu_exact_scratch_init(&scratch, (void *)(((uintptr_t)memory.data() + 15u) & ~(uintptr_t)15u), cap, parts);
  }
  struct Result {
    int status = -1;
    int64_t out = 0;
    uint64_t work = 0;
    uint64_t events = 0;
    uint64_t crossings = 0;
    uint64_t distinct = 0;
    int64_t winding = 0;
  };
  Result run(const Cell & c, uint64_t budget = 0) {
    GSHCPU_Exact_Params p = c.params;
    p.max_crossings = budget;
    GSHCPU_Exact_Cell v = c.view();
    Result r;
    r.status = gshcpu_exact_cell(&p, &scratch, &v, &r.out, &r.work);
    r.events = scratch.last_events;
    r.crossings = scratch.last_crossings;
    r.distinct = scratch.last_distinct;
    r.winding = scratch.last_wmax;
    return r;
  }
};

/** The step's rounding of the exact quantity differs from the ideal rounding by at most this, in doubled units (UC15). */
long double slack_for(uint64_t events, uint64_t end_evals) {
  return (long double)(8 * events + 2 * end_evals + 1) / (long double)(1u << 30) * 2.0L + 1e-9L;
}

/** Whether the step's @p r equals the rounding of @p c's exact value, allowing the one-unit slip at a tie. */
::testing::AssertionResult equals_reference(Step & step, const Cell & c, const char * what) {
  Step::Result r = step.run(c);
  if (r.status != GSHCPU_EXACT_OK) {
    return ::testing::AssertionFailure() << what << ": status " << r.status;
  }
  long double slack = slack_for(r.events, step.scratch.last_end_evals);
  Expected e = expected_of(c, slack);
  int64_t diff = r.out - e.rounded;
  if (diff == 0 || (e.near_tie && (diff == 1 || diff == -1))) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << what << ": the step gave " << r.out << ", the exact value is " << (double)e.exact
                                       << " (rounds to " << e.rounded << "), " << c.total() << " pieces";
}

}  // namespace

/* --- UC13 to UC16: the cells ------------------------------------------------------------------------------------------ */

TEST(RenderDense, RandomCellsOfOnePartAreTheExactAreaAndTheirMeetingsAreCounted) {
  Step step(80, 1);
  size_t cells = 0;
  size_t with_crossings = 0;
  for (uint32_t seed = 1; seed <= 400; seed++) {
    size_t n = 3 + seed % 45;
    Cell c = random_cell(n, seed, 1 + (int)(seed % 4));
    EXPECT_TRUE(equals_reference(step, c, "random cell")) << "seed " << seed;
    Step::Result r = step.run(c);
    size_t distinct = 0;
    uint64_t k = reference_crossings(c, &distinct);
    EXPECT_EQ(r.events, k) << "seed " << seed << ": the meetings the sweep made";
    EXPECT_LE(r.work, gshcpu_exact_cell_work_bound(c.total(), k)) << "seed " << seed << ": the work is within W_cell (UC17)";
    EXPECT_EQ(r.distinct, distinct) << "seed " << seed << ": the fragments left once the seams cancel";
    cells++;
    with_crossings += k > 0;
  }
  EXPECT_EQ(cells, 400u);
  EXPECT_GT(with_crossings, 300u) << "the population crosses itself";
}

TEST(RenderDense, CellsOfThreePartsAreTheExactAreaOfUC8sRegionUnderBothRulesAndTheComplement) {
  Step step(60, 3);
  for (uint32_t seed = 1; seed <= 120; seed++) {
    Cell c = random_three(2 + seed % 12, seed, (int)(seed & 1), (int)((seed >> 1) & 1));
    EXPECT_TRUE(equals_reference(step, c, "three parts")) << "seed " << seed;
    Step::Result r = step.run(c);
    EXPECT_EQ(r.events, reference_crossings(c)) << "seed " << seed;
  }
  for (uint32_t seed = 200; seed <= 260; seed++) {
    Cell c = random_cell(3 + seed % 20, seed, 3);
    c.region((int)(seed & 1));
    c.finish();
    EXPECT_TRUE(equals_reference(step, c, "the clip alone")) << "seed " << seed;
  }
}

TEST(RenderDense, ChordsFansAndNestedCopiesAreTheExactAreaWithTheMeetingsOfEveryPair) {
  Step step(700, 1);
  struct Case {
    const char * name;
    Cell cell;
  };
  std::vector<Case> cases;
  cases.push_back({"chords 120", chords(120, 1)});
  cases.push_back({"chords 120 one way", chords(120, 2, 1, false)});
  cases.push_back({"chords 200", chords(200, 3)});
  cases.push_back({"nested copies 3", chords(60, 4, 3)});
  cases.push_back({"nested copies 19", chords(14, 5, 19)});
  cases.push_back({"fan through a vertex", fan(32, 1, 32, 1, false)});
  cases.push_back({"fan through a vertex, alternating", fan(32, 1, 32, 1, true)});
  cases.push_back({"fan through a half", fan(32, 1, 63, 2, true)});
  cases.push_back({"fan through a quarter point", fan(16, 1, 129, 4, true)});
  cases.push_back({"fan, off the lattice", fan(64, 3, 100, 3, true)});
  cases.push_back({"collinear overlap", collinear(7)});
  cases.push_back({"collinear overlap again", collinear(8)});
  for (Case & k : cases) {
    EXPECT_TRUE(equals_reference(step, k.cell, k.name)) << k.name;
    Step::Result r = step.run(k.cell);
    EXPECT_EQ(r.events, reference_crossings(k.cell)) << k.name << ": the meetings";
  }
  EXPECT_GE(cases[5].cell.parts[0].size(), 60u) << "the fan holds many fragments through one point";
  EXPECT_GE(cases[9].cell.parts[0].size(), 20u) << "and so does the fan through a point off the lattice";
}

TEST(RenderDense, ABundleThroughOnePointIsSortedBySlopeAndItsMeetingsAreEveryPair) {
  // Every chord through (32, 32): all meet at one depth in one slab, so the heap holds events of equal depth that are
  // processed in the order of their places, and each pair exchanges once.
  Cell c = fan(32, 1, 32, 1, true);
  size_t n = c.parts[0].size();
  ASSERT_GE(n, 60u);
  Step step(n + 4, 1);
  Step::Result r = step.run(c);
  ASSERT_EQ(r.status, GSHCPU_EXACT_OK);
  EXPECT_EQ(r.events, (uint64_t)n * (n - 1) / 2) << "a pair that meets exchanges once";
  EXPECT_EQ(r.events, reference_crossings(c));
}

TEST(RenderDense, TheWindingIsNoReasonToRefuseACell) {
  // Copies of one segment, as deep as the measured stroked text goes (475, 494) and past any bound a fixed width of weight
  // could hold (1024, 1025, 100000): the area is the union, a region to the right of the segment, whatever the depth.
  const int depths[] = {255, 256, 475, 494, 1024, 1025, 4096, 100000};
  Cell one;
  one.parts[0].push_back(piece(20, 0, 44, 64));
  one.finish();
  long double u = reference_area2(one);
  for (int depth : depths) {
    Cell c;
    for (int i = 0; i < depth; i++) {
      c.parts[0].push_back(piece(20, 0, 44, 64));
    }
    c.finish();
    Step step((size_t)depth + 2, 1);
    Step::Result r = step.run(c);
    ASSERT_EQ(r.status, GSHCPU_EXACT_OK) << "a winding of " << depth;
    EXPECT_EQ(r.out, (int64_t)std::floor((long double)c.dense[0] - u + 0.5L)) << "a winding of " << depth;
    EXPECT_EQ(r.distinct, 1u) << "equal copies are one fragment";
    EXPECT_EQ(r.winding, depth);
  }
  // The same depth at the top of the left side and not in the cell's own pieces.
  for (int depth : {475, 1025, 1 << 20}) {
    Cell c;
    c.parts[0] = {piece(20, 0, 44, 64), piece(50, 64, 10, 0)};
    c.wtop[0] = depth;
    c.finish();
    Step step(4, 1);
    EXPECT_TRUE(equals_reference(step, c, "a winding from the left")) << "a winding of " << depth << " from the left";
    EXPECT_GE(step.scratch.last_wmax, depth);
  }
}

TEST(RenderDense, APieceAndItsReverseCancelAndAPartialOverlapOfOppositePiecesDoesNot) {
  Cell a;
  a.parts[0] = {piece(10, 0, 30, 64), piece(40, 0, 40, 64)};
  a.finish();
  Cell b = a;
  b.parts[0].push_back(piece(5, 7, 33, 50));
  b.parts[0].push_back(piece(33, 50, 5, 7));
  b.finish();
  Step step(16, 1);
  Step::Result ra = step.run(a);
  Step::Result rb = step.run(b);
  ASSERT_EQ(ra.status, GSHCPU_EXACT_OK);
  ASSERT_EQ(rb.status, GSHCPU_EXACT_OK);
  EXPECT_EQ(rb.out, ra.out) << "the seam is not a piece of the cell";
  EXPECT_EQ(rb.distinct, ra.distinct);
  // The reverse of a longer segment on the same line leaves its difference: not cancelled, and exact.
  Cell p;
  p.parts[0] = {piece(10, 0, 30, 64), piece(30, 32, 20, 0)};
  p.finish();
  EXPECT_TRUE(equals_reference(step, p, "a partial overlap of opposite pieces"));
}

TEST(RenderDense, TheResultDoesNotDependOnTheOrderOfThePiecesOrOnTheScratch) {
  std::mt19937 rng(11);
  Cell base = random_cell(40, 77, 3);
  Step small(48, 1);
  Step large(4096, 3);
  Step::Result want = small.run(base);
  ASSERT_EQ(want.status, GSHCPU_EXACT_OK);
  for (int trial = 0; trial < 20; trial++) {
    Cell c = base;
    std::shuffle(c.parts[0].begin(), c.parts[0].end(), rng);
    Step::Result got = (trial & 1) ? large.run(c) : small.run(c);
    EXPECT_EQ(got.out, want.out) << "trial " << trial;
    EXPECT_EQ(got.events, want.events);
    EXPECT_EQ(got.work, want.work);
  }
}

TEST(RenderDense, TheThresholdIsTheLeastNumberOfFragmentsThatCanPassABudgetAndTheWorkBoundIsMonotone) {
  EXPECT_EQ(gshcpu_exact_cross_threshold(0), UINT64_MAX) << "no budget";
  for (uint64_t budget : {1ull, 2ull, 3ull, 100ull, 1ull << 20, (1ull << 32) - 1, 1ull << 32}) {
    uint64_t t = gshcpu_exact_cross_threshold(budget);
    EXPECT_GT(t * (t - 1) / 2, budget) << budget;
    EXPECT_LE((t - 1) * (t - 2) / 2, budget) << budget << ": one fewer fragment cannot pass";
  }
  EXPECT_EQ(gshcpu_exact_cross_threshold(1u << 20), 1449u) << "the default budget";
  uint64_t last = 0;
  for (uint64_t m : {1ull, 2ull, 10ull, 100ull, 4096ull, 16384ull}) {
    uint64_t now = gshcpu_exact_cell_work_bound(m, 1u << 20);
    EXPECT_GE(now, last) << "more pieces never cost less";
    last = now;
  }
  EXPECT_LE(gshcpu_exact_cell_work_bound(16384, 1u << 20), 1u << 30) << "a cell of the cap at the default budget stays under a billion units";
}

TEST(RenderDense, TheCommittedVectorsAreTheExactAnswersInRationalArithmetic) {
  // tests/dense_vectors.h is written by tools/gen-dense-cells.py, which shares no code with the library: for each cell the
  // exact doubled area as a fraction, rounded half up (for a part's own overlap the dense value less it), the meetings by
  // testing every pair, and the fragments left once the seams cancel.
  Step step(400, 3);
  size_t ties = 0;
  ASSERT_GE(DV_COUNT, 30u);
  for (size_t i = 0; i < DV_COUNT; i++) {
    const DV_Vec & v = DV_VECTORS[i];
    Cell c;
    c.params.parts = v.parts;
    c.params.mode = v.mode;
    c.params.even_odd = v.even_odd;
    c.params.complement = v.complement;
    if (v.mode == GSHCPU_EXACT_UNION3) {
      c.params.eps[0] = c.params.eps[1] = -1;
      c.params.eps[2] = 1;
    } else if (v.mode == GSHCPU_EXACT_REGION) {
      c.params.eps[0] = 1;
    }
    for (int p = 0; p < v.parts; p++) {
      for (int k = 0; k < v.count[p]; k++) {
        c.parts[p].push_back(piece(v.pieces[p][4 * k], v.pieces[p][4 * k + 1], v.pieces[p][4 * k + 2], v.pieces[p][4 * k + 3]));
      }
      c.wtop[p] = v.wtop[p];
    }
    c.finish();
    Step::Result r = step.run(c);
    ASSERT_EQ(r.status, GSHCPU_EXACT_OK) << v.name;
    EXPECT_EQ(r.out, v.out) << v.name << ": the exact value is " << v.exact;
    EXPECT_EQ(r.events, v.crossings) << v.name << ": the meetings";
    EXPECT_EQ(r.distinct, v.distinct) << v.name << ": the fragments";
    ties += v.tie != 0;
  }
  EXPECT_GE(ties, 1u) << "the vectors hold exact ties, which round up";
}

TEST(RenderDense, ACellIsRefusedForPiecesThatAreNotTheRasterizersAndNeverGuessed) {
  Cell c = random_cell(20, 5, 2);
  Step step(32, 1);
  ASSERT_EQ(step.run(c).status, GSHCPU_EXACT_OK);
  Cell bad = c;
  bad.dense[0] += 1;
  EXPECT_EQ(step.run(bad).status, GSHCPU_EXACT_IDENTITY) << "an area the pieces do not make (UC16 ii, in integers)";
  bad = c;
  bad.dense[0] -= 1;
  EXPECT_EQ(step.run(bad).status, GSHCPU_EXACT_IDENTITY);
  bad = c;
  bad.a_left[0] += 1;
  EXPECT_EQ(step.run(bad).status, GSHCPU_EXACT_IDENTITY) << "a cover that no winding at the top of the side explains";
  bad = c;
  bad.parts[0].push_back(piece(70, 0, 20, 20));
  EXPECT_EQ(step.run(bad).status, GSHCPU_EXACT_IDENTITY) << "a piece that is not in the cell";
  bad = c;
  bad.parts[0].push_back(piece(9, 9, 9, 9));
  EXPECT_EQ(step.run(bad).status, GSHCPU_EXACT_IDENTITY) << "a piece of no length";
  // A piece the rasterizer deposited and the cell does not have: the pieces are short of the cover.
  bad = c;
  bad.parts[0].pop_back();
  EXPECT_EQ(step.run(bad).status, GSHCPU_EXACT_IDENTITY) << "a piece missed";
  // The scratch holds exactly its capacity.
  Cell full = random_cell(30, 9, 2);
  Step exact(full.parts[0].size(), 1);
  EXPECT_EQ(exact.run(full).status, GSHCPU_EXACT_OK) << "as many pieces as the scratch holds";
  Step one_short(full.parts[0].size() - 1, 1);
  EXPECT_EQ(one_short.run(full).status, GSHCPU_EXACT_LIMIT) << "one more is the limit";
}

/* --- UC19: the crossings budget ----------------------------------------------------------------------------------- */

TEST(RenderDense, ACellWithTheBudgetsCrossingsDrawsAndOneMoreIsTheLimit) {
  Cell c = chords(90, 21);
  size_t distinct = 0;
  uint64_t k = reference_crossings(c, &distinct);
  ASSERT_GT(k, 100u);
  Step step(128, 1);
  EXPECT_EQ(step.run(c, k).status, GSHCPU_EXACT_OK) << "a cell with exactly that many crossings draws";
  EXPECT_EQ(step.run(c, k - 1).status, GSHCPU_EXACT_LIMIT) << "one more is the limit";
  EXPECT_EQ(step.run(c, 1).status, GSHCPU_EXACT_LIMIT);
  EXPECT_EQ(step.run(c, k + 1000).status, GSHCPU_EXACT_OK);
  // The count itself, without the area: equal to the independent count; stopped past the limit.
  GSHCPU_Exact_Params p = c.params;
  GSHCPU_Exact_Cell v = c.view();
  uint64_t counted = 0;
  uint64_t work = 0;
  p.max_crossings = distinct * (distinct - 1) / 2 - 1;  // a cell that can pass is counted
  ASSERT_EQ(gshcpu_exact_crossings(&p, &step.scratch, &v, &counted, &work), GSHCPU_EXACT_OK);
  EXPECT_EQ(counted, k);
  p.max_crossings = k - 1;
  EXPECT_EQ(gshcpu_exact_crossings(&p, &step.scratch, &v, &counted, &work), GSHCPU_EXACT_LIMIT);
  EXPECT_GT(counted, k - 1) << "stopped at the count that passed the limit";
}

TEST(RenderDense, TheCountIsKnownWithoutTheSweepAndItsCostIsTheCountsNotTheBudgets) {
  Cell c = chords(150, 31);
  Step step(160, 1);
  Step::Result free_run = step.run(c);
  ASSERT_EQ(free_run.status, GSHCPU_EXACT_OK);
  ASSERT_GT(free_run.events, 1000u);
  Step::Result refused = step.run(c, 1000);
  ASSERT_EQ(refused.status, GSHCPU_EXACT_LIMIT);
  EXPECT_LT(refused.work, free_run.work) << "a refused cell costs the count, not the sweep";
  EXPECT_EQ(step.scratch.last_events, 0u) << "no meeting was made";
}

TEST(RenderDense, AChordGridBuiltToAnyNumberOfMeetingsMakesExactlyThatManyAndTheirCostIsMeasured) {
  // The densest cells the lattice allows: distinct chords across the cell, as many pairs crossing as are wanted.  The
  // time of a sweep is printed, with the events it made; no threshold (AD-28).
  Step step(4300, 1);
  const uint64_t targets[] = {1, 2, 1000, 50000, 1u << 20};
  for (uint64_t target : targets) {
    uint64_t reached = 0;
    Cell c = chords_to(target, 17, &reached);
    ASSERT_EQ(reached, target) << "the grid reaches the target";
    ASSERT_EQ(reference_crossings(c), target);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Step::Result r = step.run(c, target);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ASSERT_EQ(r.status, GSHCPU_EXACT_OK) << "a cell with exactly the budget's crossings draws";
    EXPECT_EQ(r.events, target);
    EXPECT_LE(r.work, gshcpu_exact_cell_work_bound(c.parts[0].size(), target)) << "the work is within W_cell (UC17)";
    if (target > 1) {
      EXPECT_EQ(step.run(c, target - 1).status, GSHCPU_EXACT_LIMIT) << "and one more is the limit";
    }
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    std::printf("chord grid: %zu chords, %llu meetings, %.1f ms with the self-checks %s (%.3f us a meeting), work %llu\n",
        c.parts[0].size(), (unsigned long long)target, ms, gshcpu_exact_check ? "on" : "off", target ? ms * 1e3 / (double)target : 0.0,
        (unsigned long long)r.work);
  }
}

/* --- the earlier step as a second engine ---------------------------------------------------------------------------- */

TEST(RenderDense, WhereTheEarlierStepIsExactEnoughTheTwoAgreeButAtATie) {
  Step step(64, 1);
  size_t same = 0;
  size_t tie_slips = 0;
  for (uint32_t seed = 1; seed <= 300; seed++) {
    Cell c = random_cell(3 + seed % 40, seed + 5000, 2);
    Step::Result now = step.run(c);
    GSHCPU_Exact_Params p = c.params;
    GSHCPU_Exact_Cell v = c.view();
    int64_t old = 0;
    uint64_t w = 0;
    int ro = gshcpu_s4b_exact_cell(&p, &step.scratch, &v, &old, &w);
    ASSERT_EQ(now.status, GSHCPU_EXACT_OK);
    if (ro != GSHCPU_EXACT_OK) {
      continue;  // its own tolerance refused the cell: it is not the exact one
    }
    int64_t diff = now.out - old;
    EXPECT_LE(diff < 0 ? -diff : diff, 1) << "seed " << seed << ": the engines differ by at most one doubled unit";
    if (diff == 0) {
      same++;
    } else {
      Expected e = expected_of(c, 1e-6L);
      EXPECT_TRUE(e.near_tie) << "seed " << seed << ": they differ, so the exact value must be a tie (" << (double)e.exact << ")";
      tie_slips++;
    }
  }
  EXPECT_GT(same, 250u);
  (void)tie_slips;
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  gshcpu_exact_check = 1; /* every cell checks its own order and its meetings against the count */
  return RUN_ALL_TESTS();
}
