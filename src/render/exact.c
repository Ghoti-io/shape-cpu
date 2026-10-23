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
 * The exact step of a stroke part's coverage (render.h, UC1 to UC20; strokes stories S4b and S4c): the area of the union of
 * the pieces of one cell, computed in integers from the cell's own edge pieces.
 *
 * **The cell.**  A cell is a pixel of the accumulator; its frame is the 26.6 square `[0, 64] x [0, 64]`, y down.  A
 * *piece* is what the rasterizer (cover.c) cuts an edge into inside the cell: two ends in the frame, as integers in
 * `[0, 64]`, directed along the contour.  A piece of nonzero height is a *fragment*; one of height 0 that touches the
 * cell's left side is a *flat piece* (the dense accumulation deposits nothing for it, and it steps the winding on the
 * left side).  Nothing else of the outline matters inside a cell.  Pieces that are the same segment in the same direction
 * are one fragment of a multiplicity, and a piece and its reverse cancel (UC13).
 *
 * **The winding.**  Let `W(x, y)` be the signed number of edges crossing the horizontal ray to the left of `(x, y)`,
 * an edge directed down counting +1, and `w = eps W` with `eps` the sign that makes a positively wound piece's inside 1.
 * Inside the cell `w(x, y)` is the winding just left of the cell, `W(0-, y)`, plus the steps of the fragments left of
 * `x` at height `y`.  `W(0-, .)` is piecewise constant and steps only at the heights where a contour crosses the line
 * `x = 0`: `J(h)` is the sum over pieces of -1 for a piece that starts at `(0, h)` and +1 for a piece that ends there,
 * a piece of length 0 counting nothing.  What the rasterizer has already summed of everything left of the cell is
 * `A_left`, the integral of `W(0-, y)` over the side, in the cover's units, so
 *
 *     64 W_top = A_left - sum_h J(h) (64 - h)
 *
 * gives `W_top`, the winding at the top of the side, exactly; a remainder is an error (a piece missed or invented).
 *
 * **The slabs.**  The distinct heights of the fragments' ends and of the steps of `J` cut the cell into slabs of integer
 * height `H`, at most 64.  In a slab every fragment either spans it or misses it, and the spanning ones have a left to
 * right order at every depth `u`, the order of `x(u) = (a + dx u) / dy` (`0 <= a <= 2^12`, `|dx| <= 64`, `1 <= dy <= 64`,
 * and `x >= 0` because the fragment spans the slab).  Two of them meet at most once, at `u = N / D` with `D = dx_p dy_q -
 * dx_q dy_p` and `N = a_q dy_p - a_p dy_q`, `D <= 2^13` and `N < H D <= 2^19`.
 *
 * **The sweep.**  The area wanted is `integral Omega(w) dA` over the cell, with `Omega` the weight of the part's winding
 * in doubled units (`2 (|w| - 1)^+ - 4 |w| [w < 0]` for the overlap of a part: what the dense sum holds more than the
 * nonzero fill covers; twice a predicate for the regions of UC8).  Summing a row's covered widths by parts over the
 * fragments, `sum_gaps Omega_k (x_{k+1} - x_k) = 64 Omega_n + sum_f x_f (Omega_{k-1} - Omega_k)`: a fragment's
 * coefficient `c_f = Omega_left - Omega_right` is constant between the instants at which a neighbour exchanges places
 * with it, and at an exchange of `p` and `q` only those two coefficients change, by `+-d` with `d = Omega_0 + Omega_2 -
 * Omega_1 - Omega_1'`, a second difference of `Omega`.  So the area is `64 H Omega_n` plus, for each fragment, the
 * telescoped integral of `c_f x_f`: `sum_k F_f(t_k) (c_{k-1} - c_k) + c_r F_f(H)` with `F_f(t) = (2 a t + dx t^2) / (2 dy)`
 * the exact antiderivative of `x_f`, the `t_k` the exchange instants.  The order is carried from slab to slab and kept by
 * exchanging neighbours at their meetings, earliest first, from a binary heap indexed by the pair's place; no instant is
 * rounded.  The cost of a slab is `O(n)` and of a cell `O(64 n + K log n)`, `K <= n (n - 1) / 2` the meetings.
 *
 * **The arithmetic** is integer, in `int64_t` and `uint64_t`, with no libm, no `long double`, no 128-bit type, no FMA.
 * `F` is evaluated at an exact instant `N / D` as `Num / Den`, `Num = 2 a N D + dx N^2 < 2^46`, `Den = 2 dy D^2 <= 2^33`,
 * as `floor(2^30 F) = (Q << 30) + ((Rm << 30) / Den)` with `Q = Num / Den <= 2^12` and `Rm = Num % Den`, so
 * `Rm << 30 < 2^63`: low by less than `2^-30` unit squared.  At a slab's end `2^30 F(H) = ((2 a H + dx H^2) << 30) / (2 dy)`
 * from a numerator below `2^20`.  Orders and meetings compare products of lattice quantities below `2^32`.  `acc` is the
 * sum of `2^30` times the area in doubled units, `uint64_t` with defined wrap-around: a partial sum may pass `2^63` but the
 * true total is below `2^54` for `|w| <= 1024`, so the wrapped sum is exact.  A coefficient is at most `6 mu` in magnitude
 * (`|Omega| <= 2 |w| + 2`, differences of at most `6` per unit of winding step) and a term `c F30` is below `2^55`.  The
 * error of a cell of `K` exchanges and `E` slab-end evaluations with largest multiplicity `mu` is below
 * `2^-30 * 6 mu (2 K + E)` doubled units.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/safemath.h>

#include <string.h>

typedef GSHCPU_Exact_Frag Frag;
typedef GSHCPU_Exact_Gap Gap;

#define NONE UINT32_MAX

/* Test hooks: the library never sets either (render_internal.h). */
GSHCPU_Exact_Cell_Fn gshcpu_exact_hook = NULL;
int gshcpu_exact_check = 0;

/* --- scratch ------------------------------------------------------------------------------------------------- */

