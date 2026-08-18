// Neural state (DESIGN §6): continuous neuromodulators that gate learning, attention and
// decision temperature. Couplings are explicit and measurable:
//   - high stress  -> narrower attention (k=2), faster threat learning
//   - high arousal -> fine ticks (engine), broader exploration
//   - negative valence -> stronger episodic encoding (engine importance boost)
//   - low uncertainty -> lower exploration temperature
//   - predictionError spikes -> surprise-gated learning rates (applied by LearnSystem)
#pragma once

#include <cstdint>

#include "core/serialize.hpp"

namespace eidolon {

struct Neuromod {
  float arousal = 0.3f;
  float valence = 0.0f;
  float stress = 0.0f;      // 0..100 cortisol-like
  float reward = 0.0f;      // latest reward signal
  float threat = 0.0f;      // latest ThreatNet estimate (0..1)
  float curiosity = 0.0f;   // EMA of novelty
  float novelty = 0.0f;     // surprise at unfamiliar percepts
  float uncertainty = 0.5f; // model uncertainty 0..1 (drives exploration)
  float predictionError = 0.0f; // |RPE| + novelty (spike gates learning)

  // Advance the modulator layer after a tick's reward/outcome.
  void update(float reward, float rpe, float noveltyIn, float threatIn, float stressReactivity);

  // Temperament priors modulate learning/decision quantities (latent-driven).
  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

} // namespace eidolon