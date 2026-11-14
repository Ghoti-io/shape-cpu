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
#
# Ghoti.io Shape CPU is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
"""
The filter oracle and its vectors (shape-scene filter.h, FL1 to FL20): an independent statement, in exact rationals, of what a
filter does to a layer, and the cases it commits as tests/filter_vectors.h.  It shares no code with the library.

Usage: gen-filter-vectors.py [--check]    writes (or checks) tests/filter_vectors.h

The model.  A layer is a canvas of W by H device pixels of premultiplied RGBA in the group's blend space, here sRGB.  A filter
region and a primitive subregion are rectangles in user space; the group's map takes the user space to the device by a scale, a
mirror or a quarter turn and a translation (D-FL8).  A pixel's weight in a rectangle is the area of the pixel the rectangle covers
(FL8).  A length in user units becomes device pixels by the map's scale on the axis it lands on, an offset by the map's linear
part and then rounding to whole pixels (FL10).  Each primitive works in its own color space and its result is clipped to its
subregion; the filter's result is converted back to the layer's space and clipped to the region (FL20).

The transcendentals (the sRGB transfer, the Gaussian weights) are Decimal at 60 digits and the vectors carry their rounding to
double; the library is within 1e-12 of them.
"""

import math
import os
import sys
from decimal import Decimal, getcontext
from fractions import Fraction as F

getcontext().prec = 60

# --- exact helpers ---------------------------------------------------------------------------------------------------


def dec(x):
    return Decimal(x.numerator) / Decimal(x.denominator)


def frac(d):
    return F(d)


def clamp01(x):
    return F(0) if x < 0 else (F(1) if x > 1 else x)


def srgb_decode(c):
    """N2: x / 12.92 up to 0.04045, else ((x + 0.055) / 1.055)^2.4 (the linear segment continues below zero)."""
    if c <= F(4045, 100000):
        return c / F(1292, 100)
    return frac((dec(c + F(55, 1000)) / Decimal("1.055")) ** Decimal("2.4"))


def srgb_encode(y):
    if y <= F(31308, 10000000):
        return y * F(1292, 100)
    return frac(Decimal("1.055") * dec(y) ** (Decimal(1) / Decimal("2.4")) - Decimal("0.055"))


def convert(px, src, dst):
    """A premultiplied pixel from space src to dst ('srgb' or 'linear'); alpha 0 is 0."""
    if src == dst:
        return list(px)
    r, g, b, a = px
    if a == 0:
        return [F(0)] * 4
    c = [r / a, g / a, b / a]
    if src == "srgb":
        c = [srgb_decode(v) for v in c]
    if dst == "srgb":
        c = [srgb_encode(v) for v in c]
    return [c[0] * a, c[1] * a, c[2] * a, a]


# --- the geometry ----------------------------------------------------------------------------------------------------


class Map:
    """x_dev = a x + b y + e, y_dev = c x + d y + f; axis aligned (b = c = 0 or a = d = 0)."""

    def __init__(self, a, b, c, d, e, f):
        self.a, self.b, self.c, self.d, self.e, self.f = [F(v) for v in (a, b, c, d, e, f)]
        assert (self.b == 0 and self.c == 0) or (self.a == 0 and self.d == 0)
        self.swap = self.a == 0 and self.d == 0

    def point(self, x, y):
        return (self.a * x + self.b * y + self.e, self.c * x + self.d * y + self.f)

    def inverse_point(self, X, Y):
        det = self.a * self.d - self.b * self.c
        X, Y = X - self.e, Y - self.f
        return ((self.d * X - self.b * Y) / det, (-self.c * X + self.a * Y) / det)

    def rect_to_device(self, r):
        pts = [self.point(r[0], r[1]), self.point(r[2], r[1]), self.point(r[2], r[3]), self.point(r[0], r[3])]
        return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))

    def rect_to_user(self, r):
        pts = [self.inverse_point(r[0], r[1]), self.inverse_point(r[2], r[1]), self.inverse_point(r[2], r[3]),
               self.inverse_point(r[0], r[3])]
        return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))

    def device_lengths(self, ux, uy):
        """A pair of lengths along the user axes, as lengths along the device axes."""
        if not self.swap:
            return (abs(self.a) * ux, abs(self.d) * uy)
        return (abs(self.b) * uy, abs(self.c) * ux)

    def device_vector(self, ux, uy):
        return (self.a * ux + self.b * uy, self.c * ux + self.d * uy)


def pixel_weights(lo, hi, n):
    """The area of each pixel (width n) the interval [lo, hi] covers."""
    out = []
    for i in range(n):
        out.append(max(F(0), min(F(i + 1), hi) - max(F(i), lo)))
    return out


def clip(img, rect_dev, W, H):
    wx = pixel_weights(rect_dev[0], rect_dev[2], W)
    wy = pixel_weights(rect_dev[1], rect_dev[3], H)
    out = []
    for j in range(H):
        for i in range(W):
            w = wx[i] * wy[j]
            p = img[j * W + i]
            out.append([c * w for c in p])
    return out


# --- the primitives -------------------------------------------------------------------------------------------------


def unpremultiply(p):
    r, g, b, a = p
    if a == 0:
        return [F(0), F(0), F(0), F(0)]
    return [r / a, g / a, b / a, a]


def premultiply(c):
    return [c[0] * c[3], c[1] * c[3], c[2] * c[3], c[3]]


