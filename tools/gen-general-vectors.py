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
"""The independent reference for non-isolated and knockout groups in shape-cpu's renders (KC1 to KC11).

Writes tests/general_vectors.h, or with --check compares the file on disk with what this would write.  It is the third
restatement of the transparency model's group rules (a group's initial backdrop, the element step, the group's result
and its group alpha and shape): it shares no code with the C library and none with shape-scene's tools/composite-oracle.py
(which states them for the waist), and takes only the arithmetic of the plain compositor (N1 to N5, the conversion between
spaces, the blend modes' mixing functions, the 8-bit coverage of a strip and its D8 weights) from this library's
tools/gen-render-vectors.py, which states those independently of the library as well.

What it states, in exact rationals (`fractions.Fraction`), for one pixel of a one-pixel target:

  - the element step in the literal form of ISO 32000's transparency model on premultiplied values, `L' = sat((1 - f) L +
    (f - Aa) Bk + T)` with `T = a` for source-over and `T = a (1 - Ab) + Aa Ab sat(B(Cb, Ca))` for a blend mode, and for every
    other operator `L' = sat((1 - f) L + f op(S, Bk))`;
  - the same step in the form the sink evaluates, KC4's `L' = sat((1 - f) (L - Bk) + op_X(A, Bk))`, and it requires the two to
    be equal for every step of every case: the rewrite is an identity in the reals;
  - the initial backdrop (T4) read from the parent, through knockout parents, and converted into the group's space;
  - the group's result (T5), `G = sat(L - (1 - Ag) L0)` on the colors with alpha `Ag`, converted to the parent's space and
    composited as an element (shape `m fg` and `A = m o G` for PDF's operators, shape `m` and `S = o G` for every other);
  - the group alpha (stepped with 0 for its backdrop in a knockout group) and the group shape `fg + f - fg f`.

For each case the header holds the backdrop (a float, so that a float target stores it), the target's space, the scene, whether a
double evaluator can match the value bit for bit (`exact`: every operation of KC4's order, traced here with the sink's operand
order, is exact in doubles), and the root layer before the write (the nearest doubles).

    python3 tools/gen-general-vectors.py          # write tests/general_vectors.h
    python3 tools/gen-general-vectors.py --check  # fail if it differs
"""

import importlib.util
import sys
from fractions import Fraction as F
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "tests" / "general_vectors.h"

_spec = importlib.util.spec_from_file_location("render_vectors_generator", HERE / "gen-render-vectors.py")
rv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rv)

ZERO, ONE = rv.ZERO, rv.ONE
sat = rv.sat
SRGB_SPACE = rv.SRGB_SPACE
LIN = rv.LIN
P3 = rv.P3
ZSPACE = (0, 0, 0.0)

SRC_OVER, CLEAR, SRC, DEST, DEST_OVER, SRC_IN, DEST_IN, SRC_OUT, DEST_OUT, SRC_ATOP, DEST_ATOP, XOR, PLUS = range(13)
SCREEN, OVERLAY, DARKEN, LIGHTEN, DODGE, BURN, HARD_LIGHT, SOFT_LIGHT, DIFFERENCE, EXCLUSION, MULTIPLY = range(13, 24)
HUE, SATURATION, COLOR, LUMINOSITY = range(24, 28)


def is_blend(op):
    return op >= SCREEN


def is_pdf(op):
    return op == SRC_OVER or is_blend(op)


def is_bounded(op):
    return op in rv.BOUNDED or is_blend(op)


class Trace:
    """Whether every value met in KC4's order of operations is a double (a dyadic rational of at most 53 bits)."""

    def __init__(self):
        self.exact = True

    def see(self, x):
        x = F(x)
        d = x.denominator
        if d & (d - 1) or abs(x.numerator) >= (1 << 53) or (abs(x) >= F(2) ** 64):
            self.exact = False
        return x


def pre_blend(op, a, b):
    """B2 on premultiplied values: `cs (1 - Ab) + cb (1 - As) + As Ab sat(B(Cb, Cs))` with source-over's alpha; `a` is the
    source, `b` the backdrop."""
    sa, da = a[3], b[3]
    mixed = [ZERO] * 3
    if sa * da != 0:
        rv.BLENDED.append(op)
        mixed = rv.b_mix(op, [sat(b[k] / da) for k in range(3)], [sat(a[k] / sa) for k in range(3)])
    return [a[k] * (1 - da) + b[k] * (1 - sa) + sa * da * mixed[k] for k in range(3)] + [sa + da * (1 - sa)]


