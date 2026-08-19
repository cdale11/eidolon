#include "sim/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace eidolon {

namespace {
constexpr int kDefaultWorldW = 128;
constexpr int kDefaultWorldH = 128;
// A descent steeper than this (in elevation units) counts as a damaging fall. Chosen
// against the freq-0.02 terrain: falls are occasional, painless scratches that mostly
// bruise (wounds) and feed the infection vector — never a dominant survival drain.
constexpr double kFallDamageDrop = 0.12;
// Fall damage multiplier: dmg = (drop - kFallDamageDrop) * scale (max ~0.8 near cliffs).
constexpr double kFallDamageScale = 20.0;
// A fall opens a wound above this damage; wounds feed the infection/disease vector.
constexpr double kFallWoundThreshold = 0.25;

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
  memorySys_ = MemorySystem(256);

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
  recordEpisode(EventKind::Birth, 0, 0.5, 255, Participant::Self, Outcome::Success, 0.0f, 0.0f, 0.0f, 0.0f, Relevance::Rewarding);
}

void Engine::recordEpisode(EventKind kind, uint8_t detail, double importance,
                           uint8_t action, Participant participants, Outcome outcome,
                           float prediction, float predictionError,
                           float emotionalValence, float socialRelevance,
                           Relevance relevance) noexcept {
  // Negative valence strengthens episodic encoding (DESIGN §6 neuromodulator coupling).
  const double v = learn_.neuromod().valence;
  if (v < 0.0) importance *= (1.0 + 0.5 * -v);
  const Vec2i p = world_.organismPos();
  Episode e;
  e.t = clock_.now();
  e.x = static_cast<int16_t>(p.x);
  e.y = static_cast<int16_t>(p.y);
  e.kind = kind;
  e.action = action;
  e.participants = participants;
  e.outcome = outcome;
  e.prediction = prediction;
  e.predictionError = predictionError;
  e.emotionalValence = emotionalValence;
  e.socialRelevance = socialRelevance;
  e.relevance = relevance;
  e.importance = std::max(0.0, std::min(1.0, importance));
  e.detail = detail;
  memorySys_.ring().add(e);
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

  // Decay episode importances (slow forgetting).
  memorySys_.tickDecay();

  // State features at the START of this tick (decision + TD state s_t).
  learn_.buildFeatures(world_.perceive(world_.organismPos(), clock_), body_, featsBefore_);

  const Action action = decide();

  // Step size: sleep is coarsest, rest is coarse, active is fine.
  const StepKind step = body_.isSleeping()
                            ? StepKind::Sleep
                            : (action == Action::Rest ? StepKind::Coarse : StepKind::Fine);
  stepClock(step);

  const double dt = static_cast<double>(step);

  const WorldUpdate wu = world_.update(clock_, static_cast<int64_t>(step), rngWeather_);
  if (wu.weatherChanged) {
    events_.push({clock_.now(), 1 /* kind: weather */, 0});
    recordEpisode(EventKind::Weather, 0, 0.05, 255, Participant::None, Outcome::Unknown, 0.0f, 0.0f, 0.0f, 0.0f, Relevance::None);
  }

  const Activity act = body_.isSleeping()          ? Activity::Sleep
                       : action == Action::Rest    ? Activity::Rest
                       : action == Action::Observe ? Activity::Observe
                       : action == Action::Forage  ? Activity::Forage
                       : action == Action::Drink   ? Activity::Drink
                                                   : Activity::Move;
  const Physiology before = body_;
  const int infectedBefore = body_.infectedWounds();
  // Infection spread from cellular automata (Phase 5 branch).
  const int nearbyInfected = world_.infectionCA().infectedCountInRadius(
      world_.organismPos().x, world_.organismPos().y, 4);
  body_.update(dt, world_.weather().ambientTempC(clock_), act, hazardDose(), nearbyInfected);
  stats_.infections += static_cast<uint64_t>(std::max(0, body_.infectedWounds() - infectedBefore));

  const uint64_t berriesBefore = stats_.berriesEaten;
  const uint64_t drinksBefore = stats_.drinks;
  execute(action);

  // Predator attacks resolved during the wildlife step land after the organism acts.
  // Damage -> pain/health loss -> aversive tick -> ThreatNet sensitization, and the
  // attack is encoded as an aversive, threatening Predator episode.
  if (wu.attacked) {
    body_.takeDamage(wu.attackDamage);
    ++stats_.predatorAttacks;
    events_.push({clock_.now(), 4 /* kind: attack */,
                  static_cast<uint16_t>(wu.attackDamage * 10.0)});
    // Predator bites open wounds (Phase 5 injury model feeds infection risk later).
    const double woundSev = std::min(0.6, 0.15 + wu.attackDamage * 0.04);
    body_.addWound(woundSev, 0 /* source: predator */);
    ++stats_.woundsSustained;
    recordEpisode(EventKind::Attack, static_cast<uint8_t>(std::min(255.0, wu.attackDamage * 2.5)),
                  0.6, static_cast<uint8_t>(Action::Flee),
                  Participant::Self | Participant::Predator, Outcome::Failure, 0.0f, 0.0f, -1.0f,
                  0.3f, Relevance::Aversive | Relevance::Threatening);
  }

  // Check for sleep-to-wake transition to trigger consolidation.
  const bool wasSleeping = before.isSleeping();
  const bool nowSleeping = body_.isSleeping();
  if (wasSleeping && !nowSleeping) {
    // Just woke up: run sleep consolidation.
    if (archive_) memorySys_.consolidate(learn_, archive_);
    // Dreams v1: associative recombination.
    memorySys_.dream(learn_);
  }

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
    const Episode* last = memorySys_.ring().last();
    if (!last || last->kind != EventKind::NearDeath ||
        clock_.now() - last->t >= 1800) {
      recordEpisode(EventKind::NearDeath, static_cast<uint8_t>(body_.health()), 0.9, 255, Participant::Self, Outcome::Failure, 0.0f, 0.0f, -1.0f, 0.0f, Relevance::Aversive | Relevance::Threatening);
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
                                                   "Wander", "Observe", "Flee"};
  int bushDist = -1;
  const Plant* plant = world_.nearestEdiblePlant(p, Perception::kSightRadius);
  if (plant) {
    bushDist = distCheb(plant->pos, p);
  }
  int waterDist = -1;
  const WaterSource* water = world_.nearestWaterSource(p, Perception::kSightRadius);
  if (water) {
    waterDist = distCheb(water->pos, p);
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
    case PolicyAction::Flee: return Action::Flee;
  }
  return Action::Observe;
}

