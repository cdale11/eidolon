#!/usr/bin/env python3
"""Integration tests for the teacher pipeline (Phase 4).

End-to-end, fully offline: dump experiences via the sim, fit a reward-guided prior,
load it back into a fresh organism, and prove (a) the prior loads, (b) online learning
continues on top of it, and (c) the frozen .eprp artifact makes runs deterministic.
A stub OpenAI-compatible endpoint exercises the teacher client without any LLM.
"""
import http.server
import json
import os
import shutil
import socketserver
import subprocess
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from teacher.dataset import ACTION_NAMES, load_experiences  # noqa: E402
from teacher.fit_prior import read_prior, write_prior  # noqa: E402
from teacher.label import TeacherClient  # noqa: E402

SIM = os.environ.get("SIM_BIN", "./build/bin/eidolon-sim")
BASE = "/tmp/eidolon_teacher_integration"


def run_sim(args):
    return subprocess.run([SIM, *args], capture_output=True, text=True, timeout=300)


def read_log(d):
    with open(os.path.join(d, "events.log")) as f:
        return f.read()


def dump_experiences(tmp):
    os.makedirs(tmp, exist_ok=True)
    r = run_sim(["--data", tmp, "--seed", "42", "--deterministic", "--days", "0.15",
                 "--world", "64x64", "--dump-experiences", f"{tmp}/exp.jsonl"])
    assert r.returncode == 0, r.stderr
    return f"{tmp}/exp.jsonl"


def test_dump_and_dataset(tmp):
    path = dump_experiences(tmp)
    exp = load_experiences(path)
    assert len(exp) > 1000, f"dump too small: {len(exp)}"
    names = {e.action for e in exp}
    assert names <= set(ACTION_NAMES), f"unexpected actions: {names}"
    # interpretable context is non-empty and feats are 27 floats
    assert exp[0].interpretable_text()
    assert exp[0].feats.shape == (27,)


def test_prior_fit_and_roundtrip(tmp):
    from teacher.dataset import feature_matrix, labels, reward_best_labels
    from teacher.fit_prior import fit_prior

    path = dump_experiences(tmp)
    exp = load_experiences(path)
    X = feature_matrix(exp)
    y = reward_best_labels(exp)
    res = fit_prior(X, y, epochs=40, val_frac=0.0)
    assert 0.0 <= res["acc"] <= 1.0
    prior = f"{tmp}/prior.eprp"
    write_prior(prior, res["weights"], res["bias"])
    assert os.path.getsize(prior) == 4 + 12 + 5 * 28 * 4  # header + 5 rows of (27+1) f32
    W, b = read_prior(prior)
    assert W.shape == (5, 27) and b.shape == (5,)
    assert (W == res["weights"]).all() and (b == res["bias"]).all()


def test_sim_loads_prior_and_retrains(tmp):
    from teacher.dataset import feature_matrix, reward_best_labels
    from teacher.fit_prior import fit_prior, write_prior

    path = dump_experiences(tmp)
    exp = load_experiences(path)
    res = fit_prior(feature_matrix(exp), reward_best_labels(exp), epochs=40, val_frac=0.0)
    prior = f"{tmp}/prior.eprp"
    write_prior(prior, res["weights"], res["bias"])

    a = f"{tmp}/a"
    b = f"{tmp}/b"
    ra = run_sim(["--data", a, "--seed", "42", "--deterministic", "--days", "0.2",
                  "--world", "64x64", "--policy-prior", prior])
    assert ra.returncode == 0, ra.stderr
    assert "policy prior loaded" in ra.stderr, ra.stderr
    rb = run_sim(["--data", b, "--seed", "42", "--deterministic", "--days", "0.2",
                  "--world", "64x64", "--policy-prior", prior])
    assert rb.returncode == 0, rb.stderr
    # frozen artifact -> deterministic replay
    assert read_log(a) == read_log(b), "runs with the same prior+seed diverged"
    # online learning continues on top of the prior
    with open(os.path.join(a, "metrics.log")) as f:
        metrics = f.read()
    assert "learnerUpdates=" in metrics
    updates = int(metrics.split("learnerUpdates=", 1)[1].splitlines()[0])
    inferences = int(metrics.split("learnerInferences=", 1)[1].splitlines()[0])
    assert updates > 0 and inferences > 0, (updates, inferences)


def test_teacher_client_against_stub(tmp):
    """A stub OpenAI-compatible endpoint proves the client parses teacher JSON."""
    label = ACTION_NAMES[2]

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            self.rfile.read(length)
            body = json.dumps({"choices": [{"message": {"content":
                                    json.dumps({"action": label})}}]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):  # silence
            pass

    class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
        daemon_threads = True

    srv = Server(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=srv.serve_forever, daemon=True)
    thread.start()
    try:
        client = TeacherClient(base=f"http://127.0.0.1:{srv.server_address[1]}/v1",
                               model="stub", timeout=5.0)
        got = client.label("hunger=50, thirst=80, fatigue=30; clear weather")
        assert got == label, got
        # endpoint down -> graceful None (pipeline falls back)
        dead = TeacherClient(base="http://127.0.0.1:1/v1", timeout=1.0)
        assert dead.label("any context") is None
    finally:
        srv.shutdown()


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
    print("all teacher tests passed")


if __name__ == "__main__":
    main()