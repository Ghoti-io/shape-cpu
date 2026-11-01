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
"""A behavior probe for PDF's two group flags, through any PDF renderer (masks-images-patterns story M3, decision M3-D3).

The groups' definitions (T1 to T5 of shape-scene's transparency model, KC1 to KC11 of render.h) are written and checked here
against the library's own restatements; a lockstep misreading of what "the knockout backdrop" is would be shared by all of
them.  This probe asks a renderer instead, on minimal pairs: the same page drawn with a flag set and with it unset, where the
flag decides the pixel.  It supplies no value reference (a renderer's numbers are not ours to match: AD-18); it confirms which
way the convention goes.

    python3 tools/probe-groups.py --write DIR       write the six PDFs of three pairs into DIR
    python3 tools/probe-groups.py --check NAME DIR  read DIR/<pair>-<flag>.png (or .ppm) as NAME rendered them and judge each pair
    python3 tools/probe-groups.py --selftest        the pairs' expected pixels, and the judgement on renders made here

A pair is judged against the pixel the definition decides, an interior one away from every edge:

  - the pair must differ exactly where the definition's two pixels differ (they always do here: the pairs are made so);
  - a renderer on which the flag-on page equals the flag-off page ignores the flag: it is recorded as ignoring it and excluded
    (the pair acquits the probe, not the renderer), and the run does not fail on it;
  - a renderer that honors the flag must give the pixel the definition gives, within one level of 255 (the renderers round);
    an honoring renderer that gives another pixel is a failure, which is the point.

Under `GHOTI_ORACLE_REQUIRED=1` a missing render is a failure, not a skip.

The pairs (a 64 by 64 page over a backdrop of (0.8, 0.4, 0.2), the group drawn as a form XObject whose `/Group` names the flags):

  non-isolated        a multiply fill of (0.5, 0.5, 1) inside the form: a non-isolated form multiplies with the page
                      ((0.4, 0.2, 0.2)); an isolated one has nothing beneath the fill and shows it ((0.5, 0.5, 1)).
  knockout            two overlapping fills of 50% red and then 50% green inside a non-isolated form: in a knockout form each
                      composites with the page, so the second replaces the first where they overlap (half green over the page:
                      (0.4, 0.7, 0.1)); in an ordinary one the green is over the red over the page ((0.45, 0.6, 0.05)).
  isolated-knockout   the same two fills in an isolated form: a knockout group of an isolated form has no backdrop to knock back
                      to but the nothing beneath it, so the second still replaces the first and the form is half green over the
                      page ((0.4, 0.7, 0.1)); in an ordinary isolated form the green is over the red and the form (alpha 3/4)
                      is over the page ((0.45, 0.6, 0.05)).
"""

import os
import struct
import sys
import zlib
from fractions import Fraction as F

PAGE = 64
BACKDROP = (F(8, 10), F(4, 10), F(2, 10))
PIXEL = (40, 40)  # inside the overlap of the two fills (and of the one), away from every edge


def over(src, alpha, dst):
    """Source-over of a straight color with alpha onto an opaque color."""
    return tuple(alpha * s + (1 - alpha) * d for s, d in zip(src, dst))


def multiply(src, dst):
    return tuple(s * d for s, d in zip(src, dst))


def expected():
    """The pixel the definition decides for each pair and flag: {pair: {flag: (r, g, b)}}."""
    red, green = (F(1), F(0), F(0)), (F(0), F(1), F(0))
    mul = (F(1, 2), F(1, 2), F(1))
    half = F(1, 2)
    page = BACKDROP
    out = {}
    # non-isolated: the multiply group inside sees the page, or sees nothing and shows its source.
    out["non-isolated"] = {"on": multiply(mul, page), "off": mul}
    # knockout (non-isolated form): the second fill composites with the page; ordinary: over the first.
    first = over(red, half, page)
    out["knockout"] = {"on": over(green, half, page), "off": over(green, half, first)}
    # isolated forms: the second composites with nothing (knockout) or over the first, and the form over the page after.
    # An isolated knockout form's elements composite with its initial backdrop, which is nothing: the second fill replaces
    # the first (half green, alpha 1/2), then the form is composited over the page.
    kg = over(green, half, page)  # half green over the page: the form's result is half green, composited over the page
    ordinary_alpha = half + half * (1 - half)  # green over red, both at one half: alpha 3/4
    ordinary_color = tuple((half * g + (1 - half) * half * r) for g, r in zip(green, red))  # premultiplied
    ordinary = tuple(c + (1 - ordinary_alpha) * p for c, p in zip(ordinary_color, page))
    out["isolated-knockout"] = {"on": kg, "off": ordinary}
    return out


