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
 * The exact step of strokes story S4b, frozen: the walk of one cell as it was when the sweep of S4c replaced it (the
 * step of the library at the commit that closed S4b), copied verbatim apart from the names and the memory it works in.
 * It is the second engine of the dual render (tests/unit/test_render_dense.cpp, tests/unit/test_render_dual.cpp, the
 * fuzz replay): the same pieces go through the library's step and through this one, and the coverage must be the same
 * byte for byte wherever the older step is exact enough, which is every cell of at most a few hundred pieces.  Its
 * value is the exact area less the roundings of its own procedure (a sum of per-sub-slab, per-gap floors over a
 * partition of each slab at every crossing), so it differs from the exact area by at most `2^-13 (m + 1) W + T 2^-20`
 * unit squared (`m` the fragments, `T` the sub-slabs, `W` the largest winding), and from the library's step by about that,
 * the library's being far closer to the exact area.  Its winding bound is the 255 it always had.
 *
 * It is compiled only into test programs, never into the library, and the library reaches it only through the hook
 * `gshcpu_exact_hook` (render_internal.h), which nothing in the library sets.  It allocates its own memory (the
 * library's scratch is of another shape) and is not thread safe across programs that share a thread.
 */

#include "../../src/render/render_internal.h"

#include <stdlib.h>
#include <string.h>

/** The most the winding may be in magnitude anywhere in a cell (the arithmetic's product bound depends on it). */
#define EXACT_WINDING_MAX 255

/** A fragment oriented top to bottom: its upper end, `dx` to its lower end, its height and sign. */
typedef struct Frag {
  int16_t px, py;
  int16_t dx, dy;
  int8_t s;       /* +1 when the contour goes down along it */
  uint8_t part;
  uint32_t idx;   /* its place in the sorted list: the last tie-break */
} Frag;

/** A fragment as one slab sees it: `x(u) = (a + dx u) / dy` at depth `u` below the slab's top. */
typedef struct Active {
  int32_t a;
  int16_t dx, dy;
  int8_t s;
  uint8_t part;
  uint32_t idx;
} Active;

/** One cell's working memory, grown as cells need it. */
typedef struct S4b_Scratch {
  size_t capacity;
  Frag * frag;
  Active * act;
  uint32_t * ord;
  uint32_t * xa;
  uint32_t * xb;
  GSHCPU_Piece * sorted;
} S4b_Scratch;

static _Thread_local S4b_Scratch g_scratch;

static void scratch_for(size_t pieces) {
  S4b_Scratch * s = &g_scratch;
  if (pieces <= s->capacity) {
    return;
  }
  size_t m = pieces < 256 ? 256 : pieces;
  free(s->frag);
  free(s->act);
  free(s->ord);
  free(s->xa);
  free(s->xb);
  free(s->sorted);
  s->frag = malloc(m * sizeof *s->frag);
  s->act = malloc(m * sizeof *s->act);
  s->ord = malloc(m * sizeof *s->ord);
  s->xa = malloc(m * sizeof *s->xa);
  s->xb = malloc(m * sizeof *s->xb);
  s->sorted = malloc(m * sizeof *s->sorted);
  if (!s->frag || !s->act || !s->ord || !s->xa || !s->xb || !s->sorted) {
    abort();
  }
  s->capacity = m;
}

typedef struct Event {
  int32_t n;
  int32_t d;
} Event;


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
  S4b_Scratch * s;
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
  S4b_Scratch * s = w->s;
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
  S4b_Scratch * s = w->s;
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
  S4b_Scratch * s = w->s;
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

int gshcpu_s4b_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * unused, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  (void)unused;
  size_t total_pieces = 0;
  for (int p = 0; p < params->parts; p++) {
    total_pieces += cell->count[p];
  }
  scratch_for(total_pieces);
  S4b_Scratch * s = &g_scratch;
  Walk w;
  memset(&w, 0, sizeof w);
  w.params = params;
  w.s = s;
  w.status = GSHCPU_EXACT_OK;
  w.mark[0] = 1;
  w.mark[64] = 1;
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
      return w.status;
    }
  }
  *work += w.work;
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

