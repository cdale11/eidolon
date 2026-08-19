#ifndef EIDOLON_WILDLIFE_SOCIAL_HPP
#define EIDOLON_WILDLIFE_SOCIAL_HPP

#include <array>
#include <vector>
#include <cstdint>

#include "world/wildlife.hpp"
#include "core/serialize.hpp"

namespace eidolon {

// Wildlife social model: tracks organism's relationships with individual wildlife agents
// Each agent gets a social profile based on species and individual interactions

struct WildlifeSocialProfile {
  int32_t agent_id = -1;              // wildlife agent ID
  Species species = Species::Rabbit;
  
  // Social dimensions (0..1)
  float familiarity = 0.0f;    // how well the organism knows this individual
  float fear = 0.0f;           // fear of this individual
  float friendliness = 0.0f;   // positive social bond
  float threat_level = 0.0f;   // perceived threat level
  
  // Interaction history
  uint32_t encounters = 0;
  uint32_t positive_encounters = 0;
  uint32_t negative_encounters = 0;
  uint64_t last_encounter_tick = 0;
  Vec2i last_known_position = {-1, -1};
  
  // Behavioral expectations
  float expected_aggression = 0.0f;  // expected aggression from this individual
  float expected_cooperation = 0.0f; // expected cooperation
  
  void record_encounter(bool positive, int64_t tick, const Vec2i& pos);
  float get_threat_assessment() const;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class WildlifeSocialSystem {
public:
  WildlifeSocialSystem() = default;
  
  // Get or create social profile for an agent
  WildlifeSocialProfile& get_profile(int32_t agent_id, Species species);
  
  // Record an encounter with a wildlife agent
  void record_encounter(int32_t agent_id, Species species, bool positive, 
                        int64_t tick, const Vec2i& pos);
  
  // Get social profile for an agent (const)
  const WildlifeSocialProfile* get_profile(int32_t agent_id) const;
  
  // Get all profiles for a species
  std::vector<const WildlifeSocialProfile*> get_profiles_for_species(Species species) const;
  
  // Decay familiarity over time
  void decay_familiarity(uint64_t current_tick);
  
  // Get overall social state summary
  struct SocialSummary {
    float avg_familiarity = 0.0f;
    float avg_fear = 0.0f;
    float avg_friendliness = 0.0f;
    uint32_t total_encounters = 0;
    uint32_t unique_individuals = 0;
    std::vector<std::pair<Species, float>> species_fear; // per-species avg fear
  };
  SocialSummary get_summary() const;
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  std::unordered_map<int32_t, WildlifeSocialProfile> profiles_;
};

} // namespace eidolon

#endif // EIDOLON_WILDLIFE_SOCIAL_HPP