def pdf(pair, flag):
    """One page as a PDF: the backdrop, then the form that holds the pair's content, its /Group naming the flags."""
    on = flag == "on"
    if pair == "non-isolated":
        isolated, knockout = (not on), False
        content = b"/G1 gs 0.5 0.5 1 rg 8 8 48 48 re f"
        gs = b"<< /Type /ExtGState /BM /Multiply >>"
    elif pair == "knockout":
        isolated, knockout = False, on
        content = b"/G1 gs 1 0 0 rg 8 8 40 40 re f 0 1 0 rg 24 24 32 32 re f"
        gs = b"<< /Type /ExtGState /ca 0.5 /CA 0.5 >>"
    else:
        isolated, knockout = True, on
        content = b"/G1 gs 1 0 0 rg 8 8 40 40 re f 0 1 0 rg 24 24 32 32 re f"
        gs = b"<< /Type /ExtGState /ca 0.5 /CA 0.5 >>"
    group = b"<< /S /Transparency /I %s /K %s /CS /DeviceRGB >>" % (b"true" if isolated else b"false", b"true" if knockout else b"false")
    page_content = b"0.8 0.4 0.2 rg 0 0 64 64 re f /F1 Do"
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 64 64] /Contents 4 0 R /Resources << /XObject << /F1 5 0 R >> >> /Group << /S /Transparency /CS /DeviceRGB >> >>",
        b"<< /Length %d >>\nstream\n%s\nendstream" % (len(page_content), page_content),
        b"<< /Type /XObject /Subtype /Form /BBox [0 0 64 64] /Group %s /Resources << /ExtGState << /G1 6 0 R >> >> /Length %d >>\nstream\n%s\nendstream"
        % (group, len(content), content),
        gs,
    ]
    out = b"%PDF-1.7\n"
    offsets = []
    for i, body in enumerate(objects):
        offsets.append(len(out))
        out += b"%d 0 obj\n%s\nendobj\n" % (i + 1, body)
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for o in offsets:
        out += b"%010d 00000 n \n" % o
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    return out


PAIRS = ["non-isolated", "knockout", "isolated-knockout"]


def read_pixel(path):
    """The pixel PIXEL of a P6 PPM, or of a PNG of 8-bit RGB or RGBA (filters 0 to 4, no interlace), as 0..255 integers."""
    data = open(path, "rb").read()
    if data[:2] == b"P6":
        parts = data.split(None, 4)
        w, h = int(parts[1]), int(parts[2])
        raw = data[len(data) - w * h * 3:]
        x, y = PIXEL
        return tuple(raw[(y * w + x) * 3 + k] for k in range(3))
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos = 8
    idat = b""
    w = h = depth = ctype = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and interlace == 0, "the probe reads 8-bit RGB or RGBA, not interlaced"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    channels = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * channels
    prev = bytearray(stride)
    rows = []
    at = 0
    for _ in range(h):
        ft = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    x, y = PIXEL
    return tuple(rows[y][x * channels + k] for k in range(3))


def judge(name, pixels):
    """pixels: {pair: {flag: (r, g, b) or None}}.  Return (verdicts, failures): a verdict per pair, a failure for each wrong one."""
    want = expected()
    verdicts, failures = {}, []
    for pair in PAIRS:
        got = pixels.get(pair, {})
        if got.get("on") is None or got.get("off") is None:
            if os.environ.get("GHOTI_ORACLE_REQUIRED") == "1":
                failures.append("%s %s: a render is missing" % (name, pair))
            verdicts[pair] = "missing"
            continue
        if max(abs(a - b) for a, b in zip(got["on"], got["off"])) <= 1:
            verdicts[pair] = "ignores the flag (excluded)"
            continue
        ok = True
        for flag in ("on", "off"):
            exact = tuple(float(v * 255) for v in want[pair][flag])
            if max(abs(g - e) for g, e in zip(got[flag], exact)) > 1.5:
                ok = False
                failures.append("%s %s %s: got %s, the definition gives %s" % (name, pair, flag, got[flag], tuple(round(v, 1) for v in exact)))
        verdicts[pair] = "honors the flag" if ok else "honors the flag and differs from the definition"
    return verdicts, failures