/** Bytes of one cell's scratch for @p max_pieces pieces over @p parts parts. */
size_t gshcpu_exact_scratch_bytes(uint64_t max_pieces, int parts) {
  size_t m = (size_t)max_pieces * (size_t)parts;
  if (parts == 0) {
    return 0;
  }
  size_t bytes = 0;
  bytes += m * sizeof(GSHCPU_Piece) + 16u;  /* the cell's pieces, sorted */
  bytes += m * sizeof(Frag) + 16u;
  bytes += m * sizeof(int32_t) + 16u;       /* a */
  bytes += 4u * (m * sizeof(uint32_t) + 16u); /* ord, ord2, st, sc */
  bytes += (m + 1u) * sizeof(Gap) + 16u;
  bytes += 4u * (m * sizeof(uint32_t) + 16u); /* heap, hpos, evn, evd */
  return bytes;
}

static unsigned char * carve(unsigned char ** at, size_t bytes) {
  uintptr_t p = ((uintptr_t) * at + 15u) & ~(uintptr_t)15u;
  unsigned char * r = (unsigned char *)p;
  *at = r + bytes;
  return r;
}

void gshcpu_exact_scratch_init(GSHCPU_Exact_Scratch * s, void * memory, uint64_t max_pieces, int parts) {
  size_t m = (size_t)max_pieces * (size_t)parts;
  unsigned char * at = memory;
  memset(s, 0, sizeof *s);
  s->capacity = m;
  s->sorted = (void *)carve(&at, m * sizeof(GSHCPU_Piece));
  s->frag = (void *)carve(&at, m * sizeof(Frag));
  s->a = (void *)carve(&at, m * sizeof(int32_t));
  s->ord = (void *)carve(&at, m * sizeof(uint32_t));
  s->ord2 = (void *)carve(&at, m * sizeof(uint32_t));
  s->st = (void *)carve(&at, m * sizeof(uint32_t));
  s->sc = (void *)carve(&at, m * sizeof(uint32_t));
  s->gap = (void *)carve(&at, (m + 1u) * sizeof(Gap));
  s->heap = (void *)carve(&at, m * sizeof(uint32_t));
  s->hpos = (void *)carve(&at, m * sizeof(uint32_t));
  s->evn = (void *)carve(&at, m * sizeof(uint32_t));
  s->evd = (void *)carve(&at, m * sizeof(uint32_t));
}

/* --- the cell's working state ------------------------------------------------------------------------------------ */

typedef struct Ctx {
  const GSHCPU_Exact_Params * pr;
  GSHCPU_Exact_Scratch * s;
  size_t nfrag;
  size_t weighted;        /* pieces left after the seams cancel, equal copies counted: the sum of the multiplicities */
  size_t raw;             /* pieces handed in, all parts */
  int64_t wtop[3];
  int32_t jnet[3][65];
  uint8_t mark[65];
  int64_t wind0[3];       /* the winding on the left side at the current slab's top, eps applied */
  /* the order of the previous slab's end, carried */
  uint32_t * ord;
  uint32_t * ord2;
  size_t n_prev;
  /* the slab */
  int ya, yb;
  int64_t height;
  size_t n;               /* fragments spanning it */
  /* the heap of events */
  size_t hn;
  /* sums */
  uint64_t acc;
  int64_t wmax;
  uint64_t events;
  uint64_t end_evals;
  uint64_t slabs;
  uint64_t work;
  uint32_t logn;          /* ceil(log2(fragments)) for the work units */
  int status;
} Ctx;

/* --- the pieces of the cell -------------------------------------------------------------------------------------- */

/** The code of a point of the cell: a total order on the 65 by 65 lattice. */
static uint32_t point_code(uint8_t x, uint8_t y) {
  return (uint32_t)y * 65u + (uint32_t)x;
}

/**
 * The key a piece sorts by: its two ends in order (the lesser first), then which way it goes.  A piece and its reverse (the
 * two copies of a seam that two pieces of an outline share) have keys that differ in the last bit alone.
 */
static uint64_t piece_key(GSHCPU_Piece pc) {
  uint32_t a = point_code(pc.x0, pc.y0);
  uint32_t b = point_code(pc.x1, pc.y1);
  return a <= b ? ((uint64_t)a << 33) | ((uint64_t)b << 1) | 1u : ((uint64_t)b << 33) | ((uint64_t)a << 1);
}

/** Sort @p n pieces in place by their keys (a heap sort: no recursion, no allocation, O(n log n)). */
static void sort_pieces(GSHCPU_Piece * v, size_t n) {
#define KEY(i) piece_key(v[i])
  if (n < 2) {
    return;
  }
  for (size_t start = n / 2; start-- > 0;) {
    size_t root = start;
    for (;;) {
      size_t child = 2 * root + 1;
      if (child >= n) {
        break;
      }
      if (child + 1 < n && KEY(child) < KEY(child + 1)) {
        child++;
      }
      if (KEY(root) >= KEY(child)) {
        break;
      }
      GSHCPU_Piece t = v[root];
      v[root] = v[child];
      v[child] = t;
      root = child;
    }
  }
  for (size_t end = n - 1; end > 0; end--) {
    GSHCPU_Piece t = v[0];
    v[0] = v[end];
    v[end] = t;
    size_t root = 0;
    for (;;) {
      size_t child = 2 * root + 1;
      if (child >= end) {
        break;
      }
      if (child + 1 < end && KEY(child) < KEY(child + 1)) {
        child++;
      }
      if (KEY(root) >= KEY(child)) {
        break;
      }
      GSHCPU_Piece u = v[root];
      v[root] = v[child];
      v[child] = u;
      root = child;
    }
  }
#undef KEY
}

/** ceil(log2(@p n)) for n >= 1. */
static uint32_t ceil_log2(uint64_t n) {
  uint32_t b = 0;
  while (b < 63 && ((uint64_t)1 << b) < n) {
    b++;
  }
  return b;
}

/**
 * The cell's pieces to its fragments (UC13).  The pieces of each part are copied, validated, and checked against what the
 * rasterizer summed of them (UC16 ii: when @p verify, exactly, in integers); sorted by segment; and the pieces left over
 * one segment after a piece and its reverse cancel, which are equal copies in one direction, become one fragment of that
 * multiplicity.  Sets `nfrag`, `weighted`, the marks and the steps `jnet` of the winding on the left side.
 *
 * @return ::GSHCPU_EXACT_OK, ::GSHCPU_EXACT_LIMIT (more pieces than the scratch holds) or ::GSHCPU_EXACT_IDENTITY.
 */
