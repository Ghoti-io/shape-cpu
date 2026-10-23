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
"""Write the seed corpus of the render fuzzer, deterministically (story 16; CAP-15, AD-19).

    python3 tools/gen-fuzz-corpus.py            # write tests/fuzz/corpus/render/
    python3 tools/gen-fuzz-corpus.py --check    # fail if the committed files differ

Every file is an input of tests/fuzz/fuzz_render.c: five header bytes (the target's
format and size, the request, where the token is set, the sink's limits, the
allocation that fails), then the paths, the two glyph runs and the list, each read
from the bytes that follow.  The **biased** files draw the header from the values
that matter and the body from random bytes; the **plain** files are random bytes,
which the fuzzer reads as valid input by construction; the **scripted** files reach,
by construction, each outcome the replay requires.  The replay gate
(`make fuzz-replay`) fails if these files, together, never reach one of those
outcomes: that is what keeps the corpus from measuring nothing, and what to read if
the byte layout below drifts from the C file.
"""
import base64
import os
import sys

BIASED_FILES = 120
PLAIN_FILES = 40

# A coordinate byte below 12 picks the table in fuzz_render.c: 0 is 0.0, 2 is 0.5, 9 is 64.0.
Z, HALF, FULL = 0, 2, 9
# Header: the target byte's two low bits are the format (2 and 3 are float), then the width's and the height's
# index into {1, 7, 33, 64}.
T64 = (3 << 2) | (3 << 4)
STRICT, CALLER_MEMORY, EXTENT, DAMAGE, SHORT = 1, 2, 4, 8, 16


class Rng:
    def __init__(self, seed):
        self.s = (seed * 0x9E3779B97F4A7C15 + 777) & (2**64 - 1)

    def next(self):
        s = self.s
        s ^= s >> 12
        s ^= (s << 25) & (2**64 - 1)
        s ^= s >> 27
        self.s = s
        return (s * 2685821657736338717) & (2**64 - 1)

    def below(self, n):
        return self.next() % n

    def byte(self):
        return self.next() & 255


def element(verb, *coords):
    """One path element: six coordinate picks (unused ones too), then the verb byte (0 move, 1 line, 3 quad,
    4 cubic, 5 close)."""
    c = list(coords) + [Z] * (6 - len(coords))
    return bytes(c + [verb])


def square_and_curve():
    """Two paths: a square over the target, and a cubic bulge (a curve, so a small point cap is met)."""
    square = bytes([5]) + (element(0, HALF, HALF) + element(1, FULL, HALF) + element(1, FULL, FULL)
                           + element(1, HALF, FULL) + element(5))
    curve = bytes([3]) + element(0, HALF, FULL) + element(4, Z, Z, FULL, Z, FULL, FULL) + element(5)
    return bytes([1]) + square + curve  # the count byte: 1 + 1 = two paths


def runs():
    # For each of the two faces: three glyphs (id 1, at (0.5, 0.5) and further), size 16, hint mode FONT, three.
    one = bytes([1, HALF, HALF, 2, HALF, FULL, 3, FULL, HALF, 0, 0, 2])
    return one + one


def fill(pick, map_bytes=b"\x00", rule=0, gradient=None, style=b""):
    # op 0, the resource pick (0..: a path; 3 the fast run, 7 the general run), the map, a paint (rgba, then a byte:
    # 0 mod 4 Display P3, 1 mod 4 a gradient whose bytes follow, else the target's space), rule; a rule with bits 1
    # and 2 set is a stroke, and the first one is followed by the style's three bytes (STYLE below)
    if gradient is None:
        return bytes([0, pick]) + map_bytes + bytes([200, 100, 50, 255, 2, rule]) + style
    return bytes([0, pick]) + map_bytes + bytes([200, 100, 50, 255, 1]) + gradient + bytes([rule]) + style


def style(width=0, space=0, join=0, cap=0, degenerate=0, alignment=0, rule=0, dashes=0, scope=0, miter1=0, offset=0):
    """The three bytes fuzz_render.c's stroke style is made from: width index (0 one, 1 zero, 2 2.5, 3 six, 4 1/16,
    5 seventeen, 6 1.5, 7 three) and the width space; join, cap and degenerate; alignment, the alignment rule, the
    dash pattern (0 none, 1 [3, 2], 2 [1, 1], 3 [0, 2]), its scope, a miter limit of 1 and a dash offset of 1/2."""
    return bytes([width | space << 3, join | cap << 3 | degenerate << 5,
                  alignment | rule << 2 | dashes << 3 | scope << 5 | miter1 << 6 | offset << 7])


