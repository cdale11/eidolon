#include "sim/engine.hpp"

#include <algorithm>
#include <cmath>
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
  resting_ = false;
  lastStatusAt_ = 0;
  statusInterval_ = 600;
  stats_ = Stats{};
  memory_ = MemoryRing();

  rngWorld_ = subsystemStream(masterSeed_, Subsystem::World);
  rngWeather_ = subsystemStream(masterSeed_, Subsystem::Weather);
  rngBody_ = subsystemStream(masterSeed_, Subsystem::Body);
  rngCognition_ = subsystemStream(masterSeed_, Subsystem::Cognition);
  rngLearn_ = subsystemStream(masterSeed_, Subsystem::Learning);
  rngEvents_ = subsystemStream(masterSeed_, Subsystem::Events);

  world_.generate(worldW > 0 ? worldW : kDefaultWorldW,
                  worldH > 0 ? worldH : kDefaultWorldH, rngWorld_);
  body_.reset();
  learn_.init(rngLearn_);
  recordEpisode(EventKind::Birth, 0, 0.5);
}

void Engine::recordEpisode(EventKind kind, uint8_t detail, double importance) noexcept {
  // Negative valence strengthens episodic encoding (DESIGN §6 neuromodulator coupling).
  const double v = learn_.neuromod().valence;
  if (v < 0.0) importance *= (1.0 + 0.5 * -v);
  const Vec2i p = world_.organismPos();
  Episode e;
  e.t = clock_.now();
  e.x = static_cast<int16_t>(p.x);
  e.y = static_cast<int16_t>(p.y);
  e.kind = kind;
  e.detail = detail;
  e.importance = std::max(0.0, std::min(1.0, importance));
  memory_.add(e);
  if (archive_) archive_->episode(e);
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

  // State features at the START of this tick (decision + TD state s_t).
  learn_.buildFeatures(world_.perceive(world_.organismPos(), clock_), body_, featsBefore_);

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
    recordEpisode(EventKind::Weather, 0, 0.05);
  }

  const Activity act = body_.isSleeping()          ? Activity::Sleep
                       : action == Action::Rest    ? Activity::Rest
                       : action == Action::Observe ? Activity::Observe
                       : action == Action::Forage  ? Activity::Forage
                       : action == Action::Drink   ? Activity::Drink
                                                   : Activity::Move;
  const Physiology before = body_;
  body_.update(dt, world_.weather().ambientTempC(clock_), act);

  const uint64_t berriesBefore = stats_.berriesEaten;
  const uint64_t drinksBefore = stats_.drinks;
  execute(action);
  const double eaten = static_cast<double>(stats_.berriesEaten - berriesBefore);
  const bool drank = stats_.drinks > drinksBefore;

  // Learning step: features at the END of the tick (s_{t+1}), intrinsic reward, then
  // TD/bandit/threat/attention/neuromodulator updates.
  learn_.buildFeatures(world_.perceive(world_.organismPos(), clock_), body_, featsAfter_);
  const float novelty = learn_.novelty(featsAfter_);
  const float reward = learn_.computeReward(body_, before, novelty, eaten, drank);
  const bool agentic = !body_.isSleeping();
  const PolicyAction pa = actionToPolicy(action);
  const bool aversive = aversiveTick(before);
  const bool safe = safeTick(reward);
  learn_.learnStep(featsBefore_, featsAfter_, pa, agentic, reward, novelty, aversive, safe);
  learn_.updateDaily(clock_.now());

  // Offline teacher-data dump (CLI only): one JSONL record per tick.
  if (experienceOut_)
    dumpExperience(pa, agentic, reward, novelty, aversive, safe, eaten, drank);

  // Near-death experiences (health critically low) are highly important memories.
  if (body_.health() < 20.0 && !died_) {
    const Episode* last = memory_.last();
    if (!last || last->kind != EventKind::NearDeath ||
        clock_.now() - last->t >= 1800) {
      recordEpisode(EventKind::NearDeath, static_cast<uint8_t>(body_.health()), 0.9);
    }
  }

  if (!body_.alive() && !died_) {
    died_ = true;
    world_.killOrganism();
  }
  return action;
}

bool Engine::loadPolicyPrior(const std::string& path) { return learn_.loadPolicyPrior(path); }

