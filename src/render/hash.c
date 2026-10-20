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
 * The formats the sink writes, and the hash and dump of a render (render.h,
 * AD-21): the pixels of an extent, as values, so that the same render hashes
 * the same on a big-endian target.
 */

#include <ghoti.io/shape-cpu/macros.h>

#include "render_internal.h"

#include <ghoti.io/shape-geometry/hash.h>

#include <inttypes.h>
#include <string.h>

/** The format version of the hash and the dump. */
#define RENDER_FORMAT_VERSION 1u

/** The kind: "REND" little-endian. */
#define RENDER_KIND 0x444e4552u

GSHCPU_Format gshcpu_format_of(const GIMG_Raster * raster) {
  const GIMG_Pixel_Format * f = raster ? gimg_raster_format(raster) : NULL;
  if (!f || f->channel_model != GIMG_CHANNEL_RGBA || f->layout != GIMG_LAYOUT_INTERLEAVED || f->channel_count != 4) {
    return (GSHCPU_Format)0;
  }
  int premultiplied;
  if (f->alpha_form == GIMG_ALPHA_STRAIGHT) {
    premultiplied = 0;
  } else if (f->alpha_form == GIMG_ALPHA_PREMULTIPLIED) {
    premultiplied = 1;
  } else {
    return (GSHCPU_Format)0;
  }
  unsigned bits;
  if (f->channel_type == GIMG_CHANNEL_UNORM) {
    bits = 8;
  } else if (f->channel_type == GIMG_CHANNEL_FLOAT) {
    bits = 32;
  } else {
    return (GSHCPU_Format)0;
  }
  for (int i = 0; i < 4; i++) {
    if (f->bits_per_channel[i] != bits) {
      return (GSHCPU_Format)0;
    }
  }
  if (bits == 8) {
    return premultiplied ? GSHCPU_FORMAT_RGBA8_PREMULTIPLIED : GSHCPU_FORMAT_RGBA8_STRAIGHT;
  }
  return premultiplied ? GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED : GSHCPU_FORMAT_RGBAF32_STRAIGHT;
}

const char * gshcpu_format_string(GSHCPU_Format format) {
  switch (format) {
    case GSHCPU_FORMAT_RGBA8_STRAIGHT:
      return "rgba8";
    case GSHCPU_FORMAT_RGBA8_PREMULTIPLIED:
      return "rgba8-premultiplied";
    case GSHCPU_FORMAT_RGBAF32_STRAIGHT:
      return "rgbaf32";
    case GSHCPU_FORMAT_RGBAF32_PREMULTIPLIED:
      return "rgbaf32-premultiplied";
    case GSHCPU_FORMAT_COUNT:
      break;
  }
  return "unknown format";
}

/** Check a raster and an extent; the extent is the whole raster for NULL. */
static GSHCPU_Result checked(const GIMG_Raster * raster, const GSHSCENE_Rect * extent, GSHCPU_Format * format,
    GSHSCENE_Rect * r) {
  if (!raster) {
    return GSHCPU_ERR_INVALID;
  }
  *format = gshcpu_format_of(raster);
  if (!*format) {
    return GSHCPU_ERR_UNSUPPORTED;
  }
  if (extent) {
    *r = *extent;
  } else {
    r->x0 = 0;
    r->y0 = 0;
    r->x1 = gimg_raster_width(raster);
    r->y1 = gimg_raster_height(raster);
  }
  if (!(r->x0 >= 0 && r->x0 < r->x1 && r->x1 <= (int64_t)gimg_raster_width(raster) && r->y0 >= 0 && r->y0 < r->y1 &&
          r->y1 <= (int64_t)gimg_raster_height(raster))) {
    return GSHCPU_ERR_INVALID;
  }
  return GSHCPU_OK;
}

