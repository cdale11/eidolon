"""Reply-quality harness (Q4): scripted multi-turn scenarios scored on grounding,
non-fabrication, relevance and voice consistency, plus per-class reply latency.

Run before/after each reply-quality slice so improvements are measured, not
felt. Uses a recording stub LLM (no GPU needed): prompt-side checks verify what
was SENT to the model (grounded facts, trait words, sampling budgets), while
offline checks verify the deterministic replies. Print scores, assert floors.
"""

import http.server as http_server
import json
import os
import re
import socketserver
import sqlite3
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_server import http, is_respond_call, start_server  # noqa: E402

PORT_BASE = int(os.environ.get("PORT_BASE", "9000"))

# Reference-machine budgets (4B Q4_K_M on the Radeon 740M iGPU, ~10 tok/s):
# factual replies are capped at 128 tokens (~13s worst case), open smalltalk at
# 256 (~26s). The harness runs against instant stubs/offline replies, so CI
# budgets below are far tighter; real-model timing is measured manually.
HARNESS_BUDGET_OFFLINE_MS = 2000.0
HARNESS_BUDGET_STUB_LLM_MS = 15000.0

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), str(detail)))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""))


def p95(xs):
    s = sorted(xs)
    return s[max(0, int(0.95 * (len(s) - 1)))]


def test_relevance_offline(work):
    port = PORT_BASE + 0
    proc = start_server(work, port)
    try:
        lat = []
        cases = [
            ("hello", "Good "),
            ("how are you?", "Status this"),
            ("where are you?", "I am at ("),
            ("what are your goals?", "goals right now"),
            ("what are your skills?", "practiced skills"),
            ("do you trust me?", "trust"),
            ("help", "You can ask me"),
        ]
        for msg, marker in cases:
            t0 = time.perf_counter()
            r = http(port, "/api/send", {"message": msg})
            lat.append((time.perf_counter() - t0) * 1000.0)
            check(f"relevant offline: {msg!r}", marker in r["reply"], r["reply"][:80])
        check("offline p95 latency under budget", p95(lat) < HARNESS_BUDGET_OFFLINE_MS,
              f"p95={p95(lat):.0f}ms")
    finally:
        proc.kill()
        proc.wait()


def test_nonfabrication_offline(work):
    fresh = os.path.join(work, "fresh")
    os.makedirs(fresh, exist_ok=True)
    port = PORT_BASE + 1
    proc = start_server(fresh, port)
    try:
        r = http(port, "/api/send", {"message": "what are your skills?"})
        # The sim practices within seconds, so competence may be real — but
        # every named skill must come from the fixed vocabulary, never invented.
        vocab = {"firemaking", "knapping", "woodworking", "foraging", "hunting",
                 "shelter-building", "cooking", "tool-use", "tracking",
                 "navigation", "wall-building", "farming", "storage",
                 "tool-invention", "recipe-discovery"}
        named = set(re.findall(r"([a-z][a-z-]+)=", r["reply"]))
        check("skills reply names only real skills", named <= vocab,
              r["reply"][:100])
        units = all(0.0 <= float(v) <= 1.0
                    for v in re.findall(r"=(\d+\.\d+)", r["reply"]))
        check("skill values are unit-range", units, r["reply"][:100])
        r = http(port, "/api/send", {"message": "do you remember the moon landing?"})
        check("unknowns not invented", "moon" not in r["reply"].lower()
              and "Energy" in r["reply"], r["reply"][:100])
    finally:
        proc.kill()
        proc.wait()


class Stub(http_server.BaseHTTPRequestHandler):
    respond_reqs = []

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        req = json.loads(self.rfile.read(length) or b"{}")
        if not is_respond_call(req):  # parse call
            text = ""
            try:
                text = json.loads(req["messages"][-1]["content"])["message"]
            except Exception:
                pass
            low = text.lower()
            if "story" in low:
                intent = "smalltalk"
            elif low.startswith("hello") or low.startswith("hi"):
                intent = "greet"
            else:
                intent = "question"
            mem = ("today" in low or "yesterday" in low or "remember" in low
                   or "did you do" in low)
            msg = {"content": json.dumps({"intent": intent, "topic": "", "tone": "neutral",
                                          "references_memory": mem})}
            body = {"choices": [{"message": msg}]}
        else:  # respond call
            Stub.respond_reqs.append(req)
            msg = {"content": "A short stub reply."}
            body = {"choices": [{"message": msg}],
                    "usage": {"prompt_tokens": 200, "completion_tokens": 20,
                              "total_tokens": 220}}
        raw = json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *a):
        pass


