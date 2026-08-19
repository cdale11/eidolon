#include "body/skill.hpp"
#include <algorithm>
#include <cmath>

namespace eidolon {

bool skillCheck(const SkillCompetence& skill, class Rng& rng) {
  float p = skill.mean();
  return rng.range(0.0, 1.0) < static_cast<double>(p);
}

std::vector<SkillType> SkillStore::proficientSkills(float threshold) const {
  std::vector<SkillType> result;
  for (size_t i = 0; i < static_cast<size_t>(SkillType::Count); ++i) {
    if (skills_[i].mean() >= threshold) {
      result.push_back(static_cast<SkillType>(i));
    }
  }
  return result;
}

void Habit::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.str(name);
  w.u32(static_cast<uint32_t>(actionSequence.size()));
  for (uint8_t a : actionSequence) w.u8(a);
  w.u32(static_cast<uint32_t>(contextSignature.size()));
  for (uint8_t c : contextSignature) w.u8(c);
  w.u32(repetitions);
  w.f32(strength);
  w.u8(active ? 1 : 0);
}

bool Habit::deserialize(struct BinaryReader& r) {
  if (!r.u32(id) || !r.str(name)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  actionSequence.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u8(actionSequence[i])) return false;
  }
  if (!r.u32(n)) return false;
  contextSignature.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u8(contextSignature[i])) return false;
  }
  if (!r.u32(repetitions) || !r.f32(strength)) return false;
  uint8_t a;
  if (!r.u8(a)) return false;
  active = a != 0;
  return true;
}

void HabitStore::maybeFormHabit(const std::vector<uint8_t>& actions, const std::vector<uint8_t>& context) {
  if (actions.size() < 3) return; // minimum sequence length

  // Check if similar habit already exists
  for (auto& h : habits_) {
    if (h.contextSignature == context) {
      h.repetitions++;
      h.strength = std::min(1.0f, static_cast<float>(h.repetitions) / 50.0f);
      return;
    }
  }

  // Create new habit
  Habit h;
  h.id = nextId_++;
  h.name = "habit_" + std::to_string(h.id);
  h.actionSequence = actions;
  h.contextSignature = context;
  h.repetitions = 1;
  h.strength = 0.02f;
  h.active = true;
  habits_.push_back(h);
}

const Habit* HabitStore::checkTrigger(const std::vector<uint8_t>& context) const {
  for (const auto& h : habits_) {
    if (!h.active) continue;
    if (h.contextSignature == context && h.strength > 0.5f) {
      return &h;
    }
  }
  return nullptr;
}

const std::vector<uint8_t>* HabitStore::executeHabit(const Habit* habit) const {
  if (!habit || !habit->active) return nullptr;
  return &habit->actionSequence;
}

void HabitStore::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(habits_.size()));
  for (const auto& h : habits_) {
    h.serialize(w);
  }
  w.u32(nextId_);
}

bool HabitStore::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  habits_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!habits_[i].deserialize(r)) return false;
  }
  if (!r.u32(nextId_)) return false;
  return true;
}

} // namespace eidolon