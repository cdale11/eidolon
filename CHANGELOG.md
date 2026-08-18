# Changelog

All notable user-visible changes to Eidolon, grouped by phase. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

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