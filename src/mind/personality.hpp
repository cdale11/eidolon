// Personality latent vector (DESIGN §12): 16-dim, initialized once from the master seed
// as temperament priors, then slowly drifted by life statistics (reward history, threat
// history, novelty history, success rates). No textual personality anywhere — this is
// data that modulates learning rates, drive weights and decision temperature.
#pragma once

#include <cstdint>

#include "core/rng.hpp"
#include "core/serialize.hpp"

namespace eidolon {

// Life statistics used to drift the latent vector (EMA over the organism's history).
struct LifeStats {
  float avgReward = 0.0f;
  float rewardVar = 0.0f;   // reward variance -> impulsivity
  float avgNovelty = 0.0f;
  float threatRate = 0.0f;  // fraction of ticks with aversive events
  float avgValence = 0.0f;
  float forageRate = 0.0f;  // forage success EMA
  float drinkRate = 0.0f;   // drink success EMA
  float restRate = 0.0f;    // rest/sleep recovery EMA
  float successRate = 0.0f; // overall action success EMA
  float avgPain = 0.0f;
};

class PersonalityLatent {
public:
  static constexpr int kDims = 16;
  // Dimension roles (indices used by LearnSystem).
  static constexpr int kRewardSensitivity = 0;
  static constexpr int kThreatSensitivity = 1;
  static constexpr int kNoveltySensitivity = 2;
  static constexpr int kSocialSensitivity = 3;   // reserved (no social in Phase 3)
  static constexpr int kImpulsivity = 4;
  static constexpr int kPersistence = 5;
  static constexpr int kAttachment = 6;          // reserved
  static constexpr int kStressReactivity = 7;
  static constexpr int kFoodAffinity = 8;
  static constexpr int kWaterAffinity = 9;
  static constexpr int kRestAffinity = 10;
  static constexpr int kExplorationAffinity = 11;
  static constexpr int kResidueValence = 12;
  static constexpr int kResidueReward = 13;
  static constexpr int kResidueNovelty = 14;
  static constexpr int kResidueThreat = 15;

  void init(Rng& r);

  // Slow drift from life statistics (called ~once per sim-day).
  void drift(const LifeStats& s, float lr);

  // Sensitivity in [0.5, 1.5] used to scale learning rates / temperature.
  float sensitivity(int dim) const { return 1.0f + 0.3f * value(dim); }

  float value(int dim) const { return v_[static_cast<size_t>(dim)]; }
  const float* data() const { return v_; }
  float* data() { return v_; }
  float& operator[](int dim) { return v_[static_cast<size_t>(dim)]; }
  const float& operator[](int dim) const { return v_[static_cast<size_t>(dim)]; }

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  float v_[kDims] = {};
};

// Relative drive strengths that evolve slowly (via the latent affinities). They scale the
// drive features the policy sees, so personality shapes behaviour over weeks.
struct DriveWeights {
  float hunger = 1.0f;
  float thirst = 1.0f;
  float rest = 1.0f;
  float energy = 1.0f;
  float curiosity = 1.0f;

  // Derive from the latent affinities (clamped to [0.6, 1.4]).
  void derive(const PersonalityLatent& p);

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

} // namespace eidolon