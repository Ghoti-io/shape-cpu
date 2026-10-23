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
 * Cells for the exact step (strokes story S4c): hand-built and generated populations of the pieces of one pixel, the
 * families of the dense ones (chords across the cell, fans through one point, nested copies, alternating windings,
 * collinear overlap), and two instruments that know nothing of the sweep.  A cell is its pieces, the winding at the top
 * of its left side and the sign of each part; the cover to its left and the dense area the rasterizer would have summed
 * (UC4, UC16) follow from them as integers, so that every cell is one the step has no reason to refuse.
 *
 * - `reference_area2` is the doubled area of the cell's region in `long double`: each slab is cut at every meeting of two
 *   fragments (all of the pairs, from the lattice quantities), the order of the fragments is taken at the middle of each
 *   piece of the slab by sorting, the windings are summed in that order, and the width of every gap where the predicate
 *   holds is integrated from the exact widths at the piece's ends.  It costs `O((K + 1) n log n)` and is exact to about
 *   `1e-15` of the area, so it is an instrument for the cells the step is to equal, not a rival with its own error.
 * - `reference_crossings` counts the pairs of fragments that meet strictly inside a slab by testing every pair (UC19).
 *
 * The expected value of a cell is the area rounded with ties up for the area of the regions of UC8, and for a part's own
 * overlap the shared area (the dense sum less the union) rounded with ties up, as the step rounds (UC15).
 */

#ifndef GHOTI_IO_GSHCPU_TEST_DENSE_CELLS_H
#define GHOTI_IO_GSHCPU_TEST_DENSE_CELLS_H

#include "../src/render/render_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <utility>
#include <vector>

namespace dense {

using Pieces = std::vector<GSHCPU_Piece>;

inline GSHCPU_Piece piece(int x0, int y0, int x1, int y1) {
  GSHCPU_Piece p;
  p.x0 = (uint8_t)x0;
  p.y0 = (uint8_t)y0;
  p.x1 = (uint8_t)x1;
  p.y1 = (uint8_t)y1;
  return p;
}

/** One cell: the pieces of each part, the winding at the top of each part's left side, and the step's settings. */
struct Cell {
  Pieces parts[3];
  int64_t wtop[3] = {0, 0, 0};
  GSHCPU_Exact_Params params;
  int64_t a_left[3] = {0, 0, 0};
  int64_t dense[3] = {0, 0, 0};

  Cell() {
    std::memset(&params, 0, sizeof params);
    params.parts = 1;
    params.mode = GSHCPU_EXACT_OVERLAP;
    params.eps[0] = GSHCPU_RENDER_EXACT_EPS;
    params.eps[1] = GSHCPU_RENDER_EXACT_EPS;
    params.eps[2] = 1;
  }

  /** The cell of three parts (UC8) with the settings the cover gives it. */
  void three(int even_odd, int complement) {
    params.parts = 3;
    params.mode = GSHCPU_EXACT_UNION3;
    params.even_odd = even_odd;
    params.complement = complement;
  }

  /** The cell of one part alone as the clip's own region (UC8). */
  void region(int even_odd) {
    params.parts = 1;
    params.mode = GSHCPU_EXACT_REGION;
    params.eps[0] = 1;
    params.even_odd = even_odd;
  }

  size_t total() const {
    size_t n = 0;
    for (int p = 0; p < params.parts; p++) {
      n += parts[p].size();
    }
    return n;
  }

  /** The cover to the left and the dense area the rasterizer would have summed for these pieces. */
  void finish() {
    for (int p = 0; p < params.parts; p++) {
      int64_t jnet[65] = {0};
      int64_t cover = 0;
      int64_t area = 0;
      for (const GSHCPU_Piece & q : parts[p]) {
        if (q.x0 == 0) {
          jnet[q.y0] -= 1;
        }
        if (q.x1 == 0) {
          jnet[q.y1] += 1;
        }
        int64_t dy = (int64_t)q.y1 - (int64_t)q.y0;
        cover += dy;
        area += dy * ((int64_t)q.x0 + (int64_t)q.x1);
      }
      int64_t sum = 0;
      for (int h = 0; h <= 64; h++) {
        sum += jnet[h] * (64 - h);
      }
      a_left[p] = sum + 64 * wtop[p];
      dense[p] = params.eps[p] * (128 * (a_left[p] + cover) - area);
    }
  }

