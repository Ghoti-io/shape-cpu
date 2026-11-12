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
BIASED_MASK_FILES = 60
BIASED_PATTERN_FILES = 80
PLAIN_FILES = 40

# A coordinate byte below 12 picks the table in fuzz_render.c: 0 is 0.0, 2 is 0.5, 9 is 64.0.
Z, HALF, FULL = 0, 2, 9
# Header: the target byte's two low bits are the format (2 and 3 are float), then the width's and the height's
# index into {1, 7, 33, 64}.
T64 = (3 << 2) | (3 << 4)
STRICT, CALLER_MEMORY, EXTENT, DAMAGE, SHORT = 1, 2, 4, 8, 16
MASKS = 128  # the request byte's bit 7: a group whose blend byte has bits 7 and 5 as 0 and 1 takes a soft mask


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


def image(w, h, layout=2, bits=0, premultiplied=0, space=0, samples=None, window=0, modes=0, filter_space=0, map_kind=0,
          tx=64, ty=64, tint=0):
    """An image paint's bytes, in fuzz_render.c's image_paint_of order: the size ((w - 1) | (h - 1) << 3), the format (the
    layout: 0 gray, 1 RGB, 2 RGBA, 3 alpha-only gray; the depth: 0 8 bits, 1 12, 2 16, 3 a float; premultiplied; the image's
    space), the samples (a byte each, `w h layout` of them), the window byte (1 mod 3 a sub-window), the modes byte (x extend,
    y extend, magnify and minify filter, two bits each), the filter space, the map (0 identity, 1 x4.5, 2 x0.37, 3 turned, 4
    singular, 5 far past the domain, 6 anisotropic, 7 a quarter turn), a translation, and the tint's byte (an alpha-only
    image's: 0 mod 3 solid, 1 foreground, 2 palette)."""
    count = w * h * (1 if layout == 3 else (1, 3, 4)[layout] if layout < 3 else 1)
    count = w * h * (1, 3, 4, 1)[layout]
    if samples is None:
        samples = bytes((i * 37 + 11) % 256 for i in range(count))
    return (bytes([(w - 1) | (h - 1) << 3, layout | bits << 2 | premultiplied << 4 | space << 5]) + bytes(samples)
            + bytes([window, modes, filter_space, map_kind, tx, ty, tint]))


def image_fill(pick, picture, map_bytes=b"\x00", rule=0, b=0xF7, style=b""):
    """A fill whose paint byte (0xF0 or more, 3 mod 4) takes an image paint; the multiplier is b % 5 quarters."""
    return bytes([0, pick]) + map_bytes + bytes([200, 100, 50, 255, b]) + picture + bytes([rule]) + style


def modes(ex=0, ey=0, magnify=1, minify=1):
    return ex | ey << 2 | magnify << 4 | minify << 6


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


def mask_bytes(fills=(), shape=0, mode=0, transfer=0, spaces=0, samples=b"", placement=0):
    """A soft mask's bytes, in fuzz_render.c's mask_of order after a group's five: the shape byte (1 + shape % 3 fills; bit 5 a
    masked group of its own around the first, bit 6 a clip around the last), the mode byte (bit 0 luminance, bits 1 and 2 the
    coefficient set, bit 3 acts on the shape, bits 4 to 6 the backdrop), the transfer byte (mod 4: 0 identity, 1 exponential,
    2 a table of 2 + (b >> 2) % 3 samples, 3 a stitching), the spaces byte (the picture's space low, the luminance's high), each
    fill (its path pick, map, paint and rule), a table's samples, and then the group's placement byte."""
    body = b"".join(fills)
    return bytes([shape, mode, transfer, spaces]) + body + samples + bytes([placement])


def inner_fill(pick, map_bytes=b"\x00", rule=0, gradient=None):
    """One fill of a mask's picture: a record's fill without its op byte."""
    return fill(pick, map_bytes, rule=rule, gradient=gradient)[1:]


