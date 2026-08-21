# Eidolon — Development Procedures & SOPs

This document consolidates all standard operating procedures, development workflows, and maintenance guidelines for the Eidolon codebase. It is the authoritative reference for any agent (AI or human) working in this repository.

---

## 1. Environment Setup (Mandatory)

### 1.1 System Requirements
- **OS**: Fedora 41 (or compatible Linux)
- **Compiler**: g++ 14.3.1, CMake 3.30.8, Ninja
- **Hardware**: AMD Ryzen 3 8300GE (8 threads), ~6 GB RAM, AMD Radeon 740M iGPU (RDNA 3, gfx1103)
- **Python**: Conda env `eidolon` (Python 3.12.13) — **never use base env**

### 1.2 Conda Environment
```bash
conda activate eidolon  # already active in provided shells
# Install packages as needed:
pip install numpy pyyaml requests torch  # for offline tooling only
```

### 1.3 LLM Server (Local)
```bash
# Vulkan backend (default for iGPU inference):
cd ~/llama.cpp && cmake -B build-vulkan -DGGML_VULKAN=ON -DGGML_CUDA=OFF
cmake --build build-vulkan -j$(nproc)

# Launch (optimized for 740M's tiny VRAM):
~/llama.cpp/build-vulkan/bin/llama-server \
  -m ~/llama.cpp/Qwen3-4B-Instruct-Q4_K_M.gguf \
  --device Vulkan0 --threads 8 --ctx-size 2048 --port 8080 \
  --n-gpu-layers 14 --no-kv-offload --cache-ram 0 \
  --cache-type-k q8_0 --cache-type-v q8_0 --no-mmproj
```

### 1.4 NVIDIA NIM (Teacher Training)
```bash
export EIDOLON_TEACHER_API_KEY='nvapi-...'
export EIDOLON_TEACHER_BASE='https://integrate.api.nvidia.com/v1'
export EIDOLON_TEACHER_MODEL='meta/llama-3.1-8b-instruct'  # or other model
# Use python/teacher/train_prior.py for offline prior generation
```

---

## 2. Build & Test (Gate Before Every Commit)

### 2.1 Standard Build
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra"
cmake --build build -j$(nproc)
```

### 2.2 Test Suite (All Must Pass)
```bash
./build/tests/eidolon_tests                    # C++ unit tests
python/tests/run_integration.sh                # Python integration (parallel, conda eidolon)
./build/bin/eidolon-sim --data data/runs/check --days 1  # Autonomous smoke run
```

### 2.3 Inspect Event Log
```bash
cat data/runs/check/events.log  # Check for anomalous behaviour
```

### 2.4 Deterministic Replay (For Behavioural Changes)
```bash
./build/bin/eidolon-sim --data data/runs/replay --days 1 --seed 42 --deterministic
diff data/runs/replay/events.log data/runs/previous/events.log
```

### 2.5 Performance Benchmarks
```bash
./build/bin/eidolon-sim --bench --bench-ticks 5000
# Or: ./build/bin/eidolon-sim --bench --bench-json
```

---

## 3. Code Conventions

### 3.1 C++17 Style
- **Standard**: C++17, CMake + Ninja, `-Wall -Wextra` clean
- **RAII**: Mandatory; no exceptions in hot tick path (keep `noexcept` discipline)
- **Structs over classes**: Unless polymorphism needed
- **No heap churn in tick loops**: Pools/arenas/ring buffers only
- **Determinism**: All stochastic behaviour seedable (`--seed N --deterministic`)

### 3.2 Architecture Invariants (Never Break)
1. **Organism independence**: Exists without user; UI is client, not host
2. **No personality as prompt text**: Emerges from `PersonalityLatent` (data, not words)
3. **LLM never mutates state**: Only validated structured actions; no LLM per tick
4. **Bounded memory/models**: Hot path allocation-light; no Python in C++ runtime
5. **Persistence**: Hybrid binary snapshot + SQLite (WAL), versioned, atomic, with migrations
6. **Single organism**: One humanoid; other agents are wildlife
7. **Engine portability**: `ReplicaCore` (simulation, organism, neural, memory, learning, world, planning, persistence) must never depend on browser/platform APIs
8. **Client-first compute**: Server persists/syncs; client performs max work it can support

### 3.3 Serialization
- Versioned headers; never serialize whole state per tick
- `bytes()` takes **byte count**, not element count — multiply by `sizeof(T)` for non-uint8 vectors
- Snapshot version bumped on schema changes; migrations in SQLite

### 3.4 Deterministic Sorting
- All `std::sort` in deterministic core must have tie-break:
  ```cpp
  return (a.key > b.key) || (a.key == b.key && a.index < b.index);
  ```
- Audit sites: `Attention`, `WorldPredictor`, `GoalEmergence`, `ConceptFormation`, `Voronoi`

---

## 4. Development Workflow

### 4.1 Standard Operating Procedure (SOP)
1. **Git first**: Repo initialized with user.name=`Clark Dale`, user.email=`clarkdale123@yahoo.com`, remote=github
2. **Commit at every step**: One logical step = one commit (`git add <files> && git commit -m "phase: short summary"`)
3. **Push after every commit**: `git push origin master`
4. **Use tools/skills**: Load relevant skills (e.g., `phase-gate`, `perf-guard`)
5. **Ask when in doubt**: Use question tool for ambiguous requirements
6. **Don't introduce bugs**: Build → unit tests → integration tests → autonomous sim → inspect log → fix root causes
7. **Install what you need**: Use package manager/conda; record in env setup notes
8. **Python = conda env `eidolon`**: Never in base, never outside conda

### 4.2 Commit Message Style
```
phase: short imperative summary
```
Examples: `core: add adaptive clock`, `mind: fix threat veto ordering`, `test: fix phase5 gate`

### 4.3 Branching & Merging
- **Master only**: No feature branches; commit directly to master after passing gates
- **Never amend/force-push** without explicit user request
- **No unrelated files** in same commit

---

## 5. Testing Procedures

### 5.1 Test Levels
1. **Unit** — Physiology invariants, TD convergence, ThreatNet extinction, Beta skill updates, memory decay, snapshot round-trip, RNG replay determinism
2. **Integration (Seeded Replay)** — Deterministic runs with fixed `--seed`; event sequences reproduce bit-for-bit
3. **Behavioural Scenarios** — Autonomous seeded runs: offline autonomy, learning/adaptation, personality drift, social learning, skill learning, construction, environmental pressure, sleep consolidation, LLM failure, save/load identity, long-run memory stability, performance budgets
4. **Client-Compute & Sync** — WASM parity, capability detection, ComputeScheduler, offline execution/reconnect, client/server-authoritative modes, browser checkpoint, delta/compression
5. **Backend Benchmark** — Native / WASM CPU / WASM SIMD / WASM MT / WebGPU throughput/memory/latency
6. **Adversarial** — Corrupted save, kill -9 during save, garbage provider, sync from divergent states

### 5.2 Phase Gate (Mandatory Before Commit)
Run the `phase-gate` skill which executes:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra"
cmake --build build -j$(nproc)
./build/tests/eidolon_tests
python/tests/run_integration.sh
./build/bin/eidolon-sim --data data/runs/check --days 1
# Inspect events.log for anomalies
# For behavioural changes: seeded replay diff
git add <relevant> && git commit -m "phase: summary" && git push origin master
```

