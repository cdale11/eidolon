# Changelog

All notable user-visible changes to Eidolon, grouped by phase. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Phase 15 — Client-side offload foundation (started)
- **Server hands the sim to a capable client**: when a browser posts a
  `ComputeProfile` whose selected backend is WasmPlain/WasmSimd/WasmSimdMt,
  the server's local tick loop idles and the organism's life continues via
  client snapshots (client-authoritative compute; server persists, syncs and
  answers chat — DESIGN §17).
- **New endpoints**: `GET /api/wasm/core.wasm` + `GET /api/wasm/core.js`
  (serve the matching prebuilt WASM module for the selected backend;
  404 until a profile is posted), `POST /api/client/snapshot` (validates,
  restores and persists a client-produced tick state; rejected while the
  server is hosting the sim itself).
- **`run_eidolon.sh`**: one-command launcher — starts llama-server (Vulkan
  iGPU backend, Qwen3-4B, waits for `/health`, reuses an already-healthy
  instance without touching it, kills only its own on exit) then
  `eidolon-server` on `0.0.0.0:8081`. `EIDOLON_NO_LLM=1` starts offline mode.
  Ctrl+C stops both cleanly.
- README quickstart flags corrected (`--llm`/`--llm-timeout`, port 8081).
- **Browser take-over (client-side compute)**: capable browsers run the simulation
  themselves in a Web Worker — the page ships a WASM ReplicaCore build
  (`/api/wasm/worker.js` + `core.{js,wasm}`), restores the current organism snapshot,
  ticks at the fidelity pacing with adaptive slice budgets, and posts a snapshot back
  every second. Bit-exact vs the native engine (same digest from
  `eidolon-parity-dump`). Sidebar "Compute: server/THIS TAB" toggle with capability
  detection; closing or reloading the tab hands the sim back within ~15s so the
  organism never stalls. Server-side uploads are validated, weak profiles never arm
  offload, and `"offload":false` disarms immediately.
