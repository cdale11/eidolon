#include "mind/value_net.hpp"

#include <cmath>

namespace eidolon {

ValueNet::ValueNet(int nFeatures) : nFeatures_(nFeatures), net_(nFeatures, kHidden, 1, OutAct::Linear) {}

void ValueNet::reset(Rng& r) {
  net_.reset(r, 0.3f);
  metrics_ = LearnerMetrics{};
}

float ValueNet::predict(const float* feats, float* hidden) {
  float out = 0.0f;
  net_.forward(feats, hidden, &out);
  ++metrics_.inferences;
  return out;
}

float ValueNet::predict(const float* feats, float* hidden) const {
  float out = 0.0f;
  net_.forward(feats, hidden, &out);
  return out;
}

float ValueNet::update(const float* feats, float rpe, float lr) {
  // Forward once with the current weights so the hidden activations match the gradient
  // (the caller must not pass stale hidden activations from an older forward pass).
  float hidden[kHidden];
  float v = 0.0f;
  net_.forward(feats, hidden, &v);
  net_.backprop(feats, hidden, rpe, lr);
  ++metrics_.updates;
  return std::abs(rpe);
}

void ValueNet::serialize(BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(nFeatures_));
  net_.serialize(w);
  w.f32(gamma_);
  w.u64(metrics_.inferences);
  w.u64(metrics_.updates);
}

bool ValueNet::deserialize(BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  nFeatures_ = static_cast<int>(n);
  if (!net_.deserialize(r)) return false;
  if (!r.f32(gamma_)) return false;
  return r.u64(metrics_.inferences) && r.u64(metrics_.updates);
}

} // namespace eidolon