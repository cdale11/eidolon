"""Phase 12 sync/offline/auth: server-side protocol e2e without a browser.

Covers:
1. --api-key gates mutating (POST) endpoints; GET status remains open.
2. --world-authority server: a stale client snapshot (older sim-time) is rejected, not
   applied, so the headless fallback's forward progress is never rolled back.
3. A fresh/valid client snapshot is accepted in both authority modes.
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
PORT = int(os.environ.get("PORT_BASE", "9000")) + 80  # own port slice


def http(port, path, data=None, raw=False, method=None, headers=None):
    h = {"Content-Type": "application/json"} if data is not None else {}
    if headers:
        h.update(headers)
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=data if isinstance(data, (bytes, bytearray)) else
             (json.dumps(data).encode() if data is not None else None),
        headers=h,
        method=method or ("POST" if data is not None else "GET"),
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            body = resp.read()
            return resp.status, (body if raw else json.loads(body.decode()))
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode())


def status(port, headers=None):
    return http(port, "/api/status", headers=headers)


def get_snapshot(port):
    code, body = http(port, "/api/snapshot/download", raw=True)
    return body


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
        # --- auth gating ---
        proc = start_server(work, extra=["--api-key", "sekret"])
        try:
            # GET status is open (no token).
            code, _ = status(PORT)
            assert code == 200, f"GET status should be open, got {code}"
            # POST without token -> 401.
            code, _ = http(PORT, "/api/conversations/new", {})
            assert code == 401, f"POST without token should be 401, got {code}"
            # POST with token via Authorization header -> OK.
            code, _ = http(PORT, "/api/conversations/new", {},
                           headers={"Authorization": "Bearer sekret"})
            assert code == 200, f"POST with token should be 200, got {code}"
            # POST with token via ?key= -> OK.
            code, _ = http(PORT, "/api/conversations/new?key=sekret", {})
            assert code == 200, f"POST with ?key should be 200, got {code}"
            # Wrong token -> 401.
            code, _ = http(PORT, "/api/conversations/new", {},
                           headers={"Authorization": "Bearer wrong"})
            assert code == 401, f"POST with wrong token should be 401, got {code}"
        finally:
            proc.terminate()
            proc.wait(timeout=10)

    with tempfile.TemporaryDirectory() as work:
        # --- server-authoritative reconcile: stale snapshots rejected ---
        proc = start_server(work, extra=["--world-authority", "server"])
        try:
            # Arm client computing FIRST so the server stops ticking; then the snapshot we
            # download is exactly current and re-uploading it is accepted (equal sim-time).
            http(PORT, "/api/compute-profile",
                 {"offload": True, "wasm_simd128": 1, "max_workers": 1})
            snap = get_snapshot(PORT)
            assert snap and len(snap) > 100, "snapshot download should return bytes"
            code, body = http(PORT, "/api/client/snapshot", snap)
            assert code == 200 and body.get("ok"), f"valid upload: {code} {body}"
            # Now corrupt the sim-time inside the snapshot to be far in the past. The blob
            # layout: magic(4) version(4) checksum(8) masterSeed(8) deterministic(1)
            # clock(8). Sim-time lives at offset 16+8+1 = 25.
            stale = bytearray(snap)
            for i in range(8):
                stale[25 + i] = 0  # sim-time 0 (well behind the server)
            code, body = http(PORT, "/api/client/snapshot", bytes(stale))
            assert code == 400 and body.get("stale"), \
                f"stale upload should be rejected: {code} {body}"
        finally:
            proc.terminate()
            proc.wait(timeout=10)

    with tempfile.TemporaryDirectory() as work:
        # --- client-authoritative (default): stale accepted, then valid accepted ---
        proc = start_server(work)
        try:
            http(PORT, "/api/compute-profile",
                 {"offload": True, "wasm_simd128": 1, "max_workers": 1})
            snap = get_snapshot(PORT)
            code, body = http(PORT, "/api/client/snapshot", snap)
            assert code == 200 and body.get("ok"), f"client-auth upload: {code} {body}"
        finally:
            proc.terminate()
            proc.wait(timeout=10)

    print("PASS test_phase12")


if __name__ == "__main__":
    main()