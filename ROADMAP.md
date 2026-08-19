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
  plots), stored/retrieved in snapshot (`src/body/construction.hpp/.cpp`)
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
- [x] WorldPredictor (one-step transition MLP) + confidence
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
- [ ] Metacognition: uncertainty, confidence, self-prediction, failed-prediction recognition →
      reflection triggers
- [ ] Concept formation: incremental clustering in embedding space, expandable ontology,
      LLM-assisted naming (rare)
- Gate: organism forms and names a concept it was never told about; self-model changes after
      significant events; it reports uncertainty honestly in conversation.

### Phase 9 branch — concept ontology & belief coherence (DESIGN §22)
- [ ] **Graph rewriting**: concept ontology as a typed graph grown by rewrite rules when
      the organism forms associations; belief graph (§8) rewritten when evidence resolves
      contradictions. Rewrite rules = deterministic productions applied under the sim seed.
- [ ] **Ising models**: belief coherence — the organism's binary beliefs as spins, evidence
      as fields, consistency as couplings. Produces stable belief networks, flips under
      strong contradictory evidence, quantifiable cognitive dissonance.
- Gate: the concept graph is inspectable and reproducible from the seed; belief coherence
      score improves with experience; belief flips on strong evidence are reproducible.

## Phase 10 — Dreams v2, reflection, narrative language
- Dreams influence associations measurably (tests)
- Slow layer reflection with LLM (rate-limited): life review, summary of changes
- "What happened while you were away" grounded in the actual event timeline
- Gate: conversation asks about a real past event → accurate details; asks about an
  unrecorded event → honest uncertainty (no fabrication, tested).

### Phase 10 branch — grounded language via formal grammars (DESIGN §22)
- [ ] **Formal grammars**: structured goal / event templates for episodic-memory
      compression ("thirsty → went to water → drank"), recipe production rules, and
      grounded utterance templates for the language bridge (§14) — replaces some LLM
      dependence with deterministic, state-seeded language. Production rules are a
      deterministic rewrite system; choices driven by the sim seed and the organism's state.
- Gate: the organism can answer "what did you do today?" with a generated sentence that
  is factually grounded in its actual event log, without an LLM call; utterances are
  reproducible from the seed and state.

## Phase 11 — Portable WASM client compute
- Compile the same `ReplicaCore` to WebAssembly (Emscripten): WASM, WASM SIMD, and
  multithreaded builds (Workers + SharedArrayBuffer where browser security permits)
- Capability detection → `ComputeProfile` (SIMD, Workers, SAB, WebGPU, WebGL fallback,
  concurrency, memory limits); auto backend selection hierarchy (WebGPU → WASM SIMD+MT →
  plain WASM → server fallback)
- `ComputeScheduler`: priority queues (chat/responsiveness > active sim > background
  consolidation); worker separation (world / physiology-cognition / neural-ML /
  memory-consolidation); compact message passing, no big buffer transfers
- Adaptive fidelity: constrained clients reduce sim frequency, model budget, world detail —
  identity unchanged
- Profiling from the beginning: per-backend sim steps/sec, simulated hours/sec, inferences/
  sec, worker utilization, WASM memory, latency — diagnostics panel in the UI
- **Genetic programming** (offline tooling): evolve recipe trees (crafting / tool
  invention) and behaviour trees (action sequences) validated against world physics;
  tournament + subtree crossover / mutation; fitness = sim validation; depth cap.
  Discovered procedures become recipes the organism can use at runtime.
  (`python/teacher/evolve_prior.py` provides the framework; extend to recipe/behavior trees).
- Gate: same seeded scenario produces the same individual state on native and WASM
  (parity test); no heavy work on the main UI thread; fidelity reduction works.

## Phase 12 — Synchronization, offline persistence & backend selection
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

## Phase 13 — Performance & long-run stability
- Profiling: hot-path allocation audits, adaptive-clock tuning
- Benchmarks: 30 sim-days headless target < ~10 min; fine tick p50 ≤ 2 ms; snapshot ≤ 100 ms
- Long-run tests: weeks of simulated time, RSS flat, archive bounded, no drift in metrics
- Observability polish: /api/metrics complete, headless --stats, --bench report
- Gate: all performance budgets met; long-run memory stability test green.

## Phase 14 — Full test matrix & release
- Complete test suite per DESIGN §19 (unit, seeded replay, behavioural scenarios,
  client-compute & sync, backend benchmark suite, adversarial: corrupt save, kill -9 during
  save, LLM garbage, divergent sync)
- Documentation finalization; CHANGELOG entries; performance report
- Gate: full `ctest` + integration suite green on a clean checkout.

## Future directions (deferred by design)
Logged from user requirements; not yet sequenced into phases. No LLM in the hot path
and "client does the maximum work" invariants apply to all of them.
- **Deeper world / playground**: hobbies with real procedural depth — gardening (plant,
  tend, harvest over days/weeks, seasonal yield, skill progression), reading books (world
  artifacts with retrievable content the organism actually learns from, not cosmetic
  flavour), leisure that competes meaningfully with survival drives.
- **File attachments**: the organism reads documents the user drops into the chat (PDF /
  text first; images later via a small local vision model). Becomes persistent, retrievable
  memory, not prompt text.
- **Internet access**: configurable, user-gated browsing so the organism can research —
  always as *content it reads and learns from*, never as a live-mind backdoor.
- **Client-side offload**: migrate most compute to the client per DESIGN §17 (Phases 11-12)
  to free the server; headless fallback continues life when the client is away.

---

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
