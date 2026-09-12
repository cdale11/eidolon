#include "harness.hpp"

#include <unistd.h>

#include <cstdio>
#include <string>

#include "mind/genetic_memory.hpp"
#include "mind/heredity.hpp"
#include "sim/engine.hpp"

using namespace eidolon;

namespace {
// Lethal damage + one tick runs the engine death block (heredity file is saved
// only when a heredity path was set beforehand).
void killForHeredityTest(Engine& e) {
  e.body().takeDamage(100000.0);
  e.tick();
  CHECK(!e.isAlive());
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}
} // namespace

TEST(heredity_death_lessons_are_evidenced) {
  // Location is evidence ONLY for predator attacks; every other cause must
  // explicitly rule the place out (a death near water must not teach that
  // water is lethal).
  const int16_t x = 21, y = 13;
  GeneticMemory starved =
      GeneticMemorySystem::makeDeathMemory("starvation", 2, x, y, 66343, 100.0, 7.0);
  CHECK(contains(starved.summary, "gen 2"));
  CHECK(contains(starved.lesson, "tells nothing"));
  CHECK(!contains(starved.lesson, "(21,13)"));

  GeneticMemory thirsty =
      GeneticMemorySystem::makeDeathMemory("dehydration", 0, x, y, 100, 5.0, 100.0);
  CHECK(contains(thirsty.lesson, "does not make water dangerous"));

  GeneticMemory mauled =
      GeneticMemorySystem::makeDeathMemory("predator_attack", 1, x, y, 500, 10.0, 10.0);
  CHECK(contains(mauled.summary, "(21,13)"));
  CHECK(contains(mauled.lesson, "genuine evidence"));

  GeneticMemory unknown =
      GeneticMemorySystem::makeDeathMemory("mystery", 0, x, y, 10, 0.0, 0.0);
  CHECK(contains(unknown.lesson, "No lesson is certain"));

  for (const char* cause : {"energy_depletion", "temperature_extreme", "disease"}) {
    GeneticMemory m = GeneticMemorySystem::makeDeathMemory(cause, 0, x, y, 10, 0.0, 0.0);
    CHECK(contains(m.lesson, "tells nothing"));
  }
}

TEST(heredity_extract_metadata_is_honest) {
  Engine e;
  e.init(42, true, 64, 64);
  for (int i = 0; i < 200 && e.isAlive(); ++i) e.tick();
  CHECK(e.isAlive());

  char pathbuf[128];
  std::snprintf(pathbuf, sizeof(pathbuf), "/tmp/eidolon_test_heredity_%d.hrd",
                static_cast<int>(::getpid()));
  e.setHeredityPath(pathbuf);
  killForHeredityTest(e);
  const uint64_t deathTick = e.clock().now();

  HeredityGenome genome;
  CHECK(HeredityManager::loadHeredity(genome, pathbuf));
  std::remove(pathbuf);

  // Identity: this individual's generation and id, never hardcoded zeros.
  CHECK_EQ(genome.generation, 0);
  CHECK(genome.parentSeed != 0u);
  CHECK_EQ(genome.parentSeed, e.individualId());
  // Lifespan: death minus this individual's own birth (was: deathTick always).
  CHECK_EQ(genome.deathTick, deathTick);
  CHECK_EQ(genome.lifespanTicks, deathTick - static_cast<uint64_t>(e.birthTick()));
  CHECK(!genome.causeOfDeath.empty());
  // Death memory first, evidenced, bounded bundle.
  CHECK(!genome.geneticMemories.empty());
  CHECK(genome.geneticMemories.size() <= 32u);
  const GeneticMemory& death = genome.geneticMemories.front();
  CHECK(death.type == GeneticMemoryType::DeathCause);
  CHECK(death.importance > 0.8f);
}

TEST(heredity_bundle_extract_apply_roundtrip) {
  Engine parent;
  parent.init(7, true, 64, 64);
  for (int i = 0; i < 400 && parent.isAlive(); ++i) parent.tick();
  CHECK(parent.isAlive());

  const uint64_t parentId = parent.individualId();
  GeneticMemoryBundle bundle = GeneticMemorySystem::extractFromEpisodes(
      parent.memory().episodes(), parentId, 0, 32);
  CHECK(bundle.parentSeed == parentId);
  CHECK(bundle.memories.size() <= 32u);
  // Deterministic order: importance desc.
  for (size_t i = 1; i < bundle.memories.size(); ++i) {
    CHECK(bundle.memories[i - 1].importance >= bundle.memories[i].importance);
  }

  Engine child;
  child.init(7, true, 64, 64);
  const size_t before = child.memory().size();
  const size_t injected = GeneticMemorySystem::applyToOrganism(
      child, bundle.memories, parentId, 0.7f);
  CHECK_EQ(injected, child.memory().size() - before);
  // Attribution: inherited episodes are marked with the parent's id and are
  // never recorded as the successor's own experience.
  for (const Episode& ep : child.memory().episodes()) {
    if (ep.sourceIndividualId != 0) {
      CHECK_EQ(ep.sourceIndividualId, parentId);
      CHECK(ep.participants == Participant::None);
    }
  }
  // Zero parent id or zero weight injects nothing.
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(child, bundle.memories, 0, 0.7f), 0u);
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(child, bundle.memories, parentId, 0.0f), 0u);
}

