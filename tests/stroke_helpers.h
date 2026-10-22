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
 * What the stroke tests share (strokes story S4): drawing one stroke, or a list of them, into a transparent raster
 * with the tiers of a sink named, and reading the coverage back as the raster's alpha.  A white stroke of alpha 1
 * onto a transparent 8-bit straight raster leaves, at each pixel, the byte `m8` the sink composed (`apply` of
 * coverage `m8 / 255` onto zero is `m8 / 255`, written as `floor(m8 + 1/2)`), so the alpha channel is the sink's `k`
 * after the clips.
 */

#ifndef GHOTI_IO_GSHCPU_TEST_STROKE_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_STROKE_HELPERS_H

#include "render_helpers.h"
#include "stroke_gate.h"

#include "../src/render/render_internal.h"

#include <ghoti.io/shape-scene/stroke.h>

namespace sh {

using namespace rh;

/** A stroke style as the scene's, from geometry's parameters, each field by hand (not through the library's conversion). */
inline GSHSCENE_Stroke_Style scene_style(const GSHGEOM_Stroke & g) {
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = g.width;
  st.width_space = g.width_space;
  st.join = g.join;
  st.miter_limit = g.miter_limit;
  st.cap = g.cap;
  st.degenerate = g.degenerate;
  st.dash_count = g.dash_count;
  st.dashes = g.dashes;
  st.dash_offset = g.dash_offset;
  st.dash_scope = g.dash_scope;
  st.alignment = g.alignment;
  st.alignment_rule = g.alignment_rule;
  return st;
}

inline const GSHSCENE_Resource * intern_style(Store & s, const GSHGEOM_Stroke & g) {
  GSHSCENE_Stroke_Style st = scene_style(g);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &r), GSHSCENE_OK);
  s.held.push_back(r);
  return r;
}

/** A transparent raster of @p format. */
inline GIMG_Raster * transparent(GSHCPU_Format format, uint32_t w, uint32_t h) {
  GIMG_Pixel_Format f = rf_pixel_format(format);
  GIMG_Raster * r = nullptr;
  if (gimg_raster_create(w, h, &f, GIMG_RASTER_OWNED, nullptr, 0, &r) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * px = (unsigned char *)gimg_raster_pixels(r);
  size_t stride = gimg_raster_stride_bytes(r);
  size_t row = (size_t)w * gimg_raster_bytes_per_pixel(&f);
  for (uint32_t y = 0; y < h; y++) {
    std::memset(px + y * stride, 0, row);
  }
  return r;
}

/** The alpha channel of an 8-bit raster, row by row. */
inline std::vector<uint8_t> alpha_of(const GIMG_Raster * r) {
  std::vector<uint8_t> out;
  const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(r);
  size_t stride = gimg_raster_stride_bytes(r);
  for (uint32_t y = 0; y < gimg_raster_height(r); y++) {
    for (uint32_t x = 0; x < gimg_raster_width(r); x++) {
      out.push_back(px[y * stride + x * 4 + 3]);
    }
  }
  return out;
}

/** A sink with its counters and the tiers named. */
struct Sink {
  GSHSCENE_Sink * sink = nullptr;
  GSHCPU_Render_Counters counters{};
  explicit Sink(unsigned tiers = GSHCPU_RENDER_TIERS_ALL, const GSHCPU_Limits * limits = nullptr,
      const GSHSCENE_Allocator * alloc = nullptr) {
    EXPECT_EQ(gshcpu_render_sink_create_tiered(alloc, nullptr, limits, tiers, &counters, nullptr, nullptr, &sink), GSHCPU_OK);
  }
  ~Sink() {
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
  Sink(const Sink &) = delete;
  Sink & operator=(const Sink &) = delete;
};

struct Drawn {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  std::vector<uint8_t> alpha;
  uint64_t hash = 0;
  GSHSCENE_Report_Summary summary{};
  std::vector<GSHSCENE_Report_Entry> entries;
};

/** Render @p list into a fresh transparent raster on @p sink. */
inline Drawn render_list(GSHSCENE_Sink * sink, const GSHSCENE_List * list, uint32_t w, uint32_t h,
    GSHCPU_Format format = GSHCPU_FORMAT_RGBA8_STRAIGHT, uint32_t flags = 0, const GSHGEOM_Map * map = nullptr,
    const GSHSCENE_Rect * damage = nullptr, void * workspace = nullptr, size_t workspace_bytes = 0) {
  RasterRef raster(transparent(format, w, h));
  ReportRef report;
  GSHSCENE_Target t = target_of(raster.raster, map);
  t.damage = damage;
  GSHSCENE_Render_Request req = request_of(list, &t, report.report);
  req.flags = flags;
  req.workspace = workspace;
  req.workspace_bytes = workspace_bytes;
  Drawn d;
  d.result = gshscene_sink_render(sink, &req);
  d.summary = report.summary();
  d.entries = report.entries();
  for (GSHSCENE_Report_Entry & e : d.entries) {
    e.chain = nullptr;
  }
  d.summary.failure.chain = nullptr;
  if (format == GSHCPU_FORMAT_RGBA8_STRAIGHT || format == GSHCPU_FORMAT_RGBA8_PREMULTIPLIED) {
    d.alpha = alpha_of(raster.raster);
  }
  d.hash = raster.hash();
  return d;
}

/** One white stroke of the centerline @p ops under @p g and @p map, alone in a list. */
inline const GSHSCENE_List * one_stroke(Store & s, const stroke_gate::Ops & ops, const GSHGEOM_Stroke & g,
    const GSHGEOM_Map & map) {
  const GSHSCENE_Resource * path = s.path(stroke_gate::build(ops));
  const GSHSCENE_Resource * style = intern_style(s, g);
  GSHSCENE_Paint white = solid(1, 1, 1, 1);
  GSHSCENE_List * l = s.open();
  EXPECT_EQ(gshscene_list_stroke(l, path, style, &map, &white, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

/** The scene maps a test needs: translation, and the composition of a rotation and a scale about the origin. */
inline GSHGEOM_Map translation(double tx, double ty) {
  GSHGEOM_Map m;
  gshgeom_map_translate(2, tx, ty, 0.0, &m);
  return m;
}

inline GSHGEOM_Map affine_map(double a, double b, double c, double d, double tx, double ty) {
  double mm[4] = {a, b, c, d};
  double t[2] = {tx, ty};
  GSHGEOM_Map m;
  EXPECT_EQ(gshgeom_map_from_matrix(2, mm, t, &m), GSHGEOM_OK);
  return m;
}

}  // namespace sh

#endif /* GHOTI_IO_GSHCPU_TEST_STROKE_HELPERS_H */
