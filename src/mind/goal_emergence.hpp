#ifndef EIDOLON_GOAL_EMERGENCE_HPP
#define EIDOLON_GOAL_EMERGENCE_HPP

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <optional>

#include "core/vec2.hpp"
#include "core/rng.hpp"
#include "body/physiology.hpp"
#include "mind/world_predictor.hpp"

namespace eidolon {

// Goal types that can emerge from drives/state/opportunities
enum class GoalType : uint8_t {
  None = 0,
  Survive = 1,           // basic survival
  FindFood = 2,          // hunger-driven
  FindWater = 3,         // thirst-driven
  Rest = 4,              // fatigue/sleep pressure
  FleeThreat = 5,        // immediate danger
  Explore = 6,           // curiosity/novelty
  BuildShelter = 7,      // long-term safety
  CraftTool = 8,         // enable other goals
  Socialize = 9,         // social drive
  DefendTerritory = 10,  // territorial
  Count = 11
};

struct Goal {
  GoalType type = GoalType::None;
  float priority = 0.0f;           // 0..1, higher = more urgent
  float confidence = 0.0f;         // how certain we are this goal is valid
  Vec2i target_pos = {-1, -1};     // target location if spatial
  uint32_t target_id = 0;          // target entity ID if entity-based
  uint64_t created_at = 0;         // sim tick when created
  uint64_t deadline = 0;           // optional deadline (0 = none)
  std::string description;         // human-readable description

  float utility(const Physiology& body, const class World& world) const;
  bool is_satisfied(const Physiology& body, const class World& world) const;
  bool is_expired(uint64_t current_tick) const;
};

struct Opportunity {
  enum class Type : uint8_t {
    None = 0,
    FoodSource = 1,
    WaterSource = 2,
    ShelterSite = 3,
    MaterialCache = 4,
    SocialContact = 5,
    Threat = 6,
    ExplorationFrontier = 7,
    Count = 8
  };

  Type type = Type::None;
  Vec2i position;
  float value = 0.0f;              // estimated value/reward
  float accessibility = 1.0f;      // how easy to reach/use
  uint32_t entity_id = 0;          // associated entity if any
  uint64_t discovered_at = 0;
  float confidence = 1.0f;         // detection confidence
};

class GoalEmergence {
public:
  GoalEmergence() = default;
  explicit GoalEmergence(uint64_t seed);

  // Evaluate all drives/state/opportunities and return emerged goals
  std::vector<Goal> evaluate(const Physiology& body,
                             const class World& world,
                             const std::vector<Opportunity>& opportunities,
                             uint64_t current_tick,
                             class Rng& rng);

  // Get current active goals (not satisfied, not expired)
  const std::vector<Goal>& active_goals() const { return active_goals_; }

  // Update goal priorities based on new information
  void update_priorities(const Physiology& body, const class World& world);

  // Mark goal as satisfied/failed
  void complete_goal(GoalType type, bool success);

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  std::vector<Goal> active_goals_;
  std::vector<Goal> completed_goals_;
  uint64_t last_update_tick_ = 0;
  Rng rng_; // for stochastic priority updates

  // Compute priority for a potential goal
  float compute_priority(GoalType type, const Physiology& body,
                         const class World& world,
                         const std::vector<Opportunity>& opportunities) const;

  // Check if goal is feasible given current state
  bool is_feasible(GoalType type, const Physiology& body,
                   const class World& world) const;

  // Generate spatial target for goal
  std::optional<Vec2i> generate_target(GoalType type,
                                       const class World& world,
                                       const std::vector<Opportunity>& opportunities,
                                       class Rng& rng) const;
};

} // namespace eidolon

#endif // EIDOLON_GOAL_EMERGENCE_HPP