bool Engine::aversiveTick(const Physiology& before) const noexcept {
  // Acute danger only: pain, rapid health loss, or near-lethal core temperature.
  // Chronic cold is survival pressure (energy drain), not an immediate threat — it must
  // not saturate the ThreatNet every winter tick.
  if (body_.pain() > 25.0) return true;
  if (before.health() - body_.health() > 5.0) return true;
  if (std::fabs(body_.bodyTemp() - Physiology::kBodyTempC) > 8.0) return true;
  if (body_.hunger() >= 90.0 || body_.thirst() >= 90.0) return true;
  return false;
}

bool Engine::safeTick(float reward) const noexcept {
  return reward >= 0.0f && body_.pain() < 5.0 &&
         std::fabs(body_.bodyTemp() - Physiology::kBodyTempC) <= 2.0;
}

void Engine::dumpExperience(PolicyAction pa, bool agentic, float reward, float novelty,
                            bool aversive, bool safe, double eaten,
                            bool drank) noexcept {
  const Vec2i p = world_.organismPos();
  static constexpr const char* kActionNames[] = {"Forage", "Drink", "Rest",
                                                 "Wander", "Observe"};
  int bushDist = -1;
  if (const Bush* bush = world_.nearestBush(p, Perception::kSightRadius)) {
    bushDist = distCheb(bush->pos, p);
  }
  int waterDist = -1;
  for (int y = p.y - Perception::kSightRadius; y <= p.y + Perception::kSightRadius; ++y) {
    for (int x = p.x - Perception::kSightRadius;
         x <= p.x + Perception::kSightRadius; ++x) {
      if (world_.grid().at(x, y) != Terrain::Water) continue;
      const int d = distCheb({x, y}, p);
      waterDist = (waterDist < 0 || d < waterDist) ? d : waterDist;
    }
  }
  std::FILE* f = experienceOut_;
  std::fprintf(f,
               "{\"t\":%lld,\"agentic\":%d,\"action\":\"%s\",\"reward\":%.4f,"
               "\"novelty\":%.4f,\"threat\":%.4f,\"aversive\":%d,\"safe\":%d,"
               "\"body\":{\"h\":%.1f,\"t\":%.1f,\"f\":%.1f,\"e\":%.1f,\"hp\":%.1f,"
               "\"p\":%.1f,\"s\":%.1f,\"temp\":%.1f},\"wx\":{\"tempC\":%.1f,\"desc\":\"%s\"},"
               "\"bushDist\":%d,\"waterDist\":%d,\"eaten\":%.1f,\"drank\":%d,\"feats\":[",
               static_cast<long long>(clock_.now()), agentic ? 1 : 0,
               kActionNames[static_cast<int>(pa)], static_cast<double>(reward),
               static_cast<double>(novelty), static_cast<double>(learn_.threatEstimate()),
               aversive ? 1 : 0, safe ? 1 : 0, body_.hunger(), body_.thirst(),
               body_.fatigue(), body_.energy(), body_.health(), body_.pain(),
               body_.sleepPressure(), body_.bodyTemp(), world_.weather().ambientTempC(clock_),
               world_.weather().describe(), bushDist, waterDist, eaten, drank ? 1 : 0);
  for (int i = 0; i < LearnSystem::kFeatures; ++i) {
    std::fprintf(f, "%s%.4f", i ? "," : "", static_cast<double>(featsBefore_[i]));
  }
  std::fprintf(f, "]}\n");
}

Action Engine::policyToAction(PolicyAction a) noexcept {
  switch (a) {
    case PolicyAction::Forage: return Action::Forage;
    case PolicyAction::Drink: return Action::Drink;
    case PolicyAction::Rest: return Action::Rest;
    case PolicyAction::Wander: return Action::Wander;
    case PolicyAction::Observe: return Action::Observe;
  }
  return Action::Observe;
}

PolicyAction Engine::actionToPolicy(Action a) noexcept {
  switch (a) {
    case Action::Forage: return PolicyAction::Forage;
    case Action::Drink: return PolicyAction::Drink;
    case Action::Rest: return PolicyAction::Rest;
    case Action::Wander: return PolicyAction::Wander;
    default: return PolicyAction::Observe;
  }
}

