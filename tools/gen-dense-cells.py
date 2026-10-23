#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Shape CPU.
#
# Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""The committed cells of the exact union step's dense population, with their exact answers (strokes story S4c).

Writes tests/dense_vectors.h, or with --check compares the file on disk with what this would write.  Each vector is one cell of
the exact step (render.h, UC13 to UC16): the pieces of each part in the cell's frame, the winding at the top of each part's left
side, the mode, and what the step must give, computed here in rational arithmetic by tools/dense_reference.py (no rounding
anywhere) and rounded half up: the doubled area of the cell's region, or for a part's own overlap the shared area (the dense
value less that area), where the dense value is what the rasterizer would have summed for these pieces.  Also the meetings of
the cell (UC19: the pairs of fragments whose order at a slab's top differs strictly from the order at its end) and the number
of fragments left once a piece and its reverse cancel, which the step's counters must equal.  It shares no code with the C
library.

A vector whose exact value lies within 1e-6 of a tie that is not one is left out (the step rounds within its error bound of the
boundary, `8 K + 2 E` units of 2^-30, and a test of equality would then depend on the bound); an exact tie is kept and
rounds up, which the step guarantees.

    python3 tools/gen-dense-cells.py          # write tests/dense_vectors.h
    python3 tools/gen-dense-cells.py --check  # fail if it differs
"""
import random
import sys
from fractions import Fraction as F
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dense_reference import area2, predicate  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "tests" / "dense_vectors.h"
EPS_BODY = -1  # GSHCPU_RENDER_EXACT_EPS
EPS_CLIP = 1


def consistent(parts, wtops, eps):
    """The cover to the left and the dense area the rasterizer would have summed, per part."""
    a_left, dense = [], []
    for p, pieces in enumerate(parts):
        jnet, cover, area = {}, 0, 0
        for (x0, y0, x1, y1) in pieces:
            if x0 == 0:
                jnet[y0] = jnet.get(y0, 0) - 1
            if x1 == 0:
                jnet[y1] = jnet.get(y1, 0) + 1
            dy = y1 - y0
            cover += dy
            area += dy * (x0 + x1)
        al = sum(v * (64 - h) for h, v in jnet.items()) + 64 * wtops[p]
        a_left.append(al)
        dense.append(eps[p] * (128 * (al + cover) - area))
    return a_left, dense


def kept(parts):
    """Fragments after a piece and its reverse cancel and equal copies are one (UC13), with the marks they make."""
    frags, marks = [], {0, 64}
    for p, pieces in enumerate(parts):
        net, rep = {}, {}
        for (x0, y0, x1, y1) in pieces:
            a, b = (y0, x0), (y1, x1)
            key = (a, b) if a <= b else (b, a)
            net[key] = net.get(key, 0) + (1 if a <= b else -1)
        jnet = {}
        for key, n in net.items():
            if n == 0:
                continue
            (ya, xa), (yb, xb) = key
            start, end = ((xa, ya), (xb, yb)) if n > 0 else ((xb, yb), (xa, ya))
            (x0, y0), (x1, y1) = start, end
            mu = abs(n)
            if x0 == 0:
                jnet[y0] = jnet.get(y0, 0) - mu
            if x1 == 0:
                jnet[y1] = jnet.get(y1, 0) + mu
            if y0 != y1:
                if y1 > y0:
                    frags.append((x0, y0, x1 - x0, y1 - y0, p))
                else:
                    frags.append((x1, y1, x0 - x1, y0 - y1, p))
                marks.add(frags[-1][1])
                marks.add(frags[-1][1] + frags[-1][3])
        for h, v in jnet.items():
            if v != 0:
                marks.add(h)
    return frags, sorted(marks)


def crossings(parts):
    frags, marks = kept(parts)
    total = 0
    for i in range(len(marks) - 1):
        ya, yb = marks[i], marks[i + 1]
        H = yb - ya
        act = [f for f in frags if f[1] <= ya and f[1] + f[3] >= yb]
        a = [f[0] * f[3] + (ya - f[1]) * f[2] for f in act]
        for p in range(len(act)):
            for q in range(p + 1, len(act)):
                top = a[p] * act[q][3] - a[q] * act[p][3]
                end = (a[p] + act[p][2] * H) * act[q][3] - (a[q] + act[q][2] * H) * act[p][3]
                if (top < 0 and end > 0) or (top > 0 and end < 0):
                    total += 1
    return total, len(frags)


def cells():
    out = []
    rng = random.Random(20261023)

    def add(name, parts, wtops, mode=0, even_odd=False, complement=False, nparts=None):
        out.append((name, [list(p) for p in parts], list(wtops), mode, even_odd, complement))

    # closed forms
    add("two verticals", [[(30, 0, 30, 64), (40, 0, 40, 64)]], [0])
    add("two verticals, a winding of 3 from the left", [[(30, 0, 30, 64), (40, 0, 40, 64)]], [3])
    add("a cross at a lattice point", [[(0, 0, 64, 64), (64, 0, 0, 64)]], [0])
    add("a cross at a non-lattice point", [[(0, 0, 63, 64), (64, 0, 1, 64)]], [0])
    add("three chords meeting at rational points", [[(0, 0, 21, 64), (10, 0, 0, 64), (3, 0, 38, 64)]], [1])
    add("three nearly parallel chords", [[(0, 0, 64, 64), (1, 0, 64, 64), (0, 0, 63, 64)]], [0])
    add("a wedge from one vertex", [[(32, 0, 0, 64), (32, 0, 64, 64), (32, 0, 32, 64)]], [0])
    add("a pivot of opposite pieces", [[(10, 10, 50, 50), (50, 10, 10, 50), (30, 0, 30, 64), (0, 30, 64, 30)]], [0])
    add("coincident copies three deep", [[(20, 0, 44, 64)] * 3 + [(50, 64, 10, 0)]], [0])
    add("coincident copies nineteen deep", [[(20, 0, 44, 64)] * 19 + [(50, 64, 10, 0)]], [0])
    add("a seam and a partial overlap of opposite pieces", [[(10, 0, 30, 64), (30, 32, 20, 0), (5, 7, 33, 50), (33, 50, 5, 7)]], [0])
    add("a contour on the left side", [[(0, 5, 20, 5), (20, 5, 20, 40), (20, 40, 0, 40), (0, 40, 0, 5)]], [0])
    add("a vertical piece on the left side", [[(0, 0, 0, 64), (30, 0, 50, 64)]], [1])
    # chords
    for n, seed in ((14, 1), (30, 2), (60, 3)):
        pieces = []
        pool = [(a, b) for a in range(65) for b in range(65)]
        rng2 = random.Random(seed)
        rng2.shuffle(pool)
        for (a, b) in pool[:n]:
            pieces.append((a, 0, b, 64) if rng2.random() < 0.5 else (b, 64, a, 0))
        add("chords %d" % n, [pieces], [rng2.randrange(-2, 3)])
    # fans
    def fan(py_num, py_den, px_num, px_den, alternate):
        ps, count = [], 0
        for xa in range(65):
            for xb in range(65):
                if (xa * 64 * py_den + (xb - xa) * py_num) * px_den == px_num * 64 * py_den:
                    up = alternate and (count % 2 == 1)
                    count += 1
                    ps.append((xb, 64, xa, 0) if up else (xa, 0, xb, 64))
        return ps
    add("fan through (32, 32)", [fan(32, 1, 32, 1, False)], [0])
    add("fan through (32, 32), alternating", [fan(32, 1, 32, 1, True)], [0])
    add("fan through (31.5, 32), alternating", [fan(32, 1, 63, 2, True)], [0])
    add("fan through (32.25, 16), alternating", [fan(16, 1, 129, 4, True)], [0])
    add("fan through (33.33, 21.33), alternating", [fan(64, 3, 100, 3, True)], [0])
    # random
    for seed in range(1, 9):
        r = random.Random(seed * 101)
        ps = []
        for _ in range(8 + seed * 3):
            x0, y0, x1, y1 = r.randrange(65), r.randrange(65), r.randrange(65), r.randrange(65)
            if r.random() < 0.08:
                x0 = 0
            if (x0, y0) != (x1, y1):
                ps.append((x0, y0, x1, y1))
        add("random cell %d" % seed, [ps], [r.randrange(-2, 3)])
    # exact ties: small cells whose doubled area is a half-integer (found by search; they round up)
    found = 0
    for seed in range(1, 4000):
        r = random.Random(seed * 7919)
        ps = []
        for _ in range(4 + seed % 5):
            x0, y0, x1, y1 = r.randrange(65), r.randrange(65), r.randrange(65), r.randrange(65)
            if (x0, y0) != (x1, y1):
                ps.append((x0, y0, x1, y1))
        wt = [r.randrange(-1, 2)]
        a_left, dense = consistent([ps], wt, [EPS_BODY])
        area = area2([ps], wt, [EPS_BODY], 0, False, False, limit=10**7)
        if area is None:
            continue
        q = F(dense[0]) - area
        if (q + F(1, 2)).denominator == 1:
            add("an exact tie %d" % (found + 1), [ps], wt)
            found += 1
            if found == 4:
                break
    assert found == 4
    # three parts, both rules, the complement
    for seed in range(1, 7):
        r = random.Random(seed * 977)
        parts = []
        for _ in range(3):
            ps = []
            for _ in range(6):
                x0, y0, x1, y1 = r.randrange(65), r.randrange(65), r.randrange(65), r.randrange(65)
                if (x0, y0) != (x1, y1):
                    ps.append((x0, y0, x1, y1))
            parts.append(ps)
        add("three parts %d" % seed, parts, [r.randrange(-1, 2) for _ in range(3)], mode=1, even_odd=bool(seed & 1), complement=bool(seed & 2))
    for seed in range(1, 4):
        r = random.Random(seed * 31)
        ps = [(r.randrange(65), r.randrange(65), r.randrange(65), r.randrange(65)) for _ in range(10)]
        ps = [p for p in ps if (p[0], p[1]) != (p[2], p[3])]
        add("the clip alone %d" % seed, [ps], [r.randrange(-1, 2)], mode=2, even_odd=bool(seed & 1))
    return out


def render():
    lines = []
    w = lines.append
    w("/*")
    w(" * SPDX-License-Identifier: LGPL-3.0-only")
    w(" *")
    w(" * Copyright (C) 2026 Corey Pennycuff")
    w(" *")
    w(" * This file is part of Ghoti.io Shape CPU.")
    w(" */")
    w("")
    w("/*")
    w(" * GENERATED by tools/gen-dense-cells.py: the committed cells of the exact union step with their exact answers in rational")
    w(" * arithmetic (strokes story S4c).  Do not edit; `make check-vectors` fails if this is not what the generator writes.")
    w(" */")
    w("")
    w("#ifndef GHOTI_IO_GSHCPU_TEST_DENSE_VECTORS_H")
    w("#define GHOTI_IO_GSHCPU_TEST_DENSE_VECTORS_H")
    w("")
    w("#include <stddef.h>")
    w("#include <stdint.h>")
    w("")
    w("typedef struct DV_Vec {")
    w("  const char * name;")
    w("  int mode;                 /* GSHCPU_EXACT_OVERLAP 0, UNION3 1, REGION 2 */")
    w("  int parts;")
    w("  int even_odd;")
    w("  int complement;")
    w("  int count[3];             /* pieces of each part */")
    w("  const int8_t * pieces[3]; /* x0 y0 x1 y1 for each */")
    w("  int wtop[3];")
    w("  int64_t out;              /* what the step gives: the area, or for a part's own overlap the shared area, rounded half up */")
    w("  int tie;                  /* the exact value is a tie (half a unit), which rounds up */")
    w("  const char * exact;       /* the exact doubled area of the region, as numerator/denominator */")
    w("  uint64_t crossings;       /* UC19 */")
    w("  uint32_t distinct;        /* fragments once a piece and its reverse cancel (UC13) */")
    w("} DV_Vec;")
    w("")
    vecs = []
    for idx, (name, parts, wtops, mode, even_odd, complement) in enumerate(cells()):
        nparts = len(parts)
        eps = [EPS_BODY, EPS_BODY, EPS_CLIP] if mode == 1 else ([EPS_CLIP] if mode == 2 else [EPS_BODY])
        wt = list(wtops) + [0] * (3 - len(wtops))
        eps3 = eps + [0] * (3 - len(eps))
        a_left, dense = consistent(parts, wt[:nparts], eps)
        area = area2(parts, wt[:nparts], eps, mode, even_odd, complement, limit=10**9)
        assert area is not None, name
        q = (F(dense[0]) - area) if mode == 0 else area
        tie = (q + F(1, 2)).denominator == 1
        frac = (q + F(1, 2)) - ((q + F(1, 2)).numerator // (q + F(1, 2)).denominator)
        if not tie and (frac < F(1, 10**6) or frac > 1 - F(1, 10**6)):
            print("skipping %s: its exact value %s lies too near a tie" % (name, float(q)), file=sys.stderr)
            continue
        out = (q + F(1, 2)).numerator // (q + F(1, 2)).denominator
        k, distinct = crossings(parts)
        for p in range(nparts):
            body = ", ".join(str(v) for piece in parts[p] for v in piece) or "0"
            w("static const int8_t dv_%d_p%d[] = {%s};" % (idx, p, body))
        vecs.append((idx, name, nparts, mode, even_odd, complement, parts, wt, int(out), int(tie), "%d/%d" % (area.numerator, area.denominator), k, distinct))
    w("")
    w("static const DV_Vec DV_VECTORS[] = {")
    for (idx, name, nparts, mode, even_odd, complement, parts, wt, out, tie, exact, k, distinct) in vecs:
        counts = [len(parts[p]) if p < nparts else 0 for p in range(3)]
        ptrs = ["dv_%d_p%d" % (idx, p) if p < nparts else "NULL" for p in range(3)]
        w('    {"%s", %d, %d, %d, %d, {%d, %d, %d}, {%s}, {%d, %d, %d}, %dLL, %d, "%s", %dULL, %du},' % (
            name, mode, nparts, int(even_odd), int(complement), counts[0], counts[1], counts[2], ", ".join(ptrs), wt[0], wt[1], wt[2], out, tie, exact, k, distinct))
    w("};")
    w("")
    w("#define DV_COUNT (sizeof DV_VECTORS / sizeof DV_VECTORS[0])")
    w("")
    w("#endif /* GHOTI_IO_GSHCPU_TEST_DENSE_VECTORS_H */")
    return "\n".join(lines) + "\n"


def main():
    text = render()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            print("tests/dense_vectors.h is not what tools/gen-dense-cells.py writes", file=sys.stderr)
            return 1
        print("dense vectors: tests/dense_vectors.h is what the generator writes")
        return 0
    OUT.write_text(text)
    print("wrote", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
