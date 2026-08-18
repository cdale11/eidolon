"""Tiny web UI to watch dataset generation progress while the teacher labels records.

Usage: created by train_prior.py with --progress-port (default 8090). Serves:
  GET /             — light-theme dashboard, auto-refreshes from /api/progress
  GET /api/progress — JSON snapshot of the current labeling run
Runs on a background thread; offline tooling only (conda env `eidolon`), never in the
C++ runtime.
"""
from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from .progress import ProgressState

PAGE = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Eidolon · dataset generation</title>
<style>
  :root { color-scheme: light; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
         sans-serif; background: #f7f7f8; color: #0d0d0d; }
  main { max-width: 720px; margin: 0 auto; padding: 32px 20px; }
  h1 { font-size: 20px; margin: 0 0 4px; }
  .sub { color: #6b6b70; font-size: 13px; margin-bottom: 20px; }
  .card { background: #fff; border: 1px solid #ececec; border-radius: 12px;
          padding: 20px; margin-bottom: 16px; }
  .row { display: flex; justify-content: space-between; padding: 6px 0;
         font-size: 14px; border-bottom: 1px solid #f2f2f2; }
  .row:last-child { border-bottom: 0; }
  .row .k { color: #6b6b70; }
  .bar { height: 10px; background: #ececf1; border-radius: 6px; overflow: hidden; }
  .bar > div { height: 100%; background: #10a37f; width: 0; transition: width .3s; }
  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  td { padding: 6px 8px; border-bottom: 1px solid #f2f2f2; }
  td:last-child { text-align: right; font-variant-numeric: tabular-nums; }
  .err { color: #d0312d; font-size: 13px; white-space: pre-wrap; }
  .muted { color: #8e8ea0; }
</style></head>
<body><main>
  <h1>Dataset generation</h1>
  <div class="sub" id="model">…</div>
  <div class="card">
    <div class="row"><span class="k">Stage</span><span id="stage">…</span></div>
    <div class="row"><span class="k">Records</span><span id="done">…</span></div>
    <div class="bar"><div id="bar"></div></div>
    <div class="row"><span class="k">Fallback (no teacher)</span><span id="fallback">…</span></div>
    <div class="row"><span class="k">Failed / skipped</span><span id="failed">…</span></div>
    <div class="row"><span class="k">Rate</span><span id="rate">…</span></div>
    <div class="row"><span class="k">Elapsed</span><span id="elapsed">…</span></div>
    <div class="row"><span class="k">ETA</span><span id="eta">…</span></div>
    <div class="row"><span class="k">Last label</span><span id="lastlabel">…</span></div>
  </div>
  <div class="card"><h2 style="font-size:15px;margin:0 0 8px;">Labels</h2>
    <table id="labels"></table></div>
  <div class="card"><h2 style="font-size:15px;margin:0 0 8px;">Recent errors</h2>
    <div class="err" id="errors"><span class="muted">none</span></div></div>
</main>
<script>
async function poll() {
  let s;
  try {
    s = await (await fetch('/api/progress')).json();
  } catch (e) { return; }
  document.getElementById('model').textContent = 'Model: ' + (s.model || '—');
  document.getElementById('stage').textContent = s.stage;
  document.getElementById('done').textContent =
    s.done + ' / ' + s.total + '  (' + s.percent + '%)';
  document.getElementById('bar').style.width = s.percent + '%';
  document.getElementById('fallback').textContent = s.fallback;
  document.getElementById('failed').textContent =
    s.failed + ' failed / ' + s.skipped + ' skipped';
  document.getElementById('rate').textContent =
    s.rate_per_min.toFixed(1) + ' / min' +
    (s.measured_rpm ? '  (window ' + s.measured_rpm + ' rpm)' : '');
  document.getElementById('elapsed').textContent = s.elapsed + ' s';
  document.getElementById('eta').textContent =
    s.eta_seconds == null ? '—' : (s.eta_seconds / 60).toFixed(1) + ' min';
  document.getElementById('lastlabel').textContent = s.last_label || '—';
  const labels = Object.entries(s.action_counts).sort((a, b) => b[1] - a[1]);
  document.getElementById('labels').innerHTML = labels.map(([k, v]) =>
    `<tr><td>${k}</td><td>${v}</td></tr>`).join('') ||
    '<tr><td class="muted" colspan="2">no labels yet</td></tr>';
  const errs = document.getElementById('errors');
  errs.textContent = s.errors.length ? s.errors.join('\\n') : 'none';
  errs.classList.toggle('muted', !s.errors.length);
}
setInterval(poll, 1500);
poll();
</script></body></html>"""


class ProgressServer:
    def __init__(self, progress: ProgressState, host: str = "127.0.0.1", port: int = 8090):
        self.progress = progress
        self.host = host
        self.port = port
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

    def start(self) -> bool:
        handler = self._make_handler(self.progress)

        class Server(ThreadingHTTPServer):
            daemon_threads = True

        try:
            self._httpd = Server((self.host, self.port), handler)
        except OSError:
            return False
        self.port = self._httpd.server_address[1]
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()
        return True

    def url(self) -> str:
        return f"http://{self.host}:{self.port}"

    def stop(self) -> None:
        if self._httpd:
            self._httpd.shutdown()
            self._httpd = None

    @staticmethod
    def _make_handler(progress: ProgressState):
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/api/progress":
                    body = json.dumps(progress.snapshot()).encode()
                    self._send(body, "application/json")
                else:
                    self._send(PAGE.encode(), "text/html")

            def _send(self, body: bytes, ctype: str):
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *a: Any):
                pass

        return Handler