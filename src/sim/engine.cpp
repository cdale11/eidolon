#include "sim/engine.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace eidolon {

namespace {
constexpr int kDefaultWorldW = 128;
constexpr int kDefaultWorldH = 128;

const char* modeName(int m) {
  return m == 0 ? "active" : m == 1 ? "rest" : "sleep";
}

void serializeRng(BinaryWriter& w, const Rng& r) {
  const auto s = r.state();
  w.u64(s[0]);
  w.u64(s[1]);
  w.u64(s[2]);
  w.u64(s[3]);
}
bool deserializeRng(BinaryReader& r, Rng& out) {
  uint64_t a, b, c, d;
  if (!r.u64(a) || !r.u64(b) || !r.u64(c) || !r.u64(d)) return false;
  out = Rng::fromState({a, b, c, d});
  return true;
}
} // namespace

void Engine::init(uint64_t masterSeed, bool deterministic, int worldW, int worldH) {
  masterSeed_ = masterSeed;
  deterministic_ = deterministic;
  clock_.set(0);
  died_ = false;
  lastStatusAt_ = 0;
  statusInterval_ = 600;
  stats_ = Stats{};

  rngWorld_ = subsystemStream(masterSeed_, Subsystem::World);
  rngWeather_ = subsystemStream(masterSeed_, Subsystem::Weather);
  rngBody_ = subsystemStream(masterSeed_, Subsystem::Body);
  rngCognition_ = subsystemStream(masterSeed_, Subsystem::Cognition);
  rngEvents_ = subsystemStream(masterSeed_, Subsystem::Events);

  world_.generate(worldW > 0 ? worldW : kDefaultWorldW,
                  worldH > 0 ? worldH : kDefaultWorldH, rngWorld_);
  body_.reset();
}

void Engine::stepClock(StepKind kind) noexcept {
  clock_.advance(static_cast<int64_t>(kind));
  switch (kind) {
    case StepKind::Fine: ++stats_.ticksFine; break;
    case StepKind::Coarse: ++stats_.ticksCoarse; break;
    case StepKind::Sleep: ++stats_.ticksSleep; break;
  }
}

Action Engine::tick() noexcept {
  if (died_) return Action::Observe;

  const Action action = decide();

  // Step size: sleep is coarsest, rest is coarse, active is fine.
  const StepKind step = body_.isSleeping()
                            ? StepKind::Sleep
                            : (action == Action::Rest ? StepKind::Coarse : StepKind::Fine);
  stepClock(step);

  const double dt = static_cast<double>(step);

  const bool weatherChanged =
      world_.update(clock_, static_cast<int64_t>(step), rngWeather_);
  if (weatherChanged) {
    events_.push({clock_.now(), 1 /* kind: weather */, 0});
  }

  const Activity act = body_.isSleeping()          ? Activity::Sleep
                       : action == Action::Rest    ? Activity::Rest
                       : action == Action::Observe ? Activity::Observe
                                                   : Activity::Move;
  body_.update(dt, world_.weather().ambientTempC(clock_), act);

  execute(action);

  if (!body_.alive() && !died_) {
    died_ = true;
    world_.killOrganism();
  }
  return action;
}

Action Engine::decide() noexcept {
  if (body_.isSleeping()) {
    // Wake when rested enough.
    if (body_.sleepPressure() < 12.0 && body_.fatigue() < 15.0) {
      body_.setSleeping(false);
    }
    return Action::Sleep;
  }
  if (body_.needsSleep()) {
    body_.setSleeping(true);
    return Action::Sleep;
  }
  if (body_.fatigue() > 55.0 && body_.energy() < 30.0) {
    return Action::Rest;
  }
  // Curiosity: occasionally pause and observe instead of wandering blindly.
  if (rngCognition_.chance(0.15)) return Action::Observe;
  return Action::Wander;
}

void Engine::execute(Action a) noexcept {
  switch (a) {
    case Action::Wander: {
      const Vec2i p = world_.organismPos();
      const int dx = rngCognition_.irange(-1, 1);
      const int dy = rngCognition_.irange(-1, 1);
      const Vec2i q{p.x + dx, p.y + dy};
      if (world_.grid().inBounds(q.x, q.y) && world_.grid().walkable(q.x, q.y)) {
        world_.setOrganismPos(q);
      }
      ++stats_.actionsWander;
      break;
    }
    case Action::Rest:
      ++stats_.actionsRest;
      break;
    case Action::Sleep:
      ++stats_.actionsSleep;
      break;
    case Action::Observe:
      ++stats_.actionsObserve;
      break;
  }
}

void Engine::checkEvents(EventLog* log) noexcept {
  EventQueue::Event e;
  while (events_.popDue(clock_.now(), e)) {
    if (log && e.kind == 1) {
      log->line(clock_.now(), "weather", "%s", world_.weather().describe());
    }
  }
}

