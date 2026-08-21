// Phase 5 gate + hazard tests: wounds/infection/immune model, cliff & fall hazards,
// disease-vector exposure, and the end-to-end causal chains (scarcity -> exploration ->
// food; predator attacks -> threat learning -> survival). Deterministic (seeded).
#include "harness.hpp"

#include <algorithm>
#include <cmath>

#include "sim/engine.hpp"

using namespace eidolon;

namespace {

void runTicks(Engine& e, int ticks) {
  for (int i = 0; i < ticks; ++i) {
    if (!e.isAlive()) break;
    e.tick();
  }
}

// Teleport the first wolf to a walkable, non-cliff tile adjacent (or near) the organism,
// hungry and ready to hunt. Rabbits and the rest of the pack are disabled.
void parkHungryWolf(Engine& e) {
  WildlifeAgent* wolf = nullptr;
  for (WildlifeAgent& a : e.world().wildlife().agents()) {
    if (a.species == Species::Rabbit) a.alive = false;
    else if (a.species == Species::Wolf) {
      if (!wolf) wolf = &a;
      else a.alive = false;
    }
  }
  CHECK(wolf != nullptr);
  const Vec2i op = e.world().organismPos();
  const Grid& g = e.world().grid();
  // Find a nearby walkable, non-cliff-relative tile: try the 8 adjacent tiles first.
  Vec2i spot = {-1, -1};
  const int off[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0},
                         {-1, 1}, {0, 1}, {1, 1}};
  for (const auto& o : off) {
    const int nx = op.x + o[0], ny = op.y + o[1];
    if (g.inBounds(nx, ny) && g.walkable(nx, ny) &&
        !g.cliffBetween(op.x, op.y, nx, ny) && !g.cliffBetween(nx, ny, op.x, op.y)) {
      spot = {nx, ny};
      break;
    }
  }
  // Fallback: expand outward in ring order.
  if (spot.x < 0) {
    for (int r = 2; r <= 4 && spot.x < 0; ++r) {
      for (int dy = -r; dy <= r && spot.x < 0; ++dy) {
        for (int dx = -r; dx <= r && spot.x < 0; ++dx) {
          if (std::abs(dx) != r && std::abs(dy) != r) continue;
          const int nx = op.x + dx, ny = op.y + dy;
          if (g.inBounds(nx, ny) && g.walkable(nx, ny) &&
              !g.cliffBetween(op.x, op.y, nx, ny)) {
            spot = {nx, ny};
            break;
          }
        }
      }
    }
  }
  CHECK(spot.x >= 0); // Must find a valid nearby walkable tile near the organism.
  wolf->pos = spot;
  wolf->hunger = 90.0;
  wolf->state = AnimalState::Hunt;
  wolf->attackCooldownUntil = 0;
}

} // namespace

TEST(physiology_wound_infection_immune_roundtrip) {
  Physiology p;
  p.reset();
  p.addWound(0.4, 0); // predator bite
  p.addWound(0.2, 1); // fall scrape
  CHECK_EQ(p.woundCount(), 2);
  CHECK(p.woundPain() > 0.0);

  // Age the wounds past the infectable window (>= 600 sim-seconds) while active (so the
  // faster rest-healing doesn't drop severity below the infectable threshold).
  p.update(700.0, 25.0, Activity::Move, 0.0);

  // Chronic exposure in a hazard zone seeds infection in the severe wound.
  p.updateExposure(0.7, 1000.0);
  CHECK(p.totalInfection() > 0.0);
  CHECK(p.infectedWounds() >= 1);

  // Serialization round-trips wounds + immune state.
  BinaryWriter w;
  p.serialize(w);
  BinaryReader r(w.data());
  Physiology q;
  CHECK(q.deserialize(r));
  CHECK_EQ(q.woundCount(), p.woundCount());
  CHECK_EQ(q.wounds().size(), p.wounds().size());
  for (size_t i = 0; i < p.wounds().size(); ++i) {
    CHECK(std::fabs(q.wounds()[i].severity - p.wounds()[i].severity) < 1e-9);
    CHECK(std::fabs(q.wounds()[i].infection - p.wounds()[i].infection) < 1e-9);
    CHECK_EQ(q.wounds()[i].age, p.wounds()[i].age);
    CHECK_EQ(q.wounds()[i].source, p.wounds()[i].source);
  }
  CHECK(std::fabs(q.immunity() - p.immunity()) < 1e-9);
  CHECK(std::fabs(q.exposure() - p.exposure()) < 1e-9);

  // The immune system fights the infection down over time (and wounds heal).
  const double infBefore = p.totalInfection();
  for (int i = 0; i < 2000; ++i) p.update(60.0, 25.0, Activity::Sleep, 0.0);
  CHECK(p.totalInfection() < infBefore);
}

