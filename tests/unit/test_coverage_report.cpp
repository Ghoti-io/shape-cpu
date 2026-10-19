/**
 * @file
 *
 * What can be asked of a coverage: pixels, total, hash, dump, release (CAP-13).
 * The hash is checked against values an independent program computed from the
 * spelling in hash.h, for coverages built by hand (so no scan converter is
 * involved), and the dump is checked against its stated text.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "coverage_helpers.h"
#include "known_answers.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace cov;

namespace {

/** A hand-built coverage over a buffer wider than its rows, padding set to 0xEE. */
struct Hand {
  GSHCPU_Coverage c{};
  std::vector<uint8_t> buffer;

  Hand(const known::Hand & k, size_t pad) {
    c.width = k.width;
    c.height = k.height;
    c.left = k.left;
    c.top = k.top;
    c.fill = static_cast<GSHGEOM_Fill_Rule>(k.fill);
    c.stride = k.width + pad;
    buffer.assign(c.stride * (k.height ? k.height : 1), 0xEE);
    const std::string hex = k.rows;
    for (uint32_t y = 0; y < k.height; ++y) {
      for (uint32_t x = 0; x < k.width; ++x) {
        buffer[y * c.stride + x] = (uint8_t)std::strtoul(
            hex.substr(2 * (y * k.width + x), 2).c_str(), nullptr, 16);
      }
    }
    c.data = k.height && k.width ? buffer.data() : nullptr;
  }
};

}  // namespace

TEST(CoverageHash, HandBuiltCoveragesHashAsTheIndependentProgramSays) {
  for (const known::Hand & k : known::hands()) {
    for (size_t pad : {0u, 3u}) {
      Hand h(k, pad);
      EXPECT_EQ(gshcpu_coverage_hash(&h.c), k.hash) << k.name << " pad " << pad;
    }
  }
}

TEST(CoverageHash, TheStrideIsNotHashed) {
  const known::Hand & k = known::hands()[4];
  Hand a(k, 0), b(k, 17);
  EXPECT_EQ(gshcpu_coverage_hash(&a.c), gshcpu_coverage_hash(&b.c));
}

TEST(CoverageHash, EveryFieldMatters) {
  const known::Hand & k = known::hands()[4];
  Hand h(k, 0);
  const uint64_t base = gshcpu_coverage_hash(&h.c);
  std::set<uint64_t> seen{base};
  auto changed = [&](auto && edit) {
    Hand c(k, 0);
    edit(c);
    const uint64_t v = gshcpu_coverage_hash(&c.c);
    EXPECT_TRUE(seen.insert(v).second) << "two hashes are equal";
  };
  changed([](Hand & c) { c.c.width -= 1; });
  changed([](Hand & c) { c.c.height -= 1; });
  changed([](Hand & c) { c.c.left += 1; });
  changed([](Hand & c) { c.c.top += 1; });
  changed([](Hand & c) { c.c.fill = GSHGEOM_FILL_EVEN_ODD; });
  changed([](Hand & c) { c.buffer[0] ^= 1; });
  changed([](Hand & c) { c.buffer[c.c.stride * 2 + 7] ^= 0x80; });
}

TEST(CoverageHash, ARowIsNotMistakenForTheNext) {
  // Two coverages with the same bytes in a different shape must differ.
  const uint8_t bytes[6] = {1, 2, 3, 4, 5, 6};
  GSHCPU_Coverage a{}, b{};
  a.data = const_cast<uint8_t *>(bytes); a.width = 3; a.height = 2; a.stride = 3;
  b.data = const_cast<uint8_t *>(bytes); b.width = 2; b.height = 3; b.stride = 2;
  EXPECT_NE(gshcpu_coverage_hash(&a), gshcpu_coverage_hash(&b));
}

TEST(CoverageHash, NullHashesAsTheEmptyStreamAndEmptyDoesNot) {
  EXPECT_EQ(gshcpu_coverage_hash(nullptr), GSHGEOM_FNV_OFFSET_BASIS);
  GSHCPU_Coverage empty{};
  EXPECT_NE(gshcpu_coverage_hash(&empty), GSHGEOM_FNV_OFFSET_BASIS);
  EXPECT_NE(gshcpu_coverage_hash(&empty), 0u);
}

TEST(CoverageAccess, AtAndTotalAreZeroForNullAndOutside) {
  EXPECT_EQ(gshcpu_coverage_at(nullptr, 0, 0), 0);
  EXPECT_EQ(gshcpu_coverage_total(nullptr), 0u);
  const known::Hand & k = known::hands()[4];
  Hand h(k, 5);
  EXPECT_EQ(gshcpu_coverage_at(&h.c, 1, 0), 32);
  EXPECT_EQ(gshcpu_coverage_at(&h.c, 7, 1), 248);
  EXPECT_EQ(gshcpu_coverage_at(&h.c, 8, 1), 0);
  EXPECT_EQ(gshcpu_coverage_at(&h.c, 0, 3), 0);
  uint64_t sum = 0;
  for (int i = 0; i < 8; ++i) sum += i * 32 + (255 - i);
  EXPECT_EQ(gshcpu_coverage_total(&h.c), sum);
  GSHCPU_Coverage no_data = h.c;
  no_data.data = nullptr;
  EXPECT_EQ(gshcpu_coverage_at(&no_data, 0, 0), 0);
  EXPECT_EQ(gshcpu_coverage_total(&no_data), 0u);
}

