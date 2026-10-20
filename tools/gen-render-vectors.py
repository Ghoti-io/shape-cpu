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
"""The independent reference for shape-cpu's renders (story 13b, S11, D7, D8).

Writes tests/render_vectors.h, or with --check compares the file on disk with
what this would write.  It shares no code with the C library and none with
shape-scene's tools/composite-oracle.py: it restates, in exact rationals,

  - what a list means at one pixel (shape-scene's N1 to N10): the gamuts and
    their matrices, the transfers, the conversion, the operators, `apply`,
    layers, groups, clips and the foreground;
  - shape-cpu's own definitions on top of it (render.h, S11): the 8-bit
    coverage of a rectangle `w`/64 of a pixel wide and one tall (coverage.h's
    exact area: `4 w`, 255 when whole), the D8 weight of a coverage under each
    clip in force, `(m8 * c8 + 127) / 255` in the order they were pushed, the
    weight `m8 / 255`, the backdrop each format holds, and the one rounding at
    the write (round half up to the 8-bit step of `C` or of `C / A`, or to the
    nearest float), and that a pixel no unit touched is not written.

Arithmetic is `fractions.Fraction` throughout.  The power function of a
transfer is evaluated with `decimal` at 80 digits and turned back into a
rational (an error below 1e-70); a case that went through one is inexact.  The
nearest double and the nearest float of a rational are rounded here, half to
even, from the rational itself (never through a float of Python's).

For each case and each of the four formats the header holds the backdrop as the
format stores it, the root layer's value before the write (the nearest
doubles), whether a double evaluator can match it bit for bit (`exact`: every
value dyadic, no transfer and no matrix), whether the pixel is touched, and the
stored value after the write, with the other side of the rounding when the
value lies within 1e-9 of a boundary (D7).

It also writes the known answers of the render hash and dump spelling
(render.h): small rasters, their FNV-1a 64 hashes and their dump text, built
here with struct.pack.

    python3 tools/gen-render-vectors.py          # write tests/render_vectors.h
    python3 tools/gen-render-vectors.py --check  # fail if it differs
"""

import struct
import sys
from decimal import Decimal, getcontext
from fractions import Fraction as F
from pathlib import Path

getcontext().prec = 80

OUT = Path(__file__).resolve().parent.parent / "tests" / "render_vectors.h"

ZERO = F(0)
ONE = F(1)
NEAR = F(1, 10**9)  # D7: a value this close to a rounding boundary may land on either side

# --- N1: the gamuts (CIE xy of red, green, blue; the white is D65 for all) ----------------------------
WHITE = (F("0.3127"), F("0.3290"))
GAMUTS = {
    1: ((F("0.64"), F("0.33")), (F("0.30"), F("0.60")), (F("0.15"), F("0.06"))),
    2: ((F("0.64"), F("0.33")), (F("0.21"), F("0.71")), (F("0.15"), F("0.06"))),
    3: ((F("0.680"), F("0.320")), (F("0.265"), F("0.690")), (F("0.150"), F("0.060"))),
    4: ((F("0.708"), F("0.292")), (F("0.170"), F("0.797")), (F("0.131"), F("0.046"))),
}
LINEAR, SRGB, GAMMA = 1, 2, 3
SRGB_SPACE = (1, SRGB, 0.0)


class Inexact:
    """Set when a transfer or a matrix was evaluated."""

    def __init__(self):
        self.flag = False


