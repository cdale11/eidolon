#include "mind/self_model.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace eidolon {

void CapabilityAssessment::serialize(struct BinaryWriter& w) const {
  w.f32(foraging);
  w.f32(hunting);
  w.f32(building);
  w.f32(crafting);
  w.f32(navigation);
  w.f32(social);
  w.f32(combat);
  w.f32(exploration);
}

bool CapabilityAssessment::deserialize(struct BinaryReader& r) {
  return r.f32(foraging) && r.f32(hunting) && r.f32(building) &&
         r.f32(crafting) && r.f32(navigation) && r.f32(social) &&
         r.f32(combat) && r.f32(exploration);
}

void AutobiographicalSummary::serialize(struct BinaryWriter& w) const {
  w.u64(birth_tick);
  w.u64(significant_events_count);
  w.f32(survival_time_days);
  w.f32(total_distance_traveled);
  w.u32(total_goals_completed);
  w.u32(total_goals_failed);
  w.f32(longest_survival_streak);
  w.str(defining_moment);
}

bool AutobiographicalSummary::deserialize(struct BinaryReader& r) {
  return r.u64(birth_tick) && r.u64(significant_events_count) &&
         r.f32(survival_time_days) && r.f32(total_distance_traveled) &&
         r.u32(total_goals_completed) && r.u32(total_goals_failed) &&
         r.f32(longest_survival_streak) && r.str(defining_moment);
}

void Preferences::serialize(struct BinaryWriter& w) const {
  w.f32(preferred_terrain);
  w.f32(preferred_time_of_day);
  w.f32(risk_tolerance);
  w.f32(social_preference);
  w.f32(novelty_seeking);
  w.f32(routine_preference);
}

bool Preferences::deserialize(struct BinaryReader& r) {
  return r.f32(preferred_terrain) && r.f32(preferred_time_of_day) &&
         r.f32(risk_tolerance) && r.f32(social_preference) &&
         r.f32(novelty_seeking) && r.f32(routine_preference);
}

void Reputation::Entry::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(agent_id));
  w.f32(standing);
  w.u32(interactions);
  w.f32(trust);
}

bool Reputation::Entry::deserialize(struct BinaryReader& r) {
  uint32_t id;
  if (!r.u32(id)) return false;
  agent_id = static_cast<int32_t>(id);
  return r.f32(standing) && r.u32(interactions) && r.f32(trust);
}

void Reputation::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(entries.size()));
  for (const auto& e : entries) e.serialize(w);
}

bool Reputation::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  entries.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!entries[i].deserialize(r)) return false;
  }
  return true;
}

void FutureExpectations::serialize(struct BinaryWriter& w) const {
  w.f32(expected_survival_days);
  w.f32(expected_resource_abundance);
  w.f32(expected_threat_level);
  w.f32(expected_social_contact);
  w.u32(static_cast<uint32_t>(anticipated_goals.size()));
  for (const auto& g : anticipated_goals) w.str(g);
}

bool FutureExpectations::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.f32(expected_survival_days) || !r.f32(expected_resource_abundance) ||
      !r.f32(expected_threat_level) || !r.f32(expected_social_contact) ||
      !r.u32(n)) return false;
  anticipated_goals.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.str(anticipated_goals[i])) return false;
  }
  return true;
}

void SelfModel::update_from_experience(const Physiology& body,
                                       const GoalEmergence& /*goals*/,
                                       const Physiology& /*prev_body*/,
                                       uint64_t tick) {
  // Update autobiographical summary
  autobiography.survival_time_days = static_cast<float>(tick) / 86400.0f;
  
  // Update capability assessments based on recent performance
  // Foraging capability
  if (body.hunger() < 30.0f && body.energy() > 50.0f) {
    capabilities.foraging = std::min(1.0f, capabilities.foraging + 0.001f);
  } else {
    capabilities.foraging = std::max(0.0f, capabilities.foraging - 0.0005f);
  }
  
  // Update autobiographical events
  if (body.health() < 30.0f && tick - autobiography.birth_tick > 86400) {
    autobiography.significant_events_count++;
    std::ostringstream oss;
    oss << "Survived critical health at day " << tick / 86400;
    autobiography.defining_moment = oss.str();
  }
  
  // Update preferences from recent behavior
  // Risk tolerance based on recent behavior
  if (body.health() < 40.0f) {
    preferences.risk_tolerance = std::max(0.0f, preferences.risk_tolerance - 0.001f);
  } else if (body.health() > 80.0f) {
    preferences.risk_tolerance = std::min(1.0f, preferences.risk_tolerance + 0.0005f);
  }
  
  // Update future expectations
  expectations.expected_survival_days = 30.0f * (body.health() / 100.0f);
  expectations.expected_resource_abundance = 0.5f; // would be computed from world
  expectations.expected_threat_level = 0.5f; // would be computed from world
  expectations.expected_social_contact = 0.5f;
  
  // Metacognitive updates
  // Track prediction errors (simplified)
  if (prediction_confidence > 0.0f && prediction_confidence < 1.0f) {
    self_uncertainty = std::min(1.0f, self_uncertainty + 0.001f);
  }
}

void SelfModel::trigger_reflection(const std::string& /*context*/, float prediction_error) {
  prediction_errors++;
  reflection_count++;
  
  // High prediction error increases self-uncertainty
  self_uncertainty = std::min(1.0f, self_uncertainty + prediction_error * 0.1f);
  prediction_confidence = std::max(0.0f, prediction_confidence - prediction_error * 0.5f);
  
  // In a full implementation, this would write to episodic memory
  // and potentially trigger consolidation
}

std::string SelfModel::generate_self_report() const {
  std::ostringstream oss;
  oss << "Self-report (tick " << autobiography.survival_time_days << " days):\n";
  oss << "  Health: " << capabilities.foraging << " foraging, "
      << capabilities.hunting << " hunting\n";
  oss << "  Uncertainty: " << self_uncertainty << "\n";
  oss << "  Prediction confidence: " << prediction_confidence << "\n";
  oss << "  Reflection events: " << reflection_count << "\n";
  oss << "  Goals completed: " << autobiography.total_goals_completed << "\n";
  return oss.str();
}

void SelfModel::serialize(struct BinaryWriter& w) const {
  capabilities.serialize(w);
  autobiography.serialize(w);
  preferences.serialize(w);
  reputation.serialize(w);
  expectations.serialize(w);
  w.f32(self_uncertainty);
  w.f32(prediction_confidence);
  w.u32(prediction_errors);
  w.u32(reflection_count);
}

bool SelfModel::deserialize(struct BinaryReader& r) {
  if (!capabilities.deserialize(r)) return false;
  if (!autobiography.deserialize(r)) return false;
  if (!preferences.deserialize(r)) return false;
  if (!reputation.deserialize(r)) return false;
  if (!expectations.deserialize(r)) return false;
  if (!r.f32(self_uncertainty) || !r.f32(prediction_confidence) ||
      !r.u32(prediction_errors) || !r.u32(reflection_count))
    return false;
  return true;
}

} // namespace eidolon