TEST(CoverageDump, IsTheStatedLine) {
  const known::Hand & k = known::hands()[3];   // negative-origin
  Hand h(k, 0);
  char* buf = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buf, &size);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(gshcpu_coverage_dump(&h.c, f), GSHCPU_OK);
  std::fclose(f);
  char want[256];
  std::snprintf(want, sizeof want,
      "gshcpu.coverage 1 3x2 at -2147483648,2147483647 fill=even-odd total=%llu hash=%016llx\n",
      (unsigned long long)(0 + 1 + 2 + 253 + 254 + 255), (unsigned long long)k.hash);
  EXPECT_STREQ(buf, want);
  std::free(buf);
}

TEST(CoverageDump, TheHashIsPaddedToSixteenDigits) {
  const known::Hand & k = known::hands()[5];   // zero-leading-hash-digit
  ASSERT_EQ(k.hash >> 60, 0u) << "the fixture is meant to have a leading zero digit";
  Hand h(k, 0);
  char * buf = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buf, &size);
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(gshcpu_coverage_dump(&h.c, f), GSHCPU_OK);
  std::fclose(f);
  char want[64];
  std::snprintf(want, sizeof want, "hash=%016llx\n", (unsigned long long)k.hash);
  EXPECT_NE(std::strstr(buf, want), nullptr) << buf;
  EXPECT_EQ(std::strstr(buf, want)[std::strlen(want)], '\0');
  std::free(buf);
}

TEST(CoverageDump, AFailingOrMissingStreamIsReported) {
  const known::Hand & k = known::hands()[2];
  Hand h(k, 0);
  EXPECT_EQ(gshcpu_coverage_dump(&h.c, nullptr), GSHCPU_ERR_INVALID);
  EXPECT_EQ(gshcpu_coverage_dump(nullptr, stdout), GSHCPU_ERR_INVALID);
  FILE * full = std::fopen("/dev/full", "w");
  if (!full) {
    GTEST_SKIP() << "no /dev/full on this host";
  }
  EXPECT_EQ(gshcpu_coverage_dump(&h.c, full), GSHCPU_ERR_IO);
  std::fclose(full);
  FILE * ro = std::fopen("/dev/null", "r");
  ASSERT_NE(ro, nullptr);
  EXPECT_EQ(gshcpu_coverage_dump(&h.c, ro), GSHCPU_ERR_IO);
  std::fclose(ro);
}

TEST(CoverageDestroy, ZeroesTheStructAndASecondCallIsHarmless) {
  Path path;
  path.polygon({{px(1), px(1)}, {px(3), px(1)}, {px(3), px(3)}, {px(1), px(3)}});
  GSHCPU_Coverage c{};
  ASSERT_EQ(gshcpu_coverage_from_path(path, GSHGEOM_FILL_NONZERO, nullptr, nullptr, &c), GSHCPU_OK);
  ASSERT_NE(c.data, nullptr);
  gshcpu_coverage_destroy(&c);
  EXPECT_EQ(c.data, nullptr);
  EXPECT_EQ(c.width, 0u);
  EXPECT_EQ(c.allocator, nullptr);
  gshcpu_coverage_destroy(&c);
  gshcpu_coverage_destroy(nullptr);
}

TEST(CoverageCore, EveryResultHasAName) {
  for (int r = 0; r < GSHCPU_RESULT_COUNT; ++r) {
    EXPECT_STRNE(gshcpu_result_string(static_cast<GSHCPU_Result>(r)), "unknown result") << r;
  }
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_RESULT_COUNT), "unknown result");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_OK), "ok");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_IO), "input/output error");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_FORMAT), "not a format handled here");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_UNSUPPORTED), "feature not supported");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_LIMIT), "a limit was exceeded");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_CORRUPT), "corrupt data");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_OOM), "out of memory");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_INVALID), "invalid argument");
  EXPECT_STREQ(gshcpu_result_string(GSHCPU_ERR_INTERNAL), "internal error");
  EXPECT_STREQ(gshcpu_result_string(static_cast<GSHCPU_Result>(99)), "unknown result");
  // The same vocabulary as shape-geometry, in the same order.
  EXPECT_EQ((int)GSHCPU_ERR_LIMIT, (int)GSHGEOM_ERR_LIMIT);
  EXPECT_EQ((int)GSHCPU_ERR_OOM, (int)GSHGEOM_ERR_OOM);
  EXPECT_EQ((int)GSHCPU_ERR_INVALID, (int)GSHGEOM_ERR_INVALID);
  EXPECT_EQ((int)GSHCPU_ERR_IO, (int)GSHGEOM_ERR_IO);
  EXPECT_EQ((int)GSHCPU_RESULT_COUNT, (int)GSHGEOM_RESULT_COUNT);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
