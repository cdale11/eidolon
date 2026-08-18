"""Server integration tests: eidolon-server runs the sim independently of any browser,
serves the chat UI, persists conversations, survives restarts and LLM outages."""

import http.server as http_server
import json
import os
import shutil
import socketserver
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

SERVER = os.environ.get("SERVER_BIN", "build/bin/eidolon-server")
PORT_BASE = int(os.environ.get("PORT_BASE", "9000"))

from teacher.dataset import load_experiences as load_teacher_experiences  # noqa: E402
from teacher.dataset import feature_matrix as teacher_feature_matrix  # noqa: E402
from teacher.dataset import reward_best_labels as teacher_reward_labels  # noqa: E402


def start_server(data_dir, port, extra=None):
    cmd = [SERVER, "--data", data_dir, "--port", str(port), "--world", "64x64"]
    if extra:
        cmd += extra
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # wait for the HTTP listener
    for _ in range(100):
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/api/status", timeout=0.5)
            return proc
        except Exception:
            if proc.poll() is not None:
                raise RuntimeError(f"server exited early: {proc.returncode}")
            time.sleep(0.1)
    proc.kill()
    raise RuntimeError("server did not start")


def http(port, path, data=None):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=json.dumps(data).encode() if data is not None else None,
        headers={"Content-Type": "application/json"} if data is not None else {},
        method="POST" if data is not None else "GET",
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode())


def test_ui_and_status(work):
    proc = start_server(work, PORT_BASE)
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT_BASE}/", timeout=5) as resp:
            html = resp.read().decode()
        assert "<title>Eidolon</title>" in html and "id=\"input\"" in html
        s = http(PORT_BASE, "/api/status")
        assert s["alive"] is True
        assert s["day"] >= 0
        assert 0 <= s["energy"] <= 100
    finally:
        proc.kill()
        proc.wait()


def test_conversation_persists_across_restart(work):
    port = PORT_BASE + 1
    proc = start_server(work, port)
    try:
        r = http(port, "/api/send", {"message": "persist me"})
        assert r["conversation_id"] >= 1
        assert r["reply"].strip()
        t1 = http(port, "/api/status")["simTime"]
    finally:
        proc.terminate()  # graceful: final save runs
        proc.wait()
    # restart: same individual (sim continues) + same conversation
    proc = start_server(work, port)
    try:
        t2 = http(port, "/api/status")["simTime"]
        assert t2 >= t1, f"sim did not continue across restart ({t1} -> {t2})"
        convs = http(port, "/api/conversations")
        assert any(c["id"] == r["conversation_id"] for c in convs)
        msgs = http(port, f"/api/messages?conversation_id={r['conversation_id']}")
        assert msgs[0]["role"] == "user" and msgs[0]["text"] == "persist me"
        assert msgs[-1]["role"] == "organism"
    finally:
        proc.kill()
        proc.wait()


def test_sim_runs_without_browser(work):
    port = PORT_BASE + 2
    proc = start_server(work, port)
    try:
        t1 = http(port, "/api/status")["simTime"]
        # no browser interaction at all; sim must keep advancing on its own
        time.sleep(3)
        t2 = http(port, "/api/status")["simTime"]
        assert t2 > t1, "sim not advancing without browser"
    finally:
        proc.kill()
        proc.wait()


def test_offline_llm_fallback(work):
    port = PORT_BASE + 3
    # no --llm flag: offline mode, replies must come from deterministic fallback
    proc = start_server(work, port)
    try:
        r = http(port, "/api/send", {"message": "hello"})
        assert r["reply"].strip()
        # reply must be grounded in state: one of the known fallback shapes
        assert any(k in r["reply"] for k in ("thirsty", "hungry", "tired", "asleep",
                                             "Day ", "no longer alive",
                                             "critical condition"))
    finally:
        proc.kill()
        proc.wait()


def test_dead_llm_does_not_stop_sim(work):
    port = PORT_BASE + 4
    # point at a port with nothing listening: every call times out -> fallback
    proc = start_server(work, port, extra=["--llm", "http://127.0.0.1:1/v1",
                                           "--llm-timeout", "500"])
    try:
        r = http(port, "/api/send", {"message": "are you there?"})
        assert r["reply"].strip()
        t1 = http(port, "/api/status")["simTime"]
        time.sleep(2)
        t2 = http(port, "/api/status")["simTime"]
        assert t2 > t1, "sim stopped when LLM endpoint is dead"
    finally:
        proc.kill()
        proc.wait()


def test_archive_written_by_server(work):
    port = PORT_BASE + 5
    proc = start_server(work, port)
    try:
        time.sleep(2)
        db_path = os.path.join(work, "memory.db")
        assert os.path.exists(db_path)
    finally:
        proc.kill()
        proc.wait()


