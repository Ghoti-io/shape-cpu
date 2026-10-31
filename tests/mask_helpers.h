/**
 * @file
 *
 * Helpers for the tests of masks in the sink (masks-images-patterns story M2): pictures, masks and masked lists, and a render
 * that gives back what the tests look at (the bytes, the report, the counters, the work).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHCPU_TEST_MASK_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_MASK_HELPERS_H

#include "render_helpers.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace mh {

using namespace rh;

inline GSHSCENE_Color_Space space(GCOL_Primaries p, GCOL_Transfer t, double gamma = 0.0) {
  GSHSCENE_Color_Space s{};
  s.primaries = p;
  s.transfer = t;
  s.gamma = gamma;
  return s;
}

inline GSHSCENE_Color_Space srgb() {
  return space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
}

inline GSHSCENE_Color_Space linear() {
  return space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR);
}

/** A raster of @p format whose every byte is zero: the backdrop of a test that wants none. */
inline GIMG_Raster * blank(GSHCPU_Format format, uint32_t w, uint32_t h) {
  GIMG_Pixel_Format f = rf_pixel_format(format);
  GIMG_Raster * r = nullptr;
  if (gimg_raster_create(w, h, &f, GIMG_RASTER_OWNED, nullptr, 0, &r) != GIMG_OK) {
    return nullptr;
  }
  std::memset(gimg_raster_pixels(r), 0, gimg_raster_stride_bytes(r) * h);
  return r;
}

/** What a render gave. */
struct Draw {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::string bytes;
  uint64_t hash = 0;
  std::vector<GSHSCENE_Report_Entry> entries;
  std::vector<std::vector<size_t>> chains;  // each entry's chain, copied
  std::vector<size_t> failure;
  GSHSCENE_Report_Summary summary{};
  GSHCPU_Render_Counters counters{};
  size_t need = 0;                           // workspace_bytes of the request, without the flag
  std::string pixel_bytes_per_row;
  uint32_t width = 0, height = 0;
  size_t bpp = 0;
  /** The bytes of pixel (x, y). */
  std::string px(uint32_t x, uint32_t y) const {
    return bytes.substr((y * width + x) * bpp, bpp);
  }
  uint8_t at8(uint32_t x, uint32_t y, int channel) const {
    return (uint8_t)bytes[(y * width + x) * bpp + (size_t)channel];
  }
  float atf(uint32_t x, uint32_t y, int channel) const {
    float v;
    std::memcpy(&v, bytes.data() + (y * width + x) * bpp + 4u * (size_t)channel, 4);
    return v;
  }
  size_t count(GSHSCENE_Entry_Class c, GSHSCENE_Reason r = (GSHSCENE_Reason)0) const {
    size_t n = 0;
    for (const GSHSCENE_Report_Entry & e : entries) {
      n += e.entry_class == c && (!r || e.reason == r) ? 1u : 0u;
    }
    return n;
  }
};

struct Options {
  GSHCPU_Format format = GSHCPU_FORMAT_RGBA8_PREMULTIPLIED;
  uint32_t flags = 0;
  const GSHSCENE_Rect * damage = nullptr;
  unsigned tiers = GSHCPU_RENDER_TIERS_ALL;
  const GSHCPU_Limits * limits = nullptr;
  bool backdrop = false;                     // the fixtures' backdrop, not zeros
  const GSHGEOM_Map * map = nullptr;         // the target's map
  const GSHSCENE_Allocator * alloc = nullptr;
  GSHSCENE_Cancel * cancel = nullptr;
  GSHSCENE_Progress_Fn progress = nullptr;
  void * progress_ctx = nullptr;
};

