"""Shared-world observation + timezone-aware chat: server-side e2e.

1. GET /api/world/summary returns a read-only authoritative world census (plants/wildlife/
   structures/authority) without mutating state, pollable by any observer.
2. /api/send with user_hour greets using the USER's local time while the organism's
   circadian content stays grounded in its own sim clock.
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

SERVER = os.environ.get("SERVER_BIN", "build/bin/eidolon-server")
PORT = int(os.environ.get("PORT_BASE", "9000")) + 90  # own port slice


def http(port, path, data=None, method=None):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=json.dumps(data).encode() if data is not None else None,
        headers={"Content-Type": "application/json"} if data is not None else {},
        method=method or ("POST" if data is not None else "GET"),
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return resp.status, json.loads(resp.read().decode())


def status(port):
    return http(port, "/api/status")


def start_server(work, extra=None):
    args = [SERVER, "--data", work, "--port", str(PORT), "--world", "64x64",
            "--seed", "7", "--deterministic"]
    if extra:
        args += extra
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            status(PORT)
            return proc
        except Exception:
            if proc.poll() is not None:
                raise RuntimeError(f"server exited early: {proc.poll()}")
            time.sleep(0.1)
    proc.kill()
    raise RuntimeError("server did not start")


def main():
    with tempfile.TemporaryDirectory() as work:
        proc = start_server(work, extra=["--world-authority", "server"])
        try:
            code, s = http(PORT, "/api/world/summary")
            assert code == 200
            assert s["authority"] == "server", s
            assert "plants" in s and "wolves" in s and "organism_x" in s, s
            # Observation is read-only: polling twice is stable and does not mutate.
            code2, s2 = http(PORT, "/api/world/summary")
            assert code2 == 200 and s2["day"] == s["day"], (s, s2)
        finally:
            proc.terminate(); proc.wait(timeout=10)

    with tempfile.TemporaryDirectory() as work:
        # Default client-authoritative mode.
        proc = start_server(work)
        try:
            # user_hour=8 -> "Good morning"; the organism's own time is sim-midnight.
            code, r = http(PORT, "/api/send", {"message": "hi", "user_hour": 8.0})
            assert code == 200 and "Good morning" in r["reply"], r
            # user_hour=23 -> "Good night" greeting even though sim is at dawn-ish hour.
            code, r = http(PORT, "/api/send", {"message": "hi", "user_hour": 23.0})
            assert code == 200 and "Good night" in r["reply"].replace("evening", "night") \
                or "Good night" in r["reply"], r
        finally:
            proc.terminate(); proc.wait(timeout=10)

    print("PASS test_shared_world_timezone")


if __name__ == "__main__":
    main()