static int collect(Ctx * c, const GSHCPU_Exact_Cell * cell, int verify) {
  GSHCPU_Exact_Scratch * s = c->s;
  const GSHCPU_Exact_Params * params = c->pr;
  size_t pieces = 0;
  for (int p = 0; p < params->parts; p++) {
    pieces += cell->count[p];
  }
  c->raw = pieces;
  if (pieces > s->capacity) {
    return GSHCPU_EXACT_LIMIT; /* the caller counts first: reaching this is a defect, and refuses rather than guesses */
  }
  c->mark[0] = 1;
  c->mark[64] = 1;
  size_t at = 0;
  for (int p = 0; p < params->parts; p++) {
    GSHCPU_Piece * v = s->sorted + at;
    size_t n = cell->count[p];
    if (n) {
      memcpy(v, cell->pieces[p], n * sizeof *v);
    }
    /* The pieces, as the rasterizer deposited them: the cover is the sum of the heights, the area the sum of height times
     * the two x ends, and what was summed to the left of the cell is the cover of the cells before it. */
    int64_t cover = 0;
    int64_t area = 0;
    for (size_t i = 0; i < n; i++) {
      GSHCPU_Piece pc = v[i];
      if (pc.x0 > 64 || pc.x1 > 64 || pc.y0 > 64 || pc.y1 > 64 || (pc.x0 == pc.x1 && pc.y0 == pc.y1)) {
        return GSHCPU_EXACT_IDENTITY; /* a piece that is not of this cell, or has no length */
      }
      int64_t dy = (int64_t)pc.y1 - (int64_t)pc.y0;
      cover += dy;
      area += dy * ((int64_t)pc.x0 + (int64_t)pc.x1);
    }
    if (verify && cell->dense[p] != params->eps[p] * (128 * (cell->a_left[p] + cover) - area)) {
      return GSHCPU_EXACT_IDENTITY; /* the pieces are not the ones the rasterizer summed: a piece missed or invented */
    }
    c->work += n;
    sort_pieces(v, n);
    for (size_t i = 0; i < n;) {
      uint64_t group = piece_key(v[i]) >> 1;
      size_t j = i;
      int64_t net = 0;
      GSHCPU_Piece forward = v[i];
      GSHCPU_Piece backward = v[i];
      while (j < n && (piece_key(v[j]) >> 1) == group) {
        if (piece_key(v[j]) & 1u) {
          net++;
          forward = v[j];
        } else {
          net--;
          backward = v[j];
        }
        j++;
      }
      i = j;
      if (net == 0) {
        continue;
      }
      GSHCPU_Piece pc = net > 0 ? forward : backward;
      uint32_t mu = (uint32_t)(net < 0 ? -net : net);
      c->weighted += mu;
      if (pc.x0 == 0) {
        c->jnet[p][pc.y0] -= (int32_t)mu;
      }
      if (pc.x1 == 0) {
        c->jnet[p][pc.y1] += (int32_t)mu;
      }
      if (pc.y0 != pc.y1) {
        Frag * f = &s->frag[c->nfrag];
        int down = pc.y1 > pc.y0;
        f->px = down ? pc.x0 : pc.x1;
        f->py = down ? pc.y0 : pc.y1;
        f->dx = (int16_t)(down ? pc.x1 - pc.x0 : pc.x0 - pc.x1);
        f->dy = (int16_t)(down ? pc.y1 - pc.y0 : pc.y0 - pc.y1);
        f->s = down ? 1 : -1;
        f->part = (uint8_t)p;
        f->mu = mu;
        c->mark[f->py] = 1;
        c->mark[f->py + f->dy] = 1;
        c->nfrag++;
      }
    }
    at += n;
  }
  for (int p = 0; p < params->parts; p++) {
    for (int h = 0; h <= 64; h++) {
      if (c->jnet[p][h] != 0) {
        c->mark[h] = 1;
      }
    }
  }
  return GSHCPU_EXACT_OK;
}

/* --- the weight ------------------------------------------------------------------------------------------------ */

/**
 * `Omega` of a gap whose windings are @p w, in doubled units: twice the predicate of the area wanted.  For a part's own
 * overlap (mode 0) it is twice "the winding is not zero": the dense sum of the cell holds `2 w` over the cell, and the nonzero
 * fill covers where `w` is not zero, so the area to subtract from the dense sum, `2 (|w| - 1)^+ - 4 |w| [w < 0]` as a weight,
 * is the dense sum less twice this indicator; the step takes the indicator and subtracts it, so that no weight grows with the
 * winding and no depth is a reason to refuse a cell (UC20).  The regions of UC8 are twice a predicate: the clip's region by
 * its rule (mode 2), or `{w_b != 0} U ({w_a != 0} & F')` with `F'` the clip's region or its complement (mode 1).
 */
static int32_t omega_of(const GSHCPU_Exact_Params * p, const int32_t w[3]) {
  if (p->mode == GSHCPU_EXACT_OVERLAP) {
    return w[0] != 0 ? 2 : 0;
  }
  if (p->mode == GSHCPU_EXACT_REGION) {
    return (p->even_odd ? (w[0] & 1) != 0 : w[0] != 0) ? 2 : 0;
  }
  if (w[0] != 0) {
    return 2;
  }
  if (w[1] == 0) {
    return 0;
  }
  int in = p->even_odd ? ((w[2] & 1) != 0) : (w[2] != 0);
  if (p->complement) {
    in = !in;
  }
  return in ? 2 : 0;
}

/* --- the order of a slab's fragments --------------------------------------------------------------------------- */

/** The order of fragments @p i and @p j just below the slab's top: by `x` there, then by slope, then by index. */
static int cmp_top(const GSHCPU_Exact_Scratch * s, uint32_t i, uint32_t j) {
  const Frag * p = &s->frag[i];
  const Frag * q = &s->frag[j];
  int64_t xp = (int64_t)s->a[i] * q->dy;
  int64_t xq = (int64_t)s->a[j] * p->dy;
  if (xp != xq) {
    return xp < xq ? -1 : 1;
  }
  int64_t sp = (int64_t)p->dx * q->dy;
  int64_t sq = (int64_t)q->dx * p->dy;
  if (sp != sq) {
    return sp < sq ? -1 : 1;
  }
  return i < j ? -1 : i > j ? 1 : 0;
}

