#include "harness.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--list") == 0) {
    for (const auto& tc : ::eidolon::test::registry()) {
      std::printf("%s\n", tc.name);
    }
    return 0;
  }

  if (argc >= 3 && std::strcmp(argv[1], "--name") == 0) {
    std::printf("eidolon_tests\n");
    int failed = 0;
    for (const auto& tc : ::eidolon::test::registry()) {
      if (std::strcmp(tc.name, argv[2]) != 0) continue;
      ::eidolon::test::currentTestName() = tc.name;
      const int before = ::eidolon::test::failureCount();
      tc.fn();
      if (::eidolon::test::failureCount() > before) {
        std::printf("FAIL %s\n", tc.name);
        ++failed;
      } else {
        std::printf("ok   %s\n", tc.name);
      }
    }
    if (failed == 0) {
      std::printf("eidolon_tests: all tests passed\n");
      return 0;
    }
    std::fprintf(stderr, "eidolon_tests: %d test(s) failed\n", failed);
    return 1;
  }

  std::printf("eidolon_tests\n");
  const int failed = ::eidolon::test::runAll();
  if (failed == 0) {
    std::printf("eidolon_tests: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "eidolon_tests: %d test(s) failed\n", failed);
  return 1;
}