#include "mind/world_predictor.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace eidolon {

WorldPredictor::WorldPredictor(class Rng& rng) {
  init(rng);
}

void WorldPredictor::init(class Rng& rng) {
  weights_.assign(43, std::vector<float>(kInputSize, 0.0f));
  bias_.assign(43, 0.0f);
  // Initialize with small random weights
  for (int i = 0; i < 43; ++i) {
    for (int j = 0; j < kInputSize; ++j) {
      weights_[i][j] = static_cast<float>(rng.range(-0.01, 0.01));
    }
    bias_[i] = static_cast<float>(rng.range(-0.01, 0.01));
  }
  initialized_ = true;
}

std::array<float, WorldPredictor::kInputSize> WorldPredictor::buildInput(
    const std::array<float, 43>& features, uint8_t action) const {
  std::array<float, kInputSize> input{};
  for (int i = 0; i < 43; ++i) input[i] = features[i];
  // One-hot action encoding (7 actions)
  if (action < 7) input[43 + action] = 1.0f;
  return input;
}

std::pair<std::array<float, 43>, float> WorldPredictor::predict(
    const std::array<float, 43>& current_features, uint8_t action,
    class Rng& /*rng*/) const {
  if (!initialized_) return {{}, 0.0f};

  auto input = buildInput(current_features, action);
  std::array<float, 43> output{};
  float conf = 0.0f;

  for (int i = 0; i < 43; ++i) {
    float sum = bias_[i];
    for (int j = 0; j < kInputSize; ++j) {
      sum += weights_[i][j] * input[j];
    }
    output[i] = sum;
    conf += sum * sum;
  }
  conf = std::min(1.0f, conf / 100.0f);

  return {output, conf};
}

float WorldPredictor::train(const std::array<float, 43>& current_features,
                            uint8_t action, const std::array<float, 43>& next_features,
                            class Rng& /*rng*/, float learning_rate) {
  if (!initialized_) return 0.0f;

  auto input = buildInput(current_features, action);
  float mse = 0.0f;

  for (int i = 0; i < 43; ++i) {
    float sum = bias_[i];
    for (int j = 0; j < kInputSize; ++j) {
      sum += weights_[i][j] * input[j];
    }
    float pred = sum;
    float target = next_features[i];
    float error = pred - target;
    mse += error * error;

    // Gradient descent update
    float grad = error; // d/dw (pred - target)^2 = 2 * (pred - target) * input
    (void)grad;
    for (int j = 0; j < kInputSize; ++j) {
      weights_[i][j] -= learning_rate * 2.0f * error * input[j];
    }
    bias_[i] -= learning_rate * 2.0f * error;
  }
  mse /= 43.0f;
  return mse;
}

void WorldPredictor::serialize(struct BinaryWriter& w) const {
  w.u8(initialized_ ? 1 : 0);
  if (initialized_) {
    w.u32(static_cast<uint32_t>(weights_.size()));
    for (const auto& row : weights_) {
      w.u32(static_cast<uint32_t>(row.size()));
      for (float v : row) w.f32(v);
    }
    w.u32(static_cast<uint32_t>(bias_.size()));
    for (float v : bias_) w.f32(v);
  }
}

bool WorldPredictor::deserialize(struct BinaryReader& r) {
  uint8_t init;
  if (!r.u8(init)) return false;
  initialized_ = init != 0;
  if (initialized_) {
    uint32_t rows, cols;
    if (!r.u32(rows) || rows != 43) return false;
    weights_.resize(43);
    for (int i = 0; i < 43; ++i) {
      if (!r.u32(cols) || cols != kInputSize) return false;
      weights_[i].resize(kInputSize);
      for (int j = 0; j < kInputSize; ++j) {
        if (!r.f32(weights_[i][j])) return false;
      }
    }
    uint32_t bsize;
    if (!r.u32(bsize) || bsize != 43) return false;
    bias_.resize(43);
    for (int i = 0; i < 43; ++i) {
      if (!r.f32(bias_[i])) return false;
    }
  }
  return true;
}

