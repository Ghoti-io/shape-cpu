/**
 * @file
 *
 * What the gates of S11 and of masks share (render vectors, composite vectors and mask vectors): comparing the root layer
 * before the write, a store and a list that records one-pixel cases, the observer that reads the root, and the rounding of a
 * float write.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHCPU_TEST_VECTOR_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_VECTOR_HELPERS_H

#include <gtest/gtest.h>

#include "../src/render/render_internal.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vh {

inline constexpr double kTolerance = 1e-12;

/** The worst error before the write, over the inexact cases, per gate. */
struct Worst {
  double error = 0.0;
  std::string where;
  void see(double e, const std::string & w) {
    if (e > error) {
      error = e;
      where = w;
    }
  }
};

inline GSHSCENE_Color_Space space_of(int primaries, int transfer, double gamma) {
  GSHSCENE_Color_Space s{};
  s.primaries = (GCOL_Primaries)primaries;
  s.transfer = (GCOL_Transfer)transfer;
  s.gamma = gamma;
  return s;
}

inline bool same_bits(double a, double b) {
  uint64_t x;
  uint64_t y;
  std::memcpy(&x, &a, 8);
  std::memcpy(&y, &b, 8);
  return x == y;
}

/** Compare four values before the write: bit for bit when exact, else within the tolerance. */
inline void expect_pre(const double got[4], const double want[4], bool exact, const std::string & where, Worst & worst) {
  for (int i = 0; i < 4; i++) {
    if (exact) {
      // The values are reals (D7): a zero's sign is not one of them.
      EXPECT_TRUE(same_bits(got[i], want[i]) || (got[i] == 0.0 && want[i] == 0.0))
          << where << " channel " << i << ": " << got[i] << " want " << want[i];
    } else {
      // Relative past 1: N1's saturation cases hold values up to 2^64.
      double e = std::fabs(got[i] - want[i]) / std::max(1.0, std::fabs(want[i]));
      EXPECT_LE(e, kTolerance) << where << " channel " << i << ": " << got[i] << " want " << want[i];
      worst.see(e, where);
    }
  }
}

/** The root layer and touched map before the write, as the observer saw them. */
struct Seen {
  double root[4] = {0, 0, 0, 0};
  int touched = -1;
};

inline void observe(void * ctx, GSHCPU_Rect region, const double * root, const uint8_t * touched) {
  Seen * s = (Seen *)ctx;
  if (region.x1 - region.x0 == 1 && region.y1 - region.y0 == 1) {
    std::memcpy(s->root, root, sizeof s->root);
    s->touched = touched[0];
  }
}

