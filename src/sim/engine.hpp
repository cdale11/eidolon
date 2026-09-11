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
#include "mind/world_predictor.hpp"
#include "mind/self_model.hpp"
#include "mind/metacognition.hpp"
#include "mind/concept_formation.hpp"
#include "mind/attachment.hpp"
#include "mind/belief_ising.hpp"
#include "mind/graph_rewriting.hpp"
#include "body/crafting.hpp"
#include "body/construction.hpp"
#include "body/skill.hpp"
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

  // E0/E2 lifecycle seam: init() == initWorld() + initIndividual(), in that order,
  // with identical observable behavior. initWorld() establishes world-side state that
  // must survive across individual lives (world terrain/ecology, world time, the world
  // RNG streams, persistent structures); initIndividual() establishes per-life state
  // (body, mind, memory, skills, materials, individual RNG draws, heredity load).
  // NOTE: world-preserving rebirth (calling initIndividual() alone on death) is E2
  // work and is NOT wired yet — all callers still use init().
  void initWorld(uint64_t masterSeed, bool deterministic, int worldW, int worldH);
  void initIndividual();

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

  // Pure policy<->action mapping (complete 12-entry bijection). Exposed for tests.
  static Action policyToAction(PolicyAction a) noexcept;
  static PolicyAction actionToPolicy(Action a) noexcept;

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
  // Mutable body access for lifecycle handling and tests (e.g. forcing death to
  // exercise succession). Gameplay code must go through tick()/execute().
  Physiology& body() { return body_; }
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

  // E2 succession identity: the world persists across individual lives; each
  // successor is a new individual (new autobiography, new trust/attachment).
  // worldId_ is fixed at initWorld (the world seed); generation_ counts
  // successions; individualId_ derives deterministically from both.
  uint64_t worldId() const { return worldId_; }
  uint64_t individualId() const { return individualId_; }
  uint32_t generation() const { return generation_; }
  // Sim tick at which the current individual was born (initIndividual time).
  // Used for honest lifespan accounting in heredity (E3); monotonic world time
  // makes this exact across successions.
  int64_t birthTick() const { return birthTick_; }

  // Create exactly one successor after death WITHOUT resetting the world:
  // world time, ecology, structures and RNG streams continue; only per-life
  // state is re-initialized via initIndividual(). The successor spawns on a
  // walkable tile near the predecessor's structures (shelter inheritance),
  // falling back to the death site and then a random walkable tile.
  // Returns false if the organism is still alive (nothing to succeed).
  // The driver (server/CLI) owns death logging, saving and heredity files;
  // this only performs the in-memory succession. Restart-safe: the snapshot
  // (v16) carries world/individual/generation identity, so a save taken before
  // or after this call resumes exactly one continuing lineage.
  bool respawnSuccessor();
  
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

  // Wildlife domestication: feed/tame the nearest live prey within `radius` tiles. Feeding
  // reduces its hunger and fear; once fear drops below the taming threshold the prey
  // becomes a companion (tamed=true: follows the organism, ignores it as a threat). Returns
  // true if any prey was fed (and reports whether it became tamed via `tamedNow`).
  bool tameNearestPrey(int radius, bool& tamedNow) noexcept;

  // --- Integrated standalone systems (Phase 7-9 onwards) ---
  WorldPredictor& predictor() { return predictor_; }
  const WorldPredictor& predictor() const { return predictor_; }
  SelfModel& selfModel() { return selfModel_; }
  const SelfModel& selfModel() const { return selfModel_; }
  MetacognitionSystem& metacognition() { return metacognition_; }
  const MetacognitionSystem& metacognition() const { return metacognition_; }
  ConceptFormation& concepts() { return concepts_; }
  const ConceptFormation& concepts() const { return concepts_; }
  AttachmentSystem& attachment() { return attachment_; }
  const AttachmentSystem& attachment() const { return attachment_; }
  BeliefIsingModel& beliefs() { return beliefs_; }
  const BeliefIsingModel& beliefs() const { return beliefs_; }
  GraphRewritingSystem& conceptGraph() { return conceptGraph_; }
  const GraphRewritingSystem& conceptGraph() const { return conceptGraph_; }
  SkillStore& skills() { return skills_; }
  const SkillStore& skills() const { return skills_; }
  HabitStore& habits() { return habits_; }
  const HabitStore& habits() const { return habits_; }
  CraftingSystem& crafting() { return crafting_; }
  const CraftingSystem& crafting() const { return crafting_; }
  StructureManager& structures() { return structures_; }
  const StructureManager& structures() const { return structures_; }

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
  // Environment-driven goal emergence (slow layer): build world opportunities and
  // re-evaluate goals; called on a throttled cadence from tick().
  void evaluateGoals() noexcept;
  // Slow-layer mind systems (world predictor, metacognition, self-model, concept
  // formation, attachment, belief coherence) on a bounded cadence. Uses the shared
  // feature buffers; never runs on the fine-tick hot path.
  void stepSlowMind(const float* featsBefore, const float* featsAfter, PolicyAction pa) noexcept;
  std::string determineCauseOfDeath() const noexcept;
  // E2: walkable successor spawn near the predecessor's structures (sorted
  // order), then the death site, then a random walkable tile. Deterministic
  // given the world RNG stream state.
  Vec2i findSuccessorSpawn(Vec2i deathPos);
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
  // True only when the last decision came directly from the learned policy. Hardwired
  // sleep/wake/survival overrides must not train policy weights as if they were chosen.
  bool lastDecisionAgentic_ = false;
  // Health events: previous-tick sickness state, so illness/recovery episodes fire once
  // per transition (not every tick). Serialised for a bit-exact resume.
  bool wasSick_ = false;
  // Directed-exploration state: when no food/water is in perception range, the organism
  // walks in a fixed `exploreDir_` for up to `exploreTicks_` ticks then re-rolls — this
  // actually traverses terrain instead of bouncing randomly in a corner. Serialised.
  Vec2i exploreDir_{1, 0};
  int exploreTicks_ = 0;
  // Goal-emergence throttle: re-evaluate drives/opportunities into goals every
  // kGoalEvalInterval sim-seconds (slow layer); environment-driven goal priorities also
  // react to weather/season on this cadence. Serialised.
  int64_t lastGoalEvalAt_ = 0;
  Archive* archive_ = nullptr; // optional durable sink; never owned
  std::FILE* experienceOut_ = nullptr; // optional offline teacher-data dump (CLI only)
  // Feature buffers (decision + TD learning; fixed size, no heap churn).
  float featsBefore_[LearnSystem::kFeatures] = {};
  float featsAfter_[LearnSystem::kFeatures] = {};
  // Per-subsystem RNG streams (isolated so subsystem randomness never perturbs others).
  Rng rngWorld_, rngWeather_, rngBody_, rngCognition_, rngLearn_, rngEvents_;
  // Dedicated stream for the crafting/construction/skill systems (Farm/Cook/Craft/Build)
  // so making those actions live does not perturb the core survival/exploration randomness
  // that the phase-5 survival and determinism gates are tuned against.
  Rng rngCrafting_;
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

  // E2 succession identity (snapshot v16).
  uint64_t worldId_ = 0;
  uint64_t individualId_ = 0;
  uint32_t generation_ = 0;
  // E3: birth tick of the current individual (snapshot v17).
  int64_t birthTick_ = 0;

  // Crafting system for tools, structures, food processing
  CraftingSystem crafting_;
  // Material inventory (what the organism has gathered/crafted) — bounded, serialized.
  MaterialInventory materials_;
  // Construction manager (persistent structures on the grid)
  StructureManager structures_;
  // Skill/habit models (Beta competence + associative habit formation)
  SkillStore skills_;
  HabitStore habits_;
  // World-prediction + planning + metacognition + self-model + concept formation.
  WorldPredictor predictor_;
  SelfModel selfModel_;
  MetacognitionSystem metacognition_;
  ConceptFormation concepts_;
  // Attachment to the user (secure/anxious/avoidant/disorganized).
  AttachmentSystem attachment_;
  // Belief coherence (Ising) + concept ontology (graph rewriting).
  BeliefIsingModel beliefs_;
  GraphRewritingSystem conceptGraph_;
  // Slow-layer throttle for the integrated mind systems (predictor/metacognition/self)
  // so they run on a bounded cadence, not on the allocation-light fine-tick path.
  int64_t lastSlowMindAt_ = 0;

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
