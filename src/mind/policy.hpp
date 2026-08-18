// Policy: linear contextual bandit over agentic actions. Each action has a linear
// preference over the state features; softmax with a temperature (from impulsivity and
// uncertainty) turns preferences into a distribution. After each action, the chosen
// action's weights move toward the advantage (reward + gamma*V(s') - V(s)).
#pragma once

#include <cstdint>
#include <vector>

#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "mind/learner.hpp"

namespace eidolon {

enum class PolicyAction : uint8_t {
  Forage = 0,
  Drink = 1,
  Rest = 2,
  Wander = 3,
  Observe = 4,
};

class Policy : public Learner {
public:
  static constexpr int kActions = 5;

  Policy() = default;
  Policy(int nFeatures);

  int features() const { return nFeatures_; }

  void reset(Rng& r, float scale);

  // Load a teacher-baked "wisdom prior": bandit weights from a linear-softmax fit over
  // labeled experience (magic "EPRP", version 1, nFeatures, kActions, then
  // kActions*(nFeatures+1) f32 row-major, bias last). Online updates continue on top, so
  // this only changes the initialization.
  bool loadPrior(const std::string& path);

  // Preference score for one action (deterministic; no RNG).
  float score(PolicyAction a, const float* feats) const;

  // Softmax sample over actions given temperature (>= 0); returns a PolicyAction.
  PolicyAction choose(const float* feats, float temperature, Rng& r, float* scores);

  // Reinforce the chosen action by `advantage` (scaled by `lr`).
  void update(PolicyAction a, const float* feats, float advantage, float lr);

  const LearnerMetrics& metrics() const override { return metrics_; }
  void serialize(BinaryWriter& w) const override;
  bool deserialize(BinaryReader& r) override;

private:
  int nFeatures_ = 0;
  std::vector<float> w_; // kActions * (nFeatures + 1); last column is the bias
  float lr_ = 0.05f;
  LearnerMetrics metrics_;
};

} // namespace eidolon