def literal_pdf(l, bk, f, a, op):
    """ISO 32000's step on premultiplied values: `(1 - f) L + (f - Aa) Bk + T`, alpha `(1 - f) AL + (f - Aa) Ab + Aa`."""
    aa, ab = a[3], bk[3]
    if op == SRC_OVER:
        t = list(a[:3])
    else:
        mixed = [ZERO] * 3
        if aa * ab != 0:
            rv.BLENDED.append(op)
            mixed = rv.b_mix(op, [sat(bk[k] / ab) for k in range(3)], [sat(a[k] / aa) for k in range(3)])
        t = [a[k] * (1 - ab) + aa * ab * mixed[k] for k in range(3)]
    out = [sat((1 - f) * l[k] + (f - aa) * bk[k] + t[k]) for k in range(3)]
    out.append(sat((1 - f) * l[3] + (f - aa) * ab + aa))
    return out


def op_x(a, bk, op):
    """B2's operator on an element already weighed by its shape (A) and the backdrop."""
    if op == SRC_OVER:
        return [a[k] + (1 - a[3]) * bk[k] for k in range(4)]
    return pre_blend(op, a, bk)


def pd(op, s, d):
    """op(S, D) for an operator that is not PDF's, componentwise on premultiplied values."""
    if op == PLUS:
        return [min(ONE, s[k] + d[k]) for k in range(4)]
    fa, fb = rv.factors(op, s[3], d[3])
    return [fa * s[k] + fb * d[k] for k in range(4)]


def step(layer, bk, f, s, m, op, tr):
    """One element into a layer holding @layer, its backdrop @bk: the literal form, checked against KC4's, and the shape f.

    @s is the opacity source; @m the weight of the source (the shape of a fill; the clips of a group); @f the element's shape.
    Returns the new value."""
    if is_pdf(op):
        a = [m * v for v in s]  # A = m S: a fill's A = f S, a group's A = m o G
        literal = literal_pdf(layer, bk, f, a, op)
        # KC4 in the sink's order: op_X(m S, Bk) as apply makes it, then (1 - f) (L - Bk) + that.
        if op == SRC_OVER:
            o = [s[k] + (1 - s[3]) * bk[k] for k in range(4)]
        else:
            o = pre_blend(op, s, bk)
        t = [sat((1 - m) * bk[k] + m * o[k]) for k in range(4)]
        rewritten = [sat((1 - f) * (layer[k] - bk[k]) + t[k]) for k in range(4)]
        assert rewritten == literal, ("KC4 differs from T3", op, layer, bk, f, s, m)
        # Exactness: the sink's operations, each traced.
        if is_blend(op):
            tr.exact = False
        else:
            for v in [(1 - s[3]), (1 - s[3]) * bk[0], s[0] + (1 - s[3]) * bk[0], (1 - m), (1 - m) * bk[0], m * o[0],
                      (1 - f), (layer[0] - bk[0]), (1 - f) * (layer[0] - bk[0]), t[0], rewritten[0],
                      (1 - s[3]) * bk[3], o[3], (1 - m) * bk[3], m * o[3], (layer[3] - bk[3]), (1 - f) * (layer[3] - bk[3]),
                      t[3], rewritten[3]]:
                tr.see(v)
        return rewritten
    o = pd(op, s, bk)
    out = [sat((1 - m) * layer[k] + m * o[k]) for k in range(4)]
    for v in [(1 - m), (1 - m) * layer[0], m * o[0], out[0], out[3]] + o:
        tr.see(v)
    return out


class Layer:
    def __init__(self, space, group=None, parent=None):
        self.space = space
        self.group = group
        self.parent = parent
        self.l = [ZERO] * 4
        self.l0 = [ZERO] * 4
        self.ag = ZERO
        self.fg = ZERO
        self.clips = []
        self.knockout = bool(group and group.get("knockout"))
        self.nonisolated = bool(group and group.get("isolation"))
        self.outer = 255

    def backdrop(self):
        """The backdrop an element of this layer composites with (T3)."""
        return list(self.l0) if self.knockout else list(self.l)


