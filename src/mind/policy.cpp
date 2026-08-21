#include "mind/policy.hpp"

#include "core/detmath.hpp"

#include <algorithm>
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
  // Accepted .eprp schema versions. The (nFeatures, nActions) tuple in the header is
  // checked against the live policy's tuple below, so accepting multiple versions
  // can't silently let a stale prior through. v1 is retained for backward-compat with
  // any priors already on disk that happen to match the current tuple by coincidence;
  // the Python writer (fit_prior.py::PRIOR_VERSION) and this list must be bumped
  // together whenever the feature vector or action set changes.
  constexpr uint32_t kAcceptedPriorVersions[] = {1, 2};
  const bool hdr = std::fread(magic, 1, 4, f) == 4 &&
                   std::memcmp(magic, "EPRP", 4) == 0 &&
                   std::fread(&version, sizeof(version), 1, f) == 1 &&
                   std::fread(&nf, sizeof(nf), 1, f) == 1 &&
                   std::fread(&na, sizeof(na), 1, f) == 1;
  if (!hdr) {
    std::fclose(f);
    return false;
  }
  bool versionOk = false;
  for (uint32_t v : kAcceptedPriorVersions) {
    if (version == v) { versionOk = true; break; }
  }
  if (!versionOk || nf != static_cast<uint32_t>(nFeatures_) ||
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
    scores[a] = detmath::expf((scores[a] - maxS) * invT);
    sum += scores[a];
  }
  double roll = r.unit() * sum;
  for (int a = 0; a < kActions; ++a) {
    roll -= scores[a];
    if (roll <= 0.0) return static_cast<PolicyAction>(a);
  }
  return PolicyAction::Observe;
}

PolicyAction Policy::choose_with_habit(const float* feats, float temperature, Rng& r, float* scores,
                                       const float* habit_strength, float habit_weight) const {
  ++metrics_.inferences;
  float maxS = -1e30f;
  for (int a = 0; a < kActions; ++a) {
    // Base score + habit boost
    scores[a] = score(static_cast<PolicyAction>(a), feats);
    if (habit_strength && habit_weight > 0.0f) {
      scores[a] += habit_weight * habit_strength[a];
    }
    maxS = std::max(maxS, scores[a]);
  }
  float sum = 0.0f;
  const float invT = 1.0f / std::max(0.05f, temperature);
  for (int a = 0; a < kActions; ++a) {
    scores[a] = detmath::expf((scores[a] - maxS) * invT);
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

bool Policy::writePrior(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t version = 1, nf = static_cast<uint32_t>(nFeatures_),
                 na = static_cast<uint32_t>(kActions);
  bool ok = std::fwrite("EPRP", 1, 4, f) == 4 &&
            std::fwrite(&version, sizeof(version), 1, f) == 1 &&
            std::fwrite(&nf, sizeof(nf), 1, f) == 1 &&
            std::fwrite(&na, sizeof(na), 1, f) == 1 &&
            std::fwrite(w_.data(), sizeof(float), w_.size(), f) == w_.size();
  std::fclose(f);
  return ok;
}

} // namespace eidolon