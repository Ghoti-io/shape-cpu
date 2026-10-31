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
"""The independent reference for masked groups in shape-cpu's renders (masks-images-patterns story M2; MC1 to MC4).

Writes tests/mask_vectors.h, or with --check compares the file on disk with what this would write.  It is the third
restatement of the soft mask's value: it shares no code with the C library and none with shape-scene's
tools/composite-oracle.py (which states MK2 to MK7 for the waist), and takes only the arithmetic of the plain compositor
(N1 to N5, the conversion between spaces, `apply`, the 8-bit coverage of a strip and its D8 weights) from this library's
own tools/gen-render-vectors.py, which states those independently of the library as well.

What it states, in exact rationals (`fractions.Fraction`), for one pixel of a one-pixel target:

  - MC3 the mask's value from the layer the mask's picture leaves, drawn as its own isolated group in the render space R
    (`alpha`: `clamp01(TR(clamp01(a)))`; `luminance`: the premultiplied layer converted from R into the luminance space
    L, the backdrop converted into L and composited under it, `r = sat(c_L + (1 - a) B_L)`, `y = sat(k . r)`,
    `clamp01(TR(clamp01(y)))`), with the transfer of PDF's function types (identity, exponential, a table, a stitching of
    those), the coefficient sets `W3C`, `REC709`, `LUM` and `SPACE_Y` (the Y row of N3's RGB-to-XYZ matrix of L);
  - MC4 the pop: the opacity source `o v`, or the shape `m v`, and then `apply`;
  - a mask whose picture holds a masked group of its own, and a mask over an empty picture (the outside value, MK5).

For each case and each of the four formats the header holds the backdrop as the format stores it, the root layer before
the write (the nearest doubles), whether a double evaluator can match it bit for bit (`exact`), whether the pixel is
touched, and the stored value after the write with the other side of the rounding where the value lies within 1e-9 of a
boundary (D7), as tools/gen-render-vectors.py's header does.

    python3 tools/gen-mask-vectors.py          # write tests/mask_vectors.h
    python3 tools/gen-mask-vectors.py --check  # fail if it differs
"""

import importlib.util
import sys
from fractions import Fraction as F
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "tests" / "mask_vectors.h"

# The plain compositor's arithmetic, stated independently of the library by the sibling generator.
_spec = importlib.util.spec_from_file_location("render_vectors_generator", HERE / "gen-render-vectors.py")
rv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rv)

ZERO, ONE = rv.ZERO, rv.ONE
sat = rv.sat

SRGB_SPACE = rv.SRGB_SPACE
LIN = rv.LIN
P3 = rv.P3
BT2020_LIN = rv.BT2020_LIN
ZSPACE = (0, 0, 0.0)

W3C, REC709, LUM, SPACE_Y = 0, 1, 2, 3
ALPHA, LUMINANCE = 1, 2
OPACITY, SHAPE = 0, 1
IDENTITY, EXPONENTIAL, TABLE, STITCHING = 0, 1, 2, 3
COEFFS = {W3C: (F(2125, 10000), F(7154, 10000), F(721, 10000)),
          REC709: (F(2126, 10000), F(7152, 10000), F(722, 10000)),
          LUM: (F(30, 100), F(59, 100), F(11, 100))}


def clamp01(x):
    return ZERO if x < 0 else ONE if x > 1 else x