def evaluate(case, backdrop, ex):
    """The root layer after the scene, and whether a unit touched it, in the literal form."""
    tr = Trace()
    target = case["target"]
    root = Layer(target)
    root.l = list(backdrop)
    stack = [root]
    touched = False
    for item in case["items"]:
        top = stack[-1]
        kind = item["kind"]
        if kind == "fill":
            rgb = [F(v) for v in item["rgba"][:3]]
            alpha = F(item["rgba"][3])
            space = item.get("space", ZSPACE)
            if space == ZSPACE:
                space = target
            c = rv.convert(rgb, space, top.space, ex)
            s = [c[0] * alpha, c[1] * alpha, c[2] * alpha, alpha]
            m8 = rv.coverage8(item["w"])
            for c8 in top.clips:
                m8 = rv.weigh(m8, c8)
            if m8 == 0:
                continue
            f = F(m8, 255)
            apply_element(top, s, f, f, SRC_OVER, tr, ex)
            if top is root:
                touched = True
        elif kind == "clip":
            top.clips.append(rv.coverage8(item["w"]))
        elif kind == "unclip":
            top.clips.pop()
        elif kind == "group":
            blend = item.get("blend", ZSPACE)
            space = top.space if blend == ZSPACE else blend
            g = Layer(space, item, top)
            outer = 255
            for c8 in top.clips:
                outer = rv.weigh(outer, c8)
            g.outer = outer
            if g.nonisolated:
                # T4: the backdrop it has as an element of its parent, converted into its space.
                g.l0 = rv.convert_pre(top.backdrop(), top.space, space, ex)
                g.l = list(g.l0)
            stack.append(g)
        elif kind == "ungroup":
            done = stack.pop()
            parent = stack[-1]
            g = done.group
            o = F(g.get("opacity", 1.0))
            op = g.get("composite", 0)
            m = F(done.outer, 255)
            if done.outer == 0:
                continue
            # T5: the group's own result, the initial backdrop taken out.
            ag = done.ag if done.nonisolated else done.l[3]
            gres = [sat(done.l[k] - (1 - ag) * done.l0[k]) for k in range(3)] + [ag] if done.nonisolated else list(done.l)
            moved = rv.convert_pre(gres, done.space, parent.space, ex)
            s = [o * v for v in moved]
            if is_pdf(op):
                f = m * done.fg
                weight = m
            else:
                f = m
                weight = m
            applied = apply_element(parent, s, weight, f, op, tr, ex)
            if parent is root and applied:
                touched = True
        else:
            raise ValueError(kind)
    return root.l, touched, tr.exact


def apply_element(layer, s, weight, f, op, tr, ex):
    """One element into @layer: its value, its group alpha and its group shape (T3).  Returns whether the layer was composited
    (the sink skips an element of no shape, and a bounded source of nothing that cannot change the layer)."""
    if weight == 0 and f == 0:
        return False
    bk = layer.backdrop()
    layer.fg = layer.fg + f - layer.fg * f
    if is_bounded(op) and all(v == 0 for v in s) and (not layer.knockout or f == 0):
        return False
    layer.l = step(layer.l, bk, f, s, weight, op, tr)
    if layer.nonisolated:
        agb = ZERO if layer.knockout else layer.ag
        # the alpha part alone, with 0 for the colors
        lg = [ZERO, ZERO, ZERO, layer.ag]
        bg = [ZERO, ZERO, ZERO, agb]
        sg = [ZERO, ZERO, ZERO, s[3]]
        layer.ag = step(lg, bg, f, sg, weight, op, Trace())[3]
    return True


# --- rounding and the header (the sibling generator's helpers) ---------------------------------------------------
to_double = rv.to_double
double_hex = rv.double_hex


def space_c(s):
    return "{%d, %d, %s}" % (s[0], s[1], double_hex(F(s[2])))


# --- the scenes -----------------------------------------------------------------------------------------------------
fill = rv.fill


def group(isolation=0, knockout=0, opacity=1.0, composite=SRC_OVER, blend=ZSPACE):
    return {"kind": "group", "isolation": isolation, "knockout": knockout, "opacity": opacity, "composite": composite,
            "blend": blend}


UNGROUP = rv.UNGROUP
UNCLIP = rv.UNCLIP
clip = rv.clip

# Backdrops: premultiplied, every value a float so that a float target stores it, with a half-transparent, an opaque and a
# clear one.
BACKDROPS = [
    ("half", (F(1, 4), F(1, 8), F(1, 16), F(1, 2))),
    ("opaque", (F(1, 4), F(1, 2), F(3, 4), F(1))),
    ("clear", (ZERO, ZERO, ZERO, ZERO)),
]
RED = [1.0, 0.25, 0.0, 0.5]
BLUE = [0.0, 0.5, 1.0, 0.75]
GREEN = [0.25, 1.0, 0.125, 0.5]
OPAQUE_GRAY = [0.5, 0.5, 0.5, 1.0]
CLEAR_FILL = [0.5, 0.5, 0.5, 0.0]

