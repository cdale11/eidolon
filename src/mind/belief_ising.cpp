#include "mind/belief_ising.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

namespace eidolon {

void BeliefSpin::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(state));
  w.f32(external_field);
  w.f32(certainty);
  w.str(description);
}

bool BeliefSpin::deserialize(struct BinaryReader& r) {
  uint8_t s;
  if (!r.u8(s) || !r.f32(external_field) || !r.f32(certainty) || !r.str(description))
    return false;
  state = static_cast<int>(static_cast<int8_t>(s));
  return true;
}

void BeliefCoupling::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(belief_a));
  w.u32(static_cast<uint32_t>(belief_b));
  w.f32(strength);
}

bool BeliefCoupling::deserialize(struct BinaryReader& r) {
  uint32_t a, b;
  if (!r.u32(a) || !r.u32(b) || !r.f32(strength)) return false;
  belief_a = static_cast<size_t>(a);
  belief_b = static_cast<size_t>(b);
  return true;
}

BeliefIsingModel::BeliefIsingModel(size_t n_beliefs) {
  spins_.resize(n_beliefs);
  couplings_.resize(n_beliefs, std::vector<float>(n_beliefs, 0.0f));
}

size_t BeliefIsingModel::add_belief(const std::string& description, int initial_state) {
  size_t idx = spins_.size();
  Spin s;
  s.state = std::clamp(initial_state, -1, 1);
  s.description = description;
  s.external_field = 0.0f;
  s.certainty = 0.5f;
  spins_.push_back(s);
  
  // Resize coupling matrix
  size_t new_n = spins_.size();
  for (auto& row : couplings_) row.resize(couplings_.size(), 0.0f);
  couplings_.push_back(std::vector<float>(new_n, 0.0f));
  
  return idx;
}

void BeliefIsingModel::add_coupling(size_t a, size_t b, float strength) {
  if (a >= spins_.size() || b >= spins_.size()) return;
  couplings_[a][b] = strength;
  couplings_[b][a] = strength;
  coupling_list_.push_back({a, b});
}

void BeliefIsingModel::apply_evidence(size_t idx, float field_strength) {
  if (idx >= spins_.size()) return;
  spins_[idx].external_field += field_strength;
}

void BeliefIsingModel::update(class Rng& rng, float temperature) {
  temperature_ = temperature;
  
  for (size_t i = 0; i < spins_.size(); ++i) {
    // Compute local field
    float h = spins_[i].external_field;
    for (size_t j = 0; j < spins_.size(); ++j) {
      if (i != j && std::abs(couplings_[i][j]) > 1e-6f) {
        h += couplings_[i][j] * spins_[j].state;
      }
    }
    
    // Glauber dynamics: probability of state +1
    float prob_up = 1.0f / (1.0f + std::exp(-2.0f * h / temperature_));
    
    // Deterministic with seeded noise
    float r = static_cast<float>(rng.range(0.0, 1.0));
    int new_state;
    if (spins_[i].state == 1) {
      new_state = (r < prob_up) ? 1 : -1;
    } else if (spins_[i].state == -1) {
      new_state = (r < (1.0f - prob_up)) ? -1 : 1;
    } else {
      new_state = (r < prob_up) ? 1 : -1;
    }
    
    spins_[i].state = new_state;
    
    // Update certainty based on field strength
    (void)std::abs(couplings_[i][0]); // simplified
    spins_[i].certainty = std::min(1.0f, spins_[i].certainty + 0.01f);
  }
}

float BeliefIsingModel::compute_energy() const {
  float energy = 0.0f;
  for (size_t i = 0; i < spins_.size(); ++i) {
    energy -= spins_[i].external_field * spins_[i].state;
    for (size_t j = i + 1; j < spins_.size(); ++j) {
      energy -= couplings_[i][j] * spins_[i].state * spins_[j].state;
    }
  }
  return energy;
}

float BeliefIsingModel::compute_dissonance() const {
  float frustrated = 0.0f;
  float total = 0.0f;
  
  for (const auto& [a, b] : coupling_list_) {
    float coupling = couplings_[a][b];
    if (std::abs(coupling) < 1e-6f) continue;
    
    int s1 = spins_[a].state;
    int s2 = spins_[b].state;
    
    // Frustrated if coupling wants them aligned but they're opposite, or vice versa
    bool frustrated_pair = (coupling > 0 && s1 != s2) || (coupling < 0 && s1 == s2);
    if (frustrated_pair) frustrated += 1.0f;
    total += 1.0f;
  }
  
  return total > 0 ? frustrated / total : 0.0f;
}

std::vector<std::vector<size_t>> BeliefIsingModel::get_coherent_clusters() const {
  std::vector<bool> visited(spins_.size(), false);
  std::vector<std::vector<size_t>> clusters;
  
  for (size_t i = 0; i < spins_.size(); ++i) {
    if (!visited[i]) {
      std::vector<size_t> cluster;
      std::queue<size_t> q;
      q.push(i);
      visited[i] = true;
      
      while (!q.empty()) {
        size_t u = q.front(); q.pop();
        cluster.push_back(u);
        
        for (size_t j = 0; j < spins_.size(); ++j) {
          if (!visited[j] && couplings_[i][j] > 0.01f) {
            visited[j] = true;
            q.push(j);
          }
        }
      }
      if (!cluster.empty()) clusters.push_back(cluster);
    }
  }
  return clusters;
}

void BeliefIsingModel::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(spins_.size()));
  for (const auto& s : spins_) {
    w.u8(static_cast<uint8_t>(s.state));
    w.f32(s.external_field);
    w.f32(s.certainty);
    w.str(s.description);
  }
  w.u32(static_cast<uint32_t>(coupling_list_.size()));
  for (const auto& [a, b] : coupling_list_) {
    w.u32(static_cast<uint32_t>(a));
    w.u32(static_cast<uint32_t>(b));
    w.f32(couplings_[a][b]);
  }
}

bool BeliefIsingModel::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  spins_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    uint8_t s;
    if (!r.u8(s) || !r.f32(spins_[i].external_field) || !r.f32(spins_[i].certainty) || !r.str(spins_[i].description))
      return false;
    spins_[i].state = static_cast<int>(static_cast<int8_t>(s));
  }
  
  uint32_t m;
  if (!r.u32(m)) return false;
  coupling_list_.resize(m);
  couplings_.clear();
  couplings_.resize(spins_.size(), std::vector<float>(spins_.size(), 0.0f));
  for (uint32_t i = 0; i < m; ++i) {
    uint32_t a, b;
    float str_val;
    if (!r.u32(a) || !r.u32(b) || !r.f32(str_val))
      return false;
    couplings_[a][b] = str_val;
    couplings_[b][a] = str_val;
    coupling_list_.push_back({static_cast<size_t>(a), static_cast<size_t>(b)});
  }
  return true;
}

} // namespace eidolon