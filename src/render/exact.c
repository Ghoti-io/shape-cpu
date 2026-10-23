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
 * The exact step of a stroke part's coverage (render.h, UC1 to UC11; strokes story S4b): the area of the union of the
 * pieces of one cell, computed in integers from the cell's own edge pieces.
 *
 * **The cell.**  A cell is a pixel of the accumulator; its frame is the 26.6 square `[0, 64] x [0, 64]`, y down.  A
 * *piece* is what the rasterizer (cover.c) cuts an edge into inside the cell: two ends in the frame, as integers in
 * `[0, 64]`, directed along the contour.  A piece of nonzero height is a *fragment*; one of height 0 that touches the
 * cell's left side is a *flat piece* (the dense accumulation deposits nothing for it, and it steps the winding on the
 * left side).  Nothing else of the outline matters inside a cell.
 *
 * **The winding.**  Let `W(x, y)` be the signed number of edges crossing the horizontal ray to the left of `(x, y)`,
 * an edge directed down counting +1, and `w = eps W` with `eps` the sign that makes a positively wound piece's inside 1.
 * Inside the cell `w(x, y)` is the winding just left of the cell, `W(0-, y)`, plus the signs of the fragments left of
 * `x` at height `y`.  `W(0-, .)` is piecewise constant and steps only at the heights where a contour crosses the line
 * `x = 0`: `J(h)` is the sum over pieces of -1 for a piece that starts at `(0, h)` and +1 for a piece that ends there,
 * a piece of length 0 counting nothing.  What the rasterizer has already summed of everything left of the cell is
 * `A_left`, the integral of `W(0-, y)` over the side, in the cover's units, so
 *
 *     64 W_top = A_left - sum_h J(h) (64 - h)
 *
 * gives `W_top`, the winding at the top of the side, exactly; a remainder is an error (a piece missed or invented).
 *
 * **The slabs.**  The distinct heights of the fragments' ends and of the steps of `J` cut the cell into slabs of
 * integer height, at most 64 of them.  In a slab every fragment either spans it or misses it.  Two spanning fragments
 * meet at `u = N / D` (`|N| < 2^21`, `0 < D <= 2^13`); the distinct meeting depths cut the slab into sub-slabs in which
 * no two fragments cross, so their left-to-right order is one order, decided exactly at the sub-slab's start (ties by
 * slope, then by the fragment's place in the sorted list, which only coincident fragments, of zero width, ever reach).
 * In a sub-slab the width of every gap between neighbours is linear in the depth, so a gap's area is its mean width
 * times the depth, and its weight is the winding's function (`(|w| - 1)^+` for the overlap `O`, with the area of the
 * wrong-way windings `N` summed on the side, since the fill covers a winding of either sign; a predicate for the three-part
 * area of UC8).  The dense sum holds `2 O - 4 N` doubled units more than the fill covers.
 *
 * **The arithmetic** is integer: orders and meetings are decided in `int64_t` on products below `2^34`; areas are
 * accumulated in fixed point.  A boundary is rounded to 16 fractional bits, `u_fp = floor(2^16 N / D)`; a fragment's `x` at
 * `u_fp` is held to 20 fractional bits (a floor, so low by less than `2^-20` unit); a gap's width at either end is
 * clamped at 0 (two fragments that meet exactly may round the other way by one place); a term is
 * `(weight (Wa + Wb) du) >> 17` in units of `2^-20` unit squared, `Wa`, `Wb` the widths below `2^27` together and `du` below
 * `2^22`, `weight` below `2^8`, so below `2^57` in `uint64_t`.  An `x` is low by less than `2^-20` unit, a term by less than
 * `2^-20` unit squared, and a mis-timed meeting (a boundary off by less than `2^-16`) leaves an area under `2^-25`; so a
 * cell of `m` fragments, `T` sub-slabs and a largest weight `Wm` is low by at most `2^-13 (m + 1) Wm + T 2^-20` unit squared.
 * That is the bound `exact_tolerance` restates for the check.  No `long double`, no `__int128`, no libm, no FMA.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/cutil/safemath.h>

#include <string.h>

