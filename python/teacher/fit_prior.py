"""Fit a softmax-linear policy prior from (features, teacher-label) pairs.

The fitted weights are exported as an .eprp binary that Policy::loadPrior ingests:
  4-byte magic "EPRP", u32 version, u32 nFeatures, u32 nActions, then
  nActions*(nFeatures+1) float32 row-major (bias appended after the feature weights),
  matching src/mind/policy.hpp exactly.

Version history (matches the C++ side, src/mind/policy.cpp::loadPrior):
  v1 = legacy layout (nFeatures, nActions 4-bit-quantised to a single u32? — no, just
       plain two u32 fields; the schema is identical to v2 in raw bytes — the version
       bump only exists so the loader can refuse priors baked against a stale
       feature/action layout). All priors baked against a different (nFeatures, nActions)
       pair must use the matching version, and the loader must reject mismatches.
  v2 = current schema. Identical byte layout to v1; the bump is a marker that the file
       was produced by a teacher pipeline that knew the feature-vector count and action
       count (45 / 12) at bake time, so a stale (43 / 6) prior is refused even though
       the bytes parse. New priors always write v2.

Use `current_prior_version()` to read the version we write today, and
`expected_prior_bytes(nf, na)` to compute the exact on-disk size for tests / sanity
checks. Centralising these constants keeps the test assertions from drifting again when
the feature vector grows.
"""
from __future__ import annotations

import struct
from typing import Any

import numpy as np
import torch
import torch.nn as nn

from .dataset import ACTION_NAMES, N_FEATURES

# Magic, schema version, and the (nFeatures, nActions) the writer bakes for. The writer
# always uses (N_FEATURES, len(ACTION_NAMES)) today; bump PRIOR_VERSION and update the
# C++ loader's accepted-version list whenever the feature vector or action set changes.
PRIOR_MAGIC = b"EPRP"
PRIOR_VERSION = 2
PRIOR_N_FEATURES = N_FEATURES
PRIOR_N_ACTIONS = len(ACTION_NAMES)


def expected_prior_bytes(n_features: int = PRIOR_N_FEATURES,
                          n_actions: int = PRIOR_N_ACTIONS) -> int:
    """Exact on-disk size of a .eprp prior: 4 magic + 12 header + nA*(nF+1) f32."""
    return 4 + 12 + n_actions * (n_features + 1) * 4


def _tensors(X: np.ndarray, y: np.ndarray, val_frac: float):
    n = len(y)
    n_val = int(n * val_frac) if val_frac > 0 else 0
    if n_val:
        idx = np.arange(n)
        rng = np.random.default_rng(0)
        rng.shuffle(idx)
        train, val = idx[n_val:], idx[:n_val]
    else:
        train, val = np.arange(n), np.arange(0)
    Xt = torch.from_numpy(X[train]).float()
    yt = torch.from_numpy(y[train]).long()
    Xv = torch.from_numpy(X[val]).float() if len(val) else None
    yv = torch.from_numpy(y[val]).long() if len(val) else None
    return Xt, yt, Xv, yv


def fit_prior(X: np.ndarray, y: np.ndarray, epochs: int = 400, lr: float = 0.05,
              val_frac: float = 0.1, seed: int = 0,
              on_epoch=None) -> dict[str, Any]:
    """Linear-softmax fit. Returns weights (np.float32 (nActions, nFeatures)), bias, acc.

    `on_epoch(epoch, epochs, loss, acc, val_acc)`, if given, is called every epoch so a
    caller can stream live training progress (web UI). acc/val_acc are computed on a
    periodic subsample every `report_every` epochs to keep the loop cheap.
    """
    if X.shape[1] != N_FEATURES:
        raise ValueError(f"expected {N_FEATURES} features, got {X.shape[1]}")
    torch.manual_seed(seed)
    model = nn.Linear(N_FEATURES, len(ACTION_NAMES), bias=True)
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    loss_fn = nn.CrossEntropyLoss()
    Xt, yt, Xv, yv = _tensors(X, y, val_frac)
    last = None
    report_every = max(1, epochs // 40)  # ~40 live updates over the run
    for epoch in range(epochs):
        model.train()
        opt.zero_grad()
        logits = model(Xt)
        loss = loss_fn(logits, yt)
        loss.backward()
        opt.step()
        last = float(loss.detach())
        if on_epoch is not None and (epoch + 1) % report_every == 0:
            with torch.no_grad():
                acc = float((model(Xt).argmax(1) == yt).float().mean())
                val_acc = (float((model(Xv).argmax(1) == yv).float().mean())
                           if Xv is not None else None)
            on_epoch(epoch + 1, epochs, last, acc, val_acc)
    model.eval()
    with torch.no_grad():
        acc = float((model(Xt).argmax(1) == yt).float().mean())
        val_acc = float((model(Xv).argmax(1) == yv).float().mean()) if Xv is not None else None
    W = model.weight.detach().numpy().astype(np.float32)
    b = model.bias.detach().numpy().astype(np.float32)
    return {"weights": W, "bias": b, "acc": acc, "val_acc": val_acc, "loss": last}


def write_prior(path: str, weights: np.ndarray, bias: np.ndarray,
                version: int = PRIOR_VERSION) -> None:
    """Write an .eprp prior at `version` (default = PRIOR_VERSION = current schema).

    Bumping the version on every schema break is what tells the C++ loader to reject
    priors baked against a stale feature/action layout (see Policy::loadPrior).
    """
    na, nf = weights.shape
    if bias.shape != (na,):
        raise ValueError("bias shape mismatch")
    with open(path, "wb") as f:
        f.write(PRIOR_MAGIC)
        f.write(struct.pack("<III", version, nf, na))
        rows = np.concatenate([weights, bias[:, None]], axis=1).reshape(-1)
        f.write(rows.astype("<f4").tobytes())


def read_prior(path: str) -> tuple[np.ndarray, np.ndarray]:
    with open(path, "rb") as f:
        if f.read(4) != PRIOR_MAGIC:
            raise ValueError("bad magic")
        version, nf, na = struct.unpack("<III", f.read(12))
        if version != PRIOR_VERSION:
            raise ValueError(f"unsupported prior version {version} "
                             f"(expected {PRIOR_VERSION})")
        raw = np.frombuffer(f.read(), dtype="<f4")
        if raw.size != na * (nf + 1):
            raise ValueError("truncated prior")
        rows = raw.reshape(na, nf + 1)
        return rows[:, :nf].copy(), rows[:, nf].copy()