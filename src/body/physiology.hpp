// Simulated body: compact physiological state with homeostatic dynamics.
// Phase 1 covers energy, hunger, thirst, fatigue, sleep pressure, temperature, health.
// Phase 5 adds wounds, infection and an immune system (hazard/injury model).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/serialize.hpp"
#include "world/cellular_automata.hpp"

namespace eidolon {

enum class Activity : uint8_t {
  Sleep = 0,
  Rest = 1,
  Observe = 2,
  Move = 3,
  Forage = 4,
  Drink = 5,
};

// A localized injury (predator bite, fall). `severity` is the 0..1 damage share that
// contributes chronic pain and gates healing; `infection` is the 0..1 pathogen load the
// immune system fights. `age` is sim-seconds since the injury. `source`: 0=predator,
// 1=fall, 2=exposure/disease.
struct Wound {
  double severity = 0.0;
  double infection = 0.0;
  int64_t age = 0;
  uint8_t source = 0;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

class Physiology {
public:
  // Bounds.
  static constexpr double kMax = 100.0;
  static constexpr double kBodyTempC = 36.6;
  // Source-Drink water amount (one sip from a water source / waterskin).
  static constexpr double kSourceDrinkWater = 8.0;
  // Innate waterskin capacity (sips); a fresh organism starts with this.
  static constexpr uint8_t kInnateWaterskinCapacity = 5;

  void reset();

  // Advance physiology by dt simulated seconds. `ambientTempC` drives thermoregulation;
  // `hazardDose` is the 0..1 disease-vector exposure accumulated this tick (swamp tiles,
  // deep-water proximity while wet). `nearbyInfected` (optional) is the count of infected
  // CA cells in the organism's vicinity; infection dynamics scale with this.
  void update(double dt, double ambientTempC, Activity act, double hazardDose = 0.0,
              int nearbyInfected = 0);

  // Consume food/water (units of energy / thirst reduction). Returns nothing; clamps.
  void eat(double food) {
    hunger_ = std::max(0.0, hunger_ - food * 1.2);
    energy_ = std::min(kMax, energy_ + food * 2.2);
  }
  void drink(double water) {
    thirst_ = std::max(0.0, thirst_ - water * 0.7);
  }

  // Carried water (waterskin). Capacity 0 = no waterskin owned. Each "sip" is one full
  // source-Drink equivalent (8 water -> 5.6 thirst relief). Set/owned via crafting once
  // the crafting system is wired into the engine; for now every fresh organism starts
  // with a basic waterskin of capacity kInnateWaterskinCapacity (see Engine).
  uint8_t waterCapacity() const { return waterCapacity_; }
  uint8_t waterCarried() const { return waterCarried_; }
  void setWaterCapacity(uint8_t cap) { waterCapacity_ = cap; if (waterCarried_ > cap) waterCarried_ = cap; }
  void refillWater() { if (waterCapacity_ > 0) waterCarried_ = waterCapacity_; }
  // Drink one sip from the waterskin. Returns true if a sip was consumed.
  bool drinkFromSkin() {
    if (waterCarried_ == 0 || waterCapacity_ == 0) return false;
    thirst_ = std::max(0.0, thirst_ - kSourceDrinkWater * 0.7);
    --waterCarried_;
    return true;
  }

  // Take predator/environment damage: health loss + pain (both clamped). Feeds the
  // threat system through the aversive tick in the engine. `painful` is false for
  // environmental scrapes (falls) so small hazards don't flood the pain channel and
  // trigger a rest spiral; only predator/acute damage spikes pain.
  void takeDamage(double dmg, bool painful = true) {
    health_ = std::max(0.0, health_ - dmg);
    if (painful) pain_ = std::min(kMax, pain_ + dmg);
  }

  // Add a wound (severity 0..1). Wound count is bounded (oldest/least severe dropped).
  void addWound(double severity, uint8_t source);

  // Deterministic disease exposure: accumulate `dose` (0..1 per tick); a severe, old
  // enough wound turns infected once chronic exposure passes a threshold.
  void updateExposure(double dose, double dt);

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
  // Acute pain (decays) + chronic wound pain.
  double pain() const { return std::min(kMax, pain_ + woundPain()); }
  double immunity() const { return immunity_; }
  double exposure() const { return exposure_; }
  // Chronic pain contribution from open wounds (0..~kMax/4).
  double woundPain() const;
  double totalInfection() const;
  int infectedWounds() const {
    int n = 0;
    for (const Wound& w : wounds_)
      if (w.infection > 0.0) ++n;
    return n;
  }
  bool sick() const { return totalInfection() >= 0.5; }
  const std::vector<Wound>& wounds() const { return wounds_; }
  int woundCount() const { return static_cast<int>(wounds_.size()); }

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
  // Phase 5 hazards.
  double immunity_ = 0.5;  // 0..1 immune strength (nutrition/rest), fights infection
  double exposure_ = 0.0;  // 0..1 disease-vector exposure accumulator
  std::vector<Wound> wounds_;
  // Carried water (waterskin). capacity=0 means no waterskin; carried is in [0,capacity].
  uint8_t waterCarried_ = 0;
  uint8_t waterCapacity_ = 0;
};

} // namespace eidolon
