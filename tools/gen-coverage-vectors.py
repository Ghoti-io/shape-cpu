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
"""The independent references for shape-cpu's coverage (CAP-6, CAP-13).

Writes tests/known_answers.h, or with --check compares the file on disk with
what this would write.  It shares no code with the C library: Python integers
(no overflow), its own FNV-1a, and two references that are different programs:

  exact   the *definition*, by integration.  Axis-aligned polygons only: the
          rectangle is cut into 64 x 64 sub-cells per pixel (a coordinate is a
          multiple of 1/64), every sub-cell is inside or outside every polygon
          exactly (its centre is at an odd half-unit, the edges at even ones),
          and the pixel's value is the winding sum folded by the fill rule.
          No edge is walked, no cell is accumulated.

  model   the algorithm, written again from its description in coverage.h
          (cells of `cover` and `area`, an edge split at pixel boundaries with
          each boundary's crossing rounded once, half away from zero, a sweep
          per row).  Slanted polygons: the answer for those is the algorithm's
          by definition, so a second program is the check on the first.

Both must agree on every axis-aligned case (the `exact` rows are asserted
against the `model` here, so a disagreement stops the generator), and the C
tests require the library to equal them.

The hash is FNV-1a 64 over: u32 1, u32 0x52564f43, u32 width, u32 height, i32
left, i32 top, u32 fill, then the rows (hash.h's spelling), written here with
struct.pack, not shifts.
"""

import random
import struct
import sys

FNV_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
M64 = (1 << 64) - 1
ONE = 64


def fnv(data, h=FNV_BASIS):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & M64
    return h


def coverage_hash(w, h, left, top, fill, rows):
    stream = struct.pack("<II", 1, 0x52564F43)
    stream += struct.pack("<II", w, h)
    stream += struct.pack("<ii", left, top)
    stream += struct.pack("<I", fill)
    for r in rows:
        stream += bytes(r)
    return fnv(stream)


# ---- the model -------------------------------------------------------------