OPS_TESTED = [SRC_OVER, MULTIPLY, SCREEN, HUE, DEST_OVER, PLUS, SRC_ATOP, DEST_IN, SRC, XOR, CLEAR]
OP_NAMES = {SRC_OVER: "src-over", MULTIPLY: "multiply", SCREEN: "screen", HUE: "hue", DEST_OVER: "dest-over", PLUS: "plus",
            SRC_ATOP: "src-atop", DEST_IN: "dest-in", SRC: "src", XOR: "xor", CLEAR: "clear", DEST_OUT: "dest-out",
            COLOR_DODGE if False else DODGE: "color-dodge", SOFT_LIGHT: "soft-light"}


def cases():
    out = []

    def add(name, items, backdrop, target=SRGB_SPACE):
        out.append({"name": name, "items": items, "backdrop": backdrop, "target": target})

    for bname, bd in BACKDROPS:
        # The four combinations, two translucent shapes of full and of partial coverage inside, the group's operator varied.
        for iso, ko in ((0, 0), (1, 0), (0, 1), (1, 1)):
            combo = "%s%s" % ("ni" if iso else "iso", "-ko" if ko else "")
            for op in OPS_TESTED:
                for opacity in (1.0, 0.75):
                    for w in (64, 40):
                        add("%s %s %s o=%g w=%d on %s" % (combo, OP_NAMES[op], "two fills", opacity, w, bname),
                            [group(iso, ko, opacity, op), fill(RED, 64), fill(BLUE, w), UNGROUP], bd)
        # A fill of opacity 0 and some shape: it knocks out in a knockout group and does nothing in a plain one.
        for iso in (0, 1):
            add("%s knockout transparent shape on %s" % ("ni" if iso else "iso", bname),
                [group(iso, 1), fill(RED, 64), fill(CLEAR_FILL, 32), UNGROUP], bd)
            add("%s plain transparent shape on %s" % ("ni" if iso else "iso", bname),
                [group(iso, 0), fill(RED, 64), fill(CLEAR_FILL, 32), UNGROUP], bd)
        # KC11: two abutting shapes each half-covering the pixel leave a quarter of the initial backdrop in a knockout group.
        add("conflation: two half shapes in a knockout group on %s" % bname,
            [group(0, 1), fill(OPAQUE_GRAY, 32), fill(OPAQUE_GRAY, 32), UNGROUP], bd)
        add("conflation: two half shapes in a non-isolated knockout group on %s" % bname,
            [group(1, 1), fill(OPAQUE_GRAY, 32), fill(OPAQUE_GRAY, 32), UNGROUP], bd)
        # Clips: inside a group multiply an element's shape, around it they multiply once at its pop.
        add("clip inside a knockout group on %s" % bname, [group(1, 1), clip(48), fill(RED, 64), UNCLIP, fill(BLUE, 40), UNGROUP], bd)
        add("clip around a non-isolated group on %s" % bname, [clip(48), group(1, 0, 0.75, MULTIPLY), fill(RED, 64), fill(BLUE, 40), UNGROUP, UNCLIP], bd)
        add("clip around a knockout group on %s" % bname, [clip(48), group(0, 1, 1.0, SRC_OVER), fill(RED, 64), fill(BLUE, 40), UNGROUP, UNCLIP], bd)
        # Nested groups: the backdrop read through the right layers, and the group shape carried where an ancestor is knockout.
        for op in (SRC_OVER, MULTIPLY, DEST_OVER, SRC, DEST_IN):
            add("ko in ni in ko, child %s on %s" % (OP_NAMES[op], bname),
                [group(0, 1), fill(RED, 64), group(1, 0, 1.0, SRC_OVER), fill(BLUE, 48), group(0, 1, 0.75, op),
                 fill(GREEN, 40), fill(RED, 24), UNGROUP, UNGROUP, UNGROUP], bd)
            add("iso child %s in a knockout parent on %s" % (OP_NAMES[op], bname),
                [group(0, 1), fill(RED, 64), group(0, 0, 0.75, op), fill(BLUE, 40), fill(GREEN, 24), UNGROUP, fill(GREEN, 16), UNGROUP], bd)
            add("ni child %s in an isolated parent on %s" % (OP_NAMES[op], bname),
                [group(0, 0), fill(RED, 64), group(1, 0, 0.75, op), fill(BLUE, 40), UNGROUP, fill(GREEN, 16), UNGROUP], bd)
            add("ni child %s in a knockout parent on %s" % (OP_NAMES[op], bname),
                [group(0, 1), fill(RED, 64), group(1, 0, 0.75, op), fill(BLUE, 40), UNGROUP, fill(GREEN, 16), UNGROUP], bd)
        # A transparent shape in a child group of a knockout group: the child's group shape is its coverage whatever its source, so
        # its pop knocks out its share of what the parent holds (T5's shape m fg).
        for iso in (0, 1):
            for op in (SRC_OVER, MULTIPLY, DEST_OVER):
                add("%s child %s holding a transparent shape in a knockout parent on %s" % ("ni" if iso else "iso", OP_NAMES[op], bname),
                    [group(0, 1), fill(RED, 64), group(iso, 0, 1.0, op), fill(CLEAR_FILL, 32), UNGROUP, UNGROUP], bd)
        add("ni in ni on %s" % bname,
            [group(1, 0, 1.0, MULTIPLY), fill(RED, 64), group(1, 0, 0.5, SCREEN), fill(BLUE, 40), UNGROUP, UNGROUP], bd)
        add("ko ancestor two levels up on %s" % bname,
            [group(0, 1), fill(RED, 64), group(0, 0), group(0, 0, 0.75, SRC_OVER), fill(BLUE, 40), UNGROUP, UNGROUP, UNGROUP], bd)
        # Spaces: parent, group and child in different blend spaces; L0 converted into the group's space and G back.
        for name, parent, mid, inner in (("srgb/linear/p3", SRGB_SPACE, LIN, P3), ("linear/srgb/linear", LIN, SRGB_SPACE, LIN),
                                         ("srgb/p3/srgb", SRGB_SPACE, P3, SRGB_SPACE)):
            add("spaces %s: ni group over the page on %s" % (name, bname),
                [group(1, 0, 1.0, MULTIPLY, mid), fill(RED, 64), fill(BLUE, 40), UNGROUP], bd, parent)
            add("spaces %s: ni ko group over the page on %s" % (name, bname),
                [group(1, 1, 0.75, SRC_OVER, mid), fill(RED, 64, inner), fill(BLUE, 40), UNGROUP], bd, parent)
            add("spaces %s: ni in ni on %s" % (name, bname),
                [group(1, 0, 1.0, SRC_OVER, mid), fill(RED, 64), group(1, 1, 0.75, SCREEN, inner), fill(BLUE, 40), fill(GREEN, 24),
                 UNGROUP, UNGROUP], bd, parent)
        # A group whose elements are all clipped away or empty leaves the page as it was.
        add("ni group with nothing in it on %s" % bname, [group(1, 0, 1.0, MULTIPLY), UNGROUP], bd)
        add("ko group with nothing in it on %s" % bname, [group(0, 1, 1.0, DEST_IN), UNGROUP], bd)
    return out


