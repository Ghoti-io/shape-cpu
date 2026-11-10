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
"""The independent reference of coverage by the centers of the pixels (shape-scene's list.h, AA1 to AA6; both ties of AA3).

Writes tests/center_vectors.h, or with --check compares the file on disk with what this would write; `--selftest` checks the
reference against itself, and `--selftest --plant NAME` must fail for each of the planted defects (`make check-center` runs all).

There are two rules, one for each tie (AA3): the **top-left** rule (`GSHSCENE_ANTIALIAS_NONE_TOP_LEFT`), written below, and the
**bottom-right** rule (`GSHSCENE_ANTIALIAS_NONE`), its mirror.  Every case carries the pixels each covers.  The bottom-right rule is stated
the same ways: an edge straddles `c` when exactly one of `p.y < c.y` and `q.y < c.y` holds and counts when `x_e >= c.x`; it is the
crossing number of the point `c - (e, e^2)`; and by hand (a square on centers covers its right and bottom edges and its bottom-right
corner and not its top and left ones).

The top-left rule (AA2, AA3) in exact rationals (`fractions.Fraction`), sharing no code with the C library: in device space, in units of 1/64
of a pixel, the pixel `(x, y)` has the center `c = (64 x + 32, 64 y + 32)`.  An edge from `p` to `q` *straddles* `c` when exactly one
of `p.y <= c.y` and `q.y <= c.y` holds, meets the line `y = c.y` at `x_e = p.x + (c.y - p.y)(q.x - p.x)/(q.y - p.y)` and *counts*
when `x_e > c.x`, as `+1` when `q.y > p.y`, else `-1`.  The winding is the sum, a center is inside when it is not zero (nonzero rule)
or odd (even-odd), and the coverage is the whole pixel inside and nothing outside.

It is stated twice here and the two must agree on every pixel of every case: `winding_formula` is the text above, and
`winding_perturbed` asks the question the rule is the limit of, the textbook crossing number of the point `c + (e, e^2)` for a
rational `e` far smaller than any gap between rationals of these denominators (`e = 2^-40`).  A third statement is by hand: the
tie cases below name what the top-left rule says of a square, a diamond, a horizontal and a vertical edge and each corner, and the
reference must give those answers (a rule with its ties reversed agrees with itself and with nothing else).

The cases (tests/center_vectors.h) are device polygons in units, the pixels each covers by each rule, and the same polygons as a
path under an exact map (the identity, a translation, a doubling, a quarter turn, a reflection) so that the sink's rounding of a
point to the lattice changes nothing and the model-space path and the map give the device polygon exactly; and strokes of straight
lines and axis-aligned squares (butt and square caps, miter joins, the three alignments) whose outlines are exact rectangles, so that
the set a stroke covers is stated here as a union of half-open rectangles without the library's expansion.

    python3 tools/gen-center-vectors.py            # write tests/center_vectors.h
    python3 tools/gen-center-vectors.py --check    # fail if it differs
    python3 tools/gen-center-vectors.py --selftest [--plant NAME]
"""

import random
import sys
from fractions import Fraction as F
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "tests" / "center_vectors.h"

W = H = 16  # the raster of every case, in pixels

NONZERO, EVEN_ODD = 0, 1

# What a planted defect changes (the selftest and the generator's check must each fail for every one).
PLANT = None
PLANTS = ("top-left-reversed", "tie-inside", "centre-y-tie", "even-odd-as-nonzero", "edge-direction-ignored",
          "closing-edge-dropped", "winding-clamped", "half-open-swapped", "bottom-right-as-top-left", "bottom-right-x-tie-excluded",
          "bottom-right-y-tie-excluded", "bottom-right-half-open-swapped")


# --- the rule ---------------------------------------------------------------------------------------------------------------
def edges_of(contours):
    """Every edge of every contour, the closing line of each included."""
    out = []
    for pts in contours:
        n = len(pts)
        for k in range(n):
            if PLANT == "closing-edge-dropped" and k == 0:
                continue
            out.append((pts[k - 1], pts[k]))
    return out


