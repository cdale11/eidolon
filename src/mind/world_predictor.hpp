#ifndef EIDOLON_WORLD_PREDICTOR_HPP
#define EIDOLON_WORLD_PREDICTOR_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <optional>

#include "core/serialize.hpp"
#include "mind/mlp.hpp"
#include "mind/learn.hpp"

namespace eidolon {

// WorldPredictor: one-step transition model (MLP) predicting next state features
// given current features and action. Also outputs confidence/uncertainty.
class WorldPredictor {
public:
  // Feature dimension is the canonical learner state vector (LearnSystem::kFeatures),
  // plus a one-hot over the planning primitive actions. Planning only considers the
  // 7 basic primitives (Forage/Drink/Rest/Wander/Observe/Flee + Hold) — the advanced
  // Farm/Cook/Craft/Build/CollectWater/Preserve actions are compound behaviours the
  // planner composes from the primitives, not atomic fantasy steps.
  static constexpr int kFeatureDim = LearnSystem::kFeatures;
  static constexpr int kPlanPrimitives = 7;
  static constexpr int kInputSize = kFeatureDim + kPlanPrimitives;
  static constexpr int kOutputSize = kFeatureDim;
  static constexpr int kHiddenSize = 64;

  WorldPredictor() = default;
  explicit WorldPredictor(class Rng& rng);

  // Predict next features given current features and action
  // Returns {predicted_features, confidence}
  std::pair<std::array<float, kFeatureDim>, float> predict(
      const std::array<float, kFeatureDim>& current_features, uint8_t action,
      class Rng& rng) const;

  // Train on a transition (features, action, next_features)
  // Returns prediction error (MSE)
  float train(const std::array<float, kFeatureDim>& current_features, uint8_t action,
              const std::array<float, kFeatureDim>& next_features,
              class Rng& rng, float learning_rate = 0.001f);

  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  // Use linear model for each output feature (kFeatureDim separate linear models)
  // weights[i][j] = weight from input j to output i
  // bias[i] = bias for output i
  std::vector<std::vector<float>> weights_; // [kFeatureDim][kInputSize]
  std::vector<float> bias_; // [kFeatureDim]
  bool initialized_ = false;

  void init(class Rng& rng);
  std::array<float, kInputSize> buildInput(
      const std::array<float, kFeatureDim>& features, uint8_t action) const;
};

// Planner: forward/beam search over action primitives using WorldPredictor
class Planner {
public:
  struct PlanStep {
    uint8_t action;
    float predicted_value;
    std::array<float, WorldPredictor::kFeatureDim> predicted_features;
  };

  struct Plan {
    std::vector<PlanStep> steps;
    float total_value;
    float confidence;
    bool valid;
  };

  Planner() = default;
  explicit Planner(const WorldPredictor* predictor, class Rng& rng);

  // Beam search planning
  Plan plan(const std::array<float, WorldPredictor::kFeatureDim>& current_features,
            int horizon, int beam_width,
            const std::array<float, WorldPredictor::kFeatureDim>& goal_features,
            class Rng& rng) const;

  // Forward search (greedy)
  Plan planGreedy(const std::array<float, WorldPredictor::kFeatureDim>& current_features,
                  int horizon, const std::array<float, WorldPredictor::kFeatureDim>& goal_features,
                  class Rng& rng) const;

  // Replan on surprise (prediction error > threshold)
  bool shouldReplan(const std::array<float, WorldPredictor::kFeatureDim>& predicted,
                    const std::array<float, WorldPredictor::kFeatureDim>& actual,
                    float surprise_threshold) const;

private:
  const WorldPredictor* predictor_;
  [[maybe_unused]] class Rng* rng_; // reserved: stochastic replan jitters
  [[maybe_unused]] float surprise_threshold_ = 0.5f; // replan() takes it by argument

  float computeValue(const std::array<float, WorldPredictor::kFeatureDim>& features,
                     const std::array<float, WorldPredictor::kFeatureDim>& goal) const;
};

} // namespace eidolon

#endif // EIDOLON_WORLD_PREDICTOR_HPP