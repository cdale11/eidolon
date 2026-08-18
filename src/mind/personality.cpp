#include "mind/personality.hpp"

#include <algorithm>
#include <cmath>

namespace eidolon {

void PersonalityLatent::init(Rng& r) {
  // Temperament priors: small deterministic variation around a neutral center.
  for (float& v : v_) v = static_cast<float>(r.range(-0.5, 0.5));
}

void PersonalityLatent::drift(const LifeStats& s, float lr) {
  const auto d = [](float cur, float target, float lr) {
    const float v = cur + lr * (target - cur);
    return std::max(-1.0f, std::min(1.0f, v));
  };
  v_[kRewardSensitivity] = d(v_[kRewardSensitivity], s.avgReward, lr);
  v_[kThreatSensitivity] = d(v_[kThreatSensitivity], s.threatRate * 2.0f - 1.0f, lr);
  v_[kNoveltySensitivity] = d(v_[kNoveltySensitivity], s.avgNovelty, lr);
  v_[kImpulsivity] = d(v_[kImpulsivity], std::min(1.0f, s.rewardVar * 4.0f), lr);
  v_[kPersistence] = d(v_[kPersistence], s.forageRate, lr);
  v_[kStressReactivity] = d(v_[kStressReactivity], s.avgPain, lr);
  v_[kFoodAffinity] = d(v_[kFoodAffinity], s.forageRate, lr);
  v_[kWaterAffinity] = d(v_[kWaterAffinity], s.drinkRate, lr);
  v_[kRestAffinity] = d(v_[kRestAffinity], s.restRate, lr);
  v_[kExplorationAffinity] = d(v_[kExplorationAffinity], s.avgNovelty, lr);
  v_[kResidueValence] = d(v_[kResidueValence], s.avgValence, lr);
  v_[kResidueReward] = d(v_[kResidueReward], s.avgReward, lr);
  v_[kResidueNovelty] = d(v_[kResidueNovelty], s.avgNovelty, lr);
  v_[kResidueThreat] = d(v_[kResidueThreat], s.avgPain, lr);
}

void DriveWeights::derive(const PersonalityLatent& p) {
  const auto c = [](float x) { return std::max(0.6f, std::min(1.4f, x)); };
  hunger = c(1.0f + 0.35f * p.value(PersonalityLatent::kFoodAffinity));
  thirst = c(1.0f + 0.35f * p.value(PersonalityLatent::kWaterAffinity));
  rest = c(1.0f + 0.35f * p.value(PersonalityLatent::kRestAffinity));
  energy = 1.0f;
  curiosity = c(1.0f + 0.35f * p.value(PersonalityLatent::kExplorationAffinity));
}

void PersonalityLatent::serialize(BinaryWriter& w) const {
  for (float v : v_) w.f32(v);
}

bool PersonalityLatent::deserialize(BinaryReader& r) {
  for (float& v : v_) {
    if (!r.f32(v)) return false;
  }
  return true;
}

void DriveWeights::serialize(BinaryWriter& w) const {
  w.f32(hunger);
  w.f32(thirst);
  w.f32(rest);
  w.f32(energy);
  w.f32(curiosity);
}

bool DriveWeights::deserialize(BinaryReader& r) {
  return r.f32(hunger) && r.f32(thirst) && r.f32(rest) && r.f32(energy) &&
         r.f32(curiosity);
}

} // namespace eidolon