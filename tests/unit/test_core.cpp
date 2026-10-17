/**
 * @file
 *
 * The version: the placeholder test the scaffold ships with.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/shape-cpu/libver.h>

#include <cstring>

TEST(Version, StringMatchesTheGeneratedHeader) {
  EXPECT_STREQ(gshcpu_version_string(), GSHCPU_VERSION_STRING);
  EXPECT_EQ(gshcpu_version_number(), GSHCPU_VERSION_NUMBER);
  EXPECT_EQ(GSHCPU_VERSION_NUMBER,
      GSHCPU_MAKE_VERSION(GSHCPU_VERSION_MAJOR, GSHCPU_VERSION_MINOR,
          GSHCPU_VERSION_PATCH));
}

TEST(Version, StringIsNeverNullOrEmpty) {
  ASSERT_NE(gshcpu_version_string(), nullptr);
  EXPECT_GT(std::strlen(gshcpu_version_string()), 0u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
