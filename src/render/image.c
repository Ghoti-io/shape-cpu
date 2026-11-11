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
 * An image paint's value at a device point (IC1 to IC8, render.h; shape-scene's IP1 to IP9): the fill's chain composed by
 * shape-geometry and inverted once, the point taken to image space, the taps read in place from the stored rows through the
 * window's edge maps, decoded as IP4 says and filtered in `double`, in the order the rule gives, no fused multiply-add.  The
 * image is never copied.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/math/basic.h>

#include <string.h>

/** A function the per-pixel path is made of: the compiler leaves them out of line at -O2, and each call costs more than the body of the nearest case. */
#if defined(__GNUC__)
#define IM_INLINE static inline __attribute__((always_inline))
#else
#define IM_INLINE static inline
#endif

/** The half-width of the sampling domain in texels (IP12). */
#define DOMAIN 4294967296.0

/* --- the edge maps (IC5, IP6): integer arithmetic on integer taps -------------------------------------------- */

/** `a` modulo `n > 0`, never negative. */
static inline int64_t modulo(int64_t a, int64_t n) {
  int64_t r = a % n;
  return r < 0 ? r + n : r;
}

/** The texel index tap @p k reads along the window `[lo, hi)`, or -1 for the transparent texel of `NONE`. */
IM_INLINE int64_t edge(int64_t k, int64_t lo, int64_t hi, uint32_t mode) {
  if (k >= lo && k < hi) {
    return k; /* inside the window every mode reads the texel itself, and nearly every tap is */
  }
  const int64_t n = hi - lo;
  switch (mode) {
    case GSHSCENE_IMAGE_EXTEND_PAD:
      return k < lo ? lo : k > hi - 1 ? hi - 1 : k;
    case GSHSCENE_IMAGE_EXTEND_REPEAT: {
      const int64_t r = k - lo;
      if (r >= 0 && r < n) {
        return k; /* inside the window, where almost every tap is: no division */
      }
      return lo + modulo(r, n);
    }
    case GSHSCENE_IMAGE_EXTEND_REFLECT: {
      if (k >= lo && k < hi) {
        return k; /* the first period reads forward: m = k - lo < n */
      }
      const int64_t m = modulo(k - lo, 2 * n);
      return m < n ? lo + m : lo + 2 * n - 1 - m;
    }
    default:
      return k >= lo && k < hi ? k : -1;
  }
}

/* --- decode (IC4, IP4) --------------------------------------------------------------------------------------- */

/** One sample of the texel at row @p row, column @p x, channel @p c: gshscene_image_decode's, with the same divisions. */
static inline double sample(const GSHCPU_Image * im, const unsigned char * row, uint32_t x, uint32_t c) {
  const size_t bytes = im->bits <= 8u ? 1u : im->bits <= 16u ? 2u : 4u;
  const unsigned char * at = row + ((size_t)x * im->layout + c) * bytes;
  if (im->is_float) {
    float f;
    memcpy(&f, at, sizeof f);
    return gshcpu_render_saturate((double)f);
  }
  if (bytes == 1) {
    return im->lut ? im->lut[*at] : (double)*at / 255.0;
  }
  uint16_t v;
  memcpy(&v, at, sizeof v);
  return (double)v / (im->bits == 12u ? 4095.0 : 65535.0);
}

