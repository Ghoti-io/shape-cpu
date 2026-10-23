/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape CPU.
 */

/**
 * @file
 *
 * The exact step of strokes story S4b, frozen (exact_s4b.c): the second engine of the dual render.  Set
 * `gshcpu_exact_hook` (render_internal.h) to it to render with the older step.
 */

#ifndef GHOTI_IO_GSHCPU_TEST_EXACT_S4B_H
#define GHOTI_IO_GSHCPU_TEST_EXACT_S4B_H

#include "../../src/render/render_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ::gshcpu_exact_cell as strokes story S4b made it, with a winding bound of 255 (its scratch argument is not used). */
int gshcpu_s4b_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * unused, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_TEST_EXACT_S4B_H */
