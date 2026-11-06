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
"""Move the held generated plants to the lines their text is on now.

    python3 tools/relocate-plants.py REV > moved.json && mv moved.json tools/render-plants.json

The generated plants of tools/render-plants.json are addressed by line (``LINE:FRAGMENT``), so a source edit that inserts or deletes
lines leaves the plants below it pointing at the wrong ones.  REV is the revision the table was last in step with (``HEAD`` before the
edit is committed).  For each file the lines of REV are aligned with the lines of the working tree (difflib's longest matching blocks);
an entry whose line was valid in REV (the fragment is on it) and whose line is still there, unchanged, moves to its new number if the fragment
is on it and occurs the same number of times; an entry on a line the edit changed, or that was stale in REV already, is left as it is, which
is what a sweep of the changed lines (tools/gen-render-plants.py --new) is for.  The ids keep their old numbers (they only have to be unique).
Prints the table on stdout and, on stderr, how many entries moved, stayed and were left.
"""
import difflib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rev = sys.argv[1]
    path = ROOT / "tools" / "render-plants.json"
    table = json.loads(path.read_text())
    maps = {}
    moved = stayed = left = 0
    for entry in table:
        if entry[1] != "srcl":
            continue
        fname = entry[2]
        if fname not in maps:
            old = subprocess.run(["git", "show", "%s:%s" % (rev, fname)], cwd=ROOT, capture_output=True, text=True).stdout.splitlines()
            new = (ROOT / fname).read_text().splitlines()
            mapping = {}
            for block in difflib.SequenceMatcher(None, old, new, autojunk=False).get_matching_blocks():
                for k in range(block.size):
                    mapping[block.a + k + 1] = block.b + k + 1
            maps[fname] = (old, new, mapping)
        old, new, mapping = maps[fname]
        line, fragment = entry[3].split(":", 1)
        n = int(line)
        if n > len(old) or fragment not in old[n - 1] or n not in mapping:
            left += 1
            continue
        m = mapping[n]
        if fragment not in new[m - 1] or old[n - 1].count(fragment) != new[m - 1].count(fragment):
            left += 1
            continue
        if m == n:
            stayed += 1
        else:
            entry[3] = "%d:%s" % (m, fragment)
            moved += 1
    json.dump(table, sys.stdout, indent=0)
    sys.stdout.write("\n")
    print("relocate-plants: %d moved, %d stayed, %d left (changed lines, or stale already)" % (moved, stayed, left), file=sys.stderr)


if __name__ == "__main__":
    main()