/**
 * The most the winding may be in magnitude anywhere in a cell (the arithmetic's product bound depends on it).  A build
 * that only measures (tools/exact-census.c) may name a larger one on the command line to see how deep the windings of a
 * drawing go; no larger value than 32767 keeps a product in `uint64_t`.
 */
#ifndef EXACT_WINDING_MAX
#define EXACT_WINDING_MAX 255
#endif

typedef GSHCPU_Exact_Frag Frag;
typedef GSHCPU_Exact_Active Active;
/** The depth at which two fragments meet, `n / d` with `d > 0`. */
typedef struct Event {
  int32_t n;
  int32_t d;
} Event;

/* --- scratch ------------------------------------------------------------------------------------------------- */

/** Bytes of one cell's scratch for @p max_pieces pieces over @p parts parts. */
size_t gshcpu_exact_scratch_bytes(uint64_t max_pieces, int parts) {
  size_t m = (size_t)max_pieces * (size_t)parts;
  if (parts == 0) {
    return 0;
  }
  size_t bytes = 0;
  bytes += m * sizeof(Frag) + 16u;
  bytes += m * sizeof(Active) + 16u;
  bytes += m * sizeof(uint32_t) + 16u;      /* ord */
  bytes += 2u * m * sizeof(uint32_t) + 32u; /* xa, xb */
  bytes += m * sizeof(GSHCPU_Piece) + 16u;  /* the cell's pieces, sorted */
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
  s->frag = (void *)carve(&at, m * sizeof(Frag));
  s->act = (void *)carve(&at, m * sizeof(Active));
  s->ord = (void *)carve(&at, m * sizeof(uint32_t));
  s->xa = (void *)carve(&at, m * sizeof(uint32_t));
  s->xb = (void *)carve(&at, m * sizeof(uint32_t));
  s->sorted = (void *)carve(&at, m * sizeof(GSHCPU_Piece));
}

/* --- exact comparisons ----------------------------------------------------------------------------------------- */

/** Compare the event times `a` and `b` (both `D > 0`): `-1`, `0`, `1`. */
static int event_cmp(Event a, Event b) {
  int64_t l = (int64_t)a.n * b.d;
  int64_t r = (int64_t)b.n * a.d;
  return l < r ? -1 : l > r ? 1 : 0;
}

/**
 * The order of active fragments @p p and @p q just after depth `n0 / d0` (`d0 > 0`): by `x` there, then by slope (the one
 * that moves left faster as the depth grows is left of the other after they meet), then by index.
 * Operands: `a` and `d0` below `2^13`, `dx` below `2^7`, `n0` below `2^21`, so a numerator is below `2^28` and its product
 * with `dy` below `2^34`.
 */
static int order_cmp(const Active * p, const Active * q, int64_t n0, int64_t d0) {
  int64_t xp = ((int64_t)p->a * d0 + (int64_t)p->dx * n0) * q->dy;
  int64_t xq = ((int64_t)q->a * d0 + (int64_t)q->dx * n0) * p->dy;
  if (xp != xq) {
    return xp < xq ? -1 : 1;
  }
  int64_t sp = (int64_t)p->dx * q->dy;
  int64_t sq = (int64_t)q->dx * p->dy;
  if (sp != sq) {
    return sp < sq ? -1 : 1;
  }
  return p->idx < q->idx ? -1 : p->idx > q->idx ? 1 : 0;
}

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

/**
 * Cancel what cancels: sorted pieces @p v, a piece and its reverse together wind nothing (opposite signs on one segment add
 * to zero in every winding, the cover and the area), so a seam shared by two pieces of an outline, which is most of what a
 * pixel in the middle of a thick stroke holds, is not a piece of the cell.  Equal pieces keep, and only the net of a segment
 * is left, in the direction it has.  @return the pieces left, at the start of @p v.
 */
static size_t cancel_pairs(GSHCPU_Piece * v, size_t n) {
  size_t out = 0;
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
    for (int64_t k = 0; k < (net < 0 ? -net : net); k++) {
      v[out++] = net > 0 ? forward : backward;
    }
    i = j;
  }
  return out;
}

/* --- the walk of one cell -------------------------------------------------------------------------------------------- */

