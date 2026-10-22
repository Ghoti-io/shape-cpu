/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape CPU.
 */

/**
 * @file
 *
 * The coverage-level bracket of a stroke (strokes story S4, SC5): the stroke's tolerance, as one function that another
 * backend can call on its own coverage.
 *
 * For every pixel the sink's coverage `k` (0 to 255) must satisfy
 *
 *     A(R minus D(tau)) - e  <=  k / 255  <=  A(R plus D(tau)) + e + o
 *
 * with `R` the region the stroke covers (shape-geometry's SG6 to SG13, with SG12's alignment), `D(tau)` the closed disc
 * of the expansion's tolerance (1/16 pixel), `A` the area of a set inside the pixel, `e` the bound of the coverage's
 * 8-bit rounding and arithmetic, and `o` the overlap allowance of F4: the sum of two partial coverages of one pixel
 * can credit up to the overlap more than their union (AD-12's saturating sum), 64/255 as a stopgap.
 *
 * `R` comes from the geometric oracle (shape-geometry's installed `testing/stroke_oracle.h`), which shares no code with
 * the expansion.  The oracle decides membership of points, so each pixel is sampled on a grid of cells and every
 * cell is classified from the distance of its centre to the oracle's boundary samples (a superset of `R`'s boundary):
 * a cell whose centre is farther than `tau + m + r` (`m` the samples' spacing and a margin, `r` the cell's half
 * diagonal) from every sample lies wholly on one side at a distance beyond `tau`, so it is wholly in `R minus D(tau)`
 * (inside) or wholly outside `R plus D(tau)` (outside).  `A(R minus D(tau))` is therefore at least the inside cells'
 * share and `A(R plus D(tau))` at most the share of the cells that are not outside; the gate uses those, which can only
 * widen the bracket.  A cell whose membership the oracle calls ambiguous is neither.
 *
 * The aligned part's boundary crosses the fill region's at an angle `alpha`, where the composed error may reach
 * `tau / sin(alpha / 2)` (E9): a pixel near both boundaries is excluded, and the number excluded is reported.
 *
 * It records the worst overshoot past the union's area (the quantity `o` bounds), so that the bound can be stated
 * with the measurement that supports it.
 */

#ifndef GHOTI_IO_GSHCPU_TEST_STROKE_GATE_H
#define GHOTI_IO_GSHCPU_TEST_STROKE_GATE_H

#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-geometry/testing/stroke_oracle.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace stroke_gate {

namespace so = stroke_oracle;

/**
 * F4's overlap allowance, a stopgap: 64/255 of a pixel, a bound the gate fails past.  The worst overshoot measured over the
 * gate's cases is 61.8/255 (dashes with square caps), which leaves a margin of 2.2/255.  The exact union coverage of the
 * scheduled follow-up story brings it down.
 */
constexpr double kOverlap = 64.0 / 255.0;
/** The allowance first decided, 16/255: the control that shows the gate can fail runs with it. */
constexpr double kOverlapFirst = 16.0 / 255.0;
/** The expansion's tolerance in pixels (the default, 4 units of 1/64). */
constexpr double kTau = 4.0 / 64.0;

/** A path from a compact description: 'M' x y, 'L' x y, 'Q' x1 y1 x y, 'C' x1 y1 x2 y2 x y, 'Z'. */
typedef std::vector<std::pair<char, std::vector<double>>> Ops;

inline GSHGEOM_Path * build(const Ops & ops) {
  GSHGEOM_Path * p = nullptr;
  if (gshgeom_path_create(nullptr, nullptr, 2, &p) != GSHGEOM_OK) {
    return nullptr;
  }
  auto pt = [](double x, double y) {
    GSHGEOM_Point q = {x, y, 0.0};
    return q;
  };
  for (const auto & op : ops) {
    const std::vector<double> & a = op.second;
    switch (op.first) {
      case 'M':
        gshgeom_path_move_to(p, pt(a[0], a[1]));
        break;
      case 'L':
        gshgeom_path_line_to(p, pt(a[0], a[1]));
        break;
      case 'Q':
        gshgeom_path_quadratic_to(p, pt(a[0], a[1]), pt(a[2], a[3]));
        break;
      case 'C':
        gshgeom_path_cubic_to(p, pt(a[0], a[1]), pt(a[2], a[3]), pt(a[4], a[5]));
        break;
      case 'Z':
        gshgeom_path_close(p);
        break;
    }
  }
  return p;
}