/** Whether @p i is right of @p j at depth @p h of the slab (strictly). */
static int right_of_at(const GSHCPU_Exact_Scratch * s, uint32_t i, uint32_t j, int64_t h) {
  const Frag * p = &s->frag[i];
  const Frag * q = &s->frag[j];
  int64_t xp = ((int64_t)s->a[i] + (int64_t)p->dx * h) * q->dy;
  int64_t xq = ((int64_t)s->a[j] + (int64_t)q->dx * h) * p->dy;
  return xp > xq;
}

/** Sort @p v of @p n ids by the order at the slab's top (a merge sort through @p tmp; the order is total). */
static void sort_top(const GSHCPU_Exact_Scratch * s, uint32_t * v, uint32_t * tmp, size_t n) {
  uint32_t * src = v;
  uint32_t * dst = tmp;
  for (size_t width = 1; width < n; width *= 2) {
    for (size_t lo = 0; lo < n; lo += 2 * width) {
      size_t mid = lo + width < n ? lo + width : n;
      size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
      size_t i = lo;
      size_t j = mid;
      size_t k = lo;
      while (i < mid && j < hi) {
        dst[k++] = cmp_top(s, src[i], src[j]) <= 0 ? src[i++] : src[j++];
      }
      while (i < mid) {
        dst[k++] = src[i++];
      }
      while (j < hi) {
        dst[k++] = src[j++];
      }
    }
    uint32_t * t = src;
    src = dst;
    dst = t;
  }
  if (src != v) {
    memcpy(v, src, n * sizeof *v);
  }
}

/**
 * Sort @p v of @p n ids, stably, by `x` at depth @p h (a merge sort through @p tmp) and return the number of pairs the sort
 * turned round: the pairs whose order at the slab's top differs strictly from the order at depth @p h, which is the number
 * of meetings of the slab (UC19).  Fragments equal at @p h keep the order they have.
 */
static uint64_t sort_bottom(const GSHCPU_Exact_Scratch * s, uint32_t * v, uint32_t * tmp, size_t n, int64_t h) {
  uint64_t inversions = 0;
  uint32_t * src = v;
  uint32_t * dst = tmp;
  for (size_t width = 1; width < n; width *= 2) {
    for (size_t lo = 0; lo < n; lo += 2 * width) {
      size_t mid = lo + width < n ? lo + width : n;
      size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
      size_t i = lo;
      size_t j = mid;
      size_t k = lo;
      while (i < mid && j < hi) {
        if (right_of_at(s, src[i], src[j], h)) {
          inversions += (uint64_t)(mid - i);
          dst[k++] = src[j++];
        } else {
          dst[k++] = src[i++];
        }
      }
      while (i < mid) {
        dst[k++] = src[i++];
      }
      while (j < hi) {
        dst[k++] = src[j++];
      }
    }
    uint32_t * t = src;
    src = dst;
    dst = t;
  }
  if (src != v) {
    memcpy(v, src, n * sizeof *v);
  }
  return inversions;
}

/**
 * The order of the next slab's fragments, `ya` to `yb` (UC14 a): the list the previous slab ended in without the fragments
 * that end at `ya`, the ones that start at `ya` sorted and merged in, repaired if a pair that met exactly at `ya` is out
 * of order (by sorting the lot again, which costs no more than the repair of a bundle by exchanges would).  Sets `a` for
 * every fragment of the slab, `ord` and `n`.
 */
static void slab_order(Ctx * c) {
  GSHCPU_Exact_Scratch * s = c->s;
  const int ya = c->ya;
  const int yb = c->yb;
  size_t k = 0;
  for (size_t i = 0; i < c->n_prev; i++) {
    uint32_t id = c->ord[i];
    if (s->frag[id].py + s->frag[id].dy > ya) {
      c->ord[k++] = id;
    }
  }
  size_t ns = 0;
  for (size_t id = 0; id < c->nfrag; id++) {
    const Frag * f = &s->frag[id];
    if (f->py <= ya && f->py + f->dy >= yb) {
      s->a[id] = (int32_t)f->px * f->dy + (int32_t)(ya - f->py) * f->dx;
      if (f->py == ya) {
        s->st[ns++] = (uint32_t)id;
      }
    }
  }
  c->work += c->nfrag;
  sort_top(s, s->st, s->sc, ns);
  size_t i = 0;
  size_t j = 0;
  size_t out = 0;
  while (i < k && j < ns) {
    c->ord2[out++] = cmp_top(s, c->ord[i], s->st[j]) <= 0 ? c->ord[i++] : s->st[j++];
  }
  while (i < k) {
    c->ord2[out++] = c->ord[i++];
  }
  while (j < ns) {
    c->ord2[out++] = s->st[j++];
  }
  for (size_t q = 1; q < out; q++) {
    if (cmp_top(s, c->ord2[q - 1], c->ord2[q]) >= 0) {
      sort_top(s, c->ord2, s->sc, out); /* a pair that met exactly at the top: the order just after is by slope */
      break;
    }
  }
  uint32_t * t = c->ord;
  c->ord = c->ord2;
  c->ord2 = t;
  c->n = out;
  c->work += out;
}

/* --- the heap of meetings -------------------------------------------------------------------------------------- */

/** Whether the meeting of the pair at place @p a is before that of the pair at @p b: by depth, then by place. */
static int ev_less(const GSHCPU_Exact_Scratch * s, uint32_t a, uint32_t b) {
  uint64_t l = (uint64_t)s->evn[a] * s->evd[b];
  uint64_t r = (uint64_t)s->evn[b] * s->evd[a];
  return l < r || (l == r && a < b);
}

static void heap_up(Ctx * c, size_t i) {
  GSHCPU_Exact_Scratch * s = c->s;
  uint32_t v = s->heap[i];
  while (i > 0) {
    size_t parent = (i - 1) / 2;
    if (!ev_less(s, v, s->heap[parent])) {
      break;
    }
    s->heap[i] = s->heap[parent];
    s->hpos[s->heap[i]] = (uint32_t)i;
    i = parent;
  }
  s->heap[i] = v;
  s->hpos[v] = (uint32_t)i;
}

