// Simulation engine: orchestrates world → body → decision → action → log per tick with
// adaptive step sizes. Phase 1 uses a heuristic decision policy; Phase 3 adds the learned
// policy core (ValueNet/ThreatNet/policy bandit/attention + neuromodulators + personality
// latent) behind the same decision loop. The tick path is allocation-light and noexcept.
#pragma once

#include <cstdint>
#include <string>

#include "body/physiology.hpp"
#include "core/clock.hpp"
#include "core/log.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "mind/archive.hpp"
#include "mind/compute_scheduler.hpp"
#include "mind/learn.hpp"
#include "mind/memory.hpp"
#include "mind/memory_system.hpp"
#include "mind/heredity.hpp"
#include "mind/wildlife_social.hpp"
#include "body/crafting.hpp"
#include "body/construction.hpp"
#include "llm/instruction_learning.hpp"
#include "mind/goal_emergence.hpp"
#include "world/world.hpp"

namespace eidolon {

enum class Action : uint8_t {
  Wander = 0,
  Rest = 1,
  Sleep = 2,
  Observe = 3,
  Forage = 4,
  Drink = 5,
  Flee = 6,
  // New survival actions
  Farm = 7,       // Plant/harvest crops on farm plots
  Cook = 8,       // Process raw food into cooked meals
  Craft = 9,      // Craft tools, containers, structures
  Build = 10,     // Build structures (farm plots, wells, storage, shelter)
  CollectWater = 11, // Collect rainwater, dew
  Preserve = 12,  // Preserve food (drying, smoking, fermenting)
};

class Engine {
public:
struct Stats {
    uint64_t ticksFine = 0;
    uint64_t ticksCoarse = 0;
    uint64_t ticksSleep = 0;
    uint64_t actionsWander = 0;
    uint64_t actionsRest = 0;
    uint64_t actionsSleep = 0;
    uint64_t actionsObserve = 0;
    uint64_t actionsForage = 0;
    uint64_t actionsDrink = 0;
    uint64_t actionsFlee = 0;
    uint64_t actionsFarm = 0;
    uint64_t actionsCook = 0;
    uint64_t actionsCraft = 0;
    uint64_t actionsBuild = 0;
    uint64_t actionsCollectWater = 0;
    uint64_t actionsPreserve = 0;
    uint64_t predatorAttacks = 0;
    uint64_t berriesEaten = 0;
    uint64_t drinks = 0;
    uint64_t fallsTaken = 0;
    uint64_t woundsSustained = 0;
    uint64_t infections = 0;
    uint64_t waterskinFills = 0;
    uint64_t waterskinDrinks = 0;
    uint64_t cropsHarvested = 0;
    uint64_t mealsCooked = 0;
    uint64_t itemsCrafted = 0;
    uint64_t structuresBuilt = 0;
    uint64_t waterCollected = 0;
    uint64_t foodPreserved = 0;
  };

  Engine() = default;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Fresh organism. `masterSeed` drives every subsystem stream.
  void init(uint64_t masterSeed, bool deterministic, int worldW, int worldH);

  bool isAlive() const { return world_.organismAlive() && body_.alive(); }

  // The action chosen on the most recent tick (also persisted in the snapshot so a
  // resumed run continues with the correct chat-grounding state). Default until the
  // first tick: Action::Observe. Used by the LLM bridge's CognitiveSnapshot so chat
  // replies can reference what the organism is actually doing right now (the bridge
  // calls this every makeSnapshot()).
  Action lastAction() const noexcept { return lastAction_; }

  // Advance the simulation by one tick (step size chosen adaptively). noexcept hot path.
  // Returns the action chosen this tick.
  Action tick() noexcept;

  // tick() plus per-tick logging (state transitions, status lines, events). Used by
  // runDays and by the headless CLI so both trace identically.
  void tickAndLog(EventLog& log) noexcept;

  // Run until `days` simulated days have elapsed (or the organism dies). Returns false
  // only if the organism died before the end.
  bool runDays(double days, EventLog& log, std::string& whyStopped);

  const SimClock& clock() const { return clock_; }
  // Absolute sim-time target of the current run schedule. Persisted so a resumed run
  // continues the ORIGINAL schedule (resume == uninterrupted) even when coarse ticks
  // overshoot the requested boundary.
  int64_t scheduledTarget() const { return scheduledTarget_; }
  void setScheduledTarget(int64_t t) { scheduledTarget_ = t; }
  const World& world() const { return world_; }
  World& world() { return world_; }
  const Physiology& body() const { return body_; }
  // Reset physiology to a fresh healthy state (wounds/infection cleared). Keeps the
  // world, learning and personality intact. Used by tests (e.g. re-exposing an
  // experienced organism to predators) and by the harness for repeat trials.
  void resetBody() { body_.reset(); }
  const MemoryRing& memory() const { return memorySys_.ring(); }
  MemorySystem& memorySys() { return memorySys_; }
  const MemorySystem& memorySys() const { return memorySys_; }
  const Stats& stats() const { return stats_; }
  uint64_t masterSeed() const { return masterSeed_; }
  bool deterministic() const { return deterministic_; }

