#include "mind/neuromod.hpp"

#include <algorithm>
#include <cmath>

namespace eidolon {

void Neuromod::update(float rewardIn, float rpe, float noveltyIn, float threatIn,
                      float stressReactivity) {
  // Arousal: reward/novelty events energize; decays toward a quiet baseline.
  arousal = arousal + 0.05f * (std::min(1.0f, std::fabs(rewardIn) + noveltyIn) - arousal);
  arousal = std::max(0.05f, std::min(1.0f, arousal));

  // Valence: hedonic tone from reward; EMA.
  const float target = std::max(-1.0f, std::min(1.0f, rewardIn));
  valence = valence + 0.1f * (target - valence);

  // Stress: rises with threat/negative events, decays slowly; reactivity is temperamental.
  const float stressIn = 100.0f * threatIn + (rewardIn < -0.3f ? 5.0f : 0.0f) +
                         (noveltyIn > 0.5f ? 2.0f : 0.0f);
  stress = stress + (stressReactivity * 0.02f) * (stressIn - stress);
  stress = std::max(0.0f, std::min(100.0f, stress));

  novelty = noveltyIn;
  curiosity = curiosity + 0.05f * (noveltyIn - curiosity);

  predictionError = std::fabs(rpe) + noveltyIn;
  // Uncertainty tracks how badly the value model is surprised (smoothed).
  const float unc = std::min(1.0f, 0.5f * predictionError);
  uncertainty = uncertainty + 0.05f * (unc - uncertainty);
  uncertainty = std::max(0.05f, std::min(1.0f, uncertainty));

  reward = rewardIn;
  threat = threatIn;
}

void Neuromod::serialize(BinaryWriter& w) const {
  w.f32(arousal);
  w.f32(valence);
  w.f32(stress);
  w.f32(reward);
  w.f32(threat);
  w.f32(curiosity);
  w.f32(novelty);
  w.f32(uncertainty);
  w.f32(predictionError);
}

bool Neuromod::deserialize(BinaryReader& r) {
  return r.f32(arousal) && r.f32(valence) && r.f32(stress) && r.f32(reward) &&
         r.f32(threat) && r.f32(curiosity) && r.f32(novelty) && r.f32(uncertainty) &&
         r.f32(predictionError);
}

} // namespace eidolon