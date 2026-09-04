// Wildlife subsystem tests: spawn, determinism, Markov/ABM dynamics, Boids, predator
// attacks on the organism, wolf-ate-rabbit predation, and snapshot round-tripping.
#include "harness.hpp"
#include "sim/engine.hpp"
#include "world/wildlife.hpp"

#include <cstdio>
#include <cstring>

using namespace eidolon;

namespace {

int aliveRabbits(const World& w) {
  int n = 0;
  for (const WildlifeAgent& a : w.wildlife().agents()) {
    if (a.alive && a.species == Species::Rabbit) ++n;
  }
  return n;
}

int aliveWolves(const World& w) {
  int n = 0;
  for (const WildlifeAgent& a : w.wildlife().agents()) {
    if (a.alive && a.species == Species::Wolf) ++n;
  }
  return n;
}

// Run `steps` wildlife steps (dt = Wildlife::kInterval each) and return the positions.
std::vector<Vec2i> runSteps(World& w, Rng& r, int steps) {
  SimClock c;
  for (int i = 0; i < steps; ++i) {
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
  }
  std::vector<Vec2i> out;
  for (const WildlifeAgent& a : w.wildlife().agents()) out.push_back(a.pos);
  return out;
}

} // namespace

TEST(wildlife_spawns_population) {
  World w;
  Rng r(1);
  w.generate(128, 128, r);
  const Wildlife& wl = w.wildlife();
  CHECK(aliveRabbits(w) > 0);
  CHECK(aliveWolves(w) > 0);
  for (const WildlifeAgent& a : wl.agents()) {
    CHECK(w.grid().walkable(a.pos.x, a.pos.y)); // never spawn in water
    CHECK(distCheb(a.pos, w.organismPos()) > 6); // kept away from the organism
  }
}

TEST(wildlife_deterministic_replay) {
  World a, b;
  Rng ra(7), rb(7);
  a.generate(64, 64, ra);
  b.generate(64, 64, rb);
  const std::vector<Vec2i> pa = runSteps(a, ra, 50);
  const std::vector<Vec2i> pb = runSteps(b, rb, 50);
  CHECK_EQ(pa.size(), pb.size());
  for (size_t i = 0; i < pa.size(); ++i) CHECK_EQ(pa[i].x, pb[i].x);
  for (size_t i = 0; i < pa.size(); ++i) CHECK_EQ(pa[i].y, pb[i].y);
}

TEST(wildlife_snapshot_roundtrip_continues) {
  World a;
  Rng ra(3);
  a.generate(64, 64, ra);
  runSteps(a, ra, 30);

  std::vector<uint8_t> blob =
      packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) { a.serialize(w); });
  World b;
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);

  // Step both worlds from the same RNG continuation; states must stay bit-identical.
  Rng ra2(3), rb2(3);
  runSteps(a, ra2, 0); // no-op warm
  const std::vector<Vec2i> pa = runSteps(a, ra2, 40);
  const std::vector<Vec2i> pb = runSteps(b, rb2, 40);
  CHECK_EQ(pa.size(), pb.size());
  for (size_t i = 0; i < pa.size(); ++i) {
    CHECK_EQ(pa[i].x, pb[i].x);
    CHECK_EQ(pa[i].y, pb[i].y);
    CHECK_EQ(a.wildlife().agents()[i].alive, b.wildlife().agents()[i].alive);
    CHECK_EQ(a.wildlife().agents()[i].hunger, b.wildlife().agents()[i].hunger);
  }
}

TEST(wildlife_wolf_eats_rabbit) {
  World w;
  Rng r(11);
  w.generate(64, 64, r);

  Wildlife& wl = w.wildlife();
  // Find one wolf and one rabbit, teleport the wolf next to the rabbit.
  WildlifeAgent* wolf = nullptr;
  WildlifeAgent* rabbit = nullptr;
  for (WildlifeAgent& a : wl.agents()) {
    if (!wolf && a.species == Species::Wolf) wolf = &a;
    if (!rabbit && a.species == Species::Rabbit) rabbit = &a;
  }
  CHECK(wolf && rabbit);
  const int rCount = aliveRabbits(w);
  wolf->pos = {rabbit->pos.x + 1, rabbit->pos.y};
  wolf->hunger = 80.0; // hungry enough to feed
  wolf->state = AnimalState::Hunt;

  SimClock c;
  WorldUpdate out;
  (void)out; // update() returns via member state; out not filled by this overload
  w.update(c, Wildlife::kInterval, r);
  c.advance(Wildlife::kInterval);
  w.update(c, Wildlife::kInterval, r); // second step lets the kill resolve

  CHECK_EQ(aliveRabbits(w), rCount - 1);
  CHECK(wolf->hunger < 80.0); // satiated after the kill
}

