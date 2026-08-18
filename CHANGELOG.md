# Changelog

All notable user-visible changes to Eidolon, grouped by phase. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

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