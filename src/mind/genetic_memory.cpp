#include "mind/genetic_memory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "mind/belief_ising.hpp"
#include "sim/engine.hpp"

namespace eidolon {

void GeneticMemory::serialize(BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(type));
  w.f32(importance);
  w.i64(tick);
  w.u16(static_cast<uint16_t>(x));
  w.u16(static_cast<uint16_t>(y));
  w.str(summary);
  w.str(lesson);
  w.f32(emotionalValence);
  w.u32(rehearsalCount);
}

bool GeneticMemory::deserialize(BinaryReader& r) {
  uint8_t t;
  if (!r.u8(t)) return false;
  type = static_cast<GeneticMemoryType>(t);
  if (!r.f32(importance)) return false;
  if (!r.i64(tick)) return false;
  uint16_t ux, uy;
  if (!r.u16(ux)) return false;
  x = static_cast<int16_t>(ux);
  if (!r.u16(uy)) return false;
  y = static_cast<int16_t>(uy);
  if (!r.str(summary)) return false;
  if (!r.str(lesson)) return false;
  if (!r.f32(emotionalValence)) return false;
  if (!r.u32(rehearsalCount)) return false;
  return true;
}

void GeneticMemoryBundle::serialize(BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(memories.size()));
  for (const auto& m : memories) m.serialize(w);
  w.u64(parentSeed);
  w.u32(static_cast<uint32_t>(generation));
  w.u64(createdAt);
}

bool GeneticMemoryBundle::deserialize(BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  memories.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    if (!memories[i].deserialize(r)) return false;
  }
  if (!r.u64(parentSeed)) return false;
  uint32_t gen;
  if (!r.u32(gen)) return false;
  generation = static_cast<int>(gen);
  if (!r.u64(createdAt)) return false;
  return true;
}

namespace {
// Retention threshold: inherited memories below importance*weight are dropped.
constexpr float kRetainThreshold = 0.35f;
constexpr size_t kMaxInjected = 32;
// Inherited belief spins stay bounded no matter the bundle size.
constexpr size_t kMaxBeliefs = 24;

EventKind kindForType(GeneticMemoryType t) {
  switch (t) {
    case GeneticMemoryType::DeathCause: return EventKind::Death;
    case GeneticMemoryType::ResourceLocation: return EventKind::Forage;
    case GeneticMemoryType::ThreatLocation:
    case GeneticMemoryType::ThreatPattern: return EventKind::Attack;
    case GeneticMemoryType::SafeLocation: return EventKind::Sleep;
    case GeneticMemoryType::Skill: return EventKind::Forage;
  }
  return EventKind::Forage;
}

const char* typeName(GeneticMemoryType t) {
  switch (t) {
    case GeneticMemoryType::DeathCause: return "death";
    case GeneticMemoryType::ResourceLocation: return "resource";
    case GeneticMemoryType::ThreatLocation: return "threat";
    case GeneticMemoryType::SafeLocation: return "safe spot";
    case GeneticMemoryType::Skill: return "skill";
    case GeneticMemoryType::ThreatPattern: return "threat pattern";
  }
  return "memory";
}
} // namespace

bool GeneticMemorySystem::classifyMemory(const Episode& e, GeneticMemoryType& out) {
  switch (e.kind) {
    case EventKind::Death:
      out = GeneticMemoryType::DeathCause;
      return true;
    case EventKind::Attack:
    case EventKind::NearDeath:
    case EventKind::Illness:
      out = GeneticMemoryType::ThreatLocation;
      return true;
    case EventKind::Forage:
    case EventKind::Drink:
      if (e.outcome != Outcome::Success) return false;
      out = GeneticMemoryType::ResourceLocation;
      return true;
    case EventKind::Tamed:
      out = GeneticMemoryType::SafeLocation;
      return true;
    case EventKind::Sleep:
      if (e.outcome != Outcome::Success) return false;
      out = GeneticMemoryType::SafeLocation;
      return true;
    default:
      return false; // births, weather, wake, recovery: personal, not inheritable
  }
}

