#include "mind/heredity.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

#include "sim/engine.hpp"
#include "mind/learn.hpp"
#include "mind/personality.hpp"
#include "mind/policy.hpp"
#include "mind/genetic_memory.hpp"

namespace eidolon {

void HeredityGenome::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(policyWeights.size()));
  for (float v : policyWeights) w.f32(v);
  
  personality.serialize(w);
  
  w.f32(lifeStats.avgReward);
  w.f32(lifeStats.rewardVar);
  w.f32(lifeStats.avgNovelty);
  w.f32(lifeStats.threatRate);
  w.f32(lifeStats.avgValence);
  w.f32(lifeStats.forageRate);
  w.f32(lifeStats.drinkRate);
  w.f32(lifeStats.restRate);
  w.f32(lifeStats.successRate);
  w.f32(lifeStats.avgPain);
  
  // Genetic memories
  w.u32(static_cast<uint32_t>(geneticMemories.size()));
  for (const auto& mem : geneticMemories) mem.serialize(w);
  
  w.u64(parentSeed);
  w.u64(deathTick);
  w.u64(lifespanTicks);
  w.str(causeOfDeath);
  w.u32(static_cast<uint32_t>(generation));
}

bool HeredityGenome::deserialize(struct BinaryReader& r) {
  uint32_t n;
  
  if (!r.u32(n)) return false;
  policyWeights.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.f32(policyWeights[i])) return false;
  
  if (!personality.deserialize(r)) return false;
  
  if (!r.f32(lifeStats.avgReward)) return false;
  if (!r.f32(lifeStats.rewardVar)) return false;
  if (!r.f32(lifeStats.avgNovelty)) return false;
  if (!r.f32(lifeStats.threatRate)) return false;
  if (!r.f32(lifeStats.avgValence)) return false;
  if (!r.f32(lifeStats.forageRate)) return false;
  if (!r.f32(lifeStats.drinkRate)) return false;
  if (!r.f32(lifeStats.restRate)) return false;
  if (!r.f32(lifeStats.successRate)) return false;
  if (!r.f32(lifeStats.avgPain)) return false;
  
  // Genetic memories
  if (!r.u32(n)) return false;
  geneticMemories.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    if (!geneticMemories[i].deserialize(r)) return false;
  }
  
  if (!r.u64(parentSeed)) return false;
  if (!r.u64(deathTick)) return false;
  if (!r.u64(lifespanTicks)) return false;
  if (!r.str(causeOfDeath)) return false;
  if (!r.u32(n)) return false;
  generation = static_cast<int>(n);
  
  return true;
}

bool HeredityManager::saveHeredity(const HeredityGenome& genome, const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  
  const char magic[4] = {'H', 'E', 'R', 'D'};
  uint32_t version = 1;
  std::fwrite(magic, 1, 4, f);
  std::fwrite(&version, sizeof(version), 1, f);
  
  // Serialize genome using the standard BinaryWriter
  BinaryWriter bw;
  genome.serialize(bw);
  std::fwrite(bw.data().data(), 1, bw.data().size(), f);
  std::fclose(f);
  return true;
}

bool HeredityManager::loadHeredity(HeredityGenome& genome, const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  
  char magic[4];
  uint32_t version = 0;
  if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "HERD", 4) != 0 ||
      std::fread(&version, sizeof(version), 1, f) != 1 || version != 1) {
    std::fclose(f);
    return false;
  }
  
  std::fseek(f, 0, SEEK_END);
  size_t size = std::ftell(f);
  std::fseek(f, 4 + 4, SEEK_SET);
  
  std::vector<uint8_t> buf(size - 8);
  if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
    std::fclose(f);
    return false;
  }
  std::fclose(f);
  
  BinaryReader br(buf.data(), buf.size());
  return genome.deserialize(br);
}

HeredityGenome HeredityManager::createOffspring(
    const HeredityGenome& parent,
    float mutationRate,
    class Rng& rng,
    uint64_t offspringSeed) {
  
  HeredityGenome offspring = parent;
  offspring.parentSeed = offspringSeed;
  offspring.generation = parent.generation + 1;
  offspring.lifespanTicks = 0;
  offspring.deathTick = 0;
  offspring.causeOfDeath.clear();
  offspring.lifeStats = HeredityGenome::LifeStats{};
  
  mutateWeights(offspring.policyWeights, mutationRate, rng);
  offspring.personality = mutatePersonality(parent.personality, mutationRate, rng);
  
  return offspring;
}

