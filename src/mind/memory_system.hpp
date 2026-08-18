// MemorySystem: Phase 4 memory consolidation, retrieval, decay, rehearsal, and
// dreams. Separate from MemoryRing so the hot tick path stays clean.
#pragma once

#include "mind/memory.hpp"

namespace eidolon {

class LearnSystem;
class Archive;

class MemorySystem {
public:
  explicit MemorySystem(size_t ringCapacity = 256);

  MemoryRing& ring() { return ring_; }
  const MemoryRing& ring() const { return ring_; }

  void tickDecay();
  std::vector<const Episode*> retrieve(size_t k, float relevanceBoost = 1.0f) const;
  void strengthen(size_t index, float amount);
  void consolidate(const LearnSystem& learn, Archive* archive);
  void dream(const LearnSystem& learn);

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  MemoryRing ring_;
  float decayRate_ = 0.9999f;
};

} // namespace eidolon