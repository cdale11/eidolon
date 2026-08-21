// GeneticMemory: persistent cross-generation memory inheritance
// Stores important episodic memories (especially death-related) that can be
// inherited by offspring organisms. Separate from LLM context - stored in SQLite
// and transferred via heredity files.
#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "core/serialize.hpp"
#include "mind/memory.hpp"
#include "mind/memory.hpp"

namespace eidolon {

// Forward declarations
class MemorySystem;
class LearnSystem;
class Archive;

enum class GeneticMemoryType : uint8_t {
  DeathCause = 0,          // How the parent died
  ResourceLocation = 1,    // Where food/water was found
  ThreatLocation = 2,      // Where predators were encountered
  SafeLocation = 3,        // Safe resting spots
  Skill = 4,               // Learned behaviors (e.g., "forage near water")
  ThreatPattern = 5,       // Predator behavior patterns
};

// A single genetic memory entry
struct GeneticMemory {
  GeneticMemoryType type = GeneticMemoryType::DeathCause;
  float importance = 0.0f;           // 0..1, higher = more likely to be inherited
  int64_t tick = 0;                  // When the memory was formed
  int16_t x = 0, y = 0;              // Location (if spatial)
  std::string summary;               // Human-readable summary
  std::string lesson;                // What the organism learned
  float emotionalValence = 0.0f;     // -1..1 (negative = aversive)
  uint32_t rehearsalCount = 0;       // Times replayed during sleep
  
  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

// Container for all genetic memories of an organism
struct GeneticMemoryBundle {
  std::vector<GeneticMemory> memories;
  uint64_t parentSeed = 0;
  int generation = 0;
  uint64_t createdAt = 0;            // Sim tick when bundle was created
  
  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

// GeneticMemorySystem: manages extraction, storage, and inheritance of genetic memories
class GeneticMemorySystem {
public:
  // Extract important memories from an organism's archive for inheritance
  static GeneticMemoryBundle extractFromArchive(
      uint64_t parentSeed,
      int generation,
      int maxMemories = 32);
  
  // Apply genetic memories to a new organism (inject into its memory ring)
  static size_t applyToOrganism(
      float inheritanceWeight = 0.5f);
  
  // Get death-related memories (for avoiding previous death causes)
  static std::vector<const GeneticMemory*> getDeathMemories(
      const GeneticMemoryBundle& bundle);

private:
  static float computeMemoryImportance(const Episode& e);
  static GeneticMemoryType classifyMemory(const Episode& e);
};

} // namespace eidolon
