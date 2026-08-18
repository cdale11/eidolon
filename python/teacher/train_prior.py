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
    ap.add_argument("--labels-out", default=None,
                    help="write the labels produced by this run as {t, label} JSONL "
                         "(the generated dataset; reusable via --labels)")
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
    ap.add_argument("--progress-host", default="0.0.0.0",
                    help="bind address for the progress UI (0.0.0.0 = reachable on the LAN)")
    ap.add_argument("--keep-server", action="store_true",
                    help="keep the progress/results web UI running after the fit (until "
                         "Ctrl-C) so the results can be inspected in the browser")
    ap.add_argument("--compare-labels", default=None,
                    help="optional second teacher's {t, label} JSONL to compute "
                         "cross-teacher agreement on overlapping records")
    ap.add_argument("--eval-seeds", default="",
                    help="comma-separated held-out seeds for behavioural evaluation "
                         "(fresh deterministic sims, this prior vs random init)")
    ap.add_argument("--eval-days", type=int, default=1,
                    help="days per behavioural-evaluation sim")
    ap.add_argument("--eval-compare", default="",
                    help="additional priors to compare against in the behavioural eval, "
                         "as name=path comma list (e.g. local4b=data/priors/teacher_policy.eprp)")
    ap.add_argument("--sim-bin", default=None,
                    help="path to the eidolon-sim binary (default: ./build/bin/eidolon-sim)")
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
    fallback_count = 0

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
        label_writer = None
        if args.labels_out:
            lf = open(args.labels_out, "w")
            label_writer = lambda t, lab: (lf.write('{"t":%d,"label":"%s"}\n' % (t, lab)),
                                           lf.flush())[1]
        counters: dict = {}
        try:
            labels_used = label_experiences(exp, client=client,
                                            fallback=("reward" if use_teacher else args.label_mode),
                                            progress=progress, on_label=label_writer,
                                            counters=counters)
        finally:
            if args.labels_out:
                lf.close()
        fallback_count = counters.get("fallback", 0)
        label_idx = [ACTION_NAMES.index(a) for a in labels_used]
        if args.labels_out:
            print(f"  labels written to {args.labels_out}")
        if progress:
            progress.stage("fitting")

    X = feature_matrix(exp)
    y = __import__("numpy").asarray(label_idx, dtype="int64")
    print(f"fitting softmax-linear prior ({X.shape[0]} x {X.shape[1]}) ...")
    res = fit_prior(X, y, epochs=args.epochs, lr=args.lr, val_frac=args.val_frac)
    write_prior(args.out, res["weights"], res["bias"])
    val = f"  val-acc={res['val_acc']:.3f}" if res["val_acc"] is not None else ""
    print(f"prior written to {args.out}")
    print(f"  train-acc={res['acc']:.3f}{val} loss={res['loss']:.4f}")

    # Quality report (fit + data sanity) exposed through the progress web UI.
    other = None
    if args.compare_labels:
        other = {}
        with open(args.compare_labels) as f:
            for line in f:
                rec = __import__("json").loads(line)
                other[int(rec["t"])] = rec["label"]
    from .quality import quality_report

    report = quality_report(
        exp, label_idx, fallback_count=fallback_count,
        fit_res={k: res[k] for k in ("acc", "val_acc", "loss")},
        artifact=args.out, other_labels=other)

    # Behavioural evaluation: fresh deterministic sims on held-out seeds, with and
    # without the prior. Reported through the progress web UI as `sim_eval`.
    if args.eval_seeds:
        from .eval import sim_eval

        seeds = [int(s) for s in args.eval_seeds.replace(" ", "").split(",") if s]
        sim_bin = args.sim_bin or os.path.join(
            os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
            "build", "bin", "eidolon-sim")
        if not os.path.exists(sim_bin):
            print(f"  warning: sim binary {sim_bin} missing; skipping behavioural eval")
        else:
            print(f"evaluating prior on seeds {seeds} ({args.eval_days} day(s)) ...")
            configs: dict = {"random init": None, "this prior": args.out}
            for kv in args.eval_compare.replace(" ", "").split(","):
                if "=" in kv:
                    name, path = kv.split("=", 1)
                    configs[name] = path
            ev = sim_eval(sim_bin, configs, seeds, days=args.eval_days)
            report["sim_eval"] = ev
            for name, agg in ev.items():
                print(f"  {name}: ticks={agg.get('ticks')} "
                      f"actions={agg.get('actions')} "
                      f"non-agentic(Wander/Observe)={agg.get('non_agentic_actions')} "
                      f"survived={agg.get('survived')}/{len(seeds)}")

    if progress:
        progress.set_results(report)
    if server and args.keep_server:
        print(f"  results live at {server.url()}  (Ctrl-C to exit)")
        try:
            while True:
                __import__("time").sleep(3600)
        except KeyboardInterrupt:
            pass
    if server:
        server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())