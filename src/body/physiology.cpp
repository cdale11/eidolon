#include "body/physiology.hpp"

#include <algorithm>
#include <cmath>

namespace eidolon {

namespace {
constexpr int kMaxWounds = 8;
constexpr double kInfectionThreshold = 0.5;   // total infection load for "sick"
constexpr double kExposureThreshold = 0.6;    // chronic exposure needed to infect wounds
constexpr double kMinimumInfectableAge = 600; // wounds younger than 10 min don't infect
constexpr double kMinimumInfectableSeverity = 0.3;
} // namespace

void Wound::serialize(BinaryWriter& w) const {
  w.f64(severity);
  w.f64(infection);
  w.i64(age);
  w.u8(source);
}

bool Wound::deserialize(BinaryReader& r) {
  if (!r.f64(severity) || !r.f64(infection) || !r.i64(age) || !r.u8(source)) return false;
  severity = std::max(0.0, std::min(severity, 1.0));
  infection = std::max(0.0, std::min(infection, 1.0));
  return true;
}

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
  immunity_ = 0.5;
  exposure_ = 0.0;
  wounds_.clear();
}

void Physiology::update(double dt, double ambientTempC, Activity act, double hazardDose,
              int nearbyInfected) {
  if (dt <= 0.0) return;
  const double asleep = sleeping_ ? 1.0 : 0.0;
  const double activity = act == Activity::Sleep ? 0.0
                          : act == Activity::Rest ? 0.25
                          : act == Activity::Observe ? 0.5
                                                     : 1.0; // Move/Forage/Drink

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

  // Phase 5 hazards: exposure, immune dynamics, wound healing and infection.
  // Nearby infected CA cells increase exposure (disease spread) and infection growth.
  if (nearbyInfected > 0) {
    const double spreadFactor = std::min(1.0, static_cast<double>(nearbyInfected) / 8.0);
    exposure_ += spreadFactor * 0.001 * dt;
    // Infected CA cells also boost existing wound infections slightly.
    if (!wounds_.empty()) {
      for (Wound& w : wounds_) {
        if (w.infection > 0.0) {
          w.infection = std::min(1.0, w.infection + spreadFactor * 0.0001 * dt);
        }
      }
    }
  }
  updateExposure(hazardDose, dt);
  const bool restingOrSleeping = act == Activity::Rest || act == Activity::Sleep;
  immunity_ += 0.00005 * dt;  // slow baseline recovery
  if (energy_ > 70.0 && hunger_ < 30.0 && thirst_ < 40.0 && restingOrSleeping)
    immunity_ += 0.00012 * dt;  // well-fed rest strengthens immunity
  if (hunger_ >= 80.0 || thirst_ >= 80.0 || energy_ < 20.0 || sick())
    immunity_ -= 0.0002 * dt;  // deprivation / active infection weakens it
  immunity_ = std::max(0.1, std::min(immunity_, 1.0));

  double infectionLoad = 0.0;
  for (auto it = wounds_.begin(); it != wounds_.end();) {
    Wound& w = *it;
    w.age += static_cast<int64_t>(dt);
    if (w.infection > 0.0) {
      // The immune system fights the infection; clearing depends on immunity.
      w.infection -= immunity_ * 0.00015 * dt;
      infectionLoad += w.infection;
      if (w.infection <= 0.0) w.infection = 0.0;
    } else {
      // Uninfected wounds heal; resting/sleeping doubles the rate.
      const double heal = 0.00012 * dt * (restingOrSleeping ? 2.0 : 1.0);
      w.severity -= heal;
    }
    if (w.severity <= 0.02) {
      it = wounds_.erase(it);
    } else {
      ++it;
    }
  }

  // Sickness: a heavy infection drains energy and health and induces fever.
  if (infectionLoad >= kInfectionThreshold) {
    health_ -= 0.0004 * dt;
    energy_ -= 0.0006 * dt;
    bodyTemp_ += 0.0003 * (38.5 - bodyTemp_) * dt;  // mild fever toward 38.5C
  }

  // Acute pain slowly subsides (chronic wound pain remains via wounds_).
  pain_ = std::max(0.0, pain_ - 0.0015 * dt);

  clamp();
}

void Physiology::addWound(double severity, uint8_t source) {
  const double s = std::max(0.0, std::min(severity, 1.0));
  if (s < 0.02) return;
  // Bounded wound list: drop the oldest once full, keeping the most severe.
  if (static_cast<int>(wounds_.size()) >= kMaxWounds) {
    auto oldest = wounds_.begin();
    for (auto it = wounds_.begin(); it != wounds_.end(); ++it) {
      if (it->age > oldest->age) oldest = it;
    }
    if (oldest->severity >= s) return;  // new wound is less severe than the oldest
    wounds_.erase(oldest);
  }
  wounds_.push_back(Wound{s, 0.0, 0, source});
}

void Physiology::updateExposure(double dose, double dt) {
  if (dt <= 0.0) return;
  // Disease-vector exposure accumulates in hazard zones, decays elsewhere.
  exposure_ += dose * dt;
  exposure_ -= 0.0003 * dt;
  exposure_ = std::max(0.0, std::min(exposure_, 1.0));
  // Chronic exposure to a severe, sufficiently old wound can seed an infection.
  if (exposure_ >= kExposureThreshold) {
    for (Wound& w : wounds_) {
      if (w.infection <= 0.0 && w.age >= kMinimumInfectableAge &&
          w.severity > kMinimumInfectableSeverity) {
        w.infection = 0.2;
      }
    }
  }
}

double Physiology::woundPain() const {
  double p = 0.0;
  for (const Wound& w : wounds_) p += w.severity * 4.0;
  return std::min(p, 15.0);
}

double Physiology::totalInfection() const {
  double t = 0.0;
  for (const Wound& w : wounds_) t += w.infection;
  return t;
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
  w.f64(immunity_);
  w.f64(exposure_);
  w.u32(static_cast<uint32_t>(wounds_.size()));
  for (const Wound& wo : wounds_) wo.serialize(w);
  w.u8(waterCarried_);
  w.u8(waterCapacity_);
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
  if (!r.f64(immunity_) || !r.f64(exposure_)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  if (n > static_cast<uint32_t>(kMaxWounds)) return false;
  wounds_.clear();
  wounds_.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    Wound wo;
    if (!wo.deserialize(r)) return false;
    wounds_.push_back(wo);
  }
  if (!r.u8(waterCarried_) || !r.u8(waterCapacity_)) return false;
  if (waterCarried_ > waterCapacity_) waterCarried_ = waterCapacity_;
  clamp();
  return true;
}

} // namespace eidolon