- **Bugfix (found while validating)**: the learned policy had 12 outputs but only 6
  were mapped to real actions — Farm/Cook/Craft/Build/CollectWater/Preserve silently
  degenerated into Observe (and trained Observe's weights). The mapping is now a
  complete bijection with a roundtrip regression test; the experience dump's action
  name table grew to all 12 (it previously segfaulted on the newly reachable actions).

### Fix-as-you-go — life-stats grounding for chat
- `CognitiveSnapshot::lifeStatsSummary`: deterministic, non-persisted string
  derived from `Engine::stats()` + `rebirthCount()` (age in days, per-action
  totals, attack/wound/infection/build/craft/eat/drink milestones). The
  respond prompt now references it so the LLM can honestly answer "how old
  are you?" / "have you been attacked?" without inventing facts; the
  offline fallback reply appends the organism's age.

### Fix-as-you-go — `Engine::lastAction()` persisted for chat grounding
- **`Engine::lastAction()` accessor** (`src/sim/engine.hpp`): returns the action chosen
  by `decide()` on the most recent tick. The LLM bridge now reads it via
  `engine.lastAction()` to populate `CognitiveSnapshot::currentAction` — the previous
  hardcoded `"active"` placeholder is gone.
- **Persisted in the snapshot (v10)** (`src/sim/engine.cpp::serializeState`,
  `src/core/serialize.hpp`): `lastAction_` is written after the explore-state block and
  read back with a bounds check (rejects `>12` so a corrupt snapshot returns a clear
  error per DESIGN §15). A resumed run now continues with the correct chat-grounding
  action instead of always showing `"active"`.
- **`init` explicit default**: `lastAction_ = Action::Observe` is set on every fresh
  init so pre-first-tick snapshots are well-defined.
- **Test coverage** (4 new tests, +1 invariant re-verified):
  - `engine_lastAction_default_then_updates` (test_engine.cpp) — pre-tick default is
    Observe, after ticks lastAction matches the returned Action from `tick()`.
  - `engine_lastAction_survives_snapshot_roundtrip` (test_engine.cpp) — central
    invariant: snapshot A → restore into B → both continue with identical lastAction.
  - `snapshot_current_action_is_meaningful` (test_bridge.cpp) — `currentAction` is no
    longer `"active"` placeholder; matches engine's lastAction via the documented
    names (`"wander"`, `"forage"`, `"drink"`, etc.).
  - `snapshot_current_action_survives_resume` (test_bridge.cpp) — resumed run's
    `currentAction` matches the original run's at snapshot time.
  - `test_saveload_continuity` (Python integration) — unchanged, re-verified: byte-
    identical resume == uninterrupted run still holds with v10 (no determinism
    regression from the snapshot bump).
- **Result**: 118/118 C++ unit tests pass (was 114), full `run_integration.sh` green.
- **Bonus**: the previously-unused `actionName()` helper in `bridge.cpp` (which had
  caused a `-Wunused-function` warning for several commits) is now used; the build is
  fully warning-clean.

### Teacher pipeline — .eprp schema versioning + stale test fixes
- **Versioned the `.eprp` schema** (`python/teacher/fit_prior.py`,
  `src/mind/policy.cpp`): `PRIOR_VERSION = 2` is now the canonical constant; writer
  defaults to v2, reader refuses anything ≠ v2, C++ loader accepts `{1, 2}` and
  verifies `(nFeatures, nActions)` matches the live policy tuple. The header layout
  is byte-identical to v1; the version bump only exists so the loader can refuse
  priors baked against a stale feature/action layout.
- **Centralised magic numbers** in `fit_prior.py`: `PRIOR_MAGIC`, `PRIOR_VERSION`,
  `PRIOR_N_FEATURES`, `PRIOR_N_ACTIONS`, and `expected_prior_bytes(nf, na)` (4 +
  12 + nA*(nF+1)*4). Test assertions now read these constants instead of hardcoded
  `43` / `6 * 44 * 4`, so the next feature-vector bump fails loudly in CI rather
  than silently drifting.
- **Stale integration tests fixed**: `test_dump_and_dataset` and
  `test_prior_fit_and_roundtrip` previously hardcoded `(43,)` features and
  `4 + 12 + 6 * 44 * 4` header size (stale since the Phase 5 wildlife + Phase 7
  planning feature-vector bump to 45 dims and action-set expansion to 12). Now use
  `N_FEATURES` + `expected_prior_bytes()`.
- **New tests**: `test_prior_version_marker_present` (Python, asserts the raw bytes
  carry the current magic + version) and `policy_prior_rejects_bad_version` (C++,
  asserts the loader refuses future version, wrong magic, and pre-Phase-5 tuple).
- **Result**: all 8 Python teacher tests pass, all 114 C++ unit tests pass, full
  `run_integration.sh` green (was 2 pre-existing failures — see MISTAKES 2026-08-22).
- **Rule for future bumps**: when the C++ feature vector or action set grows, bump
  `PRIOR_VERSION` AND extend the C++ loader's accepted-version list AND regenerate
  every prior on disk. The single source of truth (`expected_prior_bytes()`) makes
  the test side fail first, not the loader side — a load failure mid-run would
  silently fall back to random init (MISTAKES 2026-08-19).

### Future Directions (partial) — Time-of-day awareness in chat
- **Circadian state in `CognitiveSnapshot`** (`src/llm/bridge.hpp/.cpp`): six new fields
  derived deterministically from existing snapshot state — no new persistent state, no
  extra LLM calls, no hot-path cost:
  - `phaseOfDay`: "deep_night" / "dawn" / "day" / "dusk" / "night" / "asleep"
  - `timeOfDayPhrase`: "deep night" / "just before dawn" / "early morning" /
    "mid-morning" / "midday" / "afternoon" / "late afternoon" / "evening" / "night"
  - `seasonName`: "spring" / "summer" / "autumn" / "winter" (from `Weather::season()`)
  - `physiologicalState`: "rested" / "drowsy" / "tired" / "exhausted" / "asleep" /
    "pained" / "sick" / "fine" (derived from sleep pressure / fatigue / pain / sickness)
  - `primaryNeed`: "thirsty" / "hungry" / "tired" / "fine" (most pressing drive)
  - `circadianTone`: one-word tone hint ("groggy" / "calm" / "alert" / "tense" /
    "agitated" / "peaceful" / "weary" / "terrified" / "restless" / "drowsy")
- **`respond` system prompt** now instructs the LLM to set its tone from the circadian /
  physiological / drive fields ("Examples: an asleep organism cannot answer (one short
  sleep line); an exhausted organism at 3am is groggy; a thirsty organism mentions
  thirst first; a well-rested organism at midday is calm and clear"). The full state
  string sent to the LLM now includes a `circadian=[phase=… phrase=… season=…]` block
  and a `physiological=[state=… primaryNeed=… tone=…]` block.
- **`fallbackReply` (no LLM path)** rewritten so every reply carries the time of day:
  asleep replies name the time + season; threat / thirst / hunger / tired / sick /
  pained replies all prefix their message with the time of day; healthy replies open
  with "Good morning/afternoon/evening/night" so the user gets a circadian-grounded
  answer even when the LLM is down. Verified live: 3am ping → "I feel fine and peaceful
  in the deep night"; midday → "Hello, I'm awake at midday in spring"; tired at night →
  "I'm drowsy in the deep night, barely awake, and my thirst is high".
- **Determinism preserved**: all six fields are pure functions of the existing snapshot
  state; no RNG, no wall-clock input, no extra LLM call. The snapshot itself stays a
  fixed-layout struct — extending it is backward-compatible for any external consumer.
- **8 new C++ unit tests** in `tests/test_bridge.cpp` covering: snapshot field
  population, phase-of-day mapping at midday / deep-night, physiological state on
  fresh body, fallback includes time-of-day, asleep fallback mentions time + sleep
  state, thirsty fallback mentions drive + time, deterministic fallback (same snapshot
  → same reply regardless of user text).
- **Integration test updated**: `python/tests/test_server.py::test_offline_llm_fallback`
  now accepts the new "Good morning/afternoon/evening/night" greeting slot + circadian
  phrases in the fallback reply.

### Bugfix — survival decision logic (sleep + waterskin + exploration)
- A sleeping organism with rising thirst stayed asleep until it died: the wake-from-sleep
  threshold (`thirst > 85`) matched the sleep-entry block (`thirst < 85`), so at thirst=79
  the organism neither woke nor was blocked from re-entering sleep — a death loop. Wake
  threshold lowered to 55; sleep-entry block tightened to `thirst < 55 && hunger < 75`.
- Emergency Drink safety valve lowered from `thirst > 80` to `thirst > 55`, with a
  `hunger < 80` guard so a thirsty-and-starving organism forages rather than drinking water
  it doesn't need.
- `Execute(Drink)` fix: when `adjacentToWater(p)` (terrain check) was true but
  `drinkFromSource` returned 0 (the matching `waterSources_` entry depleted or >16 tiles
  away), the action did nothing and never fell through to `drinkFromSkin`. A stranded
  organism could call Drink 33k+ times with a full waterskin and die of thirst. Now a
  source-dry adjacent Drink falls through to the emergency waterskin reserve, and only
  seeks water (gradient/wander) once the skin is empty.
- **Directed exploration**: 1-tile random walks in Wander/Forage/Drink/Flee caused organisms
  to bounce in corners until starvation/dehydration. Added `exploreStep()` — a sustained
  16-tick directional walk that rotates on obstacles, replacing ineffective random jitter.
  Used whenever no resource is in perception range. v2 prior 2-day survival improves from
  3/5 to 6/7 seeds; baseline 3-day survival 72/100 seeds.

### Bugfix — survival valves & threat veto ordering
- **Pain valve forced Rest even when predator in sight**: The pain valve (`pain > 40 → Rest`)
  ran before the threat veto and didn't check for predator presence. Trained organisms with
  high pain (>40) and high threat (>0.65) rested instead of fleeing, maintaining lower
  average distance from predators than naive organisms. Fixed: pain valve now checks
  `!nearestPredator(sightRadius)` before forcing Rest, allowing threat veto to force Flee.
- **Wildlife accumulator unbounded growth**: `Wildlife::update` used `if (accum_ >= kInterval)`
  instead of `while (accum_ >= kInterval)`. During sleep (`dt=30`), `accum_` grew by +25/tick
  (net). During training (`dt=1`), huge `accum_` caused wildlife steps every tick (5× normal
  rate), making wolf hunger increase 5× faster. Wolves starved in ~75 ticks instead of
  surviving 120+ ticks. Fixed: `while (accum_ >= kInterval) { accum_ -= kInterval; step(); }`
- **Spatial hash stale after test teleports**: `parkHungryWolf`/`placeWolfAtDist` moved wolves
  but didn't rebuild spatial hash. Organism's `nearestPredator` used stale hash for 4 ticks,
  failed to detect wolf at distance 6. Fixed: added `Wildlife::rebuildHashForDebug()` and
  call it after test teleports.
- **Test wolf hunger too high**: `placeWolfAtDist` set hunger=90 at distance 6. Wolf took 3
  wildlife steps to reach organism; hunger reached 100 and starved at tick 75 (of 120).
  Changed initial hunger to 55 (attack threshold) so wolf attacks immediately, hunger drops
  to ~10, survives >750 ticks. Both wolves now survive full 120-tick comparison.

### LLM Cognitive Snapshot — Comprehensive NLP Center
- **Overhauled `CognitiveSnapshot`** (src/llm/bridge.hpp/.cpp) from ~15 fields to 40+ fields
  covering identity, physiology, position, threats, resources, inventory, cognition,
  personality, drives, social relationships (user + wildlife), skills, and recent memories.
- **LLM is now the true NLP center**: the user's only window into the organism's world,
  with full access to comprehensive state. LLM responses grounded in actual organism state.
- **Exposed internal systems to LLM**: GoalEmergence (active goals), UserModel (trust,
  familiarity, affection), WildlifeSocialSystem (per-agent profiles), PersonalityLatent,
  DriveWeights, ThreatNet estimate.
- **Added public accessors** on Engine: `goalEmergence()`, `userModel()`, `wildlifeSocial()`
- **Updated parse/respond prompts** with comprehensive state string (~2-4k tokens)
- **Fixed fallbackReply** to include position, water, threats, predator distance
- **All 108 tests pass**; server responds with complete organism state

### Phase 5 — Rich world & wildlife (world generation + ecology)
- Noise-field world generation (simplex fbm): elevation, temperature, humidity per tile;
  biomes (temperate plains/forest, boreal, tundra, desert, savannah, jungle, mountain,
  swamp, water bodies, rivers) derived from the climate fields.
- Rivers carved by gradient descent into low-elevation drainage paths; terrain smoothing
  by majority vote; guaranteed water bodies.
- Plants with types (edible/toxic/medicinal/wood), amount, regrowth, toxicity and
  medicinal value; biome-weighted type distribution; a guaranteed starter edible plant
  near spawn so the organism always has a first meal.
- Water sources (river/lake/spring) placed on walkable shore tiles (never on water), each
  with capacity, current level and flow rate; drinking consumes the source, which refills
  over time.
- Seasons (spring/summer/autumn/winter) drive weather probability and ambient
  temperature; weather now tracks humidity and wind speed; snow/rain/storm gating
  reshaped by season.
- Perception extended to 20 features: season, terrain, nearest plant (distance/direction/
  fullness), nearest water source, plant density, nearest toxic plant (distance/direction),
  medicinal distance, elevation, humidity.
- Engine drink/forage integrated with sources/plants (nearest-source adjacency drinking);
  experience dump records nearest plant/water distances.
- Grid snapshot now round-trips all climate arrays (tiles, biomes, elevation, temperature,
  humidity) — fixes a post-restore out-of-bounds crash; snapshot version bumped to 4.
- Snapshot now persists the run's scheduled target so a resumed run continues the ORIGINAL
  day schedule — `--days 0.5` + `--days 0.5` is byte-identical to `--days 1.0` even when
  coarse ticks overshoot the boundary (fixes `test_saveload_continuity`).
- **Wildlife** (`src/world/wildlife.hpp`): seeded, deterministic ecosystem agents — prey
  (rabbit/deer) and predators (wolf/bear) that move on terrain, drink, flee, and hunt; the
  organism senses them (distance/direction/type in perception) and can **Flee** when a
  predator attacks. Predator attacks deal damage; prey flee predators. New perception
  channels raise the feature vector to 28 and the learned policy to 6 actions (incl.
  Flee); teacher pipeline updated to the 6×44 prior layout.
- Grid snapshot fidelity fix: `Grid::serialize`/`deserialize` passed the ELEMENT count to
  `bytes()` (a BYTE-count API), so only the first 1024 floats of each 4096-float climate
  array (elevation/temperature/humidity) were persisted and the tail read back as zeros
  after a resume — a resumed organism perceived flat terrain and diverged from the
  uninterrupted run within a tick. Full arrays now round-trip; snapshot version bumped to
  6. `test_saveload_continuity` (CLI resume == uninterrupted run, byte-identical) passes.
- **Phase 5 hazards & gate (current session)**: `Physiology` extended with wounds (severity/age/infection/source, max 8), infection dynamics (`exposure_`, `immunity_`, `updateExposure()`), and immune response (`sick()` drains health/energy with fever); `Grid` gains `kCliffStep`, `cliffBetween()`, `deepWater()` (-0.52), `setElevation()`; `Engine` gains `stepTo(q, allowFall=false)`, `hazardDose()`, fall/wound damage, `resetBody()`, stats (`fallsTaken`/`woundsSustained`/`infections`); wildlife `stepToward` made cliff-aware (`stepWalkable`); `attackOrganism` fixes mid-charge attack (wolf no longer overshoots and misses adjacent prey); snapshot version 7; `tests/test_phase5.cpp` (7 tests, all passing); `phase5_gate_threat_learning` passes (predator bites → ThreatNet sensitization); `phase5_gate_survival_improves_with_experience` redesigned to measure defensive-behavior distance (trained organism maintains ~2× average distance from predator via threat veto vs emergency-only flee) rather than an unsupported hit-reduction assertion; worldgen `freqScale` kept at 0.01 (reverted from broken 0.04/0.03/0.02 that caused a survival-death spiral through fall-pain rest-spiral). See `MISTAKES.md` for the frequency-scale and test-design lessons.

### Phase 6 — Skills, tools, crafting, construction
- **Skill models** (`src/body/skill.hpp/.cpp`): Beta/Bernoulli competence per skill type (16 skills), skill checks, habit formation
- **Crafting system** (`src/body/crafting.hpp/.cpp`): Recipe-based crafting with ingredients, tools, skill requirements, experimentation/discovery, MaterialInventory, snapshot serialization
- **Formal grammars** (`src/world/grammar.hpp/.cpp`): CFG engine with deterministic/stochastic derivation, predefined grammars (goals, events, recipes, utterances)
- **Construction system** (`src/body/construction.hpp/.cpp`): StructureManager with 7 blueprints (campfire, lean-to, wall, storage, farm plot, shelter), placement/progress/repair/decay, snapshot serialization
- **Affordance discovery** (`src/body/affordance.hpp/.cpp`): Tool/material affordance registration, unexpected usage detection, procedure generation hooks
- All tests pass; snapshot version 8

### Phase 7 — Planning & world model
- **WorldPredictor** (`src/mind/world_predictor.hpp/.cpp`): Linear one-step transition model (43 features × 50 inputs → 43 outputs), predicts next features given current features + action, outputs confidence. Online training with SGD.
- **Planner** (`src/mind/world_predictor.hpp/.cpp`): Greedy and beam search (configurable width/horizon) over action primitives using WorldPredictor. Replan-on-surprise (prediction error > threshold).
- **GoalEmergence** (`src/mind/goal_emergence.hpp/.cpp`): Drive-based goal generation from physiology state + environmental opportunities. 8 goal types (Survive, FindFood, FindWater, Rest, FleeThreat, Explore, BuildShelter, CraftTool). Priority computed from drives + opportunity proximity. Snapshot serialization.
- **LLM Planner** (`src/mind/llm_planner.hpp/.cpp`): LLM-assisted high-level plan proposals. `LLMPlanner` with prompt building, response parsing, validation hooks. `LLMPlanProposal` with steps, confidence, validation hooks. Ready for LLM integration (callback-based, no hot-path LLM calls).
- Tests: 6 planning tests + 1 goal emergence test.
- All tests pass; snapshot version 9

### Phase 8 — Social cognition & learning from the user
- **User model** (`src/mind/user_model.hpp/.cpp`): Familiarity, trust, affection, fear, respect, resentment, reciprocity, expectations. Interaction history, verifiable fact learning, attachment pressure from absence.
- **Wildlife social** (`src/mind/wildlife_social.hpp/.cpp`): Per-agent social profiles (familiarity, fear, friendliness, threat). Per-species and per-individual tracking. Decay over time.
- **Attachment** (`src/mind/attachment.hpp/.cpp`): Secure/anxious/avoidant/disorganized styles. Separation distress, reunion response, proximity seeking. Deterministic initialization from seed.
- **Belief Ising model** (`src/mind/belief_ising.hpp/.cpp`): Binary beliefs as spins. Evidence = external fields, consistency = couplings. Glauber dynamics with seeded RNG. Coherent clusters, dissonance metric, snapshot serialization.
- **Affordance discovery** (`src/body/affordance.hpp/.cpp`): Tool/material affordance registration, unexpected usage detection, procedure generation hooks.
- All tests pass; snapshot version 10

### Phase 9 — Self-model, concepts, metacognition (current session)
- **Self-model** (`src/mind/self_model.hpp/.cpp`): Capability assessment, autobiographical summary, preferences, reputation, future expectations. Metacognitive state (uncertainty, prediction confidence, reflection triggers). Experience-based updates from physiology and goals.
- **Metacognition** (`src/mind/metacognition.hpp/.cpp`): Prediction tracking, error detection, surprise detection, reflection triggers, confidence/uncertainty dynamics. Prediction history with MSE tracking, reflection triggers on surprise, confidence/uncertainty dynamics.
- **Concept formation** (`src/mind/concept_formation.hpp/.cpp`): Incremental K-means clustering in embedding space, experience buffering, concept merging, LLM-assisted naming hooks, concept activation lookup.
- **Graph rewriting** (`src/mind/graph_rewriting.hpp/.cpp`): Typed graph for concept ontology (Concept/Relation/Property/Event/Category nodes; IsA/HasProperty/Causes/PartOf/RelatedTo/Opposes/Enables edges), `RewriteRule` with pattern matching and replacement, incremental `match_pattern` / `apply_rules`, `sync_with_concepts` hook, full snapshot serialization.
- **Ising belief coherence** (`src/mind/belief_ising.hpp/.cpp`): Binary beliefs as spins (+1/-1/0), evidence as external fields, consistency as couplings. Glauber dynamics with seeded RNG, coherent clusters, energy computation, cognitive dissonance metric. Full serialization.
- All tests pass; snapshot version 12

### Phase 10 — Dreams v2, reflection, narrative language (in progress)
- **Dreams v2** (`src/mind/memory_system.cpp`): Enhanced associative recombination during sleep. Episodes sharing location, participants, outcome, action, or kind are paired; dream traces strengthen source episodes and perturb policy toward recombined action sequences. Deterministic via seeded RNG.
- **Reflection system** (`src/mind/reflection.hpp/.cpp`): Slow-layer reflection with rate-limited LLM calls. `ReflectionSystem` provides:
  - `reflect_on_recent_events()`: recent experience reflection with narrative summary, key events, insights, changes
  - `summarize_absence()`: "what happened while you were away" grounded in actual event timeline
  - `generate_life_review()`: periodic deep life review with narrative arc, major events, drive/social patterns, lessons, self-assessment
  - `answer_about_past()`: question answering with honest uncertainty for unrecorded events
- **Grounded language** (`src/mind/grounded_language.hpp/.cpp`): Formal grammar-based utterance generation grounded in actual event log. `GroundedLanguage` provides:
  - `answer_what_did_you_do()`: generates daily activity summary from event log + memories
  - `generate_daily_summary()`: structured daily summary with key events and drive patterns
  - `answer_about_past()`: question answering with honest uncertainty for unrecorded topics
  - `generate_greeting()`: state-aware greetings
  - `generate_need_statement()`: need expressions based on recent behavior
  - `generate_observation()`: world observations
  - All utterances deterministically generated from seed + state; no LLM required
- All tests pass; snapshot version 12

### Phase 12 — Synchronization, offline persistence & backend selection
- `eidolon-sim --bench [--bench-ticks N] [--bench-json]`: hot-path benchmark + backend
  selection. Reports tick latency (p50/p95/max/mean), sim/wall throughput, snapshot size
  & save cost, RSS, learner inference/update counts, ticks by step class; derives a host
  ComputeProfile from the measured throughput and emits the auto-selected backend
  (ServerFallback / WasmPlain / WasmSimd / WasmSimdMt / WebGPU). Budget: fine tick p50
  ≤ 2 ms (measured ~0.06 ms on the Ryzen 8300GE).
- Backend-selection unit tests: capability→backend mapping and fidelity auto-leveling
  for weak/SIMD/SIMD-MT/WebGPU profiles; ComputeProfile serialization round-trip.
- **Native ↔ WASM bit-exact parity achieved**: same seeded scenario produces identical
  individual state digests and snapshots across libstdc++ (native) and libc++ (WASM).
  Root causes fixed: (1) WASM archive `CMAKE_RANLIB=emranlib` (llvm-ar 24); (2) `detmath`
  module with deterministic sin/cos/tanh/exp/log1p (bit-identical, max rel err ~3e-7);
  (3) `-ffp-contract=off` on both backends; (4) `Attention::attend` sort tie-break
  (libstdc++ and libc++ permute equal keys differently). Verified across seeds 1,7,13,42,99,2024.
- **Binary snapshot download/upload endpoints** (`GET /api/snapshot/download`, `POST /api/snapshot/upload`):
  client-authoritative persistence path; raw blob transfer for offline-first clients.
- **Checkpoint/delta sync protocol** (`POST /api/checkpoint/create`, `GET /api/checkpoint/delta?base=<id>`, `POST /api/checkpoint/apply`):
  compact binary deltas (patch list: offset + length + new_data) from a base checkpoint;
  no tick streaming; batched + compressed via patch encoding.
- **Server accepts client ComputeProfile** (`POST /api/compute-profile`): client reports
  capabilities (SIMD, Workers, SAB, WebGPU, WebGL, concurrency, memory); server auto-selects
  fastest stable backend (WebGPU → WASM SIMD+MT → plain WASM → server fallback) and maps
  to adaptive fidelity settings (Low/Medium/High) affecting pacing/model budget/world detail
  only — never tick semantics.

### Future Directions (partial) — Internet access for the organism
- **Configurable, user-gated browsing**: `--internet-enabled` flag, optional search API
  endpoint/key, per-request timeouts, result limits.
- **Search & fetch endpoints**: `POST /api/browse/search` (query → ranked results with
  snippets), `POST /api/browse/fetch` (URL → extracted text content).
- **Safety**: results flow through normal memory/learning pipeline as "read content" —
  never injected as prompt text or live-mind backdoor.
- **Graceful degradation**: search/fetch errors return structured JSON; CAPTCHA/blocking
  detected and reported; HTTPS handled via OpenSSL-linked httplib.
- **Current limitation**: DuckDuckGo HTML scraping is CAPTCHA-blocked on html.duckduckgo.com.
  For production use, configure `--search-endpoint` with a proper search API (SerpAPI, Brave, etc.)
  and implement the corresponding parser in `src/llm/web_browser.cpp`.
- **Implemented**: SearchProvider abstraction with 6 engines (SearXNG default, DuckDuckGo,
  SerpAPI, Brave, Google, Custom), multi-instance SearXNG with CAPTCHA detection,
  Brave/SerpAPI/Google/Custom providers with API key support, fetch with redirect following
  and HTML text extraction, server config flags, endpoints for search/fetch/compute-profile.

### Future Directions (partial) — Learning from the user's actual speech (text)
- **Intent parsing + instruction validation implemented** (`src/llm/intent_parser.hpp/cpp`):
  - 18 intent types mapped to organism actions (GoTo, FollowMe, Explore, Forage, Drink, Rest, Sleep, Flee, Avoid, Build, Craft, Observe, Status, Greet, Thank, Stop, Wait, Cancel)
  - Keyword-based parsing with confidence scoring (position + keyword length)
  - Target/parameter extraction from text
  - ValidationContext: checks organism state (energy, hunger, thirst, health, sleep state, nearby threats/resources)
  - Validation rules: energy thresholds, hunger/thirst requirements, sleep state, predator awareness
  - Returns ParsedInstruction with intent, confidence, target, params, valid flag, error message
- **Trust modulation + repetition learning implemented** (`src/llm/instruction_learning.hpp/cpp`):
  - InstructionLearningSystem: full pipeline from text → validation → trust modulation → habit tracking
  - Trust modulation: successful instructions → trust+ (0.015), harmful → trust- (0.04), ignored → trust- (0.01)
  - Repetition learning: InstructionMemory tracks instruction history, builds habit_strength (logarithmic, capped at 1.0)
  - InstructionTrustModulator: static trust weights for success/failure/ignored/harmful outcomes
  - InstructionMemory: tracks instruction history (first/last tick, count, avg_confidence, trust_at_first, success/fail counts, habit_strength)
  - InstructionLearningSystem: full pipeline process_instruction → validate → record_execution → trust/habit updates
  - Integration with UserModel: trust, familiarity, reciprocity updated via record_interaction
  - Ready for integration with GoalEmergence, Policy, UserModel

### Future Directions (partial) — Learning from the user's actual speech
- **Speech-to-text + intent parsing**: User spoken/typed instructions ("eat", "sleep",
  "go to the river", "avoid wolves") become validated, structured goals the organism
  pursues through its normal planning loop (never injected as prompt text, never
  mutating world state directly).
- **Instruction following improves with repetition**; instructions that prove harmful
  lower the organism's trust in the user's advice (ties into Phase 8 user model).
- **Requires**: speech-to-text / intent parsing in the language bridge, then grounding
  into the existing goal system.

### Phase 11 — Portable WASM client compute (in progress)
- **WASM compilation** (`cmake/Modules/Platform/Emscripten.cmake`, `src/CMakeLists.wasm.txt`): Eidolon ReplicaCore compiles to WebAssembly via Emscripten
  - Emscripten toolchain configured and working
  - Static library `libeidolon_replica_core.a` builds successfully (~1.1 MB)
  - Test executable `eidolon_wasm_test.js` links and runs in Node.js
  - Native builds and all tests remain unaffected
  - Platform-specific code (`getpid()`, SQLite, pthreads) cleanly excluded via `EIDOLON_WASM_BUILD` / `EIDOLON_NO_SQLITE` / `EIDOLON_NO_THREADS` defines
- **WASM SIMD compilation** (`src/CMakeLists.wasm-simd.txt`): SIMD128 support for WebAssembly
  - `-msimd128` flag enabled
  - Static library `libeidolon_replica_core_simd.a` builds successfully (~1.1 MB)
  - Test executable `eidolon_wasm_test_simd.js` links and runs in Node.js
  - SIMD-optimized math operations available for neural networks, physics, etc.
- **WASM Multithreaded compilation** (`src/CMakeLists.wasm-mt.txt`): pthreads/SharedArrayBuffer support
  - `-pthread` + `-sPROXY_TO_PTHREAD=0` + `-sPTHREAD_POOL_SIZE=4` enabled
  - Static library `libeidolon_replica_core_mt.a` builds successfully (~1.1 MB)
  - Test executable `eidolon_wasm_test_mt.js` links and runs in Node.js
  - Note: ALLOW_MEMORY_GROWTH + pthreads may impact non-WASM performance
- **Capability detection & backend selection** (`src/mind/compute_profile.hpp/.cpp`): `ComputeProfile` with auto backend selection
  - `ComputeProfileDetector::fromJsCapabilities()`: creates profile from JS-provided capabilities (SIMD, SAB, WebGPU, WebGL, concurrency, memory)
  - `selectBackend()`: auto-selects best backend (WebGPU → WASM SIMD+MT → WASM SIMD → WASM plain → server fallback)
  - `getBackendPriority()`, `isBackendViable()`, `estimatePerformance()`: priority ordering, viability checks, performance estimation
  - Full serialization support for `ComputeProfile` and `BackendSelection`
- **Heredity system** (`src/mind/heredity.hpp/.cpp`): Inheritance mechanism for organism death/restart
  - `HeredityGenome`: saves policy weights + personality latent vector (16-d)
  - `HeredityManager`: `extractGenome()` on death, `createOffspring()` with mutation, `applyHeredity()` to fresh engine
  - `HeredityGenome` serialization for persistence across restarts
  - Policy `weights()` accessor added; `PersonalityLatent` operator[] for mutation/blending
  - Engine non-const `learn()` for heredity application
- **ComputeScheduler** (`src/mind/compute_scheduler.hpp/.cpp`): coordinates deferrable background
  work (consolidation, reflection, planning, LLM proposals) around the single-threaded
  deterministic tick without reordering it
  - Priority classes Responsive > Normal > Background > Idle, FIFO within priority, fixed-capacity queue
  - Worker separation (world / physiology-cognition / neural-ML / memory-consolidation)
  - Compact message ring (no big buffer transfers); per-domain wall-time profiling
  - Wired into `Engine::tick` (profiling only, never gates tick output) and the Diagnostics panel
- **Adaptive fidelity** (`src/mind/compute_profile.hpp/.cpp`): `FidelityLevel` Low/Medium/High +
  `FidelitySettings` + `FidelityController` map a client compute profile to sim pacing, model
  budget, and world detail — identity unchanged, tick semantics untouched (`--fidelity 0|1|2|3`)
- **Diagnostics panel** (`/api/metrics` + sidebar toggle): live scheduler queue/message depth,
  per-domain profiling, action counts, learner inference/update counts, fidelity settings
- **Genetic programming** (`python/teacher/gp_evolve.py`): offline GP for recipe trees
  (crafting/tool invention) and behavior trees — deterministic RNG, tournament selection,
  subtree crossover/mutation, depth cap, validated against world physics; emits flat
  artifacts (`CraftingSystem::loadEvolvedRecipes`) the organism can use at runtime.
  Fixes a pre-existing duplicate-skill variable and double recipe-id bump in crafting.cpp.
- **Chat latency fix (Nemotron 3 Nano)**: disable the model's `[THINK]` reasoning phase
  (`chat_template_kwargs.enable_thinking=false`) for classify/respond — reasoning rambled
  for hundreds of tokens (~90s on the iGPU) and blew the LLM timeout, so the organism
  always fell back to a bare state dump. Replies are now short and grounded.
- **Entropy seeds**: `eidolon-server` no longer defaults to seed 42 — a fresh organism
  derives its master seed from the system clock / random_device / pid unless `--seed` is
  given (matches `eidolon-sim`). Every restart produces a new world; `--deterministic`
  still requires `--seed`.
- **iGPU offload**: llama-server for Nemotron 3 Nano now runs with `--n-gpu-layers 20`
  (up from 14; the 42-layer model is generation-bound on the Radeon 740M, so 26+ layers
  add no speed and destabilized the Vulkan context).
- Gate: same seeded scenario produces the same individual state on native and WASM (parity test pending); no heavy work on the main UI thread

### Tests & tooling
- C++ unit suite runs in parallel: each test in its own process (`eidolon_tests --list`
  / `--name <t>`) driven by `python/tests/run_unit.sh` with `xargs -P$(nproc)`; pid-unique
  temp files for archive DBs, event logs and policy priors; ~5 s wall vs ~8 s serial.
- New test hooks: harness `currentTestName()`, `test_main --list`/`--name` filters.

### Phase 4 — Memory systems & sleep
- Full episodic encoding: Episode struct now carries participants, action, outcome,
  prediction, prediction error, emotional valence, social relevance, and relevance
  tags in addition to the original fields.
- MemorySystem: learned retrieval with recency/relevance weighting, per-tick importance
  decay, explicit strengthening API, and sleep consolidation pipeline.
- Sleep consolidation: runs on wake; replays high-importance episodes, rehearses skill
  sequences, extracts goal-relevant traces, creates compressed summaries, and archives
  consolidated episodes to the durable SQLite store.
- Dreams v1: associative recombination during sleep — randomly pairs recent episodes
  sharing location/participants/outcome to generate "dream traces" that nudge the
  policy toward recombined action sequences (no LLM).
- Engine hooks: per-tick importance decay; sleep-to-wake transition triggers
  consolidation + dreams; episode recording now captures full semantic context
  (participants, outcome, prediction/PE, emotional valence, social relevance).
- All C++ unit tests and Python integration tests pass; deterministic replay verified.
- Feasibility mapping of deterministic, seedable, LLM-free generative techniques into
  Eidolon: cellular automata (terrain/biome shaping + live plant ecology), L-systems
  (plant/branch geometry, river/root networks), procedural generation (landmarks,
  semantically tagged objects for memory ground truth), evolutionary algorithms (offline
  prior/wildlife/recipe search), grammars (event/goal templates, recipe rules) —
  DESIGN §22 + ROADMAP Phase 5 branch.
- First working PoC: `python/teacher/evolve_prior.py` evolves the policy-prior weights
  directly (5×(27+1) floats) on held-out deterministic seeds. Tournament selection +
  per-weight crossover + decaying gaussian mutation + elitism; fitness rewards survival
  and a balanced Forage/Drink/Rest diet, penalizes wasted Wander/Observe and behavioural
  fixation. Fully seedable (`--seed`); sim-eval per individual; `--progress-port` shows
  the search in the browser.
- `python/teacher/eval.py`: `evaluate()` per-policy per-seed rows (used by the EA) +
  `sim_eval()` aggregate reporting; run dirs are cleaned per evaluation (keeps /tmp
  tmpfs bounded — see MISTAKES).
- EA search is deterministic and reproducible; naive tick-maximising fitness converges to
  a Drink-fixated policy (4/5 seeds survive), so the fitness is deliberately survival- and
  diet-weighted. Best evolved prior lands in `data/priors/teacher_policy_evolved.eprp`
  (gitignored artifact).

### Teacher pipeline: results web UI + data quality & behavioural evaluation
- Progress UI now also reports **results** after the fit: train/val accuracy, label
  distribution, agreement of the teacher labels with the organism's own actions and with
  the reward heuristic, per-label mean body state ("drive sanity"), and cross-teacher
  agreement vs a second teacher's labels on the same records (`--compare-labels`).
- **Behavioural evaluation** of the fitted prior (`--eval-seeds`, `--eval-compare`,
  `--eval-days`): runs fresh deterministic `eidolon-sim` runs on held-out seeds with and
  without the prior and reports survival, per-action usage and final body state, all
  surfaced in the results UI (`python/teacher/eval.py`).
- `--keep-server` keeps the dashboard (bound to `0.0.0.0` by default, reachable on the
  LAN) alive after the fit so results can be inspected in the browser.
- Teacher prompt tightened to force a terse single-JSON answer; `max_tokens` is now 1024
  only for reasoning models (128 otherwise) — verbose local models now label in ~3 s
  instead of ~60 s.
- Labels are streamed to `--labels-out` as they are produced (resumable dataset).

### NIM (NVIDIA Nemotron) teacher evaluation
- First live NIM dataset run: 50 records, Nemotron-3-120B with thinking, 25 RPM. Teacher
  answered 49/50 (2% fallback), cross-teacher agreement vs local Qwen-4B on the same
  records 44%. Label distribution Drink 31 / Forage 10 / Wander 6 / Rest 2 / Observe 1.
- Behavioural eval (5 held-out seeds, 1 day): random init 73.1k non-agentic
  (Wander+Observe) ticks; local-4B prior 3.9k, 5/5 survive; NIM prior 4.5k, 4/5 survive
  (Drink-skewed prior collapses Forage). Verdict: NIM labels are sound but 50 records is
  too small / too Drink-skewed — a larger balanced pass is needed to beat the existing
  local-4B prior.

### Teacher pipeline: rate limiting + progress web UI
- Teacher requests are rate-limited to 25 RPM by default (NVIDIA NIM free-tier limit;
  configurable via `--rpm` / `EIDOLON_TEACHER_RPM`). The limiter is a sliding-window
  `RpmLimiter` in `python/teacher/label.py`.
- Dataset-generation progress web UI (`python/teacher/progress_server.py`): `train_prior
  --progress-port 8090` serves a light-theme dashboard at `http://127.0.0.1:8090`
  showing stage, records done/total, % bar, fallback/failed/skipped, rate + measured RPM,
  elapsed, ETA, per-action label counts and recent errors.
- `TeacherClient` supports NIM reasoning models: `--teacher-thinking` /
  `--teacher-reasoning-budget` (or `EIDOLON_TEACHER_THINKING` / `_REASONING_BUDGET`)
  send `chat_template_kwargs: {enable_thinking: true}` + `reasoning_budget`; `max_tokens`
  raised to 1024 so the final answer always fits after the chain-of-thought.

### Web UI (ChatGPT-style light theme)
- Light theme, ChatGPT-like layout: left sidebar with "New chat" and "Restart world"
  buttons, centered message column with avatars, dark user bubbles, auto-growing input.
- Delete old chats (`POST /api/conversations/delete`, ✕ per conversation).
- Start new chats (`POST /api/conversations/new`); conversations are titled from their
  first message.
- Start a fresh world with a brand-new organism (`POST /api/world/reset`; entropy seed by
  default, optional `seed`; re-applies the policy prior). The organism itself never sees
  the reset — it simply gets a new birth.

### LLM response parsing (reasoning models)
- Teacher client and server LLM bridge now handle reasoning-enabled models (DeepSeek,
  Nemotron, Qwen3, …) that emit a long chain-of-thought: the structured answer is taken
  from the **last balanced JSON object** in `content`, chat replies strip fence/thinking
  wrappers and stay bounded, and `reasoning_content` is never treated as the answer or
  surfaced to the user. Verified end-to-end against stub reasoning responses.

### Teacher pipeline (Phase 4 branch)
- `eidolon-sim --dump-experiences FILE`: one JSONL record per tick with the 27-feature
  state vector plus interpretable context (body drives, weather, nearest bush/water,
  eaten/drank, reward, novelty, threat, aversive/safe flags) for offline training.
- `eidolon-sim --policy-prior FILE` / `eidolon-server --policy-prior FILE`: seed a fresh
  organism's policy from a teacher-baked `.eprp` prior (magic "EPRP", version 1,
  5×(27+1) float32 softmax-linear weights). Online learning continues on top of the
  prior — it changes the initialization, never scripts behaviour. Snapshot round-trip and
  bit-exact replay verified with a prior loaded.
