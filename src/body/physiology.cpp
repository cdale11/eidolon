#include "body/physiology.hpp"

#include <algorithm>
#include <cmath>

namespace eidolon {

void Physiology::reset() {
  energy_ = 70.0;
  hunger_ = 0.0;
  thirst_ = 0.0;
  fatigue_ = 0.0;
  sleepPressure_ = 5.0;
  bodyTemp_ = kBodyTempC;
  health_ = 100.0;
  pain_ = 0.0;
  sleeping_ = false;
}

void Physiology::update(double dt, double ambientTempC, Activity act) {
  if (dt <= 0.0) return;
  const double asleep = sleeping_ ? 1.0 : 0.0;
  const double activity = static_cast<double>(act) / 3.0; // 0..1

  // Metabolism: energy burns with activity, much slower asleep. Without food/water the
  // organism can survive ~2-3 days on reserves + sleep recovery (Phase 2 adds foraging).
  energy_ -= 0.0018 * dt * (sleeping_ ? 0.35 : 1.0) * (0.6 + 0.4 * activity);

  // Thermoregulation: maintaining core temperature against cold/warm ambient costs
  // energy (hard winter is genuine pressure until shelter/clothes exist in later phases).
  const double ambientDev = std::abs(ambientTempC - 22.5);
  if (ambientDev > 7.5) {
    energy_ -= 0.00006 * (ambientDev - 7.5) * dt;
  }

  // Drives accumulate when awake (thirst kills in ~2 days, hunger in ~3, without input).
  hunger_ += 0.0012 * dt * (sleeping_ ? 0.5 : 1.0);
  thirst_ += 0.0013 * dt * (sleeping_ ? 0.4 : 1.0);

  // Fatigue: exertion builds it, rest and sleep clear it.
  fatigue_ += 0.022 * dt * activity;
  fatigue_ -= 0.06 * dt * (sleeping_ ? 1.0 : 0.25) * (1.0 - 0.5 * activity);
  fatigue_ = std::max(0.0, fatigue_);

  // Sleep pressure: rises awake, clears during sleep.
  sleepPressure_ += 0.0016 * dt * (1.0 - asleep);
  sleepPressure_ -= 0.0035 * dt * asleep;

  // Sleep restores energy and health slowly (a full night recovers ~80%).
  if (sleeping_) {
    energy_ += 0.0028 * dt;
    health_ += 0.0025 * dt;
  }

  // Thermoregulation: strong pull toward the setpoint, weak pull from ambient. Core
  // temperature only collapses when ambient is extreme; cold mainly drains energy above.
  bodyTemp_ += 0.0004 * (kBodyTempC - bodyTemp_) * dt + 0.00005 * (ambientTempC - kBodyTempC) * dt;
  if (bodyTemp_ < 31.0) {
    health_ -= (31.0 - bodyTemp_) * 0.05 * dt;
  } else if (bodyTemp_ > 40.5) {
    health_ -= (bodyTemp_ - 40.5) * 0.06 * dt;
  }

  // Starvation / dehydration damage (slow, so there is time to act in later phases).
  if (energy_ <= 0.0) health_ -= 0.05 * dt;
  if (hunger_ >= kMax) health_ -= 0.05 * dt;
  if (thirst_ >= kMax) health_ -= 0.12 * dt;

  clamp();
}

void Physiology::clamp() noexcept {
  energy_ = std::max(0.0, std::min(energy_, kMax));
  hunger_ = std::max(0.0, std::min(hunger_, kMax));
  thirst_ = std::max(0.0, std::min(thirst_, kMax));
  fatigue_ = std::max(0.0, std::min(fatigue_, kMax));
  sleepPressure_ = std::max(0.0, std::min(sleepPressure_, kMax));
  bodyTemp_ = std::max(20.0, std::min(bodyTemp_, 44.0));
  health_ = std::max(0.0, std::min(health_, kMax));
  pain_ = std::max(0.0, std::min(pain_, kMax));
}

void Physiology::serialize(BinaryWriter& w) const {
  w.f64(energy_);
  w.f64(hunger_);
  w.f64(thirst_);
  w.f64(fatigue_);
  w.f64(sleepPressure_);
  w.f64(bodyTemp_);
  w.f64(health_);
  w.f64(pain_);
  w.u8(sleeping_ ? 1 : 0);
}

bool Physiology::deserialize(BinaryReader& r) {
  if (!r.f64(energy_) || !r.f64(hunger_) || !r.f64(thirst_) || !r.f64(fatigue_) ||
      !r.f64(sleepPressure_) || !r.f64(bodyTemp_) || !r.f64(health_) ||
      !r.f64(pain_)) {
    return false;
  }
  uint8_t sleeping;
  if (!r.u8(sleeping)) return false;
  sleeping_ = sleeping != 0;
  clamp();
  return true;
}

} // namespace eidolon