---

## 6. Key Subsystems & Debugging

### 6.1 Engine (`src/sim/engine.cpp`)
- **Tick structure**: Perceive → Build features → Decide → Execute → Physiology update → Learn step → Log
- **Adaptive step**: Fine (1s) / Coarse (5-30s) / Sleep (30-60s)
- **Survival valves** (in `decide()`): Predator within 3 → Flee; Thirst>55 → Drink; Hunger>80 → Forage; Pain>40 → Rest (unless predator in sight)
- **Threat veto**: If threat>0.65 and policy chooses Wander/Observe/Rest → Flee (if predator in sight) else Rest
- **Directed exploration**: `exploreStep()` — 16-tick (32 at edges) directional walk

### 6.2 Learning (`src/mind/learn.cpp`)
- **Models**: ValueNet (TD), ThreatNet (aversive/safe), Policy (contextual bandit), Attention (top-k)
- **Neuromodulators**: Arousal, valence, stress, curiosity, uncertainty, predictionError
- **Personality**: 16-dim `PersonalityLatent` drifted daily by `LifeStats`
- **Teacher prior**: `.eprp` files (versioned, feature/action count validated)

### 6.3 World (`src/world/world.cpp`)
- **Generation**: Noise-field elevation, biome, rivers, plants, water sources, wildlife
- **Wildlife**: Wolves (hunt), rabbits (forage/flee), boids flocking
- **Wildlife step**: Every 5 sim-seconds (`kInterval=5`), accumulator pattern
- **Spatial hash**: Rebuilt each wildlife step; call `rebuildHashForDebug()` after test teleports

### 6.4 Persistence (`src/sim/engine.cpp`, `src/store/sqlite_archive.cpp`)
- **Binary snapshot**: `save.snap` (atomic write-temp + fsync + rename, versioned, checksummed)
- **SQLite WAL**: `memory.db` (episodes, events, conversations, concepts, schema versions)
- **Autosave**: Every 10 sim-minutes (600s), on significant events, at exit

### 6.5 Server (`src/server/server.cpp`)
- **Auto-rebirth**: On death, fresh organism with new seed, same world, heredity/prior loaded
- **Rebirth count**: Tracked in status API (`rebirths`)
- **Endpoints**: `/api/status`, `/api/send`, `/api/conversations`, `/api/snapshot`, `/api/world/reset`
- **LLM Bridge**: Parse (user msg → JSON), Respond (snapshot → grounded reply), Fallback (state-based)

