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
 * Prints what this build is.
 *
 * The smallest program that links the library, so that `make examples` fails
 * if the installed headers and the shared library disagree.
 */

#include <ghoti.io/shape-cpu/shape-cpu.h>

#include <stdio.h>

int main(void) {
  printf("Ghoti.io Shape CPU %s (0x%06x)\n", gshcpu_version_string(),
      gshcpu_version_number());
  return 0;
}
