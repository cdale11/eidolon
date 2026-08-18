// Learned attention (DESIGN §7): a salience score per perception channel. Channels that
// are active when good outcomes occur are upweighted; the top-k (k <= 8) pass into
// cognition, the rest are attenuated. Drive state and stress bias the effective salience
// in the engine (hungry -> food cues upweighted; high stress -> narrow k=2).
#pragma once

#include <cstdint>
#include <vector>

#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "mind/learner.hpp"

namespace eidolon {

class Attention : public Learner {
public:
  static constexpr int kChannels = 12; // Perception::kFeatures
  static constexpr int kTopK = 8;      // DESIGN §7: k <= 8
  static constexpr int kStressK = 2;   // stress narrows attention

  Attention() = default;

  // Initialize salience to neutral (all channels = 1.0) and zero metrics.
  void reset();

  // Distribute salience: top-k channels pass through unchanged, the rest are attenuated
  // (x0.25). `in`/`out` are kChannels floats.
  void attend(const float* in, int k, float* out);

  // Outcome-driven salience update. `percept` is the (pre-attention) perception channel
  // vector. Reward upweights channels that were informative for the outcome.
  void update(const float* percept, float reward, float lr);

  float salience(int ch) const { return salience_[static_cast<size_t>(ch)]; }
  const std::vector<float>& saliences() const { return salience_; }

  const LearnerMetrics& metrics() const override { return metrics_; }
  void serialize(BinaryWriter& w) const override;
  bool deserialize(BinaryReader& r) override;

private:
  std::vector<float> salience_; // per channel, ~[0.2, 4]
  float lr_ = 0.02f;
  LearnerMetrics metrics_;
};

} // namespace eidolon