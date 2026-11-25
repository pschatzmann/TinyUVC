#pragma once
// Minimal test harness: CHECK() records failures, RUN() runs a test case,
// and testMain() returns the exit code for ctest.
#include <cstdio>
#include <cstdlib>

namespace testutils {
inline int &failures() {
  static int f = 0;
  return f;
}
inline int &checks() {
  static int c = 0;
  return c;
}
}  // namespace testutils

#define CHECK(cond)                                                        \
  do {                                                                     \
    testutils::checks()++;                                                 \
    if (!(cond)) {                                                         \
      testutils::failures()++;                                             \
      std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                      \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    testutils::checks()++;                                                   \
    auto va_ = (a);                                                          \
    auto vb_ = (b);                                                          \
    if (!(va_ == vb_)) {                                                     \
      testutils::failures()++;                                               \
      std::fprintf(stderr, "  FAILED %s:%d: %s == %s (%lld vs %lld)\n",      \
                   __FILE__, __LINE__, #a, #b, (long long)va_, (long long)vb_); \
    }                                                                        \
  } while (0)

#define RUN(test)                                   \
  do {                                              \
    std::printf("[ RUN  ] %s\n", #test);            \
    int before_ = testutils::failures();            \
    test();                                         \
    std::printf("[ %s ] %s\n",                      \
                testutils::failures() == before_ ? " OK " : "FAIL", #test); \
  } while (0)

inline int testMain() {
  std::printf("%d checks, %d failures\n", testutils::checks(),
              testutils::failures());
  return testutils::failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
