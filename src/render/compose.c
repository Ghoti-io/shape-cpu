/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape CPU.
 *
 * Ghoti.io Shape CPU is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape CPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The compositing primitives of S11 (render.h): the transfer functions (N2), the
 * conversion between spaces (N3), the operators and the one primitive `apply`
 * (N4, N5), the D8 weight, and the loads and stores of the four target formats.
 * Every value is a `double`; the power function is `math`'s; the only `float` is
 * the float store.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/math/elementary.h>

#include <string.h>

/* --- N1: the saturation ------------------------------------------------------------- */

double gshcpu_render_saturate(double x) {
  return x > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION
      : x < -GSHSCENE_COLOR_SATURATION ? -GSHSCENE_COLOR_SATURATION : x;
}

double gshcpu_render_unpremultiply(double c, double a) {
  /* Past the bound the quotient is never formed, so no infinity is: 2^64 |a| is exact (a power-of-two scaling). */
  double mc = c < 0.0 ? -c : c;
  double ma = a < 0.0 ? -a : a;
  if (mc >= GSHSCENE_COLOR_SATURATION * ma) {
    return (c < 0.0) != (a < 0.0) ? -GSHSCENE_COLOR_SATURATION : GSHSCENE_COLOR_SATURATION;
  }
  return c / a;
}

/* --- N2: the transfer functions ----------------------------------------------------- */

/** sRGB: the encoded value below which the curve is linear. */
#define SRGB_DECODE_KNEE 0.04045
/** sRGB: the linear value below which the curve is linear. */
#define SRGB_ENCODE_KNEE 0.0031308
/** sRGB: the slope of the linear segment. */
#define SRGB_SLOPE 12.92
/** sRGB: the offset of the power segment. */
#define SRGB_OFFSET 0.055
/** sRGB: the scale of the power segment. */
#define SRGB_SCALE 1.055
/** sRGB: the exponent. */
#define SRGB_EXPONENT 2.4

double gshcpu_render_decode(const GSHSCENE_Color_Space * space, double x) {
  if (space->transfer == GCOL_TRANSFER_LINEAR) {
    return x;
  }
  if (space->transfer == GCOL_TRANSFER_SRGB) {
    /* The linear segment continues below zero (N2). */
    return x <= SRGB_DECODE_KNEE ? x / SRGB_SLOPE : gmath_pow((x + SRGB_OFFSET) / SRGB_SCALE, SRGB_EXPONENT);
  }
  /* A plain gamma, odd about zero: sign(x) |x|^g. */
  if (x == 0.0) {
    return 0.0;
  }
  return x < 0.0 ? -gmath_pow(-x, space->gamma) : gmath_pow(x, space->gamma);
}

double gshcpu_render_encode(const GSHSCENE_Color_Space * space, double y) {
  if (space->transfer == GCOL_TRANSFER_LINEAR) {
    return y;
  }
  if (space->transfer == GCOL_TRANSFER_SRGB) {
    return y <= SRGB_ENCODE_KNEE ? y * SRGB_SLOPE : SRGB_SCALE * gmath_pow(y, 1.0 / SRGB_EXPONENT) - SRGB_OFFSET;
  }
  if (y == 0.0) {
    return 0.0;
  }
  return y < 0.0 ? -gmath_pow(-y, 1.0 / space->gamma) : gmath_pow(y, 1.0 / space->gamma);
}

/* --- N3: the conversion ---------------------------------------------------------------- */

/** The white of every gamut, D65 (CIE xy). */
#define WHITE_X 0.3127
#define WHITE_Y 0.3290

/** N1: the chromaticities (x, y) of red, green and blue of a gamut. */
static void chromaticities(int primaries, double xy[3][2]) {
  static const double table[4][3][2] = {
      {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}},       /* sRGB */
      {{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}},       /* Adobe RGB */
      {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}}, /* Display P3 */
      {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}}, /* BT.2020 */
  };
  int row = 0;
  switch (primaries) {
    case GCOL_PRIMARIES_ADOBE_RGB:
      row = 1;
      break;
    case GCOL_PRIMARIES_DISPLAY_P3:
      row = 2;
      break;
    case GCOL_PRIMARIES_BT2020:
      row = 3;
      break;
    default: /* sRGB; the contract admits no other gamut */
      row = 0;
      break;
  }
  memcpy(xy, table[row], sizeof table[row]);
}

static double det3(const GSHCPU_Mat3 * a) {
  const double (*m)[3] = a->m;
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
      m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/** The inverse by the adjugate. */
static void inverse3(const GSHCPU_Mat3 * a, GSHCPU_Mat3 * out) {
  const double (*m)[3] = a->m;
  double d = det3(a);
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      int r0 = (j + 1) % 3;
      int r1 = (j + 2) % 3;
      int c0 = (i + 1) % 3;
      int c1 = (i + 2) % 3;
      out->m[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d;
    }
  }
}