# --- MK4: the transfer, from the text of the rule -----------------------------------------------------------------------
def tr_leaf(leaf, x, ex):
    kind = leaf["kind"]
    if kind == IDENTITY:
        return x
    if kind == EXPONENTIAL:
        n = F(leaf["n"])
        if x == 0:
            p = ONE if n == 0 else ZERO
        elif n.denominator == 1:
            p = x ** n.numerator
        else:
            p = rv.power(x, n, ex)
        return F(leaf["c0"]) + p * (F(leaf["c1"]) - F(leaf["c0"]))
    s = [F(v) for v in leaf["samples"]]
    m = len(s)
    u = x * (m - 1)
    i = min(u.numerator // u.denominator, m - 2)
    t = u - i
    return s[i] + t * (s[i + 1] - s[i])


def tr(transfer, x, ex):
    if transfer["kind"] != STITCHING:
        return tr_leaf(transfer, x, ex)
    pieces = transfer["pieces"]
    for k, p in enumerate(pieces):
        lo, hi = F(p["lo"]), F(p["hi"])
        if lo <= x < hi or (k == len(pieces) - 1 and x == hi):
            e0, e1 = F(p["e0"]), F(p["e1"])
            return tr_leaf(p["leaf"], e0 + (x - lo) * (e1 - e0) / (hi - lo), ex)
    raise ValueError("a stitching that does not cover %s" % x)


# --- MC2, MC3: the mask's value -----------------------------------------------------------------------------------------
class Pixel:
    """What a list means at one pixel: the evaluator of the plain compositor, extended with a mask at a group's pop."""

    def __init__(self, target, fg, ex):
        self.target = target
        self.fg = fg
        self.ex = ex
        self.dyadic = True

    def value(self, mask, enclosing):
        ex = self.ex
        render = enclosing if mask["render"] == ZSPACE else mask["render"]
        lum_space = render if mask["space"] == ZSPACE else mask["space"]
        layer, _ = self.run(mask["items"], [ZERO] * 4, render, root=False)
        a = layer[3]
        if mask["mode"] == ALPHA:
            y = clamp01(a)
        else:
            c = rv.convert_pre(layer, render, lum_space, ex)
            b_space = lum_space if mask["backdrop_space"] == ZSPACE else mask["backdrop_space"]
            b = rv.convert([F(v) for v in mask["backdrop"]], b_space, lum_space, ex)
            over = [sat(c[i] + (1 - a) * b[i]) for i in range(3)]
            # The coefficients are the nearest doubles of rationals that are not dyadic (and SPACE_Y's are irrational to a
            # double's width): a double evaluator's luminance is within a rounding of this one, never bit for bit.
            ex.flag = True
            if mask["coefficients"] == SPACE_Y:
                k = rv.to_xyz(lum_space[0])[1]
            else:
                k = COEFFS[mask["coefficients"]]
            y = clamp01(sat(k[0] * over[0] + k[1] * over[1] + k[2] * over[2]))
        v = clamp01(tr(mask["transfer"], clamp01(y), ex))
        if rv.to_double(v) != v:
            self.dyadic = False
        return v

    def run(self, items, backdrop, space, root):
        """The layer after @items on @backdrop in @space, and whether a unit touched it (only the root's is read)."""
        ex = self.ex
        top = {"layer": list(backdrop), "space": space, "clips": [], "group": None, "outer": None}
        stack = [top]
        touched = False
        for item in items:
            cur = stack[-1]
            kind = item["kind"]
            if kind == "fill":
                if item.get("foreground"):
                    rgb = [F(v) for v in self.fg["rgba"][:3]]
                    alpha = F(self.fg["rgba"][3]) * F(item["rgba"][3])
                    sp = self.fg["space"]
                else:
                    rgb = [F(v) for v in item["rgba"][:3]]
                    alpha = F(item["rgba"][3])
                    sp = item.get("space", ZSPACE)
                if sp == ZSPACE:
                    sp = self.target
                c = rv.convert(rgb, sp, cur["space"], ex)
                s = [c[0] * alpha, c[1] * alpha, c[2] * alpha, alpha]
                m8 = rv.coverage8(item["w"])
                for c8 in cur["clips"]:
                    m8 = rv.weigh(m8, c8)
                if m8 == 0:
                    continue
                if m8 != 255:
                    self.dyadic = False
                cur["layer"] = rv.apply(cur["layer"], s, F(m8, 255), 0)
                touched = touched or len(stack) == 1
            elif kind == "clip":
                cur["clips"].append(rv.coverage8(item["w"]))
            elif kind == "unclip":
                cur["clips"].pop()
            elif kind == "group":
                blend = item.get("blend", ZSPACE)
                sp = cur["space"] if blend == ZSPACE else blend
                outer = 255
                for c8 in cur["clips"]:
                    outer = rv.weigh(outer, c8)
                stack.append({"layer": [ZERO] * 4, "space": sp, "clips": [], "group": item, "outer": outer})
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
                    self.dyadic = False
                moved = rv.convert_pre(done["layer"], done["space"], parent["space"], ex)
                weight = F(m8, 255)
                if g.get("mask") is not None:
                    mask = g["mask"]
                    v = self.value(mask, parent["space"])
                    if mask["acts_on"] == SHAPE:
                        s = [o * x for x in moved]
                        weight = weight * v
                    else:
                        s = [o * v * x for x in moved]
                    if rv.to_double(weight) != weight or any(rv.to_double(x) != x for x in s):
                        self.dyadic = False
                else:
                    s = [o * x for x in moved]
                if op in rv.BOUNDED and all(x == 0 for x in s):
                    continue
                parent["layer"] = rv.apply(parent["layer"], s, weight, op)
                touched = touched or len(stack) == 1
            else:
                raise ValueError(kind)
        if rv.BLENDED:
            ex.flag = True
            del rv.BLENDED[:]
        return stack[0]["layer"], touched


def evaluate(case, backdrop):
    ex = rv.Inexact()
    px = Pixel(case["target"], case["fg"], ex)
    layer, touched = px.run(case["items"], backdrop, case["target"], root=True)
    return layer, touched, ex.flag, px.dyadic


# --- the cases ----------------------------------------------------------------------------------------------------------
def fill(rgba, w=64, space=ZSPACE):
    return {"kind": "fill", "rgba": rgba, "w": w, "space": space}


def clip(w):
    return {"kind": "clip", "w": w}


UNCLIP = {"kind": "unclip"}
UNGROUP = {"kind": "ungroup"}


def group(opacity=1.0, composite=0, blend=ZSPACE, mask=None):
    return {"kind": "group", "opacity": opacity, "composite": composite, "blend": blend, "mask": mask}


def transfer_identity():
    return {"kind": IDENTITY}


def transfer_exp(c0, c1, n):
    return {"kind": EXPONENTIAL, "c0": c0, "c1": c1, "n": n}


def transfer_table(samples):
    return {"kind": TABLE, "samples": samples}


def transfer_stitching(pieces):
    return {"kind": STITCHING, "pieces": pieces}


def piece(lo, hi, e0, e1, leaf):
    return {"lo": lo, "hi": hi, "e0": e0, "e1": e1, "leaf": leaf}


def mask(items, mode=ALPHA, transfer=None, acts_on=OPACITY, coefficients=W3C, space=ZSPACE, render=ZSPACE,
         backdrop=(0.0, 0.0, 0.0), backdrop_space=ZSPACE):
    if mode == ALPHA:
        coefficients, space, backdrop, backdrop_space = 0, ZSPACE, (0.0, 0.0, 0.0), ZSPACE
    return {"items": items, "mode": mode, "coefficients": coefficients, "space": space, "render": render,
            "backdrop": backdrop, "backdrop_space": backdrop_space, "acts_on": acts_on,
            "transfer": transfer or transfer_identity()}


FG = {"rgba": [0.5, 0.25, 0.0, 0.5], "space": ZSPACE}
SRC = [1.0, 0.5, 0.0, 0.75]
BACKDROPS = [(128, 64, 255, 128), (0, 0, 255, 255), (37, 200, 91, 0), (250, 3, 77, 211)]
NAMES = rv.OPS + [rv.BLEND_NAMES[k] for k in sorted(rv.BLEND_NAMES)]


def op_name(op):
    return rv.OPS[op] if op < 13 else rv.BLEND_NAMES[op]


def cases():
    out = []

    def add(name, items, backdrop=BACKDROPS[0], target=SRGB_SPACE, fg=None):
        out.append({"name": name, "items": items, "backdrop": backdrop, "target": target, "fg": fg or FG})

    def masked(m, content=None, **kw):
        return [group(mask=m, **kw), content or fill(SRC), UNGROUP]

    # MC3, alpha: every transfer over pictures of alpha 1, 1/2, 3/4, 0 and 0.3.
    transfers = [
        ("identity", transfer_identity()),
        ("exponential n=1", transfer_exp(0.0, 1.0, 1.0)),
        ("exponential n=2", transfer_exp(0.0, 1.0, 2.0)),
        ("exponential n=1/2", transfer_exp(0.25, 1.0, 0.5)),
        ("exponential n=0", transfer_exp(0.125, 0.75, 0.0)),
        ("exponential, a value at 0", transfer_exp(0.5, 0.75, 1.0)),
        ("table of 2", transfer_table([0.0, 1.0])),
        ("table of 3", transfer_table([0.25, 0.75, 0.5])),
        ("table of 5, falling", transfer_table([1.0, 0.75, 0.5, 0.25, 0.0])),
        ("stitching", transfer_stitching([
            piece(0.0, 0.25, 0.0, 1.0, {"kind": IDENTITY}),
            piece(0.25, 0.75, 0.0, 1.0, {"kind": EXPONENTIAL, "c0": 0.0, "c1": 1.0, "n": 2.0}),
            piece(0.75, 1.0, 1.0, 0.0, {"kind": TABLE, "samples": [0.0, 0.5, 1.0]})])),
        ("stitching of one piece", transfer_stitching([piece(0.0, 1.0, 0.0, 1.0, {"kind": TABLE, "samples": [0.5, 1.0]})])),
    ]
    alphas = [(1.0, "alpha 1"), (0.5, "alpha 1/2"), (0.75, "alpha 3/4"), (0.0, "alpha 0"), (0.3, "alpha 0.3")]
    for tname, t in transfers:
        for a, aname in alphas:
            add("alpha mask, %s, %s" % (tname, aname),
                masked(mask([fill([1.0, 1.0, 1.0, a])], transfer=t)))
    # the stitching's pieces at and about a bound, and the input at 0 and 1
    sticher = transfer_stitching([piece(0.0, 0.5, 0.0, 1.0, {"kind": IDENTITY}), piece(0.5, 1.0, 0.0, 0.5, {"kind": IDENTITY})])
    for a in (0.0, 0.25, 0.5, 0.75, 1.0):
        add("alpha mask, a stitching at its bound, alpha %s" % a, masked(mask([fill([1.0, 1.0, 1.0, a])], transfer=sticher)))
    # partial coverage of the picture, and two fills composited in it
    add("alpha mask over a picture half covered (a strip 32/64)", masked(mask([fill([1.0, 1.0, 1.0, 1.0], 32)])))
    add("alpha mask over two translucent fills", masked(mask([fill([1.0, 1.0, 1.0, 0.5]), fill([0.0, 0.0, 0.0, 0.5])])))
    add("alpha mask, the picture's color is not read", masked(mask([fill([0.9, 0.1, 0.2, 0.5])])))

    # MC4: what a mask acts on, for every operator, under an isolated parent.
    for op in sorted(set(range(13)) | set(rv.BLEND_NAMES)):
        for acts, aname in ((OPACITY, "opacity"), (SHAPE, "shape")):
            add("%s acting on the %s, value 1/2" % (op_name(op), aname),
                masked(mask([fill([1.0, 1.0, 1.0, 0.5])], acts_on=acts), composite=op, opacity=0.75))
    for op in (0, 5, 6, 12):
        for acts, aname in ((OPACITY, "opacity"), (SHAPE, "shape")):
            add("%s acting on the %s, value 0" % (op_name(op), aname),
                masked(mask([fill([1.0, 1.0, 1.0, 0.0])], acts_on=acts), composite=op))
            add("%s acting on the %s, value 1" % (op_name(op), aname),
                masked(mask([fill([1.0, 1.0, 1.0, 1.0])], acts_on=acts), composite=op))
    add("plus clamping, on the opacity", masked(mask([fill([1, 1, 1, 0.5])], acts_on=OPACITY), fill([1, 1, 1, 1]), composite=12),
        backdrop=(200, 255, 200, 230))
    add("plus clamping, on the shape", masked(mask([fill([1, 1, 1, 0.5])], acts_on=SHAPE), fill([1, 1, 1, 1]), composite=12),
        backdrop=(200, 255, 200, 230))

    # MC3, luminance: each coefficient set over the colors that tell them apart, with the picture and the luminance in one space.
    colors = [("red", [1, 0, 0, 1]), ("green", [0, 1, 0, 1]), ("blue", [0, 0, 1, 1]), ("gray", [0.5, 0.5, 0.5, 1]),
              ("white", [1, 1, 1, 1]), ("black", [0, 0, 0, 1]), ("translucent", [0.8, 0.25, 0.125, 0.5]),
              ("out of range", [1.5, -0.25, 0.5, 1.0]), ("transparent", [0.9, 0.9, 0.9, 0.0])]
    for coeff, cname in ((W3C, "W3C"), (REC709, "REC709"), (LUM, "LUM"), (SPACE_Y, "SPACE_Y")):
        for name, rgba in colors:
            add("luminance %s of %s" % (cname, name), masked(mask([fill(rgba)], mode=LUMINANCE, coefficients=coeff)))
    for name, rgba in colors[:4]:
        add("luminance, a straight color is not the premultiplied one: %s" % name,
            masked(mask([fill([rgba[0], rgba[1], rgba[2], 0.5])], mode=LUMINANCE, coefficients=LUM)))
    # gray in every set is the gray itself
    for coeff in (W3C, REC709, LUM, SPACE_Y):
        add("luminance gray invariance, set %d" % coeff, masked(mask([fill([0.625, 0.625, 0.625, 1.0])], mode=LUMINANCE, coefficients=coeff)))

    # MC3, R != L: the picture rendered in one space, the luminance taken in another.
    spaces = [("linear", LIN), ("sRGB", SRGB_SPACE), ("Display P3", P3), ("BT.2020 linear", BT2020_LIN)]
    pic = [fill([0.9, 0.3, 0.1, 0.5]), fill([0.1, 0.4, 0.9, 0.5], 40)]
    for rname, r in spaces:
        for lname, l in spaces:
            if r == l:
                continue
            add("luminance, picture in %s, luminance in %s" % (rname, lname),
                masked(mask(pic, mode=LUMINANCE, render=r, space=l, coefficients=LUM)))
    add("luminance, picture in linear light, luminance in the enclosing sRGB, W3C",
        masked(mask(pic, mode=LUMINANCE, render=LIN)))
    add("luminance, SPACE_Y of Display P3", masked(mask([fill([0.2, 0.9, 0.4, 1.0])], mode=LUMINANCE, space=P3, coefficients=SPACE_Y)))
    add("luminance, SPACE_Y of BT.2020", masked(mask([fill([0.2, 0.9, 0.4, 1.0])], mode=LUMINANCE, space=BT2020_LIN, coefficients=SPACE_Y)))

    # MC3, the backdrop: composited in L after the conversion, and the outside value (MK5).
    for bname, bd, bs in (("black", (0.0, 0.0, 0.0), ZSPACE), ("gray", (0.5, 0.5, 0.5), ZSPACE), ("colored", (0.25, 0.5, 1.0), ZSPACE),
                          ("in linear light", (0.25, 0.5, 1.0), LIN), ("in Display P3", (0.2, 0.3, 0.9), P3)):
        for aname, rgba, w in (("empty picture", [1, 1, 1, 1], 0), ("opaque picture", [1, 1, 1, 1], 64),
                               ("translucent picture", [0.9, 0.2, 0.1, 0.5], 64), ("half-covered picture", [0.9, 0.2, 0.1, 1.0], 32)):
            add("luminance, %s backdrop, %s" % (bname, aname),
                masked(mask([fill(rgba, w)], mode=LUMINANCE, coefficients=LUM, backdrop=bd, backdrop_space=bs, space=SRGB_SPACE)))
    add("luminance, backdrop with R != L and a transfer with a value at 0",
        masked(mask([fill([0.9, 0.2, 0.1, 0.25])], mode=LUMINANCE, render=LIN, space=SRGB_SPACE, coefficients=REC709,
                    backdrop=(0.25, 0.25, 0.25), transfer=transfer_table([0.25, 1.0]))))
    for tname, t in transfers[5:9]:
        add("empty picture, alpha, %s: the outside value" % tname, masked(mask([fill([1, 1, 1, 1], 0)], transfer=t)))
        add("empty picture, luminance, %s" % tname, masked(mask([fill([1, 1, 1, 1], 0)], mode=LUMINANCE, transfer=t, backdrop=(0.5, 0.5, 0.5))))
    add("a picture of nothing at all", masked(mask([])))
    add("outside value acting on the shape", masked(mask([], transfer=transfer_exp(0.5, 0.5, 1.0), acts_on=SHAPE)))
    add("outside value 1 acting on the shape under dest-in", masked(mask([], transfer=transfer_exp(1.0, 1.0, 1.0), acts_on=SHAPE), composite=6))

    # the order and the weights: a clip, an opacity, a mask and a mode on one group
    for op in (0, 6, 11, 23, 27):
        add("%s under a clip of 32/64, opacity 1/2, a mask of 3/4" % op_name(op),
            [clip(32)] + masked(mask([fill([1, 1, 1, 0.75])]), composite=op, opacity=0.5) + [UNCLIP], backdrop=BACKDROPS[3])
    add("a mask and a clip around a group of two fills", [clip(48), group(mask=mask([fill([1, 1, 1, 0.5], 48)])), fill([1, 0, 0, 1]),
                                                           fill([0, 1, 0, 0.5], 32), UNGROUP, UNCLIP], backdrop=(0, 0, 0, 0))
    add("a clip inside a masked group is the group's alone",
        [group(mask=mask([fill([1, 1, 1, 0.5])])), clip(20), fill([1, 0, 0, 1]), UNCLIP, fill([0, 0, 1, 0.5], 28), UNGROUP])
    add("a clip around a masked group is not taken into the mask",
        [clip(16), group(mask=mask([fill([1, 1, 1, 1], 64)])), fill([1, 0, 0, 1]), UNGROUP, UNCLIP])
    add("a clip in the mask's picture", masked(mask([clip(32), fill([1, 1, 1, 1]), UNCLIP, fill([1, 1, 1, 0.25])])))
    add("a group in the mask's picture", masked(mask([group(0.5), fill([1, 1, 1, 1]), UNGROUP, fill([1, 1, 1, 0.25])])))
    add("a blend mode in the mask's picture", masked(mask([fill([0.5, 0.5, 0.5, 1.0]), group(1.0, 23), fill([0.5, 0.25, 1, 1.0]), UNGROUP],
                                                         mode=LUMINANCE, coefficients=LUM)))
    add("a masked group in a group in linear light",
        [group(1.0, 0, LIN)] + masked(mask([fill([1, 1, 1, 0.5])]), composite=0, opacity=0.75) + [UNGROUP])
    add("a masked group of its own space, a mask rendered in the enclosing one",
        masked(mask([fill([0.9, 0.2, 0.1, 0.75])], mode=LUMINANCE, coefficients=LUM), blend=P3))
    add("nested masked groups", [group(0.75, 0, ZSPACE, mask([fill([1, 1, 1, 0.5])])), fill([1, 0, 0, 1]),
                                 group(1.0, 0, ZSPACE, mask([fill([1, 1, 1, 0.5], 40)], acts_on=SHAPE)), fill([0, 0, 1, 1]), UNGROUP, UNGROUP])
    # a mask of a mask: the picture's own group is masked
    inner = mask([fill([1, 1, 1, 0.5])])
    add("a mask of a mask", masked(mask([group(mask=inner), fill([1, 1, 1, 1]), UNGROUP])))
    add("a mask of a mask of a mask", masked(mask([group(mask=mask([group(mask=inner), fill([1, 1, 1, 1]), UNGROUP], transfer=transfer_table([0.0, 1.0]))),
                                                   fill([1, 1, 1, 0.75]), UNGROUP], mode=LUMINANCE, coefficients=LUM)))
    add("a luminance mask of a picture holding an alpha-masked group",
        masked(mask([group(mask=mask([fill([1, 1, 1, 0.5], 32)])), fill([0.2, 0.9, 0.4, 1.0]), UNGROUP], mode=LUMINANCE, coefficients=REC709)))
    return out


# --- the header ---------------------------------------------------------------------------------------------------------
def hexd(x):
    return rv.double_hex(F(x))


def space_c(s):
    return "{%d, %d, %s}" % (s[0], s[1], hexd(s[2]))


def collect_masks(items, masks):
    """Number every mask once (in the order a depth-first walk meets them, a picture's own first) into @masks."""
    refs = []
    for it in items:
        if it["kind"] == "group" and it.get("mask") is not None:
            m = it["mask"]
            collect_masks(m["items"], masks)
            masks.append(m)
            m["index"] = len(masks) - 1
            refs.append(m)
    return refs


def c_transfer(t, w, name):
    """The transfer's initializer; the arrays it points to are written first, under @name."""
    samples, pieces = "nullptr", "nullptr"
    if t["kind"] == TABLE:
        w("static const double %s_s[] = {%s};" % (name, ", ".join(hexd(v) for v in t["samples"])))
        samples = "%s_s" % name
    if t["kind"] == STITCHING:
        for k, p in enumerate(t["pieces"]):
            if p["leaf"]["kind"] == TABLE:
                w("static const double %s_p%d[] = {%s};" % (name, k, ", ".join(hexd(v) for v in p["leaf"]["samples"])))
        rows = []
        for k, p in enumerate(t["pieces"]):
            leaf = p["leaf"]
            rows.append("{%s, %s, %s, %s, %d, %s, %s, %s, %d, %s}" % (
                hexd(p["lo"]), hexd(p["hi"]), hexd(p["e0"]), hexd(p["e1"]), leaf["kind"], hexd(leaf.get("c0", 0.0)),
                hexd(leaf.get("c1", 0.0)), hexd(leaf.get("n", 0.0)), len(leaf.get("samples", [])),
                "%s_p%d" % (name, k) if leaf["kind"] == TABLE else "nullptr"))
        w("static const MPiece %s_pc[] = {%s};" % (name, ", ".join(rows)))
        pieces = "%s_pc" % name
    n = len(t["samples"]) if t["kind"] == TABLE else 0
    pc = len(t["pieces"]) if t["kind"] == STITCHING else 0
    return "{%d, %s, %s, %s, %d, %s, %d, %s}" % (t["kind"], hexd(t.get("c0", 0.0)), hexd(t.get("c1", 0.0)),
                                                hexd(t.get("n", 0.0)), n, samples, pc, pieces)


def item_c(it):
    kinds = {"fill": 1, "clip": 2, "unclip": 3, "group": 4, "ungroup": 5}
    m = it.get("mask")
    return "{%d, %d, {%s}, %s, %s, %s, %d, %d, %d}" % (
        kinds[it["kind"]], 2 if it.get("foreground") else 0, ", ".join(hexd(v) for v in it.get("rgba", [0, 0, 0, 0])),
        space_c(it.get("space", ZSPACE)), hexd(it.get("opacity", 1.0)), space_c(it.get("blend", ZSPACE)),
        it.get("composite", 0), it.get("w", 0), m["index"] if m is not None else -1)


def generate():
    lines = []
    w = lines.append
    w("// Generated by tools/gen-mask-vectors.py (exact rationals; independent of the library and of")
    w("// shape-scene's oracle).  Do not edit; run the generator.")
    w("#ifndef GHOTI_IO_GSHCPU_TEST_MASK_VECTORS_H")
    w("#define GHOTI_IO_GSHCPU_TEST_MASK_VECTORS_H")
    w("")
    w("#include <cstdint>")
    w("")
    w("namespace mask_vectors {")
    w("")
    w("struct MSpace {")
    w("  int primaries;")
    w("  int transfer;")
    w("  double gamma;")
    w("};")
    w("")
    w("// A stitching's piece: its interval, the leaf's input at each end, the leaf (kind 0 identity, 1 exponential, 2 table).")
    w("struct MPiece {")
    w("  double lo, hi, e0, e1;")
    w("  int leaf;")
    w("  double c0, c1, n;")
    w("  int sample_count;")
    w("  const double * samples;")
    w("};")
    w("")
    w("// A transfer: kind 0 identity, 1 exponential (c0, c1, n), 2 table, 3 stitching.")
    w("struct MTransfer {")
    w("  int kind;")
    w("  double c0, c1, n;")
    w("  int sample_count;")
    w("  const double * samples;")
    w("  int piece_count;")
    w("  const MPiece * pieces;")
    w("};")
    w("")
    w("// kind: 1 fill, 2 clip push, 3 clip pop, 4 group push, 5 group pop.  paint: 0 solid, 2 foreground.  mask: an index")
    w("// into kMasks, or -1.  width: a fill's or clip's rectangle in 1/64 of the pixel from its left edge, the pixel's height.")
    w("struct MItem {")
    w("  int kind;")
    w("  int paint;")
    w("  double rgba[4];")
    w("  MSpace space;")
    w("  double opacity;")
    w("  MSpace blend;")
    w("  int composite;")
    w("  int width;")
    w("  int mask;")
    w("};")
    w("")
    w("// A mask: mode 1 alpha, 2 luminance; coefficients 0 W3C, 1 REC709, 2 LUM, 3 SPACE_Y; acts_on 0 opacity, 1 shape; and")
    w("// the picture's items (which may hold masked groups of lower index).")
    w("struct MMask {")
    w("  int mode;")
    w("  int coefficients;")
    w("  MSpace space;")
    w("  MSpace render;")
    w("  double backdrop[3];")
    w("  MSpace backdrop_space;")
    w("  int acts_on;")
    w("  MTransfer transfer;")
    w("  int item_count;")
    w("  const MItem * items;")
    w("};")
    w("")
    w("struct MFormat {")
    w("  int format;")
    w("  uint32_t backdrop[4];")
    w("  double pre[4];")
    w("  bool exact;")
    w("  bool touched;")
    w("  uint32_t want[4];")
    w("  uint32_t alt[4];")
    w("};")
    w("")
    w("struct MCase {")
    w("  const char * name;")
    w("  MSpace target;")
    w("  double fg[4];")
    w("  MSpace fg_space;")
    w("  int item_count;")
    w("  const MItem * items;")
    w("  MFormat formats[4];")
    w("};")
    w("")
    all_cases = cases()
    masks = []
    for case in all_cases:
        collect_masks(case["items"], masks)
    for n, m in enumerate(masks):
        w("static const MItem kMaskItems%d[] = {" % n)
        for it in m["items"]:
            w("    %s," % item_c(it))
        if not m["items"]:
            w("    {0, 0, {0, 0, 0, 0}, {0, 0, 0.0}, 0.0, {0, 0, 0.0}, 0, 0, -1},  // (empty: never read)")
        w("};")
    w("")
    for n, m in enumerate(masks):
        pass
    mask_rows = []
    for n, m in enumerate(masks):
        transfer = c_transfer(m["transfer"], w, "kT%d" % n)
        mask_rows.append("    {%d, %d, %s, %s, {%s}, %s, %d, %s, %d, kMaskItems%d},  // %d" % (
            m["mode"], m["coefficients"], space_c(m["space"]), space_c(m["render"]), ", ".join(hexd(v) for v in m["backdrop"]),
            space_c(m["backdrop_space"]), m["acts_on"], transfer, len(m["items"]), n, n))
    w("")
    w("static const MMask kMasks[] = {")
    for row in mask_rows:
        w(row)
    w("};")
    w("")
    for n, case in enumerate(all_cases):
        w("static const MItem kItems%d[] = {" % n)
        for it in case["items"]:
            w("    %s," % item_c(it))
        w("};")
    w("")
    w("static const MCase kCases[] = {")
    exact_count = 0
    for n, case in enumerate(all_cases):
        fmts = []
        for fmt, _ in rv.FORMATS:
            stored, value = rv.backdrop_of(fmt, case["backdrop"])
            layer, touched, inexact, dyadic = evaluate(case, value)
            exact = not inexact and dyadic and all(rv.to_double(v) == v for v in layer + value)
            exact_count += 1 if exact else 0
            want, alt = rv.write_of(fmt, layer, touched, stored)
            fmts.append("{%d, {%s}, {%s}, %s, %s, {%s}, {%s}}" % (
                fmt, ", ".join("%du" % v for v in stored), ", ".join(hexd(rv.to_double(v)) for v in layer),
                "true" if exact else "false", "true" if touched else "false", ", ".join("%du" % v for v in want),
                ", ".join("%du" % v for v in alt)))
        fg = case["fg"]
        w("    {\"%s\", %s, {%s}, %s, %d, kItems%d, {%s}}," % (
            case["name"], space_c(case["target"]), ", ".join(hexd(v) for v in fg["rgba"]), space_c(fg["space"]),
            len(case["items"]), n, ", ".join(fmts)))
    w("};")
    w("")
    w("static const int kCaseCount = %d;" % len(all_cases))
    w("static const int kExactCount = %d;" % exact_count)
    w("")
    w("}  // namespace mask_vectors")
    w("")
    w("#endif")
    return "\n".join(lines) + "\n"


def selftest():
    """The transfer and the value against facts that hold without the library: the identities of MK4 and the outside value."""
    ex = rv.Inexact()
    t = transfer_stitching([piece(0.0, 1.0, 0.0, 1.0, {"kind": TABLE, "samples": [0.0, 1.0]})])
    assert tr(t, F(1, 3), ex) == F(1, 3) and tr(transfer_exp(0.0, 1.0, 1.0), F(2, 7), ex) == F(2, 7)
    assert tr(transfer_exp(0.0, 1.0, 0.0), ZERO, ex) == 1  # 0^0 = 1
    st = transfer_stitching([piece(0.0, 0.5, 0.0, 1.0, {"kind": IDENTITY}), piece(0.5, 1.0, 0.0, 0.5, {"kind": IDENTITY})])
    assert tr(st, F(1, 2), ex) == 0 and tr(st, ONE, ex) == F(1, 2)  # a bound belongs to the upper piece, the last takes 1
    for coeff in COEFFS.values():
        assert sum(coeff) == 1
    # gray in any set is gray, and an empty picture's value is the transfer of the backdrop's luminance
    px = Pixel(SRGB_SPACE, FG, rv.Inexact())
    gray = mask([fill([0.5, 0.5, 0.5, 1.0])], mode=LUMINANCE, coefficients=REC709)
    assert px.value(gray, SRGB_SPACE) == F(1, 2)
    empty = mask([], mode=LUMINANCE, coefficients=LUM, backdrop=(0.5, 0.5, 0.5))
    assert px.value(empty, SRGB_SPACE) == F(1, 2)
    assert px.value(mask([fill([1, 1, 1, 1], 0)], transfer=transfer_exp(0.375, 1.0, 1.0)), SRGB_SPACE) == F(3, 8)


def main():
    selftest()
    text = generate()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            print("check-mask-vectors: %s is not what tools/gen-mask-vectors.py writes" % OUT, file=sys.stderr)
            return 1
        print("check-mask-vectors: ok")
        return 0
    OUT.write_text(text)
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
