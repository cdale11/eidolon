"""Offline quality report for a generated teacher-label dataset (and the fit prior).

Checks, all from the experience dump alone (no LLM):
  - per-action label counts & skew
  - agreement of teacher labels with the organism's own actions and with the
    reward-based offline heuristic
  - drive sanity: labels should track the organism's actual needs (e.g. Drink-labeled
    records show high thirst, close water; Forage-labeled show high hunger, close bushes)
  - (optional) agreement with a second teacher's labels on overlapping records
  - fit results (train/val accuracy, loss) and artifact info when provided
"""
from __future__ import annotations

import os
from typing import Any, Iterable

from .dataset import ACTION_NAMES, reward_best_labels


def _mean(xs: Iterable[float]) -> float:
    xs = list(xs)
    return sum(xs) / len(xs) if xs else 0.0


def quality_report(exp: list[Any], label_idx: list[int],
                   fallback_count: int = 0, fit_res: dict | None = None,
                   artifact: str | None = None,
                   other_labels: dict[int, str] | None = None) -> dict:
    n = len(exp)
    if n == 0:
        return {"error": "no records"}
    labels = [ACTION_NAMES[i] for i in label_idx]

    counts = {a: 0 for a in ACTION_NAMES}
    for lab in labels:
        counts[lab] += 1

    # agreement with the organism's actual actions
    agree_self = _mean(1.0 if lab == e.action else 0.0
                       for lab, e in zip(labels, exp))
    # agreement with the reward-based offline heuristic
    rb = reward_best_labels(exp)
    agree_reward = _mean(1.0 if labels[i] == rb[i] else 0.0 for i in range(n))

    def stats() -> dict:
        """mean body state + distances among records carrying each label."""
        feats = {a: [] for a in ACTION_NAMES}
        for e, lab in zip(exp, labels):
            feats[lab].append(e)
        out = {}
        for a in ACTION_NAMES:
            recs = feats[a]
            if not recs:
                out[a] = {"n": 0}
                continue
            out[a] = {
                "n": len(recs),
                "thirst": round(_mean(r.body.get("t", 0.0) for r in recs), 2),
                "hunger": round(_mean(r.body.get("h", 0.0) for r in recs), 2),
                "fatigue": round(_mean(r.body.get("f", 0.0) for r in recs), 2),
                "bush_dist": round(_mean(r.bush_dist for r in recs), 2),
                "water_dist": round(_mean(r.water_dist for r in recs), 2),
            }
        return out

    per_label = stats()
    base = {
        "thirst": round(_mean(e.body.get("t", 0.0) for e in exp), 2),
        "hunger": round(_mean(e.body.get("h", 0.0) for e in exp), 2),
        "fatigue": round(_mean(e.body.get("f", 0.0) for e in exp), 2),
        "bush_dist": round(_mean(e.bush_dist for e in exp), 2),
        "water_dist": round(_mean(e.water_dist for e in exp), 2),
    }

    report: dict[str, Any] = {
        "n": n,
        "label_counts": counts,
        "agree_self": round(agree_self, 3),
        "agree_reward_heuristic": round(agree_reward, 3),
        "fallback_count": fallback_count,
        "per_label_state": per_label,
        "overall_means": base,
    }
    if other_labels:
        both = [(labels[i], other_labels[e.t]) for i, e in enumerate(exp) if e.t in other_labels]
        if both:
            report["agree_other_teacher"] = round(
                _mean(1.0 if a == b else 0.0 for a, b in both), 3)
            report["other_teacher_n"] = len(both)
    if fit_res:
        report["fit"] = fit_res
    if artifact and os.path.exists(artifact):
        report["artifact"] = {
            "path": artifact,
            "size_bytes": os.path.getsize(artifact),
        }
    return report