HeredityGenome HeredityManager::extractGenome(
    const class Engine& engine,
    uint64_t deathTick,
    const std::string& causeOfDeath) {
  
  HeredityGenome genome;
  
  const auto& learn = engine.learn();
  
  // Extract policy weights
  genome.policyWeights = learn.policy().serializedWeights();
  
  genome.personality = learn.personality();
  
  // Life stats from learn system
  const auto ls = learn.lifeStats();
  genome.lifeStats.avgReward = ls.avgReward;
  genome.lifeStats.rewardVar = ls.rewardVar;
  genome.lifeStats.avgNovelty = ls.avgNovelty;
  genome.lifeStats.threatRate = ls.threatRate;
  genome.lifeStats.avgValence = ls.avgValence;
  genome.lifeStats.forageRate = ls.forageRate;
  genome.lifeStats.drinkRate = ls.drinkRate;
  genome.lifeStats.restRate = ls.restRate;
  genome.lifeStats.successRate = ls.successRate;
  genome.lifeStats.avgPain = ls.avgPain;
  
  // Extract genetic memories from engine's memory system
  // Note: In a full implementation, we'd extract from the Archive
  // For now, create a death-cause memory
  GeneticMemory deathMem;
  deathMem.type = GeneticMemoryType::DeathCause;
  deathMem.importance = 0.9f;
  deathMem.tick = deathTick;
  deathMem.x = static_cast<int16_t>(engine.world().organismPos().x);
  deathMem.y = static_cast<int16_t>(engine.world().organismPos().y);
  deathMem.summary = "Died from " + causeOfDeath;
  deathMem.lesson = "Avoid this location/situation - " + causeOfDeath;
  deathMem.emotionalValence = -0.8f;
  deathMem.rehearsalCount = 0;
  genome.geneticMemories.push_back(deathMem);
  
  genome.deathTick = deathTick;
  genome.lifespanTicks = deathTick;
  genome.causeOfDeath = causeOfDeath;
  genome.generation = 0;
  
  return genome;
}

void HeredityManager::applyHeredity(
    class Engine& engine,
    const HeredityGenome& genome,
    float inheritanceWeight) {
  
  if (inheritanceWeight <= 0.0f) return;
  if (inheritanceWeight > 1.0f) inheritanceWeight = 1.0f;
  
  auto& learn = engine.learn();
  auto& policy = learn.policy();
  
  // Apply policy weights (blend with current)
  if (genome.policyWeights.size() == policy.serializedWeightsSize()) {
    const std::vector<float>& currentWeights = policy.weights();
    std::vector<float> blendedWeights(policy.serializedWeightsSize());
    for (size_t i = 0; i < blendedWeights.size(); ++i) {
      blendedWeights[i] = currentWeights[i] * (1.0f - inheritanceWeight) + genome.policyWeights[i] * inheritanceWeight;
    }
    auto& w = policy.weights();
    for (size_t i = 0; i < w.size(); ++i) {
      w[i] = blendedWeights[i];
    }
  }
  
  // Apply personality (blend)
  auto& currentPersonality = learn.personality();
  for (int i = 0; i < PersonalityLatent::kDims; ++i) {
    currentPersonality[i] = currentPersonality[i] * (1.0f - inheritanceWeight) + genome.personality[i] * inheritanceWeight;
  }
  learn.rederiveDrives();
  
  // Apply genetic memories - inject death memories into threat system
  for (const auto& mem : genome.geneticMemories) {
    if (mem.type == GeneticMemoryType::DeathCause && mem.importance * inheritanceWeight > 0.5f) {
      // Inject as a strong threat memory at the death location
      // This will make the organism avoid the death location
      // In a full implementation, this would inject into the memory ring
    }
  }
}

std::string HeredityManager::defaultHeredityPath(const std::string& dataDir, int generation) {
  return dataDir + "/heredity_gen" + std::to_string(generation) + ".hrd";
}

void HeredityManager::mutateWeights(std::vector<float>& weights, float rate, class Rng& rng) {
  if (rate <= 0.0f) return;
  for (float& w : weights) {
    if (rng.range(0.0, 1.0) < rate) {
      w += static_cast<float>(rng.range(-0.1, 0.1));
    }
  }
}

PersonalityLatent HeredityManager::mutatePersonality(const PersonalityLatent& parent, float rate, class Rng& rng) {
  PersonalityLatent offspring = parent;
  if (rate <= 0.0f) return offspring;
  for (int i = 0; i < PersonalityLatent::kDims; ++i) {
    float& v = offspring[i];
    if (rng.range(0.0, 1.0) < rate) {
      v += static_cast<float>(rng.range(-0.05, 0.05));
    }
  }
  return offspring;
}

} // namespace eidolon