  GSHCPU_Exact_Cell view() const {
    GSHCPU_Exact_Cell c;
    std::memset(&c, 0, sizeof c);
    for (int p = 0; p < params.parts; p++) {
      c.pieces[p] = parts[p].data();
      c.count[p] = parts[p].size();
      c.a_left[p] = a_left[p];
      c.dense[p] = dense[p];
    }
    return c;
  }
};

/* --- the instruments ---------------------------------------------------------------------------------------------- */

struct RFrag {
  int px, py, dx, dy;  // oriented top to bottom
  int sign;            // +1 when the piece went down
  int part;
};

/** The predicate of the cell's region at windings @p w (the part's, already multiplied by its sign). */
inline bool in_region(const GSHCPU_Exact_Params & pr, const int64_t w[3]) {
  if (pr.mode == GSHCPU_EXACT_OVERLAP) {
    return w[0] != 0;
  }
  if (pr.mode == GSHCPU_EXACT_REGION) {
    return pr.even_odd ? (w[0] & 1) != 0 : w[0] != 0;
  }
  if (w[0] != 0) {
    return true;
  }
  if (w[1] == 0) {
    return false;
  }
  bool in = pr.even_odd ? (w[2] & 1) != 0 : w[2] != 0;
  return pr.complement ? !in : in;
}

inline std::vector<RFrag> fragments_of(const Cell & c) {
  std::vector<RFrag> out;
  for (int p = 0; p < c.params.parts; p++) {
    for (const GSHCPU_Piece & q : c.parts[p]) {
      if (q.y0 == q.y1) {
        continue;
      }
      RFrag f;
      bool down = q.y1 > q.y0;
      f.px = down ? q.x0 : q.x1;
      f.py = down ? q.y0 : q.y1;
      f.dx = down ? q.x1 - q.x0 : q.x0 - q.x1;
      f.dy = down ? q.y1 - q.y0 : q.y0 - q.y1;
      f.sign = down ? 1 : -1;
      f.part = p;
      out.push_back(f);
    }
  }
  return out;
}

/** The winding of each part, as `w = eps W`, at the top of the left side of the slab starting at @p ya. */
inline void left_windings(const Cell & c, int ya, int64_t w[3]) {
  for (int p = 0; p < 3; p++) {
    w[p] = 0;
    if (p >= c.params.parts) {
      continue;
    }
    int64_t sum = c.wtop[p];
    for (const GSHCPU_Piece & q : c.parts[p]) {
      if (q.x0 == 0 && q.y0 <= ya) {
        sum -= 1;
      }
      if (q.x1 == 0 && q.y1 <= ya) {
        sum += 1;
      }
    }
    w[p] = c.params.eps[p] * sum;
  }
}

/** The doubled area of the cell's region, `long double`, by cutting each slab at every meeting of two fragments. */
inline long double reference_area2(const Cell & c) {
  std::vector<RFrag> frags = fragments_of(c);
  std::vector<int> marks = {0, 64};
  for (const RFrag & f : frags) {
    marks.push_back(f.py);
    marks.push_back(f.py + f.dy);
  }
  for (int p = 0; p < c.params.parts; p++) {
    for (const GSHCPU_Piece & q : c.parts[p]) {
      if (q.x0 == 0) {
        marks.push_back(q.y0);
      }
      if (q.x1 == 0) {
        marks.push_back(q.y1);
      }
    }
  }
  std::sort(marks.begin(), marks.end());
  marks.erase(std::unique(marks.begin(), marks.end()), marks.end());
  long double total = 0;
  for (size_t mi = 0; mi + 1 < marks.size(); mi++) {
    int ya = marks[mi];
    int yb = marks[mi + 1];
    int H = yb - ya;
    std::vector<RFrag> act;
    for (const RFrag & f : frags) {
      if (f.py <= ya && f.py + f.dy >= yb) {
        act.push_back(f);
      }
    }
    int n = (int)act.size();
    std::vector<long double> a0(n);
    for (int i = 0; i < n; i++) {
      a0[i] = (long double)((int64_t)act[i].px * act[i].dy + (int64_t)(ya - act[i].py) * act[i].dx);
    }
    auto xat = [&](int i, long double t) { return (a0[i] + (long double)act[i].dx * t) / (long double)act[i].dy; };
    std::vector<long double> times = {0.0L, (long double)H};
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        int64_t d = (int64_t)act[i].dx * act[j].dy - (int64_t)act[j].dx * act[i].dy;
        if (d == 0) {
          continue;
        }
        int64_t ni = (int64_t)act[j].px * act[j].dy + (int64_t)(ya - act[j].py) * act[j].dx;
        int64_t nj = (int64_t)act[i].px * act[i].dy + (int64_t)(ya - act[i].py) * act[i].dx;
        int64_t num = ni * act[i].dy - nj * act[j].dy;
        long double t = (long double)num / (long double)d;
        if (t > 0 && t < (long double)H) {
          times.push_back(t);
        }
      }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    int64_t base[3];
    left_windings(c, ya, base);
    std::vector<int> order(n);
    for (size_t k = 0; k + 1 < times.size(); k++) {
      long double t0 = times[k];
      long double t1 = times[k + 1];
      long double mid = (t0 + t1) / 2;
      for (int i = 0; i < n; i++) {
        order[i] = i;
      }
      std::stable_sort(order.begin(), order.end(), [&](int i, int j) { return xat(i, mid) < xat(j, mid); });
      int64_t w[3] = {base[0], base[1], base[2]};
      long double acc = 0;
      for (int g = 0; g <= n; g++) {
        if (g > 0) {
          const RFrag & f = act[order[g - 1]];
          w[f.part] += c.params.eps[f.part] * f.sign;
        }
        if (in_region(c.params, w)) {
          long double l0 = g > 0 ? xat(order[g - 1], t0) : 0.0L;
          long double l1 = g > 0 ? xat(order[g - 1], t1) : 0.0L;
          long double r0 = g < n ? xat(order[g], t0) : 64.0L;
          long double r1 = g < n ? xat(order[g], t1) : 64.0L;
          acc += ((r0 - l0) + (r1 - l1)) / 2;
        }
      }
      total += acc * (t1 - t0);
    }
  }
  return 2.0L * total;
}

