// Phase 7 tests: Goal emergence
#include "harness.hpp"

#include <vector>

#include "mind/goal_emergence.hpp"
#include "body/physiology.hpp"
#include "sim/engine.hpp"
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

// Environment-driven goal emergence is wired into the engine tick: after a short run the
// organism's goal system has populated active goals from drives + world opportunities
// (no LLM/user input), so scarcity/weather shape behaviour autonomously.
TEST(engine_goal_emergence_wired) {
  Engine e;
  e.init(17, true, 64, 64);
  for (int i = 0; i < 200 && e.isAlive(); ++i) e.tick();
  const auto& goals = e.goalEmergence().active_goals();
  // The wired system must run and populate goals. The organism may be satiated early (so
  // FindFood/FindWater resolve immediately) — the invariant is that *some* goal emerges
  // and the evaluation throttle has advanced, not a fixed subset.
  CHECK(!goals.empty());
  CHECK(e.goalEmergence().last_update_tick() > 0);
  bool any = false;
  for (const auto& g : goals) {
    if (g.type == GoalType::Survive || g.type == GoalType::FindFood ||
        g.type == GoalType::FindWater || g.type == GoalType::Explore ||
        g.type == GoalType::BuildShelter || g.type == GoalType::CraftTool) {
      any = true;
      break;
    }
  }
  CHECK(any);
}
// ---------------------------------------------------------------------------
// Command autonomy: user chat reaches the organism, which decides itself.
// ---------------------------------------------------------------------------

namespace {
bool hasGoalType(const Engine& e, GoalType t) {
  for (const auto& g : e.goalEmergence().active_goals()) {
    if (g.type == t) return true;
  }
  return false;
}
} // namespace

TEST(user_command_accepted_and_injected_when_trusted) {
  Engine e;
  e.init(17, true, 64, 64);
  CHECK(e.userModel().trust >= 0.25f); // default stance: willing
  const Engine::InstructionOutcome oc = e.processUserInstruction("explore the area", 100);
  CHECK(oc.actionable);
  CHECK_EQ(oc.verdict, std::string("accepted"));
  CHECK(oc.injected);
  CHECK(!oc.reason.empty());
  CHECK(hasGoalType(e, GoalType::Explore));
  CHECK_EQ(e.lastInstruction().verdict, std::string("accepted"));
}

TEST(user_command_refused_when_distrusted) {
  Engine e;
  e.init(17, true, 64, 64);
  e.userModel().trust = 0.0f;
  const Engine::InstructionOutcome oc = e.processUserInstruction("explore the area", 100);
  CHECK(oc.actionable);
  CHECK_EQ(oc.verdict, std::string("refused"));
  CHECK(!oc.injected);
  CHECK(oc.reason.find("trust") != std::string::npos);
}

TEST(user_command_refused_when_impossible) {
  Engine e;
  e.init(17, true, 64, 64);
  // Fresh organism is satiated: foraging is correctly rejected by validation.
  const Engine::InstructionOutcome oc = e.processUserInstruction("forage for food", 100);
  CHECK(oc.actionable);
  CHECK_EQ(oc.verdict, std::string("refused"));
  CHECK(!oc.injected);
  CHECK(oc.reason.find("I can't do that") != std::string::npos);
}

TEST(user_question_never_steers_behaviour) {
  Engine e;
  e.init(17, true, 64, 64);
  const size_t before = e.goalEmergence().active_goals().size();
  const Engine::InstructionOutcome oc =
      e.processUserInstruction("what are your goals?", 100);
  CHECK(!oc.actionable);
  CHECK(!oc.injected);
  CHECK_EQ(oc.verdict, std::string("no_action"));
  CHECK_EQ(e.goalEmergence().active_goals().size(), before);
}

TEST(instruction_outcome_survives_snapshot_roundtrip) {
  Engine e;
  e.init(17, true, 64, 64);
  const Engine::InstructionOutcome oc = e.processUserInstruction("explore the area", 100);
  CHECK_EQ(oc.verdict, std::string("accepted"));
  std::string err;
  const std::vector<uint8_t> blob = e.snapshot();
  Engine e2;
  e2.init(99, true, 64, 64);
  CHECK(e2.restore(blob, err));
  CHECK_EQ(e2.lastInstruction().verdict, std::string("accepted"));
  CHECK_EQ(e2.lastInstruction().reason, oc.reason);
  CHECK(e2.lastInstruction().injected);
  CHECK(hasGoalType(e2, GoalType::Explore)); // injected goal persisted too
}
