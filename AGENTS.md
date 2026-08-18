# AGENTS.md — Operating Procedures for AI Agents in this Repository

Rules for any agent (AI or human) working in this repo. Read before touching anything.

---

## 1. SOP (Standard Operating Procedure) — mandatory

1. **Git first.** The repo is already initialized and configured:
   - user.name = `Clark Dale`, user.email = `clarkdale123@yahoo.com`
   - remote `origin` = `git@github.com:cdale11/eidolon.git`
   If you ever clone fresh: `git init`, set the two config values above, add the remote.
   Never change these without asking the user.
2. **Commit at every step.** One logical step = one commit. After any completed, verified
   change: `git add <relevant files>` + `git commit -m "<concise message>"`. Use the repo's
   commit style (imperative, short summary line). Never commit secrets or large binaries
   (see `.gitignore`). Never commit unrelated files together. Never amend/push/force-push
   without explicit user request. **Push after every commit** (`git push origin master`).
3. **Use tools and skills.** Use the available skills, search tools, and task agents as
   needed. When a task matches the `customize-opencode` skill (opencode config only), load
   it. Prefer the read/edit/write tools over shell file surgery.
4. **Ask the user when in doubt.** If a requirement is ambiguous, the architecture choice
   matters, or the user's intent is unclear — use the question tool. Do not guess on things
   that are expensive to redo.
5. **Don't introduce bugs.** After every change: compile with warnings enabled
   (`-Wall -Wextra`), run unit tests, run integration tests, run an autonomous simulation,
   inspect the event log for anomalous behaviour, fix root causes. A change that breaks the
   build or tests is not a completed step — it is a step that must be fixed before commit.
6. **Install what you need yourself, or ask the user to install it.** If a required tool is
   missing: try to install it yourself (package manager, pip, etc.) using the standard
   project setup; if you lack permissions or it's ambiguous, ask the user to install it.
7. **Python = conda env `eidolon`, always.** Any Python work (tooling, tests, training,
   analysis) must run in the `eidolon` conda environment: `conda activate eidolon` (it is
   already active in shells) — install every required package into that env, never into
   `base`, and never outside conda. Record new dependencies in the env setup notes below.
   Python is for offline tooling/tests only — never in the C++ runtime hot path.

## 2. Environment & hardware

- **OS**: Fedora 41, g++ 14.3.1, CMake 3.30.8, ninja, sqlite3 dev headers (`/usr/include/sqlite3.h`).
- **CPU**: AMD Ryzen 3 8300GE (8 threads), ~6 GB RAM total. Keep runtime small and CPU-first.
- **iGPU** (important for ML dependency decisions): **AMD Radeon 740M (RDNA 3, `gfx1103`,
  Phoenix2 APU)**, 0.5 GB VRAM, `amdgpu` driver, `/dev/dri/renderD128`, `/dev/kfd` present,
  ROCm 6.2.1 userspace installed.
  - **PyTorch note**: official ROCm PyTorch wheels historically lack `gfx1103` kernels
    (rocBLAS Tensile / MIOpen); `HSA_OVERRIDE_GFX_VERSION=11.0.0` is NOT a viable workaround
    (page faults / hard hangs). **Default: CPU PyTorch** in conda env `eidolon` (our models
    are tiny). iGPU acceleration is experimental — only via ROCm nightly/TheRock builds
    that ship gfx1103 kernels; don't rely on it.
- **LLM**: local llama.cpp build at `~/llama.cpp` with built `llama-server` and GGUF models
  (`Qwen3-4B-Instruct-Q4_K_M.gguf` recommended default). The sim must run fully without it.
  - **iGPU inference (default for the LLM)**: rebuild llama.cpp with the Vulkan backend
    (`cmake -B build-vulkan -DGGML_VULKAN=ON -DGGML_CUDA=OFF`; Mesa `libvulkan_radeon.so`
    + `glslc` present) and launch inference on the Radeon 740M. Optimized for ~6 GB total
    RAM and the 740M's tiny VRAM (cap ~1 GB via exact layer count; `auto` offloads too much):
    `~/llama.cpp/build-vulkan/bin/llama-server -m ~/llama.cpp/Qwen3-4B-Instruct-Q4_K_M.gguf
    --device Vulkan0 --threads 8 --ctx-size 2048 --port 8080 --n-gpu-layers 14
    --no-kv-offload --cache-ram 0 --cache-type-k q8_0 --cache-type-v q8_0 --no-mmproj`
    (~1.05 GiB GTT on the iGPU, KV cache quantized in CPU RAM, no prompt-cache RAM).
    Note the model path lives in `~/llama.cpp/` root, not `models/`.
  - **eidolon-server on the LAN**: launch with `--host 0.0.0.0 --llm
    http://127.0.0.1:8080/v1 --llm-timeout 20000`; firewall already permits
    ports 1025-65535/tcp, so the chat UI is reachable at `http://<lan-ip>:8081`.