static void heap_down(Ctx * c, size_t i) {
  GSHCPU_Exact_Scratch * s = c->s;
  size_t n = c->hn;
  uint32_t v = s->heap[i];
  for (;;) {
    size_t child = 2 * i + 1;
    if (child >= n) {
      break;
    }
    if (child + 1 < n && ev_less(s, s->heap[child + 1], s->heap[child])) {
      child++;
    }
    if (!ev_less(s, s->heap[child], v)) {
      break;
    }
    s->heap[i] = s->heap[child];
    s->hpos[s->heap[i]] = (uint32_t)i;
    i = child;
  }
  s->heap[i] = v;
  s->hpos[v] = (uint32_t)i;
}

/** Take the pair at place @p pos out of the heap, if it is in it. */
static void heap_remove(Ctx * c, uint32_t pos) {
  GSHCPU_Exact_Scratch * s = c->s;
  size_t i = s->hpos[pos];
  if (i == NONE) {
    return;
  }
  s->hpos[pos] = NONE;
  c->hn--;
  if (i == c->hn) {
    return;
  }
  s->heap[i] = s->heap[c->hn];
  s->hpos[s->heap[i]] = (uint32_t)i;
  heap_down(c, i);
  heap_up(c, i);
}

/**
 * The meeting of the neighbours at places @p pos and @p pos + 1, if they meet inside the slab (UC14 c): `p`, the left one,
 * moves right of `q` iff `D > 0`, at the depth `N / D`.  Sets the event and returns 1, or returns 0.
 */
static int event_of(Ctx * c, uint32_t pos, uint32_t * n, uint32_t * d) {
  const GSHCPU_Exact_Scratch * s = c->s;
  uint32_t ip = c->ord[pos];
  uint32_t iq = c->ord[pos + 1];
  const Frag * p = &s->frag[ip];
  const Frag * q = &s->frag[iq];
  int64_t dd = (int64_t)p->dx * q->dy - (int64_t)q->dx * p->dy;
  if (dd <= 0) {
    return 0;
  }
  int64_t nn = (int64_t)s->a[iq] * p->dy - (int64_t)s->a[ip] * q->dy;
  if (nn <= 0 || nn >= c->height * dd) {
    return 0; /* nn <= 0 with a meeting ahead cannot happen with the order true at the top: a defect the sortedness check finds */
  }
  *n = (uint32_t)nn;
  *d = (uint32_t)dd;
  return 1;
}

/** Recompute the event of the pair at place @p pos and put it where it belongs in the heap. */
static void event_set(Ctx * c, uint32_t pos) {
  GSHCPU_Exact_Scratch * s = c->s;
  uint32_t n = 0;
  uint32_t d = 0;
  if (!event_of(c, pos, &n, &d)) {
    heap_remove(c, pos);
    return;
  }
  s->evn[pos] = n;
  s->evd[pos] = d;
  size_t i = s->hpos[pos];
  if (i == NONE) {
    i = c->hn++;
    s->heap[i] = pos;
    s->hpos[pos] = (uint32_t)i;
  }
  heap_up(c, i);
  heap_down(c, s->hpos[pos]);
}

/* --- the exact antiderivative ---------------------------------------------------------------------------------- */

/** `floor(2^30 F_f(N / D))`, `F` the integral of `x_f` over the depths from the slab's top to `N / D` (UC15). */
static uint64_t f30_at(const GSHCPU_Exact_Scratch * s, uint32_t id, uint64_t n, uint64_t d) {
  const Frag * f = &s->frag[id];
  int64_t num = 2 * (int64_t)s->a[id] * (int64_t)n * (int64_t)d + (int64_t)f->dx * (int64_t)n * (int64_t)n;
  uint64_t unum = (uint64_t)num; /* >= 0: x >= 0 on the slab */
  uint64_t den = 2u * (uint64_t)f->dy * d * d;
  uint64_t q = unum / den;
  uint64_t rm = unum - q * den;
  return (q << 30) + ((rm << 30) / den);
}

/** `floor(2^30 F_f(H))` at the slab's end, the depth @p h. */
static uint64_t f30_end(const GSHCPU_Exact_Scratch * s, uint32_t id, int64_t h) {
  const Frag * f = &s->frag[id];
  uint64_t num = (uint64_t)(2 * (int64_t)s->a[id] * h + (int64_t)f->dx * h * h);
  return (num << 30) / (2u * (uint64_t)f->dy);
}

/* --- the sweep of a slab ---------------------------------------------------------------------------------------- */

/** The gap windings of the slab in the order of `ord`, with their weights. */
static void slab_gaps(Ctx * c) {
  GSHCPU_Exact_Scratch * s = c->s;
  const GSHCPU_Exact_Params * pr = c->pr;
  Gap * g = s->gap;
  for (int p = 0; p < 3; p++) {
    g[0].w[p] = (int32_t)c->wind0[p];
  }
  for (size_t i = 0; i <= c->n; i++) {
    if (i > 0) {
      const Frag * f = &s->frag[c->ord[i - 1]];
      g[i] = g[i - 1];
      g[i].w[f->part] += pr->eps[f->part] * f->s * (int32_t)f->mu;
    }
    for (int p = 0; p < pr->parts; p++) {
      int64_t w = g[i].w[p] < 0 ? -(int64_t)g[i].w[p] : (int64_t)g[i].w[p];
      c->wmax = w > c->wmax ? w : c->wmax;
    }
    g[i].om = omega_of(pr, g[i].w);
  }
}

