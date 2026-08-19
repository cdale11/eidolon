#include "mind/metacognition.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace eidolon {

void PredictionRecord::serialize(struct BinaryWriter& w) const {
  w.u64(tick);
  w.str(context);
  for (int i = 0; i < 43; ++i) w.f32(predicted_features[i]);
  for (int i = 0; i < 43; ++i) w.f32(actual_features[i]);
  w.f32(mse);
  w.u8(was_surprising ? 1 : 0);
}

bool PredictionRecord::deserialize(struct BinaryReader& r) {
  if (!r.u64(tick) || !r.str(context)) return false;
  for (int i = 0; i < 43; ++i) if (!r.f32(predicted_features[i])) return false;
  for (int i = 0; i < 43; ++i) if (!r.f32(actual_features[i])) return false;
  if (!r.f32(mse)) return false;
  uint8_t s;
  if (!r.u8(s)) return false;
  was_surprising = s != 0;
  return true;
}

void ReflectionEvent::serialize(struct BinaryWriter& w) const {
  w.u64(tick);
  w.str(trigger);
  w.f32(prediction_error);
  w.str(insight);
  w.u32(static_cast<uint32_t>(updated_beliefs.size()));
  for (const auto& b : updated_beliefs) w.str(b);
  w.f32(confidence_change);
}

bool ReflectionEvent::deserialize(struct BinaryReader& r) {
  if (!r.u64(tick) || !r.str(trigger) || !r.f32(prediction_error) || !r.str(insight)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  updated_beliefs.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.str(updated_beliefs[i])) return false;
  }
  if (!r.f32(confidence_change)) return false;
  return true;
}

MetacognitionSystem::MetacognitionSystem(uint64_t seed) {
  (void)seed; // for future RNG use
}

void MetacognitionSystem::record_prediction(const std::string& context,
                                            const std::array<float, 43>& predicted,
                                            const std::array<float, 43>& actual,
                                            uint64_t tick) {
  PredictionRecord record;
  record.tick = tick;
  record.context = context;
  record.predicted_features = predicted;
  record.actual_features = actual;
  
  // Compute MSE
  float mse = 0.0f;
  for (int i = 0; i < 43; ++i) {
    float diff = predicted[i] - actual[i];
    mse += diff * diff;
  }
  mse /= 43.0f;
  record.mse = mse;
  record.was_surprising = mse > surprise_threshold_;
  
  if (record.was_surprising) {
    prediction_errors_++;
  }
  
  prediction_history_.push_back(record);
  if (prediction_history_.size() > kMaxHistory) {
    prediction_history_.pop_front();
  }
}

void MetacognitionSystem::update(const SelfModel& self, const WorldPredictor& predictor,
                                 const Physiology& body, uint64_t tick) {
  (void)self;
  (void)predictor;
  (void)body;
  (void)tick;
  
  // Decay confidence over time
  confidence_ = std::max(0.0f, confidence_ - confidence_decay_);
  
  // Grow uncertainty over time
  uncertainty_ = std::min(1.0f, uncertainty_ + uncertainty_growth_);
  
  // Analyze recent prediction errors
  analyze_error_patterns();
  
  // Update confidence based on recent performance
  update_confidence();
}

bool MetacognitionSystem::should_reflect() const {
  // Trigger reflection if:
  // 1. Recent prediction errors exceed threshold
  // 2. Confidence drops below threshold
  // 3. Uncertainty exceeds threshold
  if (prediction_errors_ > 5) return true;
  if (confidence_ < 0.2f) return true;
  if (uncertainty_ > 0.8f) return true;
  return false;
}

std::vector<std::string> MetacognitionSystem::trigger_reflection(const std::string& context, float error) {
  reflection_count_++;
  
  ReflectionEvent event;
  event.tick = 0; // would be set by caller
  event.trigger = context;
  event.prediction_error = error;
  event.insight = "Prediction error detected in " + context + ": " + std::to_string(error);
  event.confidence_change = -error * 0.5f;
  
  reflection_history_.push_back(event);
  if (reflection_history_.size() > 100) reflection_history_.erase(reflection_history_.begin());
  
  // Update metacognitive state
  confidence_ = std::max(0.0f, confidence_ - error * 0.5f);
  uncertainty_ = std::min(1.0f, uncertainty_ + error * 0.1f);
  prediction_errors_ = 0; // reset after reflection
  
  return {"Reflection triggered: " + context + " (error: " + std::to_string(error) + ")"};
}

void MetacognitionSystem::analyze_error_patterns() {
  if (prediction_history_.size() < 10) return;
  
  // Check for systematic errors in specific feature dimensions
  // Simplified: just count recent surprising predictions
  int recent_surprising = 0;
  for (auto it = prediction_history_.rbegin(); it != prediction_history_.rend() && it != prediction_history_.rend() + 20; ++it) {
    if (it->was_surprising) break;
    recent_surprising++;
  }
  (void)recent_surprising; // suppress unused warning
}

void MetacognitionSystem::update_confidence() {
  if (prediction_history_.empty()) return;
  
  float recent_mse = 0.0f;
  int count = 0;
  for (auto it = prediction_history_.rbegin(); it != prediction_history_.rend() && count < 20; ++it, ++count) {
    recent_mse += it->mse;
  }
  if (count > 0) recent_mse /= count;
  
  // Lower MSE = higher confidence
  float target_confidence = std::max(0.0f, 1.0f - recent_mse * 10.0f);
  confidence_ = 0.9f * confidence_ + 0.1f * target_confidence;
}

void MetacognitionSystem::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(prediction_history_.size()));
  for (const auto& p : prediction_history_) p.serialize(w);
  w.u32(static_cast<uint32_t>(reflection_history_.size()));
  for (const auto& r : reflection_history_) r.serialize(w);
  w.f32(uncertainty_);
  w.f32(confidence_);
  w.u32(prediction_errors_);
  w.u32(reflection_count_);
}

bool MetacognitionSystem::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  prediction_history_.clear();
  prediction_history_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!prediction_history_[i].deserialize(r)) return false;
  }
  uint32_t m;
  if (!r.u32(m)) return false;
  reflection_history_.resize(static_cast<size_t>(m));
  for (size_t i = 0; i < static_cast<size_t>(m); ++i) {
    if (!reflection_history_[i].deserialize(r)) return false;
  }
  return r.f32(uncertainty_) && r.f32(confidence_) &&
         r.u32(prediction_errors_) && r.u32(reflection_count_);
}

} // namespace eidolon