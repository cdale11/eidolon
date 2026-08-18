---
name: perf-guard
description: Enforce Eidolon's performance budgets when modifying hot paths, memory systems, learning models, world simulation, or during Phase 11/13 performance work — allocation-light rules, --bench benchmarks, RSS/memory stability checks, before/after comparisons, and the performance budgets from DESIGN §18. Use when touching performance-critical code or when asked to benchmark, profile, or check memory.
---

# Performance Guard

Performance is a first-class requirement (DESIGN §17-§18). Use this skill whenever you
modify hot-path code, anything touching memory/learning models, or during performance
phases.

## Budgets (from DESIGN §18 — the gate)

- Idle/sleep CPU: < 5% of one core; active moments < 40%.
- Fine tick p50 ≤ 2 ms; snapshot save ≤ 100 ms; boot ≤ 1 s.
- Total learned weights ≈ 200–600 k floats (a few MB max).
- Hot memory ring bounded (≤ 4096 episodes); archive prunes detail.
- RSS growth flat over months of simulated life (no unbounded growth).
- Benchmark target: 30 simulated days headless < ~10 min wall time.

## Hot-path rules

- No heap allocation churn in tick loops: pools, arenas, ring buffers, pre-allocated
  structs/vectors. Never `new`/`push_back` a hot-path container every tick.
- No Python in the C++ runtime, ever.
- No LLM calls in the hot path (snapshot/semantic layers only, throttled).
- Struct-of-arrays over array-of-structs where iteration is the hot path.
- Spatial indexing for world entity lookups (no linear scans of the whole world).
- Adaptive clock: fine ticks only during active/high-arousal periods.

## Measurement procedure

Baseline FIRST, then change, then measure again. Without before/after numbers the gate is
not passed.

```bash
# Benchmark mode (30 sim-days headless)
./build/bin/eidolon-sim --bench --data data/runs/bench --days 30

# Check metrics in the report: tick p50/p95, RSS end, inferred counts, model bytes
grep -E "p50|p95|RSS|rss|inference|weights" data/runs/bench/metrics.log
```

RSS growth check (long-run memory stability):

```bash
./build/bin/eidolon-sim --data data/runs/long --days 7 --autosave-interval 3600 &
# sample RSS over time:
watch -n 60 "ps -o rss= -p \$(pgrep eidolon-sim)"
```

RSS must plateau. If it grows with simulated time, find the leak: unbounded containers,
SQLite blobs held in RAM, memory ring not pruning, archives not summarising.

Allocation audit (when suspicion of churn):

```bash
valgrind --tool=massif ./build/bin/eidolon-sim --data data/runs/massif --days 1
# or simpler: count allocations with --dhat or LD_PRELOAD malloc counters; a plateauing
# massif heap snapshot graph means steady-state allocation is healthy.
```

## When to run this skill

- Any change touching: tick loop, world grid/spatial index, memory ring/archive, learners,
  planning search, perception feature extraction, serialization.
- Any Phase 11/13 work (performance hardening, long-run stability).
- Whenever the user asks to benchmark or profile.