/** The fragments the step keeps (a piece and its reverse cancel, equal copies are one, UC13), with the heights it marks. */
struct Kept {
  std::vector<RFrag> frags;
  std::vector<int> marks;
};

inline Kept kept_of(const Cell & c) {
  Kept k;
  k.marks = {0, 64};
  for (int p = 0; p < c.params.parts; p++) {
    std::map<std::pair<std::pair<int, int>, std::pair<int, int>>, int> net;  // segment (the lesser end first) -> net direction count
    std::map<std::pair<std::pair<int, int>, std::pair<int, int>>, GSHCPU_Piece> sample;
    for (const GSHCPU_Piece & q : c.parts[p]) {
      std::pair<int, int> a = {q.y0, q.x0};
      std::pair<int, int> b = {q.y1, q.x1};
      bool forward = a <= b;
      auto key = forward ? std::make_pair(a, b) : std::make_pair(b, a);
      // The key of the step orders points by y then x; "forward" is from the lesser to the greater.
      net[key] += forward ? 1 : -1;
      sample[key] = forward ? q : piece(q.x1, q.y1, q.x0, q.y0);  // always the forward form
    }
    int jnet[65] = {0};
    for (auto & kv : net) {
      if (kv.second == 0) {
        continue;
      }
      GSHCPU_Piece q = sample[kv.first];
      int mu = kv.second < 0 ? -kv.second : kv.second;
      if (kv.second < 0) {
        q = piece(q.x1, q.y1, q.x0, q.y0);
      }
      if (q.x0 == 0) {
        jnet[q.y0] -= mu;
      }
      if (q.x1 == 0) {
        jnet[q.y1] += mu;
      }
      if (q.y0 != q.y1) {
        bool down = q.y1 > q.y0;
        RFrag f;
        f.px = down ? q.x0 : q.x1;
        f.py = down ? q.y0 : q.y1;
        f.dx = down ? q.x1 - q.x0 : q.x0 - q.x1;
        f.dy = down ? q.y1 - q.y0 : q.y0 - q.y1;
        f.sign = down ? 1 : -1;
        f.part = p;
        k.frags.push_back(f);
        k.marks.push_back(f.py);
        k.marks.push_back(f.py + f.dy);
      }
    }
    for (int h = 0; h <= 64; h++) {
      if (jnet[h] != 0) {
        k.marks.push_back(h);
      }
    }
  }
  std::sort(k.marks.begin(), k.marks.end());
  k.marks.erase(std::unique(k.marks.begin(), k.marks.end()), k.marks.end());
  return k;
}