/** Sweep the slab: the sentinel, the meetings in order, the ends (UC14, UC15). */
static void sweep_slab(Ctx * c) {
  GSHCPU_Exact_Scratch * s = c->s;
  const GSHCPU_Exact_Params * pr = c->pr;
  const int64_t height = c->height;
  const size_t n = c->n;
  c->work += 6u * n + 8u;
  slab_gaps(c);
  Gap * g = s->gap;
  c->acc += (uint64_t)((int64_t)64 * height * (int64_t)g[n].om) << 30;
  /* The meetings of the neighbours; with none, the slab is crossing-free and the heap is never built. */
  c->hn = 0;
  if (n >= 2) {
    for (size_t pos = 0; pos + 1 < n; pos++) {
      uint32_t en = 0;
      uint32_t ed = 0;
      if (event_of(c, (uint32_t)pos, &en, &ed)) {
        s->evn[pos] = en;
        s->evd[pos] = ed;
        s->hpos[pos] = (uint32_t)c->hn;
        s->heap[c->hn++] = (uint32_t)pos;
      } else {
        s->hpos[pos] = NONE;
      }
    }
    for (size_t i = c->hn / 2; i-- > 0;) {
      heap_down(c, i);
    }
  }
  while (c->hn > 0) {
    uint32_t pos = s->heap[0];
    uint64_t en = s->evn[pos];
    uint64_t ed = s->evd[pos];
    heap_remove(c, pos);
    uint32_t ip = c->ord[pos];
    uint32_t iq = c->ord[pos + 1];
    const Frag * q = &s->frag[iq];
    Gap mid = g[pos];
    mid.w[q->part] += pr->eps[q->part] * q->s * (int32_t)q->mu;
    for (int p = 0; p < pr->parts; p++) {
      int64_t w = mid.w[p] < 0 ? -(int64_t)mid.w[p] : (int64_t)mid.w[p];
      c->wmax = w > c->wmax ? w : c->wmax;
    }
    mid.om = omega_of(pr, mid.w);
    int32_t dd = g[pos].om + g[pos + 2].om - g[pos + 1].om - mid.om;
    if (dd != 0) {
      uint64_t fp = f30_at(s, ip, en, ed);
      uint64_t fq = f30_at(s, iq, en, ed);
      c->acc += (uint64_t)(int64_t)dd * (fp - fq);
    }
    c->events++;
    c->work += (uint64_t)c->logn + 12u;
    g[pos + 1] = mid;
    c->ord[pos] = iq;
    c->ord[pos + 1] = ip;
    /* The pair that just exchanged has met; the pairs on either side are new neighbours. */
    if (pos > 0) {
      event_set(c, pos - 1);
    }
    if (pos + 2 < n) {
      event_set(c, pos + 1);
    }
  }
  for (size_t i = 0; i < n; i++) {
    int32_t cf = g[i].om - g[i + 1].om;
    if (cf != 0) {
      c->acc += (uint64_t)(int64_t)cf * f30_end(s, c->ord[i], height);
      c->end_evals++;
    }
  }
  if (gshcpu_exact_check) {
    for (size_t i = 1; i < n; i++) {
      if (right_of_at(s, c->ord[i - 1], c->ord[i], height)) {
        c->status = GSHCPU_EXACT_IDENTITY; /* UC16 iii: a missed meeting leaves an inversion */
        return;
      }
    }
  }
}

/* --- the slabs ---------------------------------------------------------------------------------------------------- */

/**
 * Walk the slabs top to bottom: with @p count set, only to count the meetings (each slab's order is carried, sorted by the
 * order at its end, and the inversions summed, stopping once they pass @p limit); otherwise to sweep and accumulate.  The
 * winding on the left side at the top is `wtop` per part; the steps of `J` apply from their heights.
 */
static int walk_slabs(Ctx * c, int count, uint64_t limit, uint64_t * crossings) {
  GSHCPU_Exact_Scratch * s = c->s;
  const GSHCPU_Exact_Params * pr = c->pr;
  int64_t left[3] = {0, 0, 0};
  for (int p = 0; p < pr->parts; p++) {
    left[p] = c->wtop[p];
  }
  c->n_prev = 0;
  c->ord = s->ord;
  c->ord2 = s->ord2;
  uint64_t total = 0;
  for (int h = 0; h <= 64; h++) {
    if (!c->mark[h]) {
      continue;
    }
    for (int p = 0; p < pr->parts; p++) {
      left[p] += c->jnet[p][h]; /* the steps at this height apply from here down */
    }
    int yb = h + 1;
    while (yb <= 64 && !c->mark[yb]) {
      yb++;
    }
    if (yb > 64) {
      break;
    }
    c->ya = h;
    c->yb = yb;
    c->height = yb - h;
    for (int p = 0; p < 3; p++) {
      c->wind0[p] = p < pr->parts ? pr->eps[p] * left[p] : 0;
    }
    slab_order(c);
    if (count) {
      c->work += (uint64_t)c->n * ((uint64_t)c->logn + 1u);
      uint64_t k = c->n >= 2 ? sort_bottom(s, c->ord, s->sc, c->n, c->height) : 0;
      total += k;
      if (total > limit) {
        *crossings = total;
        return GSHCPU_EXACT_LIMIT;
      }
    } else {
      c->slabs++;
      sweep_slab(c);
      if (c->status != GSHCPU_EXACT_OK) {
        return c->status;
      }
    }
    c->n_prev = c->n;
  }
  *crossings = total;
  return GSHCPU_EXACT_OK;
}

/** The winding at the top of each part's left side from the cover summed there (UC4). */
static int tops(Ctx * c, const GSHCPU_Exact_Cell * cell) {
  for (int p = 0; p < c->pr->parts; p++) {
    int64_t sum = 0;
    for (int h = 0; h <= 64; h++) {
      if (c->jnet[p][h] != 0) {
        sum += (int64_t)c->jnet[p][h] * (64 - h);
      }
    }
    int64_t rem = cell->a_left[p] - sum;
    if (rem % 64 != 0) {
      return GSHCPU_EXACT_IDENTITY;
    }
    c->wtop[p] = rem / 64;
    if (c->wtop[p] > GSHCPU_EXACT_WINDING_LIMIT || c->wtop[p] < -GSHCPU_EXACT_WINDING_LIMIT) {
      return GSHCPU_EXACT_LIMIT; /* a winding no outline of a library-bounded size has: the int32 of a gap would not hold it */
    }
  }
  return GSHCPU_EXACT_OK;
}

/** Whether a cell of @p n fragments could have more meetings than @p budget: `n (n - 1) / 2 > budget`. */
static int may_pass(uint64_t n, uint64_t budget) {
  return budget != 0 && n >= 2 && (n * (n - 1)) / 2 > budget;
}

