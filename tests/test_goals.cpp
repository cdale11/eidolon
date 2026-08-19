// Phase 7 tests: Goal emergence
#include "harness.hpp"

#include <vector>

#include "mind/goal_emergence.hpp"
#include "body/physiology.hpp"
#include "world/world.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"

using namespace eidolon;

TEST(goal_emergence_basic) {
  Physiology body;
  body.reset();

  World world;
  Rng rng(42);
  world.generate(64, 64, rng);

  std::vector<Opportunity> opportunities;
  // Add a food source opportunity
  Opportunity food_opp;
  food_opp.type = Opportunity::Type::FoodSource;
  food_opp.position = {10, 10};
  food_opp.value = 1.0f;
  food_opp.confidence = 1.0f;
  opportunities.push_back(food_opp);

  GoalEmergence ge(42);
  auto goals = ge.evaluate(body, world, opportunities, 1000, rng);

  // Should have at least survival and find food goals
  bool has_survive = false, has_food = false;
  for (const auto& g : goals) {
    if (g.type == GoalType::Survive) has_survive = true;
    if (g.type == GoalType::FindFood) has_food = true;
  }
  CHECK(has_survive);
  CHECK(has_food);
}