/**
 * @file
 *
 * The lean cover (the fast stroke coverage, lean.c) against the exact coverage on outlines that do not overlap themselves:
 * boxes, a triangle, a star, a many-sided polygon, through windows that cut them and shifts that move them, with the scratch
 * left all zero afterwards.  Where nothing overlaps the two agree to the rounding of a float, which is what the tolerance
 * allows; where pieces overlap the lean one sums them, which is its contract and not tested here.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "stroke_helpers.h"

using stroke_gate::Ops;

#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>

using namespace sh;

namespace {

struct Flat {
  GSHGEOM_Device_Path * path = nullptr;
  GSHGEOM_Flat_Path * flat = nullptr;
  explicit Flat(const std::vector<std::vector<std::pair<int32_t, int32_t>>> & polys) {
    EXPECT_EQ(gshgeom_device_path_create(nullptr, nullptr, &path), GSHGEOM_OK);
    for (const auto & c : polys) {
      for (size_t i = 0; i < c.size(); i++) {
        GSHGEOM_Device_Point p;
        p.x = c[i].first;
        p.y = c[i].second;
        i == 0 ? gshgeom_device_path_move_to(path, p) : gshgeom_device_path_line_to(path, p);
      }
      gshgeom_device_path_close(path);
    }
    EXPECT_EQ(gshgeom_device_path_flatten(path, nullptr, nullptr, &flat), GSHGEOM_OK);
  }
  ~Flat() {
    gshgeom_flat_path_destroy(flat);
    gshgeom_device_path_destroy(path);
  }
  Flat(const Flat &) = delete;
  Flat & operator=(const Flat &) = delete;
};

using Poly = std::vector<std::pair<int32_t, int32_t>>;

Poly regular(int sides, double cx, double cy, double r, double turn) {
  Poly p;
  for (int i = 0; i < sides; i++) {
    double a = turn + 2.0 * M_PI * i / sides;
    p.push_back({(int32_t)std::lround((cx + r * std::cos(a)) * 64.0), (int32_t)std::lround((cy + r * std::sin(a)) * 64.0)});
  }
  return p;
}

Poly star(int points, double cx, double cy, double outer, double inner) {
  Poly p;
  for (int i = 0; i < 2 * points; i++) {
    double a = M_PI * i / points;
    double r = i % 2 ? inner : outer;
    p.push_back({(int32_t)std::lround((cx + r * std::cos(a)) * 64.0), (int32_t)std::lround((cy + r * std::sin(a)) * 64.0)});
  }
  return p;
}

struct Spans {
  int64_t x0, y0;      // the rectangle's origin, the spans' frame (the paths' frame, moved by the shift)
  int64_t sx, sy;      // the shift the receiver adds
  int64_t w, h;
  std::vector<uint8_t> cov;
  size_t calls = 0;
};

void collect(void * ctx, int64_t y, int64_t x, size_t n, const uint8_t * m8) {
  Spans * s = static_cast<Spans *>(ctx);
  s->calls++;
  int64_t py = y + s->sy - s->y0;
  ASSERT_GE(py, 0);
  ASSERT_LT(py, s->h);
  for (size_t i = 0; i < n; i++) {
    int64_t px = x + (int64_t)i + s->sx - s->x0;
    ASSERT_GE(px, 0);
    ASSERT_LT(px, s->w);
    s->cov[(size_t)(py * s->w + px)] = m8[i];
  }
}

struct Worst {
  int max = 0;
  size_t over = 0;
};

/** The lean coverage of @p polys over @p rect, shifted by (@p sx, @p sy), against the exact coverage of the unshifted paths. */
Worst compare(const std::vector<Poly> & polys, GSHCPU_Rect rect, int64_t sx, int64_t sy) {
  Flat flat(polys);
  const int64_t w = rect.x1 - rect.x0, h = rect.y1 - rect.y0;
  // The exact one, in the paths' frame: the rectangle moved back by the shift.
  GSHCPU_Rect back = rect;
  back.x0 -= sx;
  back.x1 -= sx;
  back.y0 -= sy;
  back.y1 -= sy;
  std::vector<uint8_t> want((size_t)(w * h), 0);
  {
    std::vector<int64_t> acc(2 * (size_t)(w + 1) * GSHCPU_RENDER_BAND_ROWS, 0);
    uint64_t cells = 0;
    const GSHGEOM_Flat_Path * paths[1] = {flat.flat};
    EXPECT_EQ(gshcpu_render_cover_counted(paths, 1, GSHGEOM_FILL_NONZERO, back, back, want.data(), acc.data(), UINT64_MAX, &cells),
        GSHCPU_OK);
  }
  const size_t bytes = 2 * (size_t)(w + 1) * GSHCPU_RENDER_BAND_ROWS * sizeof(int64_t);
  std::vector<int64_t> scratch(bytes / sizeof(int64_t), 0);
  std::vector<uint8_t> line((size_t)w + 16, 0);
  Spans s;
  s.x0 = rect.x0;
  s.y0 = rect.y0;
  s.sx = sx;
  s.sy = sy;
  s.w = w;
  s.h = h;
  s.cov.assign((size_t)(w * h), 0);
  EXPECT_EQ(gshcpu_render_lean_cover(flat.flat, sx, sy, rect, scratch.data(), bytes, line.data(), collect, &s), GSHCPU_OK);
  for (int64_t v : scratch) {
    EXPECT_EQ(v, 0) << "the scratch is left all zero";
    if (v != 0) {
      break;
    }
  }
  Worst worst;
  for (size_t i = 0; i < want.size(); i++) {
    int d = std::abs((int)s.cov[i] - (int)want[i]);
    worst.max = std::max(worst.max, d);
    if (d > 3) {
      worst.over++;
    }
  }
  return worst;
}

