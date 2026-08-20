"""Dataset loading for experience dumps produced by `eidolon-sim --dump-experiences`."""
from __future__ import annotations

import json
from typing import Any, Iterator

import numpy as np

# Policy action index layout, must match src/mind/policy.hpp (kActions + order).
ACTION_NAMES: list[str] = ["Forage", "Drink", "Rest", "Wander", "Observe", "Flee"]
ACTION_INDEX: dict[str, int] = {name: i for i, name in enumerate(ACTION_NAMES)}

# Feature-vector length, must match src/mind/learn.hpp (kFeatures).
N_FEATURES = 43


class Experience:
    __slots__ = ("t", "agentic", "action", "reward", "novelty", "threat", "aversive",
                 "safe", "body", "wx", "bush_dist", "water_dist", "eaten", "drank",
                 "feats", "action_index", "teacher_label_index",
                 "value", "temperature", "scores", "probs", "neuromod", "personality",
                 "drives", "life", "metrics", "attention")

    def __init__(self, rec: dict[str, Any]):
        self.t: int = int(rec["t"])
        self.agentic: bool = bool(rec.get("agentic", 1))
        self.action: str = rec["action"]
        self.reward: float = float(rec.get("reward", 0.0))
        self.novelty: float = float(rec.get("novelty", 0.0))
        self.threat: float = float(rec.get("threat", 0.0))
        self.aversive: bool = bool(rec.get("aversive", 0))
        self.safe: bool = bool(rec.get("safe", 1))
        self.body: dict[str, float] = rec.get("body", {})
        self.wx: dict[str, Any] = rec.get("wx", {})
        self.bush_dist: int = int(rec.get("bushDist", -1))
        self.water_dist: int = int(rec.get("waterDist", -1))
        self.eaten: float = float(rec.get("eaten", 0.0))
        self.drank: bool = bool(rec.get("drank", 0))
        feats = rec.get("feats")
        if not feats or len(feats) != N_FEATURES:
            raise ValueError(f"record t={self.t}: expected {N_FEATURES} features, got "
                             f"{len(feats) if feats else 0}")
        self.feats: np.ndarray = np.asarray(feats, dtype=np.float32)
        if self.action not in ACTION_INDEX:
            raise ValueError(f"record t={self.t}: unknown action '{self.action}'")
        self.action_index: int = ACTION_INDEX[self.action]
        tl = rec.get("teacherLabel")
        if tl is not None:
            if tl not in ACTION_INDEX:
                raise ValueError(f"record t={self.t}: unknown teacher label '{tl}'")
            self.teacher_label_index: int | None = ACTION_INDEX[tl]
        else:
            self.teacher_label_index = None
        # Extended training-data fields for ALL live learning systems (ValueNet, policy
        # bandit, ThreatNet, Attention, neuromodulators, personality latent, drives, life
        # stats, learner metrics). Present only in dumps from the current runtime.
        self.value: float = float(rec.get("value", 0.0))
        self.temperature: float = float(rec.get("temp", 0.5))
        self.scores: np.ndarray | None = (np.asarray(rec["scores"], dtype=np.float32)
                                          if "scores" in rec else None)
        self.probs: np.ndarray | None = (np.asarray(rec["probs"], dtype=np.float32)
                                         if "probs" in rec else None)
        self.neuromod: dict[str, float] = rec.get("neuromod", {})
        self.personality: np.ndarray | None = (np.asarray(rec["personality"], dtype=np.float32)
                                               if "personality" in rec else None)
        self.drives: dict[str, float] = rec.get("drives", {})
        self.life: dict[str, float] = rec.get("life", {})
        self.metrics: dict[str, list[int]] = rec.get("metrics", {})
        self.attention: np.ndarray | None = (np.asarray(rec["attention"], dtype=np.float32)
                                             if "attention" in rec else None)

    def interpretable_text(self) -> str:
        b = self.body
        return (
            f"time tick {self.t}; state: hunger={b.get('h', 0):.0f}/100, "
            f"thirst={b.get('t', 0):.0f}/100, fatigue={b.get('f', 0):.0f}/100, "
            f"energy={b.get('e', 0):.0f}/100, health={b.get('hp', 100):.0f}, "
            f"pain={b.get('p', 0):.0f}, sleep-pressure={b.get('s', 0):.0f}, "
            f"body-temp={b.get('temp', 36.6):.1f}C; weather: {self.wx.get('desc', 'clear')} "
            f"at {self.wx.get('tempC', 0):.1f}C; nearest berry bush {self.bush_dist} tiles, "
            f"nearest water {self.water_dist} tiles; threat={self.threat:.2f} "
            f"({'predator nearby' if self.threat >= 0.6 else 'calm'})"
        )


def load_experiences(path: str, max_records: int | None = None,
                     sample: float | None = None) -> list[Experience]:
    """Load a JSONL dump into validated Experience records.

    sample in (0,1] subsamples deterministically (hash-based) so huge dumps stay tractable.
    """
    out: list[Experience] = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if sample is not None and sample < 1.0:
                key = hash((rec.get("t", 0), rec.get("feats", ())[0] if rec.get("feats") else 0))
                if (key % 1000) / 1000.0 >= sample:
                    continue
            out.append(Experience(rec))
            if max_records is not None and len(out) >= max_records:
                break
    return out


def feature_matrix(exp: list[Experience]) -> np.ndarray:
    return np.stack([e.feats for e in exp], axis=0)


def labels(exp: list[Experience]) -> np.ndarray:
    return np.asarray([e.action_index for e in exp], dtype=np.int64)


def reward_by_action(exp: list[Experience]) -> np.ndarray:
    """Mean reward observed for each action across the dump (offline wisdom heuristic)."""
    sums = np.zeros(len(ACTION_NAMES), dtype=np.float64)
    counts = np.zeros(len(ACTION_NAMES), dtype=np.int64)
    for e in exp:
        sums[e.action_index] += e.reward
        counts[e.action_index] += 1
    return np.divide(sums, np.maximum(counts, 1))


def reward_best_labels(exp: list[Experience]) -> np.ndarray:
    """Label each record with the action that had the best mean reward in the dump.

    Used as the default offline teacher signal when no live teacher is configured, or as a
    fallback when the teacher endpoint is unreachable.
    """
    means = reward_by_action(exp)
    best = int(np.argmax(means))
    return np.full(len(exp), best, dtype=np.int64)