/** IP4: the premultiplied value of texel (@p x, @p y) in the image's own space. */
IM_INLINE void decode(const GSHCPU_Image * im, uint32_t x, uint32_t y, double out[4]) {
  const unsigned char * row = im->rows + (size_t)y * im->stride;
  if (im->alpha_only) {
    out[0] = out[1] = out[2] = 0.0;
    out[3] = sample(im, row, x, 0);
    return;
  }
  if (im->lut && !im->is_float && im->bits <= 8u && im->layout == (uint32_t)GSHSCENE_IMAGE_RGBA) {
    /* The table's case (FT-3) for the layout that is nearly every image: sample()'s reads of one byte each, without its tests per channel. */
    const unsigned char * q = row + (size_t)x * 4u;
    const double a = im->lut[q[3]];
    if (im->straight) {
      out[0] = a * im->lut[q[0]];
      out[1] = a * im->lut[q[1]];
      out[2] = a * im->lut[q[2]];
    } else {
      out[0] = im->lut[q[0]];
      out[1] = im->lut[q[1]];
      out[2] = im->lut[q[2]];
    }
    out[3] = a;
    return;
  }
  double c[3];
  double a = 1.0;
  if (im->layout == (uint32_t)GSHSCENE_IMAGE_GRAY) {
    c[0] = c[1] = c[2] = sample(im, row, x, 0);
  } else {
    for (uint32_t k = 0; k < 3; k++) {
      c[k] = sample(im, row, x, k);
    }
    if (im->layout == (uint32_t)GSHSCENE_IMAGE_RGBA) {
      a = sample(im, row, x, 3);
    }
  }
  for (int k = 0; k < 3; k++) {
    out[k] = im->straight ? a * c[k] : c[k];
  }
  out[3] = a;
}

void gshcpu_render_image_texel(const GSHCPU_Image * im, uint32_t x, uint32_t y, double out[4]) {
  decode(im, x, y, out);
}

/** The texel (@p ix, @p iy) of the window, premultiplied in the filter space (IP5, IC4). */
void gshcpu_render_image_window_texel(const GSHCPU_Image * im, uint32_t ix, uint32_t iy, double out[4]) {
  decode(im, ix, iy, out);
  if (im->convert_in) {
    gshcpu_render_convert_premultiplied(&im->in, out, out);
  }
}

/** The RGBA8 texel through the table (the fast tier's case: no conversion, not alpha-only), the arithmetic of decode()'s own table branch. */
IM_INLINE void decode_rgba8(const GSHCPU_Image * im, uint32_t x, uint32_t y, double out[4]) {
  const unsigned char * q = im->rows + (size_t)y * im->stride + (size_t)x * 4u;
  const double a = im->lut[q[3]];
  if (im->straight) {
    out[0] = a * im->lut[q[0]];
    out[1] = a * im->lut[q[1]];
    out[2] = a * im->lut[q[2]];
  } else {
    out[0] = im->lut[q[0]];
    out[1] = im->lut[q[1]];
    out[2] = im->lut[q[2]];
  }
  out[3] = a;
}

/** The value of the texel tap (@p tx, @p ty) reads, premultiplied in the filter space (IP5, IC4); @p fast is the pixel's fast configuration (see ::fast_config). */
IM_INLINE void texel_k(const GSHCPU_Image * im, int64_t tx, int64_t ty, double out[4], int fast) {
  const int64_t ix = edge(tx, (int64_t)im->x0, (int64_t)im->x1, im->extend_x);
  const int64_t iy = edge(ty, (int64_t)im->y0, (int64_t)im->y1, im->extend_y);
  if (ix < 0 || iy < 0) {
    out[0] = out[1] = out[2] = out[3] = 0.0;
    return;
  }
  if (fast) {
    decode_rgba8(im, (uint32_t)ix, (uint32_t)iy, out);
    return;
  }
  decode(im, (uint32_t)ix, (uint32_t)iy, out);
  if (im->convert_in) {
    gshcpu_render_convert_premultiplied(&im->in, out, out);
  }
}

IM_INLINE void texel(const GSHCPU_Image * im, int64_t tx, int64_t ty, double out[4]) {
  texel_k(im, tx, ty, out, 0);
}

int64_t gshcpu_render_image_edge(int64_t k, int64_t lo, int64_t hi, uint32_t mode) {
  return edge(k, lo, hi, mode);
}

void gshcpu_render_image_tap(const GSHCPU_Image * im, int64_t tx, int64_t ty, double out[4]) {
  texel(im, tx, ty, out);
}

/* --- open, place, source ------------------------------------------------------------------------------------- */