- **Conda env `eidolon`**: Python 3.12.13, activated by default in this shell. Current
  packages: pip/setuptools/wheel only. Packages needed for tooling (e.g. numpy, pyyaml,
  requests, torch-CPU) must be installed here and recorded in `python/requirements.txt`.
  `uv`/`uvx` are installed here too and run the SQLite MCP server for opencode
  (`.opencode/opencode.json`).

## 3. Build & test (the gate before every commit)

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra"
cmake --build build -j$(nproc)            # all CPU threads
./build/tests/eidolon_tests              # C++ unit tests
python/tests/run_integration.sh          # Python integration drivers, parallel on all cores (conda eidolon)
./build/bin/eidolon-sim --data data/runs/check --days 1   # autonomous smoke run
```

Build with all CPU threads (`-j$(nproc)`, ninja auto-parallelizes); the integration suite
runs each `test_*.py` file in parallel with a distinct `PORT_BASE`. The simulation core is
deliberately single-threaded for bit-exact determinism (project invariant) — parallelize
around it (tests, seeds, LLM threads), never the tick itself.

Inspect `data/runs/check/events.log` for anomalous behaviour before committing. For
behavioural changes: run a seeded replay (`--seed 42 --deterministic`) and diff against the
previous commit's log where determinism is expected.

## 4. Project invariants (never break)

- The organism exists independently of the user; the UI is a client, not the host.
- **No personality, biography, trauma, beliefs or goals as prompt text** — they emerge from
  persistent state (see DESIGN.md §1). PersonalityLatent is data, not words.
- **LLM is never the source of truth or the persistent mind**; it never mutates world state
  directly (only validated structured actions). No LLM per tick. Must work with LLM absent.
- Memory/context/model sizes stay bounded; hot path stays allocation-light; no Python in the
  C++ runtime; every stochastic behaviour is seedable (`--seed N --deterministic`).
- Persistence is hybrid binary snapshot + SQLite (WAL), versioned, atomic, with migrations.
- Only one humanoid organism; other agents are wildlife; social cognition targets the user
  and wildlife.
- **Engine portability**: `ReplicaCore` (simulation, organism, neural systems, memory,
  learning, world, planning, persistence logic) must never depend on browser or platform
  APIs; platform code lives behind `NativeBackend` / `WebAssemblyBackend` / future
  `ConsoleBackend` (DESIGN §17). The server is never the default compute bottleneck — the
  client performs the maximum work it can support; the server persists, syncs and guarantees
  continuity via headless fallback.

## 5. Coding conventions

- C++17; CMake + Ninja; `-Wall -Wextra` clean; RAII; no exceptions in the hot tick path
  where measurable (keep tick `noexcept`-style discipline); structs/vectors over classes
  unless needed; no heap churn in tick loops (pools/arenas/ring buffers).
- Small specialized models over one big model (DESIGN §8); simplest model that works.
- Serialization: versioned headers; never serialize whole state per tick.
- Commit message style: `phase: short imperative summary` (e.g. `core: add adaptive clock`).

## 6. Documentation

- `README.md` — overview & quickstart. `DESIGN.md` — consolidated design (source of truth).
- `ROADMAP.md` — phases & gates. `MISTAKES.md` — running log of mistakes/lessons (append
  when a mistake costs real time). `CHANGELOG.md` — user-visible changes per phase.
- Update the relevant doc in the same commit as the code change.