float GeneticMemorySystem::computeMemoryImportance(const Episode& e) {
  float imp = static_cast<float>(e.importance);
  const uint8_t rel = static_cast<uint8_t>(e.relevance);
  const uint8_t aversive =
      static_cast<uint8_t>(Relevance::Aversive) | static_cast<uint8_t>(Relevance::Threatening);
  if ((rel & aversive) != 0) imp += 0.2f;
  if (e.outcome == Outcome::Failure) imp += 0.1f;
  imp += std::min(static_cast<float>(e.rehearsalCount) * 0.05f, 0.2f);
  if (imp < 0.0f) imp = 0.0f;
  if (imp > 1.0f) imp = 1.0f;
  return imp;
}

GeneticMemoryBundle GeneticMemorySystem::extractFromEpisodes(
    const std::vector<Episode>& episodes,
    uint64_t parentId,
    int generation,
    int maxMemories) {
  GeneticMemoryBundle bundle;
  bundle.parentSeed = parentId;
  bundle.generation = generation;
  bundle.createdAt = episodes.empty() ? 0 : static_cast<uint64_t>(episodes.back().t);
  if (maxMemories <= 0) return bundle;

  struct Candidate {
    GeneticMemory mem;
  };
  std::vector<Candidate> kept;
  for (const Episode& e : episodes) {
    GeneticMemoryType type = GeneticMemoryType::DeathCause;
    if (!classifyMemory(e, type)) continue;
    const float imp = computeMemoryImportance(e);
    if (imp < kRetainThreshold) continue;
    GeneticMemory m;
    m.type = type;
    m.importance = imp;
    m.tick = e.t;
    m.x = e.x;
    m.y = e.y;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "Predecessor (gen %d) met %s at (%d,%d), tick %lld.",
                  generation, typeName(type), static_cast<int>(e.x),
                  static_cast<int>(e.y), static_cast<long long>(e.t));
    m.summary = buf;
    switch (type) {
      case GeneticMemoryType::ResourceLocation:
        m.lesson = "Food or water was found here; the world persists, so it may be found again.";
        break;
      case GeneticMemoryType::ThreatLocation:
        m.lesson = "Danger was met here; approach with caution.";
        break;
      case GeneticMemoryType::SafeLocation:
        m.lesson = "Rest here proved safe before.";
        break;
      default:
        m.lesson = "Remember this experience of my predecessor.";
        break;
    }
    m.emotionalValence = e.emotionalValence;
    m.rehearsalCount = e.rehearsalCount;
    kept.push_back({m});
  }
  // Deterministic order: importance desc, then recent first, then position.
  std::sort(kept.begin(), kept.end(), [](const Candidate& a, const Candidate& b) {
    if (a.mem.importance != b.mem.importance) return a.mem.importance > b.mem.importance;
    if (a.mem.tick != b.mem.tick) return a.mem.tick > b.mem.tick;
    if (a.mem.x != b.mem.x) return a.mem.x < b.mem.x;
    return a.mem.y < b.mem.y;
  });
  const size_t n = std::min(kept.size(), static_cast<size_t>(maxMemories));
  for (size_t i = 0; i < n; ++i) bundle.memories.push_back(kept[i].mem);
  return bundle;
}

