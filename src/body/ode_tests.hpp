#ifndef EIDOLON_ODE_TESTS_HPP
#define EIDOLON_ODE_TESTS_HPP

#include <vector>
#include <cstdint>
#include <functional>

#include "core/rng.hpp"

namespace eidolon {

// ODE analytic tests for body physiology (DESIGN §22, Phase 5 branch).
// Symplectic / explicit Euler with fixed step, max-rate caps.
// Unit tests against analytic solutions for each drive.

// Generic explicit Euler integrator with rate cap.
// dx/dt = f(x, t), solved with fixed step dt.
// Returns final x after T total time.
template <typename F>
double eulerIntegrate(double x0, double T, double dt, F f) {
  double x = x0;
  double t = 0.0;
  while (t < T - 1e-12) {
    double step = std::min(dt, T - t);
    x += step * f(x, t);
    t += step;
  }
  return x;
}

// Analytic solutions for each drive (body_.update with no activity/clamping):
// Energy: dx/dt = -rate (constant) -> x(t) = x0 - rate * t
// Hunger: dx/dt = +rate -> x(t) = x0 + rate * t
// Thirst: dx/dt = +rate -> x(t) = x0 + rate * t
// Fatigue: dx/dt = +rate (active) / -rate (rest) -> linear
// SleepPressure: dx/dt = +rate (awake) / -rate (sleep) -> linear
// BodyTemp: dx/dt = -k*(x - ambient) -> exponential decay to ambient
// Health: constant (no damage) or dx/dt = -damage_rate

// Test results structure
struct ODETestResult {
  const char* name;
  double numeric;
  double analytic;
  double error;
  bool passed;
};

// Run all analytic ODE tests for body physiology.
// Returns vector of results; all should pass (error < tolerance).
std::vector<ODETestResult> runPhysiologyODETests(double dt = 1.0, double tolerance = 1e-6);

// Energy decay test: dx/dt = -0.0018 (base metabolic rate)
ODETestResult testEnergyDecay(double x0, double T, double dt, double tolerance);

// Hunger rise test: dx/dt = +0.0012
ODETestResult testHungerRise(double x0, double T, double dt, double tolerance);

// Thirst rise test: dx/dt = +0.0013
ODETestResult testThirstRise(double x0, double T, double dt, double tolerance);

// Fatigue active test: dx/dt = +0.022
ODETestResult testFatigueActive(double x0, double T, double dt, double tolerance);

// Fatigue rest test: dx/dt = -0.06 * 0.25 (rest factor)
ODETestResult testFatigueRest(double x0, double T, double dt, double tolerance);

// Sleep pressure awake: dx/dt = +0.0016
ODETestResult testSleepPressureAwake(double x0, double T, double dt, double tolerance);

// Sleep pressure sleep: dx/dt = -0.0035
ODETestResult testSleepPressureSleep(double x0, double T, double dt, double tolerance);

// Body temperature regulation: dx/dt = -0.0004*(x - 36.6) + 0.00005*(ambient - 36.6)
// Test at ambient = 22.5 (steady state near 36.6)
ODETestResult testBodyTempRegulation(double x0, double ambient, double T, double dt, double tolerance);

// Body temp collapse at extreme cold: ambient = -40
ODETestResult testBodyTempExtremeCold(double x0, double T, double dt, double tolerance);

// Body temp collapse at extreme heat: ambient = 50
ODETestResult testBodyTempExtremeHeat(double x0, double T, double dt, double tolerance);

// Health starvation: dx/dt = -0.05 (energy <= 0)
ODETestResult testHealthStarvation(double x0, double T, double dt, double tolerance);

// Health dehydration: dx/dt = -0.12 (thirst >= 100)
ODETestResult testHealthDehydration(double x0, double T, double dt, double tolerance);

} // namespace eidolon

#endif // EIDOLON_ODE_TESTS_HPP