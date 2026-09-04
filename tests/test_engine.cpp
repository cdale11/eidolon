#include "harness.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "sim/engine.hpp"

using namespace eidolon;

namespace {
// Run an engine deterministically for `ticks` ticks, returning the sim time reached
// and a hash of the final state (deterministic given the seed).
struct RunResult {
  int64_t time = 0;
  uint64_t stateHash = 0;
  bool alive = true;
};

void hashMix(uint64_t& h, uint64_t v) noexcept {
  h ^= v;
  h *= 0x100000001b3ULL;
}

RunResult runTicks(Engine& e, int ticks) {
  RunResult r;
  for (int i = 0; i < ticks; ++i) {
    if (!e.isAlive()) break;
    e.tick();
  }
  r.time = e.clock().now();
  r.alive = e.isAlive();
  const auto snap = e.snapshot();
  for (const uint8_t b : snap) hashMix(r.stateHash, b);
  return r;
}
} // namespace

TEST(engine_deterministic_identical_runs) {
  Engine a, b;
  a.init(42, true, 64, 64);
  b.init(42, true, 64, 64);
  const RunResult ra = runTicks(a, 1000);
  const RunResult rb = runTicks(b, 1000);
  CHECK_EQ(ra.time, rb.time);
  CHECK_EQ(ra.stateHash, rb.stateHash);
  CHECK_EQ(ra.alive, rb.alive);
}

TEST(engine_different_seeds_diverge) {
  Engine a, b;
  a.init(42, true, 64, 64);
  b.init(43, true, 64, 64);
  const RunResult ra = runTicks(a, 1000);
  const RunResult rb = runTicks(b, 1000);
  CHECK(ra.stateHash != rb.stateHash);
}

TEST(engine_snapshot_roundtrip_continues_identically) {
  // Run engine A for 500 ticks, snapshot; continue to 1000. Engine B: snapshot-restore
  // after 500, then continue to 1000 — logs/state must be identical.
  Engine a, b;
  a.init(7, true, 64, 64);
  b.init(7, true, 64, 64);
  (void)runTicks(a, 500);
  std::string err;
  const auto snap = a.snapshot();
  CHECK(b.restore(snap, err));
  (void)runTicks(a, 500);
  (void)runTicks(b, 500);
  CHECK_EQ(a.clock().now(), b.clock().now());
  CHECK_EQ(a.snapshot(), b.snapshot());
  CHECK_EQ(a.body().energy(), b.body().energy());
  CHECK(a.world().organismPos() == b.world().organismPos());
}

TEST(engine_corrupt_snapshot_rejected) {
  Engine e;
  e.init(1, true, 32, 32);
  auto snap = e.snapshot();
  snap[snap.size() / 3] ^= 0x01;
  std::string err;
  CHECK(!e.restore(snap, err));
  CHECK(!err.empty());
}

TEST(engine_runs_days_and_logs) {
  Engine e;
  e.init(42, true, 64, 64);
  e.setStatusInterval(600);
  EventLog log;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "/tmp/eidolon_test_events_%d.log", static_cast<int>(::getpid()));
  std::string path = buf;
  std::remove(path.c_str());
  CHECK(log.open(path));
  std::string why;
  const bool completed = e.runDays(0.25, log, why); // 6 sim hours
  log.close();
  CHECK(completed);
  CHECK(why == "completed");
  CHECK(e.clock().now() >= 6 * 3600);
  CHECK(e.clock().now() < 7 * 3600);
  CHECK(e.stats().ticksFine > 0);
  CHECK(e.stats().ticksSleep > 0 || e.body().sleepPressure() < 100);
}

TEST(engine_adaptive_steps_used) {
  Engine e;
  e.init(9, true, 64, 64);
  // ~30 sim-hours: fatigue/sleep pressure should force rest and sleep steps.
  (void)runTicks(e, 110000);
  CHECK(e.stats().ticksCoarse + e.stats().ticksSleep > 0);
  CHECK(e.stats().ticksFine > 0);
}

TEST(engine_lastAction_default_then_updates) {
  // Before the first tick, lastAction is a safe default (Observe). After a few
  // ticks, lastAction must reflect whatever decide()/execute() actually chose —
  // not the default, not necessarily Observe. This is what the LLM bridge's
  // CognitiveSnapshot::currentAction reads (see src/llm/bridge.cpp::makeSnapshot).
  Engine e;
  e.init(7, true, 64, 64);
  CHECK_EQ(static_cast<int>(e.lastAction()), static_cast<int>(Action::Observe));
  for (int i = 0; i < 50; ++i) e.tick();
  // After 50 ticks, lastAction must equal the last returned Action from tick().
  // Capture the next action and verify they match.
  const Action a = e.tick();
  CHECK_EQ(static_cast<int>(e.lastAction()), static_cast<int>(a));
  // And: lastAction must NOT be stuck on the default (otherwise the snapshot
  // would lie about what the organism is doing right now).
  const bool sawNonObserve = (e.stats().actionsForage > 0) ||
                             (e.stats().actionsDrink > 0) ||
                             (e.stats().actionsRest > 0) ||
                             (e.stats().actionsWander > 0) ||
                             (e.stats().actionsFlee > 0);
  CHECK(sawNonObserve); // sanity: the organism actually does things
  CHECK(static_cast<int>(e.lastAction()) != static_cast<int>(Action::Observe) ||
        sawNonObserve == false); // tautology guard — lastAction CAN be Observe
                                  // when the policy legitimately chose it.
}

TEST(engine_lastAction_survives_snapshot_roundtrip) {
  // lastAction_ is in the snapshot (v10+) so a resumed run has the correct
  // chat-grounding action. This is the central invariant for fix-as-you-go #2.
  Engine a, b;
  a.init(11, true, 64, 64);
  b.init(11, true, 64, 64);
  for (int i = 0; i < 200; ++i) a.tick();
  const Action lastA = a.lastAction();
  std::string err;
  CHECK(b.restore(a.snapshot(), err));
  CHECK_EQ(static_cast<int>(b.lastAction()), static_cast<int>(lastA));
  // Resume a few more ticks on both — the actions must continue to match
  // (deterministic given the seed).
  for (int i = 0; i < 50; ++i) {
    a.tick();
    b.tick();
  }
  CHECK_EQ(static_cast<int>(a.lastAction()), static_cast<int>(b.lastAction()));
}

TEST(engine_policy_action_roundtrip_all12) {
  // policyToAction/actionToPolicy used to silently drop Farm/Cook/Craft/Build/
  // CollectWater/Preserve (falling through to Observe) — the policy's 12 outputs
  // must map bijectively onto the matching 12 Actions.
  const PolicyAction all[] = {
    PolicyAction::Forage, PolicyAction::Drink, PolicyAction::Rest,
    PolicyAction::Wander, PolicyAction::Observe, PolicyAction::Flee,
    PolicyAction::Farm, PolicyAction::Cook, PolicyAction::Craft,
    PolicyAction::Build, PolicyAction::CollectWater, PolicyAction::Preserve,
  };
  for (PolicyAction p : all) {
    const Action a = Engine::policyToAction(p);
    CHECK_EQ(static_cast<int>(Engine::actionToPolicy(a)), static_cast<int>(p));
  }
  // Spot-check the previously broken tail of the mapping.
  CHECK(Engine::policyToAction(PolicyAction::Build) == Action::Build);
  CHECK(Engine::policyToAction(PolicyAction::Preserve) == Action::Preserve);
  CHECK(Engine::actionToPolicy(Action::CollectWater) == PolicyAction::CollectWater);
}
