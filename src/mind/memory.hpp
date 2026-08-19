// Hot episodic memory: a bounded ring of compact episodes with importance scores.
// Phase 4 adds full episodic encoding, learned retrieval, decay/rehearsal, and
// sleep consolidation. Phase 2 keeps the ring small and deterministic.
#pragma once

#include <cstdint>
#include <vector>

#include "core/serialize.hpp"

namespace eidolon {

enum class EventKind : uint8_t {
  Birth = 0,
  Forage = 1,
  Drink = 2,
  Sleep = 3,
  Wake = 4,
  Weather = 5,
  NearDeath = 6,
  Death = 7,
  Attack = 8, // predator attack on the organism (Phase 5)
};

// Participants in the episode (bitmask for compact storage).
enum class Participant : uint8_t {
  None = 0,
  Self = 1 << 0,
  User = 1 << 1,
  Prey = 1 << 2,
  Predator = 1 << 3,
  Peer = 1 << 4,  // future: other organisms
};

// Outcome of an action.
enum class Outcome : uint8_t {
  Unknown = 0,
  Success = 1,
  Failure = 2,
  Partial = 3,
  Interrupted = 4,
};

// Emotional/social relevance tags (bitmask).
enum class Relevance : uint8_t {
  None = 0,
  Rewarding = 1 << 0,
  Aversive = 1 << 1,
  Novel = 1 << 2,
  Social = 1 << 3,
  Threatening = 1 << 4,
  GoalRelated = 1 << 5,
};

// Full episode: what happened, where, when, why, and how significant.
struct Episode {
  int64_t t = 0;                    // simulation tick
  int16_t x = 0;
  int16_t y = 0;
  EventKind kind = EventKind::Birth;
  uint8_t action = 255;             // Action enum (0..5) or 255 = none
  Participant participants = Participant::None;
  Outcome outcome = Outcome::Unknown;
  float prediction = 0.0f;          // predicted value / outcome probability
  float predictionError = 0.0f;     // |actual - predicted|
  float emotionalValence = 0.0f;    // -1..1 (negative = aversive)
  float socialRelevance = 0.0f;     // 0..1 (user/peer interaction weight)
  Relevance relevance = Relevance::None;
  double importance = 0.0;          // 0..1, drives retention/consolidation
  uint8_t detail = 0;               // kind-specific payload (berries eaten, etc.)

  // Derived/consolidation fields (not in hot ring, filled during sleep).
  uint32_t rehearsalCount = 0;      // times replayed during sleep
  bool consolidated = false;        // moved to long-term archive
};

inline Participant operator|(Participant a, Participant b) {
  return static_cast<Participant>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline Participant operator&(Participant a, Participant b) {
  return static_cast<Participant>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline Relevance operator|(Relevance a, Relevance b) {
  return static_cast<Relevance>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline Relevance operator&(Relevance a, Relevance b) {
  return static_cast<Relevance>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

class MemoryRing {
  friend class MemorySystem; // for consolidation/retrieval access to eps_

public:
  // Bounded hot ring; oldest entries evicted once full (DESIGN §10: ≤ 4096).
  explicit MemoryRing(size_t capacity = 256) : cap_(capacity) {}

  void add(Episode e);

  size_t size() const { return eps_.size(); }
  size_t capacity() const { return cap_; }
  bool empty() const { return eps_.empty(); }

  // Most recent episode, or nullptr.
  const Episode* last() const {
    return eps_.empty() ? nullptr : &eps_.back();
  }

  // Iteration in chronological order.
  const std::vector<Episode>& episodes() const { return eps_; }

  // Summaries for the cognitive snapshot / chat replies.
  size_t countKind(EventKind k) const;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  size_t cap_;
  std::vector<Episode> eps_; // chronological; bounded by cap_
};

} // namespace eidolon