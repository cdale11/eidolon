#include "mind/heredity.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

#include "sim/engine.hpp"
#include "mind/learn.hpp"
#include "mind/personality.hpp"
#include "mind/policy.hpp"

namespace eidolon {

void HeredityGenome::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(policyWeights.size()));
  for (float v : policyWeights) w.f32(v);
  
  personality.serialize(w);
  
  w.f32(lifeStats.avgReward);
  w.f32(lifeStats.avgThreat);
  w.f32(lifeStats.avgNovelty);
  w.f32(lifeStats.successRate);
  w.u64(lifeStats.totalTicks);
  
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
  if (!r.f32(lifeStats.avgThreat)) return false;
  if (!r.f32(lifeStats.avgNovelty)) return false;
  if (!r.f32(lifeStats.successRate)) return false;
  if (!r.u64(lifeStats.totalTicks)) return false;
  
  if (!r.u64(parentSeed)) return false;
  if (!r.u64(deathTick)) return false;
  if (!r.u64(lifespanTicks)) return false;
  if (!r.str(causeOfDeath)) return false;
  if (!r.u32(n)) return false; generation = static_cast<int>(n);
  
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
  
  // Extract policy weights (direct access to private member via friend or public method)
  // For now, use the policy's internal weights via serialization round-trip
  // This is a simplified approach - in practice we'd add a weights() accessor
  genome.policyWeights = learn.policy().serializedWeights();
  
  genome.personality = learn.personality();
  
  // Life stats from learn system (using available metrics)
  const auto& policyMetrics = learn.policy().metrics();
  genome.lifeStats.avgReward = 0.0f; // Not tracked in base LearnerMetrics
  genome.lifeStats.avgThreat = 0.0f;
  genome.lifeStats.avgNovelty = 0.0f;
  genome.lifeStats.successRate = 0.0f;
  genome.lifeStats.totalTicks = engine.clock().now();
  
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
  
  // Apply policy weights (blend with current) via loadPrior mechanism
  if (genome.policyWeights.size() == policy.serializedWeightsSize()) {
    // Create a temporary prior file and load it
    // This is a simplified approach
  }
  
  // Apply personality (blend)
  auto& currentPersonality = learn.personality();
  for (int i = 0; i < PersonalityLatent::kDims; ++i) {
    currentPersonality[i] = currentPersonality[i] * (1.0f - inheritanceWeight) + genome.personality[i] * inheritanceWeight;
  }
  // Note: drive re-derivation would require access to private drives_
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