def selftest():
    want = expected()
    # The pairs are made to differ, everywhere the definition says so.
    for pair in PAIRS:
        assert max(abs(a - b) for a, b in zip(want[pair]["on"], want[pair]["off"])) > F(1, 20), pair
    assert want["non-isolated"]["on"] == (F(2, 5), F(1, 5), F(1, 5)) and want["non-isolated"]["off"] == (F(1, 2), F(1, 2), F(1))
    assert want["knockout"]["on"] == (F(2, 5), F(7, 10), F(1, 10)) and want["knockout"]["off"] == (F(9, 20), F(3, 5), F(1, 20))
    assert want["isolated-knockout"] == want["knockout"], "the form's isolation does not matter to a knockout group over the page"
    assert want["knockout"]["on"] == over((F(0), F(1), F(0)), F(1, 2), BACKDROP)
    # A renderer that draws what the definition says honors both flags and passes.
    exact = {p: {f: tuple(int(round(float(v * 255))) for v in want[p][f]) for f in ("on", "off")} for p in PAIRS}
    verdicts, failures = judge("self", exact)
    assert not failures and all(v == "honors the flag" for v in verdicts.values()), (verdicts, failures)
    # One that ignores the flag (the flag-on page drawn as the flag-off one) is recorded and excluded, and does not fail.
    ignoring = {p: {"on": exact[p]["off"], "off": exact[p]["off"]} for p in PAIRS}
    verdicts, failures = judge("self", ignoring)
    assert not failures and all("ignores" in v for v in verdicts.values()), (verdicts, failures)
    # One that honors the flag and draws the wrong pixel (here the knockout page without the knockout) fails: the gate fails.
    wrong = {p: dict(exact[p]) for p in PAIRS}
    wrong["knockout"] = {"on": exact["knockout"]["off"][::-1], "off": exact["knockout"]["off"]}
    verdicts, failures = judge("self", wrong)
    assert failures and "differs" in verdicts["knockout"], (verdicts, failures)
    # The PDFs are well formed: each object's offset is where the cross-reference says.
    for pair in PAIRS:
        for flag in ("on", "off"):
            body = pdf(pair, flag)
            start = int(body.split(b"startxref\n")[1].split(b"\n")[0])
            assert body[start:start + 4] == b"xref", (pair, flag)
            assert (b"/K true" in body) == (flag == "on" and pair != "non-isolated"), (pair, flag)
            assert (b"/I true" in body) == ((pair != "knockout") and not (pair == "non-isolated" and flag == "on")), (pair, flag)
    # The pixel reader on a PNG and a PPM made here.
    pixel = bytearray(PAGE * PAGE * 3)
    x, y = PIXEL
    pixel[(y * PAGE + x) * 3:(y * PAGE + x) * 3 + 3] = bytes([10, 20, 30])
    ppm = b"P6\n%d %d\n255\n" % (PAGE, PAGE) + bytes(pixel)
    rows = b"".join(b"\0" + bytes(pixel[r * PAGE * 3:(r + 1) * PAGE * 3]) for r in range(PAGE))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", PAGE, PAGE, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    for name, data in (("self.ppm", ppm), ("self.png", png)):
        path = os.path.join(os.environ.get("TMPDIR", "/tmp"), "probe-groups-" + name)
        with open(path, "wb") as f:
            f.write(data)
        assert read_pixel(path) == (10, 20, 30), name
        os.unlink(path)
    print("probe-groups: self-test ok")


def main(argv):
    if argv[1:] == ["--selftest"]:
        selftest()
        return 0
    if len(argv) == 3 and argv[1] == "--write":
        os.makedirs(argv[2], exist_ok=True)
        for pair in PAIRS:
            for flag in ("on", "off"):
                with open(os.path.join(argv[2], "%s-%s.pdf" % (pair, flag)), "wb") as f:
                    f.write(pdf(pair, flag))
        print("probe-groups: wrote %d PDFs to %s" % (2 * len(PAIRS), argv[2]))
        return 0
    if len(argv) == 4 and argv[1] == "--check":
        name, directory = argv[2], argv[3]
        pixels = {}
        for pair in PAIRS:
            pixels[pair] = {}
            for flag in ("on", "off"):
                got = None
                for ext in ("png", "ppm"):
                    path = os.path.join(directory, "%s-%s.%s" % (pair, flag, ext))
                    if os.path.exists(path):
                        got = read_pixel(path)
                pixels[pair][flag] = got
        verdicts, failures = judge(name, pixels)
        for pair in PAIRS:
            print("probe-groups: %s %s: %s" % (name, pair, verdicts[pair]))
        for line in failures:
            print("probe-groups: FAIL " + line, file=sys.stderr)
        return 1 if failures else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
