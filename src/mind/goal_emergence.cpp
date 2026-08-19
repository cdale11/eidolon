#include "mind/goal_emergence.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace eidolon {

GoalEmergence::GoalEmergence(uint64_t seed) : rng_(seed) {}

float Goal::utility(const Physiology& /*body*/, const class World& /*world*/) const {
  // Base utility from priority, adjusted by progress
  float u = priority * confidence;
  // Could add progress toward goal here
  return u;
}

bool Goal::is_satisfied(const Physiology& body, const class World& /*world*/) const {
  switch (type) {
    case GoalType::Survive:
      return body.alive();
    case GoalType::FindFood:
      return body.hunger() < 30.0f;
    case GoalType::FindWater:
      return body.thirst() < 30.0f;
    case GoalType::Rest:
      return body.fatigue() < 25.0f;
    case GoalType::FleeThreat:
      // Would check if threat is no longer nearby
      return true; // simplified
    default:
      return false;
  }
}

bool Goal::is_expired(uint64_t current_tick) const {
  return deadline > 0 && current_tick > deadline;
}

float GoalEmergence::compute_priority(GoalType type, const Physiology& body,
                                      const class World& /*world*/,
                                      const std::vector<Opportunity>& opportunities) const {
  float base_priority = 0.0f;

  switch (type) {
    case GoalType::Survive:
      base_priority = 1.0f;
      break;
    case GoalType::FindFood:
      base_priority = std::min(1.0f, static_cast<float>(body.hunger() / 100.0f * 2.0f));
      break;
    case GoalType::FindWater:
      base_priority = std::min(1.0f, static_cast<float>(body.thirst() / 100.0f * 2.0f));
      break;
    case GoalType::Rest:
      base_priority = std::min(1.0f, static_cast<float>(body.fatigue() / 100.0f));
      if (body.needsSleep()) base_priority += 0.3f;
      break;
    case GoalType::FleeThreat:
      base_priority = 1.0f; // always high when threatened
      break;
    case GoalType::Explore:
      base_priority = 0.3f; // curiosity
      break;
    case GoalType::BuildShelter:
      base_priority = 0.4f;
      if (body.fatigue() > 70.0f) base_priority += 0.3f;
      break;
    case GoalType::CraftTool:
      base_priority = 0.2f;
      break;
    default:
      base_priority = 0.1f;
  }

  // Boost priority if relevant opportunity exists
  for (const auto& opp : opportunities) {
    bool relevant = false;
    switch (type) {
      case GoalType::FindFood:
        relevant = opp.type == Opportunity::Type::FoodSource;
        break;
      case GoalType::FindWater:
        relevant = opp.type == Opportunity::Type::WaterSource;
        break;
      case GoalType::BuildShelter:
        relevant = opp.type == Opportunity::Type::ShelterSite;
        break;
      case GoalType::CraftTool:
        relevant = opp.type == Opportunity::Type::MaterialCache;
        break;
      case GoalType::FleeThreat:
        relevant = opp.type == Opportunity::Type::Threat;
        break;
      case GoalType::Explore:
        relevant = opp.type == Opportunity::Type::ExplorationFrontier;
        break;
      default:
        break;
    }
    if (relevant) {
      base_priority = std::min(1.0f, base_priority + opp.value * opp.accessibility);
    }
  }

  return std::clamp(base_priority, 0.0f, 1.0f);
}

bool GoalEmergence::is_feasible(GoalType type, const Physiology& body,
                                const class World& /*world*/) const {
  // Basic feasibility checks
  switch (type) {
    case GoalType::Survive:
      return body.alive();
    case GoalType::FindFood:
    case GoalType::FindWater:
    case GoalType::Rest:
    case GoalType::FleeThreat:
    case GoalType::Explore:
      return body.alive();
    case GoalType::BuildShelter:
      return body.alive() && body.energy() > 20.0f;
    case GoalType::CraftTool:
      return body.alive() && body.energy() > 15.0f;
    default:
      return body.alive();
  }
}

std::optional<Vec2i> GoalEmergence::generate_target(GoalType type,
                                                    const class World& /*world*/,
                                                    const std::vector<Opportunity>& opportunities,
                                                    class Rng& /*rng*/) const {
  // Find relevant opportunity
  for (const auto& opp : opportunities) {
    switch (type) {
      case GoalType::FindFood:
        if (opp.type == Opportunity::Type::FoodSource) return opp.position;
        break;
      case GoalType::FindWater:
        if (opp.type == Opportunity::Type::WaterSource) return opp.position;
        break;
      case GoalType::BuildShelter:
        if (opp.type == Opportunity::Type::ShelterSite) return opp.position;
        break;
      case GoalType::CraftTool:
        if (opp.type == Opportunity::Type::MaterialCache) return opp.position;
        break;
      case GoalType::FleeThreat:
        if (opp.type == Opportunity::Type::Threat) {
          // Flee away from threat
          Vec2i away = { -opp.position.x, -opp.position.y };
          return away;
        }
        break;
      case GoalType::Explore:
        if (opp.type == Opportunity::Type::ExplorationFrontier) return opp.position;
        break;
      default:
        break;
    }
  }
  return std::nullopt;
}