PolicyAction Engine::actionToPolicy(Action a) noexcept {
  switch (a) {
    case Action::Forage: return PolicyAction::Forage;
    case Action::Drink: return PolicyAction::Drink;
    case Action::Rest: return PolicyAction::Rest;
    case Action::Wander: return PolicyAction::Wander;
    case Action::Flee: return PolicyAction::Flee;
    default: return PolicyAction::Observe;
  }
}

Action Engine::decide() noexcept {
  if (body_.isSleeping()) {
    // Survival overrides sleep: a predator at the door, or critical thirst/hunger.
    if (world_.nearestPredator(world_.organismPos(), 2)) {
      body_.setSleeping(false);
      return Action::Flee;
    }
    if (body_.thirst() > 85.0 || body_.hunger() > 85.0) {
      body_.setSleeping(false); // wake to drink/eat, then re-sleep
    } else if (body_.sleepPressure() < 12.0 && body_.fatigue() < 15.0) {
      body_.setSleeping(false);
      recordEpisode(EventKind::Wake, 0, 0.15, 255, Participant::Self, Outcome::Success, 0.0f, 0.0f, 0.1f, 0.0f, Relevance::Rewarding);
      return Action::Sleep;
    }
    return Action::Sleep;
  }
  // Don't sleep while critically thirsty/hungry: the active branch will drink/eat.
  if (body_.needsSleep() && body_.thirst() < 85.0 && body_.hunger() < 85.0) {
    body_.setSleeping(true);
    resting_ = false;
    recordEpisode(EventKind::Sleep, 0, 0.15, 255, Participant::Self, Outcome::Success, 0.0f, 0.0f, 0.0f, 0.0f, Relevance::None);
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
  // A predator within a few tiles outranks everything: flee first.
  if (world_.nearestPredator(world_.organismPos(), 3)) return Action::Flee;
  if (body_.thirst() > 80.0) return Action::Drink;
  if (body_.hunger() > 80.0) return Action::Forage;
  if (body_.pain() > 40.0) return Action::Rest;
  // Learned policy proposes an agentic action from the state features.
  const PolicyAction chosen = learn_.chooseAction(featsBefore_, rngLearn_);
  Action a = policyToAction(chosen);
  // ThreatNet veto: in a threatening state, avoid exploration. If a predator is in
  // sight, flee; otherwise retreat to rest.
  if (learn_.threatEstimate() > 0.65 &&
      (chosen == PolicyAction::Wander || chosen == PolicyAction::Observe ||
       chosen == PolicyAction::Rest)) {
    if (world_.nearestPredator(world_.organismPos(), Perception::kSightRadius)) {
      a = Action::Flee;
    } else {
      a = Action::Rest;
    }
  }
  return a;
}

bool Engine::stepTo(Vec2i q, bool allowFall) noexcept {
  const Grid& g = world_.grid();
  if (!g.inBounds(q.x, q.y) || !g.walkable(q.x, q.y)) return false;
  const Vec2i p = world_.organismPos();
  // Impassable cliff: steeper than kCliffStep elevation change (both up and down).
  if (g.cliffBetween(p.x, p.y, q.x, q.y)) return false;
  // Steep descents are avoided unless forced (fleeing/trapped); the fall itself hurts.
  const double drop = static_cast<double>(g.elevation(p.x, p.y)) -
                      static_cast<double>(g.elevation(q.x, q.y));
  if (drop > kFallDamageDrop) {
    if (!allowFall) return false;
    const double dmg = (drop - kFallDamageDrop) * kFallDamageScale;
    if (dmg > 0.0) {
      body_.takeDamage(dmg, false); // falls bruise without spiking pain
      ++stats_.fallsTaken;
      if (dmg > kFallWoundThreshold) {
        body_.addWound(std::min(0.4, 0.1 + dmg * 0.03), 1 /* source: fall */);
        ++stats_.woundsSustained;
      }
    }
  }
  world_.setOrganismPos(q);
  return true;
}

double Engine::hazardDose() const noexcept {
  const Vec2i p = world_.organismPos();
  const Grid& g = world_.grid();
  double dose = 0.0;
  if (g.at(p.x, p.y) == Terrain::Swamp) dose += 0.0008;  // standing water on swampland
  bool adjacentDeep = false;
  bool adjacentWater = false;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int nx = p.x + dx, ny = p.y + dy;
      if (!g.inBounds(nx, ny)) continue;
      if (g.deepWater(nx, ny)) adjacentDeep = true;
      const Terrain t = g.at(nx, ny);
      if (t == Terrain::Water || t == Terrain::River) adjacentWater = true;
    }
  }
  if (adjacentDeep) dose += 0.0006;  // deep-water proximity
  if (adjacentWater && (world_.weather().raining() || world_.weather().storming()))
    dose += 0.0005;  // wading/standing in runoff while wet
  return dose;
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
    if (stepTo({bestX, bestY})) return true;
  }
  // Dead end, or every path forward descends steeply: random jitter to escape. Prefer
  // gentle steps; only take a fall if genuinely trapped.
  for (int attempt = 0; attempt < 6; ++attempt) {
    const Vec2i q{p.x + rngCognition_.irange(-1, 1), p.y + rngCognition_.irange(-1, 1)};
    if (q != p && stepTo(q)) return true;
  }
  for (int attempt = 0; attempt < 6; ++attempt) {
    const Vec2i q{p.x + rngCognition_.irange(-1, 1), p.y + rngCognition_.irange(-1, 1)};
    if (q != p && stepTo(q, true)) return true;
  }
  return false;
}

