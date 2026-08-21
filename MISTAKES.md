# MISTAKES.md — Running Log of Mistakes & Lessons

Append when a mistake costs real time. Latest entries at the bottom. Format:

```
### YYYY-MM-DD — <one-line lesson>
Context: <what we were doing>
Mistake: <what went wrong / what cost time>
Fix / rule: <what we do instead>
```

---

### 2026-08-18 — Repo was configured before documentation existed
Context: Bootstrap.
Mistake: Environment doc data (iGPU/PyTorch caveats, conda env, llama.cpp paths) lives in
AGENTS.md/DESIGN.md rather than only in conversation — a fresh agent would otherwise
re-derive or re-install wrongly.
Fix / rule: Keep environment facts and hard-won caveats in the repo docs; commit them in the
same step as the related setup.

### 2026-08-18 — gfx1103 (Radeon 740M) has no reliable ROCm PyTorch path yet
Context: Deciding whether the conda env `eidolon` should install ROCm PyTorch for teacher
training.
Mistake risk: Official ROCm wheels lack gfx1103 rocBLAS Tensile / MIOpen kernels; the common
`HSA_OVERRIDE_GFX_VERSION=11.0.0` workaround causes GPU page faults and hard hangs on this
APU. Installing ROCm PyTorch would burn time and risk system instability.
Fix / rule: Default to CPU PyTorch for all offline tooling. Treat iGPU ROCm as experimental
only (TheRock/nightly builds). Revisit when official wheels ship gfx1103 kernels.

### 2026-08-19 — Evaluation loops filled /tmp (tmpfs) with per-run dumps
Context: Behavioural eval + EA over policies run `eidolon-sim --dump-experiences` hundreds
of times; each 1-day run writes a ~5 MB dump. `tempfile.mkdtemp()` lands in /tmp, which on
this box is tmpfs (RAM-backed). 14-pop × 8-gen × 5-seed EA saturated the 3.5 GB tmpfs with
`ENOSPC`.
Mistake: No cleanup between runs; each evaluated policy left its dump (and later its run dir)
behind.
Fix / rule: `eval._run_one` parses the dump then `shutil.rmtree`s the run dir; `evolve_prior`
deletes each individual's temp `.eprp` after evaluation. Peak usage is now ~1 run dir. Any
new bulk simulation loop must clean per-iteration artifacts, and must know /tmp is tmpfs.

### 2026-08-19 — Coarse-tick run target overshoot broke resume == uninterrupted
Context: Phase 5 world change altered the tick cadence, and `test_saveload_continuity`
failed: a `--days 0.5` then `--days 0.5` run was byte-different from `--days 1.0`.
Mistake: The CLI computed the resume target as `clock.now() + days*86400`. The first stage
overshoots its 0.5-day boundary by up to one coarse tick (1/10/30 s), so the second stage
targeted 86421 instead of 86400 and logged one extra event. Baseline passed only because
the old tick cadence happened to overshoot identically in both runs — a latent bug, not a
test quirk.
Fix / rule: Persist the run's scheduled target in the snapshot (engine field, version 4);
the CLI advances that schedule instead of the overshot clock. Never derive a resume boundary
from a clock that coarse ticks may have overshot.

### 2026-08-19 — bytes() takes BYTE count, not element count — grid floats silently truncated
Context: `test_saveload_continuity` failed again after the Phase 5 noise-field worldgen.
In-process resume diverged from the uninterrupted run at the very first resumed tick, and
the learn section (Neuromod) drifted within one tick. The resumed process perceived flat
terrain (elevation/humidity = 0) while the fresh run saw real values.
Mistake: `Grid::serialize`/`deserialize` called `w.bytes(elevation_.data(), elevation_.size())`
where `size()` is the ELEMENT count. `bytes()` reads/writes BYTES, so only 4096 bytes
(= 1024 of 4096 floats) of each climate array were persisted; the tail came back as zeros.
The default-construct + loadFile resume path resized the empty vectors to 4096 zeroed
floats, read back only the first 1024, and left the rest zero — while the init()+load path
(and any process that generated the grid first) kept the real generated values, so it
matched by luck. `Grid::hash()` hashed only tiles, so `world_serialize_roundtrip` passed
despite the data loss. Root-causing cost hours of hash tracing (world serialize matched
byte-for-byte while a raw elevation hash differed — because the truncation hid in the
serialized bytes themselves).
Fix / rule: `bytes()` is byte-count only; multiply element counts by `sizeof(T)` for
non-uint8 vectors (tiles/biomes are 1-byte enums so were accidentally correct). Snapshot
version bumped to 6. Added `grid_serialize_roundtrip_full_fidelity` which element-wise
checks every tile/biome/elevation/temperature/humidity cell through a default-constructed
round-trip. Audit every `bytes(data, size())` call when a vector element is wider than one
byte, and make serialization round-trip tests compare raw element data, not a partial hash.

