#ifndef EIDOLON_METACOGNITION_HPP
#define EIDOLON_METACOGNITION_HPP

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <deque>

#include "core/serialize.hpp"
#include "body/physiology.hpp"
#include "mind/self_model.hpp"
#include "mind/world_predictor.hpp"

namespace eidolon {

// Metacognition system: monitors own cognitive processes, detects prediction failures,
// and triggers reflection

struct PredictionRecord {
  uint64_t tick = 0;
  std::string context;          // what was being predicted
  std::array<float, 43> predicted_features;
  std::array<float, 43> actual_features;
  float mse = 0.0f;
  bool was_surprising = false;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct ReflectionEvent {
  uint64_t tick = 0;
  std::string trigger;          // what triggered reflection
  float prediction_error = 0.0f;
  std::string insight;          // what was learned
  std::vector<std::string> updated_beliefs; // which beliefs changed
  float confidence_change = 0.0f; // how much confidence changed
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class MetacognitionSystem {
public:
  MetacognitionSystem() = default;
  explicit MetacognitionSystem(uint64_t seed);
  
  // Record a prediction and its outcome
  void record_prediction(const std::string& context,
                         const std::array<float, 43>& predicted,
                         const std::array<float, 43>& actual,
                         uint64_t tick);
  
  // Update metacognitive state based on recent prediction errors
  void update(const SelfModel& self, const WorldPredictor& predictor,
              const Physiology& body, uint64_t tick);
  
  // Check if reflection should be triggered
  bool should_reflect() const;
  
  // Trigger reflection and return insights
  std::vector<std::string> trigger_reflection(const std::string& context, float error);
  
  // Get metacognitive state for self-model
  float get_uncertainty() const { return uncertainty_; }
  float get_confidence() const { return confidence_; }
  uint32_t get_prediction_errors() const { return prediction_errors_; }
  uint32_t get_reflection_count() const { return reflection_count_; }
  
  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  std::deque<PredictionRecord> prediction_history_;
  std::vector<ReflectionEvent> reflection_history_;
  static constexpr size_t kMaxHistory = 1000;
  
  float uncertainty_ = 0.5f;        // overall uncertainty about self-model
  float confidence_ = 0.5f;         // confidence in own predictions
  uint32_t prediction_errors_ = 0;  // count of failed self-predictions
  uint32_t reflection_count_ = 0;   // number of reflection events triggered
  
  float surprise_threshold_ = 0.5f; // MSE threshold for surprise
  float confidence_decay_ = 0.001f; // confidence decay per tick
  float uncertainty_growth_ = 0.0005f; // uncertainty growth per tick
  
  // Detect patterns in prediction errors
  void analyze_error_patterns();
  
  // Update confidence based on recent performance
  void update_confidence();
};

} // namespace eidolon

#endif // EIDOLON_METACOGNITION_HPP