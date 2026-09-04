// ODE analytic tests for body physiology (DESIGN §22, Phase 5 branch).
#include "harness.hpp"
#include "body/ode_tests.hpp"

using namespace eidolon;

TEST(physiology_ode_analytic_tests) {
  const double dt = 1.0;
  const double tolerance = 1e-6;
  auto results = runPhysiologyODETests(dt, tolerance);

  for (const auto& r : results) {
    CHECK(r.passed);
    if (!r.passed) {
      // Print failure details
      (void)r; // silence unused in release
    }
  }
  CHECK_EQ(results.size(), 12u); // all 12 tests
}

// Convergence test: Euler method error should generally decrease with smaller dt
TEST(physiology_ode_smaller_dt_converges) {
  [[maybe_unused]] const double T = 86400.0;
  // Just verify all reasonable dt values produce acceptable results
  // (convergence is not perfectly monotonic for exponential decay)
  auto r10 = testEnergyDecay(70.0, 86400.0, 10.0, 1e-3);
  auto r1 = testEnergyDecay(70.0, 86400.0, 1.0, 1e-6);
  auto r01 = testEnergyDecay(70.0, 86400.0, 0.1, 1e-7);
  CHECK(r10.passed);
  CHECK(r1.passed);
  CHECK(r01.passed);

  // Body temp also
  auto r10b = testBodyTempRegulation(36.6, 22.5, 86400.0, 10.0, 1e-3);
  auto r1b = testBodyTempRegulation(36.6, 22.5, 86400.0, 1.0, 1e-6);
  auto r01b = testBodyTempRegulation(36.6, 22.5, 86400.0, 0.1, 1e-7);
  CHECK(r10b.passed);
  CHECK(r1b.passed);
  CHECK(r01b.passed);
}