def stroke(pick, map_bytes=b"\x00", gradient=None, first=None):
    return fill(pick, map_bytes, rule=6, gradient=gradient, style=first if first is not None else b"")


def line_path():
    """A horizontal line along the top of the target, from x = -32 to 95.875 (the two-byte coordinates 12 0 and 15 255
    of fuzz_render.c's table), one row high at width 1: far wider than the target, so its rectangle is large against its
    few cell deposits and a stroke of it takes the sparse composite."""
    return bytes([2, 12, 0, HALF, 0, 0, 0, 0, 0, 15, 255, HALF, 0, 0, 0, 0, 1])


def paths_with_line():
    """Three paths: the square, the cubic bulge and the line (picks 0, 1 and 2)."""
    square = bytes([5]) + (element(0, HALF, HALF) + element(1, FULL, HALF) + element(1, FULL, FULL)
                           + element(1, HALF, FULL) + element(5))
    curve = bytes([3]) + element(0, HALF, FULL) + element(4, Z, Z, FULL, Z, FULL, FULL) + element(5)
    return bytes([2]) + square + curve + line_path()


def body_with_line(items):
    return paths_with_line() + runs() + bytes([len(items)]) + b"".join(items)


def gval(x):
    """The byte below 248 that fuzz_render.c's gradient_value reads as @p x (an integer or a half)."""
    for b in range(248):
        if ((b % 80) - 8) / (1.0 if b % 3 == 0 else 2.0) == x:
            return b
    raise ValueError(x)


# gradient_value's table past 247: 248 2^64, 251 one step past it, 252 one step below 2^-64.
PAST_BOUND = 251


def gradient(kind, values, extend=0, premultiplied=0, space=0, map_byte=0, stops=(), sub=0):
    """A gradient's bytes, in fuzz_render.c's gradient_of order: kind (0 linear, 1 radial, 2 sweep), six values,
    (linear: the isoline byte; sweep: the angle unit), extend, the premultiplied flag, the space byte (0 the
    target's, 1 linear sRGB, 2 a P3 gamma), the map byte (0 identity, 1 turned, 2 flat, 3 out of the domain), the
    stop count and each stop: an offset byte (k: k/8 - 1/4), a kind byte (mod 7: 5 foreground, 6 palette, else
    solid with alpha kind%5/4 and then two bytes of red and green)."""
    out = bytearray([kind]) + bytes(v if isinstance(v, int) and v >= 248 else gval(v) for v in values)
    if kind in (0, 2):
        out.append(sub)
    out += bytes([extend, premultiplied, space, map_byte, len(stops)])
    for offset_byte, kind_byte, red, green in stops:
        out += bytes([offset_byte, kind_byte])
        if kind_byte % 7 < 5:
            out += bytes([red, green])
    return bytes(out)


THREE_STOPS = ((2, 4, 250, 10), (6, 3, 20, 240), (10, 4, 10, 30))
LINEAR = gradient(0, (2, 0, 30, 0, 0, 0), stops=THREE_STOPS)
RADIAL = gradient(1, (20, 20, 4, 30, 25, 20), extend=1, premultiplied=1, space=1, stops=THREE_STOPS)
SWEEP = gradient(2, (32, 32, 10, 70, 0, 0), extend=2, space=2, map_byte=1, stops=THREE_STOPS + ((10, 5, 0, 0),),
                 sub=1)
DEGENERATE = gradient(0, (4, 4, 4, 4, 0, 0), stops=THREE_STOPS)
PAST = gradient(0, (PAST_BOUND, 0, 30, 0, 0, 0), stops=THREE_STOPS)
OUT_OF_DOMAIN = gradient(0, (2, 0, 30, 0, 0, 0), map_byte=3, stops=THREE_STOPS)


ROTATED = bytes([3, 20])  # map_of case 3: a rotation by 30 degrees about (32, 32)


def group(opacity=2, composite=0):
    # op 5, a pick (unused), opacity index (of 0..4 quarters), composite operator, blend-space pick (not 0 mod 3)
    return bytes([5, 0, opacity, composite, 1])


POP = bytes([6, 0])


def body(items):
    return square_and_curve() + runs() + bytes([len(items)]) + b"".join(items)


