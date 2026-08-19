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