Action Engine::decide() noexcept {
  if (body_.isSleeping()) {
    // Wake when rested enough.
    if (body_.sleepPressure() < 12.0 && body_.fatigue() < 15.0) {
      body_.setSleeping(false);
      recordEpisode(EventKind::Wake, 0, 0.15);
    }
    return Action::Sleep;
  }
  if (body_.needsSleep()) {
    body_.setSleeping(true);
    resting_ = false;
    recordEpisode(EventKind::Sleep, 0, 0.15);
    return Action::Sleep;
  }
  // Rest mode with hysteresis: stay resting until fatigue recovers well below the
  // trigger, so the organism doesn't oscillate around the fatigue boundary.
  if (resting_) {
    if (body_.fatigue() < 25.0) resting_ = false;
    else return Action::Rest;
  } else if (body_.fatigue() > 75.0) {
    resting_ = true;
    return Action::Rest;
  }
  // Emergency safety valves override the learned policy (deterministic survival).
  if (body_.thirst() > 80.0) return Action::Drink;
  if (body_.hunger() > 80.0) return Action::Forage;
  if (body_.pain() > 40.0) return Action::Rest;
  // Learned policy proposes an agentic action from the state features.
  const PolicyAction chosen = learn_.chooseAction(featsBefore_, rngLearn_);
  Action a = policyToAction(chosen);
  // ThreatNet veto: in a threatening state, avoid exploration.
  if (learn_.threatEstimate() > 0.65 &&
      (chosen == PolicyAction::Wander || chosen == PolicyAction::Observe)) {
    a = Action::Rest;
  }
  return a;
}

