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
"""Operator-level mutation generator for the sink's planted defects (story 13b).

Emits, as JSON on stdout, every candidate mutation of src/render as the tuples
tools/check-planted.py takes: (id, 'srcl', file, 'LINE:FRAGMENT', REPLACEMENT,
tests).  The candidates are the relational and logical operators flipped, each
condition negated, each statement deleted, the hash widths swapped, a letter of
each output string changed, a printed width dropped and a returned status
swapped.  The rules are shape-scene's tools/gen-scene-plants.py, unchanged but
for this library's result names.  It is a generator, not the gate:
tools/render-plants.json holds the candidates the tests were seen to catch (the
survivors are the equivalent or unreachable mutants check-planted.py's
docstring lists, and compile failures are dropped), and it is generated from the
sources as they stood, so a source edit that moves a line means regenerating
and re-running the sweep:

  python3 tools/gen-render-plants.py > candidates.json
  PLANT_EXTRA_JSON=candidates.json PLANT_ONLY='^r[cvgahs]-' make check-planted PREFIX=...

then keep the lines the sweep reports as ok (tools/keep-caught-plants.py).  After an edit that moved only some lines,
``--new`` writes just the candidates that tools/render-plants.json does not already hold (the entries whose mutation is
still at its line stay, the rest are the ones to sweep), and ``keep-caught-plants.py --merge tools/render-plants.json``
adds the ones the sweep caught to it.
"""
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXCL = {}
LINE_FILTER = {}

FILES = {
    "src/render/compose.c": "rc",
    "src/render/cover.c": "rv",
    "src/render/geometry.c": "rg",
    "src/render/arena.c": "ra",
    "src/render/hash.c": "rh",
    "src/render/sink.c": "rs",
    "src/render/gradient.c": "rd",
    "src/render/exact.c": "rx",   # strokes stories S4b and S4c: the exact step (UC1 to UC20)
    "src/render/stroke.c": "rk",  # strokes story S4
}
# The order is the cost: the quick and the likely first, the sweep stops at the first that fails.
# Every test of the library, in the order of the cheap ones first: a candidate is caught by the first that fails, and one
# that no test fails is a survivor, so a list that leaves a test out turns what that test sees into a survivor.
TESTS = ["testRender_vectors", "testRender_parts", "testRender_gradient", "testRender", "testRender_limits",
         "testRender_conformance", "testRender_plane", "testRender_placeholder", "testGlyph_render", "testCore",
         "testCoverage", "testCoverage_limits", "testCoverage_report", "testRender_stroke", "testRender_stroke_text",
         "testRender_stroke_bracket", "testRender_exact", "testRender_dual", "testRender_dense", "probe"]
TESTS_BY_FILE = {path: TESTS for path in FILES}

REL = [("==", "!="), ("!=", "=="), ("<=", "<"), (">=", ">"), ("<", "<="), (">", ">=")]


def strip_strings(line):
    """Blank out string literals and comments so operators inside them are not found."""
    out = []
    i = 0
    in_s = None
    while i < len(line):
        c = line[i]
        if in_s:
            out.append(" ")
            if c == "\\":
                out.append(" ")
                i += 1
            elif c == in_s:
                in_s = None
        elif c in "\"'":
            in_s = c
            out.append(" ")
        elif line.startswith("/*", i):
            j = line.find("*/", i)
            j = len(line) if j < 0 else j + 2
            out.append(" " * (j - i))
            i = j - 1
        elif line.startswith("//", i):
            out.append(" " * (len(line) - i))
            break
        else:
            out.append(c)
        i += 1
    return "".join(out)


def unique_fragment(line, a, b, new_text):
    """Expand [a, b) of line until its text occurs once in the line; returns (fragment, replacement)."""
    lo, hi = a, b
    while True:
        frag = line[lo:hi]
        if line.count(frag) == 1:
            return frag, line[lo:a] + new_text + line[b:hi]
        if lo == 0 and hi == len(line):
            return None
        lo = max(0, lo - 3)
        hi = min(len(line), hi + 3)