- `Policy::loadPrior` / `LearnSystem::loadPolicyPrior` / `Engine::loadPolicyPrior`: prior
  loading with header validation; the prior is serialized into the snapshot with the rest
  of learning state.
- Python teacher pipeline (`python/teacher/`, conda env `eidolon`, CPU PyTorch):
  `dataset.py` (validated JSONL loader), `label.py` (OpenAI-compatible teacher client —
  local llama-server by default, NVIDIA NIM via `EIDOLON_TEACHER_*`; reward-guided offline
  fallback so the pipeline runs fully without an LLM), `fit_prior.py` (softmax-linear fit
  + `.eprp` read/write), `train_prior.py` (end-to-end CLI, cached-label overlay so a
  labeled sample can be re-fit without re-calling the teacher).
- First real artifact: `data/priors/teacher_policy.eprp`, fitted from 100 live-labeled
  records (Qwen3-4B via the local llama-server) sampled across 4 seeds × 1 day. On 5
  held-out seeds it eliminates pointless Wander/Observe ticks entirely (vs ~30k per run
  with the random init) with all seeds surviving; train/val accuracy 0.84/0.70.
- Tests: `policy_loads_prior_and_retrains_online` (prior dominates init, learning moves
  weights, snapshot round-trip, identical continuation) and `python/tests/test_teacher.py`
  (dump → dataset → fit → sim reload → deterministic replay with prior + online updates
  running; stub OpenAI endpoint exercises the teacher client offline);
  `test_server.py` covers `--policy-prior` on the server.