GeneticMemory GeneticMemorySystem::makeDeathMemory(
    const std::string& cause,
    int generation,
    int16_t x,
    int16_t y,
    int64_t tick,
    double hunger,
    double thirst) {
  GeneticMemory m;
  m.type = GeneticMemoryType::DeathCause;
  m.importance = 0.9f;
  m.tick = tick;
  m.x = x;
  m.y = y;
  m.emotionalValence = -0.8f;
  m.rehearsalCount = 0;
  char buf[256];
  if (cause == "starvation") {
    std::snprintf(buf, sizeof(buf),
                  "Predecessor (gen %d) starved at tick %lld with hunger %.0f.",
                  generation, static_cast<long long>(tick), hunger);
    m.summary = buf;
    m.lesson =
        "It died hungry, not somewhere lethal: keep food stores and eat before hunger "
        "passes 70. The place it died tells nothing by itself.";
  } else if (cause == "dehydration") {
    std::snprintf(buf, sizeof(buf),
                  "Predecessor (gen %d) died of thirst at tick %lld with thirst %.0f.",
                  generation, static_cast<long long>(tick), thirst);
    m.summary = buf;
    m.lesson =
        "It died thirsty, not somewhere lethal: drink before thirst passes 55 and keep "
        "water carried. Dying near water does not make water dangerous.";
  } else if (cause == "predator_attack") {
    std::snprintf(buf, sizeof(buf),
                  "Predecessor (gen %d) was killed by a predator near (%d,%d) at tick %lld.",
                  generation, static_cast<int>(x), static_cast<int>(y),
                  static_cast<long long>(tick));
    m.summary = buf;
    m.lesson =
        "A predator hunts around these coordinates; only here is the location genuine "
        "evidence. Flee early and keep distance from predators.";
  } else if (cause == "energy_depletion") {
    std::snprintf(buf, sizeof(buf),
                  "Predecessor (gen %d) ran out of energy at tick %lld.", generation,
                  static_cast<long long>(tick));
    m.summary = buf;
    m.lesson =
        "It exhausted itself: rest before energy collapses and eat to recover. The place "
        "it fell tells nothing by itself.";
  } else if (cause == "temperature_extreme") {
    std::snprintf(buf, sizeof(buf),
                  "Predecessor (gen %d) died of cold/heat at tick %lld.", generation,
                  static_cast<long long>(tick));
    m.summary = buf;
    m.lesson =
        "It died of exposure: seek shelter, fire and warmth in storms and winter. The "
        "place it died tells nothing by itself.";
  } else if (cause == "disease") {
    std::snprintf(buf, sizeof(buf), "Predecessor (gen %d) died sick at tick %lld.",
                  generation, static_cast<long long>(tick));
    m.summary = buf;
    m.lesson =
        "It died of illness: avoid swamp water, treat wounds, rest while sick. The place "
        "it died tells nothing by itself.";
  } else {
    std::snprintf(buf, sizeof(buf),
                  "Predecessor (gen %d) died of unknown causes at tick %lld.", generation,
                  static_cast<long long>(tick));
    m.summary = buf;
    m.lesson = "No lesson is certain; stay fed, watered, rested and warm.";
  }
  return m;
}

size_t GeneticMemorySystem::applyToOrganism(
    Engine& engine,
    const std::vector<GeneticMemory>& memories,
    uint64_t parentId,
    float inheritanceWeight) {
  if (inheritanceWeight <= 0.0f || parentId == 0) return 0;
  if (inheritanceWeight > 1.0f) inheritanceWeight = 1.0f;
  size_t n = 0;
  for (const GeneticMemory& m : memories) {
    if (n >= kMaxInjected) break;
    const float imp = m.importance * inheritanceWeight;
    if (imp < kRetainThreshold) continue;
    Episode e;
    e.t = engine.clock().now();
    e.x = m.x;
    e.y = m.y;
    e.kind = kindForType(m.type);
    e.action = 255;
    // NOT Participant::Self: this was lived by the predecessor, not by me.
    e.participants = Participant::None;
    e.outcome = (m.type == GeneticMemoryType::DeathCause ||
                 m.type == GeneticMemoryType::ThreatLocation ||
                 m.type == GeneticMemoryType::ThreatPattern)
                    ? Outcome::Failure
                    : Outcome::Success;
    e.prediction = 0.0f;
    e.predictionError = 0.0f;
    e.emotionalValence = m.emotionalValence;
    e.socialRelevance = 0.0f;
    e.relevance = (m.type == GeneticMemoryType::DeathCause ||
                   m.type == GeneticMemoryType::ThreatLocation)
                      ? (Relevance::Aversive | Relevance::Threatening)
                      : Relevance::None;
    e.importance = imp;
    e.detail = 0;
    e.rehearsalCount = m.rehearsalCount;
    e.consolidated = false;
    e.sourceIndividualId = parentId;
    engine.memorySys().ring().add(e);
    ++n;
    // E3-slice-2e: plant place-anchored inherited lore as weakly-held belief
    // spins. Only evidence-shaped place claims qualify: Threat/Resource/Safe
    // locations. DeathCause lessons stay episodes-only (cause-specific prose
    // cannot anchor a place claim — slice-1 evidence rule), and Skill memories
    // never become competence (descriptions are not practiced skills).
    if (engine.beliefs().size() < kMaxBeliefs &&
        (m.type == GeneticMemoryType::ThreatLocation ||
         m.type == GeneticMemoryType::ResourceLocation ||
         m.type == GeneticMemoryType::SafeLocation)) {
      char desc[160];
      if (m.type == GeneticMemoryType::ThreatLocation) {
        std::snprintf(desc, sizeof(desc),
                      "Predecessor met predators near (%d,%d) — that area may be dangerous",
                      static_cast<int>(m.x), static_cast<int>(m.y));
      } else if (m.type == GeneticMemoryType::ResourceLocation) {
        std::snprintf(desc, sizeof(desc),
                      "Predecessor found food or water near (%d,%d)",
                      static_cast<int>(m.x), static_cast<int>(m.y));
      } else {
        std::snprintf(desc, sizeof(desc), "Predecessor rested safely near (%d,%d)",
                      static_cast<int>(m.x), static_cast<int>(m.y));
      }
      const size_t bi = engine.beliefs().add_belief(
          desc, +1, static_cast<int>(m.type), m.x, m.y);
      engine.beliefs().apply_evidence(bi, 0.8f); // inherited: held, weakly
      engine.beliefs().set_certainty(bi, 0.4f);
    }
  }
  return n;
}