TEST(hazard_cliff_blocks_movement) {
  Engine e;
  e.init(13, true, 64, 64);
  Grid& g = e.world().grid();
  // Construct a flat area with a single steep spike: an impassable cliff.
  const Vec2i from = {10, 10};
  const Vec2i to = {11, 10};
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) g.setElevation(10 + dx, 10 + dy, 0.0f);
  g.setElevation(to.x, to.y, Grid::kCliffStep + 0.05f);
  e.world().setOrganismPos(from);
  CHECK(!e.stepTo(to)); // cliff is impassable in both directions

  // A non-cliff neighbor is still reachable.
  const Vec2i okTo = {10, 9};
  CHECK(e.stepTo(okTo));
}

TEST(hazard_steep_descent_fall_damage) {
  Engine e;
  e.init(21, true, 64, 64);
  Grid& g = e.world().grid();
  // Flat area plus one steeper-than-kFallDamageDrop (but not a cliff) descent.
  const Vec2i from = {10, 10};
  const Vec2i to = {11, 10};
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) g.setElevation(10 + dx, 10 + dy, 0.0f);
  g.setElevation(to.x, to.y, -0.15f); // drop 0.15 in (kFallDamageDrop, kCliffStep)
  e.world().setOrganismPos(from);
  // A steep descent is refused during normal movement...
  CHECK(!e.stepTo(to));
  // ...but is taken (with damage) when forced to flee.
  const double healthBefore = e.body().health();
  const uint64_t fallsBefore = e.stats().fallsTaken;
  CHECK(e.stepTo(to, true)); // not a cliff, so the step succeeds when forced
  CHECK(e.stats().fallsTaken == fallsBefore + 1);
  CHECK(e.body().health() < healthBefore - 1e-6); // the fall hurt

  // A gentle step does no damage.
  const Vec2i f2 = {10, 12};
  const Vec2i t2 = {11, 12};
  g.setElevation(t2.x, t2.y, -0.05f);
  e.world().setOrganismPos(f2);
  const double h2 = e.body().health();
  const uint64_t falls2 = e.stats().fallsTaken;
  CHECK(e.stepTo(t2));
  CHECK(e.stats().fallsTaken == falls2); // no new fall
  CHECK(std::fabs(e.body().health() - h2) < 1e-6);
}

TEST(hazard_exposure_accumulates_on_swamp) {
  Engine e;
  e.init(7, true, 64, 64);
  const Grid& g = e.world().grid();
  Vec2i swamp = {-1, -1};
  for (int y = 0; y < g.height(); ++y) {
    for (int x = 0; x < g.width(); ++x) {
      if (g.at(x, y) == Terrain::Swamp) { swamp = {x, y}; break; }
    }
    if (swamp.x >= 0) break;
  }
  CHECK(swamp.x >= 0);
  // Park the organism on the swamp tile each tick (it stays in the hazard zone).
  for (int i = 0; i < 120; ++i) {
    e.world().setOrganismPos(swamp);
    e.tick();
  }
  CHECK(e.body().exposure() > 0.0);
}

TEST(phase5_causal_scarcity_explores_finds_food) {
  Engine e;
  e.init(42, true, 64, 64);
  const Vec2i spawn = e.world().organismPos();
  // Strip every plant within sight+margin of the spawn: food exists only elsewhere.
  auto& plants = e.world().plants();
  plants.erase(std::remove_if(plants.begin(), plants.end(),
                              [&](const Plant& pl) {
                                return distCheb(pl.pos, spawn) <= 12;
                              }),
               plants.end());
  CHECK(e.world().nearestEdiblePlant(spawn, Perception::kSightRadius) == nullptr);

  runTicks(e, 86400); // up to one simulated day

  // Causal chain: scarcity forced exploration away from spawn, which found food.
  CHECK(e.isAlive());
  CHECK(e.stats().berriesEaten > 0);
  const int traveled = distCheb(e.world().organismPos(), spawn);
  CHECK(traveled > 8); // the organism left the stripped home range
}

TEST(phase5_gate_threat_learning) {
  // Control: same seed, no predator pressure -> threat estimate stays ~0.
  Engine ctrl;
  ctrl.init(777, true, 64, 64);
  runTicks(ctrl, 20);
  const float threatControl = ctrl.learn().threatEstimate();

  // Attack: a hungry wolf repeatedly bites the organism; pain accumulates past the
  // aversive threshold and sensitizes the ThreatNet.
  // Run 20 ticks to ensure multiple wildlife updates (every 5 sim-seconds).
  Engine att;
  att.init(777, true, 64, 64);
  for (int i = 0; i < 20; ++i) {
    parkHungryWolf(att);
    att.tick();
  }
  CHECK(att.stats().predatorAttacks > 0);
  CHECK(att.isAlive());
  CHECK(att.learn().threatEstimate() > threatControl + 0.1f);
}

