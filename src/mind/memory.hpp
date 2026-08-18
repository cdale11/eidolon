// Hot episodic memory: a bounded ring of compact episodes with importance scores.
// Phase 2 keeps this small and deterministic; consolidation/archiving to SQLite and
// learned retrieval arrive in Phase 4.
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
};

// Compact episode: what happened, where, when, and how significant it was.
struct Episode {
  int64_t t = 0;
  int16_t x = 0;
  int16_t y = 0;
  EventKind kind = EventKind::Birth;
  double importance = 0.0; // 0..1, drives retention/consolidation later
  uint8_t detail = 0;      // kind-specific payload (berries eaten, etc.)
};

class MemoryRing {
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