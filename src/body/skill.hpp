#ifndef EIDOLON_SKILL_HPP
#define EIDOLON_SKILL_HPP

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <cmath>

#include "core/rng.hpp"
#include "core/serialize.hpp"

namespace eidolon {

// Skill models for Phase 6: Beta/Bernoulli competence, procedural store, habit formation.
// Fully deterministic, seeded, serializable.

enum class SkillType : uint8_t {
  None = 0,
  // Core survival skills
  FireMaking = 1,
  Knapping = 2,      // stone tool making
  Woodworking = 3,
  Foraging = 4,
  Hunting = 5,
  ShelterBuilding = 6,
  Cooking = 7,
  ToolUse = 8,
  Tracking = 9,
  Navigation = 10,
  // Construction skills
  WallBuilding = 11,
  Farming = 12,
  Storage = 13,
  // Advanced
  ToolInvention = 14,
  RecipeDiscovery = 15,
  Count = 16
};

// Beta distribution for skill competence (Bayesian skill model)
// alpha = successes + 1, beta = failures + 1
// Mean = alpha / (alpha + beta), variance decreases with experience
struct SkillCompetence {
  uint32_t alpha = 1;  // successes + 1
  uint32_t beta = 1;   // failures + 1

  SkillCompetence() = default;
  SkillCompetence(uint32_t a, uint32_t b) : alpha(a), beta(b) {}

  // Mean competence [0, 1]
  float mean() const {
    return static_cast<float>(alpha) / static_cast<float>(alpha + beta);
  }

  // Variance of the Beta distribution
  float variance() const {
    float a = static_cast<float>(alpha);
    float b = static_cast<float>(beta);
    float sum = a + b;
    return (a * b) / (sum * sum * (sum + 1.0f));
  }

  // Confidence (inverse of variance, scaled)
  float confidence() const {
    return 1.0f - std::sqrt(variance());
  }

  // Record a success
  void recordSuccess() { alpha++; }

  // Record a failure
  void recordFailure() { beta++; }

  // Combine with another competence (for teaching/learning)
  void combine(const SkillCompetence& other, float weight = 0.5f) {
    alpha = static_cast<uint32_t>(alpha * (1.0f - weight) + other.alpha * weight);
    beta = static_cast<uint32_t>(beta * (1.0f - weight) + other.beta * weight);
    if (alpha == 0) alpha = 1;
    if (beta == 0) beta = 1;
  }

  void serialize(struct BinaryWriter& w) const {
    w.u32(alpha);
    w.u32(beta);
  }

  bool deserialize(struct BinaryReader& r) {
    return r.u32(alpha) && r.u32(beta);
  }
};

// Bernoulli skill check: succeeds with probability = competence.mean()
// Used for probabilistic skill checks in crafting/construction
bool skillCheck(const SkillCompetence& skill, class Rng& rng);

// Procedural skill store: maps skill type -> competence
class SkillStore {
public:
  SkillStore() {
    skills_.fill(SkillCompetence());
  }

  SkillCompetence& skill(SkillType type) {
    return skills_[static_cast<size_t>(type)];
  }
  const SkillCompetence& skill(SkillType type) const {
    return skills_[static_cast<size_t>(type)];
  }

  // Get all skills with competence above threshold
  std::vector<SkillType> proficientSkills(float threshold = 0.5f) const;

  // Improve skill from practice
  void practice(SkillType type, bool success) {
    if (success) skills_[static_cast<size_t>(type)].recordSuccess();
    else skills_[static_cast<size_t>(type)].recordFailure();
  }

  // Teach skill to another store (social learning)
  void teach(SkillStore& other, SkillType type, float weight = 0.3f) {
    other.skills_[static_cast<size_t>(type)].combine(skills_[static_cast<size_t>(type)], weight);
  }

  void serialize(struct BinaryWriter& w) const {
    w.u8(static_cast<uint8_t>(SkillType::Count));
    for (size_t i = 0; i < static_cast<size_t>(SkillType::Count); ++i) {
      skills_[i].serialize(w);
    }
  }

  bool deserialize(struct BinaryReader& r) {
    uint8_t count;
    if (!r.u8(count)) return false;
    if (count != static_cast<uint8_t>(SkillType::Count)) return false;
    for (size_t i = 0; i < static_cast<size_t>(SkillType::Count); ++i) {
      if (!skills_[i].deserialize(r)) return false;
    }
    return true;
  }

private:
  std::array<SkillCompetence, static_cast<size_t>(SkillType::Count)> skills_;
};

// Habit: automated action sequence triggered by context
// Habits form through repetition (high competence + consistent context)
struct Habit {
  uint32_t id = 0;
  std::string name;
  std::vector<uint8_t> actionSequence; // encoded actions
  std::vector<uint8_t> contextSignature; // context features that trigger habit
  uint32_t repetitions = 0;
  float strength = 0.0f; // 0..1, increases with repetitions
  bool active = true;

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Habit store: manages habit formation and triggering
class HabitStore {
public:
  HabitStore() = default;

  // Try to form habit from repeated action sequence in similar context
  void maybeFormHabit(const std::vector<uint8_t>& actions, const std::vector<uint8_t>& context);

  // Check if a habit triggers in current context
  const Habit* checkTrigger(const std::vector<uint8_t>& context) const;

  // Execute habit (returns action sequence)
  const std::vector<uint8_t>* executeHabit(const Habit* habit) const;

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  std::vector<Habit> habits_;
  uint32_t nextId_ = 1;
};

} // namespace eidolon

#endif // EIDOLON_SKILL_HPP