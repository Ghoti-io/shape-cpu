/**
 * @file
 *
 * Images for the sink's tests (masks-images-patterns story I3): a raster of a named format filled from a list of sample
 * values, interned as an image resource, and the image paint over it.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHCPU_TEST_IMAGE_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_IMAGE_HELPERS_H

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/shape-scene.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace ih {

/** A format as a test names it. */
struct Fmt {
  GIMG_Channel_Model model;
  GIMG_Channel_Type type;
  unsigned channels;
  unsigned bits;
  GIMG_Alpha_Form alpha;
  bool alpha_only;
};

inline GIMG_Pixel_Format format_of(const Fmt & f) {
  GIMG_Pixel_Format format;
  std::memset(&format, 0, sizeof format);
  format.channel_model = f.model;
  format.channel_type = f.type;
  format.layout = GIMG_LAYOUT_INTERLEAVED;
  format.channel_count = (uint8_t)f.channels;
  for (unsigned i = 0; i < f.channels && i < 8; i++) {
    format.bits_per_channel[i] = (uint8_t)f.bits;
  }
  format.alignment = 1;
  format.alpha_form = (uint8_t)f.alpha;
  return format;
}

inline Fmt fmt(unsigned channels, bool floating, unsigned bits, GIMG_Alpha_Form alpha, bool alpha_only = false) {
  Fmt f;
  f.model = channels == 1 ? GIMG_CHANNEL_GRAY : channels == 3 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
  f.type = floating ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  f.channels = channels;
  f.bits = bits;
  f.alpha = alpha;
  f.alpha_only = alpha_only;
  return f;
}

inline void put_sample(unsigned char * at, unsigned bits, uint32_t v) {
  if (bits <= 8) {
    *at = (unsigned char)v;
  } else if (bits <= 16) {
    uint16_t w = (uint16_t)v;
    std::memcpy(at, &w, 2);
  } else {
    std::memcpy(at, &v, 4);
  }
}

inline uint32_t float_bits(float f) {
  uint32_t v;
  std::memcpy(&v, &f, sizeof v);
  return v;
}

/**
 * An image of @p w by @p h in @p f from @p samples (row-major, `channels` a texel, a float as its bit pattern), interned in
 * @p store with @p space; the returned reference is the caller's to release.  NULL on failure.
 */
inline const GSHSCENE_Resource * intern_image(GSHSCENE_Store * store, uint32_t w, uint32_t h, const Fmt & f,
    const std::vector<uint32_t> & samples, GSHSCENE_Color_Space space = GSHSCENE_Color_Space{}) {
  GIMG_Pixel_Format format = format_of(f);
  GIMG_Raster * raster = nullptr;
  if (gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * base = (unsigned char *)gimg_raster_pixels(raster);
  const size_t stride = gimg_raster_stride_bytes(raster);
  const size_t size = f.bits <= 8 ? 1 : f.bits <= 16 ? 2 : 4;
  size_t at = 0;
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      for (unsigned c = 0; c < f.channels; c++) {
        put_sample(base + (size_t)y * stride + ((size_t)x * f.channels + c) * size, f.bits,
            at < samples.size() ? samples[at] : 0u);
        at++;
      }
    }
  }
  GSHSCENE_Image_Params p;
  std::memset(&p, 0, sizeof p);
  p.space = space;
  p.flags = f.alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0u;
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result result = gshscene_store_intern_image(store, raster, &p, &r);
  gimg_raster_destroy(raster);
  return result == GSHSCENE_OK ? r : nullptr;
}

} // namespace ih

#endif /* GHOTI_IO_GSHCPU_TEST_IMAGE_HELPERS_H */
