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
            "results": {},  # filled after fitting: quality report + sim evaluation
            # Live training-progress fields (filled during the fit loop):
            "fit": None,  # {epoch, epochs, loss, acc, val_acc, last_epoch}
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

    def set_results(self, results: dict) -> None:
        with self._lock:
            self._s["results"] = results
            self._s["stage"] = "done"

    def fit_tick(self, epoch: int, epochs: int, loss: float,
                 acc: float | None = None, val_acc: float | None = None) -> None:
        """Report live training (fit-loop) progress. Called once per epoch (or every few
        epochs) from the fitting thread."""
        with self._lock:
            self._s["stage"] = "fitting"
            fit = self._s["fit"] or {}
            if fit.get("fit_started") is None:
                fit["fit_started"] = time.monotonic()
            curve = list(fit.get("loss_curve") or [])
            if not curve or curve[-1] != round(float(loss), 4):
                curve.append(round(float(loss), 4))
            self._s["fit"] = {
                **fit,
                "epoch": epoch,
                "epochs": epochs,
                "loss": loss,
                "acc": acc,
                "val_acc": val_acc,
                "loss_curve": curve[-200:],
            }

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
            fit = s.get("fit")
            if fit:
                fit = dict(fit)
                started = fit.get("fit_started")
                per_epoch = None
                if started:
                    per_epoch = (time.monotonic() - started) / max(fit["epoch"], 1)
                fit["fit_eta_seconds"] = (
                    round(per_epoch * (fit["epochs"] - fit["epoch"]), 1)
                    if per_epoch is not None else None)
                fit["fit_percent"] = round(100.0 * fit["epoch"] / fit["epochs"], 1)
                s["fit"] = fit
            return s