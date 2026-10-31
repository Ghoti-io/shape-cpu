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
 * The value of a soft mask at a pixel (render.h, MC3): the premultiplied layer a mask's picture left, in the picture's
 * render space `R`, to the number in `[0, 1]` that weighs the masked group's pop.  The coefficients, the spaces and the
 * transfer are shape-scene's one resolution and one evaluation (`gshscene_mask_resolve`, `gshscene_mask_transfer`); this
 * file holds only the order of the arithmetic, in `double`, with N1's saturation at each point the waist names it, and
 * no rounding: the value meets the pop's composite unrounded, and the write to the target rounds once (D7).
 *
 * The walk of the mask's picture, the frame it draws into and the pop that uses the value are sink.c's.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <string.h>

/** `clamp01(x)`; a NaN is 0 (MK2). */
static double clamp01(double x) {
  return x > 0.0 ? (x > 1.0 ? 1.0 : x) : 0.0;
}

int gshcpu_render_mask_prepare(GSHCPU_Mask_Eval * e, const GSHSCENE_Resource * mask, const GSHSCENE_Color_Space * enclosing) {
  GSHSCENE_Mask m;
  GSHSCENE_Mask_Resolved res;
  memset(e, 0, sizeof *e);
  if (gshscene_resource_mask(mask, &m) != GSHSCENE_OK || gshscene_mask_resolve(mask, enclosing, &res) != GSHSCENE_OK) {
    return 0;
  }
  e->mask = mask;
  e->alpha = m.mode == (uint32_t)GSHSCENE_MASK_ALPHA;
  e->identity = m.transfer.kind == (uint32_t)GSHSCENE_TRANSFER_IDENTITY;
  e->shape = m.acts_on == (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE;
  e->frame_space = res.render_space;
  for (int i = 0; i < 3; i++) {
    e->k[i] = res.k[i];
  }
  /* MK2: the backdrop converted (N3) into the luminance space, once for the pop. */
  GSHCPU_Convert backdrop;
  gshcpu_render_convert_prepare(&backdrop, &res.backdrop_space, &res.luminance_space);
  gshcpu_render_convert_straight(&backdrop, m.backdrop, e->backdrop);
  gshcpu_render_convert_prepare(&e->to_l, &res.render_space, &res.luminance_space);
  return 1;
}

double gshcpu_render_mask_value(const GSHCPU_Mask_Eval * e, const double g[4], int identity_tier) {
  const double a = g[3];
  double y;
  if (e->alpha) {
    y = clamp01(a);
  } else {
    /* (c_L, a) = convert(G_m, R to L) on the premultiplied value; r = sat(c_L + (1 - a) B_L): the picture composited over
     * the backdrop in L; y = sat(k . r). */
    double c[4];
    gshcpu_render_convert_premultiplied(&e->to_l, g, c);
    double over[3];
    for (int i = 0; i < 3; i++) {
      over[i] = gshcpu_render_saturate(c[i] + (1.0 - a) * e->backdrop[i]);
    }
    y = clamp01(gshcpu_render_saturate(e->k[0] * over[0] + e->k[1] * over[1] + e->k[2] * over[2]));
  }
  if (identity_tier && e->identity) {
    return y; /* FT-M2: the identity transfer is x, and the waist clamps its input and its output, both already in [0, 1] */
  }
  return clamp01(gshscene_mask_transfer(e->mask, y));
}