TEST(wildlife_predator_attacks_organism) {
  World w;
  Rng r(13);
  w.generate(64, 64, r);

  Wildlife& wl = w.wildlife();
  WildlifeAgent* wolf = nullptr;
  for (WildlifeAgent& a : wl.agents()) {
    if (a.species == Species::Wolf) { wolf = &a; break; }
  }
  CHECK(wolf);
  // Park a properly hungry wolf right next to the organism.
  const Vec2i op = w.organismPos();
  wolf->pos = {op.x + 1, op.y};
  wolf->hunger = 90.0;
  wolf->state = AnimalState::Hunt;
  wolf->attackCooldownUntil = 0;

  SimClock c;
  WorldUpdate out;
  for (int i = 0; i < 4; ++i) {
    out = w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
    if (out.attacked) break;
  }
  CHECK(out.attacked);
  CHECK(out.attackDamage > 0.0);
  CHECK(out.attackerSpecies == static_cast<uint8_t>(Species::Wolf));
  // The wolf disengages: hunger drops below the attack threshold after feeding.
  CHECK(wolf->hunger < 55.0);
}

TEST(wildlife_prey_flees_predator) {
  World w;
  Rng r(17);
  w.generate(64, 64, r);

  Wildlife& wl = w.wildlife();
  WildlifeAgent* wolf = nullptr;
  WildlifeAgent* rabbit = nullptr;
  for (WildlifeAgent& a : wl.agents()) {
    if (!wolf && a.species == Species::Wolf) wolf = &a;
    if (!rabbit && a.species == Species::Rabbit) rabbit = &a;
  }
  CHECK(wolf && rabbit);
  // Drop a wolf near the rabbit: the rabbit must move away (flee state).
  const Vec2i rp = rabbit->pos;
  wolf->pos = {rp.x + 2, rp.y};
  wolf->hunger = 20.0; // not hunting, so the rabbit's fear drives the response

  SimClock c;
  w.update(c, Wildlife::kInterval, r);
  CHECK_EQ(rabbit->state, AnimalState::Flee);
  CHECK(distCheb(rabbit->pos, wolf->pos) >= distCheb(rp, wolf->pos));
}

TEST(wildlife_perception_channels) {
  World w;
  Rng r(19);
  w.generate(64, 64, r);
  const Vec2i p = w.organismPos();
  SimClock c;

  const Perception before = w.perceive(p, c);
  CHECK(before[20] >= 0.0 && before[20] <= 1.0); // prey distance
  CHECK(before[23] >= 0.0 && before[23] <= 1.0); // predator distance
  CHECK(before[26] >= 0.0 && before[26] <= 1.0); // prey count
  CHECK(before[27] >= 0.0 && before[27] <= 1.0); // predator count

  // A predator right next to the organism must be visible and close.
  Wildlife& wl = w.wildlife();
  WildlifeAgent* wolf = nullptr;
  for (WildlifeAgent& a : wl.agents()) {
    if (a.species == Species::Wolf) { wolf = &a; break; }
  }
  CHECK(wolf);
  wolf->pos = {p.x + 1, p.y};
  wolf->alive = true;

  const Perception after = w.perceive(p, c);
  CHECK(after[23] < 1.0);           // predator in sight
  CHECK(after[23] <= 1.0 / 8.0);    // adjacent predator reads distance 1/8
  CHECK(after[27] > 0.0);           // predator count > 0
}

TEST(wildlife_organism_engine_integration) {
  // The engine must surface predator attacks: damage, an Attack episode with the
  // Predator participant, and a predator-attack stat bump.
  Engine e;
  e.init(23, true, 64, 64);
  // Park a hungry wolf on the organism and force hunger (organism will be attacked).
  World& w = const_cast<World&>(e.world());
  const Vec2i op = w.organismPos();
  WildlifeAgent* wolf = nullptr;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species == Species::Wolf) { wolf = &a; break; }
  }
  CHECK(wolf);
  wolf->pos = {op.x + 1, op.y};
  wolf->hunger = 90.0;
  wolf->state = AnimalState::Hunt;
  wolf->attackCooldownUntil = 0;

  const uint64_t attacksBefore = e.stats().predatorAttacks;
  for (int i = 0; i < 200 && e.isAlive(); ++i) e.tick();

  CHECK(e.stats().predatorAttacks > attacksBefore);
  bool sawAttackEpisode = false;
  for (const Episode& ep : e.memory().episodes()) {
    if (ep.kind == EventKind::Attack &&
        (ep.participants & Participant::Predator) != Participant::None) {
      sawAttackEpisode = true;
      break;
    }
  }
  CHECK(sawAttackEpisode);
  // The organism survived the encounter (wakes and flees instead of being eaten).
  CHECK(e.isAlive());
  CHECK(e.body().health() < 100.0); // it took at least one hit
  CHECK(e.stats().actionsFlee > 0); // it fled
}