def masked_group(mask, opacity=3, composite=0):
    """A group item whose blend byte (0x20) takes a mask, and the mask's bytes."""
    return bytes([5, 0, opacity, composite, 0x20]) + mask


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
    # Masks-images-patterns story I3: a paint byte of 0xF0 or more and 3 mod 4 takes an image paint: filters of every kind
    # (the third and fourth bits of a mode byte pair are the magnify and minify), every edge mode, windows, a filter space,
    # formats from 8-bit gray to a float RGBA, and maps that magnify, minify, turn and quarter turn.  On both targets, damaged
    # and in caller memory (the harness requires the same bytes in each, and with every fast tier off).
    imgs = [image_fill(0, image(5, 4, 2, 0, modes=modes(0, 1, 0, 1), map_kind=1)),
            image_fill(1, image(6, 5, 1, 1, modes=modes(2, 3, 2, 3), map_kind=3, filter_space=1), ROTATED),
            group(),
            image_fill(0, image(3, 3, 0, 2, modes=modes(1, 1, 1, 3), map_kind=2, tx=100, ty=40), b=0xFB),
            image_fill(1, image(4, 4, 2, 3, premultiplied=1, space=2, modes=modes(3, 0, 1, 1), map_kind=6, window=1 + 3 * 5)),
            POP,
            image_fill(0, image(6, 6, 2, 0, modes=modes(0, 0, 3, 3), map_kind=7, filter_space=3), b=0xF3)]
    out["scripted-images.bin"] = header() + body(imgs)
    out["scripted-images-float.bin"] = header(target=T64 | 3) + body(imgs)
    out["scripted-images-damage.bin"] = header(request=DAMAGE | CALLER_MEMORY) + body(imgs)
    out["scripted-images-damage-float.bin"] = header(target=T64 | 2, request=DAMAGE | CALLER_MEMORY) + body(imgs)
    # AREA under every edge mode, minified (a scale of 0.37) and at the unit scale: the exact mean, and the work limits
    # lowered (the limit byte's mode 0 with its high bit set: a pixel's visits from the bits above the mode): refused at 0x80
    # (49 visits a pixel; a scale of 0.37 needs more), drawn at 0xF8 (94; a unit scale needs 71).
    areas = [image_fill(0, image(5, 4, 2, 0, modes=modes(e, 3 - e, 3, 3), map_kind=2 if e % 2 else 0)) for e in range(4)]
    out["scripted-image-area.bin"] = header() + body(areas)
    out["scripted-image-area-limited.bin"] = header(limit=0x80) + body(areas)
    out["scripted-image-area-limited-damage.bin"] = header(request=DAMAGE, limit=0x80) + body(areas)
    out["scripted-image-area-limited-strict.bin"] = header(request=STRICT, limit=0x80) + body(areas)
    out["scripted-image-area-budget.bin"] = header(limit=0xF8) + body([image_fill(0, image(5, 4, 2, 0, modes=modes(1, 1, 3, 3), map_kind=0))])
    # A map far past the sampling domain is refused while drawing, the same with a damage; a singular one is a notice.
    out["scripted-image-domain.bin"] = header(request=DAMAGE) + body([fill(1), image_fill(0, image(3, 3, 2, 0, map_kind=5))])
    out["scripted-image-notice.bin"] = header(request=STRICT) + body([fill(0), image_fill(1, image(3, 3, 2, 0, map_kind=4))])
    # An alpha-only image with a solid, a foreground and a palette tint (the list has no palette: the third is refused by the plan).
    stencils = [image_fill(0, image(4, 3, 3, 0, modes=modes(1, 0, 1, 3), map_kind=1, tint=0)),
                image_fill(1, image(5, 5, 3, 3, modes=modes(2, 2, 0, 3), map_kind=3, tint=1), ROTATED),
                image_fill(0, image(3, 2, 3, 1, modes=modes(0, 0, 2, 3), map_kind=1, tint=2))]
    out["scripted-image-stencils.bin"] = header() + body(stencils)
    out["scripted-image-stencils-float.bin"] = header(target=T64 | 3, request=CALLER_MEMORY) + body(stencils)
    # A stroke painted with an image (the paint is sampled in the stroke's local space), with its style, and again from the cache.
    out["scripted-image-stroke.bin"] = header() + body_with_line(
        [image_fill(0, image(6, 4, 2, 0, modes=modes(1, 1, 1, 3), map_kind=1), rule=6, style=style(width=3, join=1, cap=1)),
         image_fill(1, image(3, 3, 1, 0, modes=modes(0, 0, 0, 0), map_kind=0), ROTATED, rule=6), fill(0)])
    out["scripted-image-stroke-float.bin"] = header(target=T64 | 3, request=DAMAGE) + body_with_line(
        [image_fill(2, image(4, 4, 2, 1, modes=modes(2, 2, 1, 3), map_kind=3), rule=6, style=style(width=2))])
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
    # Color-paint story P3: a group's composite byte is the operator modulo 28 (13 to 27 the blend modes).  Each blend mode over the
    # standard backdrop, on an 8-bit and a float target.
    blends = [fill(0)]
    for mode in range(13, 28):
        blends += [bytes([5, 0, 3 + mode % 2, mode, 1]), fill(1 if mode % 3 else 0), POP]
    out["scripted-blend-modes.bin"] = header() + body(blends)
    out["scripted-blend-modes-float.bin"] = header(target=T64 | 3) + body(blends)
    # Masks-images-patterns story M3: a group's blend byte with bit 7 set is general (KC2): bit 5 then makes it knockout, bit 4 beside
    # it a knockout group that is also non-isolated, and neither a non-isolated one; bit 3 gives it a soft mask when the request
    # byte takes masks.  A non-isolated group with a blend mode over the backdrop and a knockout group of translucent shapes, on
    # both targets; nested, so that a group alpha and a group shape are carried in two layers (a knockout in a non-isolated group in a
    # knockout group); a stroke in a knockout group; a masked non-isolated and a masked knockout group; each damaged and in caller
    # memory.
    def general_group(blend, opacity=2, composite=0):
        return bytes([5, 0, opacity, composite, blend])
    ni = 0x81
    ko = 0xA1
    both = 0xB1
    flat = [fill(0), general_group(ni, 3, 23), fill(1), fill(3), POP, general_group(ko), fill(1), fill(7, ROTATED), POP]
    out["scripted-general.bin"] = header() + body(flat)
    out["scripted-general-float.bin"] = header(target=T64 | 3) + body(flat)
    nested = [fill(0), general_group(ko, 3), fill(3), general_group(ni, 3, 21), fill(1), general_group(ko, 2, 24), fill(7, ROTATED), POP,
              general_group(both, 4), fill(0, ROTATED), POP, POP, general_group(0x81, 3, 1), fill(1), POP, POP]
    out["scripted-general-nested.bin"] = header() + body(nested)
    out["scripted-general-nested-memory.bin"] = header(target=T64 | 2, request=CALLER_MEMORY | DAMAGE) + body(nested)
    out["scripted-general-damage.bin"] = header(request=DAMAGE | EXTENT) + body(flat)
    out["scripted-general-strict.bin"] = header(request=STRICT) + body(nested)
    gstroke = [fill(0), general_group(ko, 3), stroke(3, first=style(width=3)), fill(1), stroke(7, ROTATED), POP,
               general_group(ni, 3, 23), stroke(3), POP]
    out["scripted-general-stroke.bin"] = header() + body_with_line(gstroke)
    out["scripted-general-stroke-memory.bin"] = header(request=CALLER_MEMORY | DAMAGE) + body_with_line(gstroke)
    gmask = [fill(0), masked_group(mask_bytes([inner_fill(1)], mode=0)), fill(3), POP]
    gmask_ni = bytes([5, 0, 3, 0, 0x89]) + mask_bytes([inner_fill(1)], mode=1)
    gmask_ko = bytes([5, 0, 3, 23, 0xA9]) + mask_bytes([inner_fill(3)], mode=0x09, transfer=1)
    masked_general = [fill(0), gmask_ni, fill(1), fill(3), POP, gmask_ko, fill(1), fill(7, ROTATED), POP, gmask[1], fill(3), POP]
    out["scripted-general-masked.bin"] = header(request=MASKS) + body(masked_general)
    out["scripted-general-masked-memory.bin"] = header(target=T64 | 3, request=MASKS | CALLER_MEMORY | DAMAGE) + body(masked_general)
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
    # Found by mutating the image seeds (story I3): under a tightened crossings budget (limit mode 6) the sparse stroke tier painted
    # the bands before the one that met the budget, and the item was then refused, so it was left part drawn; the general tier
    # refuses before it paints.  A stroke whose outline could pass the pieces or crossings limit now takes the general tier.
    # Masks-images-patterns story M2: a request byte with bit 7 set lets a group take a soft mask (MC1 to MC10).  An alpha mask of the
    # curve over the square (the mask covers part of the group: the outside value, FT-M1, and an identity transfer, FT-M2), a
    # luminance mask under a table transfer in linear light, a stitching, a mask on the shape, a mask of a mask, a gradient in
    # a mask's picture, each on an 8-bit and a float target, in caller memory and damaged.
    alpha = masked_group(mask_bytes([inner_fill(1)]))
    luminance = masked_group(mask_bytes([inner_fill(0), inner_fill(1, ROTATED)], shape=1, mode=1 | 2 << 1 | 3 << 4, transfer=2 | 1 << 2,
                                       spaces=1 | 3 << 4, samples=bytes([20, 200, 90]), placement=1))
    shape_mask = masked_group(mask_bytes([inner_fill(1)], mode=8, transfer=1 | 2 << 2 | 1 << 4 | 2 << 6, placement=2), composite=6)
    stitched = masked_group(mask_bytes([inner_fill(0, rule=1)], mode=1, transfer=3 | 1 << 2, spaces=2, placement=3), opacity=2, composite=23)
    nested = masked_group(mask_bytes([inner_fill(0)] + [mask_bytes([inner_fill(1)])[:-1]], shape=0x21, placement=0))
    graded = masked_group(mask_bytes([inner_fill(0, gradient=LINEAR)], placement=4))
    for name, group_bytes in (("alpha", alpha), ("luminance", luminance), ("shape", shape_mask), ("stitching", stitched),
                              ("nested", nested), ("gradient", graded)):
        items = [fill(0), group_bytes, fill(1), POP]
        out["scripted-mask-%s.bin" % name] = header(request=MASKS) + body(items)
        out["scripted-mask-%s-float.bin" % name] = header(target=T64 | 3, request=MASKS) + body(items)
        out["scripted-mask-%s-memory-damage.bin" % name] = header(request=MASKS | CALLER_MEMORY | DAMAGE) + body(items)
    # A mask's picture holds a fill the sink refuses while drawing (an image paint mapped far past the sampling domain, IC9), whole
    # and damaged and strict: the same refusal with the chain [the group's push, the item], the mask lacking the item (MC6).
    refused = [fill(0), masked_group(mask_bytes([image_fill(0, image(3, 3, 2, 0, map_kind=5))[1:]])), fill(0), POP]
    out["scripted-mask-refused.bin"] = header(request=MASKS) + body(refused)
    out["scripted-mask-refused-damage.bin"] = header(request=MASKS | DAMAGE) + body(refused)
    out["scripted-mask-refused-strict.bin"] = header(request=MASKS | STRICT) + body(refused)
    out["scripted-mask-cancel.bin"] = header(request=MASKS, cancel=3) + body([fill(0), alpha, fill(1), POP, fill(0)])
    # Masks-images-patterns story R2: a fill whose rule byte is 0xF8 or more takes a pattern from the bytes that follow it (fuzz_render.c,
    # pattern_of): the configuration (1 + cfg % 3 tile fills; bit 4 a stroke first, bit 5 a group, bit 6 a nested pattern, bit 7 a clip;
    # cfg % 5 == 0 abuts in x and cfg % 7 == 0 in y), the cell (sizes index of geom and geom >> 3), the steps (steps table indexes of the maps
    # byte's two nibbles), the map and tint (tint % 8: 1 translates by a fraction, 2 by whole pixels, 5 an integer turn, 6 far away, 7 is
    # singular; bits 4 and 5 both set a tint, bits 6 and 7 stop repeating), then each tile fill's path pick, map, paint and rule.
    def tile_fill(pick=0, rule=0):
        return bytes([pick]) + b"\x00" + bytes([200, 100, 50, 255, 2, rule])

    def pattern_fill(cfg, geom, maps, tint, tile, pick=0, rule=0xF8, alpha=0):
        return fill(pick, rule=rule | alpha, style=bytes([cfg, geom, maps, tint]) + tile)

    cell4 = 3 | 3 << 3  # a cell of 4 by 4
    lattices = {
        "period": pattern_fill(0, cell4, 0, 1, tile_fill(0)),                               # abutting, whole-pixel steps, a fraction
        "period-pixels": pattern_fill(0, cell4, 0, 2, tile_fill(0)),                       # ... moved by whole pixels
        "period-turn": pattern_fill(0, 1 | 1 << 3, 0, 5, tile_fill(0)),                   # an integer turn: steps (3, 4) and (-4, 3)
        "period-shear": pattern_fill(0, cell4, 0, 4, tile_fill(0)),                        # an integer shear
        "overlap": pattern_fill(1, cell4, 2 | 2 << 4, 1, tile_fill(0) + tile_fill(1)),     # cells of 4 in steps of 3
        "gapped": pattern_fill(2, 1 | 1 << 3, 5 | 5 << 4, 2, tile_fill(0) + tile_fill(1) + tile_fill(0)),   # cells of 2 in steps of 6
        "stroke": pattern_fill(0x10, cell4, 0, 1, tile_fill(2) + tile_fill(0)),             # a stroke in the tile
        "group": pattern_fill(0x21, cell4, 0, 1, tile_fill(0, 4) + tile_fill(1)),          # a multiply group in the tile
        "clip": pattern_fill(0x80, cell4, 0, 2, tile_fill(0) + tile_fill(1)),              # a clip around the last
        # CU21: a clip around the last fill whose rule byte is 0xC0 or more is a union of the path and one or two others (bits 5, 1 and 0xC0)
        "tile-union": pattern_fill(0x82, cell4, 0, 2, tile_fill(0) + tile_fill(1, rule=0xC0)),            # abutting cells, a fraction moved by whole pixels
        "tile-union-three": pattern_fill(0x83, cell4, 0, 1, tile_fill(1) + tile_fill(0) + tile_fill(0, rule=0xE2)),    # three members, the second even-odd
        "tile-union-overlap": pattern_fill(0x84, cell4, 2 | 2 << 4, 1, tile_fill(1, rule=0xC4)),  # cells of 4 in steps of 3
        "tint": pattern_fill(0, cell4, 0, 0x31, tile_fill(0)),                              # uncolored
        "once": pattern_fill(0, cell4, 0, 0x41, tile_fill(0)),                              # repeat_x 0
        "far": pattern_fill(0, cell4, 0, 6, tile_fill(0)),                                  # a lattice far from the target
        "singular": pattern_fill(0, cell4, 0, 7, tile_fill(0)),                             # a singular map
        # a tile whose first fill takes a pattern of its own (cfg bit 6): that fill's rule byte is followed by the inner pattern's bytes
        "nested": pattern_fill(0x40, cell4, 0, 1, tile_fill(0) + bytes([0, 1 | 1 << 3, 0, 1]) + tile_fill(0) + tile_fill(1), alpha=2),
    }
    for name, item in lattices.items():
        items = [fill(0), item, fill(1)]
        out["scripted-pattern-%s.bin" % name] = header(request=0) + body_with_line(items)
        out["scripted-pattern-%s-float.bin" % name] = header(target=T64 | 3) + body_with_line(items)
        out["scripted-pattern-%s-memory-damage.bin" % name] = header(request=CALLER_MEMORY | DAMAGE) + body_with_line(items)
    # Masks-images-patterns story H1: a fill whose rule byte is 0xF0 to 0xF5 (0xF6 and 0xF7 are strokes) takes a hatch from the bytes that
    # follow it (fuzz_render.c, hatch_of): a head byte (nesting is head % 3, the families 1 + (head >> 2) % 2), per family an angle byte
    # (index into the angle table, the origin in its next bits) and a byte of the along-offset index (bits 0, 1), the spacing index (bits 2
    # to 4) and the dash pattern (bits 4 to 7, % 7), then the pen byte (width index, width space, cap, join) and the map byte (kind 0
    # identity, 1 stretch, 2 rotation, 3 singular).
    def hatch_fill(head, families, pen, mp, pick=0, rule=0xF0):
        body_bytes = bytes([head])
        for a, b in families:
            body_bytes += bytes([a, b])
        return fill(pick, rule=rule, style=body_bytes + bytes([pen, mp]))

    hatches = {
        "families": hatch_fill(4, [(1, 4), (2, 0x3C)], 0, 0),            # a vertical solid family and a dashed one at 45 degrees
        "nesting": hatch_fill(1, [(2, 0x3C)], 0, 0),                      # the outer nesting
        "dots": hatch_fill(0, [(0, 0x64)], 0x20, 0),                      # dots under a square cap
        "stretch": hatch_fill(0, [(3, 0x08 | 0x04)], 0, 1),               # a stretched hatch map
        "rotated": hatch_fill(0, [(2, 0x2C)], 0x10, 2),                   # a rotated hatch map under a round cap
        "no-width": hatch_fill(0, [(2, 0x04)], 1, 0),                     # a pen of width 0: noticed
        "singular": hatch_fill(0, [(2, 0x04)], 0, 3),                     # a singular hatch map: noticed
        "no-spacing": hatch_fill(0, [(2, 0x08)], 0, 0),                   # a spacing of 0: noticed
    }
    for name, item in hatches.items():
        items = [fill(0), item, fill(1)]
        out["scripted-hatch-%s.bin" % name] = header(request=0) + body_with_line(items)
        out["scripted-hatch-%s-float.bin" % name] = header(target=T64 | 3) + body_with_line(items)
        out["scripted-hatch-%s-memory-damage.bin" % name] = header(request=CALLER_MEMORY | DAMAGE) + body_with_line(items)
    out["scripted-hatch-cached.bin"] = header(request=0) + body_with_line([fill(0), hatches["families"], hatches["families"], fill(1)])
    out["scripted-hatch-lines-limit.bin"] = header(limit=0x40) + body_with_line([fill(0), hatches["families"], fill(1)])
    out["scripted-hatch-lines-limit-strict.bin"] = header(request=STRICT, limit=0x40 | 3 << 3) + body_with_line([fill(0), hatches["stretch"], fill(1)])
    # Shape-exact-primitives story A3b: a clip push whose rule byte is 0xFE or 0xFF takes a union of two to four members from the bytes that
    # follow (fuzz_render.c, record): a count byte (2 + b % 3), then for each member a path pick, a placement byte (a multiple of 4 is
    # followed by a map; any other is the identity) and a rule byte (bit 0: even-odd).  The paths here are the square (0), the cubic bulge (1)
    # and the line (2): the square and the bulge overlap (one group, made exact), the square and the line do not (the fast tier).
    def union_clip(members, rule=0xFE, map_bytes=b"\x00"):
        count = len(members) - 2
        return bytes([4, 0]) + map_bytes + bytes([rule, count]) + b"".join(bytes([pick, 1, member_rule]) for pick, member_rule in members)

    unions = {
        "overlap": union_clip([(0, 0), (1, 0)]),                       # the square and the bulge: one group of two
        "rules": union_clip([(0, 0), (1, 1), (0, 1)]),                 # three members under both rules, the same square twice
        "fast": union_clip([(0, 0), (2, 0)]),                          # the square and a line: disjoint boxes
        "four": union_clip([(0, 0), (1, 0), (2, 1), (1, 1)]),          # four members
    }
    for name, item in unions.items():
        items = [fill(0), item, fill(1), fill(3), POP, fill(0, ROTATED)]
        out["scripted-union-%s.bin" % name] = header(request=0) + body_with_line(items)
        out["scripted-union-%s-float.bin" % name] = header(target=T64 | 3) + body_with_line(items)
        out["scripted-union-%s-memory-damage.bin" % name] = header(request=CALLER_MEMORY | DAMAGE) + body_with_line(items)
    out["scripted-union-limit.bin"] = header(limit=5) + body_with_line([fill(0), unions["overlap"], fill(1), POP])
    out["scripted-union-limit-strict.bin"] = header(request=STRICT, limit=5 | 2 << 3) + body_with_line([fill(0), unions["rules"], fill(1), POP])
    out["scripted-union-crossings.bin"] = header(limit=6 | 1 << 3) + body_with_line([fill(0), unions["four"], fill(1), POP])
    out["scripted-union-mapped.bin"] = header(request=0) + body_with_line([group(), union_clip([(0, 0), (1, 0)], 0xFF, bytes([3, 40])), fill(1), POP, POP])
    out["scripted-pattern-tile-union-limit.bin"] = header(limit=5 | 0 << 3) + body_with_line([fill(0), lattices["tile-union"], fill(1)])
    out["scripted-pattern-tile-union-limit-strict.bin"] = header(request=STRICT, limit=5 | 0 << 3) + body_with_line([fill(0), lattices["tile-union-three"], fill(1)])
    out["scripted-pattern-limit.bin"] = header(limit=3 << 3) + body_with_line([fill(0), lattices["overlap"], fill(1)])
    out["scripted-pattern-limit-strict.bin"] = header(request=STRICT, limit=5 << 3) + body_with_line([fill(0), lattices["gapped"], fill(1)])
    out["scripted-pattern-cell-cap.bin"] = header(limit=2 | 16) + body_with_line([fill(0), lattices["period"], fill(1)])
    # A cell-deposit cap (limit mode 2, a cap of 16) that the outer fill meets and the window's copies may, over a target of 7 by 7 that
    # holds the 4 by 4 window: whole cells could pass the cap, so the period tier gives the window up for the general tier (PC9).
    small = (1 << 2) | (1 << 4)
    out["scripted-pattern-gives-up.bin"] = header(target=small, limit=2 | (15 << 3)) + body_with_line([fill(0), lattices["period"], fill(1)])
    out["scripted-pattern-gives-up-damage.bin"] = header(target=small, request=DAMAGE, limit=2 | (15 << 3)) + body_with_line([fill(0), lattices["period"], fill(1)])
    out["scripted-pattern-cancel.bin"] = header(cancel=2) + body_with_line([fill(0), lattices["stroke"], fill(1)])
    out["regression-sparse-partial-under-budget.bin"] = base64.b64decode("PAAABgAiBQICAAAAAAAJAgAAAACqCQkAAAAAAQIJAAAAAAEAAAAAAAAFAwIJAAAAAPMAAAkACQkEAAAAAAAABQIMAAIAAAAAAA//AgAAAAC4AQICAgIJAwkCAAACAQICAgIJAwkCAAACAwAAAMhkMv8CBggAAAEDFMhkMv8CBgACAxTIZDL/AgY=")
    out["regression-strict-cells-outside-damage.bin"] = base64.b64decode(
        "cQsAMplAJm0ZT3ZmwR6Jf2Fd7lDCRLR/QSt6/zbm6uHj7bR6JLZqECw6JO+wOwclMCTHsAvBvaXFi6p+svmrTJ+KgxAuG0woagxRNjJeiGcC"
        "myM9FiyKlJxCJFFWzIkK8SvFqHMplT7Bdyd9VFOqgVUfzMjmgcV+F/qsYJOfKS/fHjRpgYuTPZ9DGeivfs7ibhEb5CtwDwGioZomrmEA7NLX"
        "H5rRQNlNKpMl8Og98ySqsAWGgpW+WBAM1grAFdQqD5fiB4thMQSas5I3pmv8sPE=")
    # Found by the second soak: a cell cap met in the whole render and not in the damage-limited one, so an item was
    # refused in one and drawn in the other and the pixels inside the damage differed (Implementation Notes of story
    # 16). Fixed 2026-10-21 (S4); the harness compares inside the damage with no tolerance for it.
    # Found by pass B's libFuzzer run: a stroke painted with a pattern whose tile holds a gradient fill that leaves the sampling domain in the
    # whole copies of the lattice and not in the cut ones; the damage-limited render drew only the cut ones and refused nothing (S4).
    out["regression-tile-limit-outside-damage.bin"] = base64.b64decode(
        "PIoAAAAAAgUCAgAAAAAACQIAAAAAAQkJAAAAAAECCQAAAAABAAAAAAAABQMCCQAAAAAAAAAJAAkJBAAAAAAAAAUCDAACAAAAAAAP/wICAgIJAw"
        "kCAAACAQICAgIJAwkCAAACAwAAAMhkMv8CAAAAAMhkMv8C+AAbAAcAAMhkMv8CAAABAMhkMv8CAA==")
    # Found by pass B's libFuzzer run (CG4): a union with a member that can touch no pixel (a hairline on a pixel boundary far outside the sink's
    # range): the fast tier refused the push for its geometry where the general tier skipped the member.
    out["regression-union-member-without-pixels.bin"] = base64.b64decode(
        "PAAABQACBQKjAgAAAAAACQIAAAAAAQkJAAAAAAECCQAAAAABAAAAAAAABQMCCQAAAAAAAAAJAAkJBAAAAAAAAAUCDAACAAAAAAAP/wIAAAAAAQ"
        "ECAgICCQMJAgAAAgECAgICCQMJAgAAAgQAAADIZDL/AgAEAAD+AQAABgEBAAABAMhkNP8CAAYA")
    # Found by pass B's libFuzzer run (UBSan): a union whose device box has the most negative 26.6 coordinate, which box_floor negated as an int32.
    out["regression-box-floor-of-int32-min.bin"] = base64.b64decode(
        "PAoAAAACBQICAAAAAAAJAgAAAAABCQkAAAAAAQI7CQAAAAABAAAAAAAABQMCCQAAAAAAAAAJAAkHBAAAAAAAAAUCDAACAAAAAAAP/wIAAAAAAQ"
        "ECAgICCQMJAgAAAgECAgICCQMJAgAAAgYAAADIZDP/AgAEAAD+AQABAAEGAAAAAxTIZDP/AgA=")
    # Found by pass B's libFuzzer run: a strict request whose dual render (the earlier step as the second engine) refuses an item for its limit,
    # which the strict request returns as the render's refusal where the check allowed only the limit.
    out["regression-strict-dual-refusal.bin"] = base64.b64decode(
        "PAEAAD8CBQICAAAAAAAJAgAAAAABCQkAAAAABAIJAAAAAAEAAAAAAAAFAwIJAAAAAAAAAAkACQkEAAAAAAAABQIMAAIAAAAAAA//AgAAAAABAQ"
        "ICAgIJAwkLAAACAQICAgIJAwkCAAACAgAAAMhkMv8CBgUMQAABAMhkMv8CBg==")
    # A stroked pattern copy with a union clip, in caller memory: the clip's fold rewound the arena to the level's mark under the
    # copy's stroke, and the stroke was then destroyed with its members overwritten (a read of a wild pointer).
    out["regression-pattern-stroke-union-clip-arena.bin"] = base64.b64decode(
        "PAoAAAACBQICAAAAAAAJAgAAAAABCQkAAAAAAQIJAAAAAAEAAAAAAAAFAwIJAAAAAAAAAAkACQkEAAAAAAAABQIMAAIAAAAAAA//AgAAAAABAQICAgIJAwkCAAACAQICAgIJAwkCAAACAwAAAMhkMv8CAAAA/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8/Pz8AMhkMv8C8AADDAABAAEAyGQy/wIA")
    # A stroke of about 3,000 outline points in a nested pattern tile: a copy of it costs its points, and the count of copies alone let 2^22 of them
    # run (not finished in five minutes); the cost is counted now (PC8) and the use refused.
    out["regression-pattern-stroke-cost.bin"] = base64.b64decode("PAgAAAABBQICAAAAAAAJGgAA0EnpTp3Be2vUeqeVyuL0W/Unw0024c+f9SdAsjbhz5+4ZAd3vwDQSenw1ifttQYR8/mWAM10nr4O4JKiqcUetT2kPIiRIAABBwIEABoaGhoaGhoaGhoaGhoaGhoaGhoaGhoaGhoaGhoaGgAAyJQpQbj52ayXNKqstiP8lYUm1E4398vCuAAAAAAAAP//EgILMFV6n8TpDjNYfaLH7BE2W4ClS8rvFDleg6jN8hc8YYar0PUaAAAABUBAAAA=")
    # The same shape found by a 25-minute libFuzzer run after the cost above was counted: a stroke of about 2,300 outline points in a tile whose
    # copies each have a translation with a fraction of its own, so every copy expands its own stroke (and reduces its seams) and none is served
    # from the cache.  That costs about a microsecond and a half a point a copy, 10 times the cut's, and a charge of one copy for each 16 points
    # let it run 98 seconds; one for each point past the first 16 refuses it after about 3.
    out["regression-pattern-stroke-expand.bin"] = base64.b64decode("PAgAAAABBQICAAAAAAAJGgAA0EnpTp3Be2vUeqeVyuL0W/Unw0024c+f9SdAsjbhz5+4ZAd3vwDQSenw1ifttQYR8/mWAM10nr4O4JKiqcUetT2kPIiRIAABBwIEABoaGhoaGhoaGhoaGhoaGhoaGhoaGhoaGhoaGhoaGgAAyJQpQbj52ayXNKqstiP8lYUm1E4398vCuAAAAAAAAP//EgILMFV6n8TpDjNYfaLH7BE2W4ClS8rvFDleg6jN8hc8YYar0PUaAAAABUBAAAA=")
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


