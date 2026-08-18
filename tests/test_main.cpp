#include "harness.hpp"

#include <cstdio>

int main() {
  std::printf("eidolon_tests\n");
  const int failed = ::eidolon::test::runAll();
  if (failed == 0) {
    std::printf("eidolon_tests: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "eidolon_tests: %d test(s) failed\n", failed);
  return 1;
}