def test_server_loads_policy_prior(work):
    from teacher.fit_prior import fit_prior, write_prior

    # fit a tiny prior from a quick sim dump so the server gets a real .eprp
    dump_dir = os.path.join(work, "dump")
    os.makedirs(dump_dir, exist_ok=True)
    dump = os.path.join(dump_dir, "exp.jsonl")
    sim = os.environ.get("SIM_BIN", "build/bin/eidolon-sim")
    r = subprocess.run([sim, "--data", dump_dir, "--seed", "42", "--deterministic",
                        "--days", "0.05", "--world", "64x64",
                        "--dump-experiences", dump],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr
    exp = load_teacher_experiences(dump)
    res = fit_prior(teacher_feature_matrix(exp), teacher_reward_labels(exp),
                    epochs=40, val_frac=0.0)
    prior = os.path.join(work, "prior.eprp")
    write_prior(prior, res["weights"], res["bias"])

    port = PORT_BASE + 6
    proc = start_server(work, port, extra=["--policy-prior", prior])
    try:
        s = http(port, "/api/status")
        assert s["alive"] is True
    finally:
        proc.kill()
        proc.wait()


def test_chat_lifecycle(work):
    port = PORT_BASE + 7
    proc = start_server(work, port)
    try:
        # new chat
        nc = http(port, "/api/conversations/new", {})
        cid = nc["conversation_id"]
        assert cid > 0
        # send the first message -> conversation gets a title from it
        r = http(port, "/api/send", {"message": "hello new world", "conversation_id": str(cid)})
        assert r["conversation_id"] == cid
        convs = http(port, "/api/conversations")
        mine = [c for c in convs if c["id"] == cid]
        assert mine and mine[0]["title"].startswith("hello new world"), mine
        # delete it
        http(port, "/api/conversations/delete", {"conversation_id": str(cid)})
        convs = http(port, "/api/conversations")
        assert not any(c["id"] == cid for c in convs)
    finally:
        proc.kill()
        proc.wait()


def test_world_reset(work):
    port = PORT_BASE + 8
    proc = start_server(work, port)
    try:
        time.sleep(2)
        t1 = http(port, "/api/status")["simTime"]
        assert t1 > 0
        s = http(port, "/api/world/reset", {})
        assert s["alive"] is True
        # fresh organism: simTime starts near zero again
        assert s["simTime"] < 3600, f"world reset did not restart the sim: {s['simTime']}"
        time.sleep(1)
        t2 = http(port, "/api/status")["simTime"]
        assert t2 > s["simTime"], "sim not advancing after reset"
    finally:
        proc.kill()
        proc.wait()


def test_reasoning_model_replies(work):
    """Reasoning models emit long reasoning_content + a final answer. The server must
    extract the structured JSON (parse) and the prose reply (respond) correctly."""
    class RHandler(http_server.BaseHTTPRequestHandler):
        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            req = json.loads(self.rfile.read(length) or b"{}")
            mt = int(req.get("max_tokens", 0))
            think = "The organism is autonomous. " * 120  # long chain-of-thought
            if mt <= 200:  # /parse call: JSON object wrapped after the reasoning
                msg = {"reasoning_content": think,
                       "content": "Let me classify this message carefully. "
                                  "```json\n{\"intent\":\"question\",\"topic\":\"weather\","
                                  "\"tone\":\"neutral\",\"references_memory\":false}\n```"}
            else:  # /respond call: prose answer wrapped in fences
                msg = {"reasoning_content": think,
                       "content": "```\nThe weather is pleasant right now.\n```"}
            body = json.dumps({"choices": [{"message": msg}]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    class Srv(socketserver.ThreadingMixIn, http_server.HTTPServer):
        daemon_threads = True

    srv = Srv(("127.0.0.1", 0), RHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        base = f"http://127.0.0.1:{srv.server_address[1]}/v1"
        port = PORT_BASE + 9
        proc = start_server(work, port, extra=["--llm", base, "--llm-timeout", "5000"])
        try:
            r = http(port, "/api/send", {"message": "how is the weather?"})
            assert r["reply"].strip(), "no reply from reasoning model"
            # the fence-wrapped reply must be unwrapped, not leaked raw
            assert "```" not in r["reply"], f"fences leaked: {r['reply']!r}"
            assert r["reply"].strip() == "The weather is pleasant right now."
        finally:
            proc.kill()
            proc.wait()
    finally:
        srv.shutdown()


def main():
    if not os.path.exists(SERVER):
        print(f"error: {SERVER} not built", file=sys.stderr)
        sys.exit(1)
    base = os.environ.get("WORK_BASE", "/tmp/eidolon_srv_test")
    if os.path.exists(base):
        shutil.rmtree(base)
    os.makedirs(base, exist_ok=True)
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    failed = 0
    for t in tests:
        work = os.path.join(base, t.__name__)
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
    print("all server integration tests passed")


if __name__ == "__main__":
    main()