  // Snapshot: serialize/restore the entire engine state (identity-preserving).
  std::vector<uint8_t> snapshot() const;
  bool restore(const std::vector<uint8_t>& blob, std::string& err);

  bool saveFile(const std::string& path, std::string& err) const;
  bool loadFile(const std::string& path, std::string& err);

  void setStatusInterval(int64_t seconds) { statusInterval_ = seconds; }

  // Optional durable archive sink (SQLite behind the Archive interface). May be null.
  void setArchive(Archive* archive) { archive_ = archive; }

  // Movement helpers (deterministic greedy best-step with random escape fallback).
  // Try to move one tile toward `target` (see moveToward in engine.cpp). Returns true
  // if the organism moved.
  bool moveToward(Vec2i target) noexcept;
  // Try to move one tile away from `threat` (used by the Flee action).
  bool moveAwayFrom(Vec2i threat) noexcept;
  // Move one tile to `q` if reachable: walkable, in bounds, and not a cliff (elevation
// difference > kCliffStep). Steep descents (drop > kFallDamageDrop) are refused unless
// `allowFall` (fleeing / trapped): normal movement routes around them, so falls happen
// only when forced and stay a rare, survivable hazard. A taken fall deals damage.
bool stepTo(Vec2i q, bool allowFall = false) noexcept;

  // Sustained directional walk used when no resource is in perception range: walk in
  // `exploreDir_` for ~16 ticks, rotate 90 on obstacle. Replaces 1-tile random walks
  // that bounce the organism in a corner until it starves/dehydrates.
  bool exploreStep() noexcept;

  // Learning-core access (tests + metrics).
  const LearnSystem& learn() const { return learn_; }
  LearnSystem& learn() { return learn_; }

  // Seed the policy bandit with teacher-baked weights (a "wisdom prior") instead of the
  // random init. Only meaningful for a fresh organism; online learning continues on top.
bool loadPolicyPrior(const std::string& path);
  
  // Export the current organism's learned policy as an .eprp prior file, so a well-adapted
  // organism can seed future fresh runs (mirror of loadPolicyPrior).
  bool savePolicyPrior(const std::string& path) const;
  
  // Heredity: load inheritance from a previous organism's genome
  // Must be called before init() or after init() before first tick.
  void setHeredityPath(const std::string& path, float inheritanceWeight = 0.7f) {
    heredityPath_ = path;
    heredityInheritanceWeight_ = inheritanceWeight;
  }

  // Load and apply heredity from file (called automatically in init if set)
  bool loadHeredity();

  // Save current organism's genome as heredity for future inheritance
  // Called automatically on death if heredityPath_ is set.
  bool saveHeredity(uint64_t deathTick, const std::string& causeOfDeath) const;

  // Rebirth tracking
  uint32_t rebirthCount() const { return rebirthCount_; }
  void incrementRebirthCount() { ++rebirthCount_; }
  
  // Optional offline experience dump (teacher training data): when set, each tick appends
  // one JSONL record (features, action, reward, interpretable context). Used only by the
  // headless CLI for offline teacher pipelines; never in the server hot path.
  void setExperienceOut(std::FILE* f) { experienceOut_ = f; }

  // Compute scheduler (Phase 11): coordinates optional background work and reports
  // per-domain profiling. Never changes tick semantics (determinism invariant).
  ComputeScheduler& scheduler() { return scheduler_; }
  const ComputeScheduler& scheduler() const { return scheduler_; };

  // User instruction processing (Phase: Learning from user speech)
  InstructionLearningSystem& instructionLearning() { return instructionLearning_; }
  const InstructionLearningSystem& instructionLearning() const { return instructionLearning_; };

  GoalEmergence& goalEmergence() { return goal_emergence_; }
  const GoalEmergence& goalEmergence() const { return goal_emergence_; }

  UserModel& userModel() { return userModel_; }
  const UserModel& userModel() const { return userModel_; }

  WildlifeSocialSystem& wildlifeSocial() { return wildlife_social_; }
  const WildlifeSocialSystem& wildlifeSocial() const { return wildlife_social_; }

