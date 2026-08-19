"""Evolutionary search over the policy-prior weights (offline, deterministic).

The policy prior is just 6x(43+1) float weights, so we can evolve it directly with a
tiny, seedable population search instead of (or on top of) the teacher-label fit. This
is the "evolutionary algorithms" augmentation for the Eidolon offline tooling: it is
deterministic (fixed RNG seed, --deterministic sims on held-out seeds) and never runs
in the C++ runtime.

Fitness rewards living out the full run, being usefully active (foraging/drinking/
resting), and penalizes wasted movement (Wander/Observe) and behavioural fixation
(one action dominating — e.g. the Drink-fixation seen with the small NIM dataset).
Population = seeded init priors (optional) + random inits; parents by tournament;
offspring via per-weight uniform crossover + decaying gaussian mutation; elitism keeps
the best few each generation. The best prior is written as an .eprp artifact.
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

from .dataset import ACTION_NAMES, N_FEATURES
from .eval import evaluate
from .fit_prior import read_prior, write_prior

FIXATION_SHARE = 0.70  # a single action > this share of active ticks counts as fixation
WANDER_PENALTY = 8.0   # per non-agentic (Wander/Observe) tick
ACTIVE_BONUS = 1.2     # per active tick (secondary to survival)
MIN_ACTION_SHARE = 0.05  # each of Forage/Drink/Rest must stay above this share


def _fitness(row: dict, days: int) -> float:
    total = 86400 * days
    alive_until = row["alive_until_t"]
    survived = alive_until >= total - 60
    ticks = row["ticks"]
    na = row["actions"]["Wander"] + row["actions"]["Observe"]
    counts = np.asarray([row["actions"][a] for a in ACTION_NAMES], dtype=float)
    share = counts / max(ticks, 1)
    # survival is the primary objective: big bonus for living the full run, and a hard
    # penalty for dying early.
    alive_bonus = 60000.0 if survived else -30000.0 + 1500.0 * (alive_until / total)
    top_share = float(share.max())
    fixation = max(0.0, top_share - FIXATION_SHARE) * ticks * 0.4
    # a survival organism must actually forage, drink AND rest
    shortage = float(max(0.0, MIN_ACTION_SHARE - share.min())) * ticks * 2.0
    # mildly reward not neglecting forage entirely (a starvation risk signal)
    forage_share = row["actions"]["Forage"] / max(ticks, 1)
    neglect = (max(0.0, 0.05 - forage_share) / 0.05) * ticks * 0.2
    return alive_bonus + ACTIVE_BONUS * ticks - WANDER_PENALTY * na \
        - fixation - shortage - neglect


def _to_vector(weights: np.ndarray, bias: np.ndarray) -> np.ndarray:
    return np.concatenate([weights.reshape(-1), bias]).astype(np.float64)


def _from_vector(v: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    nf = N_FEATURES
    w = v[: len(ACTION_NAMES) * nf].reshape(len(ACTION_NAMES), nf).astype(np.float32)
    b = v[len(ACTION_NAMES) * nf:].astype(np.float32)
    return w, b


def _random_vec(rng: np.random.Generator) -> np.ndarray:
    w, b = rng.normal(0.0, 1.2, size=(len(ACTION_NAMES), N_FEATURES)), \
        rng.normal(0.0, 0.4, size=(len(ACTION_NAMES),))
    return _to_vector(w, b)


def evolve(sim_bin: str, seeds: list[int], days: int = 1, pop: int = 12,
           gens: int = 6, rng_seed: int = 1, inits: list[np.ndarray] | None = None,
           tmpdir: str | None = None, log=print) -> dict:
    if tmpdir:
        os.makedirs(tmpdir, exist_ok=True)
    rng = np.random.default_rng(rng_seed)
    pop_v: list[np.ndarray] = []
    if inits:
        pop_v = list(inits[: max(1, pop // 3)])
    while len(pop_v) < pop:
        pop_v.append(_random_vec(rng))

    best_vec, best_fit = None, float("-inf")
    history = []
    for gen in range(gens):
        # Evaluate the whole generation in parallel (one thread per individual; each
        # individual fans out its seeds over the remaining cores). Fitness stays
        # deterministic because sims are seeded independently of scheduling.
        from concurrent.futures import ThreadPoolExecutor

        work: list[tuple[np.ndarray, str]] = [
            (v, os.path.join(tmpdir or ".", f"gen{gen}_i{i}.eprp"))
            for i, v in enumerate(pop_v)
        ]
        for v, prior in work:
            w, b = _from_vector(v)
            write_prior(prior, w, b)

        def evaluate_one(item: tuple[np.ndarray, str]) -> tuple[float, np.ndarray]:
            v, prior = item
            # outer pool already uses all cores; keep this individual's seeds sequential
            rows = evaluate(sim_bin, prior, seeds, days=days, tmpdir=tmpdir, workers=1)
            os.remove(prior)
            return sum(_fitness(r, days) for r in rows) / max(len(rows), 1), v

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as ex:
            fits_v = list(ex.map(evaluate_one, work))
        fits = [f for f, _ in fits_v]
        for f, v in fits_v:
            if f > best_fit:
                best_fit, best_vec = f, v.copy()
        order = np.argsort(fits)[::-1]
        log(f"  gen {gen + 1}/{gens}: best fitness={fits[order[0]]:.0f} "
            f"mean={np.mean(fits):.0f}")
        history.append({"gen": gen + 1, "best": float(fits[order[0]]),
                        "mean": float(np.mean(fits))})
        if gen == gens - 1:
            break
        # elitism + crossover + mutation
        next_pop = [pop_v[order[i]].copy() for i in range(max(2, pop // 6))]
        sigma = 0.4 * (1.0 - gen / gens) + 0.05
        while len(next_pop) < pop:
            p1 = pop_v[order[rng.integers(0, min(4, pop))]]
            p2 = pop_v[order[rng.integers(0, min(4, pop))]]
            mask = rng.random(p1.size) < 0.5
            child = np.where(mask, p1, p2)
            child += rng.normal(0.0, sigma, size=child.size)
            next_pop.append(child)
        pop_v = next_pop

    return {"best_fitness": best_fit, "best": best_vec, "history": history}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, help="output .eprp prior path")
    ap.add_argument("--seeds", default="7,19,31,44,58",
                    help="held-out seeds used for evaluation")
    ap.add_argument("--days", type=int, default=1)
    ap.add_argument("--pop", type=int, default=12)
    ap.add_argument("--gens", type=int, default=6)
    ap.add_argument("--seed", type=int, default=1, help="search RNG seed (deterministic)")
    ap.add_argument("--init-priors", default="",
                    help="comma list of .eprp files to seed the population "
                         "(e.g. the current teacher artifacts)")
    ap.add_argument("--sim-bin", default=None)
    ap.add_argument("--progress-port", type=int, default=0,
                    help="serve an evaluation progress UI on this port (0 = off)")
    args = ap.parse_args(argv)

    inits = []
    for p in [x for x in args.init_priors.split(",") if x]:
        try:
            w, b = read_prior(p)
            inits.append(_to_vector(w, b))
        except Exception as ex:
            print(f"  warning: could not read init prior {p}: {ex}")
    seeds = [int(s) for s in args.seeds.replace(" ", "").split(",") if s]
    sim_bin = args.sim_bin or os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "build", "bin", "eidolon-sim")
    if not os.path.exists(sim_bin):
        print(f"error: sim binary {sim_bin} not found", file=sys.stderr)
        return 1

    progress = server = None
    if args.progress_port > 0:
        from .progress import ProgressState
        from .progress_server import ProgressServer
        progress = ProgressState()
        server = ProgressServer(progress, host="0.0.0.0", port=args.progress_port)
        if server.start():
            progress.start("evolving", pop * gens, f"ea-pop{args.pop}-gen{args.gens}")
            print(f"  progress UI: {server.url()}")

    print(f"evolving prior: pop={args.pop} gens={args.gens} seeds={seeds} "
          f"rng-seed={args.seed}")
    import tempfile
    work = tempfile.mkdtemp(prefix="eidolon_ea_")
    if progress:
        import threading
        lock = threading.Lock()
        def logged(msg: str) -> None:
            print(msg)
        res = evolve(sim_bin, seeds, days=args.days, pop=args.pop, gens=args.gens,
                     rng_seed=args.seed, inits=inits, tmpdir=work, log=logged)
    else:
        res = evolve(sim_bin, seeds, days=args.days, pop=args.pop, gens=args.gens,
                     rng_seed=args.seed, inits=inits, tmpdir=work)

    w, b = _from_vector(res["best"])
    write_prior(args.out, w, b)
    print(f"best prior written to {args.out} (fitness {res['best_fitness']:.0f})")

    # Final report: best vs random init (and the init priors used).
    from .eval import sim_eval
    configs: dict = {"random init": None, "evolved": args.out}
    for i, p in enumerate([x for x in args.init_priors.split(",") if x]):
        configs[f"init{i}"] = p
    report = sim_eval(sim_bin, configs, seeds, days=args.days, tmpdir=work)
    for name, agg in report.items():
        print(f"  {name}: ticks={agg.get('ticks')} actions={agg.get('actions')} "
              f"non-agentic={agg.get('non_agentic_actions')} "
              f"survived={agg.get('survived')}/{agg.get('seeds')}")
    if progress:
        progress.set_results({"evolve": {"best_fitness": res["best_fitness"],
                                         "history": res["history"]}, "sim_eval": report})
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