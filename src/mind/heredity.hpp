#ifndef EIDOLON_HEREDITY_HPP
#define EIDOLON_HEREDITY_HPP

#include <vector>
#include <string>
#include <cstdint>

#include "core/serialize.hpp"
#include "mind/personality.hpp"
#include "mind/policy.hpp"
#include "mind/genetic_memory.hpp"

namespace eidolon {

class Engine;
class Rng;

// Heredity: saves the organism's learned state for inheritance by offspring
// When an organism dies, its "genome" (learned weights, personality) is saved
// A new organism can inherit from this heredity, receiving a blend of the parent's traits

struct HeredityGenome {
  // Neural network weights (policy only for now)
  std::vector<float> policyWeights;
  
  // Personality latent vector (16-d)
  PersonalityLatent personality;
  
  // Life statistics for personality drift baseline (mirrors Personality::LifeStats)
  struct LifeStats {
    float avgReward = 0.0f;
    float rewardVar = 0.0f;
    float avgNovelty = 0.0f;
    float threatRate = 0.0f;
    float avgValence = 0.0f;
    float forageRate = 0.0f;
    float drinkRate = 0.0f;
    float restRate = 0.0f;
    float successRate = 0.0f;
    float avgPain = 0.0f;
  } lifeStats;
  
  // Genetic memories inherited from parent
  std::vector<GeneticMemory> geneticMemories;
  
  // Metadata
  uint64_t parentSeed = 0;
  uint64_t deathTick = 0;
  uint64_t lifespanTicks = 0;
  std::string causeOfDeath;
  int generation = 0; // 0 = first generation
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class HeredityManager {
public:
  // Save heredity when organism dies
  static bool saveHeredity(
      const HeredityGenome& genome,
      const std::string& path);
  
  // Load heredity for inheritance
  static bool loadHeredity(
      HeredityGenome& genome,
      const std::string& path);
  
  // Create offspring genome from parent (with mutation)
  // mutationRate: 0.0 = exact copy, 0.01 = 1% gaussian noise, etc.
  static HeredityGenome createOffspring(
      const HeredityGenome& parent,
      float mutationRate,
      class Rng& rng,
      uint64_t offspringSeed);
  
  // Extract genome from current engine state
  static HeredityGenome extractGenome(
      const class Engine& engine,
      uint64_t deathTick,
      const std::string& causeOfDeath);
  
  // Apply heredity to a fresh engine
  static void applyHeredity(
      class Engine& engine,
      const HeredityGenome& genome,
      float inheritanceWeight); // 0.0 = ignore, 1.0 = full inheritance
  
  // Get default heredity path for a data directory
  static std::string defaultHeredityPath(const std::string& dataDir, int generation);
  
private:
  static void mutateWeights(std::vector<float>& weights, float rate, class Rng& rng);
  static PersonalityLatent mutatePersonality(const PersonalityLatent& parent, float rate, class Rng& rng);
};

} // namespace eidolon

#endif // EIDOLON_HEREDITY_HPP