std::vector<Goal> GoalEmergence::evaluate(const Physiology& body,
                                          const class World& world,
                                          const std::vector<Opportunity>& opportunities,
                                          uint64_t current_tick,
                                          class Rng& rng) {
  // Remove expired/satisfied goals
  active_goals_.erase(
      std::remove_if(active_goals_.begin(), active_goals_.end(),
                     [&](const Goal& g) {
                       if (g.is_satisfied(body, world) || g.is_expired(current_tick)) {
                         completed_goals_.push_back(g);
                         return true;
                       }
                       return false;
                     }),
      active_goals_.end());

  // Evaluate potential new goals
  GoalType all_types[] = {
      GoalType::Survive, GoalType::FindFood, GoalType::FindWater,
      GoalType::Rest, GoalType::FleeThreat, GoalType::Explore,
      GoalType::BuildShelter, GoalType::CraftTool
  };

  for (GoalType type : all_types) {
    // Skip if already have this goal type active
    bool exists = false;
    for (const auto& g : active_goals_) {
      if (g.type == type) { exists = true; break; }
    }
    if (exists) continue;

    if (!is_feasible(type, body, world)) continue;

    float priority = compute_priority(type, body, world, opportunities);
    if (priority < 0.15f) continue; // threshold for goal emergence

    // Check if we already have a similar completed goal recently
    bool recent = false;
    for (const auto& g : completed_goals_) {
      if (g.type == type && current_tick - g.created_at < 86400) { // within 1 day
        recent = true; break;
      }
    }
    if (recent) continue;

    // Create new goal
    Goal goal;
    goal.type = type;
    goal.priority = priority;
    goal.confidence = 0.7f;
    goal.created_at = current_tick;
    goal.target_pos = generate_target(type, world, opportunities, rng).value_or(Vec2i{-1, -1});
    goal.description = [&]() {
      switch (type) {
        case GoalType::FindFood: return "Find food";
        case GoalType::FindWater: return "Find water";
        case GoalType::Rest: return "Rest";
        case GoalType::FleeThreat: return "Flee threat";
        case GoalType::Explore: return "Explore";
        case GoalType::BuildShelter: return "Build shelter";
        case GoalType::CraftTool: return "Craft tool";
        default: return "Unknown";
      }
    }();

    // Set deadline for urgent goals
    if (type == GoalType::FleeThreat || type == GoalType::FindWater) {
      goal.deadline = current_tick + 3600; // 1 hour
    } else if (type == GoalType::FindFood) {
      goal.deadline = current_tick + 7200; // 2 hours
    }

    active_goals_.push_back(goal);
  }

  // Sort by priority (highest first)
  std::sort(active_goals_.begin(), active_goals_.end(),
            [](const Goal& a, const Goal& b) {
              return a.priority > b.priority;
            });

  last_update_tick_ = current_tick;
  return active_goals_;
}

void GoalEmergence::update_priorities(const Physiology& /*body*/, const class World& /*world*/) {
  for (auto& goal : active_goals_) {
    // Recompute priority based on current state
    // This would need access to opportunities - simplified for now
    goal.priority = std::clamp(goal.priority + static_cast<float>(rng_.range(-0.1, 0.1)), 0.0f, 1.0f);
  }
  std::sort(active_goals_.begin(), active_goals_.end(),
            [](const Goal& a, const Goal& b) {
              return a.priority > b.priority;
            });
}

void GoalEmergence::complete_goal(GoalType type, bool success) {
  auto it = std::find_if(active_goals_.begin(), active_goals_.end(),
                         [type](const Goal& g) { return g.type == type; });
  if (it != active_goals_.end()) {
    if (success) {
      completed_goals_.push_back(*it);
    }
    active_goals_.erase(it);
  }
}

void GoalEmergence::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(active_goals_.size()));
  for (const auto& g : active_goals_) {
    w.u8(static_cast<uint8_t>(g.type));
    w.f32(g.priority);
    w.f32(g.confidence);
    w.u32(g.target_pos.x);
    w.u32(g.target_pos.y);
    w.u32(g.target_id);
    w.u64(g.created_at);
    w.u64(g.deadline);
    w.str(g.description);
  }
  w.u32(static_cast<uint32_t>(completed_goals_.size()));
  for (const auto& g : completed_goals_) {
    w.u8(static_cast<uint8_t>(g.type));
    w.f32(g.priority);
    w.f32(g.confidence);
    w.u32(g.target_pos.x);
    w.u32(g.target_pos.y);
    w.u32(g.target_id);
    w.u64(g.created_at);
    w.u64(g.deadline);
    w.str(g.description);
  }
  w.u64(last_update_tick_);
}

bool GoalEmergence::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  active_goals_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    uint8_t type_byte;
    if (!r.u8(type_byte) ||
        !r.f32(active_goals_[i].priority) || !r.f32(active_goals_[i].confidence) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&active_goals_[i].target_pos.x)) || !r.u32(*reinterpret_cast<uint32_t*>(&active_goals_[i].target_pos.y)) ||
        !r.u32(active_goals_[i].target_id) || !r.u64(active_goals_[i].created_at) ||
        !r.u64(active_goals_[i].deadline) || !r.str(active_goals_[i].description))
      return false;
    active_goals_[i].type = static_cast<GoalType>(type_byte);
  }
  if (!r.u32(n)) return false;
  completed_goals_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u8(reinterpret_cast<uint8_t&>(completed_goals_[i].type)) ||
        !r.f32(completed_goals_[i].priority) || !r.f32(completed_goals_[i].confidence) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&completed_goals_[i].target_pos.x)) || !r.u32(*reinterpret_cast<uint32_t*>(&completed_goals_[i].target_pos.y)) ||
        !r.u32(completed_goals_[i].target_id) || !r.u64(completed_goals_[i].created_at) ||
        !r.u64(completed_goals_[i].deadline) || !r.str(completed_goals_[i].description))
      return false;
  }
  if (!r.u64(last_update_tick_)) return false;
  return true;
}

} // namespace eidolon