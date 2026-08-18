# Eidolon

A performant, persistent **artificial-human organism simulation** with a minimal ChatGPT-like web UI.

Eidolon is not an LLM chatbot. It is an autonomous digital organism with a simulated body,
a small brain of online-learned neural models, memories that consolidate in sleep, an evolving
personality, skills it acquires through experience, and a small living world that pressures it
to survive. It keeps living, perceiving, learning and changing while you are offline. When you
return, you can chat with the same individual — who may be happy to see you, scared of the dogs
that chased it, or proud of the shelter it built while you were away. Nothing about its
personality is written in a prompt; everything emerges from persistent internal state and
experience.

```
        ┌───────────────────────────────────────────────────────────┐
        │  WORLD ↔ BODY ↔ NEURAL STATE ↔ LEARNING ↔ MEMORY ↔        │
        │          ↔ COGNITION ↔ ACTION ↔ WORLD                     │
        └───────────────────────────────────────────────────────────┘
```

## Core principles

1. **The organism exists independently of the player.** It runs headless, offline, indefinitely.
2. **The LLM is a mouth and ears, not a mind.** A local `llama-server` instance handles natural
   language (understanding user messages, producing grounded replies). The mind is persistent
   computational state: small MLPs, online linear models, associative memories, TD value
   learning, learned attention, learned world prediction.
3. **No personality in prompts.** Temperament starts from broad biological priors (reward
   sensitivity, threat sensitivity, novelty sensitivity, social sensitivity, impulsivity,
   persistence, attachment sensitivity, stress reactivity). Personality is a slowly learned
   latent vector that diverges with experience.
4. **Small, specialized models.** No giant networks, no vector DB, no unbounded context.
   Several tiny CPU-efficient models, each doing one job, online-updatable, persisted.
5. **Truthful memory.** When asked "what happened while I was away?" it retrieves its actual
   timeline. When asked "why are you afraid of dogs?" it retrieves the learned association and
   the events that formed it. It may say "I don't know". It never fabricates.

## Repository layout

```
src/         C++17 runtime: world, body, neural state, learning, memory, cognition, LLM bridge, server, tools
web/         Vanilla JS/CSS chat UI (ChatGPT-like, lightweight)
python/      Offline tooling only (conda env "eidolon"): teacher training (NVIDIA NIM),
             data generation, analysis, integration test drivers. Never in the hot path.
tests/       C++ unit tests + Python integration tests (seeded replay, long-run, performance)
third_party/ Vendored single-header libs (cpp-httplib, …)
data/        Runs, saves, logs (gitignored)
docs in      README.md, DESIGN.md, ROADMAP.md, AGENTS.md, MISTAKES.md, CHANGELOG.md
```

## Quickstart

Prerequisites: g++ ≥ 12 (C++17), CMake ≥ 3.20, ninja, sqlite3 dev headers, a built
`llama-server` with a GGUF model (recommended: `Qwen3-4B-Instruct-Q4_K_M.gguf`).

```bash
# Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run tests (C++ unit)
./build/tests/eidolon_tests

# Headless simulation (no UI; runs until SIGINT)
./build/bin/eidolon-sim --data data/runs/run1 --days 2

# Web server (chat UI at http://localhost:8080)
./build/bin/eidolon-server --data data/runs/run1 --port 8080 \
    --llm-url http://127.0.0.1:8081 --llm-model Qwen3-4B-Instruct-Q4_K_M.gguf

# Seeded deterministic replay (debugging)
./build/bin/eidolon-sim --seed 42 --deterministic --data data/runs/replay1 --days 1
```

The simulation runs entirely inside the C++ process. The browser is only a client; closing
it never stops the organism. The LLM is optional at runtime: the organism lives, learns and
acts with zero LLM calls; language is added when a provider is reachable.

## Configuration

`eidolon.toml` (or CLI flags) controls: world size, seed, tick policy, physiology setpoints,
learning rates, model sizes, memory bounds, LLM endpoint/model/timeouts, autosave policy,
observability sampling. All learned parameters and state persist across runs.

## Persistence

Hybrid scheme, versioned with migrations:

- **Binary snapshot** (atomic rename, checksummed): world, body, neural weights, personality
  latent, skills, beliefs, goals, self-model, relationships, RNG streams.
- **SQLite (WAL)**: archived memories, event timeline, conversations, concepts, schema
  metadata.

Load restores the same individual, mid-life, exactly as it was.

## Observability

`GET /api/metrics` (or `--stats` in headless mode) reports: simulated time, tick rate,
CPU/RAM, neural inference counts, learning updates, LLM calls & latency, active goals,
memory counts, model sizes, world event counts.

## Documents

- `DESIGN.md` — consolidated design: requirements, architecture, implementation details, ideas.
- `ROADMAP.md` — sequential build phases with acceptance criteria.
- `AGENTS.md` — agent SOP (commit discipline, tools, conda env, coding conventions).
- `MISTAKES.md` — running log of design mistakes and lessons.
- `CHANGELOG.md` — versioned change log.
