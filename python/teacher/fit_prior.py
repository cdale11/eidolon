"""Fit a softmax-linear policy prior from (features, teacher-label) pairs.

The fitted weights are exported as an .eprp binary that Policy::loadPrior ingests:
  4-byte magic "EPRP", u32 version=1, u32 nFeatures, u32 nActions, then
  nActions*(nFeatures+1) float32 row-major (bias appended after the feature weights),
  matching src/mind/policy.hpp exactly.
"""
from __future__ import annotations

import struct
from typing import Any

import numpy as np
import torch
import torch.nn as nn

from .dataset import ACTION_NAMES, N_FEATURES


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
              val_frac: float = 0.1, seed: int = 0) -> dict[str, Any]:
    """Linear-softmax fit. Returns weights (np.float32 (nActions, nFeatures)), bias, acc."""
    if X.shape[1] != N_FEATURES:
        raise ValueError(f"expected {N_FEATURES} features, got {X.shape[1]}")
    torch.manual_seed(seed)
    model = nn.Linear(N_FEATURES, len(ACTION_NAMES), bias=True)
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    loss_fn = nn.CrossEntropyLoss()
    Xt, yt, Xv, yv = _tensors(X, y, val_frac)
    last = None
    for epoch in range(epochs):
        model.train()
        opt.zero_grad()
        logits = model(Xt)
        loss = loss_fn(logits, yt)
        loss.backward()
        opt.step()
        last = float(loss.detach())
    model.eval()
    with torch.no_grad():
        acc = float((model(Xt).argmax(1) == yt).float().mean())
        val_acc = float((model(Xv).argmax(1) == yv).float().mean()) if Xv is not None else None
    W = model.weight.detach().numpy().astype(np.float32)
    b = model.bias.detach().numpy().astype(np.float32)
    return {"weights": W, "bias": b, "acc": acc, "val_acc": val_acc, "loss": last}


def write_prior(path: str, weights: np.ndarray, bias: np.ndarray) -> None:
    na, nf = weights.shape
    if bias.shape != (na,):
        raise ValueError("bias shape mismatch")
    with open(path, "wb") as f:
        f.write(b"EPRP")
        f.write(struct.pack("<III", 1, nf, na))
        rows = np.concatenate([weights, bias[:, None]], axis=1).reshape(-1)
        f.write(rows.astype("<f4").tobytes())


def read_prior(path: str) -> tuple[np.ndarray, np.ndarray]:
    with open(path, "rb") as f:
        if f.read(4) != b"EPRP":
            raise ValueError("bad magic")
        version, nf, na = struct.unpack("<III", f.read(12))
        if version != 1:
            raise ValueError(f"unsupported prior version {version}")
        raw = np.frombuffer(f.read(), dtype="<f4")
        if raw.size != na * (nf + 1):
            raise ValueError("truncated prior")
        rows = raw.reshape(na, nf + 1)
        return rows[:, :nf].copy(), rows[:, nf].copy()