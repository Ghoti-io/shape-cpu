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
 * The element step of a general layer (render.h, KC1 to KC11): what one pixel of a layer of a non-isolated or a knockout group
 * does when an element composites into it, and what the layer holds when its group is popped.  The arithmetic is T3 to T5 of
 * shape-scene's transparency model, in `double`, with one operator (compose.c's `gshcpu_render_element`, which the plain
 * layer's `apply` also is, by B4) and N1's saturation where the waist says it.  Where the layers live, how the initial backdrop
 * is read, and which pixels a pop visits are sink.c's.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <string.h>

int gshcpu_render_group_step(double l[4], const double bk[4], double * ag, double * fg, int knockout, const double s[4], double m,
    double shape, uint32_t op) {
  if (m == 0.0 && shape == 0.0) {
    return 0; /* an element of no shape leaves the layer, its group alpha and its group shape as they are (T3 with f = 0) */
  }
  if (fg) {
    *fg = (*fg + shape) - *fg * shape; /* the group shape, after each element: fg + f - fg f */
  }
  /* A bounded operator with a source of nothing leaves L as it is when the backdrop is L (T6 at the defaults); in a knockout group
   * it knocks out its shape's share of what the layer holds beyond its initial backdrop (T3 with A = 0), unless the shape is 0 too. */
  if (gshcpu_render_bounded(op) && s[0] == 0.0 && s[1] == 0.0 && s[2] == 0.0 && s[3] == 0.0 && (!knockout || shape == 0.0)) {
    return 0;
  }
  if (!gshcpu_render_element(l, bk, s, m, shape, op)) {
    return -1; /* unreachable: the planning pass refuses an operator the caps do not claim */
  }
  if (ag) {
    /* The group alpha steps as the layer's alpha does, with 0 for its backdrop in a knockout group, from the alphas alone. */
    const double agb = knockout ? 0.0 : *ag;
    double lg[4] = {0.0, 0.0, 0.0, *ag};
    const double bg[4] = {0.0, 0.0, 0.0, agb};
    const double sg[4] = {0.0, 0.0, 0.0, s[3]};
    (void)gshcpu_render_element(lg, bg, sg, m, shape, op);
    *ag = lg[3];
  }
  return 1;
}

void gshcpu_render_group_result(const double l[4], double ag, int isolated, const double l0[4], double g[4]) {
  if (isolated) {
    memcpy(g, l, 4 * sizeof *g); /* L0 = 0 and the layer's alpha is the group's */
    return;
  }
  const double keep = 1.0 - ag;
  for (int i = 0; i < 3; i++) {
    g[i] = gshcpu_render_saturate(l[i] - keep * l0[i]); /* T5: the initial backdrop taken out */
  }
  g[3] = ag;
}