/** The four channels of a pixel as the hash spells them. */
static void hash_pixel(GSHGEOM_Hash * h, GSHCPU_Format format, const unsigned char * p) {
  if (gshcpu_render_pixel_bytes(format) == 4u) {
    for (int i = 0; i < 4; i++) {
      gshgeom_hash_u8(h, p[i]);
    }
    return;
  }
  for (int i = 0; i < 4; i++) {
    uint32_t bits;
    memcpy(&bits, p + 4 * i, sizeof bits); /* a float's bit pattern, in the host's order: as a value */
    gshgeom_hash_u32(h, bits);
  }
}

static uint64_t hash_of(const GIMG_Raster * raster, GSHCPU_Format format, GSHSCENE_Rect r) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, RENDER_FORMAT_VERSION);
  gshgeom_hash_u32(&h, RENDER_KIND);
  gshgeom_hash_u32(&h, (uint32_t)format);
  gshgeom_hash_i64(&h, r.x0);
  gshgeom_hash_i64(&h, r.y0);
  gshgeom_hash_i64(&h, r.x1);
  gshgeom_hash_i64(&h, r.y1);
  const unsigned char * pixels = gimg_raster_pixels_const(raster);
  size_t stride = gimg_raster_stride_bytes(raster);
  size_t bytes = gshcpu_render_pixel_bytes(format);
  for (int64_t y = r.y0; y < r.y1; y++) {
    const unsigned char * row = pixels + (size_t)y * stride;
    for (int64_t x = r.x0; x < r.x1; x++) {
      hash_pixel(&h, format, row + (size_t)x * bytes);
    }
  }
  return gshgeom_hash_value(&h);
}

GSHCPU_Result gshcpu_render_hash(const GIMG_Raster * raster, const GSHSCENE_Rect * extent, uint64_t * out) {
  GSHCPU_Format format;
  GSHSCENE_Rect r;
  if (!out) {
    return GSHCPU_ERR_INVALID;
  }
  GSHCPU_Result result = checked(raster, extent, &format, &r);
  if (result != GSHCPU_OK) {
    return result;
  }
  *out = hash_of(raster, format, r);
  return GSHCPU_OK;
}

GSHCPU_Result gshcpu_render_dump(const GIMG_Raster * raster, const GSHSCENE_Rect * extent, FILE * out) {
  GSHCPU_Format format;
  GSHSCENE_Rect r;
  if (!out) {
    return GSHCPU_ERR_INVALID;
  }
  GSHCPU_Result result = checked(raster, extent, &format, &r);
  if (result != GSHCPU_OK) {
    return result;
  }
  int failed = fprintf(out, "gshcpu.render %u format=%s extent=%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
      " hash=%016" PRIx64 "\n", RENDER_FORMAT_VERSION, gshcpu_format_string(format), r.x0, r.y0, r.x1, r.y1,
      hash_of(raster, format, r)) < 0;
  const unsigned char * pixels = gimg_raster_pixels_const(raster);
  size_t stride = gimg_raster_stride_bytes(raster);
  size_t bytes = gshcpu_render_pixel_bytes(format);
  for (int64_t y = r.y0; y < r.y1 && !failed; y++) {
    const unsigned char * row = pixels + (size_t)y * stride;
    failed = fprintf(out, "r %" PRId64, y) < 0;
    for (int64_t x = r.x0; x < r.x1 && !failed; x++) {
      const unsigned char * p = row + (size_t)x * bytes;
      if (bytes == 4u) {
        failed = fprintf(out, " %02x%02x%02x%02x", p[0], p[1], p[2], p[3]) < 0;
      } else {
        uint32_t v[4];
        memcpy(v, p, sizeof v);
        failed = fprintf(out, " %08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32, v[0], v[1], v[2], v[3]) < 0;
      }
    }
    if (!failed) {
      failed = fputc('\n', out) == EOF;
    }
  }
  if (failed) {
    return GSHCPU_ERR_IO;
  }
  /* ferror as well as fflush: on an unbuffered stream glibc's fprintf can return a count while the error flag is set. */
  return (fflush(out) == 0 && !ferror(out)) ? GSHCPU_OK : GSHCPU_ERR_IO;
}
