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
import os
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
                    help="label source; 'teacher' uses cached 'teacherLabel' fields, else "
                         "the live endpoint if reachable, falling back to 'reward'")
    ap.add_argument("--labels", default=None,
                    help="optional JSONL of cached teacher labels {t, label} to overlay "
                         "on the dump (label once, re-fit many times without the LLM)")
    ap.add_argument("--teacher-base", default=None, help="OpenAI-compatible base URL")
    ap.add_argument("--teacher-model", default=None, help="model id/name")
    ap.add_argument("--teacher-thinking", action="store_true", default=None,
                    help="enable the model's chain-of-thought (NIM reasoning models)")
    ap.add_argument("--teacher-reasoning-budget", type=int, default=None,
                    help="max tokens the model may spend thinking")
    ap.add_argument("--rpm", type=int, default=None,
                    help="teacher request rate limit (default 25, NVIDIA NIM free tier)")
    ap.add_argument("--progress-port", type=int, default=8090,
                    help="port for the dataset-generation progress web UI (0 = off)")
    ap.add_argument("--progress-host", default="127.0.0.1")
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

    # Overlay cached teacher labels ({t, label} JSONL) if provided.
    overlay: dict[int, str] = {}
    if args.labels:
        with open(args.labels) as f:
            for line in f:
                rec = __import__("json").loads(line)
                overlay[int(rec["t"])] = rec["label"]
        print(f"  {len(overlay)} cached teacher labels overlaid")

    use_teacher = args.label_mode == "teacher"
    cached = all(e.teacher_label_index is not None or overlay.get(e.t) is not None
                 for e in exp) if use_teacher else False

    # Progress web UI (watch labeling from the browser).
    progress = None
    server = None
    if args.progress_port > 0:
        from .progress import ProgressState
        from .progress_server import ProgressServer

        progress = ProgressState()
        server = ProgressServer(progress, host=args.progress_host, port=args.progress_port)
        if server.start():
            print(f"  progress UI: {server.url()}")
        else:
            print(f"  warning: progress UI port {args.progress_port} in use; continuing "
                  "without it")
            server = None
            progress = None

    if cached:
        label_idx = [
            (e.teacher_label_index if e.teacher_label_index is not None
             else ACTION_NAMES.index(overlay[e.t])) for e in exp
        ]
        print(f"  using {len(exp)} cached teacher labels")
        if progress:
            progress.start("cached labels", len(exp), args.teacher_model or "")
            for i, e in enumerate(exp):
                progress.tick(label=ACTION_NAMES[label_idx[i]])
            progress.stage("fitting")
    else:
        model = args.teacher_model or os.environ.get("EIDOLON_TEACHER_MODEL", "")
        if progress:
            progress.start("labeling", len(exp), model)
        client = TeacherClient(args.teacher_base, args.teacher_model,
                               enable_thinking=args.teacher_thinking,
                               reasoning_budget=args.teacher_reasoning_budget,
                               rpm=args.rpm) if use_teacher else None
        labels_used = label_experiences(exp, client=client,
                                        fallback=("reward" if use_teacher else args.label_mode),
                                        progress=progress)
        label_idx = [ACTION_NAMES.index(a) for a in labels_used]
        if progress:
            progress.stage("fitting")

    X = feature_matrix(exp)
    y = __import__("numpy").asarray(label_idx, dtype="int64")
    print(f"fitting softmax-linear prior ({X.shape[0]} x {X.shape[1]}) ...")
    res = fit_prior(X, y, epochs=args.epochs, lr=args.lr, val_frac=args.val_frac)
    write_prior(args.out, res["weights"], res["bias"])
    if progress:
        progress.stage("done")
    val = f"  val-acc={res['val_acc']:.3f}" if res["val_acc"] is not None else ""
    print(f"prior written to {args.out}")
    print(f"  train-acc={res['acc']:.3f}{val} loss={res['loss']:.4f}")
    if server:
        server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())