### 2026-08-19 — Phase 5 worldgen freqScale 0.04/0.03/0.02 all broke survival (death spiral)
Context: Phase 5 hazards (cliffs/falls) needed steep terrain, so `freqScale` was raised from 0.01 to 0.04.
Mistake: At 0.04 (and 0.03/0.02), natural elevation deltas ≥ 0.12 appear everywhere. Every descent becomes a painful fall (`kFallDamageDrop=0.12`), which raises `pain_`, which triggers `Rest`, which lowers `energy_`, which reduces foraging/drinking, which raises `hunger_`/`thirst_` — a death spiral. Seed 42 died at 11.7 days at 0.02. The problem went unnoticed because the build/tests only check determinism, not long-run survival.
Fix / rule: `freqScale` reverted to 0.01 (max adjacent delta ≈ 0.107, below `kFallDamageDrop`). Hazard tests now construct steep terrain manually via `Grid::setElevation()` (this also lets the cliff tests work deterministically at any freq). Any future change to `freqScale` must include a `--days 1 --seed 42` smoke-run survival check.

### 2026-08-19 — Phase 5 gate test `expHits < naiveHits` asserts a non-existent mechanism
Context: Designing the `phase5_gate_survival_improves_with_experience` gate test; wrote `CHECK(expHits < naiveHits)` (fewer predator hits = survival improvement).
Mistake: A committed predator (wolf `speed=3`) always closes distance faster (`net +2`/tick) than a fleeing organism (`speed=1`). With `stepWalkable()` (cliff-aware), the wolf catches and bites regardless of whether the organism flees from 12 tiles (threat veto) or 3 tiles (emergency valve). Empirical runs at seed 2024 showed 3 hits for both branches; sometimes the experienced branch even got more hits (longer chase). The assertion is unsupported by the mechanics.
Fix / rule: Redesign the survival gate to test what the mechanics actually deliver: a durable elevated `threatEstimate()` carrying into a later encounter, and a measurable defensive-behavior product — the trained organism keeps a strictly larger average distance from the predator (proactive flee at sight radius vs emergency-only). The gate now asserts `expAvgDist > naiveAvgDist` (verified empirically: ~2x distance) + `expAlive` + `threatEstimate() > 0.6f`. Never assert behavioral outcomes that the mechanics don't support.

### 2026-08-19 — Graph rewriting implementation had duplicate definitions and missing includes
Context: Adding Phase 9 branch graph rewriting system (`src/mind/graph_rewriting.hpp/.cpp`).
Mistake: The initial implementation had several issues that caused build failures: duplicate `using Match` and `match_pattern` declarations in the header; duplicate `add_rule` definition in the cpp; `apply_rules` function body was outside any function scope (missing signature); methods from `apply_rule_at_match` onward missing `GraphRewritingSystem::` prefix; missing closing brace for namespace; unused variables in `RewriteRule::deserialize` triggering warnings.
Fix / rule: When adding new C++ modules, write the header first with complete declarations, then implement each method with full qualification. Run a zero-warning build (`-Wall -Wextra`) immediately after each module. Use the phase-gate skill to catch issues before commit.

### 2026-08-19 — Policy priors incompatible with current feature set (version skew)
Context: Attempting to load teacher-baked policy priors (`.eprp` files) to fix organism death from thirst/hunger.
Mistake: The existing `.eprp` priors were generated with an older version of the codebase (27 features, 5 actions: Forage/Drink/Rest/Wander/Observe). The current codebase has 43 features and 6 actions (added Flee in Phase 5). `Policy::loadPrior()` validates feature count and action count, so all existing priors fail to load with "feature count mismatch" or "action count mismatch".
Fix / rule: Policy priors are versioned artifacts tied to the exact feature/action layout. When the feature vector or action set changes, all priors must be regenerated via the teacher pipeline (`python/teacher/train_prior.py`). Document the expected feature/action counts in the prior format spec. Consider adding backward compatibility or migration logic if version skew is expected to be common.