GSHCPU_Rect rect_of(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  GSHCPU_Rect r;
  r.x0 = x0;
  r.y0 = y0;
  r.x1 = x1;
  r.y1 = y1;
  return r;
}

}  // namespace

TEST(RenderLean, AgreesWithTheExactCoverageOnOutlinesThatDoNotOverlap) {
  const std::vector<std::vector<Poly>> shapes = {
      {{{10 * 64 + 20, 7 * 64 + 5}, {40 * 64 + 33, 7 * 64 + 5}, {40 * 64 + 33, 25 * 64 + 17}, {10 * 64 + 20, 25 * 64 + 17}}},
      {{{5 * 64, 30 * 64 + 9}, {50 * 64 + 7, 12 * 64 + 40}, {33 * 64 + 21, 3 * 64 + 2}}},
      {star(5, 30.3, 20.7, 18.1, 7.3)},
      {regular(37, 28.2, 19.6, 17.4, 0.3)},
      {star(11, 30.0, 20.0, 19.0, 4.0), },
  };
  for (size_t k = 0; k < shapes.size(); k++) {
    Worst w = compare(shapes[k], rect_of(0, 0, 64, 48), 0, 0);
    EXPECT_LE(w.max, 3) << "shape " << k;
    EXPECT_EQ(w.over, 0u) << "shape " << k;
  }
}

TEST(RenderLean, AWindowThatCutsTheShapeAndAShiftThatMovesIt) {
  const std::vector<Poly> shape = {star(7, 30.3, 24.7, 22.1, 9.3)};
  const GSHCPU_Rect windows[] = {rect_of(0, 0, 64, 48), rect_of(10, 8, 40, 36), rect_of(-5, -5, 20, 20), rect_of(25, 15, 80, 60),
      rect_of(0, 0, 7, 48)};
  const int64_t shifts[][2] = {{0, 0}, {3, -2}, {-6, 5}, {40, 40}};
  for (const GSHCPU_Rect & r : windows) {
    for (const auto & sh : shifts) {
      Worst w = compare(shape, rect_of(r.x0 + sh[0], r.y0 + sh[1], r.x1 + sh[0], r.y1 + sh[1]), sh[0], sh[1]);
      EXPECT_LE(w.max, 3) << "window " << r.x0 << "," << r.y0 << " shift " << sh[0] << "," << sh[1];
    }
  }
}

