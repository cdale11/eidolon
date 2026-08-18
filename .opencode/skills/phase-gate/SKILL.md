---
name: phase-gate
description: Run the mandatory pre-commit gate for Eidolon after ANY code change — build with warnings enabled, unit tests, integration tests, autonomous simulation smoke run, event-log anomaly inspection, seeded replay diff, then commit and push. Use whenever you are about to commit a code change or whenever a build/test gate is required.
---

# Phase Gate (mandatory before every commit of code changes)

Every completed step in this repo ends with this gate (AGENTS.md §3). Never skip it. A
change that breaks the build or tests is NOT complete — fix root causes, then commit.

## 1. Build (Release, warnings on)

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra"
cmake --build build -j
```

- Zero warnings tolerated. Fix warnings, do not silence them.

## 2. Tests

```bash
./build/tests/eidolon_tests              # C++ unit tests
python/tests/run_integration.sh          # Python integration drivers (conda env eidolon)
```

## 3. Autonomous smoke run + event log inspection

```bash
./build/bin/eidolon-sim --data data/runs/check --days 1
```

Then inspect `data/runs/check/events.log` and look for anomalies:

- Impossible physiology (negative hunger, NaN, energy > max for long stretches).
- Actions that should not be possible (eating without food nearby, sleeping while
  fleeing).
- Value/threat/learning signals that explode or freeze (no learning at all is a bug too).
- Memory count / RSS growth out of proportion to simulated time.
- Goal churn: goals that open and close every tick without progress.
- Clock anomalies: sim time not advancing, tick step sizes wrong for the state.

Fix any root cause before committing. "It's random" is not an excuse — use a seed.

## 4. Seeded replay diff (behavioural changes only)

If the change affects behaviour/learning:

```bash
./build/bin/eidolon-sim --seed 42 --deterministic --data data/runs/replay --days 1
git stash --include-untracked   # or note the pre-change commit
# run the same replay on the previous state, then:
diff <(git show <prev_commit>:<path>/events.log 2>/dev/null) data/runs/replay/events.log
```

Deterministic runs must reproduce bit-for-bit within the same binary. Differences across
commits must be explainable by the change (e.g. learning-rate change) — never by
uncontrolled randomness.

## 5. Commit + push

Per AGENTS.md SOP: commit with a concise imperative message, update docs in the same
commit, then `git push origin master`. Never commit secrets or data/ artifacts.

## When to run this skill

- Before committing any code change.
- After any Phase transition in ROADMAP.md (each phase has its own gate).
- Whenever the user asks for a build/test run or says "gate it".