### 6.6 Client-First Compute (`src/mind/compute_profile.cpp`)
- **ComputeProfile**: SIMD, threading, GPU, memory, performance hints
- **Backend selection**: WebGPU → WASM SIMD MT → WASM SIMD → WASM Plain → ServerFallback
- **Fidelity levels**: Low (60 sim-s/wall-s) / Medium (300) / High (1000)
- **Sync**: Checkpoint/delta (compact binary), client-authoritative cognition

---

## 7. Common Debugging Patterns

### 7.1 Run Single Test with Debug
```bash
./build/tests/eidolon_tests --gtest_filter=phase5_gate_survival_improves_with_experience
```

### 7.2 Seeded Replay for Determinism
```bash
./build/bin/eidolon-sim --data data/runs/debug --days 1 --seed 42 --deterministic
```

### 7.3 Inspect Snapshot
```bash
./build/bin/eidolon-sim --data data/runs/foo --bench  # Includes backend selection debug
```

### 7.4 Profile Hot Path
```bash
./build/bin/eidolon-sim --bench --bench-ticks 10000 --bench-json
```

### 7.5 Live Server Debug
```bash
curl http://localhost:8081/api/metrics   # Scheduler, stats, learner, fidelity
curl http://localhost:8081/api/status    # Organism state
curl -X POST http://localhost:8081/api/world/reset -d '{}'  # Fresh organism
```

---

## 8. Performance Budgets (DESIGN §18)

| Metric | Target |
|--------|--------|
| Idle/sleep CPU | < 5% of one core |
| Active CPU | < 40% of one core |
| Fine tick p50 | ≤ 2 ms |
| Snapshot save | ≤ 100 ms |
| Boot time | ≤ 1 s |
| Learned weights | 200–600 k floats (~2–5 MB) |
| Hot memory ring | ≤ 4096 episodes |
| RSS growth | Flat over months of sim time |
| 30 sim-days headless | < 10 min wall time |

---

## 9. Mistake Avoidance Checklist (From MISTAKES.md)

- [ ] **Wake threshold < sleep-entry threshold** (strictly) — prevents sleep-death loop
- [ ] **Drink fall-through** — adjacent-to-water but source dry must fall through to waterskin
- [ ] **No 1-tile random walks** — use `exploreStep()` for sustained directional walk
- [ ] **Wildlife accumulator while-loop** — `while (accum_ >= kInterval)` not `if`
- [ ] **Pain valve checks predator** — `!nearestPredator(...)` before forcing Rest
- [ ] **Spatial hash rebuilt** after test teleports — call `rebuildHashForDebug()`
- [ ] **Test wolf hunger = 55** — attack threshold, not 90 (starvation during comparison)
- [ ] **Deterministic sort tie-break** — `a.key == b.key && a.index < b.index`
- [ ] **Serialization byte count** — `sizeof(T) * count` for non-uint8 vectors
- [ ] **Policy prior version match** — feature count (45) and action count (12) must match
- [ ] **Teacher labels to persistent path** — `data/priors/`, never `/tmp`
- [ ] **Sleep wake threshold < block threshold** — documented in DESIGN §13

---

## 10. Documentation Maintenance

### 10.1 Files to Update in Same Commit as Code
- `README.md` — Overview & quickstart
- `DESIGN.md` — Consolidated design (source of truth)
- `ROADMAP.md` — Phases & gates
- `MISTAKES.md` — Append when mistake costs real time
- `CHANGELOG.md` — User-visible changes per phase
- `DEVELOPMENT.md` — This file (update procedures as they evolve)

### 10.2 Versioning
- Snapshot version: bump on schema change (`kSnapshotVersion` in `engine.cpp`)
- SQLite schema: version table with migrations
- Prior format: `.eprp` header includes feature/action count

---

## 11. Emergency Procedures

### 11.1 Server Crashes
```bash
pkill -f eidolon-server
# Check data/runs/server/save.snap integrity
# Restart with same --data dir (auto-resumes from snapshot)
```

### 11.2 LLM Server Down
- Organism continues with deterministic fallback replies
- Check `llama_server.log` for OOM/crash
- Restart llama-server; game server reconnects automatically

### 11.3 Corrupted Snapshot
```bash
# Engine reports clean error; never silent reset
# Fall back to previous autosave or manual backup
```

### 11.4 Power Loss During Teacher Run
- Experience dumps: regenerable via `--dump-experiences --deterministic`
- NIM labels: **must write to `data/priors/`**, not `/tmp` (tmpfs = lost on power loss)
- Mirror labels periodically during long runs

---

## 12. Adding New Features (Checklist)

1. [ ] Read `DESIGN.md` for architectural fit
2. [ ] Follow C++ conventions (RAII, noexcept hot path, no heap churn)
3. [ ] Add unit test in `tests/` (deterministic, seeded)
4. [ ] Update serialization if new state added (version bump)
5. [ ] Update SQLite schema if new tables/columns (migration)
6. [ ] Run full phase gate (build, test, sim, inspect)
7. [ ] Update `DESIGN.md`, `CHANGELOG.md`, `MISTAKES.md` if applicable
8. [ ] Commit with proper message, push

---

*This document is the authoritative reference for development procedures. Update it in the same commit as any procedural changes.*