static void mul3(const GSHCPU_Mat3 * a, const double v[3], double out[3]) {
  double r[3];
  for (int i = 0; i < 3; i++) {
    r[i] = a->m[i][0] * v[0] + a->m[i][1] * v[1] + a->m[i][2] * v[2];
  }
  memcpy(out, r, sizeof r);
}

void gshcpu_render_rgb_to_xyz(int primaries, GSHCPU_Mat3 * out) {
  double xy[3][2];
  GSHCPU_Mat3 p;
  chromaticities(primaries, xy);
  for (int c = 0; c < 3; c++) {
    p.m[0][c] = xy[c][0] / xy[c][1];
    p.m[1][c] = 1.0;
    p.m[2][c] = (1.0 - xy[c][0] - xy[c][1]) / xy[c][1];
  }
  const double white[3] = {WHITE_X / WHITE_Y, 1.0, (1.0 - WHITE_X - WHITE_Y) / WHITE_Y};
  GSHCPU_Mat3 pi;
  double s[3];
  inverse3(&p, &pi);
  mul3(&pi, white, s);
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      out->m[r][c] = s[c] * p.m[r][c];
    }
  }
}

static int space_equal(const GSHSCENE_Color_Space * a, const GSHSCENE_Color_Space * b) {
  return a->primaries == b->primaries && a->transfer == b->transfer && a->gamma == b->gamma;
}

void gshcpu_render_convert_prepare(GSHCPU_Convert * c, const GSHSCENE_Color_Space * from, const GSHSCENE_Color_Space * to) {
  memset(c, 0, sizeof *c);
  c->from = *from;
  c->to = *to;
  c->same = space_equal(from, to);
  c->matrix = !c->same && from->primaries != to->primaries;
  if (c->matrix) {
    GSHCPU_Mat3 dest;
    gshcpu_render_rgb_to_xyz((int)from->primaries, &c->to_xyz);
    gshcpu_render_rgb_to_xyz((int)to->primaries, &dest);
    inverse3(&dest, &c->from_xyz);
  }
}

void gshcpu_render_convert_straight(const GSHCPU_Convert * c, const double in[3], double out[3]) {
  if (c->same) {
    /* Untouched, exactly (N3). */
    memmove(out, in, 3 * sizeof *in);
    return;
  }
  /* N1: saturated after each step, so that a power only ever takes a value of magnitude at most 2^64. */
  double lin[3];
  for (int i = 0; i < 3; i++) {
    lin[i] = gshcpu_render_saturate(gshcpu_render_decode(&c->from, in[i]));
  }
  if (c->matrix) {
    /* When only the transfers differ the two matrices cancel and are not applied (N3). */
    mul3(&c->to_xyz, lin, lin);
    mul3(&c->from_xyz, lin, lin);
    for (int i = 0; i < 3; i++) {
      lin[i] = gshcpu_render_saturate(lin[i]);
    }
  }
  for (int i = 0; i < 3; i++) {
    out[i] = gshcpu_render_saturate(gshcpu_render_encode(&c->to, lin[i]));
  }
}

void gshcpu_render_convert_premultiplied(const GSHCPU_Convert * c, const double in[4], double out[4]) {
  if (c->same) {
    memmove(out, in, 4 * sizeof *in);
    return;
  }
  double a = in[3];
  if (a == 0.0) {
    out[0] = out[1] = out[2] = out[3] = 0.0;
    return;
  }
  double straight[3] = {gshcpu_render_unpremultiply(in[0], a), gshcpu_render_unpremultiply(in[1], a),
      gshcpu_render_unpremultiply(in[2], a)};
  gshcpu_render_convert_straight(c, straight, straight);
  out[0] = straight[0] * a;
  out[1] = straight[1] * a;
  out[2] = straight[2] * a;
  out[3] = a;
}

/* --- N4, N5: the operators and apply ---------------------------------------------------- */

int gshcpu_render_bounded(uint32_t op) {
  switch (op) {
    case GSHSCENE_COMPOSITE_DEST:
    case GSHSCENE_COMPOSITE_SRC_OVER:
    case GSHSCENE_COMPOSITE_DEST_OVER:
    case GSHSCENE_COMPOSITE_DEST_OUT:
    case GSHSCENE_COMPOSITE_SRC_ATOP:
    case GSHSCENE_COMPOSITE_XOR:
    case GSHSCENE_COMPOSITE_PLUS:
      return 1;
    default:
      return 0;
  }
}