/** The meetings of the cell (UC19): pairs of kept fragments whose order at a slab's top differs strictly from the order at its end. */
inline uint64_t reference_crossings(const Cell & c, size_t * distinct = nullptr) {
  Kept k = kept_of(c);
  if (distinct) {
    *distinct = k.frags.size();
  }
  uint64_t total = 0;
  for (size_t mi = 0; mi + 1 < k.marks.size(); mi++) {
    int ya = k.marks[mi];
    int yb = k.marks[mi + 1];
    int H = yb - ya;
    std::vector<RFrag> act;
    for (const RFrag & f : k.frags) {
      if (f.py <= ya && f.py + f.dy >= yb) {
        act.push_back(f);
      }
    }
    for (size_t i = 0; i < act.size(); i++) {
      for (size_t j = i + 1; j < act.size(); j++) {
        const RFrag & p = act[i];
        const RFrag & q = act[j];
        int64_t ap = (int64_t)p.px * p.dy + (int64_t)(ya - p.py) * p.dx;
        int64_t aq = (int64_t)q.px * q.dy + (int64_t)(ya - q.py) * q.dx;
        // x at the top: ap / dy_p against aq / dy_q; at the end: (ap + dx_p H) / dy_p against (aq + dx_q H) / dy_q.
        int64_t top = ap * q.dy - aq * p.dy;
        int64_t end = (ap + (int64_t)p.dx * H) * q.dy - (aq + (int64_t)q.dx * H) * p.dy;
        if ((top < 0 && end > 0) || (top > 0 && end < 0)) {
          total++;
        } else if (top == 0 && end != 0) {
          // Equal at the top: the order just after is by slope; they have met at the top and diverge, never inside.
        }
      }
    }
  }
  return total;
}

/* --- the cell's expected value ---------------------------------------------------------------------------------- */

/** What the step gives for @p c if it is exact: the rounding, ties up, of the area or of the shared area (UC15). */
struct Expected {
  long double exact;        // the quantity before rounding
  int64_t rounded;
  bool near_tie;            // the exact quantity is within @p slack of a tie: the step may round it either way
};

inline Expected expected_of(const Cell & c, long double slack) {
  long double u = reference_area2(c);
  long double q = c.params.mode == GSHCPU_EXACT_OVERLAP ? (long double)c.dense[0] - u : u;
  Expected e;
  e.exact = q;
  long double r = std::floor(q + 0.5L);
  e.rounded = (int64_t)r;
  long double frac = (q + 0.5L) - r;  // in [0, 1): zero at a tie
  e.near_tie = frac < slack || frac > 1.0L - slack;
  return e;
}

/* --- the families ---------------------------------------------------------------------------------------------- */

/** A chord across the whole height from (xa, 0) to (xb, 64), going down. */
inline GSHCPU_Piece chord(int xa, int xb) {
  return piece(xa, 0, xb, 64);
}

