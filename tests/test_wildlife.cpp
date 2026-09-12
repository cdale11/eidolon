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
  w.killOrganism();

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
    if (a.species == Species::Rabbit) a.alive = false;
    else if (a.species == Species::Wolf && wolf == nullptr) wolf = &a;
    else if (a.species == Species::Wolf) a.alive = false;
  }
  CHECK(wolf);
  wolf->pos = {op.x + 1, op.y};
  wolf->hunger = 90.0;
  wolf->state = AnimalState::Hunt;
  wolf->attackCooldownUntil = 0;
  w.wildlife().rebuildHashForDebug();

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

TEST(wildlife_domestication_tames_prey) {
  // Repeated friendly feeding lowers a rabbit's fear until it becomes a tamed companion.
  Engine e;
  e.init(31, true, 64, 64);
  World& w = const_cast<World&>(e.world());

  WildlifeAgent* rabbit = nullptr;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species == Species::Rabbit) { rabbit = &a; break; }
  }
  CHECK(rabbit);
  rabbit->pos = {w.organismPos().x, w.organismPos().y + 1}; // adjacent
  rabbit->fear = 1.0;
  rabbit->hunger = 80.0;
  CHECK(!rabbit->tamed);

  bool tamedNow = false;
  CHECK(e.tameNearestPrey(3, tamedNow));  // feeds
  CHECK(rabbit->hunger < 80.0);           // fed (hunger reduced)
  CHECK(rabbit->fear < 1.0);              // fear reduced
  CHECK(!tamedNow);                       // not yet below the taming threshold

  // Repeat until tamed; fear drops 0.4 per feed and tames at <=0.15.
  int guard = 0;
  while (!rabbit->tamed && guard++ < 10) {
    bool tn = false;
    e.tameNearestPrey(3, tn);
    if (tn) tamedNow = true;
  }
  CHECK(rabbit->tamed);
  CHECK(tamedNow);
  CHECK(rabbit->fear <= 0.15);
}

// ---------------------------------------------------------------------------
// E4b: development, reproduction, aging, injury, disease.
// ---------------------------------------------------------------------------

namespace {
// A breeding-ready pair parked side by side (adult fed female + adult male).
void parkBreedingPair(World& w, Species s, WildlifeAgent*& female, WildlifeAgent*& male) {
  female = male = nullptr;
  int n = 0;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species != s || !a.alive) continue;
    a.ageTicks = (s == Species::Wolf ? 25 : 6) * 86400; // adult
    a.hunger = 10.0;
    a.disease = 0.0;
    a.offspringCooldownUntil = 0;
    a.female = (n++ % 2 == 0); // force a pair regardless of spawn sexes
    if (a.female && !female) female = &a;
    if (!a.female && !male) male = &a;
  }
  CHECK(female && male);
  male->pos = {female->pos.x + 1, female->pos.y};
}

int countSpecies(const World& w, Species s) {
  int n = 0;
  for (const WildlifeAgent& a : w.wildlife().agents()) {
    if (a.alive && a.species == s) ++n;
  }
  return n;
}
} // namespace

TEST(wildlife_reproduction_births_juvenile_with_blended_traits) {
  World w;
  Rng r(41);
  w.generate(64, 64, r);
  // No wolves: pups must survive to be counted (predation is tested elsewhere).
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species == Species::Wolf) a.alive = false;
  }
  WildlifeAgent *female = nullptr, *male = nullptr;
  parkBreedingPair(w, Species::Rabbit, female, male);
  female->metabolism = 1.1;
  male->metabolism = 0.9;
  uint32_t maxId = 0;
  for (const WildlifeAgent& a : w.wildlife().agents()) maxId = std::max(maxId, a.id);
  const int before = countSpecies(w, Species::Rabbit);
  const uint32_t motherId = female->id;
  const Vec2i birthplace = female->pos;
  SimClock c;
  for (int i = 0; i < 20; ++i) {
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
  }
  CHECK(countSpecies(w, Species::Rabbit) > before);
  // The newborn is a juvenile with blended traits, near where it was born.
  bool foundPup = false;
  for (const WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species != Species::Rabbit || !a.alive) continue;
    if (a.id > maxId && a.ageTicks < 5 * 86400 &&
        distCheb(a.pos, birthplace) <= 12) {
      foundPup = true;
      CHECK(a.metabolism >= 0.8 && a.metabolism <= 1.2);
      CHECK(a.hardiness >= 0.8 && a.hardiness <= 1.2);
    }
  }
  CHECK(foundPup);
  bool motherResting = false;
  for (const WildlifeAgent& a : w.wildlife().agents()) {
    if (a.id == motherId && a.offspringCooldownUntil > 0) motherResting = true;
  }
  CHECK(motherResting); // mother rests before next birth
}

