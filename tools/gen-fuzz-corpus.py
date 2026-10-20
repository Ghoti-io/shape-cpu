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


def fill(pick, map_bytes=b"\x00", rule=0):
    # op 0, the resource pick (0..: a path; 3 the fast run, 7 the general run), the map, a paint (rgba, space), rule
    return bytes([0, pick]) + map_bytes + bytes([200, 100, 50, 255, 1, rule])


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
    # Found by the first libFuzzer soak (story 16): strict, a damage and a small cell cap. The damage-limited render
    # passes and the whole render meets the cap outside the damage, so it is refused; the harness had allowed only a
    # region limit there.
    out["regression-strict-cells-outside-damage.bin"] = base64.b64decode(
        "cQsAMplAJm0ZT3ZmwR6Jf2Fd7lDCRLR/QSt6/zbm6uHj7bR6JLZqECw6JO+wOwclMCTHsAvBvaXFi6p+svmrTJ+KgxAuG0woagxRNjJeiGcC"
        "myM9FiyKlJxCJFFWzIkK8SvFqHMplT7Bdyd9VFOqgVUfzMjmgcV+F/qsYJOfKS/fHjRpgYuTPZ9DGeivfs7ibhEb5CtwDwGioZomrmEA7NLX"
        "H5rRQNlNKpMl8Og98ySqsAWGgpW+WBAM1grAFdQqD5fiB4thMQSas5I3pmv8sPE=")
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
