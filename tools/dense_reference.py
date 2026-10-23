# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Shape CPU.
#
# Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""The exact area of one cell of the exact union step, in rational arithmetic (strokes story S4c).

An instrument that knows nothing of the sweep in src/render/exact.c: the cell is cut into slabs at the heights of its pieces'
ends and of the steps of the winding on its left side, each slab is cut at the exact depth of every meeting of two fragments
(all of the pairs, as fractions), the order of the fragments is taken at the middle of each piece of the slab, the windings
are summed in that order, and the width of every gap in the region is integrated from the exact widths at the piece's ends.
Everything is a ``fractions.Fraction``: no rounding anywhere, so the answer is the area itself.  It costs
``O((K + 1) n log n)`` fractions for a slab of ``n`` fragments and ``K`` meetings, which is why it evaluates cells of up to a
few hundred pieces.

A cell is its parts' pieces ``(x0, y0, x1, y1)`` in the cell's frame (0..64, y down, in the direction of the contour), the
winding ``wtop`` at the top of each part's left side (``W``, an edge directed down counting +1) and the sign ``eps`` of
each part (``w = eps W``); ``mode`` 0 is a part's own overlap (the region is ``w != 0``), 2 the clip's region alone (non-zero,
or odd when ``even_odd``), 1 the three parts of UC8 (``{w_b != 0} U ({w_a != 0} & F')``, ``F'`` the clip's region or its
complement).
"""
from fractions import Fraction as F
import math


def predicate(mode, even_odd, complement, w):
    if mode == 0:
        return w[0] != 0
    if mode == 2:
        return (w[0] & 1) != 0 if even_odd else w[0] != 0
    if w[0] != 0:
        return True
    if w[1] == 0:
        return False
    inside = (w[2] & 1) != 0 if even_odd else w[2] != 0
    return (not inside) if complement else inside


def wtop_of(pieces, a_left):
    """The winding at the top of the left side from the cover summed there: ``64 W_top = A_left - sum J(h) (64 - h)``."""
    jnet = {}
    for (x0, y0, x1, y1) in pieces:
        if x0 == 0:
            jnet[y0] = jnet.get(y0, 0) - 1
        if x1 == 0:
            jnet[y1] = jnet.get(y1, 0) + 1
    rem = a_left - sum(v * (64 - h) for h, v in jnet.items())
    assert rem % 64 == 0, "the cover to the left is not a whole winding"
    return rem // 64


def area2(parts, wtops, eps, mode=0, even_odd=False, complement=False, limit=3_000_000):
    """The doubled area of the cell's region as a Fraction, or None when the work estimate passes ``limit``."""
    frags = []
    jsteps = [dict() for _ in parts]
    for p, pieces in enumerate(parts):
        for (x0, y0, x1, y1) in pieces:
            if x0 == 0:
                jsteps[p][y0] = jsteps[p].get(y0, 0) - 1
            if x1 == 0:
                jsteps[p][y1] = jsteps[p].get(y1, 0) + 1
            if y0 != y1:
                if y1 > y0:
                    frags.append((x0, y0, x1 - x0, y1 - y0, 1, p))
                else:
                    frags.append((x1, y1, x0 - x1, y0 - y1, -1, p))
    marks = {0, 64}
    for (px, py, dx, dy, s, p) in frags:
        marks.add(py)
        marks.add(py + dy)
    for js in jsteps:
        for h, v in js.items():
            if v != 0:
                marks.add(h)
    marks = sorted(marks)
    total = F(0)
    estimate = 0
    for i in range(len(marks) - 1):
        ya, yb = marks[i], marks[i + 1]
        H = yb - ya
        act = [f for f in frags if f[1] <= ya and f[1] + f[3] >= yb]
        estimate += len(act) ** 2
        if estimate > limit:
            return None
    for i in range(len(marks) - 1):
        ya, yb = marks[i], marks[i + 1]
        H = yb - ya
        act = [f for f in frags if f[1] <= ya and f[1] + f[3] >= yb]
        n = len(act)
        a0 = [f[0] * f[3] + (ya - f[1]) * f[2] for f in act]
        times = {F(0), F(H)}
        for a in range(n):
            for b in range(a + 1, n):
                da = act[a][2] * act[b][3] - act[b][2] * act[a][3]
                if da == 0:
                    continue
                u = F(a0[b] * act[a][3] - a0[a] * act[b][3], da)
                if 0 < u < H:
                    times.add(u)
        ts = sorted(times)
        estimate += len(ts) * n
        if estimate > limit * 4:
            return None
        base = []
        for p in range(len(parts)):
            base.append(wtops[p] + sum(v for h, v in jsteps[p].items() if h <= ya))

        def xat(j, t):
            return (a0[j] + act[j][2] * t) / act[j][3]

        for k in range(len(ts) - 1):
            t0, t1 = ts[k], ts[k + 1]
            mid = (t0 + t1) / 2
            order = sorted(range(n), key=lambda j: (xat(j, mid), j))
            w = [eps[p] * base[p] if p < len(parts) else 0 for p in range(3)]
            xs0 = [xat(j, t0) for j in order]
            xs1 = [xat(j, t1) for j in order]
            acc = F(0)
            for g in range(n + 1):
                if g > 0:
                    f = act[order[g - 1]]
                    w[f[5]] += eps[f[5]] * f[4]
                if predicate(mode, even_odd, complement, w):
                    l0 = xs0[g - 1] if g > 0 else F(0)
                    l1 = xs1[g - 1] if g > 0 else F(0)
                    r0 = xs0[g] if g < n else F(64)
                    r1 = xs1[g] if g < n else F(64)
                    acc += ((r0 - l0) + (r1 - l1)) / 2
            total += acc * (t1 - t0)
    return 2 * total


def rounded(x):
    """Round half up: the convention the step rounds a doubled area to."""
    return math.floor(x + F(1, 2))