def evaluate_case(case):
    ex = rv.Inexact()
    del rv.BLENDED[:]
    backdrop = list(case["backdrop"])
    root, touched, exact = evaluate(case, backdrop, ex)
    inexact = bool(ex.flag or rv.BLENDED or not exact)
    del rv.BLENDED[:]
    return root, touched, not inexact


# --- the header -----------------------------------------------------------------------------------------------------
def generate():
    lines = []
    w = lines.append
    w("// Generated by tools/gen-general-vectors.py (exact rationals; independent of the library, of shape-scene's oracle and of")
    w("// the other vector generators).  Do not edit; run the generator.")
    w("#ifndef GHOTI_IO_GSHCPU_TEST_GENERAL_VECTORS_H")
    w("#define GHOTI_IO_GSHCPU_TEST_GENERAL_VECTORS_H")
    w("")
    w("namespace general_vectors {")
    w("")
    w("struct GSpace {")
    w("  int primaries;")
    w("  int transfer;")
    w("  double gamma;")
    w("};")
    w("")
    w("// kind: 1 fill, 2 clip push, 3 clip pop, 4 group push, 5 group pop.  A fill covers width/64 of the pixel (a clip too).")
    w("struct GItem {")
    w("  int kind;")
    w("  double rgba[4];")
    w("  GSpace space;")
    w("  int width;")
    w("  double opacity;")
    w("  GSpace blend;")
    w("  int composite;")
    w("  int isolation;")
    w("  int knockout;")
    w("};")
    w("")
    w("// exact: every operation of the sink's order is exact in doubles, so a double evaluator matches `want` bit for bit.")
    w("struct GCase {")
    w("  const char * name;")
    w("  bool exact;")
    w("  GSpace target;")
    w("  double backdrop[4];")
    w("  int item_count;")
    w("  const GItem * items;")
    w("  double want[4];")
    w("  bool touched;")
    w("};")
    w("")
    all_cases = cases()
    results = []
    for n, case in enumerate(all_cases):
        root, touched, exact = evaluate_case(case)
        results.append((root, touched, exact))
        w("static const GItem kItems%d[] = {" % n)
        for it in case["items"]:
            k = it["kind"]
            if k == "fill":
                row = (1, it["rgba"], it.get("space", ZSPACE), it["w"], 1.0, ZSPACE, 0, 0, 0)
            elif k == "clip":
                row = (2, [0, 0, 0, 0], ZSPACE, it["w"], 1.0, ZSPACE, 0, 0, 0)
            elif k == "unclip":
                row = (3, [0, 0, 0, 0], ZSPACE, 0, 1.0, ZSPACE, 0, 0, 0)
            elif k == "group":
                row = (4, [0, 0, 0, 0], ZSPACE, 0, it["opacity"], it["blend"], it["composite"], it["isolation"], it["knockout"])
            else:
                row = (5, [0, 0, 0, 0], ZSPACE, 0, 1.0, ZSPACE, 0, 0, 0)
            w("    {%d, {%s}, %s, %d, %s, %s, %d, %d, %d}," % (
                row[0], ", ".join(double_hex(F(v)) for v in row[1]), space_c(row[2]), row[3], double_hex(F(row[4])),
                space_c(row[5]), row[6], row[7], row[8]))
        w("};")
    w("")
    w("static const GCase kCases[] = {")
    exact_count = 0
    for n, case in enumerate(all_cases):
        root, touched, exact = results[n]
        exact_count += 1 if exact else 0
        t = case["target"]
        w("    {\"%s\", %s, %s, {%s}, %d, kItems%d, {%s}, %s}," % (
            case["name"], "true" if exact else "false", space_c(t),
            ", ".join(double_hex(F(v)) for v in case["backdrop"]), len(case["items"]), n,
            ", ".join(double_hex(to_double(v)) for v in root), "true" if touched else "false"))
    w("};")
    w("")
    w("static const int kExactCaseCount = %d;" % exact_count)
    w("")
    w("}  // namespace general_vectors")
    w("")
    w("#endif  // GHOTI_IO_GSHCPU_TEST_GENERAL_VECTORS_H")
    return "\n".join(lines) + "\n"


