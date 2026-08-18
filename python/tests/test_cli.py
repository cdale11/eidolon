#!/usr/bin/env python3
"""Integration tests for the headless simulator CLI (Phase 1).

Covers: determinism (same seed -> identical log), seed divergence,
save/load continuity (resume == uninterrupted run), and metrics output.
All tests run the actual built binary; no pytest required.
"""
import os
import shutil
import subprocess
import sys

SIM = os.environ.get("SIM_BIN", "./build/bin/eidolon-sim")

BASE = "/tmp/eidolon_integration"


def run_sim(args):
    return subprocess.run(
        [SIM, *args],
        capture_output=True,
        text=True,
        timeout=300,
    )


def read_log(d):
    with open(os.path.join(d, "events.log")) as f:
        return f.read()


def test_determinism(tmp):
    r1 = run_sim(["--data", f"{tmp}/a", "--seed", "42", "--deterministic", "--days", "0.5", "--world", "64x64"])
    r2 = run_sim(["--data", f"{tmp}/b", "--seed", "42", "--deterministic", "--days", "0.5", "--world", "64x64"])
    assert r1.returncode == 0, r1.stderr
    assert r2.returncode == 0, r2.stderr
    assert read_log(f"{tmp}/a") == read_log(f"{tmp}/b"), "deterministic logs differ"


def test_seeds_diverge(tmp):
    r1 = run_sim(["--data", f"{tmp}/a", "--seed", "42", "--deterministic", "--days", "0.5", "--world", "64x64"])
    r2 = run_sim(["--data", f"{tmp}/b", "--seed", "43", "--deterministic", "--days", "0.5", "--world", "64x64"])
    assert r1.returncode == 0 and r2.returncode == 0
    assert read_log(f"{tmp}/a") != read_log(f"{tmp}/b"), "different seeds produced identical logs"


def test_saveload_continuity(tmp):
    """A resumed run must be byte-identical to an uninterrupted run."""
    # uninterrupted: 1.0 day
    run_sim(["--data", f"{tmp}/full", "--seed", "7", "--deterministic", "--days", "1.0", "--world", "64x64"])
    # two-stage: 0.5 + 0.5 days, resuming from the snapshot
    r1 = run_sim(["--data", f"{tmp}/stage", "--seed", "7", "--deterministic", "--days", "0.5", "--world", "64x64"])
    assert r1.returncode == 0, r1.stderr
    r2 = run_sim(["--data", f"{tmp}/stage", "--seed", "7", "--deterministic", "--days", "0.5", "--world", "64x64"])
    assert r2.returncode == 0, r2.stderr
    full = read_log(f"{tmp}/full")
    staged = read_log(f"{tmp}/stage")
    assert full == staged, (
        "resumed run diverged from uninterrupted run\n"
        f"--- full ({len(full.splitlines())} lines) vs staged ({len(staged.splitlines())} lines)"
    )


def test_metrics_written(tmp):
    r = run_sim(["--data", f"{tmp}/m", "--seed", "1", "--deterministic", "--days", "0.2", "--world", "32x32"])
    assert r.returncode == 0, r.stderr
    with open(os.path.join(tmp, "m", "metrics.log")) as f:
        text = f.read()
    for key in ("phase=3", "seed=", "deterministic=1", "simTime=", "ticksFine=", "worldHash=", "stopped=completed",
                "learnerInferences=", "learnerUpdates="):
        assert key in text, f"missing {key!r} in metrics.log"


def test_log_has_life_trace(tmp):
    r = run_sim(["--data", f"{tmp}/l", "--seed", "42", "--deterministic", "--days", "0.5", "--world", "64x64"])
    assert r.returncode == 0, r.stderr
    log = read_log(f"{tmp}/l")
    assert "[t=" in log and "status:" in log
    kinds = set(line.split("] ", 1)[1].split(":", 1)[0] if "] " in line else "" for line in log.splitlines())
    assert {"birth", "state"} <= kinds, f"unexpected kinds: {kinds}"


def main():
    if not os.path.exists(SIM):
        print(f"error: {SIM} not built", file=sys.stderr)
        sys.exit(1)
    if os.path.exists(BASE):
        shutil.rmtree(BASE)
    os.makedirs(BASE, exist_ok=True)
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    failed = 0
    for t in tests:
        # unique sandbox per test (the sim resumes from save.snap, so reuse would leak state)
        work = os.path.join(BASE, t.__name__)
        try:
            t(work)
            print(f"PASS {t.__name__}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL {t.__name__}: {e}")
        except Exception as e:  # noqa: BLE001
            failed += 1
            print(f"ERROR {t.__name__}: {e!r}")
    if failed:
        print(f"{failed} test(s) failed")
        sys.exit(1)
    print("all integration tests passed")


if __name__ == "__main__":
    main()
