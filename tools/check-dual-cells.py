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
"""Judge the cells in which the two exact steps differ (tools/exact-dual.c) against the exact area, in rational arithmetic.

    check-dual-cells.py CELLS.txt [--limit N]

Each line of CELLS.txt is a cell the library's step and the earlier step gave different values for.  For every cell the exact
doubled area of its region is computed (tools/dense_reference.py: no rounding), the value each step should have given is the
rounding of it (a part's own overlap: the shared area, the dense value less the region's area, rounded with ties up; the regions
of UC8: the area, ties up), and the cell is classified: the exact value is a tie (both steps are within half a doubled unit of
it and the rounding of a tie is the convention's), the library's value is the rounding, the earlier step's value is, or
neither is.  Cells too large for the reference are counted and not judged.  Exits 1 if the library's step is wrong in any cell it
judged and the exact value is not a tie.
"""
import sys
from fractions import Fraction as F
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dense_reference import area2, wtop_of, rounded  # noqa: E402


def parse(line):
    head, *rest = line.split(" | ")
    t = head.split()
    mode, parts, even_odd, complement, e0, e1, e2 = map(int, t[1:8])
    new, old = int(t[9]), int(t[11])
    pieces, a_left, dense = [], [], []
    for chunk in rest:
        nums = list(map(int, chunk.split()))
        a_left.append(nums[0])
        dense.append(nums[1])
        body = nums[2:]
        pieces.append([tuple(body[i:i + 4]) for i in range(0, len(body), 4)])
    return mode, parts, bool(even_odd), bool(complement), [e0, e1, e2], new, old, pieces, a_left, dense


def main():
    path = sys.argv[1]
    limit = 3_000_000
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    counts = dict(cells=0, too_large=0, tie=0, new_right=0, old_right=0, neither=0, new_wrong_not_tie=0)
    worst_new = F(0)
    worst_old = F(0)
    for line in open(path):
        if not line.startswith("CELL"):
            continue
        mode, parts, even_odd, complement, eps, new, old, pieces, a_left, dense = parse(line)
        counts["cells"] += 1
        wtops = [wtop_of(pieces[p], a_left[p]) for p in range(parts)]
        u = area2(pieces, wtops, eps, mode, even_odd, complement, limit)
        if u is None:
            counts["too_large"] += 1
            continue
        q = (F(dense[0]) - u) if mode == 0 else u
        want = rounded(q)
        is_tie = (q + F(1, 2)).denominator == 1
        # the step's value: out is the shared area for a part's own overlap, the area otherwise
        worst_new = max(worst_new, abs(F(new) - q))
        worst_old = max(worst_old, abs(F(old) - q))
        if new == want and old == want:
            continue
        if is_tie:
            counts["tie"] += 1
        elif new == want:
            counts["new_right"] += 1
        elif old == want:
            counts["old_right"] += 1
            counts["new_wrong_not_tie"] += 1
        else:
            counts["neither"] += 1
            counts["new_wrong_not_tie"] += 1
    print("cells judged:", counts["cells"] - counts["too_large"], "of", counts["cells"], "(%d too large for the reference)" % counts["too_large"])
    print("  the exact value is a tie, the two steps rounded it differently:", counts["tie"])
    print("  the library's step is the rounding of the exact value and the earlier one is not:", counts["new_right"])
    print("  the earlier step is the rounding and the library's is not:", counts["old_right"])
    print("  neither is:", counts["neither"])
    print("  the worst distance from the exact value, in doubled units: library %.6f, earlier %.6f" % (float(worst_new), float(worst_old)))
    return 1 if counts["new_wrong_not_tie"] else 0


if __name__ == "__main__":
    sys.exit(main())
