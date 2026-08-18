"""Server integration tests: eidolon-server runs the sim independently of any browser,
serves the chat UI, persists conversations, survives restarts and LLM outages."""

import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request

SERVER = os.environ.get("SERVER_BIN", "build/bin/eidolon-server")
PORT_BASE = int(os.environ.get("PORT_BASE", "9000"))


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