- `python/requirements.txt` now pins `torch` (CPU) alongside numpy/requests.

### Phase 3 — Learning core (the mind starts)
- New `src/mind/` learning stack, all seeded and serialized inside the engine snapshot
  (snapshot v3):
  - `mlp` — tiny single-hidden-layer network (tanh hidden; linear/sigmoid/tanh output;
    backprop) shared by the value and threat nets.
  - `ValueNet` — TD(0) value estimator over a compact 27-feature state vector (perception,
    drive-scaled body state, neuromodulators, threat feedback); the reward-prediction
    error feeds everything else.
  - `ThreatNet` — learned p(threat|s) with aversive sensitization and safe extinction;
    stress and threat-sensitivity accelerate learning; the engine vetoes exploration when
    threat is high.
  - `Policy` — linear contextual bandit over 5 agentic actions (Forage/Drink/Rest/Wander/
    Observe) with softmax temperature from impulsivity × uncertainty and surprise-gated
    (PE) updates.
  - `Attention` — learned top-k salience over perception channels; outcome-driven
    upweighting; drive-state bias (hungry → food cues); stress narrows attention to k=2.
  - `Neuromod` — arousal, valence, stress, curiosity, novelty, uncertainty,
    prediction-error; couplings to attention, threat learning, exploration and episodic
    encoding.
  - `PersonalityLatent` (16-d) + `DriveWeights` — temperament priors from the seed, then
    a daily drift toward life statistics (reward/threat/novelty/success EMAs); drives
    shape the features the policy sees, so personality changes behaviour over weeks.
  - `LearnSystem` — facade owning the feature layout, intrinsic reward (homeostatic
    relief + pressure + novelty + need-gated event bonuses − pain/cold), TD + bandit +
    threat + attention learning per tick, life-stats EMAs, daily drift, aggregated
    learner metrics.