// Planner implementation
Planner::Planner(const WorldPredictor* predictor, class Rng& rng)
    : predictor_(predictor), rng_(&rng) {}

float Planner::computeValue(const std::array<float, 43>& features,
                            const std::array<float, 43>& goal) const {
  // Simple value: negative distance to goal in feature space
  float dist = 0.0f;
  for (int i = 0; i < 43; ++i) {
    float diff = features[i] - goal[i];
    dist += diff * diff;
  }
  return -std::sqrt(dist);
}

Planner::Plan Planner::planGreedy(const std::array<float, 43>& current_features,
                                  int horizon, const std::array<float, 43>& goal_features,
                                  class Rng& rng) const {
  Plan plan;
  plan.steps.reserve(horizon);
  plan.valid = true;

  std::array<float, 43> current = current_features;
  float total_value = 0.0f;
  float total_conf = 0.0f;

  for (int h = 0; h < horizon; ++h) {
    float best_value = -std::numeric_limits<float>::infinity();
    uint8_t best_action = 0;
    std::array<float, 43> best_predicted{};
    float best_conf = 0.0f;

    // Try all 7 actions
    for (uint8_t a = 0; a < 7; ++a) {
      auto [predicted, conf] = predictor_->predict(current, a, rng);
      float val = computeValue(predicted, goal_features);
      if (val > best_value) {
        best_value = val;
        best_action = a;
        best_predicted = predicted;
        best_conf = conf;
      }
    }

    plan.steps.push_back({static_cast<uint8_t>(best_action), best_value, best_predicted});
    total_value += best_value;
    total_conf += best_conf;
    current = best_predicted;
  }

  plan.total_value = total_value;
  plan.confidence = total_conf / horizon;
  return plan;
}

Planner::Plan Planner::plan(const std::array<float, 43>& current_features,
                            int horizon, int beam_width,
                            const std::array<float, 43>& goal_features,
                            class Rng& rng) const {
  struct BeamNode {
    std::array<float, 43> features;
    std::vector<PlanStep> steps;
    float total_value;
    float confidence;
  };

  std::vector<BeamNode> beam;
  beam.push_back({current_features, {}, 0.0f, 1.0f});

  for (int h = 0; h < horizon; ++h) {
    std::vector<BeamNode> next_beam;
    next_beam.reserve(beam.size() * 7);

    for (const auto& node : beam) {
      for (uint8_t a = 0; a < 7; ++a) {
        auto [predicted, conf] = predictor_->predict(node.features, a, rng);
        float val = computeValue(predicted, goal_features);
        BeamNode next;
        next.features = predicted;
        next.steps = node.steps;
        next.steps.push_back({a, val, predicted});
        next.total_value = node.total_value + val;
        next.confidence = node.confidence * conf;
        next_beam.push_back(next);
      }
    }

    // Sort by total_value and keep top beam_width
    std::sort(next_beam.begin(), next_beam.end(),
              [](const BeamNode& a, const BeamNode& b) {
                return a.total_value > b.total_value;
              });
    if ((int)next_beam.size() > beam_width) next_beam.resize(beam_width);
    beam.swap(next_beam);
  }

  Plan best_plan;
  if (!beam.empty()) {
    const auto& best = beam[0];
    best_plan.steps = best.steps;
    best_plan.total_value = best.total_value;
    best_plan.confidence = best.confidence;
    best_plan.valid = true;
  } else {
    best_plan.valid = false;
  }
  return best_plan;
}

bool Planner::shouldReplan(const std::array<float, 43>& predicted,
                           const std::array<float, 43>& actual,
                           float surprise_threshold) const {
  float mse = 0.0f;
  for (int i = 0; i < 43; ++i) {
    float diff = predicted[i] - actual[i];
    mse += diff * diff;
  }
  mse /= 43.0f;
  return mse > surprise_threshold;
}

} // namespace eidolon