typedef struct Walk {
  const GSHCPU_Exact_Params * params;
  GSHCPU_Exact_Scratch * s;
  size_t nfrag;
  size_t kept_total;      /* pieces left after the seams cancel, all parts */
  int64_t wtop[3];
  int32_t jnet[3][65];
  uint8_t mark[65];
  int64_t wind0[3];       /* the winding on the left side at the current slab's top, eps applied */
  /* sums */
  uint64_t area;          /* the mode's weighted area, 2^-20 unit squared */
  uint64_t integral_pos[3]; /* the windings' own integrals, per part (the check), 2^-20 unit squared: positive windings */
  uint64_t integral_neg[3]; /* ... and negative ones, as magnitudes */
  int64_t wmax;           /* the largest winding met in magnitude */
  uint64_t subslabs;
  uint64_t work;
  int status;
  /* the slab */
  int ya, yb;
  size_t n;               /* active fragments */
} Walk;

/**
 * The weight of a gap whose windings are @p w: mode 0 is `(|w0| - 1)^+` (what the nonzero fill counts more than once), mode 1
 * the predicate of UC8 (0 or 1).  A part is filled nonzero: a winding of either sign is inside.  The pieces of an
 * expansion are positively wound, but a sliver of one that the 26.6 lattice has turned inside out is not, and the fill
 * counts it covered.
 */
static uint32_t weight_of(const GSHCPU_Exact_Params * p, const int64_t w[3]) {
  if (p->mode == GSHCPU_EXACT_OVERLAP) {
    int64_t m = w[0] < 0 ? -w[0] : w[0];
    return m > 1 ? (uint32_t)(m - 1) : 0u;
  }
  if (p->mode == GSHCPU_EXACT_REGION) {
    return (p->even_odd ? (w[0] & 1) != 0 : w[0] != 0) ? 1u : 0u;
  }
  /* UC8: P = {w_b != 0} U ({w_a != 0} & F'), F' the clip's region (or its complement). */
  if (w[0] != 0) {
    return 1u;
  }
  if (w[1] == 0) {
    return 0u;
  }
  int64_t c = w[2];
  int in = p->even_odd ? ((c & 1) != 0) : (c != 0);
  if (p->complement) {
    in = !in;
  }
  return in ? 1u : 0u;
}

/** `x` of active fragment @p f at fixed-point depth @p ufp (16 fractional bits), 20 fractional bits, floor. */
static uint32_t x_at(const Active * f, uint64_t ufp) {
  /* a * 2^20 + dx * ufp * 2^4, non-negative (x is in [0, 64] anywhere along the fragment), below 2^35 */
  int64_t num = (int64_t)f->a * 1048576 + (int64_t)f->dx * (int64_t)ufp * 16;
  if (num < 0) {
    num = 0;
  }
  return (uint32_t)((uint64_t)num / (uint64_t)f->dy);
}

/** Put the active fragments in their order just after depth @p at: an insertion sort from the order they have. */
static void order_at(Walk * w, Event at) {
  GSHCPU_Exact_Scratch * s = w->s;
  size_t n = w->n;
  for (size_t i = 1; i < n; i++) {
    uint32_t v = s->ord[i];
    size_t j = i;
    while (j > 0) {
      w->work++;
      if (order_cmp(&s->act[s->ord[j - 1]], &s->act[v], at.n, at.d) <= 0) {
        break;
      }
      s->ord[j] = s->ord[j - 1];
      j--;
    }
    s->ord[j] = v;
  }
}

