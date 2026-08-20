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
  <div class="card" id="fitcard" hidden><h2 style="font-size:15px;margin:0 0 8px;">
    Training</h2>
    <div class="row"><span class="k">Epoch</span><span id="fepoch">…</span></div>
    <div class="bar"><div id="fbar"></div></div>
    <div class="row"><span class="k">Loss</span><span id="floss">…</span></div>
    <div class="row"><span class="k">Train accuracy</span><span id="facc">…</span></div>
    <div class="row"><span class="k">Validation accuracy</span><span id="fval">…</span></div>
    <div class="row"><span class="k">ETA</span><span id="feta">…</span></div>
    <div style="margin-top:10px;height:60px;position:relative;background:#f2f2f6;
                border-radius:8px;">
      <canvas id="flosscurve" style="position:absolute;inset:0;width:100%;height:100%;">
      </canvas>
    </div>
  </div>
  <div class="card"><h2 style="font-size:15px;margin:0 0 8px;">Recent errors</h2>
    <div class="err" id="errors"><span class="muted">none</span></div></div>
  <div class="card" id="rescard" hidden><h2 style="font-size:15px;margin:0 0 8px;">
    Results</h2><div id="results"></div></div>
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
  const fc = document.getElementById('fitcard');
  const f = s.fit;
  if (f && f.epochs) {
    fc.hidden = false;
    document.getElementById('fepoch').textContent =
      f.epoch + ' / ' + f.epochs + '  (' + f.fit_percent + '%)';
    document.getElementById('fbar').style.width = f.fit_percent + '%';
    document.getElementById('floss').textContent = f.loss != null ? f.loss.toFixed(4) : '—';
    document.getElementById('facc').textContent =
      f.acc != null ? (f.acc * 100).toFixed(1) + '%' : '—';
    document.getElementById('fval').textContent =
      f.val_acc != null ? (f.val_acc * 100).toFixed(1) + '%' : '—';
    document.getElementById('feta').textContent =
      f.fit_eta_seconds == null ? '—' : (f.fit_eta_seconds / 60).toFixed(1) + ' min';
    const cv = document.getElementById('flosscurve');
    const ctx = cv.getContext('2d');
    if (f.loss_curve && f.loss_curve.length > 1) {
      const curve = f.loss_curve;
      const min = Math.min.apply(null, curve);
      const max = Math.max.apply(null, curve);
      const w = cv.clientWidth, h = cv.clientHeight;
      const span = (max - min) || 1;
      ctx.clearRect(0, 0, w, h);
      ctx.beginPath();
      ctx.strokeStyle = '#10a37f';
      ctx.lineWidth = 1.5;
      curve.forEach((v, i) => {
        const x = (i / (curve.length - 1)) * w;
        const y = h - ((v - min) / span) * (h - 6) - 3;
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }
  } else {
    fc.hidden = true;
  }
  const rc = document.getElementById('rescard');
  const r = s.results || {};
  if (Object.keys(r).length) {
    rc.hidden = false;
    const rows = [];
    if (r.fit) {
      rows.push(['train accuracy', r.fit.acc != null ? (r.fit.acc * 100).toFixed(1) + '%' : '—']);
      rows.push(['validation accuracy', r.fit.val_acc != null ? (r.fit.val_acc * 100).toFixed(1) + '%' : '—']);
      rows.push(['fit loss', r.fit.loss != null ? r.fit.loss.toFixed(4) : '—']);
    }
    rows.push(['records labeled', r.n]);
    rows.push(['agree with organism actions', r.agree_self != null ? (r.agree_self * 100).toFixed(1) + '%' : '—']);
    rows.push(['agree with reward heuristic', r.agree_reward_heuristic != null ? (r.agree_reward_heuristic * 100).toFixed(1) + '%' : '—']);
    if (r.agree_other_teacher != null)
      rows.push(['agree with 2nd teacher', (r.agree_other_teacher * 100).toFixed(1) + '%  (' + r.other_teacher_n + ' records)']);
    rows.push(['fallback (no teacher)', r.fallback_count]);
    if (r.artifact) rows.push(['artifact', r.artifact.path + '  (' + r.artifact.size_bytes + ' B)']);
    let html = rows.map(([k, v]) => `<div class="row"><span class="k">${k}</span><span>${v}</span></div>`).join('');
    const pls = r.per_label_state || {};
    const keys = Object.keys(pls);
    if (keys.length) {
      html += '<h3 style="font-size:13px;margin:12px 0 4px;">Mean state per label</h3>' +
        '<table><tr><td style="text-align:left;">label</td><td>thirst</td><td>hunger</td>' +
        '<td>fatigue</td><td>bush dist</td><td>water dist</td></tr>' +
        keys.map(k => { const p = pls[k]; return p.n
          ? `<tr><td>${k} <span class="muted">(${p.n})</span></td><td>${p.thirst}</td>` +
            `<td>${p.hunger}</td><td>${p.fatigue}</td><td>${p.bush_dist}</td>` +
            `<td>${p.water_dist}</td></tr>` : ''; }).join('') +
        '<tr><td style="text-align:left;"><b>overall</b></td>' +
        `<td>${r.overall_means.thirst}</td><td>${r.overall_means.hunger}</td>` +
        `<td>${r.overall_means.fatigue}</td><td>${r.overall_means.bush_dist}</td>` +
        `<td>${r.overall_means.water_dist}</td></tr></table>`;
    }
    document.getElementById('results').innerHTML = html;
    const se = r.sim_eval;
    if (se && Object.keys(se).length) {
      let shtml = '<h3 style="font-size:13px;margin:12px 0 4px;">Behavioural eval ' +
        '(fresh deterministic seeds)</h3>' +
        '<table><tr><td style="text-align:left;">config</td><td>ticks</td>' +
        '<td>Forage</td><td>Drink</td><td>Rest</td><td>Wander</td><td>Observe</td>' +
        '<td>survived</td><td>final thirst</td><td>final hunger</td></tr>';
      for (const [name, a] of Object.entries(se)) {
        const ac = a.actions || {};
        shtml += `<tr><td>${name}</td><td>${a.ticks}</td><td>${ac.Forage || 0}</td>` +
          `<td>${ac.Drink || 0}</td><td>${ac.Rest || 0}</td><td>${ac.Wander || 0}</td>` +
          `<td>${ac.Observe || 0}</td><td>${a.survived}/${a.seeds}</td>` +
          `<td>${a.final_thirst}</td><td>${a.final_hunger}</td></tr>`;
      }
      document.getElementById('results').innerHTML += shtml + '</table>';
    }
  } else {
    rc.hidden = true;
  }
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
        host = "127.0.0.1" if self.host in ("0.0.0.0", "") else self.host
        return f"http://{host}:{self.port}"

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