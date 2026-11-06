/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape CPU.
 *
 * Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape CPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Helpers for the tests of union clips (shape-exact-primitives stories A3 and A3b): members from lattice polygons and from the
 * vectors of shape-scene's oracle, unions, and a list that clips a white fill of a grid to one.
 */

#ifndef GHOTI_IO_GSHCPU_TEST_CLIPUNION_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_CLIPUNION_HELPERS_H

#include "mask_helpers.h"

#include <ghoti.io/shape-cpu/shape-cpu.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>
#include <ghoti.io/shape-scene/testing/clipunion_vectors.h>

#include <cstring>
#include <utility>
#include <vector>

namespace cuh {

using namespace mh;
namespace cv = clipunion_vectors;

inline GSHSCENE_Paint white() {
  return rh::solid(1.0, 1.0, 1.0, 1.0);
}

/** A path resource of the polygon with lattice vertices @p v (26.6, so v / 64 pixels is exact). */
inline const GSHSCENE_Resource * lattice_polygon(Scene & s, const std::vector<std::pair<int, int>> & v) {
  std::vector<double> xy;
  for (auto & p : v) {
    xy.push_back(p.first / 64.0);
    xy.push_back(p.second / 64.0);
  }
  return s.polygon(xy);
}

/** One member of a vector case: its contours as one path of several subpaths. */
inline const GSHSCENE_Resource * member_path(Scene & s, const cv::Member & m) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  for (size_t c = 0; c < m.contour_count; c++) {
    const cv::Contour & ct = cv::kContours[m.contour_first + c];
    for (size_t k = 0; k < ct.count; k++) {
      GSHGEOM_Point q{cv::kVerts[ct.first + k][0] / 64.0, cv::kVerts[ct.first + k][1] / 64.0, 0};
      if (k == 0) {
        gshgeom_path_move_to(p, q);
      } else {
        gshgeom_path_line_to(p, q);
      }
    }
    gshgeom_path_close(p);
  }
  return s.path(p);
}

inline const GSHSCENE_Resource * make_union(Scene & s, const std::vector<GSHSCENE_Clip_Member> & members) {
  const GSHSCENE_Resource * u = nullptr;
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, members.data(), members.size(), &u), GSHSCENE_OK);
  s.held.push_back(u);
  return u;
}

inline GSHSCENE_Clip_Member member(const GSHSCENE_Resource * r, GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO) {
  GSHSCENE_Clip_Member m;
  std::memset(&m, 0, sizeof m);
  m.resource = r;
  m.rule = rule;
  return m;
}

/** A list: the union as a clip around one white fill of the whole w by h grid. */
inline GSHSCENE_List * clipped_fill(Scene & s, const GSHSCENE_Resource * u, uint32_t w, uint32_t h, const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_List * l = s.open();
  EXPECT_EQ(gshscene_list_clip_push(l, u, map, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint wh = white();
  EXPECT_EQ(gshscene_list_fill(l, s.rect(0, 0, (double)w, (double)h), nullptr, GSHGEOM_FILL_NONZERO, &wh, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

/** The alpha byte of every pixel of @p d. */
inline std::vector<uint8_t> alphas(const Draw & d) {
  std::vector<uint8_t> out;
  for (uint32_t y = 0; y < d.height; y++) {
    for (uint32_t x = 0; x < d.width; x++) {
      out.push_back(d.at8(x, y, 3));
    }
  }
  return out;
}

/** The members of vector case @p c, in @p s. */
inline std::vector<GSHSCENE_Clip_Member> case_members(Scene & s, const cv::Case & c) {
  std::vector<GSHSCENE_Clip_Member> members;
  for (size_t i = 0; i < c.member_count; i++) {
    const cv::Member & m = cv::kMembers[c.member_first + i];
    members.push_back(member(member_path(s, m), m.even_odd ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO));
  }
  return members;
}

} // namespace cuh

#endif /* GHOTI_IO_GSHCPU_TEST_CLIPUNION_HELPERS_H */