  // Process a user text instruction: parse, validate, update trust/habits, and
  // optionally inject as a goal/policy bias. Returns whether instruction was valid.
  bool processUserInstruction(const std::string& text, uint64_t tick);

private:
  void stepClock(StepKind kind) noexcept;
  void logStatus(EventLog& log) noexcept;
  Action decide() noexcept;
  void execute(Action a) noexcept;
  void checkEvents(EventLog* log) noexcept;
  void recordEpisode(EventKind kind, uint8_t detail, double importance,
                       uint8_t action = 255, Participant participants = Participant::None,
                       Outcome outcome = Outcome::Unknown, float prediction = 0.0f,
                       float predictionError = 0.0f, float emotionalValence = 0.0f,
                       float socialRelevance = 0.0f, Relevance relevance = Relevance::None) noexcept;
  bool aversiveTick(const Physiology& before) const noexcept;
  bool safeTick(float reward) const noexcept;
  std::string determineCauseOfDeath() const noexcept;
  static Action policyToAction(PolicyAction a) noexcept;
  static PolicyAction actionToPolicy(Action a) noexcept;
  void dumpExperience(PolicyAction pa, bool agentic, float reward, float novelty,
                      bool aversive, bool safe, double eaten, bool drank) noexcept;

  // Phase 5 disease-vector exposure dose (0..1/tick) from the current tile: Swamp ground,
  // deep-water proximity, or any water adjacency while raining/storming.
  double hazardDose() const noexcept;

  void serializeState(BinaryWriter& w) const;
  bool deserializeState(BinaryReader& r, std::string& err);

  SimClock clock_;
  World world_;
  Physiology body_;
  MemorySystem memorySys_;
  LearnSystem learn_;
  EventQueue events_;
  Stats stats_;
  uint64_t masterSeed_ = 0;
  bool deterministic_ = false;
  bool died_ = false;
  int64_t lastStatusAt_ = 0;
  int64_t scheduledTarget_ = 0; // absolute end-time of the current run schedule
  int64_t statusInterval_ = 600; // sim-seconds between status lines
  int prevMode_ = 0; // last logged life mode: 0=active,1=rest,2=sleep
  bool resting_ = false; // hysteresis for rest mode (prevents boundary oscillation)
  // The most recent action chosen by `decide()` / executed by `execute()`. Persisted in
  // the snapshot (see serializeState) so a resumed run continues with the correct
  // chat-grounding state — the LLM bridge reads it via `lastAction()` to populate
  // CognitiveSnapshot::currentAction instead of the hardcoded "active" placeholder.
  Action lastAction_ = Action::Observe;
  // Directed-exploration state: when no food/water is in perception range, the organism
  // walks in a fixed `exploreDir_` for up to `exploreTicks_` ticks then re-rolls — this
  // actually traverses terrain instead of bouncing randomly in a corner. Serialised.
  Vec2i exploreDir_{1, 0};
  int exploreTicks_ = 0;
  Archive* archive_ = nullptr; // optional durable sink; never owned
  std::FILE* experienceOut_ = nullptr; // optional offline teacher-data dump (CLI only)
  // Feature buffers (decision + TD learning; fixed size, no heap churn).
  float featsBefore_[LearnSystem::kFeatures] = {};
  float featsAfter_[LearnSystem::kFeatures] = {};
  // Per-subsystem RNG streams (isolated so subsystem randomness never perturbs others).
  Rng rngWorld_, rngWeather_, rngBody_, rngCognition_, rngLearn_, rngEvents_;
  // Phase 11 compute scheduler + profiling (diagnostics only; never gates tick output).
  ComputeScheduler scheduler_;
  InstructionLearningSystem instructionLearning_;
  GoalEmergence goal_emergence_;
  // User model for tracking relationship with the user
  UserModel userModel_;
  WildlifeSocialSystem wildlife_social_;

  // Heredity (organism inheritance across generations)
  std::string heredityPath_; // path to heredity file for inheritance
  bool heredityLoaded_ = false;
  float heredityInheritanceWeight_ = 0.7f; // 0.0 = fresh, 1.0 = full inheritance

  // Rebirth tracking
  uint32_t rebirthCount_ = 0;

  // Crafting system for tools, structures, food processing
  CraftingSystem crafting_;

  // Survival helper functions
  bool hasMaterialsForFarmPlot() const noexcept;
  bool hasSeeds() const noexcept;
  void plantCrop(const Structure* farmPlot) noexcept;
  void harvestCrop(const Structure* farmPlot) noexcept;
  void tendCrop(const Structure* farmPlot) noexcept;
  bool cookFood() noexcept;
  bool craftItem() noexcept;
  bool buildStructure(StructureType type = StructureType::None) noexcept;
  bool buildStructure() noexcept; // Generic build (chooses based on needs)
  bool collectWater() noexcept;
  bool preserveFood() noexcept;
  bool hasMaterialsForStructure(StructureType type) const noexcept;
};

} // namespace eidolon