- Engine integration (`src/sim/engine`): features built each tick around the decision;
  learned policy decides with hardwired sleep/rest hysteresis + emergency safety valves;
  ThreatNet veto; TD reward learning after every outcome; negative-valence / prediction
  error boost episodic encoding; `rngLearn_` subsystem stream.
- CLI metrics (`metrics.log`) now report `phase=3` plus `learnerInferences`/`learnerUpdates`.
- Tests: `tests/test_learn.cpp` (14 tests — bandit success-rate rise on a repeated trial,
  TD convergence, threat sensitization/extinction, stress-accelerated threat learning,
  attention salience, personality divergence from identical priors, neuromodulator
  couplings, engine determinism of the latent, latent divergence across experiences,
  sustained survival with the learned policy). Python `test_cli.py` metrics test updated
  to phase=3. All 5 test seeds still survive 14 sim-days.
- Debugging notes: reward bonuses are gated on genuine need (a +0.8 "drank" bonus that
  fired at zero thirst made the organism camp by water — self-reinforcing Drink ~99%);
  `bodyTemp dev > 4` made all of winter aversive, saturating the ThreatNet and freezing
  the organism into a rest loop — acute danger only (dev > 8, pain, rapid health loss,
  critical drives) is now a threat, cold is just energy pressure; MLP backprop requires
  hidden activations from the current forward pass (update methods now recompute them
  internally instead of trusting stale caller buffers); personality drift tracks current
  life statistics, not just early life.

