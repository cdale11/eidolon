#include "body/ode_tests.hpp"
#include <cmath>
#include <vector>

namespace eidolon {

// Energy decay: dx/dt = -0.0018 (base metabolic rate when awake)
ODETestResult testEnergyDecay(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return -0.0018; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 - 0.0018 * T;
  double error = std::abs(numeric - analytic);
  return {"EnergyDecay", numeric, analytic, error, error < tolerance};
}

// Hunger rise: dx/dt = +0.0012
ODETestResult testHungerRise(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return 0.0012; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 + 0.0012 * T;
  double error = std::abs(numeric - analytic);
  return {"HungerRise", numeric, analytic, error, error < tolerance};
}

// Thirst rise: dx/dt = +0.0013
ODETestResult testThirstRise(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return 0.0013; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 + 0.0013 * T;
  double error = std::abs(numeric - analytic);
  return {"ThirstRise", numeric, analytic, error, error < tolerance};
}

// Fatigue active: dx/dt = +0.022
ODETestResult testFatigueActive(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return 0.022; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 + 0.022 * T;
  double error = std::abs(numeric - analytic);
  return {"FatigueActive", numeric, analytic, error, error < tolerance};
}

// Fatigue rest: dx/dt = -0.06 * 0.25 = -0.015
ODETestResult testFatigueRest(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return -0.015; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 - 0.015 * T;
  double error = std::abs(numeric - analytic);
  return {"FatigueRest", numeric, analytic, error, error < tolerance};
}

// Sleep pressure awake: dx/dt = +0.0016
ODETestResult testSleepPressureAwake(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return 0.0016; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 + 0.0016 * T;
  double error = std::abs(numeric - analytic);
  return {"SleepPressureAwake", numeric, analytic, error, error < tolerance};
}

// Sleep pressure sleep: dx/dt = -0.0035
ODETestResult testSleepPressureSleep(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return -0.0035; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 - 0.0035 * T;
  double error = std::abs(numeric - analytic);
  return {"SleepPressureSleep", numeric, analytic, error, error < tolerance};
}

// Body temperature regulation: dx/dt = -0.0004*(x - 36.6) + 0.00005*(ambient - 36.6)
// = -0.0004*x + 0.0004*36.6 + 0.00005*ambient - 0.00005*36.6
// = -0.0004*x + C where C = 0.01464 + 0.00005*ambient - 0.00183
// Steady state: x_ss = C / 0.0004 = 36.6 + 0.125*(ambient - 36.6)
ODETestResult testBodyTempRegulation(double x0, double ambient, double T, double dt, double tolerance) {
  double C = 0.0004 * 36.6 + 0.00005 * (ambient - 36.6);
  double k = 0.0004;
  double x_ss = C / k; // = 36.6 + 0.125*(ambient - 36.6)
  // Analytic: x(t) = x_ss + (x0 - x_ss) * exp(-k*t)
  auto f = [k, C](double x, double) { return -k * x + C; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x_ss + (x0 - x_ss) * std::exp(-k * T);
  double error = std::abs(numeric - analytic);
  return {"BodyTempRegulation", numeric, analytic, error, error < tolerance};
}

// Body temp extreme cold: ambient = -40
ODETestResult testBodyTempExtremeCold(double x0, double T, double dt, double tolerance) {
  double ambient = -40.0;
  double C = 0.0004 * 36.6 + 0.00005 * (ambient - 36.6); // 0.01464 - 0.00383 = 0.01081
  double k = 0.0004;
  double x_ss = C / k; // ~27.0
  // For extreme cold, body temp drops toward ss but health damage kicks in at <31
  // Just test the ODE integration accuracy
  auto f = [k, C](double x, double) { return -k * x + C; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x_ss + (x0 - x_ss) * std::exp(-k * T);
  double error = std::abs(numeric - analytic);
  return {"BodyTempExtremeCold", numeric, analytic, error, error < tolerance};
}

// Body temp extreme heat: ambient = 50
ODETestResult testBodyTempExtremeHeat(double x0, double T, double dt, double tolerance) {
  double ambient = 50.0;
  double C = 0.0004 * 36.6 + 0.00005 * (ambient - 36.6); // 0.01464 + 0.00067 = 0.01531
  double k = 0.0004;
  double x_ss = C / k; // ~38.3
  auto f = [k, C](double x, double) { return -k * x + C; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x_ss + (x0 - x_ss) * std::exp(-k * T);
  double error = std::abs(numeric - analytic);
  return {"BodyTempExtremeHeat", numeric, analytic, error, error < tolerance};
}

// Health starvation: dx/dt = -0.05 (when energy <= 0)
ODETestResult testHealthStarvation(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return -0.05; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 - 0.05 * T;
  double error = std::abs(numeric - analytic);
  return {"HealthStarvation", numeric, analytic, error, error < tolerance};
}

// Health dehydration: dx/dt = -0.12 (when thirst >= 100)
ODETestResult testHealthDehydration(double x0, double T, double dt, double tolerance) {
  auto f = [](double, double) { return -0.12; };
  double numeric = eulerIntegrate(x0, T, dt, f);
  double analytic = x0 - 0.12 * T;
  double error = std::abs(numeric - analytic);
  return {"HealthDehydration", numeric, analytic, error, error < tolerance};
}

// Run all tests
std::vector<ODETestResult> runPhysiologyODETests(double dt, double tolerance) {
  std::vector<ODETestResult> results;
  const double T = 86400.0; // 1 simulated day

  results.push_back(testEnergyDecay(70.0, T, dt, tolerance));
  results.push_back(testHungerRise(0.0, T, dt, tolerance));
  results.push_back(testThirstRise(0.0, T, dt, tolerance));
  results.push_back(testFatigueActive(0.0, T, dt, tolerance));
  results.push_back(testFatigueRest(50.0, T, dt, tolerance));
  results.push_back(testSleepPressureAwake(5.0, T, dt, tolerance));
  results.push_back(testSleepPressureSleep(50.0, T, dt, tolerance));
  results.push_back(testBodyTempRegulation(36.6, 22.5, T, dt, tolerance));
  results.push_back(testBodyTempExtremeCold(36.6, T, dt, tolerance));
  results.push_back(testBodyTempExtremeHeat(36.6, T, dt, tolerance));
  results.push_back(testHealthStarvation(100.0, T, dt, tolerance));
  results.push_back(testHealthDehydration(100.0, T, dt, tolerance));

  return results;
}

} // namespace eidolon