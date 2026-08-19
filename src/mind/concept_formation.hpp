#ifndef EIDOLON_CONCEPT_FORMATION_HPP
#define EIDOLON_CONCEPT_FORMATION_HPP

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

#include "core/serialize.hpp"
#include "core/rng.hpp"
#include "mind/self_model.hpp"
#include "mind/belief_ising.hpp"

namespace eidolon {

// Concept formation: incremental clustering in embedding space
// Concepts emerge from experience clustering, can be named via LLM

struct Concept {
  uint32_t id = 0;
  std::string name;              // human-readable name (may be LLM-generated)
  std::vector<float> centroid;   // centroid in feature space
  std::vector<uint32_t> member_experiences; // indices into experience buffer
  float cohesion = 0.0f;         // internal similarity (0..1)
  float separation = 0.0f;       // separation from other concepts (0..1)
  uint32_t usage_count = 0;      // how often this concept is activated
  uint64_t created_at = 0;
  uint64_t last_used = 0;
  bool named_by_llm = false;     // whether name came from LLM
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct ExperienceVector {
  std::vector<float> features;   // feature vector (e.g., 43-dim state)
  uint64_t tick = 0;
  std::string context;           // context description
  uint32_t concept_id = 0;       // assigned concept (0 = unassigned)
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class ConceptFormation {
public:
  ConceptFormation() = default;
  explicit ConceptFormation(uint64_t seed);
  
  // Process a new experience vector, assign to existing concept or create new
  // Returns concept ID (0 if no concept assigned)
  uint32_t process_experience(const std::vector<float>& features,
                              const std::string& context,
                              uint64_t tick,
                              class Rng& rng);
  
  // Get concept by ID
  const Concept* get_concept(uint32_t id) const;
  
  // Get all concepts
  const std::vector<Concept>& get_concepts() const { return concepts_; }
  
  // Merge similar concepts (if cohesion > threshold and separation < threshold)
  void merge_concepts(float cohesion_threshold = 0.8f, float separation_threshold = 0.3f);
  
  // Request LLM to name a concept (if not yet named)
  std::string request_llm_name(const Concept& concept,
                               const std::function<std::string(const std::string&)>& llm_callback);
  
  // Get concept activation for current state
  std::vector<std::pair<uint32_t, float>> get_active_concepts(
      const std::vector<float>& current_features,
      float threshold = 0.5f) const;
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  std::vector<Concept> concepts_;
  std::vector<ExperienceVector> experience_buffer_;
  uint32_t next_concept_id_ = 1;
  uint64_t seed_;
  class Rng rng_;
  
  // K-means style clustering (incremental)
  uint32_t find_best_concept(const std::vector<float>& features) const;
  float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) const;
  void update_concept_centroid(uint32_t concept_id);
  float compute_concept_cohesion(const Concept& concept) const;
  float compute_concept_separation(const Concept& concept) const;
  void maybe_create_new_concept(const std::vector<float>& features,
                                const std::string& context,
                                uint64_t tick,
                                class Rng& rng);
};

} // namespace eidolon

#endif // EIDOLON_CONCEPT_FORMATION_HPP