def winding_formula(edges, cx, cy, top_left=True):
    """AA2 literally, with AA3's tie of the mode: the top-left rule, or the bottom-right one."""
    if PLANT == "centre-y-tie":
        cy = cy + F(1, 2)  # tested against a center half a pixel low
    if PLANT == "bottom-right-as-top-left":
        top_left = True
    w = 0
    for (px, py), (qx, qy) in edges:
        if top_left:
            if PLANT == "half-open-swapped":
                straddles = (py < cy) != (qy < cy)
            else:
                straddles = (py <= cy) != (qy <= cy)
        else:
            if PLANT == "bottom-right-y-tie-excluded":
                straddles = (py <= cy) != (qy <= cy)
            elif PLANT == "bottom-right-half-open-swapped":
                straddles = (py <= cy) != (qy <= cy)
            else:
                straddles = (py < cy) != (qy < cy)
        if not straddles:
            continue
        xe = F(px) + F(cy - py) * F(qx - px) / F(qy - py)
        if not top_left:
            counts = xe > cx if PLANT == "bottom-right-x-tie-excluded" else xe >= cx
        elif PLANT == "top-left-reversed":
            counts = xe >= cx
        elif PLANT == "tie-inside":
            counts = xe >= cx or xe == cx
        else:
            counts = xe > cx
        if counts:
            if PLANT == "edge-direction-ignored":
                w += 1
            else:
                w += 1 if qy > py else -1
    if PLANT == "winding-clamped":
        w = max(-1, min(1, w))
    return w


E = F(1, 2 ** 40)


def winding_perturbed(edges, cx, cy, top_left=True):
    """The crossing number of the point c + (e, e^2) (top-left) or c - (e, e^2) (bottom-right), which is on no edge: the question AA3
    says the rule is."""
    sign = 1 if top_left else -1
    ptx, pty = F(cx) + sign * E, F(cy) + sign * E * E
    w = 0
    for (px, py), (qx, qy) in edges:
        if (py <= pty) == (qy <= pty):
            continue
        xe = F(px) + (pty - py) * F(qx - px) / F(qy - py)
        if xe > ptx:
            w += 1 if qy > py else -1
    return w


def inside(w, rule):
    if PLANT == "even-odd-as-nonzero":
        return w != 0
    return (w & 1) != 0 if rule == EVEN_ODD else w != 0


def covered(contours, rule, formula=winding_formula, top_left=True):
    edges = edges_of(contours)
    rows = []
    for y in range(H):
        row = ""
        for x in range(W):
            w = formula(edges, 64 * x + 32, 64 * y + 32, top_left)
            row += "1" if inside(w, rule) else "0"
        rows.append(row)
    return "".join(rows)


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def half_open_rect(x0, y0, x1, y1, top_left=True):
    """The pixels whose centers a rectangle of integer-unit corners holds: the half-open intervals the top-left rule makes (`[x0, x1)`),
    or the bottom-right rule (`(x0, x1]`)."""
    cells = set()
    for y in range(H):
        for x in range(W):
            cx, cy = 64 * x + 32, 64 * y + 32
            if (PLANT == "half-open-swapped" and top_left) or (not top_left and PLANT != "bottom-right-half-open-swapped"):
                inx = x0 < cx <= x1
                iny = y0 < cy <= y1
            else:
                inx = x0 <= cx < x1
                iny = y0 <= cy < y1
            if inx and iny:
                cells.add((x, y))
    return cells


def grid(cells):
    return "".join("1" if (x, y) in cells else "0" for y in range(H) for x in range(W))