TEST(RenderLean, MoreRowsThanABandAndAnEmptyRectangle) {
  const std::vector<Poly> tall = {{{10 * 64, 2 * 64 + 9}, {40 * 64 + 5, 3 * 64}, {35 * 64, 190 * 64 + 40}, {12 * 64 + 20, 188 * 64}}};
  Worst w = compare(tall, rect_of(0, 0, 50, 200), 0, 0);
  EXPECT_LE(w.max, 3);
  Flat f(tall);
  std::vector<int64_t> scratch(64, 0);
  EXPECT_EQ(gshcpu_render_lean_cover(f.flat, 0, 0, rect_of(5, 5, 5, 9), scratch.data(), 64 * 8, nullptr, collect, nullptr), GSHCPU_OK);
}

namespace {

/** A list of @p count strokes, translucent with alpha @p alpha, crossing one another on a 96 by 64 raster. */
const GSHSCENE_List * crossing_strokes(Store & store, int count, double alpha, double width, bool trailing_fill = false) {
  GSHGEOM_Stroke g;
  gshgeom_stroke_init(&g);
  g.width = width;
  g.join = GSHGEOM_STROKE_JOIN_ROUND;
  g.cap = GSHGEOM_STROKE_CAP_ROUND;
  const GSHSCENE_Resource * style = intern_style(store, g);
  GSHSCENE_List * l = store.open();
  for (int i = 0; i < count; i++) {
    const Ops ops = {{'M', {6.0 + 3.0 * i, 8.0 + 9.0 * i}}, {'Q', {50.0, 2.0 + 14.0 * i, 90.0 - 4.0 * i, 56.0 - 6.0 * i}},
        {'L', {12.0 + 5.0 * i, 58.0}}};
    const GSHSCENE_Resource * path = store.path(stroke_gate::build(ops));
    GSHSCENE_Paint paint = solid(1, 1, 1, alpha);
    GSHGEOM_Map identity = translation(0.0, 0.0);
    EXPECT_EQ(gshscene_list_stroke(l, path, style, &identity, &paint, 0, 0.0), GSHSCENE_OK);
  }
  if (trailing_fill) {
    /* a fill that draws nothing: its presence is what keeps the render off the 8-bit compositor (strokes alone), through the double layer */
    GSHSCENE_Paint nothing = solid(0, 0, 0, 0);
    EXPECT_EQ(gshscene_list_fill(l, store.rect(94, 62, 95, 63), nullptr, GSHGEOM_FILL_NONZERO, &nothing, 0, 0.0), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

struct Pair {
  std::vector<uint8_t> exact, fast;
  uint64_t fast_sparse = 0;
  GSHSCENE_Result exact_result = GSHSCENE_OK, fast_result = GSHSCENE_OK;
};

Pair draw_both(const GSHSCENE_List * list, uint64_t max_deposits) {
  Pair out;
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_deposits = max_deposits;
  {
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
    Drawn d = render_list(sink.sink, list, 96, 64, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED);
    out.exact = d.alpha;
    out.exact_result = d.result;
  }
  limits.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
  {
    Sink sink(GSHCPU_RENDER_TIERS_ALL, &limits);
    Drawn d = render_list(sink.sink, list, 96, 64, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED);
    out.fast = d.alpha;
    out.fast_result = d.result;
    out.fast_sparse = sink.counters.sparse_plain + sink.counters.sparse_hairline;
  }
  return out;
}

int worst_difference(const Pair & p) {
  int worst = 0;
  for (size_t i = 0; i < p.exact.size() && i < p.fast.size(); i++) {
    worst = std::max(worst, std::abs((int)p.exact[i] - (int)p.fast[i]));
  }
  return worst;
}

}  // namespace

TEST(RenderLeanSink, TheFastCoverageOfStrokesAloneComposesIn8BitsWithinTheRoundingOfTheLayer) {
  Store store;
  const GSHSCENE_List * opaque = crossing_strokes(store, 1, 1.0, 5.0);
  Pair a = draw_both(opaque, 0);
  ASSERT_EQ(a.exact_result, GSHSCENE_OK);
  ASSERT_EQ(a.fast_result, GSHSCENE_OK);
  EXPECT_GE(a.fast_sparse, 1u);
  for (int n = 1; n <= 5; n++) {
    // The same strokes and the same fast coverage, once composed in 8 bits straight into the target (strokes alone) and once through the double layer
    // (a fill that draws nothing follows them): the difference is the compositor's rounding at each step, a level a stroke.
    const GSHSCENE_List * direct = crossing_strokes(store, n, 0.45, 4.0 + n);
    const GSHSCENE_List * layered = crossing_strokes(store, n, 0.45, 4.0 + n, true);
    Pair b = draw_both(direct, 0);
    Pair c = draw_both(layered, 0);
    ASSERT_EQ(b.fast_result, GSHSCENE_OK);
    ASSERT_EQ(c.fast_result, GSHSCENE_OK);
    EXPECT_EQ(b.fast_sparse, (uint64_t)n);
    int worst = 0;
    for (size_t i = 0; i < b.fast.size(); i++) {
      worst = std::max(worst, std::abs((int)b.fast[i] - (int)c.fast[i]));
    }
    EXPECT_LE(worst, 1 + n) << n << " strokes: each step rounds to 8 bits, the layer rounds once";
    EXPECT_LE(worst_difference(b), 70) << "and the coverage is the fast one's against the exact: overlaps summed";
  }
}

TEST(RenderLeanSink, AStrokePastTheCapOfTheSparseTierIsDrawnThroughTheLayerAndStoredBeforeTheNext) {
  Store store;
  const GSHSCENE_List * list = crossing_strokes(store, 3, 0.6, 6.0);
  bool fell_back = false;
  bool took_both = false;
  for (uint64_t cap = 40; cap < 200000; cap = cap * 3 / 2) {
    Pair p = draw_both(list, cap);
    if (p.exact_result != GSHSCENE_OK || p.fast_result != GSHSCENE_OK) {
      continue;
    }
    EXPECT_LE(worst_difference(p), 6) << "cap " << cap;
    fell_back = fell_back || p.fast_sparse < 3;
    took_both = took_both || p.fast_sparse == 3;
  }
  EXPECT_TRUE(fell_back) << "some cap sends a stroke through the double layer";
  EXPECT_TRUE(took_both) << "and some does not";
}

namespace {

/** The caller's executor: a thread for each task but the first, which the calling thread runs; the count of calls is kept. */
std::atomic<unsigned> g_runs{0};

int thread_executor(void * user, size_t count, GSHCPU_Task_Fn task, void * ctx) {
  g_runs++;
  (void)user;
  std::vector<std::thread> workers;
  for (size_t i = 1; i < count; i++) {
    workers.emplace_back([=] { task(ctx, i); });
  }
  task(ctx, 0);
  for (std::thread & t : workers) {
    t.join();
  }
  return 0;
}

/** An executor that refuses: nothing is run, and the calling thread must draw it all. */
int refusing_executor(void *, size_t, GSHCPU_Task_Fn, void *) {
  return 1;
}

/** The hash of @p list drawn with the fast coverage and an executor of @p threads tasks, under the sparse cap @p max_deposits. */
uint64_t drawn_hash(const GSHSCENE_List * list, size_t threads, uint64_t max_deposits, GSHSCENE_Result * result = nullptr,
    GSHCPU_Executor_Fn run = thread_executor) {
  GSHCPU_Limits limits;
  gshcpu_limits_default(&limits);
  limits.max_deposits = max_deposits;
  limits.stroke_coverage = GSHCPU_STROKE_COVERAGE_FAST;
  GSHCPU_Executor executor = {threads, threads > 1 ? run : nullptr, nullptr};
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_render_sink_create_with_executor(nullptr, nullptr, &limits, GSHCPU_RENDER_TIERS_ALL, nullptr, &executor, &sink),
      GSHCPU_OK);
  Drawn d = render_list(sink, list, 96, 64, GSHCPU_FORMAT_RGBA8_PREMULTIPLIED);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  if (result) {
    *result = d.result;
  }
  return d.result == GSHSCENE_OK ? d.hash : 0;
}

}  // namespace

TEST(RenderLeanThreads, ThePictureIsTheSameBytesForAnyNumberOfThreads) {
  Store store;
  // 3 strokes are fewer than a batch takes to the threads, 24 are a few stripes' worth, 300 fill the batch more than once.
  for (int count : {1, 3, 24, 300}) {
    const GSHSCENE_List * list = crossing_strokes(store, count, 0.2, 3.0 + (count % 5));
    const uint64_t one = drawn_hash(list, 1, 0);
    ASSERT_NE(one, 0u);
    for (uint64_t threads : {2u, 3u, 8u, 256u}) {
      EXPECT_EQ(drawn_hash(list, threads, 0), one) << count << " strokes on " << threads << " threads";
    }
  }
}

TEST(RenderLeanThreads, AStrokeThatIsNotQueuedKeepsItsPlaceInTheOrder) {
  Store store;
  const GSHSCENE_List * list = crossing_strokes(store, 24, 0.3, 6.0);
  // Some cap sends some strokes through the double layer in the middle of a batch: the ones before it are drawn first.
  int fell = 0;
  for (uint64_t cap = 40; cap < 200000; cap = cap * 3 / 2) {
    GSHSCENE_Result r1 = GSHSCENE_OK, r4 = GSHSCENE_OK;
    const uint64_t one = drawn_hash(list, 1, cap, &r1);
    const uint64_t four = drawn_hash(list, 4, cap, &r4);
    EXPECT_EQ(r1, r4) << "cap " << cap;
    EXPECT_EQ(one, four) << "cap " << cap;
    fell += one != drawn_hash(list, 1, 0);
  }
  EXPECT_GT(fell, 0) << "some cap changes the picture, so some stroke took the other path";
}

TEST(RenderLeanThreads, TheExecutorIsAskedAndARefusalCostsTimeAndNotThePicture) {
  Store store;
  const GSHSCENE_List * list = crossing_strokes(store, 24, 0.2, 4.0);
  const uint64_t one = drawn_hash(list, 1, 0);
  g_runs = 0;
  EXPECT_EQ(drawn_hash(list, 4, 0), one);
  EXPECT_GT(g_runs.load(), 0u) << "the strokes were queued and the executor was asked to draw them";
  g_runs = 0;
  EXPECT_EQ(drawn_hash(list, 1, 0), one);
  EXPECT_EQ(g_runs.load(), 0u) << "one task is no executor";
  EXPECT_EQ(drawn_hash(list, 4, 0, nullptr, refusing_executor), one) << "a refusal leaves it to the calling thread";
}

TEST(RenderLeanThreads, MoreTasksThanTheCeilingAreRefused) {
  GSHCPU_Executor executor = {GSHCPU_EXECUTOR_MAX_THREADS, thread_executor, nullptr};
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshcpu_sink_create_with_executor(nullptr, nullptr, nullptr, &executor, &sink), GSHCPU_OK);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  executor.threads = GSHCPU_EXECUTOR_MAX_THREADS + 1;
  sink = nullptr;
  EXPECT_EQ(gshcpu_sink_create_with_executor(nullptr, nullptr, nullptr, &executor, &sink), GSHCPU_ERR_INVALID);
  EXPECT_EQ(sink, nullptr);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