def header(target=T64, request=0, cancel=0, limit=0, fail=0):
    return bytes([target, request, cancel, limit, fail])


STANDARD = [fill(0), group(), fill(1, rule=1), fill(3), fill(7, ROTATED), group(3, 9), fill(0, ROTATED), POP, POP]


def scripted():
    """Inputs that reach, by construction, the outcomes that random bytes reach only by luck."""
    out = {}
    out["scripted-render.bin"] = header() + body(STANDARD)
    out["scripted-float.bin"] = header(target=T64 | 3) + body(STANDARD)
    out["scripted-point-cap.bin"] = header(limit=1) + body([fill(1)])  # max_points 2: the cubic is refused
    out["scripted-strict.bin"] = header(request=STRICT, limit=1) + body([fill(0), fill(1)])
    out["scripted-region.bin"] = header(limit=3) + body([fill(0)])  # max_dimension 1
    out["scripted-cancel.bin"] = header(cancel=1) + body([fill(0), fill(1), fill(0)])
    out["scripted-oom.bin"] = header(limit=128, fail=0) + body([fill(0)])
    out["scripted-oom-sink.bin"] = header(limit=128 | 64, fail=2) + body(STANDARD)
    out["scripted-caller-memory.bin"] = header(request=CALLER_MEMORY) + body(STANDARD)
    out["scripted-workspace-short.bin"] = header(request=CALLER_MEMORY | SHORT) + body(STANDARD)
    out["scripted-damage.bin"] = header(request=DAMAGE | EXTENT) + body(STANDARD)
    out["scripted-damage-float.bin"] = header(target=T64 | 2, request=DAMAGE | CALLER_MEMORY) + body(STANDARD)
    # Color-paint story P2: gradients of each kind, extend and space, under a group and a rotation; on a float target,
    # damaged and in caller memory; one that draws nothing (a notice), one past G14's bounds (refused at intern, drawn
    # solid), one mapped out of the sampling domain (refused while drawing, the same with a damage).
    grads = [fill(0, gradient=LINEAR), group(), fill(1, ROTATED, gradient=RADIAL), fill(7, gradient=SWEEP), POP,
             fill(0, ROTATED, gradient=SWEEP)]
    out["scripted-gradients.bin"] = header() + body(grads)
    out["scripted-gradients-float-damage.bin"] = header(target=T64 | 3, request=DAMAGE | CALLER_MEMORY) + body(grads)
    out["scripted-gradient-notice.bin"] = header(request=STRICT) + body([fill(0), fill(1, gradient=DEGENERATE)])
    out["scripted-gradient-past-bound.bin"] = header() + body([fill(0, gradient=PAST)])
    out["scripted-gradient-domain.bin"] = header(request=DAMAGE) + body([fill(1), fill(0, gradient=OUT_OF_DOMAIN)])
    # N1 (Corey, 2026-10-21): a solid paint byte of 2 mod 4 with its high bit set takes a component (bit 6: green) at
    # 2^16 or one step past it ((b >> 4) & 3: 0 and 1 at it, 2 and 3 past it) in gamma 10 (bit 3) or gamma 0.1; a
    # group's blend byte of 1 mod 3 with bit 6 set blends in gamma 0.1.  At the bound into a gamma-0.1 group and onto a
    # float target, and one step past it (refused, the render goes on).
    def bound_fill(pick, b):
        return bytes([0, pick]) + b"\x00" + bytes([200, 100, 50, 255, b, 0])
    gamma01_group = bytes([5, 0, 4, 0, 0x40])  # 64 % 3 == 1, bit 6 set
    bound = [bound_fill(0, 0x80 | 0x08 | 2), gamma01_group, bound_fill(1, 0x80 | 0x10 | 2), POP,
             bound_fill(0, 0x80 | 0x20 | 2), bound_fill(1, 0x80 | 0x40 | 0x30 | 0x08 | 2)]
    out["scripted-color-bound.bin"] = header() + body(bound)
    out["scripted-color-bound-float.bin"] = header(target=T64 | 3) + body(bound)
    # Color-paint story P3: a group's composite byte is the operator modulo 28 (13 to 27 the blend modes), and its
    # blend byte with bits 7 and 5 set (0xA0) makes it knockout, with bit 7 alone (0x80) non-isolated: shape-cpu
    # refuses both (S19).  Each blend mode over the standard backdrop, on an 8-bit and a float target, and the two
    # refused groups.
    blends = [fill(0)]
    for mode in range(13, 28):
        blends += [bytes([5, 0, 3 + mode % 2, mode, 1]), fill(1 if mode % 3 else 0), POP]
    out["scripted-blend-modes.bin"] = header() + body(blends)
    out["scripted-blend-modes-float.bin"] = header(target=T64 | 3) + body(blends)
    pdf = [fill(0), bytes([5, 0, 4, 23, 0xA1]), fill(1), POP, bytes([5, 0, 4, 0, 0x81]), fill(0), POP]
    out["scripted-pdf-refused.bin"] = header() + body(pdf)
    # Strokes stories S1, S4 and S5: a fill whose rule byte has bits 1 and 2 set is a stroke of the shape instead, drawn by
    # expansion; one of a glyph run is drawn as the stroke of the path its glyphs make (ST1, ST8), unhinted whatever the
    # run asks, and the harness draws the same list with each such centerline replaced by that path and requires the same
    # bytes (ST11).  Under strict a render with no refusal is the same render.
    run_stroke = [fill(0), stroke(3, first=style(width=2, join=1, cap=1))]
    out["scripted-stroke-run.bin"] = header() + body_with_line(run_stroke)
    out["scripted-stroke-run-strict.bin"] = header(request=STRICT) + body_with_line(run_stroke)
    # The general run turned, the same run stroked twice (the second from the cache), an aligned stroke of a run, dashes of
    # both scopes, and a run stroke under a gradient.
    out["scripted-stroke-run-turned.bin"] = header() + body_with_line([fill(0), stroke(7, ROTATED, first=style(width=6, join=1))])
    out["scripted-stroke-run-cached.bin"] = header() + body_with_line([stroke(3, first=style(width=2)), stroke(3), stroke(3, ROTATED)])
    out["scripted-stroke-run-aligned.bin"] = header() + body_with_line([stroke(7, first=style(width=2, alignment=1))])
    out["scripted-stroke-run-outside.bin"] = header() + body_with_line([stroke(3, first=style(width=6, alignment=2, rule=1))])
    out["scripted-stroke-run-dashed.bin"] = header() + body_with_line([stroke(7, first=style(width=7, cap=1, dashes=1, scope=1, offset=1))])
    out["scripted-stroke-run-caller-memory.bin"] = header(request=CALLER_MEMORY) + body_with_line(run_stroke)
    out["scripted-stroke-run-float.bin"] = header(target=T64 | 3) + body_with_line(run_stroke)
    # A plain stroke of each path under a rotation and a group, a hairline and a plain sparse stroke of the line, each
    # join and cap, dashes of both scopes, every alignment (inside, outside, even-odd) over the closed square and the
    # open line, a gradient stroke, a zero width (a notice), and the same on a float target, damaged, in caller memory.
    plain = [fill(0), stroke(0, first=style(width=3)), group(), stroke(1, ROTATED), stroke(2), POP]
    out["scripted-stroke-plain.bin"] = header() + body_with_line(plain)
    out["scripted-stroke-float.bin"] = header(target=T64 | 3) + body_with_line(plain)
    out["scripted-stroke-damage.bin"] = header(request=DAMAGE | EXTENT) + body_with_line(plain)
    out["scripted-stroke-caller-memory.bin"] = header(request=CALLER_MEMORY) + body_with_line(plain)
    out["scripted-stroke-damage-float-memory.bin"] = header(target=T64 | 2, request=DAMAGE | CALLER_MEMORY) \
        + body_with_line(plain)
    out["scripted-stroke-hairline.bin"] = header() + body_with_line(
        [fill(0), stroke(2, first=style(space=1)), stroke(1), stroke(2, ROTATED)])
    out["scripted-stroke-sparse.bin"] = header() + body_with_line([stroke(2, first=style()), stroke(2)])
    out["scripted-stroke-joins.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=3, join=1, cap=2)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-bevel-arcs.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=5, join=4, cap=1, miter1=1)), stroke(1)])
    out["scripted-stroke-dashes.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=2, dashes=1, cap=1, offset=1)), stroke(1), stroke(2)])
    out["scripted-stroke-dash-path.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=2, dashes=3, scope=1, cap=1)), stroke(1), stroke(2, ROTATED)])
    out["scripted-stroke-inside.bin"] = header() + body_with_line(
        [fill(0), stroke(0, first=style(width=3, alignment=1)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-outside-even-odd.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=3, alignment=2, rule=1)), stroke(1), group(3, 9), stroke(0), POP])
    out["scripted-stroke-aligned-caller-memory.bin"] = header(request=CALLER_MEMORY | DAMAGE) + body_with_line(
        [stroke(0, first=style(width=3, alignment=1)), stroke(1, ROTATED)])
    out["scripted-stroke-aligned-float.bin"] = header(target=T64 | 3) + body_with_line(
        [stroke(0, first=style(width=3, alignment=2)), stroke(1, ROTATED)])
    out["scripted-stroke-gradient.bin"] = header() + body_with_line(
        [stroke(0, gradient=LINEAR, first=style(width=3)), stroke(1, ROTATED, gradient=RADIAL),
         stroke(2, gradient=SWEEP)])
    out["scripted-stroke-zero-width.bin"] = header(request=STRICT) + body_with_line(
        [fill(0), stroke(0, first=style(width=1)), stroke(1)])
    # Strokes story S4b (UC1 to UC12): outlines whose pieces overlap in a pixel, so that the exact step runs: dashes with short
    # gaps and square caps, a wide stroke over a tight polyline, an aligned stroke, damage and caller memory over each, and
    # the pieces of a pixel capped small (the sink's own limit, whole or damaged).
    out["scripted-stroke-overlap-dashes.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2, join=1)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-overlap-wide.bin"] = header(target=T64 | 3) + body_with_line(
        [stroke(0, first=style(width=5, join=0, cap=1)), stroke(1, ROTATED), stroke(2, ROTATED)])
    out["scripted-stroke-overlap-aligned.bin"] = header(request=DAMAGE | CALLER_MEMORY) + body_with_line(
        [fill(0), stroke(0, first=style(width=3, alignment=1, join=2)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-overlap-aligned-outside.bin"] = header() + body_with_line(
        [stroke(0, first=style(width=2, alignment=2, rule=1, dashes=2, cap=2)), stroke(1), stroke(2, ROTATED)])
    out["scripted-stroke-exact-cap.bin"] = header(limit=5 | 8) + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2)), stroke(1)])
    out["scripted-stroke-exact-cap-damage.bin"] = header(request=DAMAGE, limit=5 | 16) + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2)), stroke(1, ROTATED)])
    out["scripted-stroke-exact-cap-strict.bin"] = header(request=STRICT, limit=5) + body_with_line(
        [stroke(0, first=style(width=5, join=0)), stroke(1)])
    # Strokes story S4c (UC19): the crossings budget of one pixel lowered (limit mode 6, a table of 1 to 1024 selected by the
    # bits above the mode), and with the piece cap (mode 7, the budget's index in the cancel byte's high bits).  A render that
    # refuses no item for the sink's limit must draw the bytes the default limits draw (the harness's wide render).
    out["scripted-stroke-budget-refused.bin"] = header(limit=6) + body_with_line(
        [stroke(0, first=style(width=5, join=0, cap=1)), stroke(1, ROTATED), stroke(2, ROTATED)])
    out["scripted-stroke-budget-refused-strict.bin"] = header(request=STRICT, limit=6 | 8) + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2, join=1)), stroke(1)])
    out["scripted-stroke-budget-refused-damage.bin"] = header(request=DAMAGE, limit=6 | 16) + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2)), stroke(1, ROTATED)])
    out["scripted-stroke-budget-same.bin"] = header(limit=6 | (11 << 3)) + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2, join=1)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-budget-same-damage.bin"] = header(request=DAMAGE | CALLER_MEMORY, limit=6 | (9 << 3)) + body_with_line(
        [fill(0), stroke(0, first=style(width=3, alignment=1, join=2)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-both-limits.bin"] = header(cancel=9 << 3, limit=7 | (20 << 3)) + body_with_line(
        [stroke(0, first=style(width=3, dashes=2, cap=2, join=1)), stroke(1, ROTATED), stroke(2)])
    out["scripted-stroke-point-cap.bin"] = header(limit=1) + body_with_line([stroke(1, first=style(width=3))])
    out["scripted-stroke-cell-cap.bin"] = header(limit=2 | 16) + body_with_line(
        [stroke(0, first=style(width=3)), stroke(1)])
    out["scripted-stroke-cell-cap-damage.bin"] = header(request=DAMAGE, limit=2 | 24) + body_with_line(
        [stroke(0, first=style(width=3, alignment=1)), stroke(1)])
    out["scripted-stroke-strict-cap.bin"] = header(request=STRICT, limit=1) + body_with_line(
        [stroke(1, first=style(width=3)), fill(0)])
    # Found by the first libFuzzer soak (story 16): strict, a damage and a small cell cap. The damage-limited render
    # passed and the whole render met the cap outside the damage, so it was refused. Since 2026-10-21 the cap is met
    # over the whole render's region whatever the damage (S4), and the harness requires both to agree.
    out["regression-strict-cells-outside-damage.bin"] = base64.b64decode(
        "cQsAMplAJm0ZT3ZmwR6Jf2Fd7lDCRLR/QSt6/zbm6uHj7bR6JLZqECw6JO+wOwclMCTHsAvBvaXFi6p+svmrTJ+KgxAuG0woagxRNjJeiGcC"
        "myM9FiyKlJxCJFFWzIkK8SvFqHMplT7Bdyd9VFOqgVUfzMjmgcV+F/qsYJOfKS/fHjRpgYuTPZ9DGeivfs7ibhEb5CtwDwGioZomrmEA7NLX"
        "H5rRQNlNKpMl8Og98ySqsAWGgpW+WBAM1grAFdQqD5fiB4thMQSas5I3pmv8sPE=")
    # Found by the second soak: a cell cap met in the whole render and not in the damage-limited one, so an item was
    # refused in one and drawn in the other and the pixels inside the damage differed (Implementation Notes of story
    # 16). Fixed 2026-10-21 (S4); the harness compares inside the damage with no tolerance for it.
    out["regression-cells-damage-differs.bin"] = base64.b64decode(
        "9BgAWsyop3vto1FSswDDWgVM3Wc837Z/Bhr8trLLWZ+T+zR4Kv4uM8LbHINihtt8J+Yff5kQmyh+3NASYQKDo4hSD1YkaL2jnZauWqEjRcW5S+MV"
        "TYEs5O3+VcU8cwKp97YVqjP+tkKYlPe40dOONqAWqnu2fwYa/Layy1mfk/s0eCr+LjPCg2KG23wn5h9/mb/Zk6LFN3COUCe4RAEAAAAAAAAzLrJp"
        "7erJLLbxT2DcaRiDr3FfL5muk266bM1ykol1iyREAQAAAAAAAClFIsW5EQNQoh+EIbRdySMraXA2")
    return out


def biased(seed):
    rng = Rng(seed)
    target = rng.byte()
    request = rng.byte() & 0x1F
    cancel = rng.below(4) if rng.below(4) == 0 else 0
    limit = [0, 0, 0, 1, 2, 3, 4][rng.below(7)] | (rng.below(16) << 3)
    if rng.below(10) == 0:
        limit |= 128 | (64 if rng.below(2) else 0)
    out = bytearray(header(target, request, cancel, limit & 255, rng.byte()))
    out += bytes(rng.byte() for _ in range(40 + rng.below(400)))
    return bytes(out)


def plain(seed):
    rng = Rng(1000 + seed)
    return bytes(rng.byte() for _ in range(5 + 50 + rng.below(500)))


def files():
    out = {}
    for i in range(BIASED_FILES):
        out["biased-%03d.bin" % i] = biased(i + 1)
    for i in range(PLAIN_FILES):
        out["plain-%03d.bin" % i] = plain(i + 1)
    out.update(scripted())
    return out


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fuzz", "corpus", "render")
    want = files()
    if "--check" in sys.argv:
        have = sorted(os.listdir(root)) if os.path.isdir(root) else []
        if have != sorted(want):
            sys.exit("gen-fuzz-corpus: the committed corpus is not the generator's set of files")
        for name, data in want.items():
            with open(os.path.join(root, name), "rb") as f:
                if f.read() != data:
                    sys.exit("gen-fuzz-corpus: %s differs from the generator's" % name)
        print("gen-fuzz-corpus: the committed corpus (%d files) is current" % len(want))
        return
    os.makedirs(root, exist_ok=True)
    for name, data in want.items():
        with open(os.path.join(root, name), "wb") as f:
            f.write(data)
    print("gen-fuzz-corpus: wrote %d files" % len(want))


if __name__ == "__main__":
    main()
