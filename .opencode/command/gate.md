---
description: Run the full mandatory gate (build, unit tests, integration tests, autonomous smoke run, event-log inspection, commit + push). Use when finishing a step or when asked to verify the build.
agent: build
---

Run the complete Eidolon gate (load the `phase-gate` skill and follow it end to end):

1. Configure and build Release with `-Wall -Wextra` via ninja; fix every warning.
2. Run the C++ unit tests and the Python integration drivers (conda env `eidolon`).
3. Run an autonomous 1-day smoke simulation into `data/runs/check` and inspect
   `events.log` for anomalies (impossible physiology, illegal actions, frozen or
   exploding learning signals, unbounded memory growth, goal churn, clock issues).
4. If the change is behavioural, run a seeded deterministic replay and diff against the
   previous state's log.
5. If the gate is green, commit with a concise imperative message (docs updated in the
   same commit) and push to origin master.

$ARGUMENTS