TEST(heredity_successor_applies_parent_genome) {
  Engine parent;
  parent.init(11, true, 64, 64);
  for (int i = 0; i < 300 && parent.isAlive(); ++i) parent.tick();
  killForHeredityTest(parent);
  HeredityGenome genome =
      HeredityManager::extractGenome(parent, parent.clock().now(), "starvation");

  Engine child;
  child.init(11, true, 64, 64);
  HeredityManager::applyHeredity(child, genome, 0.7f);

  // The predecessor's death arrives as an attributed memory, not a lived one.
  CHECK(child.memory().countKind(EventKind::Death) >= 1u);
  bool attributed = false;
  for (const Episode& ep : child.memory().episodes()) {
    if (ep.sourceIndividualId == parent.individualId()) attributed = true;
    CHECK(ep.sourceIndividualId == 0 || ep.sourceIndividualId == parent.individualId());
  }
  CHECK(attributed);
}

TEST(heredity_attribution_survives_snapshot) {
  Engine parent;
  parent.init(13, true, 64, 64);
  // Run until the ring holds inheritable experience (bounded; deterministic).
  GeneticMemoryBundle bundle;
  for (int i = 0; i < 3000 && parent.isAlive() && bundle.memories.empty(); ++i) {
    parent.tick();
    bundle = GeneticMemorySystem::extractFromEpisodes(parent.memory().episodes(),
                                                      parent.individualId(), 0, 32);
  }
  CHECK(parent.isAlive());
  CHECK(!bundle.memories.empty());

  Engine child;
  child.init(13, true, 64, 64);
  const uint64_t parentId = parent.individualId();
  CHECK(GeneticMemorySystem::applyToOrganism(child, bundle.memories, parentId, 1.0f) > 0u);

  std::string err;
  Engine restored;
  CHECK(restored.restore(child.snapshot(), err));
  bool attributed = false;
  for (const Episode& ep : restored.memory().episodes()) {
    if (ep.sourceIndividualId == parentId) attributed = true;
  }
  CHECK(attributed);
}

TEST(successor_retains_attributed_predecessor_stats) {
  // E3-slice-2d: death → genome file → true succession. The successor carries
  // the previous life's stats as attributed history (never autobiography),
  // and they survive a snapshot roundtrip (v19).
  char path[128];
  std::snprintf(path, sizeof(path), "/tmp/eidolon_pred_%d.hrd",
                static_cast<int>(::getpid()));
  std::remove(path);

  Engine e;
  e.init(42, true, 64, 64);
  e.setHeredityPath(path);
  const uint64_t parentId = e.individualId();
  killForHeredityTest(e);
  CHECK(e.predecessor().hasPredecessor == false); // corpse has no successor data
  CHECK(e.respawnSuccessor());
  CHECK(e.isAlive());
  CHECK(e.predecessor().hasPredecessor);
  CHECK_EQ(e.predecessor().parentId, parentId);
  CHECK_EQ(e.predecessor().causeOfDeath, std::string("predator_attack"));
  CHECK(e.predecessor().lifespanTicks > 0u);
  // The successor's own stats/identity are fresh, not copied.
  CHECK(e.individualId() != parentId);
  CHECK_EQ(e.generation(), 1u);

  std::string err;
  Engine restored;
  CHECK(restored.restore(e.snapshot(), err));
  CHECK(restored.predecessor().hasPredecessor);
  CHECK_EQ(restored.predecessor().parentId, parentId);
  CHECK_EQ(restored.predecessor().causeOfDeath, std::string("predator_attack"));
  std::remove(path);
}

namespace {
// One inheritable place-lore memory for revision tests.
GeneticMemory threatLore(int16_t x, int16_t y) {
  GeneticMemory m;
  m.type = GeneticMemoryType::ThreatLocation;
  m.importance = 1.0f;
  m.x = x;
  m.y = y;
  m.lesson = "predators near here";
  return m;
}

void addLived(Engine& e, EventKind kind, int16_t x, int16_t y, uint8_t detail = 0) {
  Episode ep;
  ep.t = 1000;
  ep.x = x;
  ep.y = y;
  ep.kind = kind;
  ep.detail = detail;
  ep.sourceIndividualId = 0; // lived, not inherited
  e.memorySys().ring().add(ep);
}
} // namespace