GSHCPU_Result gshcpu_render_image_open(GSHCPU_Image * im, const GSHSCENE_Resource * image, const uint32_t window[4],
    uint32_t extend_x, uint32_t extend_y, uint32_t filter, const GSHSCENE_Color_Space * filter_space,
    const GSHSCENE_Color_Space * target_space, const GSHSCENE_Color_Space * layer_space) {
  GSHSCENE_Image_Info info;
  if (!im || !image || !window || gshscene_resource_image(image, &info) != GSHSCENE_OK ||
      extend_x >= (uint32_t)GSHSCENE_IMAGE_EXTEND_COUNT || extend_y >= (uint32_t)GSHSCENE_IMAGE_EXTEND_COUNT ||
      filter >= (uint32_t)GSHSCENE_IMAGE_FILTER_COUNT) {
    return GSHCPU_ERR_INVALID;
  }
  const int whole = !window[0] && !window[1] && !window[2] && !window[3];
  im->x0 = whole ? 0 : window[0];
  im->y0 = whole ? 0 : window[1];
  im->x1 = whole ? info.width : window[2];
  im->y1 = whole ? info.height : window[3];
  if (im->x0 >= im->x1 || im->y0 >= im->y1 || im->x1 > info.width || im->y1 > info.height) {
    return GSHCPU_ERR_INVALID;
  }
  im->rows = gshscene_image_row(image, 0);
  im->stride = gshscene_image_stride(image);
  im->width = info.width;
  im->height = info.height;
  im->layout = info.layout;
  im->bits = info.bits;
  im->is_float = info.sample_type == (uint32_t)GSHSCENE_IMAGE_FLOAT;
  im->straight = info.alpha_form == (uint32_t)GSHSCENE_IMAGE_ALPHA_STRAIGHT;
  im->alpha_only = info.alpha_only != 0;
  im->extend_x = extend_x;
  im->extend_y = extend_y;
  im->filter = filter;
  im->lut = NULL;
  im->pixel = NULL;
  im->tiers = 0;
  im->mode = GSHCPU_IMAGE_GENERAL;
  im->hoisted_valid = 0;
  im->hoisted_key = 0.0;
  /* IP5: the image's space, where it says zero the target's; the filter space, where it says zero the image's. */
  GSHSCENE_Color_Space own = info.space;
  if (gshscene_color_space_is_target(&own)) {
    own = *target_space;
  }
  GSHSCENE_Color_Space filtering = *filter_space;
  if (gshscene_color_space_is_target(&filtering)) {
    filtering = own;
  }
  im->convert_in = 0;
  im->convert_out = 0;
  if (!im->alpha_only) {
    gshcpu_render_convert_prepare(&im->in, &own, &filtering);
    gshcpu_render_convert_prepare(&im->out, &filtering, layer_space);
    im->convert_in = !im->in.same;
    im->convert_out = !im->out.same;
  }
  im->mult = 1.0;
  im->tint[0] = im->tint[1] = im->tint[2] = 0.0;
  im->tint_alpha = 0.0;
  memset(im->inv, 0, sizeof im->inv);
  im->visits = 0;
  for (size_t k = 0; k < GSHCPU_IMAGE_ROW_SUMS; k++) {
    im->row_sums[k].valid = 0;
  }
  return GSHCPU_OK;
}

int gshcpu_render_image_place(GSHCPU_Image * im, const GSHGEOM_Map * composed) {
  return gshcpu_render_map_invert(composed, im->inv);
}

int gshcpu_render_image_in_domain(const GSHCPU_Image * im, GSHCPU_Rect r) {
  const double xs[2] = {gshcpu_render_i64_to_double(r.x0), gshcpu_render_i64_to_double(r.x1)};
  const double ys[2] = {gshcpu_render_i64_to_double(r.y0), gshcpu_render_i64_to_double(r.y1)};
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      const double u = xs[i] - im->inv[4];
      const double v = ys[j] - im->inv[5];
      const double px = im->inv[0] * u + im->inv[1] * v;
      const double py = im->inv[2] * u + im->inv[3] * v;
      if (!(px >= -DOMAIN && px <= DOMAIN && py >= -DOMAIN && py <= DOMAIN)) {
        return 0;
      }
    }
  }
  return 1;
}

