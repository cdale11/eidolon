# Eidolon — Roadmap (Sequential Phases)

Phased build plan. Every phase ends with the same gate:

> **Compile (Release, -Wall -Wextra) → C++ unit tests → Python integration tests → run an
> actual autonomous simulation → inspect the event log for anomalous behaviour → fix root
> causes → commit (per AGENTS.md SOP).**

Each phase has explicit acceptance criteria. Nothing is considered done until its gate
passes.

---

## Phase 0 — Repository bootstrap (done in this commit)
- [x] Git repo configured (user `Clark Dale`, `clarkdale123@yahoo.com`, remote `origin` = `git@github.com:cdale11/eidolon.git`)
- [x] Documents: README, DESIGN, ROADMAP, AGENTS (with SOP), MISTAKES, CHANGELOG
- [x] `.gitignore`, `third_party/` (cpp-httplib vendored), CMake skeleton, directory layout
- [x] Verify toolchain: g++ 14.3.1, CMake 3.30.8, ninja, sqlite3 dev headers, conda env `eidolon` (Python 3.12), llama.cpp + GGUF models, ROCm 6.2.1 iGPU runtime
- Gate: `cmake -B build && cmake --build build` succeeds; empty unit-test binary passes.

## Phase 1 — Core runtime skeleton (done in this commit)
- [x] RNG (`xoshiro256++` + per-subsystem streams, seedable, persisted)
- [x] Adaptive simulation clock (fine/coarse/sleep step sizes, event queue)
- [x] Serialization primitives (versioned binary snapshot, atomic rename)
- [x] Minimal world: grid, terrain, day/night + weather stats
- [x] Minimal body: energy/hunger/thirst/fatigue/sleepPressure/temperature
- [x] Minimal headless loop: world → body → simple heuristic decision → action → log
- [x] `eidolon-sim --seed N --deterministic --days D` reproduces identical logs
- [x] Gate: deterministic replay test passes; save/load round-trips identical state.
- Note: Phase 1 organisms have no food/water sources yet, so a long run ends in starvation
  death (≈21h) — this is expected; foraging/drinking land in Phase 2.