def shift(img, dx, dy, W, H):
    out = []
    for j in range(H):
        for i in range(W):
            si, sj = i - dx, j - dy
            out.append(list(img[sj * W + si]) if 0 <= si < W and 0 <= sj < H else [F(0)] * 4)
    return out


def over(top, bottom):
    ta = top[3]
    return [top[k] + bottom[k] * (1 - ta) for k in range(4)]


def blend_function(mode, cb, cs):
    if mode == 0:  # normal
        return cs
    if mode == 23:  # multiply
        return cb * cs
    if mode == 13:  # screen
        return cb + cs - cb * cs
    if mode == 21:  # difference
        return abs(cb - cs)
    raise ValueError(mode)


def blend_pixel(mode, top, back):
    ab, as_ = back[3], top[3]
    out = []
    ub, us = unpremultiply(back), unpremultiply(top)
    for k in range(3):
        b_term = blend_function(mode, ub[k], us[k]) if (ab != 0 and as_ != 0) else F(0)
        out.append(top[k] * (1 - ab) + back[k] * (1 - as_) + as_ * ab * b_term)
    out.append(as_ + ab - as_ * ab)
    return out


def composite_pixel(op, k, i1, i2):
    a1, a2 = i1[3], i2[3]
    if op == "over":
        return [i1[c] + i2[c] * (1 - a1) for c in range(4)]
    if op == "in":
        return [i1[c] * a2 for c in range(4)]
    if op == "out":
        return [i1[c] * (1 - a2) for c in range(4)]
    if op == "atop":
        return [i1[c] * a2 + i2[c] * (1 - a1) for c in range(4)]
    if op == "xor":
        return [i1[c] * (1 - a2) + i2[c] * (1 - a1) for c in range(4)]
    assert op == "arithmetic"
    out = [k[0] * i1[c] * i2[c] + k[1] * i1[c] + k[2] * i2[c] + k[3] for c in range(4)]
    out[3] = clamp01(out[3])
    for c in range(3):
        out[c] = max(F(0), min(out[c], out[3]))
    return out


def matrix_pixel(mtype, vals, p):
    u = unpremultiply(p)
    r, g, b, a = u
    if mtype == "matrix":
        out = []
        for row in range(4):  # four rows of five over (r, g, b, a, 1)
            base = 5 * row
            out.append(vals[base] * r + vals[base + 1] * g + vals[base + 2] * b + vals[base + 3] * a + vals[base + 4])
    elif mtype == "saturate":
        s = vals[0]
        out = [(F(213, 1000) + F(787, 1000) * s) * r + (F(715, 1000) - F(715, 1000) * s) * g + (F(72, 1000) - F(72, 1000) * s) * b,
               (F(213, 1000) - F(213, 1000) * s) * r + (F(715, 1000) + F(285, 1000) * s) * g + (F(72, 1000) - F(72, 1000) * s) * b,
               (F(213, 1000) - F(213, 1000) * s) * r + (F(715, 1000) - F(715, 1000) * s) * g + (F(72, 1000) + F(928, 1000) * s) * b,
               a]
    elif mtype == "hue":
        th = math.radians(float(vals[0]))
        c, s = F(math.cos(th)), F(math.sin(th))
        out = [(F(213, 1000) + c * F(787, 1000) - s * F(213, 1000)) * r + (F(715, 1000) - c * F(715, 1000) - s * F(715, 1000)) * g +
               (F(72, 1000) - c * F(72, 1000) + s * F(928, 1000)) * b,
               (F(213, 1000) - c * F(213, 1000) + s * F(143, 1000)) * r + (F(715, 1000) + c * F(285, 1000) + s * F(140, 1000)) * g +
               (F(72, 1000) - c * F(72, 1000) - s * F(283, 1000)) * b,
               (F(213, 1000) - c * F(213, 1000) - s * F(787, 1000)) * r + (F(715, 1000) - c * F(715, 1000) + s * F(715, 1000)) * g +
               (F(72, 1000) + c * F(928, 1000) + s * F(72, 1000)) * b,
               a]
    else:
        assert mtype == "lum"
        out = [F(0), F(0), F(0), F(2125, 10000) * r + F(7154, 10000) * g + F(721, 10000) * b]
    out = [clamp01(v) for v in out]
    return premultiply(out)


def transfer_value(t, c):
    kind = t[0]
    if kind == "identity":
        return c
    if kind == "table":
        v = t[1]
        n = len(v)
        if n == 1:
            return v[0]
        k = min(int(math.floor(c * (n - 1))), n - 2)
        return v[k] + (c - F(k, n - 1)) * (n - 1) * (v[k + 1] - v[k])
    if kind == "discrete":
        v = t[1]
        n = len(v)
        k = min(int(math.floor(c * n)), n - 1)
        return v[k]
    if kind == "linear":
        return t[1] * c + t[2]
    if kind == "gamma":
        amp, ex, off = t[1], t[2], t[3]
        return amp * F(float(c) ** float(ex)) + off if c != 0 or ex != 0 else amp + off
    raise ValueError(kind)


def transfer_pixel(ts, p):
    u = unpremultiply(p)
    out = [clamp01(transfer_value(ts[k], u[k])) for k in range(4)]
    return premultiply(out)


def edge_value(line, j, mode):
    n = len(line)
    if 0 <= j < n:
        return line[j]
    if mode == "duplicate":
        return line[0] if j < 0 else line[n - 1]
    if mode == "wrap":
        return line[j % n]
    return F(0)