void gshcpu_render_image_source(GSHCPU_Image * im, double m, const double tint[3], double tint_alpha) {
  im->mult = m;
  if (tint) {
    memcpy(im->tint, tint, sizeof im->tint);
  }
  im->tint_alpha = tint_alpha;
}

/* --- the point filters (IC6) --------------------------------------------------------------------------------- */

/** IP7's kernel piece for `0 <= d < 2`, by Horner's rule (IC6). */
static inline double kernel(double d) {
  if (d < 1.0) {
    return (((21.0 * d - 36.0) * d) * d + 16.0) / 18.0;
  }
  return (((-7.0 * d + 36.0) * d - 60.0) * d + 32.0) / 18.0;
}

/** The taps of one coordinate (@p c, in texels) for @p filter: the first texel's index and the weights (IC6). */
IM_INLINE void taps_of(uint32_t filter, double c, GSHCPU_Image_Taps * t) {
  if (filter == (uint32_t)GSHSCENE_IMAGE_FILTER_NEAREST) {
    t->i = gshcpu_render_floor_i64(c);
    return;
  }
  const double s = c - 0.5;
  /* The floor as an integer, and as a double when it is in the range where that is exact (a zero keeps its sign, as gmath_floor's). */
  const int64_t fi = gshcpu_render_floor_i64(s);
  const double fl = (s > -2147483648.0 && s < 2147483648.0) ? (s == 0.0 ? s : (double)fi) : gmath_floor(s);
  const double f = s - fl;
  t->i = fi;
  if (filter == (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR) {
    t->w[0] = 1.0 - f;
    t->w[1] = f;
    return;
  }
  t->w[0] = kernel(f + 1.0);
  t->w[1] = kernel(f);
  t->w[2] = kernel(1.0 - f);
  t->w[3] = kernel(2.0 - f);
}

IM_INLINE void nearest(const GSHCPU_Image * im, const GSHCPU_Image_Taps * x, const GSHCPU_Image_Taps * y, double out[4], int fast) {
  texel_k(im, x->i, y->i, out, fast);
}

IM_INLINE void linear(const GSHCPU_Image * im, const GSHCPU_Image_Taps * x, const GSHCPU_Image_Taps * y, double out[4], int fast) {
  const int64_t i = x->i;
  const int64_t j = y->i;
  const double wx0 = x->w[0];
  const double wx1 = x->w[1];
  const double wy0 = y->w[0];
  const double wy1 = y->w[1];
  double r[2][4];
  for (int b = 0; b < 2; b++) {
    double t0[4];
    double t1[4];
    texel_k(im, i, j + b, t0, fast);
    texel_k(im, i + 1, j + b, t1, fast);
    for (int k = 0; k < 4; k++) {
      r[b][k] = wx0 * t0[k] + wx1 * t1[k];
    }
  }
  for (int k = 0; k < 4; k++) {
    out[k] = wy0 * r[0][k] + wy1 * r[1][k];
  }
}

static void cubic(const GSHCPU_Image * im, const GSHCPU_Image_Taps * x, const GSHCPU_Image_Taps * y, double out[4]) {
  const int64_t i = x->i;
  const int64_t j = y->i;
  const double * wx = x->w;
  const double * wy = y->w;
  double lo[4];
  double hi[4];
  double r[4][4];
  for (int b = 0; b < 4; b++) {
    double t[4][4];
    for (int a = 0; a < 4; a++) {
      texel(im, i - 1 + a, j - 1 + b, t[a]);
      for (int k = 0; k < 4; k++) {
        if (a == 0 && b == 0) {
          lo[k] = hi[k] = t[a][k];
        } else {
          lo[k] = t[a][k] < lo[k] ? t[a][k] : lo[k];
          hi[k] = t[a][k] > hi[k] ? t[a][k] : hi[k];
        }
      }
    }
    for (int k = 0; k < 4; k++) {
      double s = wx[0] * t[0][k];
      s = s + wx[1] * t[1][k];
      s = s + wx[2] * t[2][k];
      r[b][k] = s + wx[3] * t[3][k];
    }
  }
  for (int k = 0; k < 4; k++) {
    double s = wy[0] * r[0][k];
    s = s + wy[1] * r[1][k];
    s = s + wy[2] * r[2][k];
    s = s + wy[3] * r[3][k];
    s = s < lo[k] ? lo[k] : s;
    out[k] = s > hi[k] ? hi[k] : s;
  }
}

/** The filtered value from the taps of u and of v, premultiplied in the layer's space; @p filter and @p fast are the image's own, or constants a specialization has folded. */
IM_INLINE void filter_taps_k(const GSHCPU_Image * im, const GSHCPU_Image_Taps * x, const GSHCPU_Image_Taps * y, double out[4], uint32_t filter, int fast) {
  double f[4];
  switch (filter) {
    case GSHSCENE_IMAGE_FILTER_NEAREST:
      nearest(im, x, y, f, fast);
      break;
    case GSHSCENE_IMAGE_FILTER_LINEAR:
      linear(im, x, y, f, fast);
      break;
    default:
      cubic(im, x, y, f);
      break;
  }
  if (!fast && im->convert_out) {
    gshcpu_render_convert_premultiplied(&im->out, f, f);
  }
  memcpy(out, f, sizeof f);
}

IM_INLINE void filter_taps(const GSHCPU_Image * im, const GSHCPU_Image_Taps * x, const GSHCPU_Image_Taps * y, double out[4]) {
  filter_taps_k(im, x, y, out, im->filter, 0);
}

void gshcpu_render_image_point(const GSHCPU_Image * im, double u, double v, double out[4]) {
  GSHCPU_Image_Taps x;
  GSHCPU_Image_Taps y;
  taps_of(im->filter, u, &x);
  taps_of(im->filter, v, &y);
  filter_taps(im, &x, &y, out);
}

/* --- the pixel (IC8) ------------------------------------------------------------------------------------------ */

/**
 * IC8's pixel for the point-filter and copy modes, with the mode, the filter and the fast configuration as parameters: the generic function passes the
 * image's own, and each specialization (::PIXEL_FN) passes constants, so the tests of state that cannot change between two pixels are folded away.
 * Every arm is the arithmetic of the generic function in the same order, which is what keeps the tiers bit-equal (SC8).
 */
IM_INLINE void pixel_core(GSHCPU_Image * im, double qx, double qy, double src[4], int mode, uint32_t filter, int fast) {
  double f[4];
  if (mode == GSHCPU_IMAGE_COPY) {
    /* FT-1: the weights are exactly 1 and 0 (or the pixel's preimage is one cell), so the value is the texel under the pixel. */
    texel_k(im, gshcpu_render_trunc_i64(qx - 0.5) - im->copy_dx, gshcpu_render_trunc_i64(qy - 0.5) - im->copy_dy, f, fast);
    if (!fast && im->convert_out) {
      gshcpu_render_convert_premultiplied(&im->out, f, f);
    }
  } else {
    const double u = qx - im->inv[4];
    const double v = qy - im->inv[5];
    if (mode == GSHCPU_IMAGE_DIAGONAL || mode == GSHCPU_IMAGE_ANTI) {
      /* FT-2: one coordinate depends on the row alone; its taps are found once for the row and kept (IC10). */
      const int diagonal = mode == GSHCPU_IMAGE_DIAGONAL;
      GSHCPU_Image_Taps x;
      GSHCPU_Image_Taps y;
      if (!im->hoisted_valid || im->hoisted_key != qy) {
        taps_of(filter, diagonal ? im->inv[3] * v : im->inv[1] * v, &im->hoisted);
        im->hoisted_key = qy;
        im->hoisted_valid = 1;
      }
      if (diagonal) {
        taps_of(filter, im->inv[0] * u, &x);
        y = im->hoisted;
      } else {
        taps_of(filter, im->inv[2] * u, &y);
        x = im->hoisted;
      }
      filter_taps_k(im, &x, &y, f, filter, fast);
    } else {
      GSHCPU_Image_Taps x;
      GSHCPU_Image_Taps y;
      taps_of(filter, im->inv[0] * u + im->inv[1] * v, &x);
      taps_of(filter, im->inv[2] * u + im->inv[3] * v, &y);
      filter_taps_k(im, &x, &y, f, filter, fast);
    }
  }
  if (!fast && im->alpha_only) {
    const double k = (im->mult * im->tint_alpha) * f[3];
    src[0] = im->tint[0] * k;
    src[1] = im->tint[1] * k;
    src[2] = im->tint[2] * k;
    src[3] = k;
    return;
  }
  for (int k = 0; k < 4; k++) {
    src[k] = f[k] * im->mult;
  }
}

void gshcpu_render_image_at(GSHCPU_Image * im, double qx, double qy, double src[4]) {
  if (im->mode != GSHCPU_IMAGE_COPY && im->filter == (uint32_t)GSHSCENE_IMAGE_FILTER_AREA) {
    /* IC2: the pixel square [qx - 1/2, qx + 1/2] x [qy - 1/2, qy + 1/2], its preimage a parallelogram of the same shape for every pixel. */
    double f[4];
    const double u = (qx - 0.5) - im->inv[4];
    const double v = (qy - 0.5) - im->inv[5];
    const double origin[2] = {im->inv[0] * u + im->inv[1] * v, im->inv[2] * u + im->inv[3] * v};
    const double ea[2] = {im->inv[0], im->inv[2]};
    const double eb[2] = {im->inv[1], im->inv[3]};
    gshcpu_render_image_region(im, origin, ea, eb, f);
    if (im->alpha_only) {
      const double k = (im->mult * im->tint_alpha) * f[3];
      src[0] = im->tint[0] * k;
      src[1] = im->tint[1] * k;
      src[2] = im->tint[2] * k;
      src[3] = k;
      return;
    }
    for (int k = 0; k < 4; k++) {
      src[k] = f[k] * im->mult;
    }
    return;
  }
  pixel_core(im, qx, qy, src, im->mode, im->filter, 0);
}

/** One specialization of the pixel: mode, filter and the fast configuration are constants. */
#define PIXEL_FN(name, mode, filter, fast) \
  static void name(GSHCPU_Image * im, double qx, double qy, double src[4]) { pixel_core(im, qx, qy, src, mode, filter, fast); }

#define PIXEL_FAMILY(suffix, fast) \
  PIXEL_FN(px_copy_n_##suffix, GSHCPU_IMAGE_COPY, GSHSCENE_IMAGE_FILTER_NEAREST, fast) \
  PIXEL_FN(px_copy_l_##suffix, GSHCPU_IMAGE_COPY, GSHSCENE_IMAGE_FILTER_LINEAR, fast) \
  PIXEL_FN(px_diag_n_##suffix, GSHCPU_IMAGE_DIAGONAL, GSHSCENE_IMAGE_FILTER_NEAREST, fast) \
  PIXEL_FN(px_diag_l_##suffix, GSHCPU_IMAGE_DIAGONAL, GSHSCENE_IMAGE_FILTER_LINEAR, fast) \
  PIXEL_FN(px_anti_n_##suffix, GSHCPU_IMAGE_ANTI, GSHSCENE_IMAGE_FILTER_NEAREST, fast) \
  PIXEL_FN(px_anti_l_##suffix, GSHCPU_IMAGE_ANTI, GSHSCENE_IMAGE_FILTER_LINEAR, fast) \
  PIXEL_FN(px_gen_n_##suffix, GSHCPU_IMAGE_GENERAL, GSHSCENE_IMAGE_FILTER_NEAREST, fast) \
  PIXEL_FN(px_gen_l_##suffix, GSHCPU_IMAGE_GENERAL, GSHSCENE_IMAGE_FILTER_LINEAR, fast)

PIXEL_FAMILY(slow, 0)
PIXEL_FAMILY(fast, 1)

/** The fast configuration: the 8-bit RGBA table decode with nothing between the texel and the filter and nothing after it (no conversions, not alpha-only). */
static int fast_config(const GSHCPU_Image * im) {
  return im->lut && !im->is_float && im->bits <= 8u && im->layout == (uint32_t)GSHSCENE_IMAGE_RGBA && !im->alpha_only && !im->convert_in &&
      !im->convert_out;
}

/** The specialization for the placed image's mode, filter and configuration, or NULL where the generic function is the one (AREA, CUBIC). */
static GSHCPU_Image_Pixel_Fn pixel_fn_of(const GSHCPU_Image * im) {
  const int fast = fast_config(im);
  const int linear = im->filter == (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR;
  if (!linear && im->filter != (uint32_t)GSHSCENE_IMAGE_FILTER_NEAREST) {
    return NULL;
  }
#define PICK(m) (fast ? (linear ? px_##m##_l_fast : px_##m##_n_fast) : (linear ? px_##m##_l_slow : px_##m##_n_slow))
  switch (im->mode) {
    case GSHCPU_IMAGE_COPY:
      return PICK(copy);
    case GSHCPU_IMAGE_DIAGONAL:
      return PICK(diag);
    case GSHCPU_IMAGE_ANTI:
      return PICK(anti);
    default:
      return PICK(gen);
  }
#undef PICK
}

unsigned gshcpu_render_image_tiers(GSHCPU_Image * im, unsigned tiers, double lut[GSHCPU_IMAGE_LUT]) {
  unsigned taken = 0;
  const double * a = im->inv;
  im->mode = GSHCPU_IMAGE_GENERAL;
  im->hoisted_valid = 0;
  im->lut = NULL;
  im->pixel = NULL;
  im->tiers = tiers;
  const int integral = a[4] == gmath_floor(a[4]) && a[5] == gmath_floor(a[5]) && a[4] > -2147483648.0 && a[4] < 2147483648.0 &&
      a[5] > -2147483648.0 && a[5] < 2147483648.0;
  if ((tiers & GSHCPU_RENDER_TIER_IMAGE_COPY) && a[0] == 1.0 && a[1] == 0.0 && a[2] == 0.0 && a[3] == 1.0 && integral &&
      im->filter != (uint32_t)GSHSCENE_IMAGE_FILTER_CUBIC) {
    /* CUBIC is not a copy: its weights at a texel's center are 1/18, 8/9 and 1/18 (IC10). */
    im->mode = GSHCPU_IMAGE_COPY;
    im->copy_dx = gshcpu_render_trunc_i64(a[4]);
    im->copy_dy = gshcpu_render_trunc_i64(a[5]);
    taken |= GSHCPU_RENDER_TIER_IMAGE_COPY;
  } else if ((tiers & GSHCPU_RENDER_TIER_IMAGE_SEPARABLE) && im->filter != (uint32_t)GSHSCENE_IMAGE_FILTER_AREA) {
    if (a[1] == 0.0 && a[2] == 0.0 && a[0] != 0.0 && a[3] != 0.0) {
      im->mode = GSHCPU_IMAGE_DIAGONAL;
      taken |= GSHCPU_RENDER_TIER_IMAGE_SEPARABLE;
    } else if (a[0] == 0.0 && a[3] == 0.0 && a[1] != 0.0 && a[2] != 0.0) {
      im->mode = GSHCPU_IMAGE_ANTI;
      taken |= GSHCPU_RENDER_TIER_IMAGE_SEPARABLE;
    }
  }
  if ((tiers & GSHCPU_RENDER_TIER_IMAGE_TABLE) && lut && !im->is_float && im->bits <= 8u) {
    for (uint32_t v = 0; v < GSHCPU_IMAGE_LUT; v++) {
      lut[v] = (double)v / 255.0;
    }
    im->lut = lut;
    taken |= GSHCPU_RENDER_TIER_IMAGE_TABLE;
  }
  im->pixel = pixel_fn_of(im);
  return taken;
}