/** Add the sub-slab from depth @p cur to depth @p next (both `n / d`, `d > 0`), the fragments in order, to the walk's sums. */
static void subslab(Walk * w, Event cur, Event next) {
  GSHCPU_Exact_Scratch * s = w->s;
  const GSHCPU_Exact_Params * pr = w->params;
  size_t n = w->n;
  w->subslabs++;
  w->work += n + 1u;
  uint64_t ua = ((uint64_t)cur.n << 16) / (uint64_t)cur.d;
  uint64_t ub = ((uint64_t)next.n << 16) / (uint64_t)next.d;
  if (ub <= ua) {
    return; /* zero width after rounding */
  }
  uint64_t du = ub - ua;
  for (size_t i = 0; i < n; i++) {
    const Active * f = &s->act[s->ord[i]];
    s->xa[i] = x_at(f, ua);
    s->xb[i] = x_at(f, ub);
  }
  int64_t wind[3] = {w->wind0[0], w->wind0[1], w->wind0[2]};
  const uint32_t edge = 64u << 20;
  for (size_t g = 0; g <= n; g++) {
    if (g > 0) {
      const Active * f = &s->act[s->ord[g - 1]];
      wind[f->part] += pr->eps[f->part] * f->s;
    }
    uint32_t la = g == 0 ? 0u : s->xa[g - 1];
    uint32_t lb = g == 0 ? 0u : s->xb[g - 1];
    uint32_t ra = g == n ? edge : s->xa[g];
    uint32_t rb = g == n ? edge : s->xb[g];
    uint64_t wa = ra > la ? (uint64_t)(ra - la) : 0u;
    uint64_t wb = rb > lb ? (uint64_t)(rb - lb) : 0u;
    uint64_t span = (wa + wb) * du;
    for (int p = 0; p < 3; p++) {
      if (wind[p] > EXACT_WINDING_MAX || wind[p] < -EXACT_WINDING_MAX) {
        w->status = GSHCPU_EXACT_LIMIT;
        return;
      }
    }
    uint32_t weight = weight_of(pr, wind);
    if (weight) {
      w->area += ((uint64_t)weight * span) >> 17;
    }
    for (int p = 0; p < pr->parts; p++) {
      int64_t mag = wind[p] < 0 ? -wind[p] : wind[p];
      if (mag > w->wmax) {
        w->wmax = mag;
      }
      if (wind[p] < 0) {
        w->integral_neg[p] += ((uint64_t)mag * span) >> 17;
      } else if (wind[p] > 0) {
        w->integral_pos[p] += ((uint64_t)mag * span) >> 17;
      }
    }
  }
}

/**
 * The slab's sub-slabs, one after another: the fragments are put in order at the start, the next meeting is the earliest
 * of the neighbours' (two fragments that meet next are neighbours just before, or all of those meeting are), and the
 * sub-slab up to it is added.  Each pair meets once, so there are at most `m (m - 1) / 2 + 1` of them.
 */
static void slab_events(Walk * w) {
  GSHCPU_Exact_Scratch * s = w->s;
  size_t n = w->n;
  const int64_t height = w->yb - w->ya;
  Event cur = {0, 1};
  const Event end = {(int32_t)height, 1};
  for (size_t i = 0; i < n; i++) {
    s->ord[i] = (uint32_t)i;
  }
  for (;;) {
    order_at(w, cur);
    Event next = end;
    for (size_t i = 0; i + 1 < n; i++) {
      w->work++;
      const Active * p = &s->act[s->ord[i]];
      const Active * q = &s->act[s->ord[i + 1]];
      int64_t d = (int64_t)p->dx * q->dy - (int64_t)q->dx * p->dy;
      if (d == 0) {
        continue;
      }
      int64_t nn = (int64_t)q->a * p->dy - (int64_t)p->a * q->dy;
      if (d < 0) {
        d = -d;
        nn = -nn;
      }
      Event e = {(int32_t)nn, (int32_t)d};
      if (nn <= 0 || nn >= height * d || event_cmp(e, cur) <= 0 || event_cmp(e, next) >= 0) {
        continue;
      }
      next = e;
    }
    subslab(w, cur, next);
    if (w->status != GSHCPU_EXACT_OK || event_cmp(next, end) == 0) {
      return;
    }
    cur = next;
  }
}

/** The permitted shortfall of the integral check, in doubled units: restated from the bounds of the file header. */
static uint64_t exact_tolerance(size_t m, uint64_t subslabs, int64_t wmax) {
  /* 2 (2^-13 (m + 1) Wm + T 2^-20) in doubled units, plus the roundings of the two readings and the clamps */
  return (((uint64_t)(m + 1u) * (uint64_t)wmax) >> 12) + (subslabs >> 19) + 3u;
}

