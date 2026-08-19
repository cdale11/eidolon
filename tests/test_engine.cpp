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
