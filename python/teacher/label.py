"""Teacher client: labels experience records with a "wisdom" action.

Speaks the OpenAI-compatible /v1/chat/completions protocol so any endpoint works:
the default is the local llama.cpp llama-server (AGENTS.md §2), and a larger model
(e.g. NVIDIA NIM) is selected via EIDOLON_TEACHER_BASE / EIDOLON_TEACHER_MODEL /
EIDOLON_TEACHER_KEY. The teacher is never part of the C++ runtime; its only output is
the frozen .eprp prior artifact.
"""
from __future__ import annotations

import json
import os
import re
from typing import Any

import requests

from .dataset import ACTION_NAMES

SYSTEM_PROMPT = (
    "You are a wise mentor teaching a small autonomous organism how to survive in a "
    "simple 2D world. The organism is always hungry, thirsty, and tired; berries are its "
    "only food and water its only drink. Its only actions are: Forage (search for and eat "
    "berries), Drink (find water and drink), Rest (recover energy, does not consume food "
    "or water), Wander (move to find food/water), Observe (look around, reduces "
    "uncertainty). Pick the single wisest action for the CURRENT situation. Reason "
    "briefly if you must, but end your answer with strict JSON on its own line: "
    '{"action": "Forage"} where the value is exactly one of the five action names.'
)


def _action_from_content(content: Any) -> str | None:
    """Extract the action name from a possibly reasoning-wrapped assistant message.

    Reasoning models (DeepSeek, Nemotron, …) put a long chain-of-thought in
    `reasoning_content` and/or at the start of `content`; the final JSON answer is the
    last `{"action": ...}` in `content`. We never parse the chain-of-thought as the
    answer — only the final structured payload.
    """
    if content is None:
        return None
    if isinstance(content, list):  # OpenAI content parts
        content = "".join(
            p.get("text", "") if isinstance(p, dict) else str(p) for p in content
        )
    if not isinstance(content, str):
        return None
    last = None
    for m in re.finditer(r'"action"\s*:\s*"(\w+)"', content):
        last = m
    if last is None:
        return None
    action = last.group(1)
    return action if action in ACTION_NAMES else None


class TeacherClient:
    def __init__(self, base: str | None = None, model: str | None = None,
                 key: str | None = None, timeout: float = 30.0):
        self.base = (base or os.environ.get("EIDOLON_TEACHER_BASE")
                     or "http://127.0.0.1:8080/v1").rstrip("/")
        self.model = (model or os.environ.get("EIDOLON_TEACHER_MODEL")
                      or "Qwen3-4B-Instruct-Q4_K_M.gguf")
        self.key = key or os.environ.get("EIDOLON_TEACHER_KEY") or ""
        self.timeout = timeout

    def label(self, context: str) -> str | None:
        """Return the teacher's chosen action name, or None if the endpoint is unavailable
        or the message contains no parseable final answer."""
        headers = {"Content-Type": "application/json"}
        if self.key:
            headers["Authorization"] = f"Bearer {self.key}"
        payload = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": f"Situation: {context}. Which action is wisest?"},
            ],
            "temperature": 0.2,
            "max_tokens": 256,
            "response_format": {"type": "json_object"},
        }
        try:
            r = requests.post(f"{self.base}/chat/completions", json=payload,
                              headers=headers, timeout=self.timeout)
            r.raise_for_status()
            msg = r.json()["choices"][0]["message"]
        except (requests.RequestException, KeyError, IndexError, ValueError):
            return None
        # `reasoning_content` (chain-of-thought) is deliberately ignored; the label comes
        # only from the final `content`, which reasoning models populate after thinking.
        return _action_from_content(msg.get("content"))


def label_experiences(exp: list[Any], client: TeacherClient | None = None,
                      fallback: str = "reward", report_every: int = 500) -> list[str]:
    """Label each record; unavailable/absent teacher falls back to the offline heuristic.

    fallback: 'reward' = action with best mean observed reward; 'self' = the action the
    organism actually took.
    """
    from .dataset import reward_best_labels

    if fallback not in ("reward", "self"):
        raise ValueError(f"unknown fallback '{fallback}'")

    labels: list[str] = []
    fallback_used = 0
    reward_fallback: int | None = None
    for i, e in enumerate(exp):
        label: str | None = None
        if client is not None:
            label = client.label(e.interpretable_text())
            if label is not None and i % report_every == 0:
                print(f"  teacher: t={e.t} -> {label}")
        if label is None:
            fallback_used += 1
            if fallback == "reward":
                if reward_fallback is None:
                    reward_fallback = int(reward_best_labels(exp)[0])
                label = ACTION_NAMES[reward_fallback]
            else:
                label = e.action
        labels.append(label)
    if client is not None and fallback_used:
        print(f"  teacher: {fallback_used}/{len(exp)} records fell back to '{fallback}'")
    return labels