void gshcpu_render_apply(double d[4], const double s[4], double m, uint32_t op) {
  const double sa = s[3];
  const double da = d[3];
  double fa = 0.0;
  double fb = 0.0;
  double o[4];
  switch (op) {
    case GSHSCENE_COMPOSITE_CLEAR:
      break;
    case GSHSCENE_COMPOSITE_SRC:
      fa = 1.0;
      break;
    case GSHSCENE_COMPOSITE_DEST:
      fb = 1.0;
      break;
    case GSHSCENE_COMPOSITE_SRC_OVER:
      fa = 1.0;
      fb = 1.0 - sa;
      break;
    case GSHSCENE_COMPOSITE_DEST_OVER:
      fa = 1.0 - da;
      fb = 1.0;
      break;
    case GSHSCENE_COMPOSITE_SRC_IN:
      fa = da;
      break;
    case GSHSCENE_COMPOSITE_DEST_IN:
      fb = sa;
      break;
    case GSHSCENE_COMPOSITE_SRC_OUT:
      fa = 1.0 - da;
      break;
    case GSHSCENE_COMPOSITE_DEST_OUT:
      fb = 1.0 - sa;
      break;
    case GSHSCENE_COMPOSITE_SRC_ATOP:
      fa = da;
      fb = 1.0 - sa;
      break;
    case GSHSCENE_COMPOSITE_DEST_ATOP:
      fa = 1.0 - da;
      fb = sa;
      break;
    case GSHSCENE_COMPOSITE_XOR:
      fa = 1.0 - da;
      fb = 1.0 - sa;
      break;
    default:
      break;
  }
  for (int i = 0; i < 4; i++) {
    if (op == GSHSCENE_COMPOSITE_PLUS) {
      double sum = s[i] + d[i];
      o[i] = sum < 1.0 ? sum : 1.0;
    } else {
      o[i] = fa * s[i] + fb * d[i];
    }
  }
  for (int i = 0; i < 4; i++) {
    d[i] = gshcpu_render_saturate((1.0 - m) * d[i] + m * o[i]); /* N1 */
  }
}

/* --- D8: the weight under a clip ------------------------------------------------------------- */

uint32_t gshcpu_render_weigh(uint32_t m8, uint32_t c8) {
  return (m8 * c8 + 127u) / 255u;
}

/* --- the stores and loads --------------------------------------------------------------------- */

uint8_t gshcpu_render_store8(double v) {
  double t = v * 255.0;
  if (!(t > 0.0)) {
    return 0; /* below zero, and a NaN, are 0 */
  }
  if (t >= 255.0) {
    return 255;
  }
  /* floor(t + 1/2) without adding: t is in (0, 255), so the conversion is exact and is the floor, and t - n is
   * exact too.  A 32-bit conversion, never a 64-bit one (AD-17: i686 would use the x87 stack for that). */
  int32_t n = (int32_t)t;
  double fraction = t - (double)n;
  return (uint8_t)(n + (fraction >= 0.5 ? 1 : 0));
}

float gshcpu_render_store_float(double v) {
  return (float)v;
}

size_t gshcpu_render_pixel_bytes(GSHCPU_Format format) {
  return format == GSHCPU_FORMAT_RGBAF32_STRAIGHT || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED ? 16u : 4u;
}

static int premultiplied(GSHCPU_Format format) {
  return format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED || format == GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED;
}

void gshcpu_render_load(GSHCPU_Format format, const unsigned char * row, size_t x, double out[4]) {
  double v[4];
  if (gshcpu_render_pixel_bytes(format) == 4u) {
    const unsigned char * p = row + x * 4u;
    for (int i = 0; i < 4; i++) {
      v[i] = (double)p[i] / 255.0;
    }
  } else {
    float f[4];
    memcpy(f, row + x * 16u, sizeof f);
    for (int i = 0; i < 4; i++) {
      v[i] = (double)f[i];
    }
  }
  if (!premultiplied(format)) {
    for (int i = 0; i < 3; i++) {
      v[i] = v[i] * v[3];
    }
  }
  memcpy(out, v, sizeof v);
}

void gshcpu_render_store(GSHCPU_Format format, unsigned char * row, size_t x, const double v[4]) {
  double w[4];
  memcpy(w, v, sizeof w);
  if (!premultiplied(format)) {
    for (int i = 0; i < 3; i++) {
      w[i] = w[3] == 0.0 ? 0.0 : gshcpu_render_unpremultiply(w[i], w[3]); /* N1 */
    }
  }
  if (gshcpu_render_pixel_bytes(format) == 4u) {
    unsigned char * p = row + x * 4u;
    for (int i = 0; i < 4; i++) {
      p[i] = gshcpu_render_store8(w[i]);
    }
  } else {
    float f[4];
    for (int i = 0; i < 4; i++) {
      f[i] = gshcpu_render_store_float(w[i]);
    }
    memcpy(row + x * 16u, f, sizeof f);
  }
}