### Phase 2 — Minimal end-to-end organism
- World resources (`src/world`): berry bushes (density ~1/128 tiles, seeded regrowth
  capped), water drinking; `Perception` feature vector (sight radius 8, hearing 16,
  12 features).
- Drives → actions (`src/sim/engine`): forage (4 berries/meal) and drink drives with
  need thresholds, obstacle-aware greedy movement (with random escape to avoid local
  traps), rest-mode hysteresis; all 5 test seeds survive 14 days.
- Memory (`src/mind/memory.cpp`): bounded episodic ring (256 capacity) with importance
  scores, serialized in the snapshot; episodes for birth, weather, foraging, drinking,
  sleep/wake, near-death (throttled) and death.
- Persistence (`src/store/sqlite_archive.cpp`): SQLite (WAL) archive behind a portable
  `Archive` interface — episodes, events, conversations, messages; versioned schema
  (`user_version=1`). The engine core stays platform-independent.
- Chat server (`src/server` + `src/tools/eidolon-server.cpp`): `eidolon-server` runs the
  sim + serves a minimal ChatGPT-like UI (sidebar, chat, input) with `/api/status`,
  `/api/send`, `/api/conversations`, `/api/messages`, `/api/snapshot`. Browser
  disconnect does not stop the sim; autosave every 10 sim-minutes; SIGTERM/SIGINT
  triggers a graceful final save.
