/**
 * @file
 *
 * Shared helpers for the sink's tests (story 13b): rendering a list on a raster
 * with a request of the test's choosing, reading the committed goldens, and
 * comparing pixels.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHCPU_TEST_RENDER_HELPERS_H
#define GHOTI_IO_GSHCPU_TEST_RENDER_HELPERS_H

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/testing/alloc_tracker.h>

#include "../src/render/render_internal.h"
#include "render_fixtures.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace rh {

/** A fixture's lists, held for the test. */
struct Fixture {
  RF_Ctx ctx;
  GSHSCENE_List * list = nullptr;
  explicit Fixture(size_t index) {
    rf_begin(&ctx);
    list = RF_FIXTURES[index].build(&ctx);
    EXPECT_EQ(ctx.failed, 0) << RF_FIXTURES[index].name;
  }
  ~Fixture() {
    rf_release(&ctx);
  }
  Fixture(const Fixture &) = delete;
  Fixture & operator=(const Fixture &) = delete;
};

struct RasterRef {
  GIMG_Raster * raster = nullptr;
  explicit RasterRef(GIMG_Raster * r = nullptr) : raster(r) {}
  ~RasterRef() {
    gimg_raster_destroy(raster);
  }
  RasterRef(const RasterRef &) = delete;
  RasterRef & operator=(const RasterRef &) = delete;
  /** The rows' bytes, without the stride. */
  std::string bytes() const {
    std::string out;
    size_t row = gimg_raster_width(raster) * gimg_raster_bytes_per_pixel(gimg_raster_format(raster));
    const char * px = (const char *)gimg_raster_pixels_const(raster);
    for (uint32_t y = 0; y < gimg_raster_height(raster); y++) {
      out.append(px + y * gimg_raster_stride_bytes(raster), row);
    }
    return out;
  }
  /** The bytes of pixel (x, y). */
  std::string pixel(uint32_t x, uint32_t y) const {
    size_t b = gimg_raster_bytes_per_pixel(gimg_raster_format(raster));
    const char * px = (const char *)gimg_raster_pixels_const(raster);
    return std::string(px + y * gimg_raster_stride_bytes(raster) + x * b, b);
  }
  uint64_t hash() const {
    uint64_t h = 0;
    EXPECT_EQ(gshcpu_render_hash(raster, nullptr, &h), GSHCPU_OK);
    return h;
  }
};

struct SinkRef {
  GSHSCENE_Sink * sink = nullptr;
  explicit SinkRef(const GSHCPU_Limits * limits = nullptr, const GSHSCENE_Allocator * alloc = nullptr) {
    EXPECT_EQ(gshcpu_sink_create(alloc, nullptr, limits, &sink), GSHCPU_OK);
  }
  ~SinkRef() {
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
  SinkRef(const SinkRef &) = delete;
  SinkRef & operator=(const SinkRef &) = delete;
};

struct ReportRef {
  GSHSCENE_Report * report = nullptr;
  explicit ReportRef(const GSHSCENE_Allocator * alloc = nullptr) {
    EXPECT_EQ(gshscene_report_create(alloc, nullptr, &report), GSHSCENE_OK);
  }
  ~ReportRef() {
    gshscene_report_destroy(report);
  }
  ReportRef(const ReportRef &) = delete;
  ReportRef & operator=(const ReportRef &) = delete;
  GSHSCENE_Report_Summary summary() const {
    GSHSCENE_Report_Summary s{};
    gshscene_report_summary(report, &s);
    return s;
  }
  std::vector<GSHSCENE_Report_Entry> entries() const {
    std::vector<GSHSCENE_Report_Entry> out;
    for (size_t i = 0; i < gshscene_report_count(report); i++) {
      GSHSCENE_Report_Entry e{};
      gshscene_report_entry(report, i, &e);
      out.push_back(e);
    }
    return out;
  }
};

/** A raster target on @p raster under @p map (NULL: the identity). */
inline GSHSCENE_Target target_of(GIMG_Raster * raster, const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_Target t{};
  t.kind = GSHSCENE_TARGET_RASTER;
  t.surface = raster;
  if (map) {
    t.map = *map;
  }
  return t;
}

inline GSHSCENE_Render_Request request_of(const GSHSCENE_List * list, const GSHSCENE_Target * target,
    GSHSCENE_Report * report) {
  GSHSCENE_Render_Request r{};
  r.list = list;
  r.target = target;
  r.report = report;
  return r;
}

/** The committed goldens: (fixture, format) to hash, from tests/corpus/render.hash. */
inline std::map<std::pair<std::string, std::string>, uint64_t> goldens() {
  std::map<std::pair<std::string, std::string>, uint64_t> out;
  std::ifstream in("tests/corpus/render.hash");
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string tag, fixture, format, hex;
    if (words >> tag >> fixture >> format >> hex && tag == "render") {
      out[{fixture, format}] = std::stoull(hex, nullptr, 16);
    }
  }
  return out;
}

