#include "harness.hpp"

#include <unistd.h>

#include <cmath>
#include <cstdio>

#include "mind/learn.hpp"
#include "mind/personality.hpp"
#include "mind/policy.hpp"
#include "mind/value_net.hpp"
#include "sim/engine.hpp"

using namespace eidolon;

// ---------------------------------------------------------------------------
// Model-level tests
// ---------------------------------------------------------------------------

TEST(policy_repeated_trial_improves) {
  // Repeated scenario: the organism is thirsty. The correct action is Drink. The
  // bandit should push the Drink probability up over repeated trials (Gate: "seeded
  // test shows repeated scenario -> success rate rises").
  Rng r(1);
  Policy p(2);
  p.reset(r, 0.3f);
  const float feats[2] = {0.9f, 0.5f};
  int earlyGood = 0, lateGood = 0;
  const int kTrials = 2000;
  for (int t = 0; t < kTrials; ++t) {
    float scores[Policy::kActions];
    const PolicyAction a = p.choose(feats, 0.5f, r, scores);
    const bool good = a == PolicyAction::Drink;
    if (t < 200) earlyGood += good ? 1 : 0;
    if (t >= kTrials - 200) lateGood += good ? 1 : 0;
    p.update(a, feats, good ? 1.0f : -0.5f, 0.05f);
  }
  CHECK(lateGood > earlyGood);
  CHECK(lateGood * 100 / 200 > 70); // majority of late trials pick the right action
}

TEST(value_net_td_converges) {
  // Constant reward r=0.3, gamma=0.9 -> V* = r/(1-gamma) = 3.0.
  Rng r(2);
  ValueNet v(3);
  v.reset(r);
  const float feats[3] = {0.2f, 0.5f, 0.8f};
  float h[ValueNet::kHidden];
  for (int i = 0; i < 4000; ++i) {
    float hh[ValueNet::kHidden];
    const float val = v.predict(feats, hh);
    const float rpe = 0.3f + v.gamma() * val - val;
    v.update(feats, rpe, 0.05f);
  }
  const float val = v.predict(feats, h);
  CHECK(val > 2.4f);
  CHECK(val < 3.6f);
}

TEST(threat_net_sensitizes_then_extinguishes) {
  Rng r(3);
  ThreatNet tn(2);
  tn.reset(r);
  const float feats[2] = {0.7f, 0.2f};
  float h[ThreatNet::kHidden];
  const float before = tn.predict(feats, h);
  for (int i = 0; i < 30; ++i) tn.update(feats, 1.0f, 0.2f);
  const float sensitized = tn.predict(feats, h);
  CHECK(sensitized > before);
  CHECK(sensitized > 0.7f);
  for (int i = 0; i < 60; ++i) tn.update(feats, 0.0f, 0.1f);
  const float extinguished = tn.predict(feats, h);
  CHECK(extinguished < sensitized - 0.3f);
}

TEST(stress_accelerates_threat_learning) {
  // Neuromodulator coupling: higher stress -> faster threat learning. Simulated here by
  // different effective learning rates.
  Rng r1(4), r2(5);
  ThreatNet a(2), b(2);
  a.reset(r1);
  b.reset(r2);
  const float feats[2] = {0.6f, 0.3f};
  float ha[ThreatNet::kHidden], hb[ThreatNet::kHidden];
  (void)a.predict(feats, ha);
  (void)b.predict(feats, hb);
  for (int i = 0; i < 10; ++i) {
    a.update(feats, 1.0f, 0.3f); // stressed (high lr)
    b.update(feats, 1.0f, 0.05f); // calm (low lr)
  }
  CHECK(a.predict(feats, ha) > b.predict(feats, hb) + 0.1f);
}

TEST(attention_salience_follows_reward) {
  Attention att;
  att.reset();
  float percept[Attention::kChannels] = {};
  percept[4] = 1.0f; // food channel is informative
  for (int i = 0; i < 200; ++i) att.update(percept, 1.0f, 0.02f);
  CHECK(att.salience(4) > att.salience(0) + 0.3f);
  float out[Attention::kChannels];
  att.attend(percept, 8, out);
  CHECK_EQ(out[4], 1.0f);
}

