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
 * @file core.h
 *
 * The version this build reports.
 */

#ifndef GHOTI_IO_GSHCPU_CORE_H
#define GHOTI_IO_GSHCPU_CORE_H

#include <ghoti.io/shape-cpu/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * @return A static string, never NULL.  "0.0.0", or "0.0.0-dev" when BRANCH
 *   was overridden.
 */
GSHCPU_API const char * gshcpu_version_string(void);

/**
 * @brief This build's version, packed as ::GSHCPU_MAKE_VERSION packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GSHCPU_API unsigned gshcpu_version_number(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHCPU_CORE_H */
