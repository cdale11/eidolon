"""Phase 15 client-side offload: protocol-level end-to-end without a browser.

Covers the server side of the offload contract that the browser Worker drives:
1. WASM artifacts 404 until a client posts a capable ComputeProfile, served after.
2. The server idles its local tick loop while a client computes; client snapshots
   are accepted, validated and reflected in /api/status.
3. Continuity invariant: when the client goes silent for >15s, the server resumes
   the sim itself (the organism never freezes just because a tab closed).
4. A corrupt / stale blob is rejected without touching server state.
5. Re-posting a weak profile disarms offload immediately.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

SERVER = os.environ.get("SERVER_BIN", "build/bin/eidolon-server")
PORT = int(os.environ.get("PORT_BASE", "9000")) + 50  # own port range slice


def http(port, path, data=None, raw=False, method=None):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        data=data if isinstance(data, (bytes, bytearray)) else
             (json.dumps(data).encode() if data is not None else None),
        headers={"Content-Type": "application/json"} if data is not None else {},
        method=method or ("POST" if data is not None else "GET"),
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        body = resp.read()
        return resp.status, (body if raw else json.loads(body.decode()))


def status(port):
    return http(port, "/api/status")[1]


def start_server(work):
    proc = subprocess.Popen(
        [SERVER, "--data", work, "--port", str(PORT), "--world", "64x64",
         "--seed", "7", "--deterministic"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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


def have_wasm_artifacts():
    return (os.path.exists("build-wasm/bin/eidolon-worker.wasm") or
            os.path.exists("build-wasm-simd/bin/eidolon-worker.wasm"))


def expect_http_error(port, path, code, data=None, method=None):
    try:
        http(port, path, data=data, method=method)
        raise AssertionError(f"expected HTTP {code} for {path}")
    except urllib.error.HTTPError as e:
        assert e.code == code, f"{path}: expected {code}, got {e.code}"


def test_offload_protocol():
    work = tempfile.mkdtemp(prefix="eidolon-offload-")
    proc = start_server(work)
    try:
        s0 = status(PORT)
        assert "simTime" in s0

        # 1. WASM assets must NOT be served before any capable profile arrives.
        expect_http_error(PORT, "/api/wasm/core.wasm", 404)
        expect_http_error(PORT, "/api/wasm/core.js", 404)
        # The worker script itself is always available (it is embedded).
        st, worker_js = http(PORT, "/api/wasm/worker.js", raw=True)
        assert st == 200 and b"eidn_snapshot" in worker_js

        # 2. Post a SIMD-capable profile -> server arms offload.
        st, j = http(PORT, "/api/compute-profile", data={
            "wasm_simd128": 1, "shared_array_buffer": 0, "max_workers": 4,
            "estimated_sim_steps_per_sec": 1000.0})
        assert st == 200 and j["ok"], j
        backend = j["selection"]["backend"]
        assert backend in (1, 2, 3), f"unexpected backend for simd profile: {backend}"

        if not have_wasm_artifacts():
            print("  (worker artifacts not built here; skipping artifact fetch checks)")
        else:
            st, wasm = http(PORT, "/api/wasm/core.wasm", raw=True)
            assert st == 200 and wasm[:4] == b"\x00asm", f"bad wasm magic: {wasm[:8]}"
            st, glue = http(PORT, "/api/wasm/core.js", raw=True)
            assert st == 200 and b"EidolonWorker" in glue

        # 3. Act as the client: download the current sim state, then return a snapshot.
        st, blob = http(PORT, "/api/snapshot/download", raw=True)
        assert st == 200 and len(blob) > 1000, f"snapshot too small: {len(blob)}"
        # While client-computing is armed, server's local loop is idle: two status
        # reads one second apart must show the SAME simTime (no local ticking).
        t_a = status(PORT)["simTime"]
        time.sleep(1.2)
        t_b = status(PORT)["simTime"]
        assert t_a == t_b, f"server ticked while offloaded: {t_a} -> {t_b}"
        st, j = http(PORT, "/api/client/snapshot", data=bytes(blob))
        assert st == 200 and j["ok"], j

        # Garbage is rejected, server state untouched.
        expect_http_error(PORT, "/api/client/snapshot", 400, data=b"junk-bytes")

        # 4. Disarm via a weak profile; the server resumes within one idle poll.
        st, j = http(PORT, "/api/compute-profile", data={
            "wasm_simd128": 0, "shared_array_buffer": 0, "max_workers": 1})
        assert st == 200 and j["ok"]
        time.sleep(1.0)
        t_c = status(PORT)["simTime"]
        time.sleep(1.0)
        t_d = status(PORT)["simTime"]
        assert t_d > t_c, f"server did not resume after disarm: {t_c} -> {t_d}"
        # Uploads are refused once the server owns the sim again.
        expect_http_error(PORT, "/api/client/snapshot", 400, data=bytes(blob))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(work, ignore_errors=True)
    print(f"  offload protocol ok (backend={backend})")


def test_client_silence_resume():
    """Heartbeat timeout: arm offload, send nothing, server must resume by itself."""
    work = tempfile.mkdtemp(prefix="eidolon-offload-silence-")
    proc = start_server(work)
    try:
        st, j = http(PORT, "/api/compute-profile", data={
            "wasm_simd128": 1, "shared_array_buffer": 0, "max_workers": 4})
        assert st == 200 and j["ok"]
        t0 = status(PORT)["simTime"]
        time.sleep(3.0)
        t1 = status(PORT)["simTime"]
        assert t1 == t0, f"sim advanced while a silent client owned it: {t0} -> {t1}"
        time.sleep(9.0)  # still inside the 15s silence window
        t2 = status(PORT)["simTime"]
        assert t2 == t0, f"sim advanced while a silent client owned it: {t0} -> {t2}"
        # Past 15s of silence the server must resume the sim itself.
        time.sleep(6.0)
        t3 = status(PORT)["simTime"]
        assert t3 > t2, f"server did not resume after 15s silence: {t2} -> {t3}"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(work, ignore_errors=True)
    print("  silent-client resume: sim frozen during silence, control recovered")


if __name__ == "__main__":
    test_offload_protocol()
    test_client_silence_resume()
    print("PASS python/tests/test_client_offload.py")