### 2026-08-20 — libstdc++ vs libc++ std::sort permutes equal keys differently, breaking native/WASM parity
Context: Phase 12 parity test — same seeded scenario must produce identical digests on native (g++, libstdc++) and WASM (em++, libc++). All libm transcendentals replaced with deterministic `detmath`, `-ffp-contract=off` applied, yet digests diverged from tick 1.
Mistake: `Attention::attend` used `std::sort` on 28 channels with equal saliences (`1.0f` at init). libstdc++ introsort and libc++ introsort produce different permutations of equal keys. The sorted order determines which channels get full weight (1.0) vs attenuated (0.25), so the feature vector differed → policy scores differed → learning updates diverged → full learn-state divergence (~0.1% diffs). Root-causing required bisecting to tick 1, diffing 270 KB snapshots, and decoding the serialization to find the first 9 float divergence in `neuromod_` — the sort was the only source of non-determinism in the tick path.
Fix / rule: Add deterministic tie-break to every `std::sort` in the deterministic core: `return (a.key > b.key) || (a.key == b.key && a.index < b.index)`. Audit all `std::sort` call sites in `src/` for potential tie instability (world_predictor, goal_emergence, concept_formation, voronoi, attention). Never rely on STL sort stability for equal keys when cross-platform bit-exactness is required.

### 2026-08-20 — NIM teacher labels (~6965 records, ~9.3h of API calls) lost: stored only in /tmp (tmpfs)
Context: Phase 11 teacher-baked policy prior retrain using NVIDIA NIM (nvidia/nemotron-3-super-120b-a12b)
to label an experience dataset. Run: `EIDOLON_TEACHER_BASE='https://integrate.api.nvidia.com/v1'
EIDOLON_TEACHER_MODEL='nvidia/nemotron-3-super-120b-a12b' ... python -m teacher.train_prior
--dump /tmp/opencode/nim_run/sample.jsonl --labels-out /tmp/opencode/nim_run/labels.jsonl
--out ../data/priors/teacher_policy_nim.eprp --teacher-thinking --teacher-reasoning-budget 2048
--rpm 25 --progress-port 8090`. Reached 6965/9000 labels (77.4%) at ~12.5/min in ~9.3h before a
power cut. The progress web UI (port 8090) showed it on track to finish in ~2.7 min.
Mistake: ALL expensive artifacts (the 6965 NIM labels AND the experience dumps AND the local Qwen
labels) were written exclusively to `/tmp/opencode/nim_run/`. `/tmp` on this box is a **tmpfs**
(RAM-backed, `mount | grep /tmp` → `tmpfs on /tmp type tmpfs`, size 3.5 GB). A power cut wipes tmpfs
instantly — there is no fsync durability in RAM. The power loss destroyed ~9.3h of NIM API labeling
that cannot be recovered (the labels were never copied to disk, never committed, never mirrored).
The experience dumps ARE recoverable (deterministic `eidolon-sim --dump-experiences --seed N
--deterministic` regenerates them bit-for-bit in minutes), but the NIM teacher *labels* are not —
they cost ~6h of wall-clock API time at the 25 RPM free tier to remake. A prior MISTAKES entry
(2026-08-19) already flagged /tmp as tmpfs for bulk sim loops, yet the labeling run repeated the
trap because `--labels-out` defaulted to a /tmp path and nothing enforced persistence.
Fix / rule: (1) Teacher labeling runs must write `--labels-out` and `--out` to a PERSISTENT path
under `data/priors/` (e.g. `data/priors/labels_nim.jsonl`, `data/priors/teacher_policy_nim.eprp`),
never `/tmp`. (2) Always pass `--labels-out` on live (expensive) teacher runs and `cp`/mirror the
labels file to a second persistent location periodically during long runs (or flush+vdsync is not
enough on tmpfs). (3) Keep a persistent copy of the input dump too (`data/priors/sample.jsonl`) so a
re-fit needs no regen. (4) Before any multi-hour LLM/teacher run, verify the output path is on a real
disk filesystem (`df -h <path>` — tmpfs shows `tmpfs`), and `nohup`/`setsid` the process so a shell
disconnect can't SIGTERM it. (5) Power on this box is not guaranteed — treat >1h compute as
non-recoverable unless its artifacts land on real disk before the run continues.