/** A stroke's parameters with the dashes held. */
struct Params {
  GSHGEOM_Stroke s;
  std::vector<double> dashes;
  Params() {
    gshgeom_stroke_init(&s);
  }
  const GSHGEOM_Stroke & get() {
    s.dash_count = dashes.size();
    s.dashes = dashes.empty() ? nullptr : dashes.data();
    return s;
  }
};

inline so::Style oracle_style(const GSHGEOM_Stroke & s) {
  so::Style o;
  o.width = s.width;
  o.space = (int)s.width_space;
  o.join = (int)s.join;
  o.limit = s.miter_limit;
  o.cap = (int)s.cap;
  o.degenerate = (int)s.degenerate;
  for (size_t i = 0; i < s.dash_count; i++) {
    o.dashes.push_back(s.dashes[i]);
  }
  o.dash_offset = s.dash_offset;
  o.dash_scope = (int)s.dash_scope;
  return o;
}

/** The fill region of the closed subpaths with extent, mapped to device pixels: flattened finely, an oracle of its own. */
struct FillRegion {
  std::vector<std::vector<so::V2>> rings;
  bool even_odd = false;
  std::vector<so::V2> boundary; ///< points of its boundary, at most 1/64 pixel apart
  bool contains(so::V2 x) const {
    int winding = 0;
    for (const auto & ring : rings) {
      for (size_t i = 0; i < ring.size(); i++) {
        so::V2 a = ring[i];
        so::V2 b = ring[(i + 1) % ring.size()];
        if ((a.y <= x.y) != (b.y <= x.y)) {
          so::Real t = (x.y - a.y) / (b.y - a.y);
          so::Real cx = a.x + t * (b.x - a.x);
          if (cx > x.x) {
            winding += b.y > a.y ? 1 : -1;
          }
        }
      }
    }
    return even_odd ? (winding & 1) != 0 : winding != 0;
  }
};

inline FillRegion fill_region(const so::Centerline & cl, const so::Affine & map, bool even_odd) {
  FillRegion f;
  f.even_odd = even_odd;
  for (const so::Sub & sub : cl.subs) {
    if (!sub.closed || sub.segs.empty()) {
      continue;
    }
    std::vector<so::V2> ring;
    ring.push_back(map.apply(sub.start));
    for (const so::Seg & seg : sub.segs) {
      so::Poly poly = so::make_poly(seg);
      for (int i = 1; i <= 512; i++) {
        ring.push_back(map.apply(so::poly_point(poly, (so::Real)i / 512)));
      }
    }
    f.rings.push_back(ring);
    for (size_t i = 0; i < ring.size(); i++) {
      so::V2 a = ring[i];
      so::V2 b = ring[(i + 1) % ring.size()];
      int steps = (int)std::ceil(so::norm(b - a) * 64) + 1;
      for (int k = 0; k < steps; k++) {
        f.boundary.push_back(a + ((so::Real)k / steps) * (b - a));
      }
    }
  }
  return f;
}

/** What one case found. */
struct Result {
  long pixels = 0;
  long decided = 0;       ///< pixels whose bracket is narrower than the pixel
  long excluded = 0;      ///< aligned: near both the aligned part's boundary and the fill region's
  long ambiguous = 0;     ///< pixels with a cell the oracle could not classify
  long violations = 0;
  double worst_over = 0;  ///< the largest of (k/255 - upper - e), in 1/255 units, over all pixels (0 when none)
  double worst_under = 0; ///< the largest of (lower - e - k/255), in 1/255 units
  std::string first;      ///< the first violation, described
  std::vector<float> lower; ///< each pixel's bracket (for a report of a failure), row by row
  std::vector<float> upper;
};

/** A picture of a failed case: the coverage and the bracket, a character per pixel, row by row. */
inline std::string picture(const Result & r, int width, int height, const std::vector<uint8_t> & coverage) {
  std::string out;
  const char* ramp = " .:-=+*#%@";
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      out += ramp[std::min(9, coverage[(size_t)y * width + x] * 10 / 256)];
    }
    out += "   ";
    for (int x = 0; x < width; x++) {
      size_t i = (size_t)y * width + x;
      double lo = i < r.lower.size() ? r.lower[i] : -1;
      double hi = i < r.upper.size() ? r.upper[i] : -1;
      out += lo < 0 ? '?' : ramp[std::min(9, (int)(((lo + hi) / 2) * 9.99))];
    }
    out += "\n";
  }
  return out;
}

