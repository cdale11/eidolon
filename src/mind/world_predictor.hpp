#ifndef EIDOLON_WORLD_PREDICTOR_HPP
#define EIDOLON_WORLD_PREDICTOR_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <optional>

#include "core/serialize.hpp"
#include "mind/mlp.hpp"

namespace eidolon {

// WorldPredictor: one-step transition model (MLP) predicting next state features
// given current features and action. Also outputs confidence/uncertainty.
class WorldPredictor {
public:
  static constexpr int kInputSize = 43 + 7;  // 43 features + 7 action one-hot
  static constexpr int kOutputSize = 43;     // predicts next 43 features
  static constexpr int kHiddenSize = 64;

  WorldPredictor() = default;
  explicit WorldPredictor(class Rng& rng);

  // Predict next features given current features and action
  // Returns {predicted_features, confidence}
  std::pair<std::array<float, 43>, float> predict(
      const std::array<float, 43>& current_features, uint8_t action,
      class Rng& rng) const;

  // Train on a transition (features, action, next_features)
  // Returns prediction error (MSE)
  float train(const std::array<float, 43>& current_features, uint8_t action,
              const std::array<float, 43>& next_features,
              class Rng& rng, float learning_rate = 0.001f);

  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  // Use linear model for each output feature (43 separate linear models)
  // weights[i][j] = weight from input j to output i
  // bias[i] = bias for output i
  std::vector<std::vector<float>> weights_; // [43][50]
  std::vector<float> bias_; // [43]
  bool initialized_ = false;

  void init(class Rng& rng);
  std::array<float, kInputSize> buildInput(
      const std::array<float, 43>& features, uint8_t action) const;
};

// Planner: forward/beam search over action primitives using WorldPredictor
class Planner {
public:
  struct PlanStep {
    uint8_t action;
    float predicted_value;
    std::array<float, 43> predicted_features;
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
  Plan plan(const std::array<float, 43>& current_features,
            int horizon, int beam_width,
            const std::array<float, 43>& goal_features,
            class Rng& rng) const;

  // Forward search (greedy)
  Plan planGreedy(const std::array<float, 43>& current_features,
                  int horizon, const std::array<float, 43>& goal_features,
                  class Rng& rng) const;

  // Replan on surprise (prediction error > threshold)
  bool shouldReplan(const std::array<float, 43>& predicted,
                    const std::array<float, 43>& actual,
                    float surprise_threshold) const;

private:
  const WorldPredictor* predictor_;
  class Rng* rng_;
  float surprise_threshold_ = 0.5f;

  float computeValue(const std::array<float, 43>& features,
                     const std::array<float, 43>& goal) const;
};

} // namespace eidolon

#endif // EIDOLON_WORLD_PREDICTOR_HPP