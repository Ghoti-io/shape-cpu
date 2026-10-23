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
 * The earlier exact step (tests/support/exact_s4b.c) as a preloaded shared object, so that a program that draws strokes with
 * the library, the gallery or a benchmark frame, can be run twice, once as it is and once with the earlier step in place of
 * the library's, and its output compared byte for byte (the dual render of strokes story S4c, beyond the unit tests):
 *
 *     make -C libs/shape-cpu install PREFIX=P EXTRA_CFLAGS='-fvisibility=default'
 *     cc -std=c17 -O2 -fPIC -shared -I libs/shape-cpu/src/render `pkg-config --cflags ghoti.io-shape-cpu-0` \
 *        -o exact-frozen.so libs/shape-cpu/tools/exact-frozen.c
 *     LD_PRELOAD=./exact-frozen.so program ...
 *
 * (The library is built with its symbols visible so that the preloaded `gshcpu_exact_cell` is the one the scan converter's
 * calls reach, as for exact-census.c.)  Not part of the library.
 */

#include "../tests/support/exact_s4b.c"

int gshcpu_exact_cell(const GSHCPU_Exact_Params * params, GSHCPU_Exact_Scratch * s, const GSHCPU_Exact_Cell * cell,
    int64_t * out, uint64_t * work) {
  return gshcpu_s4b_exact_cell(params, s, cell, out, work);
}