/**
 * The bracket for one stroke: @p coverage holds `k` for each pixel of a @p width by @p height raster, row by row; @p map
 * is the stroke's map to device pixels, @p alignment 0, 1 (inside) or 2 (outside), @p even_odd the alignment rule.
 */
inline Result bracket(const GSHGEOM_Path * path, const GSHGEOM_Stroke & stroke, const GSHGEOM_Map & map, int alignment,
    bool even_odd, int width, int height, const std::vector<uint8_t> & coverage, double overlap = kOverlap) {
  Result res;
  so::Centerline cl = so::read_centerline(path);
  so::Style style = oracle_style(stroke);
  so::Affine af = so::affine_of(map);
  so::Stroked st = so::build_stroke(cl, style, af, alignment);
  FillRegion fill = fill_region(cl, af, even_odd);
  const so::Real sigma = 1.0L / 64;
  const so::Real m = sigma + 1e-6L;
  const int n = 8;                                        // cells per side of a pixel
  const so::Real r = std::sqrt((so::Real)2) / (2 * n);    // a cell's half diagonal
  const so::Real reach = kTau + m + r;                     // beyond it a cell is decided
  so::Samples all;
  all.cell = 2;
  so::Samples aligned_boundary;
  aligned_boundary.cell = 2;
  so::Samples fill_boundary;
  fill_boundary.cell = 2;
  so::sample_region(all, st.body, sigma);
  if (st.has_aligned) {
    so::sample_region(all, st.aligned, sigma);
    so::sample_region(aligned_boundary, st.aligned, sigma);
    for (const so::V2 & q : fill.boundary) {
      all.add(q);
      fill_boundary.add(q);
    }
  }
  auto contains = [&](so::V2 x, bool * ambiguous) {
    so::Verdict vb = so::region_contains(st.body, x);
    if (vb.inside) {
      return true;
    }
    if (vb.ambiguous) {
      *ambiguous = true;
    }
    if (!st.has_aligned) {
      return false;
    }
    so::Verdict va = so::region_contains(st.aligned, x);
    if (va.ambiguous) {
      *ambiguous = true;
    }
    if (!va.inside) {
      return false;
    }
    bool in_fill = fill.contains(x);
    return alignment == 1 ? in_fill : !in_fill;
  };
  const double e = 2.0 / 255.0;
  res.lower.assign((size_t)width * height, -1.0f);
  res.upper.assign((size_t)width * height, -1.0f);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      res.pixels++;
      double k = coverage[(size_t)y * width + x] / 255.0;
      so::V2 center = {x + 0.5L, y + 0.5L};
      if (st.has_aligned && aligned_boundary.nearest(center, 2) < 1.6L && fill_boundary.nearest(center, 2) < 1.6L) {
        res.excluded++;
        continue;
      }
      double lower;
      double upper;
      bool ambiguous = false;
      if (all.nearest(center, 2) >= 1.2L) {
        bool in = contains(center, &ambiguous);
        if (ambiguous) {
          res.ambiguous++;
          continue;
        }
        lower = upper = in ? 1.0 : 0.0;
      } else {
        int inside = 0;
        int outside = 0;
        for (int j = 0; j < n; j++) {
          for (int i = 0; i < n; i++) {
            so::V2 q = {x + (i + 0.5L) / n, y + (j + 0.5L) / n};
            if (all.nearest(q, reach) < reach) {
              continue; // not decided
            }
            bool amb = false;
            bool in = contains(q, &amb);
            if (amb) {
              continue;
            }
            (in ? inside : outside)++;
          }
        }
        lower = (double)inside / (n * n);
        upper = (double)(n * n - outside) / (n * n);
      }
      res.lower[(size_t)y * width + x] = (float)lower;
      res.upper[(size_t)y * width + x] = (float)upper;
      if (upper - lower < 1.0) {
        res.decided++;
      }
      double over = k - upper - e;
      double under = lower - e - k;
      if (over > 0) {
        res.worst_over = std::max(res.worst_over, over * 255.0);
      }
      if (under > 0) {
        res.worst_under = std::max(res.worst_under, under * 255.0);
      }
      if (over > overlap || under > 0) {
        if (res.violations++ == 0) {
          char buf[200];
          std::snprintf(buf, sizeof buf, "pixel (%d, %d): k %.4f outside [%.4f - e, %.4f + e + o]", x, y, k, lower, upper);
          res.first = buf;
        }
      }
    }
  }
  return res;
}

} // namespace stroke_gate

#endif /* GHOTI_IO_GSHCPU_TEST_STROKE_GATE_H */