/** Render @p list on a w by h raster. */
inline Draw draw(const GSHSCENE_List * list, uint32_t w, uint32_t h, const Options & o = Options()) {
  Draw d;
  d.width = w;
  d.height = h;
  RasterRef r(o.backdrop ? rf_raster(o.format, w, h) : blank(o.format, w, h));
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshcpu_render_sink_create_tiered(o.alloc, nullptr, o.limits, o.tiers, &d.counters, nullptr, nullptr, &sink), GSHCPU_OK);
  ReportRef report(o.alloc);
  GSHSCENE_Target t = target_of(r.raster, o.map);
  t.damage = o.damage;
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = o.flags;
  req.cancel = o.cancel;
  req.progress = o.progress;
  req.progress_ctx = o.progress_ctx;
  std::vector<unsigned char> workspace;
  EXPECT_EQ(gshscene_sink_workspace_bytes(sink, &req, &d.need), GSHSCENE_OK);
  if (o.flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    workspace.resize(d.need);
    req.workspace = workspace.data();
    req.workspace_bytes = d.need;
  }
  d.result = gshscene_sink_render(sink, &req);
  d.summary = report.summary();
  d.entries = report.entries();
  for (const GSHSCENE_Report_Entry & e : d.entries) {
    d.chains.emplace_back(e.chain, e.chain + e.chain_count);
  }
  d.failure.assign(d.summary.failure.chain, d.summary.failure.chain + d.summary.failure.chain_count);
  for (GSHSCENE_Report_Entry & e : d.entries) {
    e.chain = nullptr;
  }
  d.summary.failure.chain = nullptr;
  d.bytes = r.bytes();
  d.hash = r.hash();
  d.bpp = gimg_raster_bytes_per_pixel(gimg_raster_format(r.raster));
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  return d;
}

/** A scene: a store, and the pictures, masks and lists a test builds in it. */
struct Scene : Store {
  GSHSCENE_Group group(double opacity = 1.0, uint32_t composite = GSHSCENE_COMPOSITE_SRC_OVER, const GSHSCENE_Resource * mask = nullptr) {
    GSHSCENE_Group g{};
    g.opacity = opacity;
    g.composite = composite;
    g.soft_mask = mask;
    return g;
  }

  /** A picture of the list @p l (closed here, if it is not). */
  const GSHSCENE_Resource * picture(GSHSCENE_List * l) {
    if (!gshscene_list_is_closed(l)) {
      EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    }
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_picture(store, l, &r), GSHSCENE_OK);
    held.push_back(r);
    return r;
  }

  /** A picture of one fill of @p path in @p paint. */
  const GSHSCENE_Resource * picture_of(const GSHSCENE_Resource * path, const GSHSCENE_Paint & paint) {
    GSHSCENE_List * l = open();
    EXPECT_EQ(gshscene_list_fill(l, path, nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    return picture(l);
  }

  /** An interned mask; the fields not given are the defaults (an alpha mask acting on the opacity, the identity transfer). */
  const GSHSCENE_Resource * mask(const GSHSCENE_Mask & content) {
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_mask(store, &content, &r), GSHSCENE_OK);
    held.push_back(r);
    return r;
  }

  const GSHSCENE_Resource * alpha_mask(const GSHSCENE_Resource * picture, uint32_t acts_on = GSHSCENE_MASK_ACTS_ON_OPACITY) {
    GSHSCENE_Mask m{};
    m.picture = picture;
    m.mode = GSHSCENE_MASK_ALPHA;
    m.acts_on = acts_on;
    return mask(m);
  }

  /** A path resource of the closed polygon @p xy (x, y pairs). */
  const GSHSCENE_Resource * polygon(const std::vector<double> & xy) {
    GSHGEOM_Path * p = nullptr;
    gshgeom_path_create(nullptr, nullptr, 2, &p);
    for (size_t i = 0; i + 1 < xy.size(); i += 2) {
      GSHGEOM_Point q{xy[i], xy[i + 1], 0};
      if (i == 0) {
        gshgeom_path_move_to(p, q);
      } else {
        gshgeom_path_line_to(p, q);
      }
    }
    gshgeom_path_close(p);
    return path(p);
  }

  /** A list of one masked group of the fill of @p path in @p paint. */
  GSHSCENE_List * masked_fill(const GSHSCENE_Resource * mask, const GSHSCENE_Resource * path, const GSHSCENE_Paint & paint,
      double opacity = 1.0, uint32_t composite = GSHSCENE_COMPOSITE_SRC_OVER, const GSHGEOM_Map * placement = nullptr) {
    GSHSCENE_List * l = open();
    GSHSCENE_Group g = group(opacity, composite, mask);
    if (placement) {
      g.soft_mask_map = *placement;
    }
    EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, path, nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  }
};

inline GSHGEOM_Map translate(double x, double y) {
  GSHGEOM_Map m;
  EXPECT_EQ(gshgeom_map_translate(2, x, y, 0.0, &m), GSHGEOM_OK);
  return m;
}

inline GSHGEOM_Map scale(double x, double y) {
  GSHGEOM_Map m;
  EXPECT_EQ(gshgeom_map_scale(2, x, y, 1.0, &m), GSHGEOM_OK);
  return m;
}

} // namespace mh

#endif /* GHOTI_IO_GSHCPU_TEST_MASK_HELPERS_H */
