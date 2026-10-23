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
"""Keep the generated plants a sweep was seen to catch (story 13b).

    python3 tools/keep-caught-plants.py [--merge HELD.json] CANDIDATES.json PLANTED.log... > tools/render-plants.json

With ``--merge HELD.json`` the candidates are the ones that tools/gen-render-plants.py --new wrote, and the output is HELD.json's
entries followed by the candidates that were caught; write it to a new file and move that over HELD.json, since the shell
empties a redirect's target before this reads it.

CANDIDATES.json is what tools/gen-render-plants.py wrote; each PLANTED.log is
the output of a `make check-planted` that ran them.  A candidate is kept when a
log says `ok` for its id; one that did not compile, or that no test caught, is
not (check-planted.py's docstring says why each survivor cannot be seen).
"""
import json
import re
import sys


def main():
    args = sys.argv[1:]
    held = []
    if args[:1] == ["--merge"] and len(args) >= 2:
        held = json.load(open(args[1]))
        args = args[2:]
    if len(args) < 2:
        sys.exit(__doc__)
    candidates = json.load(open(args[0]))
    caught = set()
    for log in args[1:]:
        for line in open(log):
            m = re.match(r"ok\s+(\S+)\s+caught by", line)
            if m:
                caught.add(m.group(1))
    kept = [c for c in candidates if c[0] in caught]
    json.dump(held + kept, sys.stdout, indent=0)
    sys.stdout.write("\n")
    print("kept %d of %d candidates%s" % (len(kept), len(candidates), " (and %d held)" % len(held) if held else ""), file=sys.stderr)


if __name__ == "__main__":
    main()