TEST(wildlife_wolf_pups_do_not_hunt) {
  // Juveniles forage; only adults stalk prey or the organism.
  World w;
  Rng r(42);
  w.generate(64, 64, r);
  WildlifeAgent* wolf = nullptr;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species == Species::Wolf) { wolf = &a; break; }
  }
  CHECK(wolf);
  wolf->ageTicks = 86400; // 1 day old: pup
  wolf->hunger = 90.0;
  wolf->state = AnimalState::Hunt;
  SimClock c;
  for (int i = 0; i < 10; ++i) {
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
  }
  CHECK(wolf->state != AnimalState::Hunt);
}

TEST(wildlife_old_age_kills_and_feeds_soil) {
  World w;
  Rng r(43);
  w.generate(32, 32, r);
  WildlifeAgent* rabbit = nullptr;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species == Species::Rabbit) { rabbit = &a; break; }
  }
  CHECK(rabbit);
  rabbit->ageTicks = 25 * 86400 + 5 * 60; // past the rabbit grave age
  rabbit->hunger = 0.0;
  rabbit->energy = 100.0;
  const float soilBefore = w.soilAt(rabbit->pos.x, rabbit->pos.y);
  SimClock c;
  w.update(c, Wildlife::kInterval, r);
  CHECK(!rabbit->alive);
  CHECK(w.soilAt(rabbit->pos.x, rabbit->pos.y) > soilBefore); // remains feed soil
}

TEST(wildlife_disease_spreads_and_kills_without_immunity) {
  World w;
  Rng r(44);
  w.generate(128, 128, r); // room for an index case + three neighbors
  // One index case + three naive neighbors, re-parked together each step; the
  // index case is refreshed (a starved untreated body dies in ~2 steps).
  WildlifeAgent* sick = nullptr;
  std::vector<WildlifeAgent*> naive;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species != Species::Rabbit || !a.alive) continue;
    if (!sick) sick = &a;
    else if (naive.size() < 3) naive.push_back(&a);
  }
  CHECK(sick && naive.size() == 3u);
  SimClock c;
  bool caught = false;
  for (int i = 0; i < 30; ++i) {
    sick->alive = true;
    sick->disease = 0.9;
    sick->hunger = 90.0;
    for (WildlifeAgent* h : naive) {
      h->pos = {sick->pos.x + 1, sick->pos.y};
      h->immunity = 0.0;
    }
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
    for (const WildlifeAgent* h : naive) {
      if (h->disease > 0.0) caught = true;
    }
  }
  CHECK(caught); // transmission works
  // And untreated starved disease kills: let one case run its course.
  sick->disease = 0.9;
  sick->hunger = 90.0;
  for (int i = 0; i < 10 && sick->alive; ++i) {
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
  }
  CHECK(!sick->alive);
}

TEST(wildlife_recovery_banks_immunity) {
  World w;
  Rng r(45);
  w.generate(32, 32, r);
  WildlifeAgent* rabbit = nullptr;
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species == Species::Rabbit) { rabbit = &a; break; }
  }
  CHECK(rabbit);
  rabbit->disease = 0.5;
  rabbit->hunger = 0.0; // fed body clears it
  rabbit->immunity = 0.0;
  SimClock c;
  for (int i = 0; i < 60; ++i) {
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
    rabbit->hunger = 0.0; // keep fed (deterministic recovery)
  }
  CHECK(rabbit->disease < 0.1);
  CHECK(rabbit->immunity > 0.0); // recovery taught resistance
}

TEST(wildlife_population_capped) {
  // Even ideal breeding conditions cannot explode past the species cap.
  World w;
  Rng r(46);
  w.generate(48, 48, r);
  for (WildlifeAgent& a : w.wildlife().agents()) {
    if (a.species != Species::Rabbit) continue;
    a.ageTicks = 6 * 86400;
    a.hunger = 0.0;
    a.disease = 0.0;
    a.offspringCooldownUntil = 0;
  }
  SimClock c;
  for (int i = 0; i < 400; ++i) {
    w.update(c, Wildlife::kInterval, r);
    c.advance(Wildlife::kInterval);
  }
  CHECK(countSpecies(w, Species::Rabbit) <= 64);
}

TEST(wildlife_lifestate_survives_roundtrip) {
  World a, b;
  Rng ra(47), rb(47);
  a.generate(48, 48, ra);
  b.generate(48, 48, rb);
  SimClock c;
  for (int i = 0; i < 30; ++i) {
    a.update(c, Wildlife::kInterval, ra);
    c.advance(Wildlife::kInterval);
  }
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK_EQ(a.wildlife().agents().size(), b.wildlife().agents().size());
  for (size_t i = 0; i < a.wildlife().agents().size(); ++i) {
    const WildlifeAgent& x = a.wildlife().agents()[i];
    const WildlifeAgent& y = b.wildlife().agents()[i];
    CHECK_EQ(x.ageTicks, y.ageTicks);
    CHECK_EQ(x.female, y.female);
    CHECK_EQ(x.injury, y.injury);
    CHECK_EQ(x.disease, y.disease);
    CHECK_EQ(x.immunity, y.immunity);
    CHECK_EQ(x.metabolism, y.metabolism);
    CHECK_EQ(x.hardiness, y.hardiness);
  }
}