/**
 * Chords: @p n distinct segments from the top side to the bottom side of the cell, in a seeded order and direction, the most
 * pairs of them crossing the lattice allows (two chords cross iff their ends are in the opposite order).  With @p copies
 * above one, each segment appears that many times (nested copies, a multiplicity).
 */
inline Cell chords(size_t n, uint32_t seed, int copies = 1, bool mixed = true) {
  std::mt19937 rng(seed);
  std::vector<std::pair<int, int>> all;
  for (int a = 0; a <= 64; a++) {
    for (int b = 0; b <= 64; b++) {
      all.push_back({a, b});
    }
  }
  std::shuffle(all.begin(), all.end(), rng);
  Cell c;
  for (size_t i = 0; i < n && i < all.size(); i++) {
    bool up = mixed && (rng() & 1u);
    for (int k = 0; k < copies; k++) {
      c.parts[0].push_back(up ? piece(all[i].second, 64, all[i].first, 0) : chord(all[i].first, all[i].second));
    }
  }
  c.wtop[0] = 0;
  c.finish();
  return c;
}

/**
 * Chords built to exactly @p target meetings: segments from the top side to the bottom side, added one at a time in a
 * seeded order while the meetings stay within the target, then the segment (of those left) that adds the most the target
 * still allows, until it is reached.  Two chords meet inside the cell iff their ends are in the opposite order, so the count
 * of a chord against the chords already placed is a pass over them; a chord that adds none (a copy of a neighbour's slope
 * and a place beside it) is always left, so the target is reached exactly.  Returns the cell and, in @p reached, its meetings.
 */
inline Cell chords_to(uint64_t target, uint32_t seed, uint64_t * reached) {
  Cell best_cell;
  uint64_t best_total = 0;
  for (uint32_t attempt = 0; attempt < 400; attempt++) {
    std::mt19937 rng(seed + attempt * 7919u);
    std::vector<std::pair<int, int>> all;
    for (int a = 0; a <= 64; a++) {
      for (int b = 0; b <= 64; b++) {
        all.push_back({a, b});
      }
    }
    std::shuffle(all.begin(), all.end(), rng);
    std::vector<std::pair<int, int>> placed;
    uint64_t total = 0;
    std::vector<char> used(all.size(), 0);
    auto adds = [&](const std::pair<int, int> & c) {
      uint64_t k = 0;
      for (const auto & q : placed) {
        k += ((int64_t)(c.first - q.first) * (c.second - q.second) < 0);
      }
      return k;
    };
    // First phase: the shuffled order while the chords placed stay short of the target by a margin the second can close.
    uint64_t margin = target < 600 ? 0 : 150 + (attempt % 17) * 25;
    for (size_t i = 0; i < all.size() && total + margin < target; i++) {
      uint64_t k = adds(all[i]);
      if (total + k + margin <= target) {
        placed.push_back(all[i]);
        used[i] = 1;
        total += k;
      }
    }
    // Second phase: the chord left that adds the most the remainder allows, until the target is reached.
    while (total < target) {
      uint64_t best = 0;
      size_t at = all.size();
      for (size_t i = 0; i < all.size(); i++) {
        if (used[i]) {
          continue;
        }
        uint64_t k = adds(all[i]);
        if (k <= target - total && k > best) {
          best = k;
          at = i;
        }
      }
      if (at == all.size()) {
        break;
      }
      placed.push_back(all[at]);
      used[at] = 1;
      total += best;
    }
    if (total >= best_total) {
      best_total = total;
      best_cell = Cell();
      for (const auto & q : placed) {
        best_cell.parts[0].push_back(chord(q.first, q.second));
      }
    }
    if (total == target) {
      break;
    }
  }
  best_cell.finish();
  *reached = best_total;
  return best_cell;
}

/**
 * A fan: every chord from the top side to the bottom side that passes through the point (@p px_num / @p px_den, @p py_num /
 * @p py_den), the pieces going down or up alternately when @p alternate: the most fragments through one point the lattice
 * allows.
 */
