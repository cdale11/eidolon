// Simulated body: compact physiological state with homeostatic dynamics.
// Phase 1 covers energy, hunger, thirst, fatigue, sleep pressure, temperature, health.
#pragma once

#include <cstdint>

#include "core/serialize.hpp"

namespace eidolon {

enum class Activity : uint8_t { Sleep = 0, Rest = 1, Observe = 2, Move = 3 };

class Physiology {
public:
  // Bounds.
  static constexpr double kMax = 100.0;
  static constexpr double kBodyTempC = 36.6;

  void reset();

  // Advance physiology by dt simulated seconds. `ambientTempC` drives thermoregulation.
  void update(double dt, double ambientTempC, Activity act);

  bool needsSleep() const {
    return sleepPressure() >= 55.0 || (energy() < 30.0 && fatigue() > 40.0);
  }
  bool isSleeping() const { return sleeping_; }
  void setSleeping(bool s) { sleeping_ = s; }

  double energy() const { return energy_; }
  double hunger() const { return hunger_; }
  double thirst() const { return thirst_; }
  double fatigue() const { return fatigue_; }
  double sleepPressure() const { return sleepPressure_; }
  double bodyTemp() const { return bodyTemp_; }
  double health() const { return health_; }
  double pain() const { return pain_; }

  bool alive() const { return health_ > 0.0 && energy_ >= 0.0; }

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  void clamp() noexcept;

  double energy_ = 70.0;
  double hunger_ = 0.0;
  double thirst_ = 0.0;
  double fatigue_ = 0.0;
  double sleepPressure_ = 5.0;
  double bodyTemp_ = kBodyTempC;
  double health_ = 100.0;
  double pain_ = 0.0;
  bool sleeping_ = false;
};

} // namespace eidolon