/** A store, the rectangles the cases use, and a list; released at the end. */
struct Bench {
  GSHSCENE_Store * store = nullptr;
  std::vector<const GSHSCENE_Resource *> held;
  GSHSCENE_List * list = nullptr;
  Bench() {
    gshscene_store_create(nullptr, nullptr, &store);
    gshscene_list_create(nullptr, nullptr, store, &list);
  }
  ~Bench() {
    gshscene_list_destroy(list);
    for (const GSHSCENE_Resource * r : held) {
      gshscene_resource_release(r);
    }
    gshscene_store_destroy(store);
  }
  Bench(const Bench &) = delete;
  Bench & operator=(const Bench &) = delete;
  /** A rectangle @p width / 64 of the pixel wide, the pixel's height, at its left edge. */
  const GSHSCENE_Resource * strip(int width) {
    GSHGEOM_Path * p = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &p);
    double w = width / 64.0;
    gshgeom_path_move_to(p, GSHGEOM_Point{0, 0, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{w, 0, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{w, 1, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{0, 1, 0});
    gshgeom_path_close(p);
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(store, p, &r), GSHSCENE_OK);
    gshgeom_path_destroy(p);
    held.push_back(r);
    return r;
  }
  /** Record one item (a composite_vectors or render_vectors item, in their common shape). */
  void record(int kind, int paint, const double rgba[4], GSHSCENE_Color_Space space, double opacity,
      GSHSCENE_Color_Space blend, int composite, int width, int entry = 0) {
    if (kind == 1) {
      GSHSCENE_Paint p{};
      p.kind = paint == 2 ? GSHSCENE_PAINT_FOREGROUND : paint == 3 ? GSHSCENE_PAINT_PALETTE : GSHSCENE_PAINT_SOLID;
      if (paint == 2 || paint == 3) {
        p.rgba[3] = rgba[3];
        p.entry = (uint32_t)entry;
      } else {
        std::memcpy(p.rgba, rgba, sizeof p.rgba);
        p.space = space;
      }
      ASSERT_EQ(gshscene_list_fill(list, strip(width), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    } else if (kind == 2) {
      ASSERT_EQ(gshscene_list_clip_push(list, strip(width), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    } else if (kind == 3) {
      ASSERT_EQ(gshscene_list_clip_pop(list), GSHSCENE_OK);
    } else if (kind == 4) {
      GSHSCENE_Group g{};
      g.opacity = opacity;
      g.blend_space = blend;
      g.composite = (uint32_t)composite;
      ASSERT_EQ(gshscene_list_group_push(list, &g, 0, 0.0), GSHSCENE_OK);
    } else {
      ASSERT_EQ(gshscene_list_group_pop(list), GSHSCENE_OK);
    }
  }
};

inline GIMG_Raster * one_pixel(GSHCPU_Format format) {
  GIMG_Pixel_Format f{};
  f.channel_model = GIMG_CHANNEL_RGBA;
  f.layout = GIMG_LAYOUT_INTERLEAVED;
  f.channel_count = 4;
  bool wide = format == GSHCPU_FORMAT_RGBAF32_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
  f.channel_type = wide ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  for (int i = 0; i < 4; i++) {
    f.bits_per_channel[i] = wide ? 32 : 8;
  }
  f.alpha_form = format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED
      ? GIMG_ALPHA_PREMULTIPLIED
      : GIMG_ALPHA_STRAIGHT;
  GIMG_Raster * r = nullptr;
  EXPECT_EQ(gimg_raster_create(1, 1, &f, GIMG_RASTER_OWNED, nullptr, 0, &r), GIMG_OK);
  return r;
}

/** Render @p list on @p raster in @p space with an observed sink. */
inline Seen render(const GSHSCENE_List * list, GIMG_Raster * raster, GSHSCENE_Color_Space space) {
  Seen seen;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_render_sink_create_observed(nullptr, nullptr, nullptr, observe, &seen, &sink), GSHCPU_OK);
  GSHSCENE_Report * report = nullptr;
  EXPECT_EQ(gshscene_report_create(nullptr, nullptr, &report), GSHSCENE_OK);
  GSHSCENE_Target target{};
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  target.blend_space = space;
  GSHSCENE_Render_Request request{};
  request.list = list;
  request.target = &target;
  request.report = report;
  EXPECT_EQ(gshscene_sink_render(sink, &request), GSHSCENE_OK);
  gshscene_report_destroy(report);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  return seen;
}

/**
 * The value a float target stores for @p v: rounded once, or the float on the other side within 1e-9 of halfway.  When
 * the value before the write is not exact (D7: within the tolerance of @p v), the store is the one rounding of a value
 * within that tolerance: near zero, where floats are far finer than the tolerance, that is a range of floats.
 */
inline bool float_store_ok(float got, double v, bool exact) {
  float want = (float)v;
  if (got == want) {
    return true;
  }
  float other = std::nextafter(want, (double)want < v ? INFINITY : -INFINITY);
  double mid = ((double)want + (double)other) / 2.0;
  if (got == other && std::fabs(v - mid) < 1e-9) {
    return true;
  }
  double tol = kTolerance * std::max(1.0, std::fabs(v));
  return !exact && got >= (float)(v - tol) && got <= (float)(v + tol);
}

} // namespace vh

#endif /* GHOTI_IO_GSHCPU_TEST_VECTOR_HELPERS_H */
