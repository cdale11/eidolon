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