/** Fold the cell's figures into the scratch's totals. */
static void note(Ctx * c, uint64_t crossings) {
  GSHCPU_Exact_Scratch * s = c->s;
  s->last_wmax = c->wmax;
  s->last_pieces = c->raw;
  s->last_distinct = c->nfrag;
  s->last_slabs = c->slabs;
  s->last_crossings = crossings;
  s->last_events = c->events;
  s->last_end_evals = c->end_evals;
  s->events += c->events;
  s->end_evals += c->end_evals;
  s->crossings += crossings;
  if (c->raw > s->pieces_max) {
    s->pieces_max = c->raw;
  }
  if (c->nfrag > s->distinct_max) {
    s->distinct_max = c->nfrag;
  }
  if (crossings > s->crossings_max) {
    s->crossings_max = crossings;
  }
  if ((uint64_t)c->wmax > s->wmax_max) {
    s->wmax_max = (uint64_t)c->wmax;
  }
}

static void ctx_init(Ctx * c, const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s) {
  memset(c, 0, sizeof *c);
  c->pr = params;
  c->s = s;
  s->last_wmax = 0;
  s->last_pieces = 0;
  s->last_distinct = 0;
  s->last_slabs = 0;
  s->last_crossings = 0;
  s->last_events = 0;
  s->last_end_evals = 0;
}

int gshcpu_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  if (gshcpu_exact_hook) {
    return gshcpu_exact_hook(params, s, cell, out, work);
  }
  Ctx c;
  ctx_init(&c, params, s);
  int status = collect(&c, cell, 1);
  if (status != GSHCPU_EXACT_OK) {
    *work += c.work;
    return status;
  }
  c.logn = ceil_log2(c.nfrag ? c.nfrag : 1u);
  if (params->mode == GSHCPU_EXACT_OVERLAP && c.weighted <= 1u) {
    /* One piece left, or none, once the seams are cancelled: the cell is split by one contour into two parts whose windings
     * differ by one, so the nonzero fill covers all of it or the part the dense sum measures, and the fold of the dense value
     * is the fold of the area.  (Most of a stroke's boundary pixels: its edge, whose seams with the pieces beside it cancel.) */
    *work += c.work;
    *out = 0;
    note(&c, 0);
    return GSHCPU_EXACT_OK;
  }
  status = tops(&c, cell);
  if (status != GSHCPU_EXACT_OK) {
    *work += c.work;
    return status;
  }
  uint64_t crossings = 0;
  int counted = 0;
  if (may_pass(c.nfrag, params->max_crossings) || (gshcpu_exact_check && c.nfrag >= 2)) {
    /* UC19: the meetings are counted before they are made, so that a cell past the budget costs its count and not the budget. */
    status = walk_slabs(&c, 1, params->max_crossings ? params->max_crossings : UINT64_MAX, &crossings);
    counted = 1;
    if (status != GSHCPU_EXACT_OK) {
      *work += c.work;
      return status;
    }
  }
  uint64_t swept = 0;
  status = walk_slabs(&c, 0, 0, &swept);
  *work += c.work;
  if (status != GSHCPU_EXACT_OK) {
    return status;
  }
  if (counted && gshcpu_exact_check && c.events != crossings) {
    return GSHCPU_EXACT_IDENTITY; /* the count and the sweep disagree */
  }
  /* The result, rounded to a doubled unit with ties up as the earlier step rounded: the quantity rounded is the area of the
   * regions of UC8, and for a part's own overlap what the dense sum holds beyond the union (`dense - area`, the shared
   * area).  Each `F30` is low by less than one unit of `2^-30`, a meeting adds two of them times a coefficient of at most 4,
   * a slab's end one times 2, so the sum is within `8 K + 2 E` units of the exact one (UC15); a cell whose exact value is
   * a tie, which lattice geometry makes common (a thin feature of whole units is a half unit of area), would otherwise round
   * either way by the sign of that error.  The offset `beta` makes it round up, as it exactly does. */
  uint64_t beta = 8u * c.events + 2u * c.end_evals + 1u;
  int64_t v = c.acc >= ((uint64_t)1 << 63) ? -(int64_t)(~c.acc + 1u) : (int64_t)c.acc;
  if (params->mode == GSHCPU_EXACT_OVERLAP) {
    /* `floor(dense - v / 2^30 + 1/2 + beta / 2^30)` without forming `dense * 2^30`, which a deep winding overflows. */
    int64_t t = v - ((int64_t)1 << 29) - (int64_t)beta;
    int64_t up = t >= 0 ? ((t + (((int64_t)1 << 30) - 1)) >> 30) : -((-t) >> 30);
    *out = cell->dense[0] - up;
  } else {
    int64_t q = v + ((int64_t)1 << 29) + (int64_t)beta;
    *out = q >= 0 ? (q >> 30) : -(((-q) + (((int64_t)1 << 30) - 1)) >> 30);
  }
  note(&c, c.events);
  return GSHCPU_EXACT_OK;
}

int gshcpu_exact_crossings(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    uint64_t * crossings, uint64_t * work) {
  Ctx c;
  ctx_init(&c, params, s);
  *crossings = 0;
  int status = collect(&c, cell, 0);
  if (status != GSHCPU_EXACT_OK) {
    *work += c.work;
    return status;
  }
  c.logn = ceil_log2(c.nfrag ? c.nfrag : 1u);
  if (c.nfrag >= 2 && may_pass(c.nfrag, params->max_crossings)) {
    status = walk_slabs(&c, 1, params->max_crossings, crossings);
  }
  *work += c.work;
  return status;
}

/* --- the memory of a cover's exact step ---------------------------------------------------------------------------- */

/** Pieces of the pool: enough for one cell at the cap, never fewer than the minimum. */
static size_t pool_pieces(uint64_t max_pieces) {
  return max_pieces < GSHCPU_EXACT_PIECES_MIN ? (size_t)GSHCPU_EXACT_PIECES_MIN : (size_t)max_pieces;
}

