# Changelog

All notable user-visible changes to Eidolon, grouped by phase. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

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