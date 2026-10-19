/**
 * @file
 *
 * A sink that fails the k-th write, for the dumps' error paths (CAP-13).
 *
 * An unbuffered stream's `fprintf` can return a count while the stream's error
 * flag is set, so a dump that checks every return value and ends with `fflush`
 * reports success for a write that failed.  This is the instrument that shows
 * it: a stream over `fopencookie` whose `fail_at`-th write fails, buffered and
 * unbuffered.  glibc only; elsewhere the helper does nothing.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHCPU_TEST_DUMP_SINK_H
#define GHOTI_IO_GSHCPU_TEST_DUMP_SINK_H

#include <gtest/gtest.h>

#include <cstdio>

#ifdef __GLIBC__
namespace dump_sink {

struct Cookie {
  long fail_at;  // the write call that fails; -1 never
  long writes;
};

inline ssize_t sink_write(void * cookie, const char *, size_t size) {
  Cookie * c = (Cookie *)cookie;
  long n = c->writes++;
  if (n == c->fail_at) {
    return -1;
  }
  return (ssize_t)size;
}

/** Run @p dump into a stream whose @p fail_at-th write fails (once); returns the dump's result as an int. */
template <typename Dump, typename Object>
int run(Dump dump, const Object * object, long fail_at, long * writes, bool unbuffered) {
  Cookie cookie{fail_at, 0};
  cookie_io_functions_t functions = {nullptr, sink_write, nullptr, nullptr};
  FILE * f = fopencookie(&cookie, "w", functions);
  if (!f) {
    return -1;
  }
  if (unbuffered) {
    std::setvbuf(f, nullptr, _IONBF, 0);
  }
  int result = (int)dump(object, f);
  std::fclose(f);
  *writes = cookie.writes;
  return result;
}

/**
 * Every write failing in turn must make the dump report @p io_error; a write past the last
 * one changes nothing.  Unbuffered, every call of the dump is a write; buffered, the writes
 * are the dump's flushes.
 */
template <typename Dump, typename Object>
void expect_every_write_checked(Dump dump, const Object * object, int io_error, const char * what) {
  for (bool unbuffered : {true, false}) {
    long total = 0;
    ASSERT_EQ(run(dump, object, -1, &total, unbuffered), 0) << what;
    ASSERT_GT(total, 0) << what;
    for (long k = 0; k < total; k++) {
      long writes = 0;
      EXPECT_EQ(run(dump, object, k, &writes, unbuffered), io_error)
          << what << (unbuffered ? " (unbuffered)" : " (buffered)") << ": write " << k << " of " << total
          << " failed unnoticed";
    }
    long writes = 0;
    EXPECT_EQ(run(dump, object, total, &writes, unbuffered), 0) << what;
  }
}

}  // namespace dump_sink
#define GSHCPU_EVERY_WRITE_CHECKED(dump, object, what) \
  dump_sink::expect_every_write_checked(dump, object, (int)GSHCPU_ERR_IO, what)
#else
#define GSHCPU_EVERY_WRITE_CHECKED(dump, object, what) GTEST_SKIP() << "no fopencookie on this host"
#endif

#endif /* GHOTI_IO_GSHCPU_TEST_DUMP_SINK_H */