def payload_of(req):
    return json.loads(req["messages"][-1]["content"])


def test_grounding_voice_sampling(work):
    Stub.respond_reqs = []

    class Srv(socketserver.ThreadingMixIn, http_server.HTTPServer):
        daemon_threads = True

    srv = Srv(("127.0.0.1", 0), Stub)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        base = f"http://127.0.0.1:{srv.server_address[1]}/v1"
        port = PORT_BASE + 2
        proc = start_server(work, port, extra=["--llm", base, "--llm-timeout", "5000"])
        try:
            db = sqlite3.connect(os.path.join(work, "memory.db"))
            try:
                db.execute("INSERT INTO episodes (t, x, y, kind, importance, detail)"
                           " VALUES (?,?,?,?,?,?)", (0, 3, 4, 1, 0.8, 0))
                db.commit()
            finally:
                db.close()

            lat = []

            def send(msg):
                t0 = time.perf_counter()
                r = http(port, "/api/send", {"message": msg})
                lat.append((time.perf_counter() - t0) * 1000.0)
                return r

            r = send("what did you do today?")
            assert r["reply"].strip()
            r = send("how are you?")
            assert r["reply"].strip()
            r = send("tell me a story")
            assert r["reply"].strip()
            assert len(Stub.respond_reqs) == 3, len(Stub.respond_reqs)

            mem_payload = payload_of(Stub.respond_reqs[0])
            check("memory answer grounded in archive",
                  "foraging" in mem_payload.get("grounded_memory", ""),
                  mem_payload.get("grounded_memory", "")[:80])
            check("prompt carries bounded history", "history" in mem_payload)
            check("prompt carries lastInstruction", "lastInstruction" in mem_payload["state"])
            check("prompt carries predecessor_history", "predecessor_history" in mem_payload)

            pers = [re.search(r"personality=\[(.*?)\]", payload_of(q)["state"])
                    for q in Stub.respond_reqs]
            check("personality words present in all prompts",
                  all(p and p.group(1).strip() for p in pers),
                  [p.group(1) if p else None for p in pers])
            check("no raw floats in personality",
                  all(p and not re.search(r"\d", p.group(1)) for p in pers))
            check("voice consistent across turns", pers[0].group(1) == pers[1].group(1)
                  == pers[2].group(1), pers[0].group(1))

            drives = re.search(r"drives=\[(.*?)\]",
                               payload_of(Stub.respond_reqs[0])["state"])
            check("no raw floats in drives",
                  drives and not re.search(r"\d", drives.group(1)),
                  drives.group(1) if drives else None)

            temps = [q.get("temperature") for q in Stub.respond_reqs]
            caps = [q.get("max_tokens") for q in Stub.respond_reqs]
            # factual ("what did you do today?", "how are you?") vs open ("story")
            check("factual sampling is tight",
                  all(t is not None and t <= 0.25 for t in temps[:2])
                  and all(c is not None and c <= 128 for c in caps[:2]),
                  f"temp={temps[:2]} max_tokens={caps[:2]}")
            check("open sampling is loose but capped",
                  temps[2] is not None and temps[2] >= 0.6
                  and caps[2] is not None and caps[2] <= 256,
                  f"temp={temps[2]} max_tokens={caps[2]}")
            check("stub-LLM p95 latency under budget",
                  p95(lat) < HARNESS_BUDGET_STUB_LLM_MS, f"p95={p95(lat):.0f}ms")
        finally:
            proc.kill()
            proc.wait()
    finally:
        srv.shutdown()


if __name__ == "__main__":
    import tempfile

    work = tempfile.mkdtemp(prefix="eidolon_quality_")
    try:
        print("reply-quality harness:")
        test_relevance_offline(work)
        test_nonfabrication_offline(work)
        test_grounding_voice_sampling(work)
        nfail = sum(1 for _, ok, _ in results if not ok)
        nok = len(results) - nfail
        print(f"harness: {nok}/{len(results)} checks passed")
        if nfail:
            sys.exit(1)
        print("harness: all passed")
    finally:
        import shutil

        shutil.rmtree(work, ignore_errors=True)
