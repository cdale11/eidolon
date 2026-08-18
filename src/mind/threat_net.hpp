// ThreatNet: learned probability of threat in a state (0..1). Positive (aversive)
// outcomes sensitize; repeated safe outcomes extinguish. Learning accelerates under
// stress (neuromodulator coupling). The engine vetoes exploration when threat is high.
#pragma once

#include <cstdint>

#include "core/rng.hpp"
#include "mind/learner.hpp"
#include "mind/mlp.hpp"

namespace eidolon {

class ThreatNet : public Learner {
public:
  static constexpr int kHidden = 24;

  ThreatNet() = default;
  explicit ThreatNet(int nFeatures);

  int features() const { return nFeatures_; }

  // Seed the network weights (small) and zero the metrics.
  void reset(Rng& r);

  // Predict p(threat|s) in [0,1]. `hidden` is caller scratch (>= kHidden floats).
  float predict(const float* feats, float* hidden);

  // Binary update toward `target` (1.0 = aversive event, 0.0 = safe event). `lr` may
  // embed stress/threatSensitivity couplings. Returns the |p - target| error.
  float update(const float* feats, float target, float lr);

  const LearnerMetrics& metrics() const override { return metrics_; }
  void serialize(BinaryWriter& w) const override;
  bool deserialize(BinaryReader& r) override;

private:
  int nFeatures_ = 0;
  Mlp net_; // in x kHidden x 1, sigmoid output
  LearnerMetrics metrics_;
};

} // namespace eidolon