def box_blur_line(line, w, off, mode="none"):
    n = len(line)
    out = []
    for i in range(n):
        s = F(0)
        for t in range(w):
            s += edge_value(line, i - off + t, mode)
        out.append(s / w)
    return out


def gaussian_kernel(s):
    r = int(math.ceil(float(s) * 3))
    ds = Decimal(s.numerator) / Decimal(s.denominator)
    weights = [frac((-(Decimal(k) ** 2) / (2 * ds * ds)).exp()) for k in range(-r, r + 1)]
    total = sum(weights, F(0))
    return r, [w / total for w in weights]


def gauss_line(line, s, mode="none"):
    r, ker = gaussian_kernel(s)
    n = len(line)
    out = []
    for i in range(n):
        acc = F(0)
        for t in range(-r, r + 1):
            acc += ker[t + r] * edge_value(line, i + t, mode)
        out.append(acc)
    return out


def blur_axis_line(line, s, mode="none"):
    """One axis of a blur by deviation s (device pixels, a Fraction)."""
    if s == 0:
        return line
    if s >= 2:
        d = int(math.floor(float(s) * 3 * math.sqrt(2 * math.pi) / 4 + 0.5))
        if d % 2 == 1:
            for _ in range(3):
                line = box_blur_line(line, d, (d - 1) // 2, mode)
        else:
            line = box_blur_line(line, d, d // 2, mode)
            line = box_blur_line(line, d, d // 2 - 1, mode)
            line = box_blur_line(line, d + 1, d // 2, mode)
        return line
    return gauss_line(line, s, mode)


def blur(img, sx, sy, W, H, mode="none", span=None):
    if sx == 0 and sy == 0:
        return [list(p) for p in img]
    x0, y0, x1, y1 = (0, 0, W, H) if mode == "none" or span is None else span
    if x1 <= x0 or y1 <= y0:
        return [list(p) for p in img]
    out = [list(p) for p in img]
    for c in range(4):
        if sx != 0:
            for j in range(y0, y1):
                row = blur_axis_line([out[j * W + i][c] for i in range(x0, x1)], sx, mode)
                for i in range(x0, x1):
                    out[j * W + i][c] = row[i - x0]
        if sy != 0:
            for i in range(x0, x1):
                col = blur_axis_line([out[j * W + i][c] for j in range(y0, y1)], sy, mode)
                for j in range(y0, y1):
                    out[j * W + i][c] = col[j - y0]
    return out


def whole_px(v):
    return int(math.floor(v + F(1, 2)))


def morphology(img, rx, ry, dilate, W, H):
    out = []
    for j in range(H):
        for i in range(W):
            px = []
            for c in range(4):
                best = None
                for dy in range(-ry, ry + 1):
                    for dx in range(-rx, rx + 1):
                        x, y = i + dx, j + dy
                        v = img[y * W + x][c] if 0 <= x < W and 0 <= y < H else F(0)
                        if best is None or (v > best if dilate else v < best):
                            best = v
                px.append(best)
            out.append(px)
    return out


def edge_pixel(img, W, r, x, y, mode):
    if x < r[0] or x >= r[2] or y < r[1] or y >= r[3]:
        if mode == "duplicate":
            x = min(max(x, r[0]), r[2] - 1)
            y = min(max(y, r[1]), r[3] - 1)
        elif mode == "wrap":
            x = r[0] + (x - r[0]) % (r[2] - r[0])
            y = r[1] + (y - r[1]) % (r[3] - r[1])
        else:
            return [F(0)] * 4
    return list(img[y * W + x])


def convolve(img, pr, W, H, r):
    ox, oy = pr["order"]
    tx, ty = pr["target"]
    k = [F(v) for v in pr["kernel"]]
    div, bias = F(pr["divisor"]), F(pr.get("bias", 0))
    out = []
    for j in range(H):
        for i in range(W):
            sm = [F(0)] * 4
            for ky in range(oy):
                for kx in range(ox):
                    p = edge_pixel(img, W, r, i - tx + kx, j - ty + ky, pr["edge"])
                    if pr["preserve"]:
                        p = unpremultiply(p)
                    kv = k[(oy - 1 - ky) * ox + (ox - 1 - kx)]
                    for c in range(4):
                        sm[c] += p[c] * kv
            if pr["preserve"]:
                a = img[j * W + i][3]
                out.append(premultiply([clamp01(sm[0] / div + bias), clamp01(sm[1] / div + bias), clamp01(sm[2] / div + bias), a]))
            else:
                a = clamp01(sm[3] / div + bias)
                px = [max(F(0), min(sm[c] / div + bias * a, a)) for c in range(3)]
                out.append(px + [a])
    return out


def tile(img, W, H, r):
    rw, rh = r[2] - r[0], r[3] - r[1]
    out = []
    for j in range(H):
        for i in range(W):
            if rw <= 0 or rh <= 0:
                out.append([F(0)] * 4)
            else:
                out.append(list(img[(r[1] + (j - r[1]) % rh) * W + r[0] + (i - r[0]) % rw]))
    return out


def displace(img, mp, scale_x, scale_y, xch, ych, W, H):
    out = []
    for j in range(H):
        for i in range(W):
            u = unpremultiply(mp[j * W + i])
            sx = whole_px(i + scale_x * (u[xch] - F(1, 2)))
            sy = whole_px(j + scale_y * (u[ych] - F(1, 2)))
            out.append(list(img[sy * W + sx]) if 0 <= sx < W and 0 <= sy < H else [F(0)] * 4)
    return out


# --- the filter ------------------------------------------------------------------------------------------------------


def floor_half(v):
    return int(math.floor(v + F(1, 2)))


def pixel_rect(m, u, rnd, W, H):
    d = m.rect_to_device(u)
    o = [d[0], d[1], d[2], d[3]]
    out = []
    for k in range(4):
        v = math.floor(o[k] + F(1, 2)) if rnd else (math.floor(o[k]) if k < 2 else math.ceil(o[k]))
        lim = W if k % 2 == 0 else H
        out.append(int(min(max(v, 0), lim)))
    return out


def run_filter(case):
    """The case's result: the layer after the filter, in the layer's space (sRGB)."""
    W, H = case["w"], case["h"]
    m = case["map"]
    layer_space = "srgb"
    fspace = case["space"]
    canvas_dev = (F(0), F(0), F(W), F(H))
    if case["region"] is None:
        region_u = m.rect_to_user(canvas_dev)
    else:
        x, y, w, h = [F(v) for v in case["region"]]
        region_u = (x, y, x + w, y + h)
    region_d = m.rect_to_device(region_u)
    source = clip(case["src"], region_d, W, H)
    results = []  # (pixels, space, subregion in user space)

    def get(spec, i):
        kind = spec[0]
        if kind == "prev":
            return get(("src",), i) if i == 0 else get(("res", i - 1), i)
        if kind == "src":
            return (source, layer_space, region_u)
        if kind == "alpha":
            return ([[F(0), F(0), F(0), p[3]] for p in source], None, region_u)
        return results[spec[1]]

    for i, pr in enumerate(case["prims"]):
        space = pr.get("space") or fspace
        ins = []
        for spec in pr["inputs"]:
            px, sp, sub = get(spec, i)
            if sp is not None and sp != space:
                px = [convert(p, sp, space) for p in px]
            ins.append((px, sub))
        default = region_u
        if ins:
            default = (min(s[0] for _, s in ins), min(s[1] for _, s in ins), max(s[2] for _, s in ins), max(s[3] for _, s in ins))
        sub = list(default)
        if "sub" in pr:
            for k in range(4):
                v = pr["sub"][k]
                if v is None:
                    continue
                if k == 0:
                    sub[0] = F(v)
                elif k == 1:
                    sub[1] = F(v)
                elif k == 2:
                    sub[2] = sub[0] + F(v)
                else:
                    sub[3] = sub[1] + F(v)
        sub = tuple(sub)
        kind = pr["kind"]
        if kind == "offset":
            ddx, ddy = m.device_vector(F(pr["dx"]), F(pr["dy"]))
            out = shift(ins[0][0], floor_half(ddx), floor_half(ddy), W, H)
        elif kind == "flood":
            col = pr["color"]
            cs = pr.get("color_space", "srgb")
            c = convert([F(col[0]) * F(col[3]), F(col[1]) * F(col[3]), F(col[2]) * F(col[3]), F(col[3])], cs, space)
            op = F(pr["opacity"])
            px = [v * op for v in c]
            out = [list(px) for _ in range(W * H)]
        elif kind == "merge":
            out = [list(p) for p in ins[0][0]]
            for px, _ in ins[1:]:
                out = [over(px[q], out[q]) for q in range(W * H)]
        elif kind == "blend":
            out = [blend_pixel(pr["mode"], ins[0][0][q], ins[1][0][q]) for q in range(W * H)]
        elif kind == "composite":
            k = [F(v) for v in pr.get("k", [0, 0, 0, 0])]
            out = [composite_pixel(pr["op"], k, ins[0][0][q], ins[1][0][q]) for q in range(W * H)]
        elif kind == "matrix":
            vals = [F(v) for v in pr["values"]]
            out = [matrix_pixel(pr["type"], vals, p) for p in ins[0][0]]
        elif kind == "transfer":
            ts = []
            for t in pr["funcs"]:
                if t[0] == "identity":
                    ts.append(("identity",))
                elif t[0] in ("table", "discrete"):
                    ts.append((t[0], [F(v) for v in t[1]]))
                else:
                    ts.append((t[0],) + tuple(F(v) for v in t[1:]))
            out = [transfer_pixel(ts, p) for p in ins[0][0]]
        elif kind == "blur":
            sx, sy = m.device_lengths(F(pr["sx"]), F(pr["sy"]))
            span = pixel_rect(m, ins[0][1], 0, W, H)
            out = blur(ins[0][0], sx, sy, W, H, pr.get("edge", "none"), span)
        elif kind == "morph":
            dx, dy = m.device_lengths(F(pr["rx"]), F(pr["ry"]))
            rx, ry = whole_px(dx), whole_px(dy)
            if rx <= 0 and ry <= 0:
                out = [list(p) for p in ins[0][0]]
            else:
                out = morphology(ins[0][0], max(rx, 0), max(ry, 0), pr["op"] == "dilate", W, H)
        elif kind == "convolve":
            out = convolve(ins[0][0], pr, W, H, pixel_rect(m, ins[0][1], 0, W, H))
        elif kind == "tile":
            out = tile(ins[0][0], W, H, pixel_rect(m, ins[0][1], 1, W, H))
        elif kind == "displace":
            dx, dy = m.device_lengths(F(pr["scale"]), F(pr["scale"]))
            out = displace(ins[0][0], ins[1][0], dx, dy, pr["xch"], pr["ych"], W, H)
        elif kind == "shadow":
            sx, sy = m.device_lengths(F(pr["sx"]), F(pr["sy"]))
            alpha = [[F(0), F(0), F(0), p[3]] for p in ins[0][0]]
            bl = blur(alpha, sx, sy, W, H)
            ddx, ddy = m.device_vector(F(pr["dx"]), F(pr["dy"]))
            sh = shift(bl, floor_half(ddx), floor_half(ddy), W, H)
            col = pr["color"]
            cs = pr.get("color_space", "srgb")
            c = convert([F(col[0]) * F(col[3]), F(col[1]) * F(col[3]), F(col[2]) * F(col[3]), F(col[3])], cs, space)
            op = F(pr["opacity"])
            shadow = []
            for p in sh:
                a = p[3]
                shadow.append([c[0] * op * a, c[1] * op * a, c[2] * op * a, c[3] * op * a])
            out = [[ins[0][0][q][k2] + shadow[q][k2] * (1 - ins[0][0][q][3]) for k2 in range(4)] for q in range(W * H)]
        else:
            raise ValueError(kind)
        out = clip(out, m.rect_to_device(sub), W, H)
        results.append((out, space, sub))
    px, sp, _ = results[-1]
    if sp != layer_space:
        px = [convert(p, sp, layer_space) for p in px]
    return clip(px, region_d, W, H)


# --- the cases ------------------------------------------------------------------------------------------------------


def ramp(W, H):
    """A premultiplied sRGB source: an opaque red block, a half-transparent green block and a faint blue pixel."""
    img = [[F(0)] * 4 for _ in range(W * H)]
    for j in range(H):
        for i in range(W):
            p = img[j * W + i]
            if 1 <= i <= 3 and 1 <= j <= 3:
                img[j * W + i] = [F(1), F(1, 4), F(0), F(1)]
            elif 5 <= i <= 6 and 2 <= j <= 4:
                a = F(1, 2)
                img[j * W + i] = [F(1, 4) * a, F(3, 4) * a, F(1, 2) * a, a]
    img[0] = [F(0), F(0), F(1, 8), F(1, 8)]
    return img


IDENT = Map(1, 0, 0, 1, 0, 0)
W0, H0 = 8, 6


def case(name, prims, w=W0, h=H0, m=IDENT, region=None, space="linear", src=None):
    c = {"name": name, "w": w, "h": h, "map": m, "region": region, "space": space, "prims": prims,
         "src": src if src is not None else ramp(w, h)}
    c["want"] = run_filter(c)
    return c


PREV = ("prev",)
SRC = ("src",)
ALPHA = ("alpha",)


def res(i):
    return ("res", i)


def cases():
    out = []
    out.append(case("offset-whole", [{"kind": "offset", "inputs": [PREV], "dx": 2, "dy": 1}]))
    out.append(case("offset-rounds-half-up", [{"kind": "offset", "inputs": [PREV], "dx": 1.5, "dy": -0.5}], space="srgb"))
    out.append(case("offset-under-scale", [{"kind": "offset", "inputs": [PREV], "dx": 1, "dy": 1}],
                    m=Map(2, 0, 0, 3, 0, 0)))
    out.append(case("offset-under-quarter-turn", [{"kind": "offset", "inputs": [PREV], "dx": 1, "dy": 0}],
                    m=Map(0, -1, 1, 0, 7, 0)))
    out.append(case("flood-in-srgb", [{"kind": "flood", "inputs": [], "color": [1, F(1, 2), 0, F(3, 4)], "opacity": F(1, 2)}],
                    space="srgb"))
    out.append(case("flood-in-linear", [{"kind": "flood", "inputs": [], "color": [1, F(1, 2), 0, 1], "opacity": 1}]))
    out.append(case("flood-region-area-weights", [{"kind": "flood", "inputs": [], "color": [0, 0, 1, 1], "opacity": 1}],
                    region=(F(3, 2), F(1, 2), F(9, 2), F(7, 2)), space="srgb"))
    out.append(case("flood-subregion", [{"kind": "flood", "inputs": [], "color": [0, 1, 0, 1], "opacity": 1,
                                         "sub": [2, 1, 3, 2]}], space="srgb"))
    out.append(case("merge-flood-under-source", [
        {"kind": "flood", "inputs": [], "color": [0, 0, 1, 1], "opacity": F(1, 2)},
        {"kind": "merge", "inputs": [res(0), SRC]}]))
    for mode, name in ((0, "normal"), (23, "multiply"), (13, "screen"), (21, "difference")):
        out.append(case("blend-" + name, [
            {"kind": "flood", "inputs": [], "color": [F(1, 2), F(1, 2), F(3, 4), F(3, 4)], "opacity": 1},
            {"kind": "blend", "inputs": [SRC, res(0)], "mode": mode}]))
    for op in ("over", "in", "out", "atop", "xor"):
        out.append(case("composite-" + op, [
            {"kind": "offset", "inputs": [SRC], "dx": 1, "dy": 1},
            {"kind": "composite", "inputs": [SRC, res(0)], "op": op}], space="srgb"))
    out.append(case("composite-arithmetic", [
        {"kind": "offset", "inputs": [SRC], "dx": 1, "dy": 0},
        {"kind": "composite", "inputs": [SRC, res(0)], "op": "arithmetic", "k": [F(1, 2), F(1, 4), F(1, 4), F(1, 10)]}],
        space="srgb"))
    out.append(case("matrix-matrix", [{"kind": "matrix", "inputs": [PREV], "type": "matrix", "values": [
        0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, F(1, 4), 0, 0, 0, F(1, 2), F(1, 10)]}]))
    out.append(case("matrix-saturate", [{"kind": "matrix", "inputs": [PREV], "type": "saturate", "values": [F(1, 2)]}]))
    out.append(case("matrix-saturate-over", [{"kind": "matrix", "inputs": [PREV], "type": "saturate", "values": [3]}], space="srgb"))
    out.append(case("matrix-hue-rotate", [{"kind": "matrix", "inputs": [PREV], "type": "hue", "values": [90]}], space="srgb"))
    out.append(case("matrix-luminance-to-alpha", [{"kind": "matrix", "inputs": [PREV], "type": "lum", "values": []}]))
    out.append(case("transfer-table-discrete", [{"kind": "transfer", "inputs": [PREV], "funcs": [
        ("table", [0, F(1, 4), 1]), ("discrete", [F(1, 2), 1]), ("identity",), ("identity",)]}], space="srgb"))
    out.append(case("transfer-linear-gamma", [{"kind": "transfer", "inputs": [PREV], "funcs": [
        ("linear", F(1, 2), F(1, 4)), ("gamma", 1, F(1, 2), 0), ("gamma", 2, 2, F(-1, 10)), ("linear", 1, 0)]}], space="srgb"))
    out.append(case("blur-gaussian-small", [{"kind": "blur", "inputs": [PREV], "sx": F(1), "sy": F(1, 2)}], space="srgb"))
    out.append(case("blur-box-odd", [{"kind": "blur", "inputs": [PREV], "sx": F(2), "sy": 0}], w=12, h=4, space="srgb"))
    out.append(case("blur-box-even", [{"kind": "blur", "inputs": [PREV], "sx": F(5, 2), "sy": F(5, 2)}], w=14, h=10,
                    space="srgb"))
    out.append(case("blur-zero-is-pass-through", [{"kind": "blur", "inputs": [PREV], "sx": 0, "sy": 0}]))
    out.append(case("blur-under-scale", [{"kind": "blur", "inputs": [PREV], "sx": F(1), "sy": F(1)}],
                    m=Map(2, 0, 0, F(1, 2), 0, 0), space="srgb"))
    out.append(case("blur-under-quarter-turn", [{"kind": "blur", "inputs": [PREV], "sx": F(2), "sy": F(1, 2)}],
                    m=Map(0, 2, -1, 0, 0, 7), space="srgb", w=10, h=8))
    out.append(case("drop-shadow", [{"kind": "shadow", "inputs": [PREV], "dx": 2, "dy": 1, "sx": F(1), "sy": F(1),
                                     "color": [0, 0, 0, 1], "opacity": F(3, 4)}], space="srgb"))
    out.append(case("drop-shadow-linear", [{"kind": "shadow", "inputs": [PREV], "dx": 1, "dy": 1, "sx": F(1, 2), "sy": F(1, 2),
                                            "color": [1, F(1, 2), 0, F(1, 2)], "opacity": 1}]))
    out.append(case("chain-blur-offset-flood-merge", [
        {"kind": "blur", "inputs": [ALPHA], "sx": F(1), "sy": F(1)},
        {"kind": "offset", "inputs": [PREV], "dx": 1, "dy": 1},
        {"kind": "flood", "inputs": [], "color": [0, 0, 0, 1], "opacity": F(1, 2)},
        {"kind": "composite", "inputs": [res(2), res(1)], "op": "in"},
        {"kind": "merge", "inputs": [res(3), SRC]}]))
    out.append(case("per-primitive-space", [
        {"kind": "matrix", "inputs": [SRC], "type": "saturate", "values": [F(1, 2)], "space": "srgb"},
        {"kind": "blend", "inputs": [res(0), SRC], "mode": 23, "space": "linear"}]))
    out.append(case("blur-duplicate-edge", [{"kind": "blur", "inputs": [PREV], "sx": F(1), "sy": F(1), "edge": "duplicate"}],
                    space="srgb"))
    out.append(case("blur-wrap-edge-box", [{"kind": "blur", "inputs": [PREV], "sx": F(2), "sy": 0, "edge": "wrap"}], w=12, h=4,
                    space="srgb"))
    out.append(case("morphology-erode", [{"kind": "morph", "inputs": [PREV], "rx": 1, "ry": 0, "op": "erode"}], space="srgb"))
    out.append(case("morphology-dilate", [{"kind": "morph", "inputs": [PREV], "rx": 1, "ry": 1, "op": "dilate"}], space="srgb"))
    out.append(case("morphology-under-scale", [{"kind": "morph", "inputs": [PREV], "rx": F(1, 2), "ry": F(1, 2), "op": "dilate"}],
                    m=Map(2, 0, 0, 2, 0, 0), w=10, h=8, space="srgb"))
    out.append(case("morphology-tiny-radius-is-pass-through", [{"kind": "morph", "inputs": [PREV], "rx": F(1, 4), "ry": F(1, 4), "op": "erode"}]))
    for edge in ("none", "duplicate", "wrap"):
        out.append(case("convolve-" + edge, [{"kind": "convolve", "inputs": [PREV], "order": (3, 3), "target": (1, 1),
                                              "kernel": [0, 1, 0, 1, -4, 1, 0, 1, 0], "divisor": 1, "bias": F(1, 4),
                                              "edge": edge, "preserve": False}], space="srgb"))
    out.append(case("convolve-preserve-alpha", [{"kind": "convolve", "inputs": [PREV], "order": (2, 2), "target": (0, 1),
                                                 "kernel": [1, 2, 3, 4], "divisor": 10, "bias": F(1, 20), "edge": "duplicate",
                                                 "preserve": True}], space="srgb"))
    out.append(case("tile-subregion", [
        {"kind": "offset", "inputs": [SRC], "dx": 0, "dy": 0, "sub": [1, 1, 3, 3]},
        {"kind": "tile", "inputs": [PREV], "sub": [0, 0, 8, 6]}], space="srgb"))
    out.append(case("displacement-map", [
        {"kind": "flood", "inputs": [], "color": [F(3, 4), F(1, 4), 0, 1], "opacity": 1, "color_space": "srgb"},
        {"kind": "displace", "inputs": [SRC, res(0)], "scale": 3, "xch": 0, "ych": 1}], space="srgb"))
    out.append(case("displacement-map-from-the-source", [{"kind": "displace", "inputs": [SRC, SRC], "scale": 4, "xch": 1, "ych": 0}],
                    space="srgb"))
    out.append(case("region-clips-the-result", [{"kind": "offset", "inputs": [PREV], "dx": 2, "dy": 0}],
                    region=(2, 1, 4, 3), space="srgb"))
    return out


# --- the header -----------------------------------------------------------------------------------------------------

KINDS = {"offset": 1, "flood": 2, "merge": 3, "blend": 4, "composite": 5, "matrix": 6, "transfer": 7, "blur": 8, "shadow": 9,
         "morph": 10, "convolve": 11, "tile": 12, "displace": 14}
EDGES = {"none": 0, "duplicate": 1, "wrap": 2}
OPS = {"over": 0, "in": 1, "out": 2, "atop": 3, "xor": 4, "arithmetic": 5}
MTYPES = {"matrix": 0, "saturate": 1, "hue": 2, "lum": 3}
TTYPES = {"identity": 0, "table": 1, "discrete": 2, "linear": 3, "gamma": 4}
SPACES = {None: 0, "linear": 1, "srgb": 2}
INKINDS = {"prev": (0, 0), "src": (1, 0), "alpha": (2, 0)}


def d(v):
    return repr(float(v))


def emit_extra(p):
    """The fields of the stage 3 primitives, after `mode`: edge, orders, target, divisor, bias, preserve, scale, channels, radii, op."""
    k = p["kind"]
    ox, oy = p.get("order", (0, 0))
    tx, ty = p.get("target", (0, 0))
    return ", %d, %d, %d, %d, %d, %s, %s, %d, %s, %d, %d, %s, %s, %d" % (
        EDGES[p.get("edge", "none")], ox, oy, tx, ty, d(p.get("divisor", 0)), d(p.get("bias", 0)), 1 if p.get("preserve") else 0,
        d(p.get("scale", 0)), p.get("xch", 0), p.get("ych", 0), d(p.get("rx", 0)), d(p.get("ry", 0)), 1 if p.get("op") == "dilate" else 0)


def emit_prim(p):
    ins = p["inputs"]
    kinds, idx = [0, 0, 0], [0, 0, 0]
    for n, spec in enumerate(ins):
        if spec[0] == "res":
            kinds[n], idx[n] = 3, spec[1]
        else:
            kinds[n], idx[n] = INKINDS[spec[0]]
    sub = p.get("sub")
    mask = 0
    subv = [0, 0, 0, 0]
    if sub:
        for k in range(4):
            if sub[k] is not None:
                mask |= 1 << k
                subv[k] = sub[k]
    k = p["kind"]
    fields = {
        "kind": KINDS[k], "space": SPACES[p.get("space")], "n_in": len(ins), "in_kind": kinds, "in_idx": idx,
        "sub_mask": mask, "sub": subv, "dx": p.get("dx", 0), "dy": p.get("dy", 0),
        "color": [0, 0, 0, 0], "color_space": SPACES[p.get("color_space", "srgb")] if "color" in p else 0, "opacity": p.get("opacity", 0),
        "mode": 0, "k": [0, 0, 0, 0], "nvals": 0, "vals": [0] * 20, "ttype": [0] * 4, "tcount": [0] * 4,
        "tvals": [[0] * 4 for _ in range(4)], "tparam": [[0] * 3 for _ in range(4)], "sx": p.get("sx", 0), "sy": p.get("sy", 0),
    }
    if "color" in p:
        fields["color"] = [F(v) for v in p["color"]]
    if k == "blend":
        fields["mode"] = p["mode"]
    if k == "composite":
        fields["mode"] = OPS[p["op"]]
        fields["k"] = p.get("k", [0, 0, 0, 0])
    if k == "matrix":
        fields["mode"] = MTYPES[p["type"]]
        fields["nvals"] = len(p["values"])
        for n, v in enumerate(p["values"]):
            fields["vals"][n] = v
    if k == "convolve":
        fields["nvals"] = len(p["kernel"])
        for n, v in enumerate(p["kernel"]):
            fields["vals"][n] = v
    if k == "transfer":
        for n, t in enumerate(p["funcs"]):
            fields["ttype"][n] = TTYPES[t[0]]
            if t[0] in ("table", "discrete"):
                fields["tcount"][n] = len(t[1])
                for q, v in enumerate(t[1]):
                    fields["tvals"][n][q] = v
            elif t[0] == "linear":
                fields["tparam"][n][0], fields["tparam"][n][1] = t[1], t[2]
            elif t[0] == "gamma":
                fields["tparam"][n] = [t[1], t[2], t[3]]

    def arr(vs):
        return "{" + ", ".join(d(v) for v in vs) + "}"

    extra = emit_extra(p)
    return ("{%d, %d, %d, {%s}, {%s}, %d, %s, %s, %s, %s, %d, %s, %s, %d, %s, {%s}, {%s}, {%s}, {%s}, %s, %s}" % (
        fields["kind"], fields["space"], fields["n_in"], ", ".join(map(str, kinds)), ", ".join(map(str, idx)),
        fields["sub_mask"], arr(fields["sub"]), d(fields["dx"]), d(fields["dy"]), arr(fields["color"]),
        fields["color_space"], d(fields["opacity"]), arr(fields["k"]), fields["nvals"],
        arr(fields["vals"]), ", ".join(map(str, fields["ttype"])), ", ".join(map(str, fields["tcount"])),
        ", ".join(arr(t) for t in fields["tvals"]), ", ".join(arr(t) for t in fields["tparam"]), d(fields["sx"]),
        d(fields["sy"])))


def header():
    cs = cases()
    lines = ["// Generated by tools/gen-filter-vectors.py (exact rationals; independent of the library).  Do not edit; run the",
             "// generator.  A layer is premultiplied sRGB RGBA; map is (a, b, c, d, e, f): x' = a x + b y + e, y' = c x + d y + f.",
             "#ifndef GHOTI_IO_GSHCPU_TEST_FILTER_VECTORS_H", "#define GHOTI_IO_GSHCPU_TEST_FILTER_VECTORS_H", "",
             "namespace filter_vectors {", "",
             "// kind: 1 offset, 2 flood, 3 merge, 4 blend, 5 composite, 6 color matrix, 7 component transfer, 8 blur, 9 drop shadow;",
             "// space: 0 inherit, 1 linearRGB, 2 sRGB; in_kind: 0 previous, 1 source graphic, 2 source alpha, 3 result in_idx;",
             "// mode: a blend mode (0 normal, 13 screen, 21 difference, 23 multiply), a composite operator, a matrix type; the",
             "// transfers' parameters are (slope, intercept) or (amplitude, exponent, offset).",
             "struct FPrim {", "  int kind, space, n_in;", "  int in_kind[3], in_idx[3];", "  int sub_mask;", "  double sub[4];",
             "  double dx, dy;", "  double color[4];", "  int color_space;", "  double opacity;", "  double k[4];", "  int nvals;",
             "  double vals[20];", "  int ttype[4], tcount[4];", "  double tvals[4][4];", "  double tparam[4][3];", "  double sx, sy;",
             "  int mode;", "  int edge, order_x, order_y, target_x, target_y;", "  double divisor, bias;", "  int preserve;", "  double scale;",
             "  int xch, ych;", "  double rx, ry;", "  int dilate;", "};", "",
             "struct FCase {", "  const char * name;", "  int w, h;", "  double map[6];", "  int region_mode;  // 0 explicit, 1 auto",
             "  double region[4];", "  int space;  // the filter's: 1 linearRGB, 2 sRGB", "  int nprims;", "  const FPrim * prims;",
             "  const double * src;   // w * h * 4", "  const double * want;  // w * h * 4", "};", ""]
    for n, c in enumerate(cs):
        for kind in ("src", "want"):
            vals = []
            for p in (c[kind]):
                vals.extend(float(v) for v in p)
            lines.append("static const double kPix_%d_%s[] = {%s};" % (n, kind, ", ".join(repr(v) for v in vals)))
        lines.append("static const FPrim kPrims_%d[] = {" % n)
        for p in c["prims"]:
            lines.append("    " + emit_prim(p).rstrip("}") + ", %d%s}," % (mode_of(p), emit_extra(p)))
        lines.append("};")
    lines.append("")
    lines.append("static const FCase kCases[] = {")
    for n, c in enumerate(cs):
        m = c["map"]
        region = c["region"]
        lines.append('    {"%s", %d, %d, {%s}, %d, {%s}, %d, %d, kPrims_%d, kPix_%d_src, kPix_%d_want},' % (
            c["name"], c["w"], c["h"], ", ".join(d(v) for v in (m.a, m.b, m.c, m.d, m.e, m.f)), 1 if region is None else 0,
            ", ".join(d(v) for v in (region or (0, 0, 0, 0))), SPACES[c["space"]], len(c["prims"]), n, n, n))
    lines += ["};", "", "}  // namespace filter_vectors", "", "#endif", ""]
    return "\n".join(lines)


def mode_of(p):
    if p["kind"] == "blend":
        return p["mode"]
    if p["kind"] == "composite":
        return OPS[p["op"]]
    if p["kind"] == "matrix":
        return MTYPES[p["type"]]
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "tests", "filter_vectors.h")
    text = header()
    if "--check" in sys.argv:
        with open(path) as f:
            if f.read() != text:
                print("filter_vectors.h is not what gen-filter-vectors.py writes", file=sys.stderr)
                return 1
        print("filter vectors: current")
        return 0
    with open(path, "w") as f:
        f.write(text)
    print("filter vectors: wrote %d cases" % len(cases()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
