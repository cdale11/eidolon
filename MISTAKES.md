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