TEST(inherited_threat_lore_plants_anchored_belief) {
  // E3-slice-2e: ThreatLocation inheritance plants a weakly-held, anchored
  // belief spin — descriptions, never competence.
  Engine e;
  e.init(42, true, 64, 64);
  CHECK_EQ(e.beliefs().size(), 0u);
  const std::vector<GeneticMemory> mems = {threatLore(10, 10)};
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(e, mems, 4242, 1.0f), 1u);
  CHECK_EQ(e.beliefs().size(), 1u);
  CHECK_EQ(e.beliefs().get_state(0), 1);
  CHECK_EQ(e.beliefs().anchorKind(0), static_cast<int>(GeneticMemoryType::ThreatLocation));
  CHECK_EQ(e.beliefs().anchorX(0), 10);
  CHECK_EQ(e.beliefs().anchorY(0), 10);
  CHECK(e.beliefs().get_description(0).find("(10,10)") != std::string::npos);
  // Anchors survive the snapshot (v20).
  std::string err;
  Engine restored;
  CHECK(restored.restore(e.snapshot(), err));
  CHECK_EQ(restored.beliefs().size(), 1u);
  CHECK_EQ(restored.beliefs().anchorKind(0),
           static_cast<int>(GeneticMemoryType::ThreatLocation));
}

TEST(lived_safety_revises_inherited_threat_belief) {
  // E3-slice-2e: sustained peaceful acts where danger was claimed flip the
  // belief to rejected; the flip count is reported.
  Engine e;
  e.init(42, true, 64, 64);
  const std::vector<GeneticMemory> mems = {threatLore(10, 10)};
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(e, mems, 4242, 1.0f), 1u);
  for (int i = 0; i < 8; ++i) addLived(e, EventKind::Forage, 11, 12);
  const size_t revised = GeneticMemorySystem::reviseInheritedBeliefs(
      e.beliefs(), e.memorySys().ring().episodes());
  CHECK_EQ(revised, 1u);
  CHECK_EQ(e.beliefs().get_state(0), -1);
}

TEST(lived_harm_confirms_inherited_threat_belief) {
  // Counter-case: attacks near the anchor confirm the warning — no flip.
  Engine e;
  e.init(42, true, 64, 64);
  const std::vector<GeneticMemory> mems = {threatLore(10, 10)};
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(e, mems, 4242, 1.0f), 1u);
  for (int i = 0; i < 3; ++i) addLived(e, EventKind::Attack, 10, 11);
  const size_t revised = GeneticMemorySystem::reviseInheritedBeliefs(
      e.beliefs(), e.memorySys().ring().episodes());
  CHECK_EQ(revised, 0u);
  CHECK_EQ(e.beliefs().get_state(0), 1);
}

TEST(inherited_records_never_revise_beliefs) {
  // Only lived experience (sourceIndividualId == 0) counts as evidence.
  Engine e;
  e.init(42, true, 64, 64);
  const std::vector<GeneticMemory> mems = {threatLore(10, 10)};
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(e, mems, 4242, 1.0f), 1u);
  for (int i = 0; i < 8; ++i) {
    Episode ep;
    ep.t = 1000;
    ep.x = 11;
    ep.y = 12;
    ep.kind = EventKind::Forage;
    ep.sourceIndividualId = 4242; // predecessor's record, not my life
    e.memorySys().ring().add(ep);
  }
  CHECK_EQ(GeneticMemorySystem::reviseInheritedBeliefs(
               e.beliefs(), e.memorySys().ring().episodes()),
           0u);
  CHECK_EQ(e.beliefs().get_state(0), 1);
}

TEST(inherited_skill_memories_never_become_competence) {
  // Descriptions are not practiced skills: the skill store stays untouched.
  Engine e;
  e.init(42, true, 64, 64);
  GeneticMemory m;
  m.type = GeneticMemoryType::Skill;
  m.importance = 1.0f;
  m.lesson = "forage near water";
  const std::vector<GeneticMemory> mems = {m};
  CHECK_EQ(GeneticMemorySystem::applyToOrganism(e, mems, 4242, 1.0f), 1u);
  const SkillCompetence& c = e.skills().skill(SkillType::Foraging);
  CHECK_EQ(c.alpha + c.beta - 2, 0u); // zero trials: never attempted
  CHECK(e.beliefs().size() == 0u);    // skill lore plants no belief either
}
