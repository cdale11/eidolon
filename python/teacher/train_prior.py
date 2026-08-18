#!/usr/bin/env python3
"""Train a teacher-baked policy prior (.eprp) from an experience dump.

Usage:
  python -m teacher.train_prior --dump exp.jsonl --out prior.eprp [options]

The fitted prior seeds a fresh organism's policy (eidolon-sim --policy-prior). Online
learning always continues on top of it; the artifact is frozen so replays stay
deterministic. Teacher endpoints speak the OpenAI-compatible chat API (local llama-server
by default; NVIDIA NIM via EIDOLON_TEACHER_* env vars). With no reachable teacher, the
offline reward heuristic is used so the pipeline works fully without an LLM.
"""
from __future__ import annotations

import argparse
import sys

from .dataset import ACTION_NAMES, feature_matrix, labels, load_experiences
from .fit_prior import fit_prior, write_prior
from .label import TeacherClient, label_experiences


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dump", required=True, help="experience dump (JSONL from eidolon-sim)")
    ap.add_argument("--out", required=True, help="output .eprp prior path")
    ap.add_argument("--max-records", type=int, default=None,
                    help="cap records read from the dump (default: all)")
    ap.add_argument("--sample", type=float, default=None,
                    help="deterministic subsample fraction in (0,1)")
    ap.add_argument("--label-mode", choices=["teacher", "reward", "self"],
                    default="teacher",
                    help="label source; 'teacher' uses the live endpoint if reachable, "
                         "falling back to 'reward'")
    ap.add_argument("--teacher-base", default=None, help="OpenAI-compatible base URL")
    ap.add_argument("--teacher-model", default=None, help="model id/name")
    ap.add_argument("--epochs", type=int, default=400)
    ap.add_argument("--lr", type=float, default=0.05)
    ap.add_argument("--val-frac", type=float, default=0.1,
                    help="held-out fraction for accuracy reporting")
    args = ap.parse_args(argv)

    print(f"loading dump {args.dump} ...")
    exp = load_experiences(args.dump, max_records=args.max_records, sample=args.sample)
    if not exp:
        print("error: empty dump", file=sys.stderr)
        return 1
    print(f"  {len(exp)} records")

    use_teacher = args.label_mode == "teacher"
    client = TeacherClient(args.teacher_base, args.teacher_model) if use_teacher else None
    labels_used = label_experiences(exp, client=client,
                                    fallback=("reward" if use_teacher else args.label_mode))
    label_idx = [ACTION_NAMES.index(a) for a in labels_used]

    X = feature_matrix(exp)
    y = __import__("numpy").asarray(label_idx, dtype="int64")
    print(f"fitting softmax-linear prior ({X.shape[0]} x {X.shape[1]}) ...")
    res = fit_prior(X, y, epochs=args.epochs, lr=args.lr, val_frac=args.val_frac)
    write_prior(args.out, res["weights"], res["bias"])
    val = f"  val-acc={res['val_acc']:.3f}" if res["val_acc"] is not None else ""
    print(f"prior written to {args.out}")
    print(f"  train-acc={res['acc']:.3f}{val} loss={res['loss']:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())