int gshcpu_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  Walk w;
  memset(&w, 0, sizeof w);
  w.params = params;
  w.s = s;
  w.status = GSHCPU_EXACT_OK;
  w.mark[0] = 1;
  w.mark[64] = 1;
  s->last_wmax = 0;
  /* The pieces of every part: sorted, and the steps of the winding on the left side. */
  size_t nfrag = 0;
  size_t pieces = 0;
  for (int p = 0; p < params->parts; p++) {
    pieces += cell->count[p];
  }
  if (pieces > s->capacity) {
    return GSHCPU_EXACT_LIMIT; /* the caller counts first: reaching this is a defect, and refuses rather than guesses */
  }
  size_t at = 0;
  for (int p = 0; p < params->parts; p++) {
    GSHCPU_Piece * v = s->sorted + at;
    size_t kept = 0;
    if (cell->count[p]) {
      memcpy(v, cell->pieces[p], cell->count[p] * sizeof *v);
      for (size_t i = 0; i < cell->count[p]; i++) {
        GSHCPU_Piece pc = v[i];
        if (pc.x0 > 64 || pc.x1 > 64 || pc.y0 > 64 || pc.y1 > 64 || (pc.x0 == pc.x1 && pc.y0 == pc.y1)) {
          return GSHCPU_EXACT_IDENTITY; /* a piece that is not of this cell, or has no length */
        }
      }
      sort_pieces(v, cell->count[p]);
      kept = cancel_pairs(v, cell->count[p]);
    }
    w.kept_total += kept;
    for (size_t i = 0; i < kept; i++) {
      GSHCPU_Piece pc = v[i];
      if (pc.x0 == 0) {
        w.jnet[p][pc.y0] -= 1;
      }
      if (pc.x1 == 0) {
        w.jnet[p][pc.y1] += 1;
      }
      if (pc.y0 != pc.y1) {
        Frag * f = &s->frag[nfrag];
        int down = pc.y1 > pc.y0;
        f->px = down ? pc.x0 : pc.x1;
        f->py = down ? pc.y0 : pc.y1;
        f->dx = (int16_t)(down ? pc.x1 - pc.x0 : pc.x0 - pc.x1);
        f->dy = (int16_t)(down ? pc.y1 - pc.y0 : pc.y0 - pc.y1);
        f->s = down ? 1 : -1;
        f->part = (uint8_t)p;
        f->idx = (uint32_t)(at + i);
        w.mark[f->py] = 1;
        w.mark[f->py + f->dy] = 1;
        nfrag++;
      }
    }
    at += cell->count[p];
    w.work += cell->count[p];
  }
  w.nfrag = nfrag;
  if (params->mode == GSHCPU_EXACT_OVERLAP && w.kept_total <= 1u) {
    /* One piece left, or none, once the seams are cancelled: the cell is split by one contour into two parts whose windings
     * differ by one, so the nonzero fill covers all of it or the part the dense sum measures, and the fold of the dense value
     * is the fold of the area.  (Most of a stroke's boundary pixels: its edge, whose seams with the pieces beside it cancel.) */
    *work += w.work;
    *out = 0;
    return GSHCPU_EXACT_OK;
  }
  for (int p = 0; p < params->parts; p++) {
    int64_t sum = 0;
    for (int h = 0; h <= 64; h++) {
      if (w.jnet[p][h] != 0) {
        w.mark[h] = 1;
        sum += (int64_t)w.jnet[p][h] * (64 - h);
      }
    }
    int64_t rem = cell->a_left[p] - sum;
    if (rem % 64 != 0) {
      return GSHCPU_EXACT_IDENTITY;
    }
    w.wtop[p] = rem / 64;
    if (w.wtop[p] > EXACT_WINDING_MAX || w.wtop[p] < -EXACT_WINDING_MAX) {
      return GSHCPU_EXACT_LIMIT;
    }
  }
  /* The slabs. */
  int64_t left[3] = {0, 0, 0};
  for (int p = 0; p < params->parts; p++) {
    left[p] = w.wtop[p];
  }
  for (int h = 0; h <= 64; h++) {
    if (!w.mark[h]) {
      continue;
    }
    for (int p = 0; p < params->parts; p++) {
      left[p] += w.jnet[p][h]; /* the steps at this height apply from here down */
    }
    /* The next marked height ends the slab that starts here. */
    int yb = h + 1;
    while (yb <= 64 && !w.mark[yb]) {
      yb++;
    }
    if (yb > 64) {
      break;
    }
    int ya = h;
    w.ya = ya;
    w.yb = yb;
    for (int p = 0; p < 3; p++) {
      w.wind0[p] = p < params->parts ? params->eps[p] * left[p] : 0;
    }
    for (int p = 0; p < 3; p++) {
      if (w.wind0[p] > EXACT_WINDING_MAX || w.wind0[p] < -EXACT_WINDING_MAX) {
        return GSHCPU_EXACT_LIMIT;
      }
    }
    /* The fragments that span this slab. */
    size_t n = 0;
    for (size_t i = 0; i < nfrag; i++) {
      const Frag * f = &s->frag[i];
      if (f->py <= ya && f->py + f->dy >= yb) {
        Active * a = &s->act[n++];
        a->a = (int32_t)f->px * f->dy + (int32_t)(ya - f->py) * f->dx;
        a->dx = f->dx;
        a->dy = f->dy;
        a->s = f->s;
        a->part = f->part;
        a->idx = f->idx;
      }
    }
    w.work += nfrag;
    w.n = n;
    slab_events(&w);
    if (w.status != GSHCPU_EXACT_OK) {
      *work += w.work;
      s->last_wmax = w.wmax;
      return w.status;
    }
  }
  *work += w.work;
  s->last_wmax = w.wmax;
  /* The check: the windings' own integrals must give back the dense areas the rasterizer summed. */
  for (int p = 0; p < params->parts; p++) {
    int64_t pos = (int64_t)((2u * w.integral_pos[p] + ((uint64_t)1 << 19)) >> 20);
    int64_t neg = (int64_t)((2u * w.integral_neg[p] + ((uint64_t)1 << 19)) >> 20);
    int64_t diff = pos - neg - cell->dense[p];
    if (diff < 0) {
      diff = -diff;
    }
    if ((uint64_t)diff > exact_tolerance(nfrag, w.subslabs, w.wmax)) {
      return GSHCPU_EXACT_IDENTITY;
    }
  }
  if (params->mode == GSHCPU_EXACT_OVERLAP) {
    /* The area the nonzero fill covers is `v + 4 N - 2 O` in doubled units: `v` the dense sum of the winding, `N` the area of
     * what is wound the wrong way (the lattice's slivers) and `O` the overlap `(|w| - 1)^+`.  What is subtracted from the
     * dense sum is `2 O - 4 N`: nothing where there is neither. */
    int64_t t = 2 * (int64_t)w.area - 4 * (int64_t)w.integral_neg[0];
    int64_t q = t + ((int64_t)1 << 19);
    *out = q >= 0 ? (q >> 20) : -(((-q) + (((int64_t)1 << 20) - 1)) >> 20);
  } else {
    *out = (int64_t)((2u * w.area + ((uint64_t)1 << 19)) >> 20);
  }
  return GSHCPU_EXACT_OK;
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