bool Engine::moveToward(Vec2i target) noexcept {
  const Vec2i p = world_.organismPos();
  if (p == target) return true;
  // Greedy best-step: try all 8 neighbors, move to the walkable one that reduces the
  // Chebyshev distance to the target most (ties broken by fixed offset order). This
  // flanks water barriers instead of freezing against them.
  static constexpr int kOffsets[8][2] = {
      {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
  int bestX = -1, bestY = -1, bestScore = distCheb(p, target);
  for (const auto& off : kOffsets) {
    const Vec2i q{p.x + off[0], p.y + off[1]};
    if (!world_.grid().inBounds(q.x, q.y) || !world_.grid().walkable(q.x, q.y)) continue;
    const int score = distCheb(q, target);
    if (score < bestScore) {
      bestScore = score;
      bestX = q.x;
      bestY = q.y;
    }
  }
  if (bestX >= 0) {
    world_.setOrganismPos({bestX, bestY});
    return true;
  }
  // Dead end (surrounded by water): random jitter to escape.
  for (int attempt = 0; attempt < 4; ++attempt) {
    const Vec2i q{p.x + rngCognition_.irange(-1, 1), p.y + rngCognition_.irange(-1, 1)};
    if (world_.grid().inBounds(q.x, q.y) && world_.grid().walkable(q.x, q.y) &&
        q != p) {
      world_.setOrganismPos(q);
      return true;
    }
  }
  return false;
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
    case Action::Forage: {
      ++stats_.actionsForage;
      const Vec2i p = world_.organismPos();
      const Bush* bush = world_.nearestBush(p, Perception::kSightRadius);
      if (!bush) {
        // No food in sight: wander to look for some.
        const int dx = rngCognition_.irange(-1, 1);
        const int dy = rngCognition_.irange(-1, 1);
        const Vec2i q{p.x + dx, p.y + dy};
        if (world_.grid().inBounds(q.x, q.y) && world_.grid().walkable(q.x, q.y)) {
          world_.setOrganismPos(q);
        }
        break;
      }
      if (bush->pos == p) {
        const double eaten = world_.consumeBerries(bush->pos, 4.0);
        if (eaten > 0.0) {
          body_.eat(eaten);
          stats_.berriesEaten += static_cast<uint64_t>(eaten);
          events_.push({clock_.now(), 2, static_cast<uint16_t>(eaten * 10.0)});
          recordEpisode(EventKind::Forage,
                        static_cast<uint8_t>(std::min(15.0, eaten * 2.5)),
                        0.25 + eaten / 12.0);
        }
      } else {
        // Step onto the bush tile (it is walkable), then eat next tick.
        moveToward(bush->pos);
      }
      break;
    }
    case Action::Drink: {
      ++stats_.actionsDrink;
      const Vec2i p = world_.organismPos();
if (world_.adjacentToWater(p)) {
        body_.drink(8.0);
        ++stats_.drinks;
        events_.push({clock_.now(), 3, 0});
        recordEpisode(EventKind::Drink, 0, 0.25);
      } else {
        // Walk toward the nearest water tile in sight (or wander).
        int targetX = -1, targetY = -1, bestD = Perception::kSightRadius + 1;
        for (int y = p.y - Perception::kSightRadius; y <= p.y + Perception::kSightRadius;
             ++y) {
          for (int x = p.x - Perception::kSightRadius;
               x <= p.x + Perception::kSightRadius; ++x) {
            if (world_.grid().at(x, y) != Terrain::Water) continue;
            const int d = distCheb({x, y}, p);
            if (d < bestD) {
              bestD = d;
              targetX = x;
              targetY = y;
            }
          }
        }
        if (targetX >= 0) {
          moveToward({targetX, targetY});
        } else {
          const int dx = rngCognition_.irange(-1, 1);
          const int dy = rngCognition_.irange(-1, 1);
          const Vec2i q{p.x + dx, p.y + dy};
          if (world_.grid().inBounds(q.x, q.y) && world_.grid().walkable(q.x, q.y)) {
            world_.setOrganismPos(q);
          }
        }
      }
      break;
    }
  }
}

void Engine::checkEvents(EventLog* log) noexcept {
  EventQueue::Event e;
  while (events_.popDue(clock_.now(), e)) {
    const char* type = nullptr;
    char buf[64];
    switch (e.kind) {
      case 1:
        type = "weather";
        std::snprintf(buf, sizeof(buf), "%s", world_.weather().describe());
        break;
      case 2:
        type = "forage";
        std::snprintf(buf, sizeof(buf), "berries=%.1f", e.payload / 10.0);
        break;
      case 3:
        type = "drink";
        std::snprintf(buf, sizeof(buf), "water=8.0");
        break;
      default:
        break;
    }
    if (log && type) log->line(clock_.now(), type, "%s", buf);
    if (archive_ && type) archive_->event(clock_.now(), type, buf);
  }
}

void Engine::logStatus(EventLog& log) noexcept {
  const Physiology& b = body_;
  const Weather& w = world_.weather();
  const Perception p = world_.perceive(world_.organismPos(), clock_);
  log.line(clock_.now(), "status",
           "day=%lld hour=%.1f pos=(%d,%d) terrain=%d temp=%.1fC weather=%s "
           "energy=%.1f hunger=%.1f thirst=%.1f fatigue=%.1f sleepP=%.1f "
           "bodyTemp=%.1f health=%.1f pain=%.1f asleep=%d food=%.1f water=%.1f",
           static_cast<long long>(clock_.day()), clock_.hourOfDay(),
           world_.organismPos().x, world_.organismPos().y,
           static_cast<int>(world_.grid().at(world_.organismPos().x,
                                             world_.organismPos().y)),
           w.ambientTempC(clock_), w.describe(), b.energy(), b.hunger(),
           b.thirst(), b.fatigue(), b.sleepPressure(), b.bodyTemp(), b.health(),
           b.pain(), b.isSleeping() ? 1 : 0, p[4], p[8]);
}

void Engine::tickAndLog(EventLog& log) noexcept {
  const Action action = tick();
  if (died_) {
    recordEpisode(EventKind::Death, 0, 1.0);
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
  serializeRng(w, rngLearn_);
  serializeRng(w, rngEvents_);
  world_.serialize(w);
  body_.serialize(w);
  memory_.serialize(w);
  learn_.serialize(w);
  w.u64(stats_.ticksFine);
  w.u64(stats_.ticksCoarse);
  w.u64(stats_.ticksSleep);
  w.u64(stats_.actionsWander);
  w.u64(stats_.actionsRest);
  w.u64(stats_.actionsSleep);
  w.u64(stats_.actionsObserve);
  w.u64(stats_.actionsForage);
  w.u64(stats_.actionsDrink);
  w.u64(stats_.berriesEaten);
  w.u64(stats_.drinks);
  w.u8(resting_ ? 1 : 0);
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
      !deserializeRng(r, rngLearn_) || !deserializeRng(r, rngEvents_)) {
    err = "snapshot rng corrupt";
    return false;
  }
  if (!world_.deserialize(r) || !body_.deserialize(r) || !memory_.deserialize(r) ||
      !learn_.deserialize(r)) {
    err = "snapshot world/body/memory corrupt";
    return false;
  }
  died_ = !world_.organismAlive();
  if (!r.u64(stats_.ticksFine) || !r.u64(stats_.ticksCoarse) ||
      !r.u64(stats_.ticksSleep) || !r.u64(stats_.actionsWander) ||
      !r.u64(stats_.actionsRest) || !r.u64(stats_.actionsSleep) ||
      !r.u64(stats_.actionsObserve) || !r.u64(stats_.actionsForage) ||
      !r.u64(stats_.actionsDrink) || !r.u64(stats_.berriesEaten) ||
      !r.u64(stats_.drinks)) {
    err = "snapshot stats corrupt";
    return false;
  }
  uint8_t resting;
  if (!r.u8(resting) || resting > 1) {
    err = "snapshot header corrupt";
    return false;
  }
  resting_ = resting != 0;
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
