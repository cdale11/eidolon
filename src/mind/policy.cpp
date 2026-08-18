#include "mind/policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace eidolon {

Policy::Policy(int nFeatures)
    : nFeatures_(nFeatures), w_(static_cast<size_t>(kActions) * (nFeatures + 1)) {}

void Policy::reset(Rng& r, float scale) {
  for (float& v : w_) v = static_cast<float>(r.range(-scale, scale));
}

bool Policy::loadPrior(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  char magic[4];
  uint32_t version = 0, nf = 0, na = 0;
  const bool hdr = std::fread(magic, 1, 4, f) == 4 &&
                   std::memcmp(magic, "EPRP", 4) == 0 &&
                   std::fread(&version, sizeof(version), 1, f) == 1 && version == 1 &&
                   std::fread(&nf, sizeof(nf), 1, f) == 1 &&
                   std::fread(&na, sizeof(na), 1, f) == 1;
  if (!hdr || nf != static_cast<uint32_t>(nFeatures_) ||
      na != static_cast<uint32_t>(kActions)) {
    std::fclose(f);
    return false;
  }
  std::vector<float> w(static_cast<size_t>(kActions) * (nFeatures_ + 1));
  if (std::fread(w.data(), sizeof(float), w.size(), f) != w.size()) {
    std::fclose(f);
    return false;
  }
  std::fclose(f);
  w_ = std::move(w);
  metrics_ = LearnerMetrics{};
  return true;
}

float Policy::score(PolicyAction a, const float* feats) const {
  const float* row = &w_[static_cast<size_t>(a) * (nFeatures_ + 1)];
  float s = row[nFeatures_];
  for (int i = 0; i < nFeatures_; ++i) s += row[i] * feats[i];
  return s;
}

PolicyAction Policy::choose(const float* feats, float temperature, Rng& r, float* scores) {
  ++metrics_.inferences;
  float maxS = -1e30f;
  for (int a = 0; a < kActions; ++a) {
    scores[a] = score(static_cast<PolicyAction>(a), feats);
    maxS = std::max(maxS, scores[a]);
  }
  float sum = 0.0f;
  const float invT = 1.0f / std::max(0.05f, temperature);
  for (int a = 0; a < kActions; ++a) {
    scores[a] = std::exp((scores[a] - maxS) * invT);
    sum += scores[a];
  }
  double roll = r.unit() * sum;
  for (int a = 0; a < kActions; ++a) {
    roll -= scores[a];
    if (roll <= 0.0) return static_cast<PolicyAction>(a);
  }
  return PolicyAction::Observe;
}

void Policy::update(PolicyAction a, const float* feats, float advantage, float lr) {
  ++metrics_.updates;
  float* row = &w_[static_cast<size_t>(a) * (nFeatures_ + 1)];
  const float step = lr * advantage;
  for (int i = 0; i < nFeatures_; ++i) row[i] += step * feats[i];
  row[nFeatures_] += step;
}

void Policy::serialize(BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(nFeatures_));
  w.u32(static_cast<uint32_t>(w_.size()));
  for (float v : w_) w.f32(v);
  w.f32(lr_);
  w.u64(metrics_.inferences);
  w.u64(metrics_.updates);
}

bool Policy::deserialize(BinaryReader& r) {
  uint32_t n, nw;
  if (!r.u32(n) || !r.u32(nw)) return false;
  if (nw != static_cast<uint32_t>(kActions) * (n + 1)) return false;
  nFeatures_ = static_cast<int>(n);
  w_.resize(nw);
  for (float& v : w_) {
    if (!r.f32(v)) return false;
  }
  if (!r.f32(lr_)) return false;
  return r.u64(metrics_.inferences) && r.u64(metrics_.updates);
}

} // namespace eidolon