def selftest():
    # The element step on a plain layer is apply: with Bk = L the rewrite's (L - Bk) term is 0, so the step is the plain
    # composite of the weighted source.
    ex = rv.Inexact()
    tr = Trace()
    l = [F(1, 4), F(1, 8), F(1, 16), F(1, 2)]
    s = [F(1, 2), F(1, 4), ZERO, F(1, 2)]
    got = step(l, list(l), F(1, 2), s, F(1, 2), SRC_OVER, tr)
    assert got == rv.apply(l, s, F(1, 2), SRC_OVER)
    # A knockout element of shape 1 replaces the layer by the element over the initial backdrop.
    got = step(l, [ZERO] * 4, ONE, s, ONE, SRC_OVER, tr)
    assert got == s
    # The identity of every operator and shape: KC4's rewrite equals the literal step (asserted inside step).
    for op in list(range(0, 13)) + list(range(13, 28)):
        if is_pdf(op):
            for f in (F(1, 4), F(1)):
                step([F(1, 4), F(1, 8), F(1, 16), F(1, 2)], [F(1, 2), F(1, 4), F(1, 4), F(3, 4)], f, [F(1, 4), F(1, 4), F(1, 8), F(1, 2)], f, op, tr)
    del rv.BLENDED[:]
    assert ex.flag is False


def main():
    selftest()
    text = generate()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            print("check-general-vectors: %s is not what tools/gen-general-vectors.py writes" % OUT, file=sys.stderr)
            return 1
        print("check-general-vectors: ok")
        return 0
    OUT.write_text(text)
    print("wrote %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