void Engine::logStatus(EventLog& log) noexcept {
  const Physiology& b = body_;
  const Weather& w = world_.weather();
  log.line(clock_.now(), "status",
           "day=%lld hour=%.1f pos=(%d,%d) terrain=%d temp=%.1fC weather=%s "
           "energy=%.1f hunger=%.1f thirst=%.1f fatigue=%.1f sleepP=%.1f "
           "bodyTemp=%.1f health=%.1f pain=%.1f asleep=%d",
           static_cast<long long>(clock_.day()), clock_.hourOfDay(),
           world_.organismPos().x, world_.organismPos().y,
           static_cast<int>(world_.grid().at(world_.organismPos().x,
                                             world_.organismPos().y)),
           w.ambientTempC(clock_), w.describe(), b.energy(), b.hunger(),
           b.thirst(), b.fatigue(), b.sleepPressure(), b.bodyTemp(), b.health(),
           b.pain(), b.isSleeping() ? 1 : 0);
}

void Engine::tickAndLog(EventLog& log) noexcept {
  const Action action = tick();
  if (died_) {
    log.line(clock_.now(), "death",
             "energy=%.1f hunger=%.1f thirst=%.1f health=%.1f pos=(%d,%d)",
             body_.energy(), body_.hunger(), body_.thirst(), body_.health(),
             world_.organismPos().x, world_.organismPos().y);
    return;
  }
  // Log life-mode transitions only (active/rest/sleep) to keep the trace readable.
  const int mode = body_.isSleeping() ? 2 : (action == Action::Rest ? 1 : 0);
  if (mode != prevMode_) {
    prevMode_ = mode;
    log.line(clock_.now(), "state", "%s", modeName(mode));
  }
  if (clock_.now() - lastStatusAt_ >= statusInterval_) {
    lastStatusAt_ = clock_.now() - (clock_.now() % statusInterval_);
    logStatus(log);
  }
  checkEvents(&log);
}

bool Engine::runDays(double days, EventLog& log, std::string& whyStopped) {
  const int64_t target = static_cast<int64_t>(days * 86400.0);
  while (!died_ && clock_.now() < target) {
    tickAndLog(log);
    if (died_) {
      whyStopped = "organism died";
      return false;
    }
  }
  whyStopped = "completed";
  return true;
}

// ---------------------------------------------------------------------------

void Engine::serializeState(BinaryWriter& w) const {
  w.u64(masterSeed_);
  w.u8(deterministic_ ? 1 : 0);
  w.i64(clock_.now());
  w.i64(lastStatusAt_);
  w.u8(static_cast<uint8_t>(prevMode_));

  serializeRng(w, rngWorld_);
  serializeRng(w, rngWeather_);
  serializeRng(w, rngBody_);
  serializeRng(w, rngCognition_);
  serializeRng(w, rngEvents_);
  world_.serialize(w);
  body_.serialize(w);
  w.u64(stats_.ticksFine);
  w.u64(stats_.ticksCoarse);
  w.u64(stats_.ticksSleep);
  w.u64(stats_.actionsWander);
  w.u64(stats_.actionsRest);
  w.u64(stats_.actionsSleep);
  w.u64(stats_.actionsObserve);
}

bool Engine::deserializeState(BinaryReader& r, std::string& err) {
  uint64_t seed;
  uint8_t det;
  int64_t now, lastStatus;
  if (!r.u64(seed) || !r.u8(det) || !r.i64(now) || !r.i64(lastStatus)) {
    err = "snapshot header corrupt";
    return false;
  }
  uint8_t prevMode;
  if (!r.u8(prevMode) || prevMode > 2) {
    err = "snapshot header corrupt";
    return false;
  }
  masterSeed_ = seed;
  deterministic_ = det != 0;
  clock_.set(now);
  lastStatusAt_ = lastStatus;
  prevMode_ = prevMode;
  if (!deserializeRng(r, rngWorld_) || !deserializeRng(r, rngWeather_) ||
      !deserializeRng(r, rngBody_) || !deserializeRng(r, rngCognition_) ||
      !deserializeRng(r, rngEvents_)) {
    err = "snapshot rng corrupt";
    return false;
  }
  if (!world_.deserialize(r) || !body_.deserialize(r)) {
    err = "snapshot world/body corrupt";
    return false;
  }
  died_ = !world_.organismAlive();
  if (!r.u64(stats_.ticksFine) || !r.u64(stats_.ticksCoarse) ||
      !r.u64(stats_.ticksSleep) || !r.u64(stats_.actionsWander) ||
      !r.u64(stats_.actionsRest) || !r.u64(stats_.actionsSleep) ||
      !r.u64(stats_.actionsObserve)) {
    err = "snapshot stats corrupt";
    return false;
  }
  return r.done();
}

std::vector<uint8_t> Engine::snapshot() const {
  return packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) { serializeState(w); });
}

bool Engine::restore(const std::vector<uint8_t>& blob, std::string& err) {
  return unpackSnapshot(blob, kSnapshotVersion,
                        [&](BinaryReader& r) { return deserializeState(r, err); }, err);
}

bool Engine::saveFile(const std::string& path, std::string& err) const {
  return saveSnapshotFile(path, kSnapshotVersion,
                          [&](BinaryWriter& w) { serializeState(w); }, err);
}

bool Engine::loadFile(const std::string& path, std::string& err) {
  return loadSnapshotFile(path, kSnapshotVersion,
                          [&](BinaryReader& r) { return deserializeState(r, err); },
                          err);
}

} // namespace eidolon
