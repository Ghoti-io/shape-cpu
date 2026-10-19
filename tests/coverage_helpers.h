/**
 * @file
 *
 * Helpers for the coverage tests: building device paths, and the references
 * that need no cells (the winding of a point, the area of a polygon clipped to
 * a pixel).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHCPU_TEST_COVERAGE_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_COVERAGE_HELPERS_H

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace cov {

using Pt = std::pair<int32_t, int32_t>;
using Contour = std::vector<Pt>;

/** Whole pixels to 26.6. */
constexpr int32_t px(int v) { return v * 64; }

/** A device path owned for the length of a test. */
class Path {
public:
  explicit Path(const GSHGEOM_Allocator * alloc = nullptr,
      const GSHGEOM_Limits * limits = nullptr) {
    EXPECT_EQ(gshgeom_device_path_create(alloc, limits, &path_), GSHGEOM_OK);
  }
  ~Path() { gshgeom_device_path_destroy(path_); }
  Path(const Path &) = delete;
  Path & operator=(const Path &) = delete;

  operator const GSHGEOM_Device_Path *() const { return path_; }
  GSHGEOM_Device_Path * get() { return path_; }

  Path & move(int32_t x, int32_t y) {
    EXPECT_EQ(gshgeom_device_path_move_to(path_, {x, y}), GSHGEOM_OK);
    return *this;
  }
  Path & line(int32_t x, int32_t y) {
    EXPECT_EQ(gshgeom_device_path_line_to(path_, {x, y}), GSHGEOM_OK);
    return *this;
  }
  Path & quad(int32_t cx, int32_t cy, int32_t x, int32_t y) {
    EXPECT_EQ(gshgeom_device_path_quadratic_to(path_, {cx, cy}, {x, y}),
        GSHGEOM_OK);
    return *this;
  }
  Path & cubic(int32_t c1x, int32_t c1y, int32_t c2x, int32_t c2y, int32_t x,
      int32_t y) {
    EXPECT_EQ(gshgeom_device_path_cubic_to(path_, {c1x, c1y}, {c2x, c2y},
                  {x, y}), GSHGEOM_OK);
    return *this;
  }
  Path & close() {
    EXPECT_EQ(gshgeom_device_path_close(path_), GSHGEOM_OK);
    return *this;
  }

  /** A closed polygon. */
  Path & polygon(const Contour & pts, bool close_it = true) {
    for (size_t i = 0; i < pts.size(); ++i) {
      if (i == 0) {
        move(pts[i].first, pts[i].second);
      }
      else {
        line(pts[i].first, pts[i].second);
      }
    }
    if (close_it && !pts.empty()) {
      close();
    }
    return *this;
  }

private:
  GSHGEOM_Device_Path * path_ = nullptr;
};

/** A coverage owned for the length of a test. */
struct Cov {
  GSHCPU_Coverage c{};
  ~Cov() { gshcpu_coverage_destroy(&c); }
  Cov() = default;
  Cov(const Cov &) = delete;
  Cov & operator=(const Cov &) = delete;
  GSHCPU_Coverage * operator&() { return &c; }
  const GSHCPU_Coverage * ptr() const { return &c; }

  std::string hex() const {
    std::string out;
    static const char digits[] = "0123456789abcdef";
    for (uint32_t row = 0; row < c.height; ++row) {
      for (uint32_t col = 0; col < c.width; ++col) {
        const uint8_t v = gshcpu_coverage_at(&c, col, row);
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 15]);
      }
    }
    return out;
  }
};

/** The rows of a coverage, copied. */
inline std::vector<uint8_t> rows_of(const GSHCPU_Coverage & c) {
  std::vector<uint8_t> out;
  for (uint32_t row = 0; row < c.height; ++row) {
    for (uint32_t col = 0; col < c.width; ++col) {
      out.push_back(gshcpu_coverage_at(&c, col, row));
    }
  }
  return out;
}

/** The area of a polygon (shoelace), twice, in 26.6 squared units. */
inline double area2(const Contour & pts) {
  double sum = 0;
  for (size_t i = 0; i < pts.size(); ++i) {
    const Pt & a = pts[i];
    const Pt & b = pts[(i + 1) % pts.size()];
    sum += (double)a.first * b.second - (double)b.first * a.second;
  }
  return sum;
}

/**
 * The exact area of a simple polygon inside the pixel [x0, x0+64) x [y0, y0+64)
 * as a fraction of the pixel, by clipping with the four sides in turn
 * (Sutherland-Hodgman).  Double precision, so a reference with a tolerance.
 */
inline double pixel_area(const Contour & poly, int64_t x0, int64_t y0) {
  struct P { double x, y; };
  std::vector<P> in;
  for (const Pt & p : poly) {
    in.push_back({(double)p.first, (double)p.second});
  }
  auto clip = [](const std::vector<P> & subject, int side, double edge) {
    std::vector<P> out;
    auto inside = [&](const P & p) {
      switch (side) {
        case 0: return p.x >= edge;
        case 1: return p.x <= edge;
        case 2: return p.y >= edge;
        default: return p.y <= edge;
      }
    };
    auto cut = [&](const P & a, const P & b) {
      if (side < 2) {
        const double t = (edge - a.x) / (b.x - a.x);
        return P{edge, a.y + t * (b.y - a.y)};
      }
      const double t = (edge - a.y) / (b.y - a.y);
      return P{a.x + t * (b.x - a.x), edge};
    };
    for (size_t i = 0; i < subject.size(); ++i) {
      const P & a = subject[i];
      const P & b = subject[(i + 1) % subject.size()];
      if (inside(b)) {
        if (!inside(a)) {
          out.push_back(cut(a, b));
        }
        out.push_back(b);
      }
      else if (inside(a)) {
        out.push_back(cut(a, b));
      }
    }
    return out;
  };
  in = clip(in, 0, (double)x0);
  in = clip(in, 1, (double)(x0 + 64));
  in = clip(in, 2, (double)y0);
  in = clip(in, 3, (double)(y0 + 64));
  double sum = 0;
  for (size_t i = 0; i < in.size(); ++i) {
    const P & a = in[i];
    const P & b = in[(i + 1) % in.size()];
    sum += a.x * b.y - b.x * a.y;
  }
  return (sum < 0 ? -sum : sum) / 2.0 / 4096.0;
}

}  // namespace cov

#endif /* GHOTI_IO_GSHCPU_TEST_COVERAGE_HELPERS_H */
