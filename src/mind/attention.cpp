#include "mind/attention.hpp"

#include <algorithm>
#include <cmath>

namespace eidolon {

void Attention::reset() {
  salience_.assign(kChannels, 1.0f);
  metrics_ = LearnerMetrics{};
}

void Attention::attend(const float* in, int k, float* out) {
  if (salience_.size() != kChannels) {
    for (int i = 0; i < kChannels; ++i) out[i] = in[i];
    return;
  }
  // Index the k most salient channels.
  int order[kChannels];
  for (int i = 0; i < kChannels; ++i) order[i] = i;
  std::sort(order, order + kChannels, [this](int a, int b) {
    const float sa = salience_[static_cast<size_t>(a)];
    const float sb = salience_[static_cast<size_t>(b)];
    if (sa != sb) return sa > sb;
    return a < b; // Deterministic tie-break: libstdc++ and libc++ permute equal keys differently.
  });
  const int keep = std::max(1, std::min(k, kChannels));
  for (int i = 0; i < kChannels; ++i) {
    const int ch = order[i];
    out[ch] = in[ch] * (i < keep ? 1.0f : 0.25f);
  }
  ++metrics_.inferences;
}

void Attention::update(const float* percept, float reward, float lr) {
  if (salience_.empty()) return;
  const float sign = reward > 0.0f ? reward : 0.0f;
  for (int i = 0; i < kChannels; ++i) {
    float& s = salience_[static_cast<size_t>(i)];
    const float target = 1.0f + sign * std::fabs(percept[i]);
    s += lr * (target - s);
    s = std::max(0.2f, std::min(4.0f, s));
  }
  ++metrics_.updates;
}

void Attention::serialize(BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(salience_.size()));
  for (float v : salience_) w.f32(v);
  w.f32(lr_);
  w.u64(metrics_.inferences);
  w.u64(metrics_.updates);
}

bool Attention::deserialize(BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n) || n != kChannels) return false;
  salience_.resize(n);
  for (float& v : salience_) {
    if (!r.f32(v)) return false;
  }
  if (!r.f32(lr_)) return false;
  return r.u64(metrics_.inferences) && r.u64(metrics_.updates);
}

} // namespace eidolon