TEST(phase5_gate_survival_improves_with_experience) {
  // Both branches start from the same world snapshot (identical RNG). The experienced
  // organism first learns predator threat, then both face the same hungry wolf.
  Engine base;
  base.init(1, true, 128, 128);
  runTicks(base, 300);
  const std::vector<uint8_t> snap = base.snapshot();

  Engine naive;
  {
    std::string err;
    CHECK(naive.restore(snap, err));
  }
  naive.resetBody();

  Engine exp;
  {
    std::string err;
    CHECK(exp.restore(snap, err));
  }
  // Training: repeated predator bites (every wildlife step) build pain past the
  // aversive threshold; ThreatNet sensitization takes ~5-10 bites (fast with pain>
  // 25 accumulating between steps since each bite heals slowly).
  for (int i = 0; i < 40; ++i) {
    parkHungryWolf(exp);
    runTicks(exp, 5); // one wildlife update (bite) per cycle
    if (exp.body().health() < 40.0) exp.resetBody(); // keep organism alive to learn
  }
  CHECK(exp.learn().threatEstimate() > 0.6f); // durable threat signal after training
  exp.resetBody(); // healed body; the learned threat (ThreatNet weights) remains.

  // Survival comparison: both face the same hungry wolf, initially at distance ~6
  // tiles (outside the 3-tile emergency valve, inside the 12-tile sight radius).
  // The trained organism's elevated threat (>0.6) activates the veto immediately,
  // causing it to flee at sight; the naive organism only reacts when the wolf closes
  // to 3 tiles. Over 120 ticks this produces a clearly larger average distance for
  // the trained branch (verified empirically: avg distance ~2x).
  auto placeWolfAtDist = [](Engine& e, int desired) {
    WildlifeAgent* wolf = nullptr;
    for (WildlifeAgent& a : e.world().wildlife().agents()) {
      if (a.species == Species::Wolf && a.alive) { wolf = &a; break; }
    }
    CHECK(wolf != nullptr);
    // Kill rabbits and other wolves.
    for (WildlifeAgent& a : e.world().wildlife().agents()) {
      if (a.species == Species::Rabbit) a.alive = false;
      else if (a.species == Species::Wolf && a.id != wolf->id) a.alive = false;
    }
    const Vec2i op = e.world().organismPos();
    const Grid& g = e.world().grid();
    Vec2i spot{-1, -1};
    for (int r = desired; r <= desired + 2 && spot.x < 0; ++r) {
      for (int dy = -r; dy <= r && spot.x < 0; ++dy) {
        for (int dx = -r; dx <= r && spot.x < 0; ++dx) {
          if (std::abs(dx) != r && std::abs(dy) != r) continue;
          int nx = op.x + dx, ny = op.y + dy;
          if (g.inBounds(nx, ny) && g.walkable(nx, ny) &&
              !g.cliffBetween(op.x, op.y, nx, ny)) {
            spot = {nx, ny};
            break;
          }
        }
      }
    }
    CHECK(spot.x >= 0);
    wolf->pos = spot;
    wolf->hunger = 90.0;
    wolf->state = AnimalState::Hunt;
    wolf->attackCooldownUntil = 0;
  };
  placeWolfAtDist(naive, 6);
  placeWolfAtDist(exp, 6);

  double naiveAvgDist = 0.0, expAvgDist = 0.0;
  int nNaive = 0, nExp = 0;
  for (int i = 0; i < 120; ++i) {
    if (naive.isAlive()) {
      double d = -1;
      for (const WildlifeAgent& a : naive.world().wildlife().agents()) {
        if (a.species == Species::Wolf && a.alive) {
          const Vec2i op = naive.world().organismPos();
          d = static_cast<double>(std::max(std::abs(a.pos.x - op.x), std::abs(a.pos.y - op.y)));
          break;
        }
      }
      if (d >= 0) { naiveAvgDist += d; ++nNaive; }
    }
    if (exp.isAlive()) {
      double d = -1;
      for (const WildlifeAgent& a : exp.world().wildlife().agents()) {
        if (a.species == Species::Wolf && a.alive) {
          const Vec2i op = exp.world().organismPos();
          d = static_cast<double>(std::max(std::abs(a.pos.x - op.x), std::abs(a.pos.y - op.y)));
          break;
        }
      }
      if (d >= 0) { expAvgDist += d; ++nExp; }
    }
    if (naive.isAlive()) naive.tick();
    if (exp.isAlive()) exp.tick();
  }
  const bool expAlive = exp.isAlive();

  // The defense gate: the trained organism maintains strictly more distance from the
  // predator during the chase (proactive flee at sight radius vs emergency-only).
  if (nExp > 0) expAvgDist /= nExp;
  if (nNaive > 0) naiveAvgDist /= nNaive;
  CHECK(expAvgDist > naiveAvgDist);
  CHECK(expAlive);
}