def candidates(path):
    text = (ROOT / path).read_text().split("\n")
    flt = re.compile(LINE_FILTER[path]) if path in LINE_FILTER else None
    out = []
    in_comment = False
    for no, raw in enumerate(text, 1):
        stripped = raw.strip()
        if in_comment:
            if "*/" in raw:
                in_comment = False
            continue
        if stripped.startswith("/*") and "*/" not in stripped:
            in_comment = True
            continue
        if not stripped or stripped.startswith(("/*", "*", "//", "#")):
            continue
        if "ERR_INTERNAL" in raw:
            continue
        if flt and not flt.search(raw):
            continue
        code = strip_strings(raw)
        # relational and logical operators
        for m in re.finditer(r"(==|!=|<=|>=|&&|\|\||<<|>>|->|<|>)", code):
            op = m.group(1)
            if op in ("<<", ">>", "->"):
                continue
            if op in ("<", ">") and (code[m.start() - 1:m.start()] in ("<", ">", "-") or code[m.end():m.end() + 1] in ("=", "<", ">")):
                continue
            if op in ("<", ">") and "#include" in raw:
                continue
            if op == "&&":
                new = "||"
            elif op == "||":
                new = "&&"
            else:
                new = dict(REL)[op]
            f = unique_fragment(raw, m.start(), m.end(), new)
            if f:
                out.append((no, "op%s" % op, f[0], f[1]))
        # negate a condition
        for kw in ("if (", "while ("):
            idx = code.find(kw)
            if idx >= 0 and (code[:idx].strip() in ("", "}", "} else")):
                start = idx + len(kw)
                depth = 1
                j = start
                while j < len(code) and depth:
                    if code[j] == "(":
                        depth += 1
                    elif code[j] == ")":
                        depth -= 1
                    j += 1
                if depth == 0:
                    cond = raw[start:j - 1]
                    f = unique_fragment(raw, start - 1, j, "(!(" + cond + "))")
                    if f:
                        out.append((no, "neg", f[0], f[1]))
        # delete a statement
        s = stripped
        if s.endswith(";") and not s.startswith(("return", "break", "continue", "goto", "case ", "default", "typedef", "static ", "const ", "int ", "size_t ", "uint", "GSHSCENE_", "GSHGEOM_", "struct ", "unsigned ", "double ", "void ", "char ", "else", "}", "{")) :
            if re.match(r"^[A-Za-z_][A-Za-z0-9_>.\[\]\-*() ]*(\(|=|\+\+|--|\+=|-=)", s) or s.endswith("++;") or s.endswith("--;"):
                indent = raw[: len(raw) - len(raw.lstrip())]
                f = unique_fragment(raw, len(indent), len(raw), ";")
                if f:
                    out.append((no, "del", f[0], f[1]))
        # hash width: a field written with another width
        for old_fn, new_fn in (("gshgeom_hash_u8(", "gshgeom_hash_u32("), ("gshgeom_hash_u32(", "gshgeom_hash_u64("), ("gshgeom_hash_u64(", "gshgeom_hash_u32(")):
            idx = raw.find(old_fn)
            if idx >= 0:
                f = unique_fragment(raw, idx, idx + len(old_fn), new_fn)
                if f:
                    out.append((no, "hw" + old_fn[len("gshgeom_hash_"):-1], f[0], f[1]))
        # a string literal that is output text or a name: its first letter changed
        if path in STRING_FILES:
            for m in re.finditer(r'"([A-Za-z][^"\\]*)"', raw):
                lit = m.group(1)
                if lit.startswith(("/dev", "%")) and False:
                    continue
                a = m.start(1)
                ch = raw[a]
                new_ch = "Z" if ch != "Z" else "Y"
                f = unique_fragment(raw, a, a + 1, new_ch)
                if f:
                    out.append((no, "str", f[0], f[1]))
            for m in re.finditer(r"%016|%08|%02x", raw):
                f = unique_fragment(raw, m.start(), m.end(), "%" if m.group(0) != "%02x" else "%x")
                if f:
                    out.append((no, "wd", f[0], f[1]))
        # swap returned results
        m = re.search(r"return (GS(HSCENE|HCPU)_ERR_(INVALID|LIMIT|UNSUPPORTED|OOM|REFUSED));", raw)
        if m:
            swap = {"INVALID": "LIMIT", "LIMIT": "INVALID", "UNSUPPORTED": "INVALID", "OOM": "LIMIT", "REFUSED": "LIMIT"}[m.group(3)]
            f = unique_fragment(raw, m.start(1), m.end(1), "GS%s_ERR_%s" % (m.group(2), swap))
            if f:
                out.append((no, "ret", f[0], f[1]))
    return out


STRING_FILES = {"src/render/hash.c", "src/render/sink.c"}


def main():
    plants = []
    for path, tag in FILES.items():
        seen = {}
        for no, kind, frag, new in candidates(path):
            k = (no, kind)
            seen[k] = seen.get(k, 0) + 1
            pid = "%s-%d-%s%s" % (tag, no, {"op==": "eq", "op!=": "ne", "op<=": "le", "op>=": "ge", "op<": "lt", "op>": "gt",
                                            "op&&": "and", "op||": "or"}.get(kind, kind), "" if seen[k] == 1 else str(seen[k]))
            if pid in EXCL:
                continue
            plants.append((pid, "srcl", path, "%d:%s" % (no, frag), new, TESTS_BY_FILE[path]))
    if "--new" in sys.argv:
        # Only the candidates that tools/render-plants.json does not hold (the ones to sweep after a source edit).
        held = {json.dumps(e[:5]) for e in json.load(open(ROOT / "tools" / "render-plants.json"))}
        plants = [p for p in plants if json.dumps(list(p[:5])) not in held]
    json.dump(plants, sys.stdout)


main()