# --- the tie cases, by hand ---------------------------------------------------------------------------------------------------
# Each is (name, contours, rule, pixels covered by the top-left rule, pixels covered by the bottom-right rule): the answers the two
# rules give, written without the code above.
def hand_cases():
    out = []
    # A square whose corners are the centers of pixels (1,1), (4,1), (4,3) and (1,3): under the top-left rule its top and left edges and its
    # top-left corner are inside and its right and bottom edges and the other three corners outside; under the bottom-right rule it is the
    # other way round.
    sq = [rect(96, 96, 288, 224)]
    want_tl = {(x, y) for x in (1, 2, 3) for y in (1, 2)}
    want_br = {(x, y) for x in (2, 3, 4) for y in (2, 3)}
    out.append(("square on centers", sq, NONZERO, want_tl, want_br))
    # The same wound the other way, and under even-odd: the rule does not care which way a contour goes.
    out.append(("square on centers reversed", [list(reversed(sq[0]))], NONZERO, want_tl, want_br))
    out.append(("square on centers even-odd", sq, EVEN_ODD, want_tl, want_br))
    # A square with whole-pixel corners covers exactly its pixels, under either rule.
    whole = {(x, y) for x in (2, 3, 4) for y in (1, 2, 3)}
    out.append(("square of whole pixels", [rect(128, 64, 320, 256)], NONZERO, whole, whole))
    # Two squares that share an edge on the centers of a column: no pixel in both, none in neither, under either rule.
    out.append(("abutting squares, left", [rect(96, 96, 224, 224)], NONZERO, {(x, y) for x in (1, 2) for y in (1, 2)},
                {(x, y) for x in (2, 3) for y in (2, 3)}))
    out.append(("abutting squares, right", [rect(224, 96, 352, 224)], NONZERO, {(x, y) for x in (3, 4) for y in (1, 2)},
                {(x, y) for x in (4, 5) for y in (2, 3)}))
    # A diamond whose vertices are the centers of pixels (3,1), (5,3), (3,5) and (1,3): the point c + (e, e^2) is inside it at the left
    # vertex (the wedge opens to the right) and not at the other three, and on the two edges that have the interior to their right.  The
    # diamond is its own half turn about the pixel (3,3), and the half turn takes one rule to the other, so the bottom-right answer is the
    # top-left one turned: (x, y) to (6 - x, 6 - y).
    d = [(224, 96), (352, 224), (224, 352), (96, 224)]
    diamond = {(1, 3), (2, 2), (2, 3), (2, 4), (3, 2), (3, 3), (3, 4), (4, 3)}
    out.append(("diamond", [d], NONZERO, diamond, {(6 - x, 6 - y) for x, y in diamond}))
    # A triangle with its corners on the centers of pixels (0,0), (4,4) and (0,4).  Top-left: the diagonal through three centers is a
    # right edge (the interior is below it, to its left as the point moves right), the left edge is inside, the bottom edge and the
    # corners are not.  Bottom-right: the left edge and the corners (0,0) and (0,4) are outside; the diagonal, the bottom edge and the
    # corner (4,4) are inside.
    out.append(("triangle on centers", [[(32, 32), (288, 288), (32, 288)]], NONZERO,
                {(0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)},
                {(1, 1), (1, 2), (2, 2), (1, 3), (2, 3), (3, 3), (1, 4), (2, 4), (3, 4), (4, 4)}))
    # Two squares wound the same way, one inside the other: the inner one is a hole under even-odd and not under nonzero.
    ring_tl = half_open_rect(96, 96, 800, 800) - half_open_rect(224, 224, 672, 672)
    ring_br = half_open_rect(96, 96, 800, 800, False) - half_open_rect(224, 224, 672, 672, False)
    out.append(("nested, same way, even-odd", [rect(96, 96, 800, 800), rect(224, 224, 672, 672)], EVEN_ODD, ring_tl, ring_br))
    out.append(("nested, same way, nonzero", [rect(96, 96, 800, 800), rect(224, 224, 672, 672)], NONZERO, half_open_rect(96, 96, 800, 800),
                half_open_rect(96, 96, 800, 800, False)))
    out.append(("nested, opposite ways, nonzero", [rect(96, 96, 800, 800), list(reversed(rect(224, 224, 672, 672)))], NONZERO, ring_tl, ring_br))
    return out


