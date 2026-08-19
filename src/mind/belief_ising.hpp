#ifndef EIDOLON_BELIEF_ISING_HPP
#define EIDOLON_BELIEF_ISING_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <string>

#include "core/serialize.hpp"
#include "core/rng.hpp"

namespace eidolon {

// Ising model for social belief/norm dynamics
// The organism's binary beliefs and trust states as spins
// Evidence = external fields; consistency = couplings between beliefs

struct BeliefSpin {
  int state = 0;           // +1 = belief held, -1 = belief rejected, 0 = uncertain
  float external_field = 0.0f;  // evidence for/against this belief
  float certainty = 0.0f;       // how certain this belief is (0..1)
  std::string description;      // human-readable description
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct BeliefCoupling {
  size_t belief_a = 0;
  size_t belief_b = 0;
  float strength = 0.0f;  // positive = consistent, negative = contradictory
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class BeliefIsingModel {
public:
  BeliefIsingModel() = default;
  explicit BeliefIsingModel(size_t n_beliefs);
  
  // Add a new belief
  size_t add_belief(const std::string& description, int initial_state = 0);
  
  // Add coupling between two beliefs
  void add_coupling(size_t a, size_t b, float strength);
  
  // Apply external evidence (field) to a belief
  void apply_evidence(size_t idx, float field_strength);
  
  // Update belief states using Glauber dynamics (deterministic with seeded noise)
  void update(class Rng& rng, float temperature = 1.0f);
  
  // Get belief state
  int get_state(size_t idx) const { return spins_[idx].state; }
  float get_certainty(size_t idx) const { return spins_[idx].certainty; }
  const std::string& get_description(size_t idx) const { return spins_[idx].description; }
  
  // Get coherent clusters (connected components with positive couplings)
  std::vector<std::vector<size_t>> get_coherent_clusters() const;
  
  // Compute energy of current configuration
  float compute_energy() const;
  
  // Measure cognitive dissonance (fraction of frustrated couplings)
  float compute_dissonance() const;
  
  // Get belief clusters for serialization
  size_t size() const { return spins_.size(); }
  std::vector<int> get_states() const {
    std::vector<int> states;
    for (const auto& s : spins_) states.push_back(s.state);
    return states;
  }
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  struct Spin {
    int state = 0;
    float external_field = 0.0f;
    float certainty = 0.0f;
    std::string description;
  };
  
  std::vector<Spin> spins_;
  std::vector<std::vector<float>> couplings_; // sparse matrix: couplings_[i][j]
  std::vector<std::pair<size_t, size_t>> coupling_list_; // for efficient iteration
  float temperature_ = 1.0f;
};

} // namespace eidolon

#endif // EIDOLON_BELIEF_ISING_HPP