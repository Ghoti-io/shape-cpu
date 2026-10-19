/**
 * @file
 *
 * Coverage (CAP-6): the scan converter moved from font, against references
 * that share nothing with it.
 *
 *  - known answers from tools/gen-coverage-vectors.py: the definition by
 *    integration for axis-aligned polygons, a second program of the algorithm
 *    for slanted ones;
 *  - a double-precision exact-area clipper, for slanted shapes, with the
 *    tolerance the 26.6 splitting of an edge needs;
 *  - the properties of the two fill rules.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "coverage_helpers.h"
#include "known_answers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace cov;

namespace {

GSHCPU_Result fill_into(const Path & path, GSHGEOM_Fill_Rule fill, int32_t left,
    int32_t top, uint32_t w, uint32_t h, Cov & out,
    const GSHCPU_Limits * limits = nullptr) {
  return gshcpu_coverage_fill(path, fill, left, top, w, h, limits, nullptr,
      &out.c);
}

}  // namespace

// --- the known answers -----------------------------------------------------

TEST(CoverageKnown, EveryCaseFilledIntoItsRectangleIsTheReferenceByteForByte) {
  for (const known::Fill & k : known::fills()) {
    Path path;
    for (const auto & contour : k.contours) {
      path.polygon(Contour(contour.begin(), contour.end()));
    }
    Cov out;
    ASSERT_EQ(fill_into(path, static_cast<GSHGEOM_Fill_Rule>(k.fill), k.left,
                  k.top, k.width, k.height, out), GSHCPU_OK) << k.name;
    EXPECT_EQ(out.c.left, k.left) << k.name;
    EXPECT_EQ(out.c.top, k.top) << k.name;
    EXPECT_EQ(out.c.width, k.width) << k.name;
    EXPECT_EQ(out.c.height, k.height) << k.name;
    EXPECT_EQ(out.hex(), k.rows) << k.name;
    EXPECT_EQ(gshcpu_coverage_hash(out.ptr()), k.hash) << k.name;
  }
}

TEST(CoverageKnown, EveryCaseFromItsPathIsTheTrimmedReferenceByteForByte) {
  for (const known::Fill & k : known::fills()) {
    Path path;
    for (const auto & contour : k.contours) {
      path.polygon(Contour(contour.begin(), contour.end()));
    }
    Cov out;
    ASSERT_EQ(gshcpu_coverage_from_path(path,
                  static_cast<GSHGEOM_Fill_Rule>(k.fill), nullptr, nullptr,
                  &out.c), GSHCPU_OK) << k.name;
    EXPECT_EQ(out.c.left, k.t_left) << k.name;
    EXPECT_EQ(out.c.top, k.t_top) << k.name;
    EXPECT_EQ(out.c.width, k.t_width) << k.name;
    EXPECT_EQ(out.c.height, k.t_height) << k.name;
    EXPECT_EQ(out.hex(), k.t_rows) << k.name;
    EXPECT_EQ(gshcpu_coverage_hash(out.ptr()), k.t_hash) << k.name;
  }
}

TEST(CoverageKnown, TheCorpusHasBothKindsOfShapeAndBothRules) {
  int axis = 0, slanted = 0, nz = 0, eo = 0;
  for (const known::Fill & k : known::fills()) {
    (k.axis_aligned ? axis : slanted) += 1;
    (k.fill == 0 ? nz : eo) += 1;
  }
  EXPECT_GE(axis, 30);
  EXPECT_GE(slanted, 60);
  EXPECT_GE(nz, 30);
  EXPECT_GE(eo, 30);
}

// --- exact area on a whole-pixel rectangle -----------------------------------

TEST(Coverage, AWholePixelRectangleIsFullInsideAndEmptyOutside) {
  Path path;
  path.polygon({{px(2), px(1)}, {px(5), px(1)}, {px(5), px(3)}, {px(2), px(3)}});
  Cov out;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 8, 5, out), GSHCPU_OK);
  for (uint32_t y = 0; y < 5; ++y) {
    for (uint32_t x = 0; x < 8; ++x) {
      const bool inside = x >= 2 && x < 5 && y >= 1 && y < 3;
      EXPECT_EQ(gshcpu_coverage_at(&out.c, x, y), inside ? 255 : 0)
          << x << "," << y;
    }
  }
  EXPECT_EQ(gshcpu_coverage_total(&out.c), 6u * 255u);
}

TEST(Coverage, AHalfPixelEdgeIsHalfCoverageToWithinTheLastBit) {
  Path path;
  path.polygon({{px(1) + 32, px(1)}, {px(3), px(1)}, {px(3), px(2)}, {px(1) + 32, px(2)}});
  Cov out;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 3, out), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 1, 1), 128);   // 256 * 1/2, floor of the exact value
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 2, 1), 255);
}

TEST(Coverage, EitherWindingGivesTheSameCoverageForOneContour) {
  Path cw, ccw;
  cw.polygon({{px(1), px(1)}, {px(4) + 5, px(1) + 9}, {px(2) + 7, px(4) + 3}});
  ccw.polygon({{px(2) + 7, px(4) + 3}, {px(4) + 5, px(1) + 9}, {px(1), px(1)}});
  Cov a, b;
  ASSERT_EQ(fill_into(cw, GSHGEOM_FILL_NONZERO, 0, 0, 7, 7, a), GSHCPU_OK);
  ASSERT_EQ(fill_into(ccw, GSHGEOM_FILL_NONZERO, 0, 0, 7, 7, b), GSHCPU_OK);
  // The area of a pixel is the same either way; the fold takes the floor of
  // the signed area before the absolute value, so the two can differ by one
  // level on a partly covered pixel and nowhere else.
  int worst = 0;
  for (uint32_t y = 0; y < 7; ++y) {
    for (uint32_t x = 0; x < 7; ++x) {
      worst = std::max(worst, std::abs((int)gshcpu_coverage_at(&a.c, x, y)
                                       - (int)gshcpu_coverage_at(&b.c, x, y)));
    }
  }
  EXPECT_LE(worst, 1);
}

// --- slanted shapes against the exact area -------------------------------------

TEST(Coverage, SlantedPolygonsAreWithinTheRoundingBoundOfTheExactArea) {
  // The bound: an edge is split where it crosses a pixel boundary and each
  // split point is rounded once to 1/64 pixel, which moves the edge by at
  // most 1/128 pixel along the boundary; a pixel has at most two such points
  // on one edge, and each moves the area by at most 1/128 of a pixel, which is
  // 2 of the 256 levels.  Taking the floor of the signed area adds at most 1.
  constexpr int kBound = 5;
  int worst = 0;
  long compared = 0;
  for (const known::Fill & k : known::fills()) {
    if (k.axis_aligned || k.contours.size() != 1) {
      continue;
    }
    const Contour poly(k.contours[0].begin(), k.contours[0].end());
    if (k.fill == 1 && std::string(k.name).find("pentagram") == 0) {
      continue;   // a self-crossing polygon: the clipper's winding is not simple
    }
    // Only simple polygons are clipped exactly; a random polygon may cross
    // itself, which Sutherland-Hodgman does not handle, so test it for
    // simplicity first.
    bool simple = true;
    const size_t n = poly.size();
    for (size_t i = 0; i < n && simple; ++i) {
      for (size_t j = i + 2; j < n && simple; ++j) {
        if (i == 0 && j == n - 1) {
          continue;
        }
        const auto cross = [](Pt o, Pt a, Pt b) {
          return (double)(a.first - o.first) * (b.second - o.second)
              - (double)(a.second - o.second) * (b.first - o.first);
        };
        const Pt a = poly[i], b = poly[(i + 1) % n], c = poly[j], d = poly[(j + 1) % n];
        const double d1 = cross(a, b, c), d2 = cross(a, b, d);
        const double d3 = cross(c, d, a), d4 = cross(c, d, b);
        if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 * d2 != 0
            && d3 * d4 != 0) {
          simple = false;
        }
      }
    }
    // Sutherland-Hodgman is exact for a convex polygon and for a concave one
    // only where the clip does not split it; take convex polygons.
    bool convex = simple;
    int sign = 0;
    for (size_t i = 0; i < n && convex; ++i) {
      const Pt a = poly[i], b = poly[(i + 1) % n], c = poly[(i + 2) % n];
      const double z = (double)(b.first - a.first) * (c.second - b.second)
          - (double)(b.second - a.second) * (c.first - b.first);
      if (z != 0) {
        const int s = z > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) convex = false;
      }
    }
    if (!convex || area2(poly) == 0) {
      continue;
    }
    Path path;
    path.polygon(poly);
    Cov out;
    ASSERT_EQ(fill_into(path, static_cast<GSHGEOM_Fill_Rule>(k.fill), k.left,
                  k.top, k.width, k.height, out), GSHCPU_OK) << k.name;
    for (uint32_t y = 0; y < k.height; ++y) {
      for (uint32_t x = 0; x < k.width; ++x) {
        const double exact = pixel_area(poly, ((int64_t)k.left + x) * 64,
            ((int64_t)k.top + y) * 64);
        int want = (int)std::floor(exact * 256.0);
        if (want > 255) want = 255;
        const int got = gshcpu_coverage_at(&out.c, x, y);
        worst = std::max(worst, std::abs(got - want));
        compared += 1;
        ASSERT_LE(std::abs(got - want), kBound) << k.name << " at " << x << "," << y
            << " exact " << exact * 256.0;
      }
    }
  }
  EXPECT_GT(compared, 1000) << "too few pixels were compared for this to mean anything";
  std::printf("# slanted convex polygons: %ld pixels, worst difference %d\n",
      compared, worst);
}

// --- the two fill rules ------------------------------------------------------

namespace {

Contour square(int x0, int y0, int x1, int y1) {
  return {{px(x0), px(y0)}, {px(x1), px(y0)}, {px(x1), px(y1)}, {px(x0), px(y1)}};
}

}  // namespace

TEST(CoverageFill, NonzeroSaturatesWhereWindingIsTwoAndEvenOddIsEmpty) {
  Path path;
  path.polygon(square(1, 1, 5, 5));
  path.polygon(square(2, 2, 4, 4));   // same direction: winding 2 inside
  Cov nz, eo;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 7, 7, nz), GSHCPU_OK);
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_EVEN_ODD, 0, 0, 7, 7, eo), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 3, 3), 255);
  EXPECT_EQ(gshcpu_coverage_at(&eo.c, 3, 3), 0);
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 1, 1), 255);
  EXPECT_EQ(gshcpu_coverage_at(&eo.c, 1, 1), 255);
  EXPECT_EQ(nz.c.fill, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(eo.c.fill, GSHGEOM_FILL_EVEN_ODD);
}

TEST(CoverageFill, OppositeWindingsCancelUnderNonzeroOnlyWhereTheyOverlap) {
  Path path;
  path.polygon(square(1, 1, 5, 5));
  path.polygon({{px(2), px(2)}, {px(2), px(4)}, {px(4), px(4)}, {px(4), px(2)}});   // the other way
  Cov nz;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 7, 7, nz), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 3, 3), 0);   // winding 0: a hole
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 1, 3), 255);
}

TEST(CoverageFill, TheRampFoldsAtWindingTwoUnderEvenOdd) {
  // Three squares of the same direction offset by a half pixel: the half
  // pixel overlaps have winding 2, partly covered pixels fold.
  Path path;
  path.polygon({{px(1), px(1)}, {px(3), px(1)}, {px(3), px(3)}, {px(1), px(3)}});
  path.polygon({{px(1) + 32, px(1)}, {px(3) + 32, px(1)}, {px(3) + 32, px(3)}, {px(1) + 32, px(3)}});
  Cov eo, nz;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_EVEN_ODD, 0, 0, 6, 5, eo), GSHCPU_OK);
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 6, 5, nz), GSHCPU_OK);
  // Pixel column 1 holds winding 1 on its left half and 2 on its right half:
  // accumulated area 1.5 pixels, which even-odd folds to 0.5.
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 1, 1), 255);
  EXPECT_EQ(gshcpu_coverage_at(&eo.c, 1, 1), 128);
  // Column 2 is winding 2 throughout: nonzero saturates, even-odd is empty.
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 2, 1), 255);
  EXPECT_EQ(gshcpu_coverage_at(&eo.c, 2, 1), 0);
  // Column 3: the first square has ended, the second covers its left half.
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 3, 1), 128);
  EXPECT_EQ(gshcpu_coverage_at(&eo.c, 3, 1), 128);
}

TEST(CoverageFill, WindingThreeUnderEvenOddIsFull) {
  Path path;
  path.polygon(square(1, 1, 4, 4));
  path.polygon(square(1, 1, 4, 4));
  path.polygon(square(1, 1, 4, 4));
  Cov eo, nz;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_EVEN_ODD, 0, 0, 6, 6, eo), GSHCPU_OK);
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 6, 6, nz), GSHCPU_OK);
  EXPECT_EQ(gshcpu_coverage_at(&eo.c, 2, 2), 255);   // 768 folds to 256, which is 255
  EXPECT_EQ(gshcpu_coverage_at(&nz.c, 2, 2), 255);
}

TEST(CoverageFill, AFillRuleOutsideTheEnumIsInvalidAndNothingIsWritten) {
  Path path;
  path.polygon(square(1, 1, 3, 3));
  Cov out;
  out.c.width = 99;
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_RULE_COUNT, 0, 0, 5, 5,
                nullptr, nullptr, &out.c), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_coverage_fill(path, static_cast<GSHGEOM_Fill_Rule>(-1), 0, 0,
                5, 5, nullptr, nullptr, &out.c), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_RULE_COUNT, nullptr,
                nullptr, &out.c), GSHCPU_ERR_INVALID);
  EXPECT_EQ(out.c.width, 99u);
  out.c.width = 0;
}

// --- curves, open subpaths, degenerate paths -------------------------------------

TEST(CoverageCurves, ACurveIsFlattenedByShapeGeometryThenFilled) {
  Path curves;
  curves.move(px(1), px(4));
  curves.quad(px(3), px(-1), px(6), px(4));
  curves.cubic(px(5), px(6), px(2), px(7), px(1), px(4));
  curves.close();

  GSHGEOM_Flat_Path * flat = nullptr;
  ASSERT_EQ(gshgeom_device_path_flatten(curves, nullptr, nullptr, &flat), GSHGEOM_OK);
  ASSERT_GT(gshgeom_flat_path_point_count(flat), 8u);
  Path lines;
  GSHGEOM_Flat_Subpath sub;
  ASSERT_EQ(gshgeom_flat_path_subpath(flat, 0, &sub), GSHGEOM_OK);
  const GSHGEOM_Device_Point * p = gshgeom_flat_path_points(flat);
  for (size_t i = 0; i < sub.count; ++i) {
    if (i == 0) lines.move(p[i].x, p[i].y);
    else lines.line(p[i].x, p[i].y);
  }
  lines.close();
  gshgeom_flat_path_destroy(flat);

  for (int fill = 0; fill < GSHGEOM_FILL_RULE_COUNT; ++fill) {
    Cov a, b;
    ASSERT_EQ(fill_into(curves, static_cast<GSHGEOM_Fill_Rule>(fill), -1, -3, 10, 14, a), GSHCPU_OK);
    ASSERT_EQ(fill_into(lines, static_cast<GSHGEOM_Fill_Rule>(fill), -1, -3, 10, 14, b), GSHCPU_OK);
    EXPECT_EQ(a.hex(), b.hex()) << fill;
    EXPECT_GT(gshcpu_coverage_total(&a.c), 1000u);
  }
}

TEST(CoverageCurves, ACircleOfCubicsHasTheAreaOfACircle) {
  const double k = 0.5522847498307936;   // the cubic circle constant
  const double r = 20.0 * 64.0, cx = 24.0 * 64.0, cy = 24.0 * 64.0;
  auto c = [](double v) { return (int32_t)std::lround(v); };
  Path path;
  path.move(c(cx + r), c(cy));
  path.cubic(c(cx + r), c(cy + k * r), c(cx + k * r), c(cy + r), c(cx), c(cy + r));
  path.cubic(c(cx - k * r), c(cy + r), c(cx - r), c(cy + k * r), c(cx - r), c(cy));
  path.cubic(c(cx - r), c(cy - k * r), c(cx - k * r), c(cy - r), c(cx), c(cy - r));
  path.cubic(c(cx + k * r), c(cy - r), c(cx + r), c(cy - k * r), c(cx + r), c(cy));
  path.close();
  Cov out;
  ASSERT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, nullptr,
                nullptr, &out.c), GSHCPU_OK);
  const double area = 3.14159265358979 * 400.0;
  const double got = (double)gshcpu_coverage_total(&out.c) / 255.0;
  // The cubic approximation of a circle is within 0.03 percent in radius.
  EXPECT_NEAR(got, area, area * 0.002);
  EXPECT_EQ(out.c.width, 40u);   // the trim removes the margin the extent was grown by
}

TEST(CoverageOpen, AnOpenSubpathIsFilledAsIfItWereClosed) {
  Path open, closed;
  open.polygon({{px(1), px(1)}, {px(5) + 3, px(2)}, {px(3), px(5)}}, false);
  closed.polygon({{px(1), px(1)}, {px(5) + 3, px(2)}, {px(3), px(5)}}, true);
  Cov a, b;
  ASSERT_EQ(fill_into(open, GSHGEOM_FILL_NONZERO, 0, 0, 8, 8, a), GSHCPU_OK);
  ASSERT_EQ(fill_into(closed, GSHGEOM_FILL_NONZERO, 0, 0, 8, 8, b), GSHCPU_OK);
  EXPECT_EQ(a.hex(), b.hex());
  EXPECT_GT(gshcpu_coverage_total(&a.c), 0u);
}

TEST(CoverageOpen, ASubpathAfterACloseStartsAtTheClosedStart) {
  Path path;
  path.polygon(square(1, 1, 3, 3));
  path.line(px(3), px(1));   // a drawing verb after the close: a new subpath from (1,1)
  Cov out;
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 5, out), GSHCPU_OK);
}

TEST(CoverageDegenerate, NothingToDrawIsAnEmptyCoverage) {
  Path none;
  Path lone;
  lone.move(px(2), px(2));
  Path horizontal;
  horizontal.move(px(1), px(2)).line(px(4), px(2)).line(px(1), px(2)).close();
  Path sliver;   // zero area: out and back along one line
  sliver.move(px(1), px(1)).line(px(4), px(3)).line(px(1), px(1)).close();
  Path point;
  point.move(px(3), px(3)).line(px(3), px(3)).line(px(3), px(3)).close();
  struct Case { const char * name; Path * path; };
  const Case cases[] = {{"no element", &none}, {"a lone move", &lone},
      {"horizontal only", &horizontal}, {"out and back", &sliver},
      {"a point", &point}};
  for (const Case & c : cases) {
    Cov out;
    ASSERT_EQ(gshcpu_coverage_from_path(*c.path, GSHGEOM_FILL_NONZERO, nullptr,
                  nullptr, &out.c), GSHCPU_OK) << c.name;
    EXPECT_EQ(out.c.width, 0u) << c.name;
    EXPECT_EQ(out.c.height, 0u) << c.name;
    EXPECT_EQ(out.c.data, nullptr) << c.name;
    EXPECT_EQ(out.c.left, 0) << c.name;
    EXPECT_EQ(out.c.top, 0) << c.name;
    EXPECT_EQ(out.c.fill, GSHGEOM_FILL_NONZERO) << c.name;
    EXPECT_EQ(out.c.allocator, gshgeom_allocator_default()) << c.name;
    EXPECT_EQ(gshcpu_coverage_total(&out.c), 0u) << c.name;
  }
}

TEST(CoverageDegenerate, AZeroSizedRectangleIsEmptyEvenForAPathOutsideIt) {
  Path path;
  path.polygon(square(1, 1, 3, 3));
  Cov a, b;
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 0, 5, a), GSHCPU_OK);
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 0, b), GSHCPU_OK);
  EXPECT_EQ(a.c.data, nullptr);
  EXPECT_EQ(b.c.data, nullptr);
}

TEST(CoverageDegenerate, APathOutsideTheRectangleIsInvalidAndNothingIsWritten) {
  Path path;
  path.polygon(square(1, 1, 3, 3));
  Cov out;
  out.c.height = 77;
  // Each side: a point on the far edge is outside (the rectangle is half open),
  // and so is one before the near edge.
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 3, 5, out), GSHCPU_ERR_INVALID);
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 3, out), GSHCPU_ERR_INVALID);
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 2, 0, 5, 5, out), GSHCPU_ERR_INVALID);
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 2, 5, 5, out), GSHCPU_ERR_INVALID);
  EXPECT_EQ(out.c.height, 77u);
  out.c.height = 0;
  // And the rectangle that has a pixel to spare on every side is fine.
  EXPECT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, 0, 0, 4, 4, out), GSHCPU_OK);
}

TEST(CoverageArguments, NullIsInvalid) {
  Path path;
  path.polygon(square(1, 1, 3, 3));
  Cov out;
  EXPECT_EQ(gshcpu_coverage_fill(nullptr, GSHGEOM_FILL_NONZERO, 0, 0, 5, 5,
                nullptr, nullptr, &out.c), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_coverage_fill(path, GSHGEOM_FILL_NONZERO, 0, 0, 5, 5,
                nullptr, nullptr, nullptr), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_coverage_from_path(nullptr, GSHGEOM_FILL_NONZERO, nullptr,
                nullptr, &out.c), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, nullptr,
                nullptr, nullptr), GSHCPU_ERR_INVALID);
  gshcpu_coverage_trim(nullptr);
  gshcpu_coverage_destroy(nullptr);
}

// --- trimming ---------------------------------------------------------------

TEST(CoverageTrim, MovesLeftAndTopBySomeRowsAndColumnsAndKeepsThePixels) {
  Path path;
  path.polygon(square(3, 2, 5, 4));
  Cov out;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, -1, -1, 9, 9, out), GSHCPU_OK);
  gshcpu_coverage_trim(&out.c);
  EXPECT_EQ(out.c.left, 3);
  EXPECT_EQ(out.c.top, 2);
  EXPECT_EQ(out.c.width, 2u);
  EXPECT_EQ(out.c.height, 2u);
  EXPECT_EQ(out.c.stride, 2u);
  EXPECT_EQ(out.hex(), "ffffffff");
  // Trimming what is trimmed already changes nothing.
  gshcpu_coverage_trim(&out.c);
  EXPECT_EQ(out.c.left, 3);
  EXPECT_EQ(out.c.width, 2u);
}

TEST(CoverageTrim, ACoverageWithNoPixelBecomesEmptyAndFreesItsData) {
  Path path;
  path.move(px(1), px(1)).line(px(3), px(1)).line(px(1), px(1)).close();
  Cov out;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, -2, -3, 6, 6, out), GSHCPU_OK);
  ASSERT_NE(out.c.data, nullptr);
  gshcpu_coverage_trim(&out.c);
  EXPECT_EQ(out.c.data, nullptr);
  EXPECT_EQ(out.c.width, 0u);
  EXPECT_EQ(out.c.height, 0u);
  EXPECT_EQ(out.c.left, 0);
  EXPECT_EQ(out.c.top, 0);
  EXPECT_EQ(out.c.stride, 0u);
}

TEST(CoverageTrim, AnEmptyCoverageIsLeftAsItIs) {
  GSHCPU_Coverage zero{};
  gshcpu_coverage_trim(&zero);
  EXPECT_EQ(zero.data, nullptr);
  EXPECT_EQ(zero.width, 0u);
  EXPECT_EQ(zero.left, 0);
  Path none;
  Cov empty;
  ASSERT_EQ(gshcpu_coverage_from_path(none, GSHGEOM_FILL_EVEN_ODD, nullptr, nullptr, &empty.c), GSHCPU_OK);
  gshcpu_coverage_trim(&empty.c);
  EXPECT_EQ(empty.c.data, nullptr);
  EXPECT_EQ(empty.c.fill, GSHGEOM_FILL_EVEN_ODD);
}

TEST(CoverageTrim, KeepsAnIsolatedPixelAtEachCorner) {
  // One partly covered pixel in each of the four corners of the rectangle:
  // the trim must keep the extreme row and column on every side.
  Path path;
  path.polygon({{px(0), px(0)}, {px(0) + 32, px(0)}, {px(0) + 32, px(0) + 32}, {px(0), px(0) + 32}});
  path.polygon({{px(5), px(0)}, {px(5) + 32, px(0)}, {px(5) + 32, px(0) + 32}, {px(5), px(0) + 32}});
  path.polygon({{px(0), px(4)}, {px(0) + 32, px(4)}, {px(0) + 32, px(4) + 32}, {px(0), px(4) + 32}});
  path.polygon({{px(5), px(4)}, {px(5) + 32, px(4)}, {px(5) + 32, px(4) + 32}, {px(5), px(4) + 32}});
  Cov out;
  ASSERT_EQ(fill_into(path, GSHGEOM_FILL_NONZERO, -1, -1, 9, 8, out), GSHCPU_OK);
  gshcpu_coverage_trim(&out.c);
  EXPECT_EQ(out.c.left, 0);
  EXPECT_EQ(out.c.top, 0);
  EXPECT_EQ(out.c.width, 6u);
  EXPECT_EQ(out.c.height, 5u);
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 0, 0), 64);
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 5, 0), 64);
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 0, 4), 64);
  EXPECT_EQ(gshcpu_coverage_at(&out.c, 5, 4), 64);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
