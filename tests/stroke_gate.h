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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace stroke_gate {

namespace so = stroke_oracle;

/**
 * The overlap allowance `o` (UC12): one level.  The sink's coverage is the area of the union of the outline's pieces, so
 * what is left of F4's overlap credit is the fold's and the fixed-point arithmetic's rounding, under one level.  The gate
 * fails past it and records the worst value.
 */
constexpr double kOverlap = 1.0 / 255.0;
/**
 * F4's stopgap allowance, 64/255, which held the sum of partial coverages (AD-12's saturating sum credits an overlap twice)
 * to the bracket before the exact step: the build with the step switched off passes at this bound and fails at `kOverlap`.
 */
constexpr double kOverlapStopgap = 64.0 / 255.0;
/** The allowance first decided, 16/255. */
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


/* --- the union gate (UC12): an independent reference for the area of what the outline covers ---------------------- */

/**
 * The coverage of a stroke must be the area of the union of the pieces of its outline.  This is the instrument that
 * says so, and it shares nothing with the sink's exact step (UC4 to UC6): for every pixel row it takes `rows` horizontal
 * sub-rows (midpoints, second-order error), and for each one sweeps every edge of the whole outline that crosses it,
 * sorts the crossings, keeps the winding and measures, in `long double`, the length of the stretches whose winding is
 * not zero.  The polygon is the one the rasterizer covers, not the one the expansion returned: every edge is cut at the
 * row and column boundaries with the rounding of UC2 (the only thing it takes from the sink's definition), because that
 * is the geometry whose area the sink computes.
 */
struct Pt {
  int64_t x, y;
};
typedef std::vector<Pt> Contour;
struct Part {
  std::vector<Contour> contours;
  int rule = 0;          ///< 0 nonzero, 1 even-odd
  bool complement = false;
};

inline int64_t round_div(int64_t n, int64_t d) {
  uint64_t a = (uint64_t)(n < 0 ? -n : n);
  uint64_t b = (uint64_t)(d < 0 ? -d : d);
  uint64_t q = (a + b / 2) / b;
  return (n < 0) != (d < 0) ? -(int64_t)q : (int64_t)q;
}

inline int64_t floor_div(int64_t n, int64_t d) {
  int64_t q = n / d;
  if (n % d != 0 && ((n < 0) != (d < 0))) {
    q--;
  }
  return q;
}

/**
 * @p a to @p b cut at row and column boundaries as the rasterizer does (UC2): the points after @p a, ending with @p b.  An
 * edge that goes up is cut from its upper end (the sink's exact step does, so that the two copies of a shared segment
 * are cut at the same points), and its points are given in its own direction.
 */
inline void cut_edge(Pt a, Pt b, std::vector<Pt> & out) {
  if (a.y == b.y) {
    out.push_back(b);
    return;
  }
  if (a.y > b.y) {
    std::vector<Pt> down;
    cut_edge(b, a, down); // the points after b, ending with a
    out.insert(out.end(), down.rbegin() + 1, down.rend());
    out.push_back(b);
    return;
  }
  int64_t ey1 = floor_div(a.y, 64);
  int64_t ey2 = floor_div(b.y, 64);
  bool down = ey2 > ey1;
  int64_t lo = std::min(ey1, ey2);
  int64_t hi = std::max(ey1, ey2);
  for (int64_t k = 0; k <= hi - lo; k++) {
    int64_t ey = down ? lo + k : hi - k;
    auto row_cut = [&](int64_t boundary) {
      Pt p;
      p.x = a.x + round_div((boundary - a.y) * (b.x - a.x), b.y - a.y);
      p.y = boundary;
      return p;
    };
    Pt from = ey == ey1 ? a : row_cut((down ? ey : ey + 1) * 64);
    Pt to = ey == ey2 ? b : row_cut((down ? ey + 1 : ey) * 64);
    if (from.y == to.y) {
      if (!(to.x == from.x)) {
        out.push_back(to);
      }
      continue;
    }
    int64_t ex1 = floor_div(from.x, 64);
    int64_t ex2 = floor_div(to.x, 64);
    if (ex1 != ex2) {
      bool right = ex2 > ex1;
      int64_t clo = std::min(ex1, ex2);
      int64_t chi = std::max(ex1, ex2);
      for (int64_t j = 0; j < chi - clo; j++) {
        int64_t boundary = (right ? clo + j + 1 : chi - j) * 64;
        Pt c;
        c.x = boundary;
        c.y = from.y + round_div((boundary - from.x) * (to.y - from.y), to.x - from.x);
        out.push_back(c);
      }
    }
    out.push_back(to);
  }
}

/** The contour (closed: the last point joins the first) with every edge cut. */
inline Contour cut_contour(const Contour & c) {
  Contour out;
  if (c.empty()) {
    return out;
  }
  out.push_back(c[0]);
  for (size_t i = 0; i < c.size(); i++) {
    cut_edge(c[i], c[(i + 1) % c.size()], out);
  }
  out.pop_back(); // the last point is the first again
  return out;
}

/** One part's covered stretches at height @p y (pixels): ascending, disjoint, as [begin, end] pairs in pixels. */
inline std::vector<std::pair<long double, long double>> stretches(const Part & part, long double y) {
  std::vector<std::pair<long double, int>> hits;
  for (const Contour & c : part.contours) {
    size_t n = c.size();
    for (size_t i = 0; i < n; i++) {
      const Pt & p = c[i];
      const Pt & q = c[(i + 1) % n];
      long double ay = p.y / 64.0L;
      long double by = q.y / 64.0L;
      if ((ay <= y) == (by <= y)) {
        continue;
      }
      long double t = (y - ay) / (by - ay);
      long double x = (p.x + t * (q.x - p.x)) / 64.0L;
      hits.push_back({x, by > ay ? 1 : -1});
    }
  }
  std::sort(hits.begin(), hits.end());
  std::vector<std::pair<long double, long double>> out;
  int winding = 0;
  for (size_t i = 0; i < hits.size(); i++) {
    winding += hits[i].second;
    bool in = part.rule == 1 ? (winding & 1) != 0 : winding != 0;
    if (i + 1 < hits.size() && in) {
      out.push_back({hits[i].first, hits[i + 1].first});
    }
  }
  return out;
}

inline bool inside(const std::vector<std::pair<long double, long double>> & s, long double x) {
  for (const auto & iv : s) {
    if (x >= iv.first && x < iv.second) {
      return true;
    }
    if (iv.first > x) {
      break;
    }
  }
  return false;
}

/** Which rule the instrument applies to a pixel's stretches: the union, or (a plant) the sum of the parts' areas. */
enum class Rule { Union, SumOfPieces };

/**
 * The area, in pixels, of what is covered in each pixel of a @p width by @p height raster.  @p body and @p aligned are
 * nonzero parts; @p clip, if it has contours, is the fill region the aligned part is cut by (UC8): the region is
 * `body U (aligned & clip)` (or its complement).  With Rule::SumOfPieces each contour's own area is added instead (the
 * plant that the instrument's self-check must see fail).
 */
inline std::vector<double> union_area(const Part & body, const Part * aligned, const Part * clip, int width, int height,
    int rows = 4096, Rule rule = Rule::Union) {
  std::vector<double> area((size_t)width * height, 0.0);
  for (int py = 0; py < height; py++) {
    for (int r = 0; r < rows; r++) {
      long double y = py + (r + 0.5L) / rows;
      if (rule == Rule::SumOfPieces) {
        for (const Contour & c : body.contours) {
          Part one;
          one.contours.push_back(c);
          for (auto & iv : stretches(one, y)) {
            for (int px = 0; px < width; px++) {
              long double lo = std::max<long double>(iv.first, px);
              long double hi = std::min<long double>(iv.second, px + 1);
              if (hi > lo) {
                area[(size_t)py * width + px] += (double)((hi - lo) / rows);
              }
            }
          }
        }
        continue;
      }
      auto b = stretches(body, y);
      std::vector<std::pair<long double, long double>> a;
      std::vector<std::pair<long double, long double>> c;
      if (aligned) {
        a = stretches(*aligned, y);
        c = stretches(*clip, y);
      }
      std::vector<long double> xs;
      xs.push_back(0.0L);
      xs.push_back((long double)width);
      for (auto * set : {&b, &a, &c}) {
        for (auto & iv : *set) {
          xs.push_back(iv.first);
          xs.push_back(iv.second);
        }
      }
      std::sort(xs.begin(), xs.end());
      for (size_t i = 0; i + 1 < xs.size(); i++) {
        long double x0 = xs[i];
        long double x1 = xs[i + 1];
        if (x1 <= 0 || x0 >= width || x1 <= x0) {
          continue;
        }
        long double mid = (x0 + x1) / 2;
        bool in = inside(b, mid);
        if (!in && aligned && inside(a, mid)) {
          bool in_clip = inside(c, mid);
          in = clip->complement ? !in_clip : in_clip;
        }
        if (!in) {
          continue;
        }
        for (int px = std::max(0, (int)std::floor((double)x0)); px < width && px < x1; px++) {
          long double lo = std::max<long double>(x0, px);
          long double hi = std::min<long double>(x1, px + 1);
          if (hi > lo) {
            area[(size_t)py * width + px] += (double)((hi - lo) / rows);
          }
        }
      }
    }
  }
  return area;
}

/** The parts of a stroke's outline as the instrument's contours, in device pixels' 26.6, whole-pixel shift applied. */
inline bool outline_parts(const GSHGEOM_Path * path, const GSHGEOM_Stroke & stroke, const GSHGEOM_Map & map, Part * body,
    Part * aligned, Part * clip, bool * has_aligned, bool cut = true) {
  GSHGEOM_Map canon;
  int64_t sx = 0;
  int64_t sy = 0;
  if (gshgeom_stroke_canonical_map(&map, &canon, &sx, &sy) != GSHGEOM_OK) {
    return false;
  }
  GSHGEOM_Stroke_Outline * o = nullptr;
  if (gshgeom_stroke_expand(path, &stroke, &canon, nullptr, nullptr, &o) != GSHGEOM_OK) {
    return false;
  }
  auto take = [&](const GSHGEOM_Flat_Path * fp, Part * out) {
    if (!fp) {
      return false;
    }
    const GSHGEOM_Device_Point * pts = gshgeom_flat_path_points(fp);
    for (size_t sub = 0; sub < gshgeom_flat_path_subpath_count(fp); sub++) {
      GSHGEOM_Flat_Subpath fs;
      gshgeom_flat_path_subpath(fp, sub, &fs);
      Contour c;
      for (size_t k = 0; k < fs.count; k++) {
        Pt p;
        p.x = pts[fs.first + k].x + 64 * sx;
        p.y = pts[fs.first + k].y + 64 * sy;
        c.push_back(p);
      }
      out->contours.push_back(cut ? cut_contour(c) : c);
    }
    return true;
  };
  take(gshgeom_stroke_outline_body(o), body);
  *has_aligned = take(gshgeom_stroke_outline_aligned(o), aligned);
  if (*has_aligned) {
    take(gshgeom_stroke_outline_clip(o), clip);
    clip->rule = gshgeom_stroke_outline_clip_rule(o) == GSHGEOM_FILL_EVEN_ODD ? 1 : 0;
    clip->complement = gshgeom_stroke_outline_clip_complement(o) != 0;
  }
  gshgeom_stroke_outline_destroy(o);
  return true;
}

/** What the union gate found in one case. */
struct UnionResult {
  long pixels = 0;
  long violations = 0;
  double worst = 0;     ///< the largest |k - 255 A| in levels
  std::string first;
};

/** The union gate's bound, in levels: the fold's floor (1), the fixed-point arithmetic (a quarter), the instrument (an eighth). */
constexpr double kUnionBound = 1.5;

/**
 * Every pixel's coverage @p coverage against the area of the union.  @p aligned_too includes the aligned part and the fill
 * region (UC8); without it only the body is judged and a stroke with an aligned part is left to the bracket.
 */
inline UnionResult union_gate(const GSHGEOM_Path * path, const GSHGEOM_Stroke & stroke, const GSHGEOM_Map & map, int width,
    int height, const std::vector<uint8_t> & coverage, bool aligned_too, int rows = 4096) {
  UnionResult res;
  Part body;
  Part aligned;
  Part clip;
  bool has_aligned = false;
  if (!outline_parts(path, stroke, map, &body, &aligned, &clip, &has_aligned)) {
    res.violations = 1;
    res.first = "the outline could not be made";
    return res;
  }
  if (has_aligned && !aligned_too) {
    return res;
  }
  std::vector<double> area = union_area(body, has_aligned ? &aligned : nullptr, has_aligned ? &clip : nullptr, width, height, rows);
  for (int i = 0; i < width * height; i++) {
    res.pixels++;
    double d = std::fabs(coverage[(size_t)i] - 255.0 * area[(size_t)i]);
    res.worst = std::max(res.worst, d);
    if (d > kUnionBound && res.violations++ == 0) {
      char buf[160];
      std::snprintf(buf, sizeof buf, "pixel (%d, %d): k %d, the union's area %.4f (%.2f levels)", i % width, i / width,
          (int)coverage[(size_t)i], area[(size_t)i], coverage[(size_t)i] - 255.0 * area[(size_t)i]);
      res.first = buf;
    }
  }
  return res;
}

} // namespace stroke_gate

#endif /* GHOTI_IO_GSHCPU_TEST_STROKE_GATE_H */
