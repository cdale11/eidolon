#ifndef EIDOLON_SELF_MODEL_HPP
#define EIDOLON_SELF_MODEL_HPP

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "core/serialize.hpp"
#include "body/physiology.hpp"
#include "mind/goal_emergence.hpp"
#include "mind/belief_ising.hpp"
#include "mind/user_model.hpp"
#include "mind/wildlife_social.hpp"
#include "mind/attachment.hpp"

namespace eidolon {

// Self-model: the organism's model of itself
// Updated continuously from experience

// Capability assessment (0..1 confidence)
struct CapabilityAssessment {
  float foraging = 0.5f;
  float hunting = 0.5f;
  float building = 0.5f;
  float crafting = 0.5f;
  float navigation = 0.5f;
  float social = 0.5f;
  float combat = 0.5f;
  float exploration = 0.5f;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Autobiographical summary
struct AutobiographicalSummary {
  uint64_t birth_tick = 0;
  uint64_t significant_events_count = 0;
  float survival_time_days = 0.0f;
  float total_distance_traveled = 0.0f;
  uint32_t total_goals_completed = 0;
  uint32_t total_goals_failed = 0;
  float longest_survival_streak = 0.0f;
  std::string defining_moment; // most impactful event
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Preferences learned from experience
struct Preferences {
  float preferred_terrain = 0.0f; // -1=avoid, 1=prefer
  float preferred_time_of_day = 0.0f; // 0=night, 1=day
  float risk_tolerance = 0.5f;
  float social_preference = 0.0f; // -1=avoid, 1=seek
  float novelty_seeking = 0.5f;
  float routine_preference = 0.5f;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Reputation with other agents
struct Reputation {
  struct Entry {
    int32_t agent_id = -1;
    float standing = 0.0f; // -1=enemy, 0=neutral, 1=ally
    uint32_t interactions = 0;
    float trust = 0.5f;
    
    void serialize(struct BinaryWriter& w) const;
    bool deserialize(struct BinaryReader& r);
  };
  std::vector<Entry> entries;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Future expectations
struct FutureExpectations {
  float expected_survival_days = 0.0f;
  float expected_resource_abundance = 0.5f;
  float expected_threat_level = 0.5f;
  float expected_social_contact = 0.5f;
  std::vector<std::string> anticipated_goals;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Self-model aggregate
struct SelfModel {
  CapabilityAssessment capabilities;
  AutobiographicalSummary autobiography;
  Preferences preferences;
  Reputation reputation;
  FutureExpectations expectations;
  
  // Metacognitive state
  float self_uncertainty = 0.5f; // overall uncertainty about self-model
  float prediction_confidence = 0.5f; // confidence in own predictions
  uint32_t prediction_errors = 0; // count of failed self-predictions
  uint32_t reflection_count = 0; // number of reflection events triggered
  
  // Update from experience
  void update_from_experience(const Physiology& body,
                              const GoalEmergence& goals,
                              const Physiology& prev_body,
                              uint64_t tick);
  
  // Trigger reflection on failed prediction
  void trigger_reflection(const std::string& context, float prediction_error);
  
  // Get self-report for conversation
  std::string generate_self_report() const;
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

} // namespace eidolon

#endif // EIDOLON_SELF_MODEL_HPP