def det3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def inv3(m):
    d = det3(m)
    out = [[ZERO] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            r0, r1, c0, c1 = (j + 1) % 3, (j + 2) % 3, (i + 1) % 3, (i + 2) % 3
            out[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d
    return out


def mat_vec(m, v):
    return [m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3)]


def to_xyz(primaries):
    """N3: the columns S_c (X_c, 1, Z_c), S solving [X; 1; Z] S = W."""
    xy = GAMUTS[primaries]
    p = [[c[0] / c[1] for c in xy], [ONE, ONE, ONE], [(1 - c[0] - c[1]) / c[1] for c in xy]]
    w = [WHITE[0] / WHITE[1], ONE, (1 - WHITE[0] - WHITE[1]) / WHITE[1]]
    s = mat_vec(inv3(p), w)
    return [[s[c] * p[r][c] for c in range(3)] for r in range(3)]


def power(base, exponent, ex):
    """base^exponent for base > 0, at 80 digits, back to a rational."""
    ex.flag = True
    b = Decimal(base.numerator) / Decimal(base.denominator)
    e = Decimal(exponent.numerator) / Decimal(exponent.denominator)
    return F((b.ln() * e).exp())


def decode(space, x, ex):
    t = space[1]
    if t == LINEAR:
        return x
    if t == SRGB:
        return x / F("12.92") if x <= F("0.04045") else power((x + F("0.055")) / F("1.055"), F("2.4"), ex)
    if x == 0:
        return ZERO
    g = F(space[2])
    return -power(-x, g, ex) if x < 0 else power(x, g, ex)


def encode(space, y, ex):
    t = space[1]
    if t == LINEAR:
        return y
    if t == SRGB:
        return y * F("12.92") if y <= F("0.0031308") else F("1.055") * power(y, 1 / F("2.4"), ex) - F("0.055")
    if y == 0:
        return ZERO
    g = 1 / F(space[2])
    return -power(-y, g, ex) if y < 0 else power(y, g, ex)


def convert(rgb, a, b, ex):
    """N3: a straight color; untouched when the spaces are equal."""
    if a == b:
        return list(rgb)
    lin = [decode(a, c, ex) for c in rgb]
    if a[0] != b[0]:
        ex.flag = True
        lin = mat_vec(inv3(to_xyz(b[0])), mat_vec(to_xyz(a[0]), lin))
    return [encode(b, c, ex) for c in lin]


def convert_pre(px, a, b, ex):
    if a == b:
        return list(px)
    if px[3] == 0:
        return [ZERO] * 4
    c = convert([px[i] / px[3] for i in range(3)], a, b, ex)
    return [c[0] * px[3], c[1] * px[3], c[2] * px[3], px[3]]


# --- N4, N5 ----------------------------------------------------------------------------------------------
OPS = ["src-over", "clear", "src", "dest", "dest-over", "src-in", "dest-in", "src-out", "dest-out", "src-atop",
       "dest-atop", "xor", "plus"]
BOUNDED = {0, 3, 4, 8, 9, 11, 12}


def factors(op, sa, da):
    return {0: (ONE, 1 - sa), 1: (ZERO, ZERO), 2: (ONE, ZERO), 3: (ZERO, ONE), 4: (1 - da, ONE), 5: (da, ZERO),
            6: (ZERO, sa), 7: (1 - da, ZERO), 8: (ZERO, 1 - sa), 9: (da, 1 - sa), 10: (1 - da, sa),
            11: (1 - da, 1 - sa)}[op]


def apply(d, s, m, op):
    if op == 12:
        o = [min(ONE, s[i] + d[i]) for i in range(4)]
    else:
        fa, fb = factors(op, s[3], d[3])
        o = [fa * s[i] + fb * d[i] for i in range(4)]
    return [(1 - m) * d[i] + m * o[i] for i in range(4)]


# --- shape-cpu's definitions (render.h, S11, D8) ---------------------------------------------------------
def coverage8(width):
    """The 8-bit coverage of a rectangle `width`/64 of a pixel wide, one pixel tall, at the pixel's left edge."""
    return 255 if width >= 64 else 4 * width


def weigh(m8, c8):
    return (m8 * c8 + 127) // 255


def evaluate(case, backdrop):
    """The root layer after the list, and whether a unit touched it.  `backdrop` is the root's premultiplied value."""
    ex = Inexact()
    target = case["target"]
    fg = case["fg"]
    root = {"layer": list(backdrop), "space": target, "clips": [], "group": None, "outer": None}
    stack = [root]
    touched = False
    dyadic = True
    for item in case["items"]:
        top = stack[-1]
        kind = item["kind"]
        if kind == "fill":
            if item.get("foreground"):
                rgb = [F(v) for v in fg["rgba"][:3]]
                alpha = F(fg["rgba"][3]) * F(item["rgba"][3])
                space = fg["space"]
            else:
                rgb = [F(v) for v in item["rgba"][:3]]
                alpha = F(item["rgba"][3])
                space = item.get("space", (0, 0, 0.0))
            if space == (0, 0, 0.0):
                space = target
            c = convert(rgb, space, top["space"], ex)
            s = [c[0] * alpha, c[1] * alpha, c[2] * alpha, alpha]
            m8 = coverage8(item["w"])
            for c8 in top["clips"]:
                m8 = weigh(m8, c8)
            if m8 == 0:
                continue
            if m8 != 255:
                dyadic = False
            top["layer"] = apply(top["layer"], s, F(m8, 255), 0)
            if top is root:
                touched = True
        elif kind == "clip":
            top["clips"].append(coverage8(item["w"]))
        elif kind == "unclip":
            top["clips"].pop()
        elif kind == "group":
            blend = item.get("blend", (0, 0, 0.0))
            space = top["space"] if blend == (0, 0, 0.0) else blend
            outer = 255
            for c8 in top["clips"]:
                outer = weigh(outer, c8)
            stack.append({"layer": [ZERO] * 4, "space": space, "clips": [], "group": item, "outer": outer})
        elif kind == "ungroup":
            done = stack.pop()
            parent = stack[-1]
            g = done["group"]
            o = F(g.get("opacity", 1.0))
            op = g.get("composite", 0)
            m8 = done["outer"]
            if m8 == 0:
                continue
            if m8 != 255:
                dyadic = False
            moved = convert_pre(done["layer"], done["space"], parent["space"], ex)
            s = [o * v for v in moved]
            if op in BOUNDED and all(v == 0 for v in s):
                continue  # render.h: exactly the backdrop, and not touched
            parent["layer"] = apply(parent["layer"], s, F(m8, 255), op)
            if parent is root:
                touched = True
        else:
            raise ValueError(kind)
    return root["layer"], touched, ex.flag, dyadic


# --- rounding a rational ------------------------------------------------------------------------------------
def nearest_binary(x, mantissa, min_exp):
    """The nearest binary floating-point value to x (half to even), with `mantissa` bits, as a Fraction."""
    if x == 0:
        return ZERO
    sign = -1 if x < 0 else 1
    a = abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    while F(2) ** e > a:
        e -= 1
    while F(2) ** (e + 1) <= a:
        e += 1
    e = max(e, min_exp)
    quantum = F(2) ** (e - mantissa + 1)
    q = a / quantum
    n = q.numerator // q.denominator
    rem = q - n
    if rem > F(1, 2) or (rem == F(1, 2) and n % 2 == 1):
        n += 1
    return sign * n * quantum


def to_double(x):
    return nearest_binary(x, 53, -1022)


def to_float(x):
    return nearest_binary(x, 24, -126)


def float_bits(x):
    """The IEEE single bit pattern of the float x (a Fraction already rounded to a float)."""
    return struct.unpack("<I", struct.pack("<f", float(x)))[0]


def double_hex(x):
    return float(x).hex() if x != 0 else "0x0.0p+0"


def float_step(f, up):
    """The float next to the float f, toward +infinity when up."""
    bits = float_bits(f)
    if f == 0:
        bits = 1 if up else 0x80000001
    elif (f > 0) == up:
        bits += 1
    else:
        bits -= 1
    return F(struct.unpack("<f", struct.pack("<I", bits))[0])


def float_neighbours(x):
    """The floats either side of x and the midpoint between them; (f, f, None) when x is a float."""
    f = to_float(x)
    if f == x:
        return f, f, None
    other = float_step(f, f < x)
    lo, hi = (f, other) if f < x else (other, f)
    return lo, hi, (lo + hi) / 2


def store8(v):
    """floor(v * 255 + 1/2) clamped to [0, 255], and the other side when v is within 1e-9 of a boundary."""
    t = v * 255
    q = (t + F(1, 2)).numerator // (t + F(1, 2)).denominator
    q = min(255, max(0, q))
    alt = q
    # The boundaries around v are (q - 1/2) / 255 and (q + 1/2) / 255.
    for b, other in (((F(q) - F(1, 2)) / 255, q - 1), ((F(q) + F(1, 2)) / 255, q + 1)):
        if 0 <= other <= 255 and abs(v - b) < NEAR:
            alt = other
    return q, alt


def storef(v):
    f = to_float(v)
    alt = f
    lo, hi, mid = float_neighbours(v)
    if mid is not None and abs(v - mid) < NEAR:
        alt = hi if f == lo else lo
    return float_bits(f), float_bits(alt)


# --- the formats: the backdrop each holds, and the write ------------------------------------------------------
FORMATS = [(1, "rgba8"), (2, "rgba8-premultiplied"), (3, "rgbaf32"), (4, "rgbaf32-premultiplied")]


def backdrop_of(fmt, s8):
    """The stored backdrop (4 integers: bytes or float bits) and its premultiplied value, from straight bytes."""
    a = F(s8[3], 255)
    if fmt == 1:
        return list(s8), [F(s8[i], 255) * a for i in range(3)] + [a]
    if fmt == 2:
        p = [(s8[i] * s8[3] + 127) // 255 for i in range(3)] + [s8[3]]
        return p, [F(v, 255) for v in p]
    # Floats: the nearest float of v / 255 (and, premultiplied, of the product of the two doubles' values, which
    # is how tests/render_fixtures.h and the C test store it: float(double(c / 255) * double(a / 255))).
    if fmt == 3:
        f = [to_float(to_double(F(v, 255))) for v in s8]
        return [float_bits(v) for v in f], [f[i] * f[3] for i in range(3)] + [f[3]]
    da = to_double(a)
    f = [to_float(to_double(to_double(F(s8[i], 255)) * da)) for i in range(3)] + [to_float(da)]
    return [float_bits(v) for v in f], f


def write_of(fmt, value, touched, stored):
    """The stored value after the write, and the other side of a near rounding."""
    if not touched:
        return list(stored), list(stored)
    v = list(value)
    if fmt in (1, 3):
        v = [ZERO if v[3] == 0 else v[i] / v[3] for i in range(3)] + [v[3]]
    want, alt = [], []
    for x in v:
        w, a = store8(x) if fmt in (1, 2) else storef(x)
        want.append(w)
        alt.append(a)
    return want, alt


# --- the cases ---------------------------------------------------------------------------------------------------
def fill(rgba, w=64, space=(0, 0, 0.0)):
    return {"kind": "fill", "rgba": rgba, "w": w, "space": space}


def fg_fill(alpha, w=64):
    return {"kind": "fill", "foreground": True, "rgba": [0, 0, 0, alpha], "w": w}


def group(opacity=1.0, composite=0, blend=(0, 0, 0.0)):
    return {"kind": "group", "opacity": opacity, "composite": composite, "blend": blend}


UNGROUP = {"kind": "ungroup"}
UNCLIP = {"kind": "unclip"}


def clip(w):
    return {"kind": "clip", "w": w}


LIN = (1, LINEAR, 0.0)
P3 = (3, SRGB, 0.0)
P3_GAMMA = (3, GAMMA, 2.2)
ADOBE_GAMMA = (2, GAMMA, 2.19921875)
BT2020_LIN = (4, LINEAR, 0.0)
FG = {"rgba": [0.5, 0.25, 0.0, 0.5], "space": (0, 0, 0.0)}
SRC = [1.0, 0.5, 0.0, 0.75]
BACKDROPS = [(128, 64, 255, 128), (0, 0, 255, 255), (37, 200, 91, 0), (250, 3, 77, 211)]


def cases():
    out = []

    def add(name, items, backdrop=BACKDROPS[0], target=SRGB_SPACE, fg=None):
        out.append({"name": name, "items": items, "backdrop": backdrop, "target": target, "fg": fg or FG})

    for op in range(13):
        for opacity in (1.0, 0.5):
            add("%s opacity %s" % (OPS[op], opacity), [group(opacity, op), fill(SRC), UNGROUP])
    for op in (0, 1, 2, 5, 6, 11, 12):
        add("%s weighed by a clip of 16/64" % OPS[op], [clip(16), group(1.0, op), fill(SRC), UNGROUP, UNCLIP])
    for w in (0, 1, 16, 32, 47, 63, 64):
        add("a fill %d/64 wide" % w, [fill(SRC, w)])
    for b in BACKDROPS:
        add("a fill over %s" % (b,), [fill([0.25, 0.5, 0.75, 0.5], 40)], backdrop=b)
        add("an untouched pixel of %s" % (b,), [fill([0.25, 0.5, 0.75, 0.5], 0)], backdrop=b)
    add("two clips weigh in their order", [clip(48), clip(36), fill(SRC, 40), UNCLIP, UNCLIP])
    add("the same clips the other way", [clip(36), clip(48), fill(SRC, 40), UNCLIP, UNCLIP])
    add("a clip around two overlapping fills weighs each",
        [clip(32), fill([1, 0, 0, 1]), fill([0, 1, 0, 0.5]), UNCLIP], backdrop=(0, 0, 0, 0))
    add("a clip around a group weighs it once",
        [clip(32), group(), fill([1, 0, 0, 1]), fill([0, 1, 0, 0.5]), UNGROUP, UNCLIP], backdrop=(0, 0, 0, 0))
    add("a clip outside a group is not taken into it", [clip(16), group(), fill([1, 0, 0, 1], 32), UNGROUP, UNCLIP])
    add("a clip inside a group is that group's alone",
        [group(), clip(20), fill([1, 0, 0, 1]), UNCLIP, fill([0, 0, 1, 0.5], 28), UNGROUP])
    add("a clip of 0 around an unbounded group leaves the backdrop", [clip(0), group(1.0, 2), fill(SRC), UNGROUP, UNCLIP])
    add("an unbounded group with an empty layer clears", [group(1.0, 2), fill(SRC, 0), UNGROUP])
    add("a group of opacity 0 with an unbounded operator clears", [group(0.0, 5), fill(SRC), UNGROUP])
    add("a group of opacity 0 changes nothing", [group(0.0, 0), fill(SRC), UNGROUP])
    add("plus clamps", [group(1.0, 12), fill([1, 1, 1, 1]), UNGROUP], backdrop=(200, 255, 200, 230))
    add("nested groups", [group(0.5, 0), fill([1, 0, 0, 1]), group(0.75, 11), fill([0, 0, 1, 0.5], 48), UNGROUP,
                          UNGROUP])
    add("currentColor", [fg_fill(0.5, 48)])
    add("currentColor in P3", [fg_fill(1.0)], fg={"rgba": [0.9, 0.2, 0.1, 0.75], "space": P3})
    add("a paint in linear light", [fill([0.5, 0.5, 0.5, 1.0], 64, LIN)])
    add("a paint in Display P3", [fill([1.0, 0.0, 0.0, 1.0], 64, P3)], backdrop=(0, 0, 0, 0))
    add("a paint in Display P3 with a gamma", [fill([0.2, 0.6, 0.8, 0.9], 52, P3_GAMMA)])
    add("a paint in Adobe RGB with a gamma", [fill([0.1, 0.7, 0.3, 0.6], 64, ADOBE_GAMMA)])
    add("a paint in linear BT.2020", [fill([0.3, 0.6, 0.9, 1.0], 64, BT2020_LIN)], backdrop=(10, 20, 30, 255))
    add("a negative component in a gamma space", [fill([-0.25, 0.5, 0.75, 1.0], 64, P3_GAMMA)])
    add("a mid gray in a linear group", [group(1.0, 0, LIN), fill([0.5, 0.5, 0.5, 1.0]), UNGROUP], backdrop=(0, 0, 0, 0))
    add("two fills blended in linear light",
        [group(1.0, 0, LIN), fill([1, 1, 1, 0.5]), fill([0, 0, 0, 0.5], 32), UNGROUP], backdrop=(0, 0, 0, 0))
    add("a group in P3 holding a paint in sRGB, half covered", [group(0.8, 0, P3), fill([0.5, 0.25, 0.75, 0.5], 32),
                                                                 UNGROUP])
    add("an inherited blend space", [group(1.0, 0, LIN), group(0.5, 0), fill([0.2, 0.4, 0.6, 1.0]), UNGROUP, UNGROUP])
    add("a paint just above the sRGB decode threshold", [fill([0.040451, 0.04, 0.5, 1.0], 64, LIN)], target=LIN,
        backdrop=(0, 0, 0, 0))
    add("a target that blends in linear sRGB", [fill([0.5, 0.25, 0.125, 0.5], 44)], target=LIN)
    add("foreground in another space over a linear target", [fg_fill(0.75, 64)], target=LIN,
        fg={"rgba": [0.5, 0.25, 0.75, 1.0], "space": P3})
    add("nested groups in two gammas of one gamut",
        [group(0.9, 0, (3, GAMMA, 1.8)), group(0.8, 0, P3_GAMMA), fill([0.6, 0.3, 0.2, 0.7]), UNGROUP, UNGROUP])
    add("an xor group under two clips", [clip(60), clip(24), group(0.9, 11, P3), fill([0.3, 0.9, 0.1, 0.8], 56), UNGROUP,
                                         UNCLIP, UNCLIP], backdrop=BACKDROPS[3])
    return out


# --- the hash and dump known answers ---------------------------------------------------------------------------
FNV_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
M64 = (1 << 64) - 1


def fnv(data, h=FNV_BASIS):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & M64
    return h


def render_hash(fmt, extent, rows):
    """rows: per row, per pixel, four integers (bytes, or float bit patterns)."""
    s = struct.pack("<III", 1, 0x444E4552, fmt) + struct.pack("<qqqq", *extent)
    for row in rows:
        for px in row:
            s += struct.pack("<BBBB", *px) if fmt in (1, 2) else struct.pack("<IIII", *px)
    return fnv(s)


def render_dump(fmt, extent, rows):
    names = {1: "rgba8", 2: "rgba8-premultiplied", 3: "rgbaf32", 4: "rgbaf32-premultiplied"}
    text = "gshcpu.render 1 format=%s extent=%d,%d,%d,%d hash=%016x\n" % (
        (names[fmt],) + tuple(extent) + (render_hash(fmt, extent, rows),))
    for i, row in enumerate(rows):
        y = extent[1] + i
        text += "r %d" % y
        for px in row:
            text += " " + ("".join("%02x" % v for v in px) if fmt in (1, 2) else "".join("%08x" % v for v in px))
        text += "\n"
    return text


def hash_cases():
    """(format, raster width, height, pixels row by row as 4-tuples, extent) for the known answers.

    Every channel holds a value of one hexadecimal digit somewhere (a byte below 16, a float whose bit pattern is
    below 2^28), so a dump that drops a leading zero differs; and one raster is chosen so that its hash has a
    leading zero digit, so a dump that drops that differs too.
    """
    out = []
    w, h = 3, 2
    pix8 = [[(x * 40 + y, 9 * (2 - x) + y * 200, (x * y * 91) & 255, 5 + x * 120 + y) for x in range(w)] for y in range(h)]
    out.append((1, w, h, pix8, (0, 0, 3, 2)))
    out.append((2, w, h, pix8, (1, 0, 3, 2)))
    tiny = to_float(F(1, 2**130))  # a subnormal float: its bit pattern has leading zero digits
    floats = [[tuple(float_bits(v) for v in (to_float(F(x + 1, 7)) if y else tiny, tiny if x else to_float(F(y + 2, 7)),
                                              to_float(F(x * y - 3, 7)) if x + y else ZERO, tiny if x + y == 1 else F(6, 7)))
               for x in range(2)] for y in range(2)]
    out.append((3, 2, 2, floats, (0, 0, 2, 2)))
    out.append((4, 2, 2, floats, (0, 1, 2, 2)))
    # A raster whose hash begins with a zero digit: the first such of a fixed walk.
    for k in range(4096):
        px = [[(k & 255, (k >> 8) & 255, 3, 4)]]
        if render_hash(1, (0, 0, 1, 1), px) >> 60 == 0:
            out.append((1, 1, 1, px, (0, 0, 1, 1)))
            break
    else:
        raise RuntimeError("no hash with a leading zero digit in the walk")
    return out


# --- the header ---------------------------------------------------------------------------------------------------
def space_c(s):
    return "{%d, %d, %s}" % (s[0], s[1], double_hex(F(s[2])))


def generate():
    lines = []
    w = lines.append
    w("// Generated by tools/gen-render-vectors.py (exact rationals; independent of the library and of")
    w("// shape-scene's oracle).  Do not edit; run the generator.")
    w("#ifndef GHOTI_IO_GSHCPU_TEST_RENDER_VECTORS_H")
    w("#define GHOTI_IO_GSHCPU_TEST_RENDER_VECTORS_H")
    w("")
    w("#include <cstdint>")
    w("")
    w("namespace render_vectors {")
    w("")
    w("struct RSpace {")
    w("  int primaries;")
    w("  int transfer;")
    w("  double gamma;")
    w("};")
    w("")
    w("// kind: 1 fill, 2 clip push, 3 clip pop, 4 group push, 5 group pop.  paint: 0 solid, 2 foreground.")
    w("// width: a fill's or clip's rectangle, in 1/64 of the pixel, from its left edge, the pixel's full height.")
    w("struct RItem {")
    w("  int kind;")
    w("  int paint;")
    w("  double rgba[4];")
    w("  RSpace space;")
    w("  double opacity;")
    w("  RSpace blend;")
    w("  int composite;")
    w("  int width;")
    w("};")
    w("")
    w("// One format: the backdrop as stored (bytes, or float bit patterns), the root layer before the write")
    w("// (exact: a double evaluator matches bit for bit), whether the pixel is touched, and the stored value")
    w("// after the write, with the other side of a rounding within 1e-9 of its boundary in alt.")
    w("struct RFormat {")
    w("  int format;")
    w("  uint32_t backdrop[4];")
    w("  double pre[4];")
    w("  bool exact;")
    w("  bool touched;")
    w("  uint32_t want[4];")
    w("  uint32_t alt[4];")
    w("};")
    w("")
    w("struct RCase {")
    w("  const char * name;")
    w("  RSpace target;")
    w("  double fg[4];")
    w("  RSpace fg_space;")
    w("  int item_count;")
    w("  const RItem * items;")
    w("  RFormat formats[4];")
    w("};")
    w("")
    kinds = {"fill": 1, "clip": 2, "unclip": 3, "group": 4, "ungroup": 5}
    all_cases = cases()
    for n, case in enumerate(all_cases):
        w("static const RItem kItems%d[] = {" % n)
        for it in case["items"]:
            rgba = it.get("rgba", [0, 0, 0, 0])
            w("    {%d, %d, {%s}, %s, %s, %s, %d, %d}," % (
                kinds[it["kind"]], 2 if it.get("foreground") else 0, ", ".join(double_hex(F(v)) for v in rgba),
                space_c(it.get("space", (0, 0, 0.0))), double_hex(F(it.get("opacity", 1.0))),
                space_c(it.get("blend", (0, 0, 0.0))), it.get("composite", 0), it.get("w", 0)))
        w("};")
    w("")
    w("static const RCase kCases[] = {")
    exact_count = 0
    for n, case in enumerate(all_cases):
        fmts = []
        for fmt, _ in FORMATS:
            stored, value = backdrop_of(fmt, case["backdrop"])
            layer, touched, inexact, dyadic = evaluate(case, value)
            exact = not inexact and dyadic and all(to_double(v) == v for v in layer + value)
            exact_count += 1 if exact else 0
            want, alt = write_of(fmt, layer, touched, stored)
            fmts.append("{%d, {%s}, {%s}, %s, %s, {%s}, {%s}}" % (
                fmt, ", ".join("%du" % v for v in stored), ", ".join(double_hex(to_double(v)) for v in layer),
                "true" if exact else "false", "true" if touched else "false", ", ".join("%du" % v for v in want),
                ", ".join("%du" % v for v in alt)))
        fg = case["fg"]
        w("    {\"%s\", %s, {%s}, %s, %d, kItems%d, {%s}}," % (
            case["name"], space_c(case["target"]), ", ".join(double_hex(F(v)) for v in fg["rgba"]),
            space_c(fg["space"]), len(case["items"]), n, ", ".join(fmts)))
    w("};")
    w("")
    w("static const int kCaseCount = %d;" % len(all_cases))
    w("static const int kExactCount = %d;" % exact_count)
    w("")
    w("// The render hash and dump spelling: a raster, the extent hashed, and the answers.")
    w("struct RHash {")
    w("  int format;")
    w("  uint32_t width;")
    w("  uint32_t height;")
    w("  const uint32_t * pixels; // row by row, four values a pixel (bytes, or float bit patterns)")
    w("  int64_t extent[4];")
    w("  uint64_t hash;")
    w("  const char * dump;")
    w("};")
    hcs = hash_cases()
    for n, (fmt, width, height, rows, extent) in enumerate(hcs):
        flat = [v for row in rows for px in row for v in px]
        w("static const uint32_t kHashPixels%d[] = {%s};" % (n, ", ".join("%du" % v for v in flat)))
    w("static const RHash kHashes[] = {")
    for n, (fmt, width, height, rows, extent) in enumerate(hcs):
        sub = [row[extent[0]:extent[2]] for row in rows[extent[1]:extent[3]]]
        dump = render_dump(fmt, extent, sub).replace("\n", "\\n")
        w("    {%d, %du, %du, kHashPixels%d, {%d, %d, %d, %d}, 0x%016xull, \"%s\"}," % (
            fmt, width, height, n, extent[0], extent[1], extent[2], extent[3], render_hash(fmt, extent, sub), dump))
    w("};")
    w("")
    w("}  // namespace render_vectors")
    w("")
    w("#endif")
    return "\n".join(lines) + "\n"


def selftest():
    """The rounding helpers against Python's own doubles and floats, where those are exact."""
    for v in (F(1, 3), F(2, 3), F(-5, 7), F(1, 10**30), F(10**30, 7)):
        assert to_double(v) == F(float(v)), v
    for v in (F(1, 3), F(2, 3), F(-5, 7), F(3, 10**9)):
        # struct rounds a double to a float, once: exact when the double is exactly halfway-free, as these are.
        assert float_bits(to_float(v)) == struct.unpack("<I", struct.pack("<f", float(v)))[0], v
        lo, hi, mid = float_neighbours(v)
        assert lo < v < hi and float_step(lo, True) == hi, v
    assert store8(F(1, 2) / 255) == (1, 0)  # exactly on a boundary: half up, the other side allowed
    assert coverage8(16) == 64 and coverage8(64) == 255 and weigh(255, 128) == 128


def main():
    selftest()
    text = generate()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            print("check-render-vectors: %s is not what tools/gen-render-vectors.py writes" % OUT, file=sys.stderr)
            return 1
        print("check-render-vectors: ok")
        return 0
    OUT.write_text(text)
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
