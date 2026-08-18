#include "mind/threat_net.hpp"

#include <cmath>

namespace eidolon {

ThreatNet::ThreatNet(int nFeatures) : nFeatures_(nFeatures), net_(nFeatures, kHidden, 1, OutAct::Sigmoid) {}

void ThreatNet::reset(Rng& r) {
  net_.reset(r, 0.3f);
  metrics_ = LearnerMetrics{};
}

float ThreatNet::predict(const float* feats, float* hidden) {
  float out = 0.0f;
  net_.forward(feats, hidden, &out);
  ++metrics_.inferences;
  return out;
}

float ThreatNet::update(const float* feats, float target, float lr) {
  float hidden[kHidden];
  float p = 0.0f;
  net_.forward(feats, hidden, &p);
  net_.backprop(feats, hidden, target - p, lr);
  ++metrics_.updates;
  return std::fabs(p - target);
}

void ThreatNet::serialize(BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(nFeatures_));
  net_.serialize(w);
  w.u64(metrics_.inferences);
  w.u64(metrics_.updates);
}

bool ThreatNet::deserialize(BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  nFeatures_ = static_cast<int>(n);
  if (!net_.deserialize(r)) return false;
  return r.u64(metrics_.inferences) && r.u64(metrics_.updates);
}

} // namespace eidolon