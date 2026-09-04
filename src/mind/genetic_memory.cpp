#include "mind/genetic_memory.hpp"
#include <algorithm>
#include <cmath>

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

GeneticMemoryBundle GeneticMemorySystem::extractFromArchive(
    uint64_t parentSeed,
    int generation,
    int maxMemories) {
  
  (void)maxMemories; // stub: archive integration pending
  GeneticMemoryBundle bundle;
  bundle.parentSeed = parentSeed;
  bundle.generation = generation;
  bundle.createdAt = 0;
  return bundle;
}

size_t GeneticMemorySystem::applyToOrganism(
    float inheritanceWeight) {
  (void)inheritanceWeight; // stub: archive integration pending
  return 0;
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

} // namespace eidolon
