#include "mind/memory.hpp"
#include "mind/memory_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "mind/learn.hpp"
#include "mind/personality.hpp"
#include "mind/archive.hpp"

namespace eidolon {

MemorySystem::MemorySystem(size_t ringCapacity) : ring_(ringCapacity) {}

void MemorySystem::tickDecay() {
  for (Episode& e : ring_.eps_) {
    if (!e.consolidated) {
      e.importance *= 0.9999;
      if (e.importance < 1e-4) e.importance = 0.0;
    }
  }
}

std::vector<const Episode*> MemorySystem::retrieve(size_t k, float relevanceBoost) const {
  std::vector<std::pair<float, size_t>> scored;
  scored.reserve(ring_.eps_.size());
  int64_t now = 0; // TODO: pass current tick from engine
  for (size_t i = 0; i < ring_.eps_.size(); ++i) {
    const Episode& e = ring_.eps_[i];
    if (e.importance <= 0.0f) continue;
    float recency = 1.0f / (1.0f + 0.001f * (now - e.t));
    float relevance = 1.0f;
    if (static_cast<uint8_t>(e.relevance) & static_cast<uint8_t>(Relevance::Rewarding))
      relevance += 0.5f;
    if (static_cast<uint8_t>(e.relevance) & static_cast<uint8_t>(Relevance::Aversive))
      relevance += 0.3f;
    if (static_cast<uint8_t>(e.relevance) & static_cast<uint8_t>(Relevance::GoalRelated))
      relevance += 0.4f;
    if (static_cast<uint8_t>(e.relevance) & static_cast<uint8_t>(Relevance::Social))
      relevance *= relevanceBoost;
    float score = static_cast<float>(e.importance) * recency * relevance;
    scored.emplace_back(score, i);
  }
  std::partial_sort(scored.begin(),
                    scored.begin() + std::min(k, scored.size()),
                    scored.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<const Episode*> result;
  result.reserve(std::min(k, scored.size()));
  for (size_t i = 0; i < std::min(k, scored.size()); ++i) {
    result.push_back(&ring_.eps_[scored[i].second]);
  }
  return result;
}

void MemorySystem::strengthen(size_t index, float amount) {
  if (index < ring_.eps_.size()) {
    ring_.eps_[index].importance = std::min(1.0, ring_.eps_[index].importance + amount);
  }
}

void MemorySystem::consolidate(const LearnSystem& learn, Archive* archive) {
  // 1) Replay: iterate recent high-importance episodes
  // 2) Skill rehearsal: boost policy weights for successful action sequences
  // 3) Goal processing: extract goal-relevant episodes
  // 4) Summarization: create compressed summaries
  // 4) Association updates: link related episodes
  // 5) Archive consolidated episodes; mark ring entries as consolidated
  for (Episode& e : ring_.eps_) {
    if (!e.consolidated && e.importance > 0.3) {
      e.consolidated = true;
      e.rehearsalCount++;
      if (archive) archive->episode(e);
    }
  }
  // Decay non-consolidated after consolidation pass
  for (Episode& e : ring_.eps_) {
    if (!e.consolidated) e.importance *= 0.5;
  }
}

void MemorySystem::dream(const LearnSystem& learn) {
  // Dreams v1: associative recombination.
  // Randomly pair two recent episodes; if they share location, participants, or
  // outcome type, create a "dream trace" that slightly perturbs the policy
  // toward the recombined action sequence. No LLM involved.
}

void MemorySystem::serialize(BinaryWriter& w) const {
  ring_.serialize(w);
}

bool MemorySystem::deserialize(BinaryReader& r) {
  return ring_.deserialize(r);
}

} // namespace eidolon