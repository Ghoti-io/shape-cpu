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
 * @file namespace.h
 *
 * Maps every public name of this library into its version namespace.
 *
 * Kept in one file rather than beside each declaration: a type rename has to
 * be in effect before any struct tag that uses the name, and an internal
 * header may define such a tag without including the public header that
 * declares the typedef.
 *
 * `make check-symbols` fails if an exported symbol is missing from this list.
 *
 * See CONVENTIONS.md section 4.
 */

#ifndef GHOTI_IO_GSHCPU_NAMESPACE_H
#define GHOTI_IO_GSHCPU_NAMESPACE_H

#include <ghoti.io/shape-cpu/libver.h>

/// @cond HIDDEN_SYMBOLS

/* Public functions. */
#define GSHCPU_Result GHOTIIO_SHAPE_CPU(GSHCPU_Result)
#define GSHCPU_Limits GHOTIIO_SHAPE_CPU(GSHCPU_Limits)
#define GSHCPU_Allocator GHOTIIO_SHAPE_CPU(GSHCPU_Allocator)
#define GSHCPU_Coverage GHOTIIO_SHAPE_CPU(GSHCPU_Coverage)
#define gshcpu_result_string GHOTIIO_SHAPE_CPU(gshcpu_result_string)
#define gshcpu_limits_default GHOTIIO_SHAPE_CPU(gshcpu_limits_default)
#define gshcpu_coverage_fill GHOTIIO_SHAPE_CPU(gshcpu_coverage_fill)
#define gshcpu_coverage_from_path GHOTIIO_SHAPE_CPU(gshcpu_coverage_from_path)
#define gshcpu_coverage_trim GHOTIIO_SHAPE_CPU(gshcpu_coverage_trim)
#define gshcpu_coverage_destroy GHOTIIO_SHAPE_CPU(gshcpu_coverage_destroy)
#define gshcpu_coverage_at GHOTIIO_SHAPE_CPU(gshcpu_coverage_at)
#define gshcpu_coverage_total GHOTIIO_SHAPE_CPU(gshcpu_coverage_total)
#define gshcpu_coverage_hash GHOTIIO_SHAPE_CPU(gshcpu_coverage_hash)
#define gshcpu_coverage_dump GHOTIIO_SHAPE_CPU(gshcpu_coverage_dump)
#define gshcpu_version_number GHOTIIO_SHAPE_CPU(gshcpu_version_number)
#define gshcpu_version_string GHOTIIO_SHAPE_CPU(gshcpu_version_string)
#define GSHCPU_Format GHOTIIO_SHAPE_CPU(GSHCPU_Format)
#define gshcpu_format_of GHOTIIO_SHAPE_CPU(gshcpu_format_of)
#define gshcpu_format_string GHOTIIO_SHAPE_CPU(gshcpu_format_string)
#define gshcpu_sink_create GHOTIIO_SHAPE_CPU(gshcpu_sink_create)
#define gshcpu_sink_is GHOTIIO_SHAPE_CPU(gshcpu_sink_is)
#define gshcpu_render_hash GHOTIIO_SHAPE_CPU(gshcpu_render_hash)
#define gshcpu_render_dump GHOTIIO_SHAPE_CPU(gshcpu_render_dump)

/// @endcond

#endif // GHOTI_IO_GSHCPU_NAMESPACE_H
