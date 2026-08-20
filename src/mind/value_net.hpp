// ValueNet: TD(0) value estimator over the compact state features. Predicts V(s); the
// temporal-difference error (RPE) is the reward-prediction-error used everywhere else.
#pragma once

#include <cstdint>

#include "core/rng.hpp"
#include "mind/learner.hpp"
#include "mind/mlp.hpp"

namespace eidolon {

class ValueNet : public Learner {
public:
  static constexpr int kHidden = 32;

  ValueNet() = default;
  explicit ValueNet(int nFeatures);

  int features() const { return nFeatures_; }
  float gamma() const { return gamma_; }

  // Seed the network weights (small) and zero the metrics.
  void reset(Rng& r);

  // Predict V(s). `hidden` is caller scratch (>= kHidden floats).
  float predict(const float* feats, float* hidden);
  // Const overload (read-only inference; same output).
  float predict(const float* feats, float* hidden) const;

  // TD update: rpe = r + gamma*V(s') - V(s) is supplied; semi-gradient step on V(s).
  // Returns the absolute TD error used as the reward-prediction-error.
  float update(const float* feats, float rpe, float lr);

  const LearnerMetrics& metrics() const override { return metrics_; }
  void serialize(BinaryWriter& w) const override;
  bool deserialize(BinaryReader& r) override;

private:
  int nFeatures_ = 0;
  Mlp net_; // in x kHidden x 1, linear output
  float gamma_ = 0.9f;
  LearnerMetrics metrics_;
};

} // namespace eidolon