bool Engine::moveAwayFrom(Vec2i threat) noexcept {
  const Vec2i p = world_.organismPos();
  // Greedy best-step maximizing Chebyshev distance from the threat.
  static constexpr int kOffsets[8][2] = {
      {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
  int bestX = -1, bestY = -1, bestScore = distCheb(p, threat);
  for (const auto& off : kOffsets) {
    const Vec2i q{p.x + off[0], p.y + off[1]};
    if (!world_.grid().inBounds(q.x, q.y) || !world_.grid().walkable(q.x, q.y)) continue;
    const int score = distCheb(q, threat);
    if (score > bestScore) {
      bestScore = score;
      bestX = q.x;
      bestY = q.y;
    }
  }
  if (bestX >= 0) {
    return stepTo({bestX, bestY}, true); // fleeing: take any step, even a fall
  }
  // Surrounded by water/cliffs: random jitter to escape (falling allowed).
  for (int attempt = 0; attempt < 6; ++attempt) {
    const Vec2i q{p.x + rngCognition_.irange(-1, 1), p.y + rngCognition_.irange(-1, 1)};
    if (q != p && stepTo(q, true)) return true;
  }
  return false;
}

void Engine::execute(Action a) noexcept {
  switch (a) {
    case Action::Wander: {
      const Vec2i p = world_.organismPos();
      const int dx = rngCognition_.irange(-1, 1);
      const int dy = rngCognition_.irange(-1, 1);
      stepTo({p.x + dx, p.y + dy});
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
      const Plant* plant = world_.nearestEdiblePlant(p, Perception::kSightRadius);
      if (!plant) {
        // No food in sight: wander to look for some.
        const int dx = rngCognition_.irange(-1, 1);
        const int dy = rngCognition_.irange(-1, 1);
        stepTo({p.x + dx, p.y + dy});
        break;
      }
      if (plant->pos == p) {
        const double eaten = world_.consumePlant(plant->pos, 4.0);
        if (eaten > 0.0) {
          body_.eat(eaten);
          stats_.berriesEaten += static_cast<uint64_t>(eaten);
          events_.push({clock_.now(), 2, static_cast<uint16_t>(eaten * 10.0)});
          recordEpisode(EventKind::Forage,
                        static_cast<uint8_t>(std::min(15.0, eaten * 2.5)),
                        0.25 + eaten / 12.0, static_cast<uint8_t>(Action::Forage),
                        Participant::Self | Participant::Prey, Outcome::Success, 0.0f, 0.0f, 0.3f, 0.0f,
                        Relevance::Rewarding | Relevance::GoalRelated);
        }
      } else {
        // Step onto the plant tile (it is walkable), then eat next tick.
        moveToward(plant->pos);
      }
      break;
    }
    case Action::Flee: {
      ++stats_.actionsFlee;
      const Vec2i p = world_.organismPos();
      const WildlifeAgent* predator = world_.nearestPredator(p, Perception::kSightRadius);
      if (predator) {
        moveAwayFrom(predator->pos);
      } else {
        // No visible predator: defensive wander.
        const int dx = rngCognition_.irange(-1, 1);
        const int dy = rngCognition_.irange(-1, 1);
        stepTo({p.x + dx, p.y + dy});
      }
      break;
    }
    case Action::Drink: {
      ++stats_.actionsDrink;
      const Vec2i p = world_.organismPos();
      if (world_.adjacentToWater(p)) {
        double drank = world_.drinkFromSource(p, 8.0);
        if (drank > 0.0) {
          body_.drink(drank);
          ++stats_.drinks;
          events_.push({clock_.now(), 3, 0});
          recordEpisode(EventKind::Drink, 0, 0.25, static_cast<uint8_t>(Action::Drink),
                          Participant::Self, Outcome::Success, 0.0f, 0.0f, 0.2f, 0.0f,
                          Relevance::Rewarding | Relevance::GoalRelated);
        }
      } else {
        // Walk toward the nearest water source in hearing range.
        const WaterSource* water = world_.nearestWaterSource(p, Perception::kHearingRadius);
        if (water) {
          moveToward(water->pos);
        } else {
          // No water in sight: walk up the local water gradient (toward a tile whose
          // 8-neighborhood touches water) instead of wandering randomly into a desert.
          static constexpr int kGradOff[9][2] = {
              {0, 0}, {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
          Vec2i best = {-1, -1};
          int bestScore = -1;
          for (const auto& off : kGradOff) {
            const Vec2i q{p.x + off[0], p.y + off[1]};
            if (!world_.grid().inBounds(q.x, q.y) || !world_.grid().walkable(q.x, q.y)) continue;
            int score = 0;
            for (int dy = -1; dy <= 1; ++dy) {
              for (int dx = -1; dx <= 1; ++dx) {
                const Terrain t = world_.grid().at(q.x + dx, q.y + dy);
                if (t == Terrain::Water || t == Terrain::River) ++score;
              }
            }
            if (score > bestScore) {
              bestScore = score;
              best = q;
            }
          }
          if (best.x >= 0 && best != p) {
            stepTo(best);
            break;
          }
          // No water gradient here: wander.
          const int dx = rngCognition_.irange(-1, 1);
          const int dy = rngCognition_.irange(-1, 1);
          stepTo({p.x + dx, p.y + dy});
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
      case 4:
        type = "attack";
        std::snprintf(buf, sizeof(buf), "damage=%.1f", e.payload / 10.0);
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
    recordEpisode(EventKind::Death, 0, 1.0, 255, Participant::Self, Outcome::Failure, 0.0f, 0.0f, -1.0f, 0.0f, Relevance::Aversive);
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
  w.i64(scheduledTarget_);
  w.u8(static_cast<uint8_t>(prevMode_));

  serializeRng(w, rngWorld_);
  serializeRng(w, rngWeather_);
  serializeRng(w, rngBody_);
  serializeRng(w, rngCognition_);
  serializeRng(w, rngLearn_);
  serializeRng(w, rngEvents_);
  world_.serialize(w);
  body_.serialize(w);
  memorySys_.serialize(w);
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
  w.u64(stats_.actionsFlee);
  w.u64(stats_.predatorAttacks);
  w.u64(stats_.berriesEaten);
  w.u64(stats_.drinks);
  w.u64(stats_.fallsTaken);
  w.u64(stats_.woundsSustained);
  w.u64(stats_.infections);
  w.u8(resting_ ? 1 : 0);
}

bool Engine::deserializeState(BinaryReader& r, std::string& err) {
  uint64_t seed;
  uint8_t det;
  int64_t now, lastStatus, scheduledTarget;
  if (!r.u64(seed) || !r.u8(det) || !r.i64(now) || !r.i64(lastStatus) ||
      !r.i64(scheduledTarget)) {
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
  scheduledTarget_ = scheduledTarget;
  prevMode_ = prevMode;
  if (!deserializeRng(r, rngWorld_) || !deserializeRng(r, rngWeather_) ||
      !deserializeRng(r, rngBody_) || !deserializeRng(r, rngCognition_) ||
      !deserializeRng(r, rngLearn_) || !deserializeRng(r, rngEvents_)) {
    err = "snapshot rng corrupt";
    return false;
  }
  if (!world_.deserialize(r) || !body_.deserialize(r) || !memorySys_.deserialize(r) ||
      !learn_.deserialize(r)) {
    err = "snapshot world/body/memory corrupt";
    return false;
  }
  died_ = !world_.organismAlive();
  if (!r.u64(stats_.ticksFine) || !r.u64(stats_.ticksCoarse) ||
      !r.u64(stats_.ticksSleep) || !r.u64(stats_.actionsWander) ||
      !r.u64(stats_.actionsRest) || !r.u64(stats_.actionsSleep) ||
      !r.u64(stats_.actionsObserve) || !r.u64(stats_.actionsForage) ||
      !r.u64(stats_.actionsDrink) || !r.u64(stats_.actionsFlee) ||
      !r.u64(stats_.predatorAttacks) || !r.u64(stats_.berriesEaten) ||
      !r.u64(stats_.drinks) || !r.u64(stats_.fallsTaken) ||
      !r.u64(stats_.woundsSustained) || !r.u64(stats_.infections)) {
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