uint64_t gshcpu_exact_work_bound(uint64_t max_cells, uint64_t max_points, uint64_t max_dimension, uint64_t max_pieces) {
  /* One cell of m pieces, f of them fragments: m to collect them; at most 64 slabs, each a scan of the f fragments and then
   * at most P + 1 sub-slabs (P = f (f - 1) / 2, since two fragments meet once), each costing the f - 1 neighbours' meetings,
   * the f + 1 gaps' areas and the ordering (f comparisons, and a swap for every meeting, P in all in a slab).  Only the
   * fragments make sub-slabs, and they are deposits, which the cell cap counts: at most `max_cells` of them in the whole
   * item, however they fall in cells, and the cost of a fragment grows with the cell it is in, so the worst is all of them
   * in cells of the cap's size.  The pieces that are not fragments (a horizontal piece on a cell's left side) cost a
   * visit and a collection each. */
  uint64_t m = max_pieces;
  uint64_t p = sat_mul(m, m > 0 ? m - 1u : 0u) / 2u;
  uint64_t z = sat_add(sat_mul(sat_add(p, 1u), sat_add(sat_mul(3u, m), 2u)), p);
  uint64_t per_fragment = m > 0 ? z / m + (z % m != 0) : 0u;
  uint64_t pieces = sat_add(max_cells, sat_mul(max_points, max_dimension));
  uint64_t fragments = sat_mul(max_cells, sat_mul(64u, sat_add(1u, per_fragment)));
  return sat_add(pieces, fragments);
}