/**
 * The list made of the first @p units items of the closed @p list (a list with no instance), with a pop for every
 * clip left open: what a render cancelled after those units has drawn.
 */
inline GSHSCENE_List * prefix(const GSHSCENE_List * list, GSHSCENE_Store * store, size_t units) {
  GSHSCENE_List * out = nullptr;
  EXPECT_EQ(gshscene_list_create(nullptr, nullptr, store, &out), GSHSCENE_OK);
  GSHSCENE_Color fg;
  gshscene_list_foreground(list, &fg);
  gshscene_list_set_foreground(out, &fg);
  size_t clips = 0;
  for (size_t i = 0; i < units; i++) {
    const GSHSCENE_Item * it = nullptr;
    EXPECT_EQ(gshscene_list_item(list, i, &it), GSHSCENE_OK);
    switch (it->kind) {
      case GSHSCENE_ITEM_FILL:
        EXPECT_EQ(gshscene_list_fill(out, it->resource, &it->map, it->fill_rule, &it->paint, it->layer, it->depth),
            GSHSCENE_OK);
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        EXPECT_EQ(gshscene_list_clip_push(out, it->resource, &it->map, it->fill_rule, it->layer, it->depth), GSHSCENE_OK);
        clips++;
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        EXPECT_EQ(gshscene_list_clip_pop(out), GSHSCENE_OK);
        clips--;
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        EXPECT_EQ(gshscene_list_group_push(out, &it->group, it->layer, it->depth), GSHSCENE_OK);
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        EXPECT_EQ(gshscene_list_group_pop(out), GSHSCENE_OK);
        break;
      default:
        ADD_FAILURE() << "prefix: an instance";
        break;
    }
  }
  for (; clips > 0; clips--) {
    EXPECT_EQ(gshscene_list_clip_pop(out), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_close(out), GSHSCENE_OK);
  return out;
}

/** Whether a closed list holds an instance at its top level. */
inline bool has_instance(const GSHSCENE_List * list) {
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    const GSHSCENE_Item * it = nullptr;
    gshscene_list_item(list, i, &it);
    if (it->kind == GSHSCENE_ITEM_INSTANCE) {
      return true;
    }
  }
  return false;
}

/** A path resource of a rectangle, held by @p held. */
inline const GSHSCENE_Resource * rect(GSHSCENE_Store * store, std::vector<const GSHSCENE_Resource *> & held, double x0,
    double y0, double x1, double y1) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{x0, y0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x1, y0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x1, y1, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x0, y1, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_path(store, p, &r), GSHSCENE_OK);
  gshgeom_path_destroy(p);
  held.push_back(r);
  return r;
}

/** A store and what a test interns in it; released at the end. */
struct Store {
  GSHSCENE_Store * store = nullptr;
  std::vector<const GSHSCENE_Resource *> held;
  std::vector<GSHSCENE_List *> lists;
  Store() {
    gshscene_store_create(nullptr, nullptr, &store);
  }
  ~Store() {
    for (GSHSCENE_List * l : lists) {
      gshscene_list_destroy(l);
    }
    for (const GSHSCENE_Resource * r : held) {
      gshscene_resource_release(r);
    }
    gshscene_store_destroy(store);
  }
  Store(const Store &) = delete;
  Store & operator=(const Store &) = delete;
  GSHSCENE_List * open() {
    GSHSCENE_List * l = nullptr;
    EXPECT_EQ(gshscene_list_create(nullptr, nullptr, store, &l), GSHSCENE_OK);
    lists.push_back(l);
    return l;
  }
  const GSHSCENE_Resource * rect(double x0, double y0, double x1, double y1) {
    return rh::rect(store, held, x0, y0, x1, y1);
  }
  const GSHSCENE_Resource * path(GSHGEOM_Path * p) {
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(store, p, &r), GSHSCENE_OK);
    gshgeom_path_destroy(p);
    held.push_back(r);
    return r;
  }
};

inline GSHSCENE_Paint solid(double r, double g, double b, double a) {
  return rf_solid(r, g, b, a);
}

} // namespace rh

#endif /* GHOTI_IO_GSHCPU_TEST_RENDER_HELPERS_H */