size_t gshcpu_exact_bytes(size_t width, uint64_t max_pieces, int parts) {
  size_t columns;
  size_t cells;
  size_t arrays;
  size_t pool;
  size_t total;
  if (max_pieces > (uint64_t)GSHCPU_EXACT_FRAGMENTS_CEILING || !gcu_safe_add_size(width, 1u, &columns) ||
      !gcu_safe_mul_size(columns, GSHCPU_RENDER_BAND_ROWS, &cells) || !gcu_safe_mul_size(cells, 2u * sizeof(uint32_t), &arrays) ||
      !gcu_safe_mul_size(pool_pieces(max_pieces), sizeof(GSHCPU_Piece), &pool) ||
      !gcu_safe_add_size(arrays, pool, &total) ||
      !gcu_safe_add_size(total, gshcpu_exact_scratch_bytes(max_pieces, parts), &total) ||
      !gcu_safe_add_size(total, 64u, &total)) {
    return SIZE_MAX;
  }
  if (parts == 3) {
    size_t composed;
    size_t sums;
    if (!gcu_safe_mul_size(cells, 2u * sizeof(uint16_t), &composed) || !gcu_safe_mul_size(columns, 3u * sizeof(int64_t), &sums) ||
        !gcu_safe_add_size(total, composed, &total) || !gcu_safe_add_size(total, sums, &total) ||
        !gcu_safe_add_size(total, 32u, &total)) {
      return SIZE_MAX;
    }
  }
  return total;
}

void gshcpu_exact_init(GSHCPU_Exact * ex, void * memory, size_t width, uint64_t max_pieces, int parts) {
  unsigned char * at = memory;
  size_t cells = (width + 1u) * GSHCPU_RENDER_BAND_ROWS;
  memset(ex, 0, sizeof *ex);
  ex->max_pieces = max_pieces;
  ex->count = (uint32_t *)(void *)carve(&at, cells * sizeof(uint32_t));
  ex->slot = (uint32_t *)(void *)carve(&at, cells * sizeof(uint32_t));
  ex->pool_cap = pool_pieces(max_pieces);
  ex->pool = (GSHCPU_Piece *)(void *)carve(&at, ex->pool_cap * sizeof(GSHCPU_Piece));
  gshcpu_exact_scratch_init(&ex->scratch, carve(&at, gshcpu_exact_scratch_bytes(max_pieces, parts)), max_pieces, parts);
  if (parts == 3) {
    ex->union_k = (uint16_t *)(void *)carve(&at, cells * sizeof(uint16_t));
    ex->clip_k = (uint16_t *)(void *)carve(&at, cells * sizeof(uint16_t));
    ex->prefix = (int64_t *)(void *)carve(&at, (width + 1u) * 3u * sizeof(int64_t));
  }
}

/* --- the declared bound of the step's work (SC11) ------------------------------------------------------------------- */

static uint64_t sat_add(uint64_t a, uint64_t b) {
  uint64_t r;
  return gcu_safe_add_u64(a, b, &r) ? r : UINT64_MAX;
}

static uint64_t sat_mul(uint64_t a, uint64_t b) {
  uint64_t r;
  return gcu_safe_mul_u64(a, b, &r) ? r : UINT64_MAX;
}

/** The integer square root of @p x (the largest r with r r <= x). */
static uint64_t isqrt(uint64_t x) {
  uint64_t lo = 0;
  uint64_t hi = x < ((uint64_t)1 << 32) ? x + 1u : ((uint64_t)1 << 32);
  while (lo + 1u < hi) {
    uint64_t mid = lo + (hi - lo) / 2u;
    if (mid <= x / mid) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return lo;
}

uint64_t gshcpu_exact_cell_work_bound(uint64_t pieces, uint64_t crossings) {
  /* W_cell = m + S (6 n' + 8) + S n' (L + 1) + K (L + 12): collecting the pieces; for each of at most 64 slabs the carry, the
   * repair, the gaps and the ends (6 n' + 8), and when the meetings are counted a merge sort by the end order (n' (L + 1));
   * and for each meeting a heap operation and an exchange (L + 12).  `L = ceil(log2 n')`, `n' <= m`, and `K` is at most the
   * pairs of fragments, `n' (n' - 1) / 2`, and at most the crossings budget (UC19). */
  uint64_t m = pieces;
  uint64_t l = ceil_log2(m > 1 ? m : 1u);
  uint64_t slab = sat_add(sat_mul(6u, m), 8u);
  uint64_t count = sat_mul(m, l + 1u);
  uint64_t per_slab = sat_add(slab, count);
  uint64_t k = m > 0 ? sat_mul(m, m - 1u) / 2u : 0u;
  if (crossings != 0 && crossings < k) {
    k = crossings;
  }
  return sat_add(sat_add(m, sat_mul(64u, per_slab)), sat_mul(k, l + 12u));
}

uint64_t gshcpu_exact_work_bound(uint64_t max_cells, uint64_t max_points, uint64_t max_dimension, uint64_t max_pieces,
    uint64_t max_crossings) {
  /* The cost of a cell of `m` pieces is at most W_cell(m, K), and every piece but a horizontal one on a cell's left side is a
   * deposit, which the cell cap counts: at most `max_cells` of them in the whole item, however they fall in cells.  A cell of
   * `n'` fragments costs per fragment at most `1 + 64 (6 + L + 1) + 8 * 64 / n'` and its meetings `K / n' (L + 12)`, with
   * `K / n' <= (n' - 1) / 2` and `K / n' <= budget / n'`, so the worst fragment is that of a cell with about `sqrt(2 budget)`
   * fragments or, with fewer pieces than that, of one at the cap.  The horizontal pieces cost a visit and a collection each. */
  uint64_t m = max_pieces;
  uint64_t l = ceil_log2(m > 1 ? m : 1u);
  uint64_t per_fragment = sat_add(sat_add(1u, sat_mul(64u, 7u + l)), sat_mul(64u, 8u));
  uint64_t reach = m > 0 ? (m - 1u) / 2u + 1u : 0u;
  if (max_crossings != 0) {
    uint64_t by_budget = isqrt(sat_mul(2u, max_crossings)) / 2u + 2u;
    if (by_budget < reach) {
      reach = by_budget;
    }
  }
  per_fragment = sat_add(per_fragment, sat_mul(reach, l + 12u));
  uint64_t pieces = sat_add(max_cells, sat_mul(max_points, max_dimension));
  return sat_add(pieces, sat_mul(max_cells, per_fragment));
}
