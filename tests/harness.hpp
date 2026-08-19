// Minimal embedded unit-test harness (no external framework, no exceptions).
// Each test file defines TEST(name) functions; test_main.cpp runs the registry.
#pragma once

#include <cstdio>
#include <vector>

namespace eidolon::test {

struct TestCase {
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

inline int& failureCount() {
  static int f = 0;
  return f;
}

inline const char*& currentTestName() {
  static const char* n = "";
  return n;
}

inline void recordFailure(const char* file, int line, const char* expr) {
  std::fprintf(stderr, "  CHECK failed %s:%d: %s\n", file, line, expr);
  ++failureCount();
}

struct Registrar {
  Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

inline int runAll() {
  int failed = 0;
  for (const TestCase& tc : registry()) {
    currentTestName() = tc.name;
    const int before = failureCount();
    tc.fn();
    if (failureCount() > before) {
      std::printf("FAIL %s\n", tc.name);
      ++failed;
    } else {
      std::printf("ok   %s\n", tc.name);
    }
  }
  currentTestName() = "";
  return failed;
}

} // namespace eidolon::test

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) ::eidolon::test::recordFailure(__FILE__, __LINE__, #cond);    \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    const auto va = (a);                                                       \
    const auto vb = (b);                                                       \
    if (!(va == vb)) {                                                         \
      ::eidolon::test::recordFailure(__FILE__, __LINE__, #a " == " #b);        \
    }                                                                          \
  } while (0)

#define TEST(name)                                                             \
  static void name();                                                          \
  static ::eidolon::test::Registrar reg_##name(#name, &name);                  \
  static void name()
