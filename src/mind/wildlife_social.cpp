#include "mind/wildlife_social.hpp"
#include <algorithm>
#include <cmath>

namespace eidolon {

void WildlifeSocialProfile::record_encounter(bool positive, int64_t tick, const Vec2i& pos) {
  encounters++;
  last_encounter_tick = tick;
  last_known_position = pos;
  
  if (positive) {
    positive_encounters++;
    friendliness = std::min(1.0f, friendliness + 0.02f);
    fear = std::max(0.0f, fear - 0.01f);
    expected_aggression = std::max(0.0f, expected_aggression - 0.01f);
    expected_cooperation = std::min(1.0f, expected_cooperation + 0.02f);
  } else {
    negative_encounters++;
    friendliness = std::max(0.0f, friendliness - 0.03f);
    fear = std::min(1.0f, fear + 0.05f);
    expected_aggression = std::min(1.0f, expected_aggression + 0.05f);
    expected_cooperation = std::max(0.0f, expected_cooperation - 0.03f);
  }
  
  familiarity = std::min(1.0f, familiarity + 0.01f);
  threat_level = fear * 0.7f + expected_aggression * 0.3f;
}

float WildlifeSocialProfile::get_threat_assessment() const {
  return threat_level * 0.6f + fear * 0.3f + expected_aggression * 0.1f;
}

void WildlifeSocialProfile::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(agent_id));
  w.u8(static_cast<uint8_t>(species));
  w.f32(familiarity);
  w.f32(fear);
  w.f32(friendliness);
  w.f32(threat_level);
  w.u32(encounters);
  w.u32(positive_encounters);
  w.u32(negative_encounters);
  w.u64(last_encounter_tick);
  w.u32(static_cast<uint32_t>(last_known_position.x));
  w.u32(static_cast<uint32_t>(last_known_position.y));
  w.f32(expected_aggression);
  w.f32(expected_cooperation);
  w.f32(threat_level);
}

bool WildlifeSocialProfile::deserialize(struct BinaryReader& r) {
  uint32_t aid;
  if (!r.u32(aid)) return false;
  agent_id = static_cast<int32_t>(aid);
  uint8_t s;
  if (!r.u8(s)) return false;
  species = static_cast<Species>(s);
  if (!r.f32(familiarity) || !r.f32(fear) || !r.f32(friendliness) || !r.f32(threat_level) ||
      !r.u32(encounters) || !r.u32(positive_encounters) || !r.u32(negative_encounters) ||
      !r.u64(last_encounter_tick) ||
      !r.u32(*reinterpret_cast<uint32_t*>(&last_known_position.x)) ||
      !r.u32(*reinterpret_cast<uint32_t*>(&last_known_position.y)) ||
      !r.f32(expected_aggression) || !r.f32(expected_cooperation) || !r.f32(threat_level))
    return false;
  return true;
}

WildlifeSocialProfile& WildlifeSocialSystem::get_profile(int32_t agent_id, Species species) {
  auto it = profiles_.find(agent_id);
  if (it == profiles_.end()) {
    WildlifeSocialProfile profile;
    profile.agent_id = agent_id;
    profile.species = species;
    profiles_[agent_id] = profile;
    return profiles_[agent_id];
  }
  return it->second;
}

void WildlifeSocialSystem::record_encounter(int32_t agent_id, Species species, bool positive,
                                            int64_t tick, const Vec2i& pos) {
  auto& profile = get_profile(agent_id, species);
  profile.record_encounter(positive, tick, pos);
}

const WildlifeSocialProfile* WildlifeSocialSystem::get_profile(int32_t agent_id) const {
  auto it = profiles_.find(agent_id);
  return it != profiles_.end() ? &it->second : nullptr;
}

std::vector<const WildlifeSocialProfile*> WildlifeSocialSystem::get_profiles_for_species(Species species) const {
  std::vector<const WildlifeSocialProfile*> result;
  for (const auto& kv : profiles_) {
    if (kv.second.species == species) {
      result.push_back(&kv.second);
    }
  }
  return result;
}

void WildlifeSocialSystem::decay_familiarity(uint64_t current_tick) {
  for (auto& kv : profiles_) {
    auto& profile = kv.second;
    if (profile.last_encounter_tick > 0) {
      uint64_t elapsed = current_tick - profile.last_encounter_tick;
      float decay = 1.0f - static_cast<float>(elapsed) / 8640000.0f;
      if (decay > 0.0f) {
        profile.familiarity *= decay;
      }
    }
  }
}

WildlifeSocialSystem::SocialSummary WildlifeSocialSystem::get_summary() const {
  SocialSummary summary;
  summary.unique_individuals = static_cast<uint32_t>(profiles_.size());
  
  for (const auto& kv : profiles_) {
    const auto& p = kv.second;
    summary.avg_familiarity += p.familiarity;
    summary.avg_fear += p.fear;
    summary.avg_friendliness += p.friendliness;
    summary.total_encounters += p.encounters;
    
    bool found = false;
    for (auto& sp : summary.species_fear) {
      if (sp.first == p.species) {
        sp.second += p.fear;
        found = true;
        break;
      }
    }
    if (!found) {
      summary.species_fear.emplace_back(p.species, p.fear);
    }
  }
  
  if (summary.unique_individuals > 0) {
    summary.avg_familiarity /= summary.unique_individuals;
    summary.avg_fear /= summary.unique_individuals;
    summary.avg_friendliness /= summary.unique_individuals;
    for (auto& sp : summary.species_fear) {
      uint32_t count = 0;
      for (const auto& kv : profiles_) {
        if (kv.second.species == sp.first) count++;
      }
      if (count > 0) sp.second /= count;
    }
  }
  return summary;
}

void WildlifeSocialSystem::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(profiles_.size()));
  for (const auto& kv : profiles_) {
    kv.second.serialize(w);
  }
}

bool WildlifeSocialSystem::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  profiles_.clear();
  for (uint32_t i = 0; i < n; ++i) {
    WildlifeSocialProfile profile;
    if (!profile.deserialize(r)) return false;
    profiles_[profile.agent_id] = profile;
  }
  return true;
}

} // namespace eidolon