def biased_mask(seed):
    """A biased file whose request byte lets a group take a mask, and whose body has more groups with the blend bits that take one."""
    data = bytearray(biased(500 + seed))
    data[1] |= MASKS
    rng = Rng(9000 + seed)
    for _ in range(12):
        # a group op, a pick, an opacity, an operator, and a blend byte of the masked kind, somewhere in the body
        at = 5 + rng.below(len(data) - 10)
        data[at:at + 5] = bytes([5, rng.byte(), rng.byte(), rng.byte(), (rng.byte() & 0x5F) | 0x20])
    return bytes(data)


def biased_pattern(seed):
    """A biased file with fills whose rule byte is 0xF8 or more (a pattern from the bytes that follow: fuzz_render.c's pattern_of) put at
    random places in the body, over a target of a size that holds a period (7 or more) and the sink's defaults or a lowered limit."""
    data = bytearray(biased(2000 + seed))
    data[0] = (data[0] & ~0x3C) | (1 + (seed % 3)) << 2 | (1 + (seed // 3 % 3)) << 4  # a width and a height of 7, 33 or 64
    rng = Rng(7000 + seed)
    for _ in range(10):
        at = 5 + rng.below(len(data) - 40)
        data[at:at + 14] = bytes([0, rng.byte(), 0, rng.byte(), rng.byte(), rng.byte(), 255, 2, 0xF8 | (rng.byte() & 7),
                                  rng.byte(), rng.byte(), rng.byte(), rng.byte(), rng.byte()])
    return bytes(data)


def files():
    out = {}
    for i in range(BIASED_FILES):
        out["biased-%03d.bin" % i] = biased(i + 1)
    for i in range(BIASED_MASK_FILES):
        out["biased-mask-%03d.bin" % i] = biased_mask(i + 1)
    for i in range(BIASED_PATTERN_FILES):
        out["biased-pattern-%03d.bin" % i] = biased_pattern(i + 1)
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
