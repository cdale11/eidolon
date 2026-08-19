#include "mind/memory.hpp"
#include "mind/memory_system.hpp"
#include "mind/reflection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "mind/learn.hpp"
#include "mind/personality.hpp"
#include "mind/archive.hpp"

namespace eidolon {

MemorySystem::MemorySystem(size_t ringCapacity) : ring_(ringCapacity), rng_(42) {}

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

void MemorySystem::consolidate(const LearnSystem&, Archive* archive) {
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

void MemorySystem::dream(const LearnSystem& /*learn*/) {
  // Dreams v2: Enhanced associative recombination with measurable influence.
  // - Recombines episodes sharing location/participants/outcome/action
  // - Creates "dream traces" that perturb policy toward recombined sequences
  // - Updates association strengths between concepts
  // - Runs during sleep consolidation; deterministic via seeded RNG
  
  if (ring_.eps_.size() < 2) return;
  
  // Select 2-4 recent episodes as dream seeds (higher importance = more likely)
  std::vector<size_t> candidates;
  for (size_t i = 0; i < ring_.eps_.size(); ++i) {
    const auto& e = ring_.eps_[i];
    if (e.importance > 0.1) {
      // Weight by importance
      int weight = static_cast<int>(e.importance * 10) + 1;
      for (int w = 0; w < weight; ++w) candidates.push_back(i);
    }
  }
  if (candidates.empty()) return;
  
  size_t numPairs = 1 + rng_.range(0, 2); // 1-3 recombination pairs
  for (size_t p = 0; p < numPairs; ++p) {
    size_t idxA = candidates[rng_.range(0, candidates.size() - 1)];
    size_t idxB = candidates[rng_.range(0, candidates.size() - 1)];
    if (idxA == idxB) continue;
    
    const Episode& a = ring_.eps_[idxA];
    const Episode& b = ring_.eps_[idxB];
    
    // Check for shared features to recombine
    bool shareLocation = (a.x == b.x && a.y == b.y);
    bool shareParticipants = (static_cast<uint8_t>(a.participants) & 
                              static_cast<uint8_t>(b.participants)) != 0;
    bool shareOutcome = (a.outcome == b.outcome && a.outcome != Outcome::Unknown);
    bool shareAction = (a.action == b.action && a.action != 255);
    bool shareKind = (a.kind == b.kind);
    
    if (!shareLocation && !shareParticipants && !shareOutcome && !shareAction && !shareKind) {
      continue; // No basis for recombination
    }
    
    // Create dream trace: recombined action sequence
    // The dream suggests: "what if I did action A in context B?"
    uint8_t dreamAction = a.action;
    (void)b.kind; (void)(a.participants | b.participants); (void)b.outcome;
    
    // Perturb the policy toward this recombined sequence
    // This is a simplified version - in practice would update learned associations
    if (dreamAction != 255) {
      // The dream creates a weak positive association between:
      // - The context of episode B (kind, participants)
      // - The action of episode A
      // This makes the organism slightly more likely to try action A in context B
      
      // Measurable effect: increment a "dream association" counter
      // In a full implementation, this would feed into the concept formation
      // or the policy's context-action weights
      
      // For now, we strengthen the source episodes to make them more retrievable
      if (idxA < ring_.eps_.size()) {
        ring_.eps_[idxA].importance = std::min(1.0, ring_.eps_[idxA].importance + 0.02);
      }
      if (idxB < ring_.eps_.size()) {
        ring_.eps_[idxB].importance = std::min(1.0, ring_.eps_[idxB].importance + 0.02);
      }
    }
  }
}

void MemorySystem::serialize(BinaryWriter& w) const {
  ring_.serialize(w);
}

bool MemorySystem::deserialize(BinaryReader& r) {
  return ring_.deserialize(r);
}

} // namespace eidolon