# --- the cases -----------------------------------------------------------------------------------------------------------------
MAPS = [
    ("identity", lambda x, y: (x, y), lambda x, y: (x, y), (1, 0, 0, 1, 0, 0), 1),
    ("translate", lambda x, y: (x + 32, y - 16), lambda x, y: (x - 32, y + 16), (1, 0, 0, 1, F(1, 2), F(-1, 4)), 1),
    ("double", lambda x, y: (2 * x, 2 * y), lambda x, y: (x // 2, y // 2), (2, 0, 0, 2, 0, 0), 2),
    ("quarter turn", lambda x, y: (-y + 1024, x), lambda x, y: (y, 1024 - x), (0, 1, -1, 0, 16, 0), 1),
    ("reflect", lambda x, y: (1024 - x, y), lambda x, y: (1024 - x, y), (-1, 0, 0, 1, 16, 0), 1),
]


def random_polygon(rng, steps, n):
    """A polygon of @p n points on a lattice of @p steps units (a coarse one makes ties common)."""
    lo, hi = -64, 1088
    pts = []
    for _ in range(n):
        x = rng.randrange(lo // steps, hi // steps + 1) * steps
        y = rng.randrange(lo // steps, hi // steps + 1) * steps
        pts.append((x, y))
    return pts


def build_cases():
    rng = random.Random(0x5EED5EED)
    cases = []

    def add(name, contours, rule, map_index=0, check=None, check_br=None):
        want = covered(contours, rule)
        other = covered(contours, rule, winding_perturbed)
        if want != other:
            raise SystemExit("the two statements of the top-left rule disagree on case %r" % name)
        want_br = covered(contours, rule, top_left=False)
        other_br = covered(contours, rule, winding_perturbed, top_left=False)
        if want_br != other_br:
            raise SystemExit("the two statements of the bottom-right rule disagree on case %r" % name)
        if check is not None and want != grid(check):
            raise SystemExit("the top-left rule disagrees with the hand-stated answer of case %r" % name)
        if check_br is not None and want_br != grid(check_br):
            raise SystemExit("the bottom-right rule disagrees with the hand-stated answer of case %r" % name)
        cases.append(dict(name=name, contours=contours, rule=rule, map=map_index, expected=want, expected_br=want_br))

    for name, contours, rule, cells, cells_br in hand_cases():
        add(name, contours, rule, 0, cells, cells_br)
    # Squares at the centers and at whole pixels, in every map.
    for mi, m in enumerate(MAPS):
        step = 2 if m[4] == 2 else 1
        for corner in (32, 64, 96):
            add("square %d %s" % (corner, m[0]), [rect(64 * 1 + corner - 32, 64 + corner - 32, 64 * 4 + corner - 32, 64 * 3 + corner - 32)], NONZERO, mi)
    # A figure eight, a star under both rules, and a ring with a hole (nested and opposite).
    star = [(512, 64), (704, 896), (96, 352), (928, 352), (320, 896)]
    for rule in (NONZERO, EVEN_ODD):
        add("star %d" % rule, [star], rule)
        add("star on centers %d" % rule, [[(x - x % 64 + 32, y - y % 64 + 32) for x, y in star]], rule)
    add("ring", [rect(96, 96, 800, 800), list(reversed(rect(224, 224, 672, 672)))], NONZERO)
    add("ring same way, nonzero", [rect(96, 96, 800, 800), rect(224, 224, 672, 672)], NONZERO)
    add("ring same way, even-odd", [rect(96, 96, 800, 800), rect(224, 224, 672, 672)], EVEN_ODD)
    add("zero area", [[(96, 96), (480, 96), (96, 96)]], NONZERO)
    add("one point", [[(96, 96)]], NONZERO)
    add("two contours that share an edge", [rect(96, 96, 288, 288), rect(288, 96, 480, 288)], NONZERO)
    add("two contours that share an edge, even-odd", [rect(96, 96, 288, 288), rect(288, 96, 480, 288)], EVEN_ODD)
    # Random polygons: coarse lattices (every point a tie), a fine one, and any integer; one to three contours; both rules; every map.
    n = 0
    for steps in (64, 32, 16, 8, 1):
        for k in range(40 if steps in (32, 64) else 24):
            mi = rng.randrange(len(MAPS))
            step = steps * (2 if MAPS[mi][4] == 2 else 1)
            ncont = rng.choice((1, 1, 1, 2, 3))
            contours = [random_polygon(rng, step, rng.randrange(3, 9)) for _ in range(ncont)]
            if MAPS[mi][4] == 2:
                contours = [[(x // 2 * 2, y // 2 * 2) for x, y in c] for c in contours]
            add("random %d.%d" % (steps, k), contours, rng.randrange(2), mi)
            n += 1
    return cases


# --- strokes: outlines that are exact rectangles --------------------------------------------------------------------------------
BUTT, SQUARE = 0, 1
ALIGN_CENTER, ALIGN_INSIDE, ALIGN_OUTSIDE = 0, 1, 2


def stroke_cases():
    """Straight lines and an axis-aligned square, stroked with butt or square caps and miter joins in whole units, so that every outline
    is a union of rectangles and the pixels it covers are the union of the half-open rectangles' pixels."""
    out = []

    def line(name, x0, y0, x1, y1, width, cap):
        # a horizontal or vertical line of an even width in units
        hw = width // 2
        ext = hw if cap == SQUARE else 0
        cells = {}
        for tl in (True, False):
            if y0 == y1:
                cells[tl] = half_open_rect(min(x0, x1) - ext, y0 - hw, max(x0, x1) + ext, y0 + hw, tl)
            else:
                cells[tl] = half_open_rect(x0 - hw, min(y0, y1) - ext, x0 + hw, max(y0, y1) + ext, tl)
        out.append(dict(name=name, points=[(x0, y0), (x1, y1)], closed=0, width=width, cap=cap, align=ALIGN_CENTER,
                        expected=grid(cells[True]), expected_br=grid(cells[False])))

    line("butt horizontal on centers", 96, 96, 608, 96, 64, BUTT)
    line("butt horizontal, edges on centers", 96, 160, 608, 160, 128, BUTT)
    line("square caps horizontal", 160, 224, 608, 224, 64, SQUARE)
    line("butt vertical", 288, 96, 288, 800, 64, BUTT)
    line("square vertical", 416, 160, 416, 800, 128, SQUARE)
    line("thin on a center line", 96, 352, 800, 352, 32, BUTT)
    line("thin off the centers", 96, 320, 800, 320, 32, BUTT)

    def square(name, x0, y0, x1, y1, width, align):
        hw = width // 2
        if align == ALIGN_CENTER:
            outer = (x0 - hw, y0 - hw, x1 + hw, y1 + hw)
            inner = (x0 + hw, y0 + hw, x1 - hw, y1 - hw)
        elif align == ALIGN_INSIDE:
            outer = (x0, y0, x1, y1)
            inner = (x0 + width, y0 + width, x1 - width, y1 - width)
        else:
            outer = (x0 - width, y0 - width, x1 + width, y1 + width)
            inner = (x0, y0, x1, y1)
        cells = half_open_rect(*outer) - half_open_rect(*inner)
        cells_br = half_open_rect(*outer, top_left=False) - half_open_rect(*inner, top_left=False)
        out.append(dict(name=name, points=[(x0, y0), (x1, y0), (x1, y1), (x0, y1)], closed=1, width=width, cap=BUTT,
                        align=align, expected=grid(cells), expected_br=grid(cells_br)))

    for name, x0, y0, x1, y1, width in (("square on centers", 96, 96, 800, 800, 64), ("square on edges", 128, 128, 832, 704, 128),
                                         ("square off", 112, 144, 784, 720, 48)):
        square(name + ", centered", x0, y0, x1, y1, width, ALIGN_CENTER)
        square(name + ", inside", x0, y0, x1, y1, width, ALIGN_INSIDE)
        square(name + ", outside", x0, y0, x1, y1, width, ALIGN_OUTSIDE)
    return out


# --- the header ------------------------------------------------------------------------------------------------------------------------
def cxx_bytes(s):
    return '"%s"' % s


def render():
    cases = build_cases()
    strokes = stroke_cases()
    o = []
    o.append("// Generated by tools/gen-center-vectors.py from the rule of shape-scene's list.h (AA2, AA3).\n")
    o.append("// Do not edit; run the generator.  Independent of the library's code: exact rationals.\n")
    o.append("#ifndef GHOTI_IO_GSHCPU_TEST_CENTER_VECTORS_H\n#define GHOTI_IO_GSHCPU_TEST_CENTER_VECTORS_H\n\n")
    o.append("#include <cstdint>\n\nnamespace center_vectors {\n\n")
    o.append("static const int kWidth = %d;\nstatic const int kHeight = %d;\n\n" % (W, H))
    o.append("// The map of a case takes the model-space path to the device polygon exactly: x' = a x + c y + e, y' = b x + d y + f in pixels.\n")
    o.append("struct Map {\n  const char * name;\n  double a, b, c, d, e, f;\n};\n")
    o.append("static const Map kMaps[] = {\n")
    for name, _, _, (a, b, c, d, e, f), _ in MAPS:
        o.append("    {\"%s\", %r, %r, %r, %r, %r, %r},\n" % (name, float(a), float(b), float(c), float(d), float(e), float(f)))
    o.append("};\n\n")
    o.append("// Device polygons in 1/64 pixel (the contours back to back, `counts` points each), the rule (0 nonzero, 1 even-odd), the map and\n")
    o.append("// the model-space points (units of 1/64 in the model: the polygon under the inverse of the map), then the pixels covered.\n")
    o.append("struct Case {\n  const char * name;\n  int rule;\n  int map;\n  int contours;\n  const int * counts;\n  const int * device;\n"
             "  const int * model;\n  const char * covered;\n  const char * covered_bottom_right;\n};\n\n")
    for i, c in enumerate(cases):
        mi = c["map"]
        inverse = MAPS[mi][2]
        counts = [len(p) for p in c["contours"]]
        dev = [v for p in c["contours"] for pt in p for v in pt]
        mod = []
        for p in c["contours"]:
            for pt in p:
                mx, my = inverse(*pt)
                mod += [mx, my]
        o.append("static const int kCounts%d[] = {%s};\nstatic const int kDevice%d[] = {%s};\nstatic const int kModel%d[] = {%s};\n" % (
            i, ", ".join(map(str, counts)), i, ", ".join(map(str, dev)), i, ", ".join(map(str, mod))))
    o.append("static const Case kCases[] = {\n")
    for i, c in enumerate(cases):
        o.append("    {\"%s\", %d, %d, %d, kCounts%d, kDevice%d, kModel%d,\n     %s,\n     %s},\n" % (
            c["name"], c["rule"], c["map"], len(c["contours"]), i, i, i, cxx_bytes(c["expected"]), cxx_bytes(c["expected_br"])))
    o.append("};\nstatic const int kCaseCount = %d;\n\n" % len(cases))
    o.append("// Strokes: the points of the centerline in 1/64 pixel, whether it is closed, the width in 1/64 pixel, the cap (0 butt, 1 square), the\n")
    o.append("// alignment (0 center, 1 inside, 2 outside), joined by miters; and the pixels covered.\n")
    o.append("struct Stroke {\n  const char * name;\n  int count;\n  const int * points;\n  int closed;\n  int width;\n  int cap;\n  int align;\n  const char * covered;\n  const char * covered_bottom_right;\n};\n\n")
    for i, s in enumerate(strokes):
        o.append("static const int kStrokePoints%d[] = {%s};\n" % (i, ", ".join(str(v) for pt in s["points"] for v in pt)))
    o.append("static const Stroke kStrokes[] = {\n")
    for i, s in enumerate(strokes):
        o.append("    {\"%s\", %d, kStrokePoints%d, %d, %d, %d, %d,\n     %s,\n     %s},\n" % (
            s["name"], len(s["points"]), i, s["closed"], s["width"], s["cap"], s["align"], cxx_bytes(s["expected"]),
            cxx_bytes(s["expected_br"])))
    o.append("};\nstatic const int kStrokeCount = %d;\n\n} // namespace center_vectors\n\n#endif\n" % len(strokes))
    return "".join(o)


def selftest():
    """The reference agrees with itself and with the hand-stated answers (build_cases raises if not), and the tie answers that no code
    could get wrong without a test noticing are stated outright."""
    build_cases()
    stroke_cases()
    # By hand: the square [32, 96) x [32, 96) in units has the center of pixel (0, 0) at its top-left corner.
    sq = [rect(32, 32, 96 + 64, 32 + 64 * 2)]
    got = covered(sq, NONZERO)
    assert got[0] == "1", "the top-left corner is inside"
    assert got[1] == "1", "the top edge's center is inside"
    assert got[2] == "0", "the right edge's center is outside"
    assert got[W + 0] == "1" and got[2 * W + 0] == "0", "the left edge is inside and the bottom edge outside"
    assert got[2 * W + 1] == "0", "the bottom-right corner is outside"
    assert got.count("1") == 2 * 2, "two by two pixels"
    # The same square under the bottom-right rule: the bottom-right corner is inside, the top-left one is not.
    got = covered(sq, NONZERO, top_left=False)
    assert got[0] == "0", "the top-left corner is outside"
    assert got[1] == "0" and got[2] == "0", "the top edge's center is outside"
    assert got[W + 0] == "0" and got[2 * W + 0] == "0", "the left edge is outside"
    assert got[W + 1] == "1" and got[2 * W + 1] == "1", "the pixels past the left and top edges are inside"
    assert got.count("1") == 2 * 2, "two by two pixels"
    return True


def main():
    global PLANT
    args = sys.argv[1:]
    if "--plant" in args:
        PLANT = args[args.index("--plant") + 1]
        if PLANT not in PLANTS:
            print("no such plant", PLANT)
            return 2
    if "--selftest" in args:
        try:
            ok = selftest()
        except (AssertionError, SystemExit) as e:
            print("selftest FAILED%s: %s" % (" (planted %s)" % PLANT if PLANT else "", e))
            return 1
        print("selftest ok" + (" (UNDETECTED plant %s)" % PLANT if PLANT else ""))
        return 0
    text = render()
    if "--check" in args:
        try:
            current = OUT.read_text()
        except OSError:
            current = ""
        if current != text:
            print("tests/center_vectors.h is not what tools/gen-center-vectors.py writes")
            return 1
        print("tests/center_vectors.h is current")
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