def rdiv(num, den):
    """Half away from zero."""
    n, d = abs(num), abs(den)
    q = (n + d // 2) // d
    return -q if (num < 0) != (den < 0) else q


def fold(area, fill):
    v = area >> 5
    v = abs(v)
    if fill == 1:
        v &= 511
        if v > 256:
            v = 512 - v
        elif v == 256:
            v = 255
    elif v >= 256:
        v = 255
    return v


def model(contours, fill, left, top, w, h):
    cells = {}
    # The library's array merges a crossing only into the entry made just
    # before it, so its length (what max_cells caps) is the number of times the
    # cell changed along the walk, not the number of distinct cells.
    state = {"last": None, "entries": 0}

    def add(ex, ey, dy, fx1, fx2):
        if dy == 0:
            return
        assert 0 <= ex < w and 0 <= ey < h, "a cell outside the rectangle"
        if state["last"] != (ey, ex):
            state["last"] = (ey, ex)
            state["entries"] += 1
        c = cells.setdefault((ey, ex), [0, 0])
        c[0] += dy
        c[1] += dy * (fx1 + fx2)

    def scan(ey, fx, fy, tx, ty):
        ex1, ex2 = fx // ONE, tx // ONE
        dx, dy = tx - fx, ty - fy
        if ex1 == ex2:
            add(ex1, ey, dy, fx - ex1 * ONE, tx - ex1 * ONE)
            return
        step = 1 if dx > 0 else -1
        cx, cy = fx, fy
        ex = ex1
        while ex != ex2:
            b = (ex + 1 if step > 0 else ex) * ONE
            sy = fy + rdiv((b - fx) * dy, dx)
            add(ex, ey, sy - cy, cx - ex * ONE, b - ex * ONE)
            cx, cy = b, sy
            ex += step
        add(ex2, ey, ty - cy, cx - ex2 * ONE, tx - ex2 * ONE)

    def line(fx, fy, tx, ty):
        if fy == ty:
            return
        ey1, ey2 = fy // ONE, ty // ONE
        if ey1 == ey2:
            scan(ey1, fx, fy, tx, ty)
            return
        dy, dx = ty - fy, tx - fx
        step = 1 if dy > 0 else -1
        cx, cy = fx, fy
        ey = ey1
        while ey != ey2:
            b = (ey + 1 if step > 0 else ey) * ONE
            sx = fx + rdiv((b - fy) * dx, dy)
            scan(ey, cx, cy, sx, b)
            cx, cy = sx, b
            ey += step
        scan(ey2, cx, cy, tx, ty)

    for contour in contours:
        pts = [(x - left * ONE, y - top * ONE) for (x, y) in contour]
        for i in range(len(pts)):
            a, b = pts[i], pts[(i + 1) % len(pts)]
            line(a[0], a[1], b[0], b[1])
    rows = [[0] * w for _ in range(h)]
    for y in range(h):
        cs = sorted((x, v) for (ry, x), v in cells.items() if ry == y)
        cover, x = 0, 0

        def span(frm, count, area):
            v = fold(area, fill)
            for i in range(max(frm, 0), min(frm + count, w)):
                if v:
                    rows[y][i] = v

        for cx, (cv, ar) in cs:
            if cover != 0 and cx > x:
                span(x, cx - x, cover)
            cover += cv * 128
            span(cx, 1, cover - ar)
            x = cx + 1
    model.entries = state["entries"]
    return rows


# ---- the definition, by integration ---------------------------------------

def winding(contours, px2, py2):
    """The winding number at a point given in half units (polygon coordinates
    are doubled by the caller); sign: clockwise on a y-down screen is positive."""
    wn = 0
    for contour in contours:
        n = len(contour)
        for i in range(n):
            x0, y0 = contour[i][0] * 2, contour[i][1] * 2
            x1, y1 = contour[(i + 1) % n][0] * 2, contour[(i + 1) % n][1] * 2
            if (y0 <= py2) != (y1 <= py2):
                # x of the crossing compared with the sample, exactly
                lhs = (px2 - x0) * (y1 - y0)
                rhs = (py2 - y0) * (x1 - x0)
                left_of = lhs < rhs if y1 > y0 else lhs > rhs
                if left_of:
                    wn += 1 if y1 > y0 else -1
    return wn


def exact(contours, fill, left, top, w, h):
    rows = [[0] * w for _ in range(h)]
    for py in range(h):
        for px in range(w):
            s = 0
            for sy in range(ONE):
                y2 = ((top + py) * ONE + sy) * 2 + 1
                for sx in range(ONE):
                    x2 = ((left + px) * ONE + sx) * 2 + 1
                    s += winding(contours, x2, y2)
            # The library accumulates the signed area with the opposite sign
            # for a clockwise contour; the fold takes the floor of the signed
            # area before the absolute value.
            rows[py][px] = fold(-2 * s, fill)
    return rows


# ---- fixtures ---------------------------------------------------------------

def px(v):
    return int(round(v * ONE))


def rect(x0, y0, x1, y1, cw=True):
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return pts if cw else pts[::-1]


def box_of(contours):
    xs = [p[0] for c in contours for p in c]
    ys = [p[1] for c in contours for p in c]
    return min(xs), max(xs), min(ys), max(ys)


def margin_rect(contours):
    x0, x1, y0, y1 = box_of(contours)
    left = x0 // ONE - 1
    top = y0 // ONE - 1
    right = -((-x1) // ONE) + 1
    bottom = -((-y1) // ONE) + 1
    return left, top, right - left, bottom - top


def trim(rows, left, top):
    h, w = len(rows), len(rows[0]) if rows else 0
    ys = [y for y in range(h) if any(rows[y])]
    xs = [x for x in range(w) if any(rows[y][x] for y in range(h))]
    if not ys:
        return 0, 0, 0, 0, []
    out = [rows[y][xs[0]:xs[-1] + 1] for y in range(ys[0], ys[-1] + 1)]
    return left + xs[0], top + ys[0], len(out[0]), len(out), out


def leading_zero_rows():
    """Two by two rows, all one value, whose hash begins with a zero hex digit,
    so that a dump written without the 16-digit padding is seen to differ."""
    for v in range(1, 256):
        rows = [[v, v], [v, v]]
        if coverage_hash(2, 2, 4, 4, 0, rows) >> 60 == 0:
            return rows
    raise SystemExit("no value gives a hash with a leading zero digit")


def cases():
    out = []

    def add(name, fill, contours, axis):
        out.append((name, fill, contours, axis))

    # Axis-aligned, exact against the definition.
    for fill in (0, 1):
        f = "nz" if fill == 0 else "eo"
        add("rect-whole-%s" % f, fill, [rect(px(1), px(1), px(4), px(3))], True)
        add("rect-sub-%s" % f, fill, [rect(px(1) + 13, px(1) + 5, px(3) + 41, px(2) + 59)], True)
        add("rect-ccw-%s" % f, fill, [rect(px(1) + 7, px(1) + 31, px(5) + 3, px(4) + 17, cw=False)], True)
        add("two-same-%s" % f, fill, [rect(px(1), px(1), px(4) + 9, px(4) + 9), rect(px(2) + 20, px(2) + 20, px(6), px(6))], True)
        add("two-opposite-%s" % f, fill, [rect(px(1), px(1), px(5), px(5)), rect(px(2), px(2), px(4), px(4), cw=False)], True)
        add("ring-%s" % f, fill, [rect(px(0), px(0), px(6), px(6)), rect(px(2) + 11, px(2) + 11, px(4) + 5, px(4) + 5)], True)
        add("triple-%s" % f, fill, [rect(px(1), px(1), px(5), px(5)), rect(px(1) + 8, px(1) + 8, px(5) + 8, px(5) + 8), rect(px(1) + 16, px(1) + 16, px(5) + 16, px(5) + 16)], True)
        add("sliver-%s" % f, fill, [rect(px(1) + 3, px(1) + 3, px(5) + 60, px(1) + 4)], True)
        add("negative-%s" % f, fill, [rect(-px(3) - 9, -px(3) - 9, -px(1) - 1, -px(1) - 1)], True)
    # Slanted: the model against the library.
    add("triangle-nz", 0, [[(px(1), px(1)), (px(7) + 13, px(2) + 9), (px(3) + 31, px(6) + 5)]], False)
    add("triangle-eo", 1, [[(px(1), px(1)), (px(7) + 13, px(2) + 9), (px(3) + 31, px(6) + 5)]], False)
    star = [(px(4) + 32, px(0) + 3), (px(6) + 40, px(7) + 1), (px(0) + 5, px(2) + 30), (px(8) + 11, px(2) + 30), (px(2) + 24, px(7) + 1)]
    add("pentagram-nz", 0, [star], False)
    add("pentagram-eo", 1, [star], False)
    add("steep-thin", 0, [[(px(2), px(1)), (px(2) + 3, px(1)), (px(2) + 40, px(9)), (px(2) + 37, px(9))]], False)
    add("shallow-thin", 0, [[(px(1), px(2)), (px(9), px(2) + 40), (px(9), px(2) + 37), (px(1), px(2) - 3)]], False)
    rng = random.Random(9)
    for i in range(60):
        n = rng.randint(3, 7)
        pts = [(rng.randint(px(1), px(13)), rng.randint(px(1), px(13))) for _ in range(n)]
        add("random-%02d-%s" % (i, "nz" if i % 2 == 0 else "eo"), i % 2, [pts], False)
    for i in range(12):
        cs = []
        for _ in range(rng.randint(2, 3)):
            x0, y0 = rng.randint(0, px(9)), rng.randint(0, px(9))
            x1, y1 = x0 + rng.randint(1, px(5)), y0 + rng.randint(1, px(5))
            cs.append(rect(x0, y0, x1, y1, cw=rng.random() < 0.5))
        add("random-rects-%02d-%s" % (i, "nz" if i % 2 == 0 else "eo"), i % 2, cs, True)
    return out


def hexrows(rows):
    return "".join("%02x" % b for r in rows for b in r)


def emit():
    lines = []
    w = lines.append
    w("// Generated by tools/gen-coverage-vectors.py. Do not edit.")
    w("// Independent references for shape-cpu's coverage: the definition by")
    w("// integration (axis-aligned) and a second program of the algorithm.")
    w("#ifndef GHOTI_IO_GSHCPU_KNOWN_ANSWERS_H")
    w("#define GHOTI_IO_GSHCPU_KNOWN_ANSWERS_H")
    w("")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("#include <utility>")
    w("#include <vector>")
    w("")
    w("namespace known {")
    w("")
    w("struct Fill {")
    w("  const char * name;")
    w("  int fill;")
    w("  bool axis_aligned;")
    w("  std::vector<std::vector<std::pair<int32_t, int32_t>>> contours;")
    w("  // gshcpu_coverage_fill into this rectangle: the whole of it.")
    w("  int32_t left, top;")
    w("  uint32_t width, height;")
    w("  const char * rows;       // hex, width * height bytes")
    w("  uint64_t hash;")
    w("  size_t cells;            // entries the accumulator holds for this path (max_cells)")
    w("  // gshcpu_coverage_from_path: the margin rectangle, trimmed.")
    w("  int32_t t_left, t_top;")
    w("  uint32_t t_width, t_height;")
    w("  const char * t_rows;")
    w("  uint64_t t_hash;")
    w("};")
    w("")
    w("inline const std::vector<Fill> & fills() {")
    w("  static const std::vector<Fill> all = {")
    n_axis = 0
    for name, fill, contours, axis in cases():
        left, top, wd, ht = margin_rect(contours)
        m = model(contours, fill, left, top, wd, ht)
        if axis:
            e = exact(contours, fill, left, top, wd, ht)
            assert e == m, "the definition and the model disagree on %s" % name
            n_axis += 1
        tl, tt, tw, th, tr = trim(m, left, top)
        cstr = ", ".join("{" + ", ".join("{%d, %d}" % p for p in c) + "}" for c in contours)
        w("    {\"%s\", %d, %s, {%s}," % (name, fill, "true" if axis else "false", cstr))
        w("     %d, %d, %du, %du, \"%s\", 0x%016xull, %du," % (left, top, wd, ht, hexrows(m), coverage_hash(wd, ht, left, top, fill, m), model.entries))
        w("     %d, %d, %du, %du, \"%s\", 0x%016xull}," % (tl, tt, tw, th, hexrows(tr), coverage_hash(tw, th, tl, tt, fill, tr)))
    w("  };")
    w("  return all;")
    w("}")
    w("")
    # Hash fixtures: a coverage given by hand, with a stride wider than its width.
    w("struct Hand {")
    w("  const char * name;")
    w("  uint32_t width, height;")
    w("  int32_t left, top;")
    w("  int fill;")
    w("  const char * rows;   // hex, width * height bytes")
    w("  uint64_t hash;")
    w("};")
    w("")
    w("inline const std::vector<Hand> & hands() {")
    w("  static const std::vector<Hand> all = {")
    hand = [
        ("empty", 0, 0, 0, 0, 0, []),
        ("empty-even-odd-moved", 0, 0, -5, 7, 1, []),
        ("one-pixel", 1, 1, 0, 0, 0, [[255]]),
        ("negative-origin", 3, 2, -2147483648, 2147483647, 1, [[0, 1, 2], [253, 254, 255]]),
        ("ramp", 8, 3, 100, -100, 0, [[i * 32 for i in range(8)], [255 - i for i in range(8)], [0] * 8]),
        ("zero-leading-hash-digit", 2, 2, 4, 4, 0, leading_zero_rows()),
    ]
    for name, wd, ht, left, top, fill, rows in hand:
        w("    {\"%s\", %du, %du, %d, %d, %d, \"%s\", 0x%016xull}," % (name, wd, ht, left if left != -2147483648 else -2147483647 - 1, top, fill, hexrows(rows), coverage_hash(wd, ht, left, top, fill, rows)))
    w("  };")
    w("  return all;")
    w("}")
    w("")
    w("}  // namespace known")
    w("")
    w("#endif  // GHOTI_IO_GSHCPU_KNOWN_ANSWERS_H")
    sys.stderr.write("%d cases (%d axis-aligned, checked against the definition)\n" % (len(cases()), n_axis))
    return "\n".join(lines) + "\n"


def main():
    text = emit()
    path = "tests/known_answers.h"
    if "--check" in sys.argv:
        if open(path).read() != text:
            sys.exit("%s differs from what the generator writes" % path)
        print("known answers: unchanged")
        return
    open(path, "w").write(text)


if __name__ == "__main__":
    main()
