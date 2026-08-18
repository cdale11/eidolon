"""Behavioural evaluation of a fitted prior: run fresh, deterministic sims on held-out
seeds and compare survival / action usage with and without the prior.

Uses `eidolon-sim --dump-experiences` (one JSONL record per tick including the actual
action), so it reports precise per-action usage (events.log can't tell Wander from
Observe) and survival length without touching the C++ runtime.
"""
from __future__ import annotations

import os
import subprocess
import tempfile
from collections import Counter
from typing import Any

from .dataset import ACTION_NAMES, load_experiences


def _run_one(sim_bin: str, seed: int, days: int, prior: str | None,
             run_dir: str) -> dict[str, Any]:
    dump = os.path.join(run_dir, "dump.jsonl")
    os.makedirs(run_dir, exist_ok=True)
    cmd = [sim_bin, "--data", run_dir, "--seed", str(seed), "--deterministic",
           "--days", str(days), "--dump-experiences", dump]
    if prior:
        cmd += ["--policy-prior", prior]
    try:
        subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return {"seed": seed, "error": "sim timeout"}
    try:
        exp = load_experiences(dump)
    except Exception:
        exp = []
    if not exp:
        return {"seed": seed, "error": "no ticks (died at birth?)"}
    counts = Counter(e.action for e in exp)
    final = exp[-1]
    return {
        "seed": seed,
        "ticks": len(exp),
        "actions": {a: counts.get(a, 0) for a in ACTION_NAMES},
        "final_thirst": final.body.get("t", 0.0),
        "final_hunger": final.body.get("h", 0.0),
        "final_health": final.body.get("hp", 100.0),
        "alive_until_t": final.t,
    }


def sim_eval(sim_bin: str, configs: dict[str, str | None], seeds: list[int],
             days: int = 1, tmpdir: str | None = None) -> dict[str, Any]:
    """configs: display name -> prior .eprp path (None = random init).

    Returns per-config means across seeds + per-seed rows (survival, per-action usage,
    final body state). Deterministic (--deterministic + fixed seeds) so it is stable
    across runs; each sim gets its own run dir.
    """
    out: dict[str, Any] = {}
    for name, prior in configs.items():
        rows = []
        base = os.path.join(tmpdir or tempfile.mkdtemp(prefix="eidolon_eval_"), name)
        for s in seeds:
            r = _run_one(sim_bin, s, days, prior, os.path.join(base, str(s)))
            if "error" not in r:
                rows.append(r)
        if not rows:
            out[name] = {"error": "all seeds failed"}
            continue
        agg: dict[str, Any] = {"seeds": len(rows)}
        for k in ("ticks", "final_thirst", "final_hunger", "final_health", "alive_until_t"):
            agg[k] = round(sum(r[k] for r in rows) / len(rows), 1)
        agg["actions"] = {a: int(sum(r["actions"][a] for r in rows))
                          for a in ACTION_NAMES}
        agg["non_agentic_actions"] = sum(agg["actions"][a]
                                         for a in ("Wander", "Observe"))
        agg["survived"] = sum(1 for r in rows if r["alive_until_t"] >= 86400 * days - 60)
        out[name] = agg
    return out