TEST(personality_latent_diverges_with_experience) {
  // Identical seeds, different life histories -> different latent vectors.
  Rng r1(9), r2(9);
  PersonalityLatent a, b;
  a.init(r1);
  b.init(r2);
  CHECK_EQ(a.value(0), b.value(0)); // identical priors from the same seed
  LifeStats sa, sb;
  sa.avgReward = 0.6f;
  sa.threatRate = 0.1f;
  sa.avgNovelty = 0.4f;
  sa.forageRate = 0.8f;
  sa.avgValence = 0.3f;
  sa.avgPain = 0.05f;
  sb.avgReward = -0.2f;
  sb.threatRate = 0.7f;
  sb.avgNovelty = 0.1f;
  sb.forageRate = 0.2f;
  sb.avgValence = -0.4f;
  sb.avgPain = 0.6f;
  for (int i = 0; i < 60; ++i) {
    a.drift(sa, 0.02f);
    b.drift(sb, 0.02f);
  }
  bool anyDiff = false;
  for (int i = 0; i < PersonalityLatent::kDims; ++i) {
    if (std::fabs(a.value(i) - b.value(i)) > 0.05f) anyDiff = true;
  }
  CHECK(anyDiff);
}

TEST(neuromod_couplings_and_temperature) {
  Neuromod nm;
  // A sustained threat source drives stress up; threat alone (even calm, no negative
  // reward) registers on the threat channel.
  for (int i = 0; i < 50; ++i) nm.update(0.0f, 0.0f, 0.0f, 0.9f, 1.0f);
  CHECK(nm.stress > 40.0f);
  CHECK_EQ(nm.threat, 0.9f);
  const float stressBefore = nm.stress;
  for (int i = 0; i < 100; ++i) nm.update(0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
  CHECK(nm.stress < stressBefore); // stress decays without a threat source
  CHECK_EQ(nm.threat, 0.0f);
}

// ---------------------------------------------------------------------------
// Engine-level tests
// ---------------------------------------------------------------------------

TEST(engine_learning_metrics_increment) {
  Engine e;
  e.init(42, true, 64, 64);
  for (int i = 0; i < 500; ++i) e.tick();
  const LearnerMetrics m = e.learn().metrics();
  CHECK(m.inferences > 0);
  CHECK(m.updates > 0);
  CHECK(e.learn().lifeTicks() == 500);
}

TEST(engine_identical_seed_identical_latent_and_learning) {
  Engine a, b;
  a.init(42, true, 64, 64);
  b.init(42, true, 64, 64);
  for (int i = 0; i < 2000; ++i) {
    a.tick();
    b.tick();
  }
  // Same experience -> identical latent and identical learned weights (determinism).
  const PersonalityLatent& pa = a.learn().personality();
  const PersonalityLatent& pb = b.learn().personality();
  for (int i = 0; i < PersonalityLatent::kDims; ++i) {
    CHECK_EQ(pa.value(i), pb.value(i));
  }
  CHECK_EQ(a.learn().metrics().updates, b.learn().metrics().updates);
  CHECK_EQ(a.learn().threatEstimate(), b.learn().threatEstimate());
}

TEST(engine_same_seed_different_experience_diverges_latent) {
  // Gate: two identical seeds with different experiences produce different latent
  // vectors. Same seed, but different worlds -> different life histories (food-dense
  // 32x32 vs sparse 128x128), so the daily personality drift diverges.
  Engine a, b;
  a.init(42, true, 32, 32);
  b.init(42, true, 128, 128);
  const int kTicks = 14 * 86400; // 14 sim-days (drift runs daily)
  for (int i = 0; i < kTicks; ++i) {
    if (!a.isAlive()) break;
    a.tick();
  }
  for (int i = 0; i < kTicks; ++i) {
    if (!b.isAlive()) break;
    b.tick();
  }
  const float* pa = a.learn().personality().data();
  const float* pb = b.learn().personality().data();
  float diff = 0.0f;
  for (int i = 0; i < PersonalityLatent::kDims; ++i) diff += std::fabs(pa[i] - pb[i]);
  CHECK(diff > 0.1f);
}

TEST(engine_learned_policy_sustains_life) {
  // The learned policy must sustain the organism: after two weeks it is still alive,
  // feeding and drinking steadily, and the intrinsic reward stays clearly positive.
  Engine e;
  e.init(42, true, 64, 64);
  const int kTicks = 14 * 86400;
  double reward = 0.0;
  int n = 0;
  for (int i = 0; i < kTicks; ++i) {
    if (!e.isAlive()) break;
    e.tick();
    reward += e.learn().neuromod().reward;
    ++n;
  }
  CHECK(e.isAlive());
  CHECK(e.clock().now() >= 14 * 86400);
  CHECK(e.stats().berriesEaten > 500);
  CHECK(e.stats().drinks > 100);
  CHECK(reward / n > 0.15);
}

TEST(policy_loads_prior_and_retrains_online) {
  // Teacher-baked prior: Drink strongly preferred when the thirst feature (index 29) is
  // high. Online learning must keep running on top of it, and the prior + learned state
  // must round-trip through the snapshot.
  char pathbuf[128];
  std::snprintf(pathbuf, sizeof(pathbuf), "/tmp/eidolon_test_prior_%d.eprp",
                static_cast<int>(::getpid()));
  const char* path = pathbuf;
  {
    std::FILE* f = std::fopen(path, "wb");
    CHECK(f != nullptr);
    std::fwrite("EPRP", 1, 4, f);
    uint32_t v = 1, nf = 43, na = 6;
    std::fwrite(&v, 4, 1, f);
    std::fwrite(&nf, 4, 1, f);
    std::fwrite(&na, 4, 1, f);
    float w[6 * 44] = {};
    float* row = &w[static_cast<int>(PolicyAction::Drink) * 44];
    row[29] = 5.0f;  // strong preference on the thirst feature
    row[43] = -0.5f; // mild negative bias (avoid defaulting to Drink)
    std::fwrite(w, sizeof(float), 6 * 44, f);
    std::fclose(f);
  }

  Engine e;
  e.init(42, true, 64, 64);
  CHECK(e.loadPolicyPrior(path));

  float feats[LearnSystem::kFeatures] = {};
  feats[29] = 0.8f; // thirsty
  const float sForage = e.learn().policy().score(PolicyAction::Forage, feats);
  const float sDrink = e.learn().policy().score(PolicyAction::Drink, feats);
  CHECK(sDrink > sForage + 1.0f); // the prior dominates the random init

  for (int i = 0; i < 500; ++i) e.tick();
  const float after = e.learn().policy().score(PolicyAction::Drink, feats);
  CHECK(std::fabs(after - sDrink) > 1e-6f); // online learning moved the weights

  const auto snap = e.snapshot();
  std::string err;
  Engine f2;
  CHECK(f2.restore(snap, err));
  CHECK_EQ(f2.learn().policy().score(PolicyAction::Drink, feats),
           e.learn().policy().score(PolicyAction::Drink, feats));
  for (int i = 0; i < 100; ++i) {
    e.tick();
    f2.tick();
  }
  CHECK_EQ(f2.learn().policy().score(PolicyAction::Drink, feats),
           e.learn().policy().score(PolicyAction::Drink, feats));

  // Round-trip: export the evolved organism as a prior, load it into a fresh engine.
  char outbuf[128];
  std::snprintf(outbuf, sizeof(outbuf), "/tmp/eidolon_test_prior_out_%d.eprp",
                static_cast<int>(::getpid()));
  CHECK(e.savePolicyPrior(outbuf));
  Engine f3;
  f3.init(123, true, 64, 64);
  CHECK(f3.loadPolicyPrior(outbuf));
  CHECK(std::fabs(f3.learn().policy().score(PolicyAction::Drink, feats) -
                  e.learn().policy().score(PolicyAction::Drink, feats)) < 1e-4f);
  std::remove(outbuf);
}

TEST(engine_survives_days_with_learning) {
  Engine e;
  e.init(42, true, 64, 64);
  const int kTicks = 14 * 86400; // 14 sim-days
  for (int i = 0; i < kTicks; ++i) {
    if (!e.isAlive()) break;
    e.tick();
  }
  CHECK(e.isAlive());
  CHECK(e.clock().now() >= 14 * 86400);
}