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

    python3 tools/keep-caught-plants.py CANDIDATES.json PLANTED.log... > tools/render-plants.json

CANDIDATES.json is what tools/gen-render-plants.py wrote; each PLANTED.log is
the output of a `make check-planted` that ran them.  A candidate is kept when a
log says `ok` for its id; one that did not compile, or that no test caught, is
not (check-planted.py's docstring says why each survivor cannot be seen).
"""
import json
import re
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    candidates = json.load(open(sys.argv[1]))
    caught = set()
    for log in sys.argv[2:]:
        for line in open(log):
            m = re.match(r"ok\s+(\S+)\s+caught by", line)
            if m:
                caught.add(m.group(1))
    kept = [c for c in candidates if c[0] in caught]
    json.dump(kept, sys.stdout, indent=0)
    sys.stdout.write("\n")
    print("kept %d of %d candidates" % (len(kept), len(candidates)), file=sys.stderr)


if __name__ == "__main__":
    main()
