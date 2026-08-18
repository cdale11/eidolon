// LearnSystem: the Phase 3 learning core (DESIGN §8). Aggregates the small online
// models — ValueNet (TD), ThreatNet, policy bandit, attention — plus the neuromodulator
// layer, personality latent and drive weights, and owns the compact state-feature layout
// shared by all learners. The engine calls buildFeatures/chooseAction each tick, then
// learnStep after the outcome. Everything is seeded, bounded, allocation-light and
// serialized inside the engine snapshot.
#pragma once

#include <cstdint>

#include "body/physiology.hpp"
#include "core/clock.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "mind/attention.hpp"
#include "mind/neuromod.hpp"
#include "mind/personality.hpp"
#include "mind/policy.hpp"
#include "mind/threat_net.hpp"
#include "mind/value_net.hpp"
#include "world/world.hpp"

namespace eidolon {

class LearnSystem {
public:
  // Feature layout (order is stable for learning + serialization):
  //   0..11  attended perception channels (Perception::kFeatures)
  //   12..19 body: hunger, thirst, fatigue, energy (drive-scaled), health, pain,
  //           sleepPressure, bodyTemp deviation
  //   20..25 neuromod: novelty, curiosity, stress, arousal, valence, uncertainty
  //   26     last ThreatNet estimate (feedback channel)
  static constexpr int kFeatures = 27;

  void init(Rng& r);

  // Build the current feature vector from the world/body/neuromod state.
  void buildFeatures(const Perception& p, const Physiology& b, float* out);

  // Choose an agentic action from the features (policy + softmax temperature).
  PolicyAction chooseAction(const float* feats, Rng& r);

  // Intrinsic reward (DESIGN §8): homeostatic relief + drive pressure + novelty +
  // event bonuses, minus pain/cold penalties. Stateless (no cross-restore coupling).
  float computeReward(const Physiology& bNow, const Physiology& bBefore, float novelty,
                      double berriesEaten, bool drank);

  // Novelty of a feature vector vs the EMA prototype (0..~1).
  float novelty(const float* feats) const;

  // Post-tick learning step: TD value update, bandit update (agentic actions), threat
  // sensitization/extinction, attention outcome update, neuromodulators, life stats.
  // `agentic` is false for hardwired sleep/wake/emergency ticks (no policy update).
  void learnStep(const float* featsBefore, const float* featsAfter,
                 PolicyAction action, bool agentic, float reward, float novelty,
                 bool aversive, bool safe);

  // Slow layer: daily personality-latent drift (throttled to once per sim-day).
  void updateDaily(int64_t now);

  float temperature() const;
  float threatEstimate() const { return lastThreat_; }

  const Neuromod& neuromod() const { return neuromod_; }
  const PersonalityLatent& personality() const { return latent_; }
  const DriveWeights& driveWeights() const { return drives_; }
  const ValueNet& valueNet() const { return valueNet_; }
  const ThreatNet& threatNet() const { return threatNet_; }
  const Policy& policy() const { return policy_; }
  const Attention& attention() const { return attention_; }

  // Current life statistics (EMAs feeding the personality drift).
  LifeStats lifeStats() const;

  LearnerMetrics metrics() const;
  uint64_t lifeTicks() const { return lifeTicks_; }

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  void updateLifeStats(float reward, bool aversive, PolicyAction action);

  Neuromod neuromod_;
  ValueNet valueNet_;
  ThreatNet threatNet_;
  Policy policy_;
  Attention attention_;
  PersonalityLatent latent_;
  DriveWeights drives_;

  float prototype_[kFeatures] = {}; // EMA prototype for novelty
  float lastThreat_ = 0.0f;
  int64_t lastDailyUpdate_ = -86400;

  // Life statistics (EMAs feeding the latent drift).
  float avgReward_ = 0.0f;
  float rewardVar_ = 0.0f;
  float avgNovelty_ = 0.0f;
  float threatRate_ = 0.0f;
  float avgValence_ = 0.0f;
  float forageRate_ = 0.0f;
  float drinkRate_ = 0.0f;
  float restRate_ = 0.0f;
  float avgPain_ = 0.0f;
  uint64_t lifeTicks_ = 0;

  // Learning rates (scaled by personality sensitivities at call sites).
  float lrValue_ = 0.05f;
  float lrPolicy_ = 0.02f;
  float lrThreat_ = 0.05f;
  float lrAttention_ = 0.02f;
  float lrLife_ = 0.01f;
  float lrDaily_ = 0.10f;
};

} // namespace eidolon