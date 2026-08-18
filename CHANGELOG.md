# Changelog

All notable user-visible changes to Eidolon, grouped by phase. Format inspired by
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

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