- LLM bridge (`src/llm/bridge.cpp` + `src/core/json.cpp`): OpenAI-compatible endpoint
  (llama.cpp), message → structured semantics parse, snapshot → grounded reply, timeouts,
  and deterministic offline fallback replies grounded in real state. Live replies were
  verified through the local Qwen3-4B model on the Radeon 740M iGPU (Vulkan backend).
- Tests: 4 new C++ unit-test files (memory, archive, json, bridge) and 6 new server
  integration tests (UI+status, restart continuity, offline fallback, dead-LLM
  resilience, archive written, sim without browser). Integration suite now runs test
  files in parallel across all CPU cores.
- Performance: 30-day headless sim ~0.2 s, event log bounded (6.8K lines vs 173K naive).

### Phase 1 — Core runtime skeleton
- Added `eidolon-sim` CLI (`--data --days --seed --deterministic --world --status-interval`):
  builds, runs, and snapshots a deterministic day/night simulation.
- Simulation core (`src/sim/engine.cpp`): adaptive tick (fine 1s / coarse 10s / sleep 30s),
  event queue, weather-driven day/night, life-mode transitions (active/rest/sleep), atomic
  versioned binary snapshot + save/resume (byte-identical continuation).
- World (`src/world/world.cpp`): 2D terrain grid (plains/forest/hills/water/desert),
  seeded generation, seasonal temperature curve (day 0 = spring), weather states
  (clear/rain/storm) with 30-minute persistence cooldown.
