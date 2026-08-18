"""Thread-safe progress state for dataset generation (labeling runs).

A single labeling process updates this from its worker thread while a tiny HTTP server
(python/teacher/progress_server.py) serves snapshots to the user's browser.
"""
from __future__ import annotations

import threading
import time
from typing import Any


class ProgressState:
    def __init__(self):
        self._lock = threading.Lock()
        self._s = {
            "stage": "idle",
            "model": "",
            "total": 0,
            "done": 0,
            "fallback": 0,
            "failed": 0,
            "skipped": 0,
            "action_counts": {},
            "errors": [],
            "last_context": "",
            "last_label": "",
            "recent": [],  # last labels (small ring)
            "started": 0.0,
            "measured_rpm": 0,
            "rate_per_min": 0.0,
        }

    def start(self, stage: str, total: int, model: str = "") -> None:
        with self._lock:
            self._s["stage"] = stage
            self._s["total"] = total
            self._s["model"] = model
            self._s["done"] = 0
            self._s["fallback"] = 0
            self._s["failed"] = 0
            self._s["skipped"] = 0
            self._s["action_counts"] = {}
            self._s["errors"] = []
            self._s["recent"] = []
            self._s["started"] = time.monotonic()
            self._s["rate_per_min"] = 0.0

    def stage(self, stage: str) -> None:
        with self._lock:
            self._s["stage"] = stage
            self._s["started"] = self._s["started"] or time.monotonic()

    def tick(self, label: str | None = None, context: str = "",
             fallback: bool = False, failed: bool = False,
             measured_rpm: int | None = None) -> None:
        with self._lock:
            self._s["done"] += 1
            if failed:
                self._s["failed"] += 1
            elif label is None:
                self._s["skipped"] += 1
            else:
                self._s["action_counts"][label] = self._s["action_counts"].get(label, 0) + 1
                self._s["recent"] = (self._s["recent"] + [label])[-20:]
            if fallback:
                self._s["fallback"] += 1
            if context:
                self._s["last_context"] = context[-120:]
            self._s["last_label"] = label or ""
            if measured_rpm is not None:
                self._s["measured_rpm"] = measured_rpm
            elapsed = max(time.monotonic() - self._s["started"], 1e-6)
            self._s["rate_per_min"] = self._s["done"] * 60.0 / elapsed

    def error(self, message: str) -> None:
        with self._lock:
            self._s["errors"] = (self._s["errors"] + [message])[-5:]

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            s = dict(self._s)
            done = s["done"]
            rate = s["rate_per_min"]
            s["eta_seconds"] = round((s["total"] - done) / rate, 1) if rate > 0 and done < s["total"] else None
            s["elapsed"] = round(time.monotonic() - s["started"], 1) if s["started"] else 0.0
            s["percent"] = round(100.0 * done / s["total"], 1) if s["total"] else 0.0
            return s