std::vector<const GeneticMemory*> GeneticMemorySystem::getDeathMemories(
    const GeneticMemoryBundle& bundle) {

  std::vector<const GeneticMemory*> results;
  for (const auto& mem : bundle.memories) {
    if (mem.type == GeneticMemoryType::DeathCause) {
      results.push_back(&mem);
    }
  }
  return results;
}

namespace {
// Deliberate lived acts (not weather drift-bys): presence-with-purpose near an
// anchor counts as evidence about that place.
bool isPeacefulAct(EventKind k) noexcept {
  switch (k) {
    case EventKind::Forage:
    case EventKind::Drink:
    case EventKind::Sleep:
    case EventKind::Wake:
    case EventKind::Recovery:
    case EventKind::Tamed:
      return true;
    default:
      return false;
  }
}

bool isHarm(EventKind k) noexcept {
  return k == EventKind::Attack || k == EventKind::NearDeath;
}

int chebDist(int16_t x1, int16_t y1, int16_t x2, int16_t y2) noexcept {
  const int dx = x1 >= x2 ? x1 - x2 : x2 - x1;
  const int dy = y1 >= y2 ? y1 - y2 : y2 - y1;
  return dx >= dy ? dx : dy;
}
} // namespace

size_t GeneticMemorySystem::reviseInheritedBeliefs(
    BeliefIsingModel& beliefs, const std::vector<Episode>& ring) noexcept {
  size_t revised = 0;
  for (size_t i = 0; i < beliefs.size(); ++i) {
    const int anchor = beliefs.anchorKind(i);
    if (anchor < 0 || beliefs.get_state(i) != 1) continue;
    const int16_t ax = beliefs.anchorX(i);
    const int16_t ay = beliefs.anchorY(i);
    float delta = 0.0f;
    for (const Episode& e : ring) {
      if (e.sourceIndividualId != 0) continue; // only my own life revises
      const int d = chebDist(e.x, e.y, ax, ay);
      if (anchor == static_cast<int>(GeneticMemoryType::ThreatLocation)) {
        if (d <= 3 && isPeacefulAct(e.kind)) {
          delta -= 0.25f; // lived safely where danger was claimed
        } else if (d <= 5 && isHarm(e.kind)) {
          delta += 0.3f; // the warning checks out
        }
      } else if (anchor == static_cast<int>(GeneticMemoryType::SafeLocation)) {
        if (d <= 3 && isHarm(e.kind)) {
          delta -= 0.5f; // hurt where safety was claimed
        }
      } else if (anchor == static_cast<int>(GeneticMemoryType::ResourceLocation)) {
        if (d <= 3 && (e.kind == EventKind::Forage || e.kind == EventKind::Drink) &&
            e.detail > 0) {
          delta += 0.2f; // harvests confirm, never flip (absence proves nothing)
        }
      }
    }
    if (delta != 0.0f) beliefs.apply_evidence(i, delta);
    // Deterministic resolve: sustained counter-evidence flips the claim;
    // sustained confirmation deepens certainty. No RNG — explainable.
    if (delta < 0.0f && beliefs.field(i) <= -0.5f) {
      beliefs.set_state(i, -1);
      beliefs.set_certainty(i, 0.6f);
      ++revised;
    } else if (delta > 0.0f && beliefs.field(i) >= 1.5f) {
      beliefs.set_certainty(i, beliefs.get_certainty(i) + 0.1f);
    }
  }
  return revised;
}

} // namespace eidolon