### 2026-08-21 — Wake threshold == sleep-entry threshold → death loop
Context: Organism found asleep at pos (0,61) with thirst=79/87, sleepPressure=16.8, a
full waterskin (5 sips) — never drank, died of thirst. `data/runs/v2c_s31`.
Mistake: `Engine::decide` woke a sleeping organism only when `thirst() > 85` (line ~410),
and *separately* blocked sleep *entry* when `thirst() < 85` (line ~420). The two thresholds
were identical, so a sleeping organism at thirst=79 satisfied neither — it neither woke nor
was prevented from re-sleeping — and just stayed asleep while thirst climbed to 100. A
safety valve at `thirst > 80` further down was unreachable because `decide` returned
`Action::Sleep` first. The bug was invisible in baseline runs (the organism rarely slept at
thirst 60–84) and only surfaced under the v2 teacher prior, which over-prioritized Sleep.
Root-cause took a full instrumented replay (33381 `actionsDrink` events with ZERO actual
drinks) because the count looked like Drink was firing — but `decide` chose Sleep *before*
ever reaching Drink.
Fix / rule: **Survival wake thresholds must sit strictly below survival block thresholds**,
otherwise the mid-band state has no escape path. Documented both numbers in DESIGN §13 and
verified 6/7 seeds survive 2 days with the v2 prior (was 3/5). When a logged action count
disagrees with a logged *event* count (e.g. `actionsDrink=33381` but 0 drink events), the
action is being chosen but its `execute()` branch is taking a no-op path — instrument
`execute()`, not `decide()`.

### 2026-08-21 — `adjacentToWater` (terrain) true but `drinkFromSource` (source list) returns 0 — no fall-through
Context: Same seed-31 strand. Organism adjacent to a water tile, called Drink thousands of
times, waterskin full — zero actual drinks. Died of thirst with `waterCarried=5`.
Mistake: `Execute(Drink)` gated the waterskin-reserve branch behind `else if
(!adjacentToWater(p))`. But `adjacentToWater` checks the **terrain** grid (Water/River
neighbours), while `drinkFromSource` searches the **`waterSources_` list** within 16 tiles
and requires `ws.current >= 1.0`. When a water tile exists but its registered source is
depleted (or further than 16 tiles — common at the world edge / a small pond that wildlife
drained), `adjacentToWater` returns true, `drinkFromSource` returns 0, the `if` branch does
nothing, and the `else if` (waterskin) is skipped entirely. The organism dies of thirst
with a full skin it was never allowed to sip.
Fix / rule: When two queries describe related-but-not-identical world state (terrain vs a
drainable source), never let a positive on the *cheaper* query suppress the *fallback*
branch behind an `else`. Refactored to a `drank_from_source` flag so a source-dry adjacent
Drink falls through to `drinkFromSkin`, and only seeks water once the skin is empty. Audit
other `adjacentTo*` / `nearest*` pairs in the engine for the same terrain-vs-instance
mismatch (forage/plant, gather/resource).

### 2026-08-21 — 1-tile random walk traps organism in corners until death
Context: Multiple seeds (1, 15, 33, 42, 71...) died at map edges with full waterskin and/or
high hunger/thirst. Organism bounced 1-tile random walks in a 4-tile zone (e.g. (90,2)
→ (88,0) → (90,2)...) for tens of thousands of ticks while energy drained to 0.
Mistake: Wander, Forage (no plant in sight), Drink (no source in hearing), and Flee
(no predator) all used single-tile random steps (`rngCognition_.irange(-1,1)`). In a
corner/edge, the return probability of a 1-step random walk is >75% per tick — the
organism effectively never escapes. Policy chose Drink/Forage 50k+ times, each tick
burning energy, but execute() just jittered in place.
Fix / rule: **Never use 1-tile random walks for exploration**. Added `exploreStep()`:
sustained 16-tick directional walk (pick random heading, hold until blocked or expired,
rotate 90° on obstacle, fall through to any walkable step). Replaces all four
single-tile jitter sites. Verified: v2 prior 2-day survival 3/5→6/7; seed 33 now
traverses >100 distinct tiles vs 5 before.