inline Cell fan(int py_num, int py_den, int px_num, int px_den, bool alternate, uint32_t seed = 1) {
  Cell c;
  std::mt19937 rng(seed);
  int count = 0;
  for (int xa = 0; xa <= 64; xa++) {
    for (int xb = 0; xb <= 64; xb++) {
      // x at height py along (xa, 0) -> (xb, 64): xa + (xb - xa) py / 64 = px_num / px_den
      int64_t lhs = ((int64_t)xa * 64 * py_den + (int64_t)(xb - xa) * py_num) * px_den;
      int64_t rhs = (int64_t)px_num * 64 * py_den;
      if (lhs == rhs) {
        bool up = alternate && ((count++ & 1) != 0);
        if (!alternate && (rng() & 7u) == 0) {
          up = true;
        }
        c.parts[0].push_back(up ? piece(xb, 64, xa, 0) : chord(xa, xb));
      }
    }
  }
  c.finish();
  return c;
}

/** Random segments with ends anywhere on the lattice (some on the left side), @p n of them, windings in -@p depth..@p depth at the top. */
inline Cell random_cell(size_t n, uint32_t seed, int depth = 2, int left_share = 6) {
  std::mt19937 rng(seed);
  Cell c;
  for (size_t i = 0; i < n; i++) {
    int x0 = (int)(rng() % 65), y0 = (int)(rng() % 65), x1 = (int)(rng() % 65), y1 = (int)(rng() % 65);
    if ((int)(rng() % 100) < left_share) {
      x0 = 0;
    }
    if ((int)(rng() % 100) < left_share) {
      x1 = 0;
    }
    if (x0 == x1 && y0 == y1) {
      continue;
    }
    c.parts[0].push_back(piece(x0, y0, x1, y1));
  }
  c.wtop[0] = (int)(rng() % (uint32_t)(2 * depth + 1)) - depth;
  c.finish();
  return c;
}

/** A three-part cell of random pieces (UC8). */
inline Cell random_three(size_t n, uint32_t seed, int even_odd, int complement) {
  std::mt19937 rng(seed);
  Cell c;
  c.three(even_odd, complement);
  for (int p = 0; p < 3; p++) {
    for (size_t i = 0; i < n; i++) {
      int x0 = (int)(rng() % 65), y0 = (int)(rng() % 65), x1 = (int)(rng() % 65), y1 = (int)(rng() % 65);
      if (rng() % 100 < 8) {
        x0 = 0;
      }
      if (x0 == x1 && y0 == y1) {
        continue;
      }
      c.parts[p].push_back(piece(x0, y0, x1, y1));
    }
    c.wtop[p] = (int)(rng() % 5) - 2;
  }
  c.finish();
  return c;
}

/** Collinear overlap: copies of unequal lengths along three lines, both ways, to exercise partial overlap (not cancelled). */
inline Cell collinear(uint32_t seed) {
  std::mt19937 rng(seed);
  Cell c;
  const int lines[3][4] = {{4, 0, 52, 64}, {10, 0, 10, 64}, {64, 8, 0, 40}};
  for (const auto & l : lines) {
    for (int k = 0; k < 6; k++) {
      // a sub-segment [s, e] of the line, by parameter in 0..16
      int s = (int)(rng() % 12), e = s + 1 + (int)(rng() % (16 - s));
      int dx = l[2] - l[0], dy = l[3] - l[1];
      if (dx % 16 || dy % 16) {
        // keep the ends on the lattice: only whole sixteenths of lines whose steps divide by 16, else the whole line
        c.parts[0].push_back(piece(l[0], l[1], l[2], l[3]));
        continue;
      }
      bool up = rng() & 1u;
      GSHCPU_Piece q = piece(l[0] + dx / 16 * s, l[1] + dy / 16 * s, l[0] + dx / 16 * e, l[1] + dy / 16 * e);
      c.parts[0].push_back(up ? piece(q.x1, q.y1, q.x0, q.y0) : q);
    }
  }
  c.finish();
  return c;
}

}  // namespace dense

#endif /* GHOTI_IO_GSHCPU_TEST_DENSE_CELLS_H */
