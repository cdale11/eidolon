// Minimal embedded unit-test harness for Eidolon (no external deps).
// Phase 0: smoke test only. Real tests arrive with Phase 1+.

#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  CHECK(2 + 2 == 4);
  if (failures == 0) {
    std::printf("eidolon_tests: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "eidolon_tests: %d failure(s)\n", failures);
  return 1;
}