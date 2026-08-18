#include "mind/learn.hpp"

#include <algorithm>
#include <cmath>

namespace eidolon {

namespace {
constexpr int kDriveHunger = 12;
constexpr int kDriveThirst = 13;
constexpr int kDriveRest = 14;
constexpr int kDriveEnergy = 15;
constexpr int kBodyHealth = 16;
constexpr int kBodyPain = 17;
constexpr int kBodySleep = 18;
constexpr int kBodyTemp = 19;
constexpr int kNmNovelty = 20;
constexpr int kNmCuriosity = 21;
constexpr int kNmStress = 22;
constexpr int kNmArousal = 23;
constexpr int kNmValence = 24;
constexpr int kNmUncertainty = 25;
constexpr int kThreat = 26;

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
} // namespace

void LearnSystem::init(Rng& r) {
  valueNet_ = ValueNet(kFeatures);
  threatNet_ = ThreatNet(kFeatures);
  policy_ = Policy(kFeatures);
  attention_.reset();
  latent_.init(r);
  drives_.derive(latent_);
  valueNet_.reset(r);
  threatNet_.reset(r);
  policy_.reset(r, 0.3f);
  for (float& v : prototype_) v = 0.0f;
  lastThreat_ = 0.0f;
  lastDailyUpdate_ = -86400;
  avgReward_ = rewardVar_ = avgNovelty_ = threatRate_ = avgValence_ = 0.0f;
  forageRate_ = drinkRate_ = restRate_ = avgPain_ = 0.0f;
  lifeTicks_ = 0;
}

void LearnSystem::buildFeatures(const Perception& p, const Physiology& b, float* out) {
  // Perception channels with drive-directed bias: hungry -> food cues upweighted,
  // thirsty -> water cues upweighted. Then attention selects the top-k salient ones.
  float biased[Attention::kChannels];
  const float hb = 0.8f * drives_.hunger * static_cast<float>(b.hunger() / 100.0);
  const float tb = 0.8f * drives_.thirst * static_cast<float>(b.thirst() / 100.0);
  for (int i = 0; i < Attention::kChannels; ++i) biased[i] = static_cast<float>(p[i]);
  biased[4] += hb; // bush distance
  biased[5] += hb; // bush dx
  biased[6] += hb; // bush dy
  biased[7] += hb; // bush fullness
  biased[8] += tb; // water distance
  biased[9] += tb; // water dx
  biased[10] += tb; // water dy
  const int k = neuromod_.stress > 60.0f ? Attention::kStressK : Attention::kTopK;
  attention_.attend(biased, k, out);

  // Drive-scaled body state (personality shapes which drives dominate the policy).
  out[kDriveHunger] = drives_.hunger * static_cast<float>(b.hunger() / 100.0);
  out[kDriveThirst] = drives_.thirst * static_cast<float>(b.thirst() / 100.0);
  out[kDriveRest] = drives_.rest * static_cast<float>(b.fatigue() / 100.0);
  out[kDriveEnergy] = drives_.energy * static_cast<float>(b.energy() / 100.0);
  out[kBodyHealth] = static_cast<float>(b.health() / 100.0);
  out[kBodyPain] = static_cast<float>(b.pain() / 100.0);
  out[kBodySleep] = static_cast<float>(b.sleepPressure() / 100.0);
  out[kBodyTemp] = clampf(static_cast<float>((b.bodyTemp() - 36.6) / 3.0), -1.0f, 1.0f);

  // Neuromodulator context.
  out[kNmNovelty] = clampf(neuromod_.novelty, -1.0f, 1.0f);
  out[kNmCuriosity] = clampf(neuromod_.curiosity, 0.0f, 1.0f);
  out[kNmStress] = static_cast<float>(neuromod_.stress / 100.0);
  out[kNmArousal] = neuromod_.arousal;
  out[kNmValence] = clampf(neuromod_.valence, -1.0f, 1.0f);
  out[kNmUncertainty] = neuromod_.uncertainty;

  out[kThreat] = lastThreat_;
}

PolicyAction LearnSystem::chooseAction(const float* feats, Rng& r) {
  float scores[Policy::kActions];
  return policy_.choose(feats, temperature(), r, scores);
}

float LearnSystem::temperature() const {
  const float impulse = latent_.sensitivity(PersonalityLatent::kImpulsivity);
  // Impulsivity scales how random early exploration is; uncertainty adds exploration
  // pressure (low uncertainty -> exploitation).
  return (0.35f + 1.2f * neuromod_.uncertainty) * impulse;
}

float LearnSystem::computeReward(const Physiology& bNow, const Physiology& bBefore,
                                 float noveltyIn, double berriesEaten, bool drank) {
  // Homeostatic relief (hunger/thirst/fatigue improvements) is the primary intrinsic
  // signal; drive pressure shapes it; events add bonuses; pain/cold subtract.
  const auto relief = [](double before, double now) {
    return static_cast<float>(std::max(0.0, before - now) / 100.0);
  };
  float r = 0.30f * (relief(bBefore.hunger(), bNow.hunger()) +
                     relief(bBefore.thirst(), bNow.thirst()) +
                     relief(bBefore.fatigue(), bNow.fatigue())) /
            3.0f;
  // Pressure term: being in a good homeostatic state is intrinsically rewarding. The
  // weight is set so a healthy organism gets a clearly positive baseline (~+0.3);
  // penalties (pain/cold) modulate it rather than drowning it out.
  r += 0.35f *
       ((100.0f - static_cast<float>(bNow.hunger())) +
        (100.0f - static_cast<float>(bNow.thirst())) +
        (100.0f - static_cast<float>(bNow.fatigue())) +
        static_cast<float>(bNow.energy())) /
       400.0f;
  r += 0.10f * clampf(noveltyIn, 0.0f, 1.0f);
  // Event bonuses are gated on genuine need so the organism cannot self-reinforce by
  // eating/drinking when already satiated (causes camping behaviour).
  if (bBefore.hunger() > 10.0) r += 0.5f * static_cast<float>(std::min(4.0, berriesEaten));
  if (drank && bBefore.thirst() > 10.0) r += 0.8f;
  r -= 0.20f * static_cast<float>(bNow.pain() / 100.0);
  // Thermoregulatory discomfort when far from the 36.6C set point (mild: cold winter
  // should be unpleasant pressure, not a dominant negative signal).
  const float tempDev = static_cast<float>(std::fabs(bNow.bodyTemp() - 36.6));
  if (tempDev > 2.0f) r -= 0.2f * std::min(1.0f, tempDev / 6.0f);
  return r;
}

float LearnSystem::novelty(const float* feats) const {
  float d = 0.0f;
  for (int i = 0; i < kFeatures; ++i) {
    const float diff = feats[i] - prototype_[i];
    d += diff * diff;
  }
  return std::min(1.0f, 0.5f * std::sqrt(d / static_cast<float>(kFeatures)));
}

void LearnSystem::learnStep(const float* featsBefore, const float* featsAfter,
                            PolicyAction action, bool agentic, float reward,
                            float noveltyIn, bool aversive, bool safe) {
  float hBefore[ValueNet::kHidden];
  float hAfter[ValueNet::kHidden];

  // TD value update: rpe = r + gamma*V(s') - V(s).
  const float vBefore = valueNet_.predict(featsBefore, hBefore);
  const float vAfter = valueNet_.predict(featsAfter, hAfter);
  const float rpe = reward + valueNet_.gamma() * vAfter - vBefore;
  const float lrValue = lrValue_ * latent_.sensitivity(PersonalityLatent::kRewardSensitivity);
  valueNet_.update(featsBefore, rpe, lrValue);

  // Surprise-gated policy learning: big RPEs learn faster (predictionError spikes gate
  // TD updates per DESIGN §6).
  if (agentic) {
    const float peGate = clampf(std::fabs(rpe) * 2.0f, 0.15f, 1.5f);
    policy_.update(action, featsBefore, rpe,
                   lrPolicy_ * peGate * latent_.sensitivity(PersonalityLatent::kPersistence));
  }

  // Threat learning: aversive events sensitize, safe events extinguish; stress and
  // threat-sensitivity accelerate it (DESIGN §6 coupling).
  const float p = threatNet_.predict(featsAfter, hAfter);
  if (aversive) {
    const float lrT = lrThreat_ * (1.0f + 0.5f * neuromod_.stress / 100.0f) *
                      latent_.sensitivity(PersonalityLatent::kThreatSensitivity);
    threatNet_.update(featsAfter, 1.0f, lrT);
  } else if (safe) {
    const float lrT = 0.3f * lrThreat_ *
                      latent_.sensitivity(PersonalityLatent::kThreatSensitivity);
    threatNet_.update(featsAfter, 0.0f, lrT);
  }
  lastThreat_ = p;

  // Attention: outcome-driven salience over perception channels.
  attention_.update(featsAfter, reward, lrAttention_);

  // Neuromodulators.
  neuromod_.update(reward, rpe, noveltyIn, p, latent_.sensitivity(PersonalityLatent::kStressReactivity));

  // EMA prototype for novelty; life statistics for the slow personality drift.
  for (int i = 0; i < kFeatures; ++i) {
    prototype_[i] += 0.01f * (featsAfter[i] - prototype_[i]);
  }
  updateLifeStats(reward, aversive, action);
  ++lifeTicks_;
}

void LearnSystem::updateLifeStats(float reward, bool aversive, PolicyAction action) {
  avgReward_ += lrLife_ * (reward - avgReward_);
  rewardVar_ += lrLife_ * ((reward - avgReward_) * (reward - avgReward_) - rewardVar_);
  avgNovelty_ += lrLife_ * (neuromod_.novelty - avgNovelty_);
  threatRate_ += lrLife_ * ((aversive ? 1.0f : 0.0f) - threatRate_);
  avgValence_ += lrLife_ * (neuromod_.valence - avgValence_);
  const bool forageSuccess = action == PolicyAction::Forage && reward > 0.05f;
  const bool drinkSuccess = action == PolicyAction::Drink && reward > 0.05f;
  const bool restSuccess = (action == PolicyAction::Rest || action == PolicyAction::Observe) &&
                           reward > 0.05f;
  forageRate_ += lrLife_ * ((forageSuccess ? 1.0f : 0.0f) - forageRate_);
  drinkRate_ += lrLife_ * ((drinkSuccess ? 1.0f : 0.0f) - drinkRate_);
  restRate_ += lrLife_ * ((restSuccess ? 1.0f : 0.0f) - restRate_);
  avgPain_ += lrLife_ * (neuromod_.stress / 100.0f - avgPain_);
}

void LearnSystem::updateDaily(int64_t now) {
  if (now - lastDailyUpdate_ < 86400) return;
  lastDailyUpdate_ = now;
  latent_.drift(lifeStats(), lrDaily_);
  drives_.derive(latent_);
}

LifeStats LearnSystem::lifeStats() const {
  LifeStats s;
  s.avgReward = avgReward_;
  s.rewardVar = rewardVar_;
  s.avgNovelty = avgNovelty_;
  s.threatRate = threatRate_;
  s.avgValence = avgValence_;
  s.forageRate = forageRate_;
  s.drinkRate = drinkRate_;
  s.restRate = restRate_;
  s.avgPain = avgPain_;
  return s;
}

LearnerMetrics LearnSystem::metrics() const {
  LearnerMetrics m;
  m += valueNet_.metrics();
  m += threatNet_.metrics();
  m += policy_.metrics();
  m += attention_.metrics();
  return m;
}

void LearnSystem::serialize(BinaryWriter& w) const {
  neuromod_.serialize(w);
  valueNet_.serialize(w);
  threatNet_.serialize(w);
  policy_.serialize(w);
  attention_.serialize(w);
  latent_.serialize(w);
  drives_.serialize(w);
  for (float v : prototype_) w.f32(v);
  w.f32(lastThreat_);
  w.i64(lastDailyUpdate_);
  w.f32(avgReward_);
  w.f32(rewardVar_);
  w.f32(avgNovelty_);
  w.f32(threatRate_);
  w.f32(avgValence_);
  w.f32(forageRate_);
  w.f32(drinkRate_);
  w.f32(restRate_);
  w.f32(avgPain_);
  w.u64(lifeTicks_);
}

bool LearnSystem::deserialize(BinaryReader& r) {
  if (!neuromod_.deserialize(r)) return false;
  if (!valueNet_.deserialize(r) || !threatNet_.deserialize(r) ||
      !policy_.deserialize(r) || !attention_.deserialize(r)) {
    return false;
  }
  if (!latent_.deserialize(r) || !drives_.deserialize(r)) return false;
  for (float& v : prototype_) {
    if (!r.f32(v)) return false;
  }
  if (!r.f32(lastThreat_)) return false;
  if (!r.i64(lastDailyUpdate_)) return false;
  return r.f32(avgReward_) && r.f32(rewardVar_) && r.f32(avgNovelty_) &&
         r.f32(threatRate_) && r.f32(avgValence_) && r.f32(forageRate_) &&
         r.f32(drinkRate_) && r.f32(restRate_) && r.f32(avgPain_) &&
         r.u64(lifeTicks_);
}

} // namespace eidolon