## Phase 2 — Minimal end-to-end organism (done in this commit)
- [x] Perception: sight/hearing radii → compact feature vector (no attention yet)
- [x] Drives → simple action selection (survive, forage, drink, sleep)
- [x] Memory: hot ring of compact episodes (bounded), importance score
- [x] SQLite archive (WAL) + schema versioning
- [x] Web server (`cpp-httplib`) + minimal ChatGPT-like chat UI (sidebar, chat, input, send)
- [x] LLM bridge: `parse` (message → JSON semantics) + `respond` (snapshot → reply), llama.cpp
      compatible endpoint, timeouts, fallback replies when offline (verified live on the
      Radeon 740M iGPU via llama.cpp's Vulkan backend)
- [x] `eidolon-server` runs sim + UI; browser disconnect does not stop it
- [x] Gate: user can start a conversation; organism replies grounded in real state; killing the
      LLM doesn't stop the sim; save/load preserves the conversation's individual.
- Note: Phase 2 runs the sim on the server (the guaranteed-continuity path). The engine core
  is already UI-independent (libeidolon); browser-side compute lands in Phases 11–12
  (DESIGN §17), after the mind features stabilize.

## Phase 3 — Learning core (the mind starts) (done in this commit)
- [x] ValueNet (TD), ThreatNet (aversive + extinction), policy bandit with temperature
- [x] Attention model (top-k salience), novelty/curiosity, surprise-gated learning
- [x] Neuromodulator couplings (stress→threat learning, valence→encoding, PE→learning bursts)
- [x] Personality latent vector (16-d) updated by life statistics; drive weights evolve
- [x] `Learner` interface + metrics (inference/update counts)
- [x] Gate: seeded test shows repeated scenario → success rate rises (policy bandit); two
      identical seeds with different experiences produce different latent vectors;
      metrics increment. All 5 test seeds still survive 14 days.
- Note: event bonuses in the intrinsic reward are gated on genuine need (no eating/drinking
  self-reinforcement); chronic cold is pressure (energy drain), not an immediate threat, so
  winter never saturates the ThreatNet.

## Phase 4 — Memory systems & sleep
- [x] Full episodic encoding (time, location, participants, action, outcome, state, prediction,
  PE, importance, emotional/social relevance)
- [x] Learned retrieval weighting; decay/strengthen/rehearsal; archive + pruning
- [x] Sleep state machine + consolidation pipeline (replay, skill rehearsal, goal processing,
  summarization, association updates)
- [x] Dreams v1 (associative recombination, no LLM)
- [x] Gate: overnight consolidation improves a rehearsed skill; retrieval returns relevant
  episodes; memory DB stays bounded over long runs; sleep occurs at sane intervals.

### Teacher pipeline (Phase 4 branch — started)
- [x] Experience dump: `eidolon-sim --dump-experiences FILE` writes one JSONL record per tick
  (27 features + interpretable body/weather/context) — done.
- [x] Policy prior: `--policy-prior FILE` seeds a fresh policy from a softmax-linear fit over
  teacher labels (`.eprp`, magic "EPRP"); online learning continues on top — done.
- [x] Python tooling (conda env `eidolon`, CPU PyTorch): `python/teacher/` dataset loader,
  OpenAI-compatible teacher client (local llama-server default; NVIDIA NIM via
  `EIDOLON_TEACHER_*` env), reward-guided offline fallback, `train_prior` CLI — done.
- [x] Rate limiter + progress UI (`--progress-port`, `--keep-server`, quality report, behavioural eval)
- [x] Evolutionary prior search (`python/teacher/evolve_prior.py`) with deterministic parallel eval
- Next (post-Phase-4): LoRA distillation of the local 4B (GGUF adapter served by the existing Vulkan
  `llama-server --lora`); NIM-credentialed batch labeling; multi-seed prior training runs;
  reward-tuning scenario suite.
- Reference artifact: `data/priors/teacher_policy.eprp` (100 live-labeled records from
  4 seeds × 1 day, Qwen3-4B via the local llama-server). Regenerate: dump across seeds →
  sample stratified → label once → `python -m teacher.train_prior --label-mode teacher
  --labels labels.jsonl` (see CHANGELOG for observed behaviour change).
- Gate: a teacher-baked prior measurably improves day-1 survival/behaviour vs random init
  on held-out seeds, while late-life learning still diverges per-organism (no scripting).

## Phase 5 — Rich world & wildlife
- [x] Seasons, weather events (rain/storm/heat/cold), temperature coupling
- [x] Plants (edible/toxic/medicinal), regrowth, depletion; water sources (river/lake/spring)
- [x] Wildlife: prey (rabbits/deer) + predators (wolves/bears) with own drives (move, drink,
  flee, hunt) and fear of the organism; hunting/fleeing/attack resolution
- [x] Hazards (cliffs, deep water, disease vectors), infection/immune model, wounds
- [x] Causal chains verified end-to-end (scarcity → exploration → food; predator attacks → threat learning → defensive behavior / survival). Phase 5 gate: `test_phase5.cpp` (7 tests, all passing).
- [x] Wildlife `stepToward` cliff-aware; mid-charge attack fix (`attackOrganism`); `Grid::setElevation()` for deterministic terrain construction in tests.
- [x] Health / infection dynamics full integration: wound aging, infection spread via cellular automata (DESIGN §22), immune decay with deprivation/illness. CA updates each tick with terrain factors (swamp/deep-water as disease vectors); Physiology scales exposure/infection by nearby infected CA cells.
- Gate: `test_phase5.cpp` all green; worldgen `freqScale` = 0.01 (reverted from broken 0.04); zero-warning build; smoke run `--days 1 --seed 42` survives; event log shows normal predator-attack events, no anomalies.

### Phase 5 branch — deterministic generative systems (DESIGN §22)
Seedable, allocation-light generative content; no LLM, bit-exact replays preserved.
- [x] **Noise fields** (Perlin / simplex / value): foundation of world gen — elevation,
      climate (temperature, humidity), biome boundaries, resource density (mineral veins,
      fertile soil, water table). Multi-octave, cached per-coord. (simplex fbm biomes +
      gradient-descent river carving live in `src/world/`; deterministic per seed)
- [x] **Voronoi / Delaunay**: biome/territory tessellation, settlement placement
      (wildlife dens, the organism's shelter), Delaunay graph for landmark connectivity
      the spatial memory indexes. `src/world/voronoi.hpp/.cpp`: Fortune's algorithm
      (deterministic sweep-line), Delaunay dual graph, Poisson-disc sampling for site
      placement, territory assignment. Fully seeded, bit-exact replay.
- [x] **Cellular automata**: infection/disease spread across tiles (Conway-style with Healthy/Infected/Recovered states, terrain-factor transmission, deterministic threshold rules). Integrated with health/infection dynamics: `World::infectionCA_` stepped each tick, swamp/deep-water are disease vectors, `Physiology` scales exposure/infection by nearby infected CA cells. Snapshot serialization included. (src/world/cellular_automata.hpp/.cpp)
- [x] **Reaction-diffusion**: terrain texture patterns (mineral veins, fertile-soil
      gradients), wildlife coat patterns, biological pattern formation. Stable explicit
      Euler with capped iterations. `src/world/reaction_diffusion.hpp/.cpp`: Gray-Scott
      model, parameters for mineral veins / fertile soil / coat patterns, deterministic
      explicit Euler, snapshot serialization. `generateMineralVeins()`, `generateFertileSoil()`,
      `generateCoatPattern()` utilities.
- [x] **L-systems**: procedural plant / bush / branch geometry and river / road / root
      networks; foraging targets get spatial identity the memory system can reference.
      `src/world/lsystem.hpp/.cpp`: deterministic turtle interpretation with depth cap,
      stochastic branch probabilities, network output for river/road graphs.
      Predefined systems: fern, bush, tree, grass, river, road, root.
- [x] **Procedural generation**: ruins / landmarks, named places, semantically tagged
      objects (memory ground truth), extending the existing seedable world gen.
      `src/world/procgen.hpp/.cpp`: `ProceduralGenerator` with `Landmark` (ruins,
      shrines, caves, ancient trees, stone circles, burial mounds, springs),
      `Ruin` (rooms, depth, entrances), `NamedPlace` (regions with descriptions),
      `ObjectTag` bitmask for semantic tagging (Edible, Medicinal, Tool, Weapon,
      Shelter, Water, Danger, Safe, Social, Resource, Landmark, Hidden, Quest).
      Name generation from component tables. Snapshot serialization included.
- [x] **Agent-based models (ABM)** formalisation: the wildlife loop (sense → decide →
      act) is the canonical ABM pattern with per-agent RNG streams (seed = world seed +
      agent id). Implemented in `src/world/wildlife.hpp/.cpp`: `WildlifeAgent` with
      `species`, `state`, `pos`, `hunger`, `energy`, `alive`, `rng` (per-agent seeded
      RNG: `seed = world_seed + agent_id`). Phase 1: simultaneous sense → Markov
      decision + Boids goal direction (perception channels, fear/threat/prey drives).
      Phase 2: sequential act → move/eat/attack/starve (spatial hash for neighbours).
      Fully deterministic, seeded, serializable (`WildlifeAgent::serialize`).
- [x] **Flocking / Boids**: collective wildlife behaviour — bird flocks, prey herds,
      wolf packs (separation / alignment / cohesion + obstacle avoidance). O(neighbours)
      per-agent update, no full-grid scan. `src/world/boids.hpp/.cpp`: separation/
      alignment/cohesion + obstacle avoidance, spatial hash for O(neighbours), deterministic
      per-agent RNG, snapshot serialization. Ready for wildlife integration.
- [x] **Markov models**: explicit chains for weather transitions, wildlife behavioural
      states, the organism's sleep / wake / active state machine, and skill-stage
      progression. Inspectable, testable, tunable. `src/world/markov.hpp/.cpp`:
      template `MarkovChain<N>` with seeded RNG, normalized transitions, serialization.
      Predefined chains: `WeatherState` (Clear/Rain/Storm/Snow), `WildlifeBehavior`
      (Forage/Flee/Rest/Hunt/Wander), `SleepState` (Awake/Drowsy/Sleep/Wake),
      `SkillStage` (Novice→Master). Fully seeded, deterministic, serializable.
- [x] **ODE systems**: already core (§5 Body physiology). Documented the integrator
      (explicit Euler with fixed step, max-rate caps) and added unit tests against
      analytic solutions for each drive. `src/body/ode_tests.hpp/.cpp`,
      `tests/test_ode.cpp`: 12 analytic ODE tests (energy, hunger, thirst, fatigue,
      sleep pressure, body temp, health) with Euler integration vs exact solutions;
      convergence verified. `tests/test_ode.cpp`: 2 tests passing.
- [x] **Evolutionary algorithms** (offline tooling): `python/teacher/evolve_prior.py` evolved
      policy-prior weights directly on held-out seeds (PoC done — seeds the population with
      the teacher artifacts, tournament + crossover + gaussian mutation, deterministic RNG,
      improves on the teacher priors on survival-weighted fitness). Next: wildlife behaviour
      parameters, recipe tuning (deferred to later phases).
- [x] **Grammars / formal grammars**: structured goal / event templates for episodic-memory
      compression, recipe production rules, grounded utterance templates (used by Phase 10).
      `src/world/grammar.hpp/.cpp`: CFG engine with deterministic/stochastic derivation,
      CYK-style parsing, weighted productions. Predefined grammars: goal templates,
      event templates, recipe production rules, grounded utterance templates.
- Gate: every generated object reproduces from its seed (determinism tests); content couples
  to behaviour (perception / affordances / memory), never cosmetics; generated ecology
  measurably changes foraging strategy over days; Markov wildlife produces testable state
  transition sequences; Boids flocking is visible in the sim; ODE drives pass analytic
  reference tests.

## Phase 6 — Skills, tools, crafting, construction
- [x] Skill models (Beta/Bernoulli competence), procedural store, habit formation (`src/body/skill.hpp/.cpp`)
- [x] Crafting with learned recipes (seeded basics: fire, sharp stone, spear, shelter) (`src/body/crafting.hpp/.cpp`)
- [x] Construction: persistent structures on grid (shelter, walls, campfire, storage, farm
  plots), stored/retrieved in snapshot and exercised by engine Build/Farm actions
  (`src/body/construction.hpp/.cpp`, `src/sim/engine.cpp`)
- [x] Affordance discovery: tool used in unexpected ways → new procedures (`src/body/affordance.hpp/.cpp`: `AffordanceSystem` with tool/material affordance registration, discovery from unexpected usage, procedure generation hooks)
- Gate: organism builds a shelter that persists across save/load; discovers at least one
  novel tool use in a seeded run; skill competence improves with practice.

### Phase 6 branch — generative crafting, construction & invention (DESIGN §22)
- [x] **Shape grammars**: construction geometry — shelter / wall / campfire / storage /
      farm-plot forms generated from a shape grammar seeded by site context and available
      materials; tools get anatomical structure (handle / blade / binding) from a shape
      grammar. Turtle interpretation with depth cap; deterministic. Implemented as
      L-systems in `src/world/lsystem.hpp/.cpp`.
- [x] **Graph rewriting** (recipe / tech tree): the recipe graph is rewritten when new
      crafting combinations are discovered or when an experiment succeeds; the organism's
      "known recipes" set is a deterministic graph that grows under rewrite rules.
      Implemented in `src/body/crafting.hpp/.cpp` with `CraftingSystem::experiment()` and
      `AffordanceSystem::generateProcedureFromAffordance()`.
- Gate: a shape-grammar-built shelter persists across save / load; GP discovers at least
  one novel recipe per seeded run; the recipe graph is inspectable and reproducible.

## Phase 7 — Planning & world model
- [x] WorldPredictor (one-step transition model) + confidence, trained from the engine's
      bounded slow layer
- [x] Forward/beam planning over primitives using learned models; replan on surprise
- [x] Goal emergence from drives + state + opportunities (goals not specified by us appear)
- [x] LLM-assisted high-level plan proposals (validated, executed by runtime only)
- Gate: planner outperforms greedy policy in a resource-fetch benchmark; unexpected
  environmental change triggers replanning.

## Phase 8 — Social cognition & learning from the user
- [x] User model: familiarity, trust, affection, fear, respect, resentment, reciprocity,
      expectations (updated by consequences of interactions; may be wrong)
- [x] Wildlife social models (species + individual familiarity/fear)
- [x] Attachment: user absence → attachment pressure; reunion affects state
- [x] Learning from user: verifiable facts → beliefs with confidence; feedback shapes behaviour
- Gate: seeded test — user warnings that prove true raise trust and change behaviour; false
  warnings lower trust; long absence produces measurable attachment response.

### Phase 8 branch — belief dynamics & social norms (DESIGN §22)
- [x] **Ising models** (social belief / norm dynamics): the organism's binary beliefs and
      trust states as spins; evidence = fields; consistency = couplings. Produces coherent
      worldviews, belief flips under strong evidence, cognitive dissonance when evidence
      conflicts. Spin update rule is deterministic + bounded noise; convergence testable.
- [x] **Markov models**: explicit chains for the user model states (familiar / stranger /
      trusted / feared) and the wildlife social states (friend / neutral / threat).
      `src/mind/markov.hpp/.cpp`: template `MarkovChain<N>` with deterministic transitions,
      steady-state computation, serialization. Predefined chains for user/wildlife states.
- Gate: a belief flip on strong evidence is reproducible; belief clusters persist across
  save / load; trust dynamics match a calibrated Ising simulation.

## Phase 9 — Self-model, concepts, metacognition
- [x] Self-model: body/abilities, autobiographical summary, preferences, beliefs, goals,
      reputation, future expectations — all experience-updated (`src/mind/self_model.hpp/.cpp`)
- [x] Metacognition: uncertainty, confidence, self-prediction, failed-prediction recognition →
      reflection triggers (`src/mind/metacognition.hpp/.cpp`)
- [x] Concept formation: incremental bounded clustering in embedding space, expandable
      ontology, LLM-assisted naming (rare), wired into the engine slow layer
      (`src/mind/concept_formation.hpp/.cpp`, `src/sim/engine.cpp`)
- Gate: organism forms and names a concept it was never told about; self-model changes after
      significant events; it reports uncertainty honestly in conversation.

### Phase 9 branch — concept ontology & belief coherence (DESIGN §22)
- [x] **Graph rewriting**: concept ontology as a typed graph grown by rewrite rules when
      the organism forms associations; belief graph (§8) rewritten when evidence resolves
      contradictions. Rewrite rules = deterministic productions applied under the sim seed.
      Implemented in `src/mind/graph_rewriting.hpp/.cpp`: typed graph (Concept/Relation/Property/Event/Category nodes; IsA/HasProperty/Causes/PartOf/RelatedTo/Opposes/Enables edges), `RewriteRule` with pattern matching and replacement, incremental `match_pattern` / `apply_rules`, `sync_with_concepts` hook, full serialization.
- [x] **Ising models**: belief coherence — the organism's binary beliefs as spins, evidence
      as fields, consistency as couplings. Produces stable belief networks, flips under
      strong contradictory evidence, quantifiable cognitive dissonance.
      Implemented in `src/mind/belief_ising.hpp/.cpp` (from Phase 8 branch): `BeliefIsingModel` with `BeliefSpin` states (+1/-1/0), external fields, couplings matrix, Glauber dynamics (`update()`), `compute_energy()`, `compute_dissonance()`, `get_coherent_clusters()`, full snapshot serialization.
- Gate: the concept graph is inspectable and reproducible from the seed; belief coherence
      score improves with experience; belief flips on strong evidence are reproducible.

## Phase 10 — Dreams v2, reflection, narrative language (complete)
- [x] Dreams influence associations measurably: `src/mind/memory_system.cpp::dream()` recombines episodes sharing location/participants/outcome/action, creates dream traces that strengthen source episodes and perturb policy toward recombined sequences (deterministic via seeded RNG)
- [x] Slow layer reflection with LLM (rate-limited): `src/mind/reflection.hpp/.cpp` — `ReflectionSystem` with `reflect_on_recent_events()`, `summarize_absence()`, `generate_life_review()`, `answer_about_past()`; min-ticks-between-LLM rate limiting; honest uncertainty for unrecorded events
- [x] "What happened while you were away" grounded in actual event timeline: `summarize_absence()` extracts events between lastSeenTick and currentTick, builds LLM prompt
- Gate: conversation asks about a real past event → accurate details; asks about an
  unrecorded event → honest uncertainty (no fabrication, tested).

### Phase 10 branch — grounded language via formal grammars (DESIGN §22)
- [x] **Formal grammars**: structured goal / event templates for episodic-memory
      compression ("thirsty → went to water → drank"), recipe production rules, and
      grounded utterance templates for the language bridge (§14) — replaces some LLM
      dependence with deterministic, state-seeded language. Production rules are a
      deterministic rewrite system; choices driven by the sim seed and the organism's state.
      Implemented in:
      - `src/world/grammar.hpp/.cpp`: CFG engine with deterministic/stochastic derivation, CYK parsing
      - `src/mind/grounded_language.hpp/.cpp`: `GroundedLanguage` generates "what did you do today?", daily summaries, past-event QA, greetings, need statements, observations — all grounded in actual event log, honest uncertainty for unrecorded topics
- Gate: the organism can answer "what did you do today?" with a generated sentence that
      is factually grounded in its actual event log, without an LLM call; utterances are
      reproducible from the seed and state.

## Phase 11 — Portable WASM client compute (complete)
- [x] Compile the same `ReplicaCore` to WebAssembly (Emscripten): plain WASM build working
  - Emscripten toolchain configured (`cmake/Modules/Platform/Emscripten.cmake`)
  - Static library `libeidolon_replica_core.a` builds successfully (~1.1 MB)
  - Test executable links and runs in Node.js
  - Native builds and tests unaffected
- [x] WASM SIMD build
  - `-msimd128` flag enabled
  - Static library `libeidolon_replica_core_simd.a` builds successfully (~1.1 MB)
  - Test executable links and runs in Node.js
- [x] Multithreaded builds (Workers + SharedArrayBuffer)
  - `-pthread` + `-sPROXY_TO_PTHREAD=0` + `-sPTHREAD_POOL_SIZE=4` enabled
  - Static library `libeidolon_replica_core_mt.a` builds successfully (~1.1 MB)
  - Test executable links and runs in Node.js
- [x] Capability detection → `ComputeProfile` (SIMD, Workers, SAB, WebGPU, WebGL fallback,
      concurrency, memory limits); auto backend selection hierarchy (WebGPU → WASM SIMD+MT →
      plain WASM → server fallback)
      Implemented in `src/mind/compute_profile.hpp/.cpp`: `ComputeProfileDetector::fromJsCapabilities()`,
      `selectBackend()`, `getBackendPriority()`, `isBackendViable()`, `estimatePerformance()`,
      with serialization support.
- [x] Heredity system (inheritance mechanism for organism death/restart)
  - `src/mind/heredity.hpp/.cpp`: `HeredityGenome` saves policy weights + personality latent
  - `HeredityManager`: `extractGenome()` on death, `createOffspring()` with mutation, `applyHeredity()` to fresh engine
  - `HeredityGenome` serialization for persistence across restarts
  - Policy weights accessor added; PersonalityLatent operator[] for mutation/blending
  - Engine non-const `learn()` for heredity application
- [x] `ComputeScheduler`: priority queues (chat/responsiveness > active sim > background
      consolidation); worker separation (world / physiology-cognition / neural-ML /
      memory-consolidation); compact message passing, no big buffer transfers
      (`src/mind/compute_scheduler.hpp/.cpp`; `--fidelity` on eidolon-server; per-domain
      profiling fed to the Diagnostics panel).
- [x] Adaptive fidelity: constrained clients reduce sim frequency, model budget, world detail —
      identity unchanged (`FidelityController` in `src/mind/compute_profile.hpp/.cpp`;
      `--fidelity 0|1|2|3`; affects pacing/model-budget/world-detail only, never the
      deterministic tick).
- [x] Profiling from the beginning: per-backend sim steps/sec, simulated hours/sec, inferences/
      sec, worker utilization, WASM memory, latency — diagnostics panel in the UI
      (`/api/metrics` + collapsible Diagnostics panel in the sidebar).
- [x] **Genetic programming** (offline tooling): evolve recipe trees (crafting / tool
      invention) and behaviour trees (action sequences) validated against world physics;
      tournament + subtree crossover / mutation; fitness = sim validation; depth cap.
      Discovered procedures become recipes the organism can use at runtime.
      (`python/teacher/gp_evolve.py` — recipe trees and behavior trees with
      deterministic RNG, tournament selection, subtree crossover/mutation, depth cap;
      emits flat artifacts `CraftingSystem::loadEvolvedRecipes` imports at runtime).
      (`python/teacher/evolve_prior.py` provides the policy-weight evolution framework.)
- Gate: same seeded scenario produces the same individual state on native and WASM
      (parity test); no heavy work on the main UI thread; fidelity reduction works.

## Phase 12 — Synchronization, offline persistence & backend selection (complete)
- Checkpoint/delta sync protocol (compact binary deltas: physiology, weight deltas,
  memories, beliefs, relationships, skills, concepts, world events, sim clock; batched +
  compressed; no tick streaming)
- Client-authoritative cognition/learning; server validates structural consistency;
  configurable world-state authority (client vs server-authoritative for future shared
  worlds)
- Offline client execution: organism keeps running without connectivity; reconcile + upload
  on reconnect; browser checkpoints (IndexedDB/OPFS) survive tab crash
- Server roles: session/auth, persistent storage, sync, optional LLM endpoint, native
  headless fallback for unattended life
- Benchmark suite: native / WASM CPU / WASM SIMD / WASM MT / WebGPU (where available) →
  auto-select fastest stable backend; Xbox-style restricted clients measured, not assumed
- Gate: kill browser mid-run → server fallback continues life; reconnect → clean reconcile
  (no loss/duplication); benchmarks produce the selection decision automatically.

### Phase 12 status
- [x] Backend benchmark suite: `eidolon-sim --bench [--bench-ticks N] [--bench-json]` measures
      hot-path tick latency (p50/p95/max), sim/wall throughput, snapshot size+cost, RSS,
      learner counts, ticks-by-class; builds a host ComputeProfile from measured throughput
      and reports the auto-selected backend (weak→ServerFallback, SIMD→WasmSimd,
      SIMD+MT→WasmSimdMt, WebGPU→WebGPU). Budget gate: p50 ≤ 2 ms fine tick (measured ~0.06 ms).
- [x] Native vs WASM parity test (same seeded scenario → same individual state)
- [x] Binary snapshot download/upload endpoints (client-authoritative persistence path)
- [x] Checkpoint/delta sync protocol (compact binary deltas + compression)
- [x] Server accepts client ComputeProfile; auto-select fastest stable backend
- [x] Offline client persistence: the WASM worker checkpoints its local state to
      IndexedDB every ~5s and resumes from it when the server offers no snapshot, so a
      tab crash/reload does not destroy the organism.
- [x] Reconnect reconcile: `--world-authority server` rejects a stale client snapshot
      (sim-time behind the headless fallback) via a header-only sim-time peek, so forward
      progress is never rolled back. Default `client` mode stays client-authoritative.
- [x] Session/auth: `--api-key` gates mutating POST endpoints with a constant-time
      Bearer/`?key=` check (single-user shared secret); read-only GET stays open.
- [x] `--world-authority client|server` flag wires configurable world-state authority.
- [x] Integration test `python/tests/test_phase12.py` (auth gating, stale-snapshot reject,
      client/server authority modes).

## Phase 13 — Performance & long-run stability (complete)
- [x] Profiling: hot-path allocation audit — removed a per-tick `std::vector<float>`
      terrain-factor allocation + O(w*h) scan in `World::update` (cached in a member,
      rebuilt on generate/deserialize; +15% throughput, bit-exact).
- [x] Benchmarks: 30 sim-days headless = **58 s** (budget < 10 min, 10× headroom);
      fine tick p50 = **53 µs** (budget ≤ 2 ms, 37× headroom); snapshot = 286 KB in
      **0.9 ms** (budget ≤ 100 ms).
- [x] Long-run tests: RSS plateaus flat at ~9.8 MB with `--archive` (no drift);
      SQLite `memory.db` bounded (~3.4 MB over 30 days, pruning active).
- [x] Observability: `/api/metrics` + headless `metrics.log` + `--bench` report all
      present; diagnostics panel wired (Phase 11/12).
- Gate: all performance budgets met; long-run memory stability green (RSS sampled
  over a full 30-day run, flat after warm-up).

## Phase 14 — Full test matrix & release
- Complete test suite per DESIGN §19 (unit, seeded replay, behavioural scenarios,
  client-compute & sync, backend benchmark suite, adversarial: corrupt save, kill -9 during
  save, LLM garbage, divergent sync)
- Documentation finalization; CHANGELOG entries; performance report
- Gate: full `ctest` + integration suite green on a clean checkout.

## Phase 15 — Client-side offload foundation (complete; ahead of 13–14 on user direction)
- [x] Server stops local ticking when a capable client claims the sim
      (`clientComputing_` flag; `simLoop` idles instead — life continues via
      client snapshots, server persists + answers chat).
- [x] WASM artifacts served to the client (`GET /api/wasm/core.wasm`,
      `GET /api/wasm/core.js`; resolves build-wasm-mt → build-wasm-simd →
      build-wasm by selected backend).
- [x] Client tick-state uploads (`POST /api/client/snapshot`; validates,
      restores, persists; rejected while the server is hosting).
- [x] `POST /api/compute-profile` now arms/disarms offload via
      `maybeStartClientComputing()` (WasmPlain/WasmSimd/WasmSimdMt → client;
      others stay server-side).
- [x] `run_eidolon.sh` — one-command launcher: starts llama-server (Vulkan
      iGPU, waits for health, reuses an already-healthy instance, kills only
      its own on exit) + eidolon-server on 0.0.0.0:8081.
- [x] Client-side Web Worker host: `tools/eidolon-wasm-worker.cpp` exports a C API
      (`eidn_new/init/restore/tick/snapshot/...`) built as `eidolon-worker.{js,wasm}`
      for plain + SIMD Emscripten targets (wasm `-msimd128`); a browser Worker script
      (embedded `kWorkerJs`, served at `/api/wasm/worker.js`) restores the server
      snapshot, ticks at fidelity pacing with adaptive slice budgets, and relays one
      snapshot per second back through the page -> `POST /api/client/snapshot`.
      Sidebar toggle "Compute: server|THIS TAB" (capability-detected, SIMD+ only);
      tab close/beacon disarms instantly.
- [x] Continuity on silence: server resumes the local tick loop after 15s without a
      client snapshot (tab crash/close never freezes the organism); periodic
      wall-clock save while offloaded; client death notice hands control back.
- [x] Offload interlocks: explicit `"offload":false` disarm, uploads rejected while
      server-owned, weak profiles never arm (only WasmSimd/WasmSimdMt do),
      COOP/COEP headers for future MT/SAB clients.
- [x] Parity + regression harnesses: `tools/wasm_worker_smoke.cjs` (fnv digest match
      vs native `eidolon-parity-dump`, wasm resume-parity) and
      `python/tests/test_client_offload.py` (protocol e2e incl. silence-resume).
- [x] Bug found and fixed en route: `PolicyAction` had 12 values but
      `Engine::policyToAction`/`actionToPolicy` mapped only 6 — advanced actions
      (Farm/Cook/Craft/Build/CollectWater/Preserve) silently became Observe and their
      rewards trained Observe's policy weights. Fixed the bijection, expanded the
      6-entry dump `kActionNames` (which then SIGSEGVed `--dump-experiences`),
      added `engine_policy_action_roundtrip_all12`. See MISTAKES 2026-09-03.
- Gate: native + wasm + wasm-simd builds warning-free; unit suite green (120 tests);
      integration suite green incl. offload protocol; parity digest identical across
      native/plain/simd wasm; `eidolon-sim --data data/runs/check --days 1` healthy.

## Future directions (deferred by design)
Logged from user requirements; not yet sequenced into phases. No LLM in the hot path
and "client does the maximum work" invariants apply to all of them.
- **Audit findings to implement next** *(repository audit, 2026-09-11)*:
  - [x] Snapshot all declared runtime counters: `Engine::Stats` now includes advanced
        action/outcome counters (`actionsFarm`, `itemsCrafted`, `structuresBuilt`, etc.)
        and snapshot v14 persists them with a field-specific round-trip test.
  - [x] Persist `LearnSystem::successRate_`: snapshot v14 writes/reads it, exposes it via
        `lifeStats()`, and verifies field-specific round-trip coverage.
  - [x] Serialize or eliminate `EventQueue events_`. Most CLI events are drained by
        `tickAndLog`, but a snapshot taken after raw `tick()` and before log draining can
        lose queued timeline/archive events. Snapshot v15 now persists pending queued
        events, with queue round-trip coverage.
  - [x] Consolidate the two structure stores: engine `StructureManager` is now the real
        construction system, while `World::SimpleStructure` is separate, unsaved by
        `World::serialize`, and still queried by some water-collection logic. The unsaved
        world store is removed; well collection and `/api/world/summary` now use the
        persisted `StructureManager`.
  - [x] Make grounded offline past-tense chat truly archive-backed. The server routes
        "what did you do" questions to `GroundedLanguage`, but SQL event extraction is
        now backed by a bounded `Archive::timeline()` query implemented by SQLite, so
        replies summarize durable episode/event facts instead of a stub.
  - [x] Tighten replay semantics around non-policy overrides: `LearnSystem::learnStep`
        documents `agentic=false` for hardwired sleep/wake/emergency ticks, but `Engine`
        now tracks whether the decision came directly from the learned policy and suppresses
        policy updates for hardwired overrides. Night sleep hysteresis was widened to avoid
        rapid sleep/wake flapping found during smoke-log inspection.
  - [x] Fix minor determinism/portability debts found in the audit: validate restored
        organism position, initialize wildlife per-agent RNG before randomized spawn
        attributes, avoid `reinterpret_cast` coordinate serialization in construction,
        and clarify that policy/action mapping intentionally excludes `Sleep`. Restore now
        rejects invalid organism positions, wildlife spawn uses the per-agent RNG before
        randomized attributes, body coordinate serialization avoids aliasing casts, and the
        `Sleep` mapping is documented/tested as a non-agentic neutral label.
- **Deeper world / playground**: hobbies with real procedural depth — gardening (plant,
  tend, harvest over days/weeks, seasonal yield, skill progression), reading books (world
  artifacts with retrievable content the organism actually learns from, not cosmetic
  flavour), leisure that competes meaningfully with survival drives.
- **File attachments**: the organism reads documents the user drops into the chat (PDF /
  text first; images later via a small local vision model). Becomes persistent, retrievable
  memory, not prompt text.
- **Internet access**: configurable, user-gated browsing so the organism can research —
  always as *content it reads and learns from*, never as a live-mind backdoor.
  - [x] Configurable `--internet-enabled` flag, search endpoint/key, timeouts, limits.
  - [x] Search endpoint: `POST /api/browse/search` (query → ranked results + snippets).
  - [x] Fetch endpoint: `POST /api/browse/fetch` (URL → extracted text content).
  - [x] Durable reading corpus (not yet end-to-end skill learning): approved
        fetches/manual resources are stored in SQLite `internet_resources`, logged as
        `read` timeline events, surfaced in metrics, and exported for offline retraining via
        `python -m teacher.internet_corpus`.
  - [x] Graceful degradation: structured error responses, CAPTCHA detection.
  - [ ] Integration with organism's decision loop (browse as tool/action).
  - [x] User consent UI in chat (per-request approval): sidebar "Read web resource" either
        stores pasted extracted text or calls `fetch` with `learn:true`.
  - [ ] Rate limiting + caching.
  - [x] Proper search API integration (SearXNG, DuckDuckGo, SerpAPI, Brave, Google, custom).
  - [x] HTTPS support via OpenSSL-linked httplib in native builds.
- **Client-side offload** *(implemented — Phase 15)*: most compute now migrates to the
  client per DESIGN §17 (a capable browser runs the sim in a Web Worker and posts
  snapshots back); headless fallback continues life when the client is away.
- **Learning from the user's actual speech (text)**: the organism understands and acts on what the
  user says — typed instructions ("eat", "sleep", "go to the river", "avoid wolves")
  become validated, structured goals the organism pursues through its normal planning loop
  (never injected as prompt text, never mutating world state directly). Instruction
  following improves with repetition; instructions that prove harmful lower the organism's
  trust in the user's advice (ties into Phase 8 user model).
  - [x] Intent parsing → structured goal actions (18 types: GoTo, FollowMe, Explore, Forage, Drink, Rest, Sleep, Flee, Avoid, Build, Craft, Observe, Status, Greet, Thank, Stop, Wait, Cancel)
  - [x] Instruction validation against current state + safety (never inject prompt text)
  - [x] Trust modulation: successful instructions → trust+, harmful → trust- (implemented in InstructionLearningSystem + UserModel)
  - [x] Repetition learning: repeated instructions → stronger habit weights (InstructionMemory with habit_strength)
  - [x] Integration with GoalEmergence + Policy for execution (Engine::processUserInstruction maps intents to GoalTypes and updates GoalEmergence)
  - [x] Trust integration with UserModel (Phase 8) (UserModel trust, familiarity, reciprocity updated via record_interaction)
- **Organism-driven self-improvement**: the organism proposes its own improvements — to
  itself (skills to rehearse, habits to form, memory to consolidate, survival strategies to
  try) and to the game (from a performance perspective: "I spend most ticks wandering —
  better pathfinding to water would save energy"; and from an emergence/consciousness
  perspective: "I avoid the cold meadow, so I never see the berries there"). Suggestions
  come from its own planner/metacognition inspecting its metrics and are offered in
   conversation as *proposals* the user may adopt. The expansion plan E7 below extends this
   toward evaluated retraining and isolated code experiments; deployment autonomy remains
   provisional.
- **Time-of-day awareness** *(slices 1–2 implemented)*: the organism's replies
  reflect its circadian state — whether it is awake/asleep, drowsy, tired, hungry or
  thirsty at the moment of speaking — and it lives on a real day/night rhythm. Grounded
  in the same snapshot that powers `respond` (no new LLM dependence), so a question at
  3am gets a groggy answer.
  - [x] **Slice 1 — circadian snapshot + tone-aware replies** (`src/llm/bridge.hpp/.cpp`):
        6 deterministic derived fields (`phaseOfDay`, `timeOfDayPhrase`, `seasonName`,
        `physiologicalState`, `primaryNeed`, `circadianTone`) appended to
        `CognitiveSnapshot`. `respond` prompt instructs the LLM to set tone from them;
        `fallbackReply` opens with a "Good morning/afternoon/evening/night" greeting
        and carries the time-of-day phrase in every branch (asleep / threat / thirsty /
        hungry / tired / sick / pained / healthy). 8 C++ unit tests + integration test
        updated; verified live (3am → "peaceful in the deep night"; midday → "I'm awake
        at midday in spring"; tired at night → "drowsy in the deep night"). Pure
        functions of existing snapshot state — no new persistent state, no extra LLM
        calls, determinism preserved.
  - [x] **Slice 2 — behavioral circadian sleep/wake rhythm** (`src/core/clock.hpp`,
        `src/sim/engine.cpp`): `SimClock::daylight()` (pure cosine envelope, 0=midnight,
        1=noon) drives a diurnal scheduler in `Engine::decide` — at night the organism
        beds down unless survival valves (`thirst>55`/`hunger>70`/`pain>40`) say otherwise,
        and wakes at daybreak; by day it stays active unless genuine sleep pressure forces
        a nap. This fixes an always-awake death spiral (energy drained faster than foraging
        could replace it). Derived from the clock only — no new persistent state, bit-exact
        determinism and native/WASM parity retained.
  - [x] **Slice 3 — sleep architecture (drowsy→light→deep→REM)**: `Physiology` models a
        real night of staged sleep (DESIGN §13) with per-stage metabolic/recovery profiles;
        the chat snapshot names the current stage and `/api/status` reports it.
  - [x] **UI circadian indicator**: the status bar shows a phase-of-day dot (deep-night /
        night / dawn / day / dusk / asleep) + a `phaseOfDay`/`daylight` field in
        `/api/status`, so the organism's time of day is visible in the UI.
  - [x] **Audio cues**: soft non-blocking WebAudio tones on key state transitions —
        distress (low health/steep health drop), alert (predator closing in), calm (wake).
        Throttled (≤1/8s) and toggleable via the sidebar "Sound" button; never blocks the
        loop and degrades silently when the browser denies audio.
  - [x] **Timezone-aware chat**: `/api/send` accepts the user's local `user_hour`; the
        fallback greeting ("Good morning/afternoon/evening/night") follows the *user's*
        local time while the organism's circadian content stays grounded in its own sim
        clock (invariant: the organism exists independently).
- **Wildlife domestication / pets** *(implemented)*: repeated friendly feeding lowers a
  prey's fear of the organism; below a threshold the prey becomes a companion (`tamed` —
  follows at a short distance, ignores the organism as a threat, and its tame is recorded
  as a social Tamed episode). Feed via `POST /api/tame` or the engine's `tameNearestPrey`.
- **Environment-driven goals** *(implemented)*: goal emergence is now wired into the tick
  (throttled, slow layer) and its priorities react to weather/season — shelter becomes
  urgent in storms/snow/winter and hydration is prioritised in hot summer, so scarcity and
  climate shape behaviour beyond immediate drive satisfaction. (Firewood/stockpiling is a
  later material-economy refinement.)
- **Health events** *(implemented)*: concrete Illness/Recovery episodes fire on
  infection/recovery transitions and are stored as aversive/rewarding memories the user
  can witness in the event log and chat timeline (extending the health/immune model).
- **Embodiment feedback**: the organism's state rendered visually in the UI (avatar,
  mood/state indicators, weather-worn appearance) — the aural layer (audio cues for
  alert/calm/distress) is now live; the richer visual avatar/mood rendering remains.

---

### Reply quality — perceived response quality (Q0–Q4)

Authority: the reply pipeline is `Server::sendMessage` → `makeSnapshot` →
`LLMBridge::parse/respond` (or `groundedReply`/`fallbackReply` offline). These slices
extend that pipeline; they do not replace it. Invariants hold throughout: no
personality/biography as prompt text (trait words below are data summaries of the
persisted latent vector, same as today's floats), LLM never mutates state, every reply
stays attributable to snapshot/archive facts. Work in slice order — Q0 fixes active
fabrication sources before Q1–Q4 tune quality on top.

#### Q0 — Stop telling the LLM false facts (correctness first)
- [x] `skillSummary` is a hardcoded placeholder (`"forage=0.8 drink=0.6 craft=0.1"`,
      `bridge.cpp`): wire real `SkillStore` competence values; omit skills with no
      practice instead of inventing numbers.
      (Done: Beta-mean per practiced skill, `"no practiced skills yet"` when fresh.)
- [x] `terrain` is a raw enum int (`"terrain=3"`): send the terrain/biome name.
      (Done: `"forest (boreal forest)"` style via `terrainName`/`biomeName`.)
- [x] `chatComplete` hardcodes a model GGUF path: use the configured model/endpoint.
      (Done: `LLMBridge::setModel`, neutral `"eidolon-llm"` default, `--llm-model` flag.)
- [x] Send all significant `activeGoals`, not just `activeGoals[0]`.
      (Done: capped list of 5 joined by `joinGoalNames`.)
- **Gate:** snapshot unit test asserts no placeholder constants, real terrain names, all
  goals present, and model string from configuration; existing bridge tests stay green.
  (Done: 4 new tests in `tests/test_bridge.cpp`; full gate green.)

#### Q1 — Give the LLM conversation memory (biggest perceived gap)
Today `respond()` sees only the current message + snapshot: the organism cannot refer
to earlier turns, answer follow-ups, or remember what the user just told it.
- [x] Attach the bounded tail of the current conversation (last ~6–10 exchanges from
      SQLite `messages`, token-capped) to the `respond` prompt as dialogue history.
      (Done: `listRecentMessages` tail query + up-to-10-turn history in the payload,
      ~1500 chars / ~400 tokens, newest-wins truncation, current message excluded.)
- [x] Keep history bounded and attributable: truncate old turns first, never send the
      whole archive; history informs wording, snapshot/archive remain the only sources
      of world fact (restate the no-invention rule with history present).
      (Done: system-prompt history clause — snapshot wins on contradiction.)
- **Gate:** multi-turn test — "I am building a shelter" … "what did I just say I was
  building?" is answered correctly with the LLM on; token budget per reply stays under
  a documented cap; offline behavior unchanged.
  (Done: recording-stub test asserts the second respond call carries the first
  exchange and excludes the current message; unit tests lock format + budget.)

#### Q2 — Ground every reply in the archive, LLM on or off
Today `groundedReply` (archive timeline) runs only when the LLM is down; with the LLM
up, past-tense accuracy rests on 6 terse episode stubs (`"foraged (t=...)"`).
- [x] Route memory-referencing questions (`parsed.references_memory`) through
      `GroundedLanguage`/archive first and pass the resulting grounded facts INTO the
      `respond` prompt, so the LLM phrases verified facts instead of recalling them.
      (Done: `grounded_memory` is computed by the server from the archive before
      `respond`; the prompt instructs the model to phrase those facts, not invent.)
- [x] Enrich `recentMemorySummary`: outcomes, places and elapsed time
      ("foraged berries near (12,28) yesterday, success") instead of bare kind+tick.
      (Done: compact entries now include owner, event, coordinates, day and outcome.)
- [x] Attribute inherited episodes in the summary (`sourceIndividualId != 0` →
      "my predecessor …"), reusing E3 attribution rather than a second mechanism.
      (Done: inherited hot-ring episodes are labeled `predecessor`; unit-tested.)
- **Gate:** "what did you do yesterday / while I was away" matches the archive with
  LLM on and off; predecessor questions cite the predecessor, never the self.
  (Done: integration test seeds the archive, enables the LLM, and verifies the
  `respond` payload contains the archive-derived memory; existing offline path still
  uses `GroundedLanguage`; predecessor summary attribution is unit-tested.)

#### Q3 — Make the offline voice answer the question
`fallbackReply` ignores the user text except for 3 hardcoded patterns; everything else
gets a status dump regardless of what was asked.
- [x] Intent-keyed offline templates: greeting, status/health, location/weather,
      goals/plans, skills, relationships, help/capabilities — each grounded in snapshot
      fields, with the status dump as the last resort instead of the default.
      (Done: `fallbackReply` routes through `IntentParser`, including 4 new
      `Question*` intents; urgent state overrides — dead/asleep/critical/predator/
      drives — still speak first; command intents state relevant facts without
      promising action.)
- [x] Reuse the `parse` result intent routing already present (`IntentParser`) rather
      than keyword-spotting twice.
      (Done: shared parser extended with question intents + "where are you";
      question intents excluded from goal injection like greetings.)
- **Gate:** a fixed questionnaire (greet/status/where/goals/skills/help) gets topical
  grounded answers fully offline; replies stay deterministic per snapshot.
  (Done: 10 unit tests + live offline questionnaire integration test; templates
  are pure functions of the snapshot.)

#### Q4 — Voice consistency and evaluation
- [x] Derive personality trait words from the latent vector thresholds
      ("cautious" for high threat sensitivity, …) to replace raw floats, keeping the
      numbers out of the prompt; tone selection stays a pure function of snapshot state.
      (Done: `traitWords` maps thirds of [-1,1] for the 7 voice-relevant dims;
      drives became a strongest-first ranking; `circadianTone` untouched.)
- [x] Per-reply sampling: short factual answers (low temperature, tight token cap) vs
      open smalltalk; keep the 4B iGPU latency budget explicit per class.
      (Done: factual = 0.2/128 tokens for memory/status/orders, open = 0.7/256;
      budgets documented on the constants; `chatComplete` takes temperature.)
- [x] Reply-quality harness: scripted multi-turn scenarios scored on grounding (every
      checkable claim traces to snapshot/archive), non-fabrication (unknowns admitted),
      relevance (answers the question asked), and voice consistency — run before/after
      each Q slice so improvements are measured, not felt.
      (Done: `python/tests/test_reply_quality.py`, auto-run by the integration
      driver — 21 checks: offline relevance/latency, non-fabrication, archive
      grounding, prompt structure, trait/drive words, voice stability, sampling
      classes, stub-LLM latency. Baseline before Q4 was 14/20 with exactly the
      trait/sampling gaps failing; after: 21/21.)
- **Gate:** harness scores improve on grounding + relevance with no fabrication
  regressions; p95 reply latency within budget on the reference machine.
  (Done: grounding + relevance stayed green from Q0–Q3, fabrication checks
  green, offline p95 ≈ 1ms vs 2000ms CI budget; iGPU reference budgets
  explicit in code, real-model timing left to manual runs.)

**Execution order:** Q0 → Q1 → Q2 → Q3 → Q4. Q1 and E3-slice-2 (predecessor
relationship retrieval) should land together where they touch the same prompt code.

---

### Expansion plan — implementation gaps only (E0–E7)

Authority: DESIGN's "Expansion agreement — persistent world, mortal individuals".
These unchecked items extend existing work; do not rebuild completed features. Before each
slice, inspect its entry points and tests, narrow the delta, and record evidence on completion.
Names/declarations alone do not establish a working integration. No new implementation was
performed when this plan was added.

**Verified baseline:** the engine already has learning, memory, concepts, metacognition,
goals, crafting/construction, instruction/trust learning, wildlife, and heredity foundations.
Web search/fetch, consent, SQLite reading corpus, JSONL export, native/WASM offload, and
Vulkan llama.cpp launch already exist. In particular:
- `Server::simLoop` auto-rebirth calls `Engine::init`, which resets the clock, structures,
  and world: automatic birth exists, persistent-world succession does not.
- `GeneticMemorySystem::extractFromArchive/applyToOrganism` are stubs; heredity files and
  death-memory construction already exist in `src/mind/heredity.cpp`.
- `run_eidolon.sh` starts/reuses llama-server and starts Eidolon, but fails if game binaries
  have not already been built. Extend this script rather than add another launcher.

#### E0 — Separate maintenance/refactoring step
- [x] Map ownership and runtime wiring in `src/sim/engine.*`, `src/server/server.*`,
      `src/store/`, and `src/mind/`; identify duplication, stubs, and large responsibilities.
      Reconcile only demonstrably stale completion claims with code/test evidence.
      (Done: audit found `Engine::init` resets world+individual together; server death
      path re-`init`s and resets the world; `GeneticMemorySystem::extractFromArchive` /
      `applyToOrganism` are stubs; archive has no world/individual/generation IDs.)
- [x] Lifecycle seam (behavior-preserving): `Engine::init` is now `initWorld` (seed,
      clock, world gen, world RNG streams, structures) + `initIndividual` (body, mind,
      memory, skills, heredity load, birth episode). All callers still use `init()`;
      seeded `seed-42` logs are byte-identical to the pre-split baseline. World-preserving
      rebirth via `initIndividual()` alone remains E2 work.
- [ ] Further focused, behavior-preserving refactoring in separate steps from feature work:
      clearer world/individual ownership seams, smaller server/lifecycle responsibilities,
      explicit persistence/error contracts, and removal of verified duplication. Purpose:
      easier maintenance, root-cause bug resolution, and performance analysis/improvement.
      Avoid a wholesale rewrite; fix discovered behavior bugs in separately scoped steps.
      **Gate:** warning-enabled build, full project gate, unchanged seeded logs and
      save/load/native-WASM behavior; before/after time and RSS for touched hot paths.

#### E1 — Finish the single-command build-and-run experience
Dependency: independent of cognitive/ecological expansion; keep separate from E0 refactoring.
- [x] Extend executable `run_eidolon.sh` to configure and incrementally build native game
      targets before startup, and build required browser worker assets when offload is
      enabled. Detect toolchains/dependencies with actionable errors. Reuse existing local
      llama.cpp/model configuration and healthy-server detection; build its local Vulkan
      target when needed from available sources, or explain missing prerequisites.
      (Done: step [0/3] checks cmake/ninja/g++/sqlite headers, configures once, builds
      incrementally every launch; skips present wasm assets, builds `build-wasm-simd`
      worker via emsdk when missing; auto-builds llama-server Vulkan target behind
      `EIDOLON_BUILD_LLAMA=1` (default), explains otherwise. `EIDOLON_NO_BUILD=1` /
      `EIDOLON_WASM=0` escape hatches. Fixed EXIT-trap exit-status clobbering so
      failures propagate nonzero.)
- [x] Preserve offline mode, persistent data, configurable paths, readiness checks and
      owned-process cleanup; propagate build/startup failures with nonzero exit status.
      Update quickstart to the one command, with initial dependencies/model setup documented.
      (Done: verified from another cwd — absent build dir builds from scratch and launches
      a healthy organism; rerun reuses data without reset; offline mode works; error paths
      for missing binary/llama-server exit 1 with actionable messages.)
      **Gate:** from another working directory, one invocation handles absent/stale game
      builds, launches chat + local LLM, reuses a healthy LLM, supports offline mode, preserves
      data on rerun, and cleans up owned children on failure/Ctrl+C. No second user launcher.

#### E2 — Correct succession without resetting the world
Dependency: E0 ownership seams; decide successor arrival/body/spawn conditions first.
(Decided: adult newcomer spawns near the predecessor's structures — shelter
inheritance — falling back to the death site, then a random walkable tile.)
- [x] Split world initialization from new-individual initialization; continue ecology and
      world time without a living humanoid. Replace server reset-on-death and align CLI/WASM
      lifecycle handling with the portable core; preserve structures even where currently
      stored outside `World`.
      (Done: `Engine::respawnSuccessor()` reuses `initIndividual()` alone — world, clock,
      ecology, structures and RNG streams continue; server death path no longer calls
      `Engine::init`; CLI gains `--generations N` reusing the same schedule. WASM needs no
      change: client death hands control back and the server respawns after silence.)
- [x] Persist distinct world/individual/generation IDs, birth/death times, predecessor links,
      life statistics, and attributable history. Make death recording and successor creation
      restart-safe with versioned migrations and deterministic successor RNG derivation.
      (Done: snapshot v16 carries `worldId`/`individualId`/`generation`/`rebirthCount`;
      successor RNG derives from `worldId ^ generation`; death log lines carry
      `gen=`+cause, birth lines carry generation/id/spawn; corpse-then-successor saves
      make crash-before-save resume exactly one lineage.)
      **Gate:** death → save/reload → exactly one successor; unchanged world identity,
      monotonic time, retained ecology/buildings, archived predecessor stats, and no copied
      autobiography. Seeded multi-generation runs and client handoff retain parity.
      (Verified: 3 new unit tests — world preservation, snapshot identity round-trip incl.
      corpse restore, determinism; CLI `--generations 4` on a harsh seed ran 4 generations
      in one persistent world; surviving-run replay byte-identical to pre-E2 baseline.)

#### E3 — Complete attributed inheritance and grounded knowledge
Dependency: E2 identities; extend `heredity`, `genetic_memory`, `memory_system`, `user_model`,
`self_model`, `concept_formation`, `grounded_language`, and SQLite archive rather than replace.
- [x] Complete archive extraction/application stubs with bounded inheritance of useful
      knowledge/skill priors. Retain predecessor identity, evidence, confidence, and source
      type (personal experience, inherited record, user claim, internet resource).
      (Done, slice 1: `GeneticMemorySystem::extractFromEpisodes` builds a bounded,
      importance-ordered bundle from the predecessor's ring; `applyToOrganism` injects
      them as attributed episodes `sourceIndividualId=parentId`, never `Self`; fixed
      `extractGenome` metadata — real generation/parent id and `lifespan=death-birth`
      via new `Engine::birthTick_`; snapshot v17 persists attribution. New
      `tests/test_heredity.cpp`, 5 tests.)
- [ ] Expose predecessor stats and relationship/conversation histories through attributed
      retrieval; let new interactions revise inherited expectations independently of the
      predecessor's trust/attachment. Provide a documented adult-language/knowledge baseline
      without claiming practical mastery or seeding a textual personality.
      (Slice 2a done: conversations/messages carry the speaker's individual id
      SQLite v3 with v2 migration; the server starts a fresh attributed chat on
      succession/reset so a successor never inherits dialogue as autobiography;
      user rows stay 0 across generations; `listConversationsByIndividual`
      retrieval + API `individual_id` fields. Slice 2b done: archived episodes
      carry their source individual (SQLite v4 with v3 migration) and the
      timeline exposes it. Slice 2c done: timeline answers qualify inherited
      records as the predecessor's ("Some of these records come from my
      predecessor, not my own life"); the respond prompt carries a bounded,
      owner-labeled, world-scoped predecessor-dialogue excerpt for citation
      with a never-claim-as-self prompt rule; status/conversations APIs expose
      `individual_id`/`world_id`. Slice 2d done: the successor retains the
      predecessor's stats (parent id, generation, lifespan, cause of death)
      from the loaded genome as attributed history (snapshot v19, cleared on
      fresh-individual init so resets never leak lineage); both reply paths
      cite it (`predecessor` prompt field + `QuestionPredecessor` offline
      template that admits first-of-lineage); status API exposes it. Belief
      revision and the knowledge baseline remain open.)
- [x] Replace simplistic death-location lessons with evidence-based causal hypotheses;
      support belief contradiction/revision and distinguish descriptions from practiced skills.
      (Done, slice 1: `makeDeathMemory` is cause-specific — location counts as evidence
      only for predator attacks; starvation/dehydration/etc. explicitly rule the place
      out. Belief revision and the knowledge baseline remain open.)
      **Gate:** successor cites its predecessor accurately, revises inherited bad advice,
      retains useful knowledge across restart, and does not learn "all rivers are lethal"
      merely from a death near water. Working retrieval/model state stays bounded.
      (Gate status: attribution + retention + no-location-blame verified by unit tests and
      deterministic multi-generation CLI runs with heredity; chat citations and SQLite
      timeline exposure are the open slice 2.)

#### E4 — Deepen biological mechanisms, incrementally
Dependency: E0 baseline; E2 needed for across-generation ecosystem gates.
Entry points: `src/world/world.*`, `wildlife.*`, existing ecology/procedural modules.
- [ ] Extend existing plant regrowth/environmental fields with functional water/nutrient/light
      budgets, root uptake, life stages, seasonal reproduction/pollination, seed dispersal,
      inherited traits, injury/disease and decomposition/soil nutrient feedback. Audit existing
      L-system/reaction-diffusion support before adding mechanisms; geometry alone is not biology.
- [ ] Extend existing rabbit/wolf sensing, feeding, hunting, fleeing and taming with development,
      reproduction, inherited variation, aging, injury/immunity/disease, and learned/species-
      appropriate social/territorial behavior. Add species only after supporting mechanisms.
- [ ] Couple bounded populations, nutrient/water cycles and food-web feedback to Eidolon's
      perception, resource use, experiments and disease exposure. Use deterministic multirate
      updates with explicit budgets; avoid making the ecosystem depend on organism presence.
      **Gate per slice:** causal intervention scenario, persistence/replay, population/resource
      accounting, and before/after runtime/RSS. Combined gate: drought changes vegetation,
      prey and predator pressure; harvesting/planting changes later ecology across succession.

#### E5 — Extend existing agency into durable projects
Dependency: E3; consume E4 mechanisms as they become available.
Entry points: goal emergence, instruction learning, planner, skills, crafting/construction.
- [ ] Add missing end-to-end project persistence: prerequisites, interruption/resumption,
      outcome evaluation, skill reuse, and evidence-based explanation/refusal of advice.
      Preserve existing intent parsing, instruction execution and trust updates.
- [ ] Extend material acquisition/properties and composable crafting processes beyond the
      starter stash/catalogue, reusing existing affordance/discovery/evolved-recipe support.
      **Gate:** chat teaching → grounded plan → resource gathering → construction/experiment
      → measured outcome → remembered skill; survives interruption/restart and can produce
      a validated process not restricted to a fixed recipe lookup.

#### E6 — Turn archived research into tested knowledge
Dependency: E3 provenance and E5 execution. This implements the existing pending internet
decision-loop and rate-limit/cache items above, not a second browsing subsystem.
- [ ] Wire bounded research requests into slow cognition with user-configured internet
      consent/budgets, rate limiting, caching and source/freshness metadata.
- [ ] Convert retrieved material into attributed claims and testable hypotheses; route through
      normal planning/experiments and offline training datasets. Record external results for
      replay; deterministic replay must not depend on fresh network or LLM calls.
      **Gate:** research a practical problem, test a proposed solution, reject an unsupported
      claim, retain a reproducible skill, and continue functioning offline within budgets.

#### E7 — Evaluated self-improvement (provisional autonomy)
Dependency: E3/E5/E6 evidence and datasets; reuse `python/teacher/` training/evolution/prior
tooling and existing metacognition rather than duplicate training infrastructure.
- [ ] Add bounded candidate training triggered by measured skill deficits, versioned data/model
      lineage, held-out multi-seed evaluation, regression/resource checks, compatible promotion,
      monitoring, and technical rollback. Prove evaluation reliability before auto-promotion.
- [ ] Add isolated code/model-architecture experiments and inspectable proposals. Runtime code
      deployment initially requires user approval; permission/evaluation-rule changes require
      an explicit decision. No runtime Python or direct LLM world mutation.
      **Gate:** improve held-out capability within the shared ~6 GB budget, reject a regressing
      candidate, preserve identity/save compatibility, and recover from a bad technical upgrade
      without resurrecting an organism that legitimately died.

**Execution order:** E0 first; E1 as its own usability step; E2 → E3 → E5 → E6 → E7.
Develop E4 in small causal slices alongside later cognitive milestones, not as an unrelated
biology rewrite. Benchmark iGPU candidates against CPU, accounting for shared memory;
existing Vulkan LLM inference is baseline, not a new task. Adult newcomer embodiment/arrival
and any expansion of deployment autonomy remain explicit decisions, not agent assumptions.

### Cross-cutting rules for every phase
- No LLM in the hot path. Ever.
- No personality/biography in prompts — ever.
- Memory, context and model sizes stay bounded; every phase re-verifies memory stability.
- All stochastic behaviour seedable; important tests use fixed seeds.
- All Python tooling runs in conda env `eidolon`; C++ runtime never calls Python.
- `ReplicaCore` stays free of browser APIs and platform dependencies (DESIGN §17); native
  and WASM share one core and one state schema.
- The server is never the default compute bottleneck; the client does the maximum work it
  can support (DESIGN §17 critical invariant).
- Commit at every step (AGENTS.md SOP).