- Body (`src/body/physiology.cpp`): homeostatic model — energy/hunger/thirst/fatigue,
  regulated core temperature with thermoregulation cost, sleep recovery, health & pain,
  starvation and dehydration death.
- Randomness (`src/core/rng.cpp`): xoshiro256++ with per-subsystem streams; `--deterministic`
  guarantees bit-exact replay from a seed.
- Events (`src/core/log.cpp`): append-only `events.log` with sim-time stamps (no wall clock),
  status lines every 10 sim-minutes, per-run `metrics.log`.
- Tests: 34 C++ unit tests (rng, serialize, clock, world, physiology, engine) and 5 Python
  integration tests (determinism, seed divergence, save/load continuity, metrics, life trace);
  all passing, `-Wall -Wextra` clean.

### Phase 0 — Repository bootstrap
- Initialized git repository (user `Clark Dale`, remote `git@github.com:cdale11/eidolon.git`).
- Added project documentation: README (overview/quickstart), DESIGN (consolidated
  architecture), ROADMAP (sequential phases & gates), AGENTS (agent SOP), MISTAKES (lesson
  log), CHANGELOG.
- Environment audit: g++ 14.3.1, CMake 3.30.8, ninja, sqlite3 headers, conda env `eidolon`,
  llama.cpp with llama-server + GGUF models, ROCm 6.2.1 iGPU runtime (Radeon 740M,
  gfx1103); PyTorch policy: CPU default, iGPU experimental.

### Docs — requirement gaps closed (both briefs)
- Added explicit intrinsic-drive list (incl. affiliation, optional status/achievement),
  model quantization policy, tiny RNN allowance, theory-of-mind (beliefs about what others
  believe) and learned concept-relationship edges (DESIGN §6/§8/§12).
- Added DESIGN §17 "Client-First Compute Architecture": portable `ReplicaCore` with
  Native/WebAssembly/future-Console backends, capability detection → ComputeProfile, worker
  partitioning + ComputeScheduler, adaptive fidelity, checkpoint/delta sync, offline client
  execution + reconcile, configurable world authority, browser checkpoints, server fallback
  roles, Xbox policy, per-backend profiling + benchmark suite.
- Roadmap extended to 14 phases: new Phase 11 (portable WASM client compute) and Phase 12
  (sync, offline persistence, backend selection); parity/sync/benchmark tests added to the
  testing strategy and risk table.