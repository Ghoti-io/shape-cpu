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
 * The version this build reports.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include <ghoti.io/shape-cpu/core.h>
#include <ghoti.io/shape-cpu/libver.h>

const char * gshcpu_version_string(void) {
  return GSHCPU_VERSION_STRING;
}

unsigned gshcpu_version_number(void) {
  return GSHCPU_VERSION_NUMBER;
}
