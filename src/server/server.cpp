#include "server/server.hpp"

#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <unordered_map>
#include <unistd.h>

#include "httplib.h"
#include "mind/compute_scheduler.hpp"

namespace eidolon {

namespace {
const char* kIndexHtml = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Eidolon</title>
<style>
  :root { color-scheme: light; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
         sans-serif; background: #fff; color: #0d0d0d; display: flex; height: 100vh; }
  #sidebar { width: 260px; background: #f7f7f8; border-right: 1px solid #e5e5e5;
             display: flex; flex-direction: column; }
  #sidehead { padding: 12px; display: flex; flex-direction: column; gap: 8px;
              border-bottom: 1px solid #ececec; }
  #newchat { width: 100%; padding: 10px 12px; border: 1px solid #d9d9e3; border-radius: 8px;
             background: #fff; color: #0d0d0d; font-size: 14px; cursor: pointer; }
  #newchat:hover { background: #f0f0f4; }
  #newworld { width: 100%; padding: 8px 12px; border: 1px solid #d9d9e3; border-radius: 8px;
              background: #ececf1; color: #0d0d0d; font-size: 13px; cursor: pointer; }
  #newworld:hover { background: #e3e3ea; }
  #saveprior { width: 100%; padding: 8px 12px; margin-top: 4px; border: 1px dashed #10a37f;
    border-radius: 8px; background: #f7f7f8; color: #10a37f; cursor: pointer; }
  #saveprior:hover { background: #e7f7f2; }
  #diagbtn { width: 100%; padding: 8px 12px; margin-top: 4px; border: 1px solid #d9d9e3;
    border-radius: 8px; background: #ececf1; color: #0d0d0d; cursor: pointer; font-size: 13px; }
  #diagbtn:hover { background: #e3e3ea; }
  #offloadbtn { width: 100%; padding: 8px 12px; margin-top: 4px; border: 1px solid #10a37f;
    border-radius: 8px; background: #ececf1; color: #0d0d0d; cursor: pointer; font-size: 13px; }
  #offloadbtn.on { background: #e7f7f2; color: #10a37f; border-color: #10a37f; }
  #diag { display: none; padding: 12px; border-top: 1px solid #ececec; background: #fafafa;
          font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 12px;
          white-space: pre; overflow-x: auto; max-height: 40vh; overflow-y: auto; }
  #convlist { flex: 1; overflow-y: auto; padding: 8px; }
  .conv { display: flex; align-items: center; padding: 8px 10px; border-radius: 8px;
          cursor: pointer; font-size: 13px; gap: 6px; color: #0d0d0d; }
  .conv:hover { background: #ececf1; }
  .conv.active { background: #e0e0ea; }
  .conv .title { flex: 1; overflow: hidden; text-overflow: ellipsis;
                 white-space: nowrap; }
  .conv .del { border: 0; background: none; cursor: pointer; color: #8e8ea0;
               font-size: 14px; padding: 0 2px; border-radius: 4px; }
  .conv .del:hover { color: #d0312d; background: #fff; }
  #main { flex: 1; display: flex; flex-direction: column; min-width: 0; }
  #statusbar { padding: 8px 16px; font-size: 12px; color: #6b6b70;
               border-bottom: 1px solid #ececec; background: #fafafa; }
  #chat { flex: 1; overflow-y: auto; background: #f9f9f9; }
  #chat .col { max-width: 760px; margin: 0 auto; padding: 24px 16px;
               display: flex; flex-direction: column; gap: 16px; }
  .msg { display: flex; gap: 12px; align-items: flex-start; }
  .msg.user { flex-direction: row-reverse; }
  .avatar { width: 30px; height: 30px; border-radius: 50%; flex: none;
            display: flex; align-items: center; justify-content: center;
            font-size: 13px; font-weight: 600; }
  .msg.user .avatar { background: #10a37f; color: #fff; }
  .msg.organism .avatar { background: #ececf1; color: #0d0d0d; }
  .bubble { padding: 10px 14px; border-radius: 14px; white-space: pre-wrap;
            line-height: 1.5; font-size: 14px; max-width: 100%; }
  .msg.user .bubble { background: #0d0d0d; color: #fff;
                      border-bottom-right-radius: 4px; }
  .msg.organism .bubble { background: #fff; color: #0d0d0d; border: 1px solid #ececec;
                          border-bottom-left-radius: 4px; }
  #inputrow { padding: 12px 16px; background: #fff; border-top: 1px solid #ececec; }
  #inputwrap { max-width: 760px; margin: 0 auto; display: flex; gap: 8px;
               align-items: flex-end; }
  #input { flex: 1; resize: none; background: #f7f7f8; color: #0d0d0d;
           border: 1px solid #e5e5e5; border-radius: 12px; padding: 12px 14px;
           font-family: inherit; font-size: 14px; line-height: 1.4; max-height: 200px;
           outline: none; }
  #input:focus { border-color: #10a37f; background: #fff; }
  #send { background: #10a37f; color: #fff; border: 0; border-radius: 12px;
          padding: 12px 18px; cursor: pointer; font-size: 14px; font-weight: 600; }
  #send:hover { background: #0e8f6f; }
  #send:disabled { opacity: .5; cursor: default; }
  .typing { opacity: .6; }
  @media (max-width: 700px) { #sidebar { width: 200px; } }
</style>
</head>
<body>
<div id="sidebar">
  <div id="sidehead">
    <button id="newchat">+ New chat</button>
    <button id="newworld">Restart world (fresh organism)</button>
    <button id="saveprior" title="Save this organism's learned behaviour as a prior for future runs">Save this organism as prior</button>
    <button id="diagbtn">Diagnostics</button>
    <button id="offloadbtn" title="Run the simulation in this browser tab (WASM worker) instead of on the server">Compute: server</button>
  </div>
  <div id="convlist"></div>
  <div id="diag">loading…</div>
</div>
<div id="main">
  <div id="statusbar">connecting…</div>
  <div id="chat"><div class="col" id="chatcol"></div></div>
  <div id="inputrow">
    <div id="inputwrap">
      <textarea id="input" rows="1" placeholder="Message the organism…" autocomplete="off"></textarea>
      <button id="send">Send</button>
    </div>
  </div>
</div>
<script>
const chat = document.getElementById('chat');
const chatcol = document.getElementById('chatcol');
const input = document.getElementById('input');
const sendBtn = document.getElementById('send');
const convsEl = document.getElementById('convlist');
let convId = null;

function esc(s) {
  const d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}
async function refreshStatus() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    document.getElementById('statusbar').textContent =
      `Day ${s.day} · hour ${s.hour.toFixed(1)} · ${s.awake ? 'awake' : s.sleepStage} · ` +
      `energy ${s.energy.toFixed(0)} · hunger ${s.hunger.toFixed(0)} · ` +
      `thirst ${s.thirst.toFixed(0)} · health ${s.health.toFixed(0)} · ` +
      `rebirths ${s.rebirths} · ` +
      `${s.weather} ${s.tempC.toFixed(1)}C`;
  } catch (e) {}
}
async function refreshConvs() {
  try {
    const r = await fetch('/api/conversations');
    const list = await r.json();
    convsEl.innerHTML = '';
    for (const c of list) {
      const el = document.createElement('div');
      el.className = 'conv' + (c.id === convId ? ' active' : '');
      el.dataset.id = c.id;
      const title = document.createElement('span');
      title.className = 'title';
      title.textContent = c.title || `conversation ${c.id}`;
      const del = document.createElement('button');
      del.className = 'del';
      del.textContent = '✕';
      del.title = 'Delete chat';
      del.onclick = async (e) => {
        e.stopPropagation();
        if (!confirm('Delete this chat?')) return;
        await fetch('/api/conversations/delete', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({conversation_id: String(c.id)})
        });
        if (convId === c.id) { convId = null; chatcol.innerHTML = ''; }
        await refreshConvs();
      };
      el.onclick = () => selectConv(c.id);
      el.appendChild(title);
      el.appendChild(del);
      convsEl.appendChild(el);
    }
  } catch (e) {}
}
function addMsg(role, text) {
  const wrap = document.createElement('div');
  wrap.className = 'msg ' + role;
  const av = document.createElement('div');
  av.className = 'avatar';
  av.textContent = role === 'user' ? 'You' : 'E';
  const b = document.createElement('div');
  b.className = 'bubble';
  b.textContent = text;
  wrap.appendChild(av);
  wrap.appendChild(b);
  chatcol.appendChild(wrap);
  chat.scrollTop = chat.scrollHeight;
  return b;
}
async function selectConv(id) {
  convId = id;
  await refreshConvs();
  const r = await fetch(`/api/messages?conversation_id=${id}`);
  const msgs = await r.json();
  chatcol.innerHTML = '';
  for (const m of msgs) addMsg(m.role === 'user' ? 'user' : 'organism', m.text);
}
async function newChat() {
  const r = await fetch('/api/conversations/new', {method: 'POST'});
  const j = await r.json();
  if (j.conversation_id) {
    convId = j.conversation_id;
    chatcol.innerHTML = '';
    await refreshConvs();
  }
}
async function resetWorld() {
   if (!confirm('Start a fresh world with a brand-new organism? Current life and world are replaced.')) return;
   if (computeOn) { await stopClientCompute(); computeOn = false; offloadBtn.classList.remove('on'); offloadBtn.textContent = 'Compute: server'; }
   await fetch('/api/world/reset', {
     method: 'POST', headers: {'Content-Type': 'application/json'}, body: '{}'
   });
   await refreshStatus();
   await newChat();
 }
async function savePrior() {
   const name = prompt('Name this organism (letters, numbers, - and _). It becomes a reusable prior '
     + 'loaded on future fresh runs:');
   if (name == null) return;
   const r = await fetch('/api/save-prior', {
     method: 'POST', headers: {'Content-Type': 'application/json'},
     body: JSON.stringify({name: name.trim()})
   });
   const j = await r.json();
   alert(j.ok ? 'Saved prior to ' + j.path : 'Save failed: ' + (j.error || 'unknown error'));
 }
function fmtUs(us) {
  if (us < 1000) return us.toFixed(0) + ' us';
  if (us < 1000000) return (us / 1000).toFixed(2) + ' ms';
  return (us / 1000000).toFixed(2) + ' s';
}
async function refreshDiag() {
  const el = document.getElementById('diag');
  try {
    const r = await fetch('/api/metrics');
    const m = await r.json();
    const lines = [];
    lines.push('SCHEDULER');
    lines.push(`  pending=${m.scheduler.pending} messages=${m.scheduler.messages} total=${fmtUs(m.scheduler.total_wall_us)}`);
    for (const [name, d] of Object.entries(m.scheduler.domains)) {
      lines.push(`  ${name.padEnd(18)} samples=${String(d.samples).padStart(6)}` +
        ` total=${fmtUs(d.total_us).padStart(11)} last=${fmtUs(d.last_us).padStart(11)} peak=${fmtUs(d.peak_us).padStart(11)}`);
    }
    lines.push('ACTIONS');
    const a = m.stats;
    lines.push(`  fine=${a.ticks_fine} coarse=${a.ticks_coarse} sleep=${a.ticks_sleep}`);
    lines.push(`  wander=${a.wander} rest=${a.rest} sleep=${a.sleep} observe=${a.observe} forage=${a.forage} drink=${a.drink} flee=${a.flee}`);
    lines.push(`  predator_attacks=${a.predator_attacks} berries=${a.berries_eaten} drinks=${a.drinks} wounds=${a.wounds} infections=${a.infections}`);
    lines.push('LEARNER');
    lines.push(`  inferences=${m.learner.inferences} updates=${m.learner.updates}`);
    el.textContent = lines.join('\n');
  } catch (e) {
    el.textContent = 'metrics unavailable: ' + e;
  }
}
let diagTimer = null;
function toggleDiag() {
  const el = document.getElementById('diag');
  const show = el.style.display === 'none' || el.style.display === '';
  el.style.display = show ? 'block' : 'none';
  if (show) {
    refreshDiag();
    diagTimer = setInterval(refreshDiag, 5000);
  } else {
    clearInterval(diagTimer);
  }
}
function autoGrow() {
  input.style.height = 'auto';
  input.style.height = Math.min(input.scrollHeight, 200) + 'px';
}
async function send() {
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  autoGrow();
  sendBtn.disabled = true;
  addMsg('user', text);
  const thinking = addMsg('organism', '…');
  thinking.classList.add('typing');
  try {
    const r = await fetch('/api/send', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({message: text, conversation_id: convId ? String(convId) : ''})
    });
    const j = await r.json();
    thinking.textContent = j.reply || j.error || '(no reply)';
    thinking.classList.remove('typing');
    if (j.conversation_id && convId !== j.conversation_id) {
      convId = j.conversation_id;
      await refreshConvs();
    }
  } catch (e) {
    thinking.textContent = 'connection error';
    thinking.classList.remove('typing');
  }
  sendBtn.disabled = false;
  chat.scrollTop = chat.scrollHeight;
}
// ---------------------------------------------------------------------------
// Phase 15: client-side compute. A capable tab can take over the tick loop in a
// Web Worker (WASM ReplicaCore, bit-exact with the server core) — the server then
// persists snapshots and handles chat only. If this tab closes or stalls, the
// server automatically resumes the sim after ~15s of silence (continuity invariant).
// ---------------------------------------------------------------------------
const offloadBtn = document.getElementById('offloadbtn');
let computeWorker = null;
let computeOn = false;
let postBusy = false;

function detectCaps() {
  // Minimal SIMD128 probe (i32x4.splat of 0); bytes produced by binaryen wasm-as,
  // verified to validate in V8 and Node.
  let simd = false;
  try {
    simd = WebAssembly.validate(new Uint8Array([
      0,97,115,109,1,0,0,0,1,5,1,96,0,1,123,3,2,1,0,10,8,1,6,0,65,0,253,17,11]));
  } catch (e) {}
  const sab = (typeof SharedArrayBuffer !== 'undefined') && (self.crossOriginIsolated === true);
  return {
    wasm_simd128: simd ? 1 : 0,
    shared_array_buffer: sab ? 1 : 0,
    max_workers: navigator.hardwareConcurrency || 1,
    thread_support: sab ? 3 : 1,
    max_memory_bytes: (performance.memory && performance.memory.jsHeapSizeLimit) || 256*1024*1024,
    preferred_memory_bytes: 256*1024*1024,
    estimated_sim_steps_per_sec: 1000,
    estimated_inferences_per_sec: 100,
    user_agent: navigator.userAgent,
    platform: navigator.platform || ''
  };
}

function onWorkerMsg(ev) {
  const m = ev.data;
  if (m.kind === 'snapshot') {
    if (postBusy) return; // drop rather than queue: heartbeats are frequent
    postBusy = true;
    fetch('/api/client/snapshot', { method: 'POST', body: m.buffer })
      .catch(() => {})
      .finally(() => { postBusy = false; });
  } else if (m.kind === 'dead') {
    computeOn = false;
    offloadBtn.classList.remove('on');
    offloadBtn.textContent = 'Compute: server';
    alert('The organism has died. The server will resume the simulation and rebirth it shortly.');
    computeWorker.terminate(); computeWorker = null;
  } else if (m.kind === 'error') {
    alert('Client compute failed: ' + m.error + ' — handing control back to the server.');
    stopClientCompute();
  }
}

function workerCall(worker, msg, transfer) {
  return new Promise((resolve, reject) => {
    const prev = worker.onmessage;
    const to = setTimeout(() => reject(new Error('worker timeout')), 15000);
    worker.onmessage = (ev) => {
      // 'ready'/'stopped' resolve; pass everything else through to the relay.
      if (ev.data.kind === 'ready' || ev.data.kind === 'stopped') {
        clearTimeout(to);
        worker.onmessage = prev;
        resolve(ev.data);
      } else {
        prev(ev);
      }
    };
    worker.onerror = (e) => { clearTimeout(to); reject(new Error(e.message || 'worker error')); };
    worker.postMessage(msg, transfer || []);
  });
}

async function startClientCompute() {
  const prof = detectCaps();
  const r = await fetch('/api/compute-profile', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(prof) });
  const j = await r.json();
  const backend = j.selection && j.selection.backend; // 2=simd, 3=simd+mt take over
  if (backend !== 2 && backend !== 3) {
    alert('This browser cannot take over the sim: ' + ((j.selection && j.selection.reason) || 'no SIMD backend'));
    return false;
  }
  const snapResp = await fetch('/api/snapshot/download');
  if (!snapResp.ok) { alert('Could not download organism state.'); return false; }
  const snap = await snapResp.arrayBuffer();
  computeWorker = new Worker('/api/wasm/worker.js');
  computeWorker.onmessage = onWorkerMsg;
  await workerCall(computeWorker, { kind: 'init', snapshot: snap }, [snap]);
  const tps = Math.round((j.fidelity && j.fidelity.sim_sec_per_wall_sec) || 300);
  computeWorker.postMessage({ kind: 'start', tps });
  return true;
}

async function stopClientCompute() {
  if (computeWorker) {
    try { await workerCall(computeWorker, { kind: 'stop' }); } catch (e) {}
    computeWorker.terminate();
    computeWorker = null;
  }
  // Disarm server-side offload so the local loop resumes immediately.
  try {
    await fetch('/api/compute-profile', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ offload: false, wasm_simd128: 0, max_workers: 1 }) });
  } catch (e) {}
}

offloadBtn.onclick = async () => {
  offloadBtn.disabled = true;
  if (!computeOn) {
    offloadBtn.textContent = 'Compute: starting…';
    try {
      if (await startClientCompute()) {
        computeOn = true;
        offloadBtn.classList.add('on');
        offloadBtn.textContent = 'Compute: THIS TAB';
      } else {
        offloadBtn.textContent = 'Compute: server';
      }
    } catch (e) {
      offloadBtn.textContent = 'Compute: server';
      alert('Client compute could not start: ' + e.message);
    }
  } else {
    await stopClientCompute();
    computeOn = false;
    offloadBtn.classList.remove('on');
    offloadBtn.textContent = 'Compute: server';
  }
  offloadBtn.disabled = false;
};

// If the tab closes while it holds the sim, tell the server to resume immediately.
window.addEventListener('beforeunload', () => {
  if (computeOn) {
    navigator.sendBeacon('/api/compute-profile',
      new Blob([JSON.stringify({ offload: false, wasm_simd128: 0, max_workers: 1 })],
               { type: 'application/json' }));
  }
});

sendBtn.onclick = send;
document.getElementById('newchat').onclick = newChat;
document.getElementById('newworld').onclick = resetWorld;
document.getElementById('saveprior').onclick = savePrior;
document.getElementById('diagbtn').onclick = toggleDiag;
input.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); }
});
input.addEventListener('input', autoGrow);
(async () => {
  await refreshConvs();
  if (convId === null && convsEl.children.length > 0) {
    await selectConv(parseInt(convsEl.children[0].dataset.id));
  } else if (convsEl.children.length === 0) {
    await newChat();
  }
  refreshStatus();
  setInterval(refreshStatus, 5000);
})();
</script>
</body>
</html>)html";

// Phase 15: browser Web Worker script. The page spawns this as a Worker; it loads the
// WASM ReplicaCore module, restores the server's snapshot, ticks at the negotiated
// fidelity pacing, and posts one snapshot per heartbeat interval back to the page
// (which relays it to POST /api/client/snapshot). Adaptive slice sizing keeps the
// worker responsive on weak clients; death stops the loop and asks the page to hand
// control back to the server (which auto-rebirths on resume).
const char* kWorkerJs = R"js(
"use strict";
let M = null;        // emscripten module
let eng = 0;         // Engine* handle
let timer = null;
let ticksPerSec = 300;      // fidelity pacing: sim ticks per wall-second
let sliceBudget = 300;      // ticks per 1s slice (adaptive)
const UPLOAD_EVERY_MS = 1000;
let lastUpload = 0;
let modulePromise = null;

function loadModule() {
  if (!modulePromise) {
    importScripts('/api/wasm/core.js');
    modulePromise = EidolonWorker({ locateFile: () => '/api/wasm/core.wasm' });
  }
  return modulePromise;
}

function snapshotBytes() {
  const n = M._eidn_snapshot_size(eng);
  if (!n) return null;
  const p = M._malloc(n);
  const wrote = M._eidn_snapshot(eng, p, n);
  if (wrote !== n) { M._free(p); return null; }
  const out = M.HEAPU8.slice(p, p + n).buffer;
  M._free(p);
  return out;
}

// ----- offline persistence (Phase 12): local IndexedDB checkpoint -------------
// A periodic binary checkpoint survives a tab crash / reload so the organism is not
// destroyed. On init we prefer the freshest local checkpoint when the server has no
// snapshot (offline resume); the uploaded server snapshot still wins when present.
const IDB_NAME = 'eidolon';
const IDB_STORE = 'checkpoints';
let idb = null;
function idbOpen() {
  if (idb) return idb;
  idb = new Promise((resolve, reject) => {
    const req = indexedDB.open(IDB_NAME, 1);
    req.onupgradeneeded = () => {
      if (!req.result.objectStoreNames.contains(IDB_STORE)) req.result.createObjectStore(IDB_STORE);
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  return idb;
}
function localSave(simtime, buf) {
  idbOpen().then((db) => {
    try {
      const tx = db.transaction(IDB_STORE, 'readwrite');
      tx.objectStore(IDB_STORE).put({ simtime, buf }, 'current');
    } catch (e) {}
  }).catch(() => {});
}
function localLoad() {
  return idbOpen().then((db) => new Promise((resolve) => {
    try {
      const tx = db.transaction(IDB_STORE, 'readonly');
      const req = tx.objectStore(IDB_STORE).get('current');
      req.onsuccess = () => resolve(req.result ? req.result.buf : null);
      req.onerror = () => resolve(null);
    } catch (e) { resolve(null); }
  })).catch(() => null);
}

let lastLocalSave = 0;
const LOCAL_SAVE_EVERY_MS = 5000;

function loop() {
  const t0 = performance.now();
  for (let i = 0; i < sliceBudget && M._eidn_alive(eng); ++i) M._eidn_tick(eng);
  const dt = performance.now() - t0;
  if (!M._eidn_alive(eng)) {
    clearInterval(timer); timer = null;
    const s = snapshotBytes();
    postMessage({ kind: 'dead', simtime: Number(M._eidn_simtime(eng)), snapshot: s },
                 s ? [s] : []);
    return;
  }
  // Adaptive slice: keep tick work below ~250ms per second; grow when cheap.
  if (dt > 250) sliceBudget = Math.max(10, (sliceBudget / 2) | 0);
  else if (dt < 120 && sliceBudget < ticksPerSec)
    sliceBudget = Math.min(ticksPerSec, Math.max(sliceBudget + 10, (sliceBudget * 1.25) | 0));
  const now = performance.now();
  if (now - lastUpload >= UPLOAD_EVERY_MS) {
    lastUpload = now;
    const s = snapshotBytes();
    if (s) postMessage({ kind: 'snapshot', simtime: Number(M._eidn_simtime(eng)), buffer: s }, [s]);
  }
  // Local IndexedDB checkpoint (survive tab crash), throttled separately.
  if (now - lastLocalSave >= LOCAL_SAVE_EVERY_MS) {
    lastLocalSave = now;
    const s = snapshotBytes();
    if (s) localSave(Number(M._eidn_simtime(eng)), s);
  }
  postMessage({ kind: 'stats', sliceMs: dt, slice: sliceBudget,
                simtime: Number(M._eidn_simtime(eng)) });
}

onmessage = async (ev) => {
  const m = ev.data;
  try {
    if (m.kind === 'init') {
      M = await loadModule();
      if (eng) { M._eidn_free(eng); eng = 0; }
      eng = M._eidn_new();
      if (!eng) { postMessage({ kind: 'error', error: 'alloc failed' }); return; }
      let restored = false;
      if (m.snapshot) {
        const b = new Uint8Array(m.snapshot);
        const p = M._malloc(b.length);
        M.HEAPU8.set(b, p);
        const ok = M._eidn_restore(eng, p, b.length);
        M._free(p);
        if (!ok) { postMessage({ kind: 'error', error: 'snapshot restore failed' }); return; }
        restored = true;
      } else if (m.allowLocalResume) {
        // Offline resume: server offered no snapshot — try the local IndexedDB checkpoint
        // first so a refreshed/reloaded tab continues the same organism offline.
        const local = await localLoad();
        if (local) {
          const b = new Uint8Array(local);
          const p = M._malloc(b.length);
          M.HEAPU8.set(b, p);
          const ok = M._eidn_restore(eng, p, b.length);
          M._free(p);
          if (ok) restored = true;
        }
      }
      if (!restored) {
        if (m.seed !== undefined) {
          M._eidn_init(eng, BigInt(m.seed), m.deterministic ? 1 : 0, m.w || 128, m.h || 128);
        } else {
          postMessage({ kind: 'error', error: 'init needs snapshot, local checkpoint, or seed' });
          return;
        }
      }
      postMessage({ kind: 'ready', simtime: Number(M._eidn_simtime(eng)),
                    seed: String(M._eidn_seed(eng)), resumedLocal: restored });
    } else if (m.kind === 'start') {
      ticksPerSec = m.tps || 300;
      sliceBudget = Math.min(300, ticksPerSec);
      if (timer) clearInterval(timer);
      timer = setInterval(loop, 1000);
    } else if (m.kind === 'stop') {
      if (timer) { clearInterval(timer); timer = null; }
      postMessage({ kind: 'stopped' });
    }
  } catch (e) {
    postMessage({ kind: 'error', error: String(e) });
  }
};
)js";

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Derive a fresh master seed from entropy sources (system clock, random_device, pid).
// Used when the user does not pass --seed so every restart/start yields a new world.
uint64_t entropySeed() {
  std::random_device rd;
  const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t s = static_cast<uint64_t>(t) ^ (static_cast<uint64_t>(rd()) << 1);
  s ^= static_cast<uint64_t>(::getpid()) << 33;
  return s;
}

// Peek the sim-time (absolute sim seconds) from a snapshot blob WITHOUT fully restoring
// it, so reconnect reconcile can reject a stale client snapshot before it rolls back the
// headless fallback's forward progress. The snapshot layout is fixed by the versioned
// format: magic(u32) version(u32) checksum(u64) .. then Engine::serializeState writes
// masterSeed(u64) deterministic(u8) clock.now(i64) first. Returns false if the header is
// malformed or unexpected (caller then falls back to a full restore/validate path).
bool peekSnapshotSimTime(const std::vector<uint8_t>& blob, int64_t& outSimTime) {
  if (blob.size() < 25) return false;
  const uint8_t* p = blob.data();
  auto u32 = [&](const uint8_t* q) {
    return static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
           (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[3]) << 24);
  };
  if (u32(p) != kSnapshotMagic) return false;
  if (u32(p + 4) != kSnapshotVersion) return false;
  // payload starts at offset 16; skip masterSeed(u64 @16) + deterministic(u8 @24).
  const uint8_t* q = p + 25;
  uint64_t t = 0;
  for (int i = 0; i < 8; ++i) t |= static_cast<uint64_t>(q[i]) << (8 * i);
  outSimTime = static_cast<int64_t>(t);
  return true;
}
} // namespace

Server::Server(Options opts) : opts_(std::move(opts)) {
  std::error_code ec;
  std::filesystem::create_directories(opts_.dataDir, ec);
  std::string err;
  archive_ = std::make_unique<SQLiteArchive>(opts_.dataDir + "/memory.db", err);
  if (!archive_) {
    std::fprintf(stderr, "warning: archive unavailable: %s\n", err.c_str());
  }
  llm_ = std::make_unique<LLMBridge>(opts_.llmEndpoint, opts_.llmTimeoutMs);
  // Internet access (Future Directions): configurable, user-gated browsing
  if (opts_.internetEnabled) {
    BrowserConfig bcfg;
    bcfg.enabled = true;
    bcfg.searchEndpoint = opts_.searchEndpoint;
    bcfg.searchApiKey = opts_.searchApiKey;
    bcfg.maxResults = opts_.maxSearchResults;
    bcfg.maxFetchChars = opts_.maxFetchChars;
    bcfg.timeoutMs = opts_.browseTimeoutMs;
    bcfg.allowFetch = true;
    browser_ = std::make_unique<HttpWebBrowser>(bcfg);
    std::fprintf(stderr, "internet access: ENABLED (max_results=%u, fetch_chars=%u)\n",
                 opts_.maxSearchResults, opts_.maxFetchChars);
  } else {
    std::fprintf(stderr, "internet access: disabled (use --internet-enabled to enable)\n");
  }
}

Server::~Server() {
  stop_.store(true);
  if (simThread_.joinable()) simThread_.join();
}

void Server::simLoop() {
  const std::string savePath = opts_.dataDir + "/save.snap";
  const std::string logPath = opts_.dataDir + "/events.log";
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    bool resumed = false;
    if (std::filesystem::exists(savePath)) {
      std::string err;
      if (engine_.loadFile(savePath, err)) {
        resumed = true;
      } else {
        std::fprintf(stderr, "warning: cannot load snapshot (%s); starting fresh\n",
                     err.c_str());
      }
    }
    if (!resumed) {
      const uint64_t seed = opts_.seed != 0 ? opts_.seed : entropySeed();
      engine_.init(seed, opts_.deterministic, opts_.worldW, opts_.worldH);
      if (!opts_.policyPriorPath.empty() &&
          !engine_.loadPolicyPrior(opts_.policyPriorPath)) {
        std::fprintf(stderr, "warning: cannot load policy prior %s; using random init\n",
                     opts_.policyPriorPath.c_str());
      } else if (!opts_.policyPriorPath.empty()) {
        std::fprintf(stderr, "policy prior loaded from %s (online learning continues)\n",
                     opts_.policyPriorPath.c_str());
      }
      if (!opts_.heredityPath.empty()) {
        engine_.setHeredityPath(opts_.heredityPath, opts_.heredityWeight);
        if (!engine_.loadHeredity()) {
          std::fprintf(stderr, "warning: could not load heredity from %s (starting fresh)\n",
                       opts_.heredityPath.c_str());
        } else {
          std::fprintf(stderr, "heredity loaded from %s (weight %.2f)\n",
                       opts_.heredityPath.c_str(), opts_.heredityWeight);
        }
      }
      std::fprintf(stderr, "fresh organism: seed=%llu world=%dx%d\n",
                   static_cast<unsigned long long>(engine_.masterSeed()),
                   opts_.worldW, opts_.worldH);
    }
    engine_.setArchive(archive_.get());
    if (!log_.open(logPath)) {
      std::fprintf(stderr, "warning: cannot open event log %s\n", logPath.c_str());
    }
    if (!resumed) {
      log_.line(engine_.clock().now(), "birth", "world seed=%llu",
                static_cast<unsigned long long>(engine_.masterSeed()));
    }
  }
  int64_t lastAutosave = 0;
  const FidelitySettings fidelity = [&] {
    if (opts_.fidelityLevel == 1) return FidelityController::settingsFor(FidelityLevel::Low);
    if (opts_.fidelityLevel == 2) return FidelityController::settingsFor(FidelityLevel::Medium);
    if (opts_.fidelityLevel == 3) return FidelityController::settingsFor(FidelityLevel::High);
    // Auto: default to Medium for the native server (client profiles arrive later).
    return FidelityController::settingsFor(FidelityLevel::Medium);
  }();
  fidelity_ = fidelity;
  // Real-time pacing: one sim-second per (1e6 / simSecondsPerWallSecond) microseconds of
  // wall time; the native server default is 500x. A constrained client drops this so the
  // sim covers less ground per wall-second, without changing the tick itself.
  const int64_t pacingUs =
      static_cast<int64_t>(1e6 / fidelity.simSecondsPerWallSecond);
  std::fprintf(stderr, "fidelity: %s\n", fidelity.toString().c_str());
  while (!stop_.load()) {
    // Phase 15: when a capable client has taken over the simulation, the server
    // stops ticking locally — it only persists client snapshots, answers chat,
    // and serves state. Continuity invariant: if the claiming client goes silent
    // for >15s (tab closed/crashed), the server resumes the sim itself.
    if (clientComputing_.load()) {
      const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      const int64_t last = lastClientContactMs_.load();
      if (last > 0 && nowMs - last > 15000) {
        clientComputing_.store(false);
        std::fprintf(stderr, "client offload: no snapshots for >15s; server resumes the sim\n");
        continue;
      }
      // Periodic wall-clock save while offloaded (crash safety; client uploads
      // themselves don't hit the disk to keep uploads cheap).
      static int64_t lastIdleSaveMs = 0;
      if (nowMs - lastIdleSaveMs >= 60000) {
        lastIdleSaveMs = nowMs;
        std::lock_guard<std::mutex> lock(engineMu_);
        std::string err;
        if (!engine_.saveFile(savePath, err)) {
          std::fprintf(stderr, "warning: offloaded save failed: %s\n", err.c_str());
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    {
      // Scope the lock so HTTP handler threads always get a window to acquire the
      // mutex; holding it across the pacing sleep below would starve them out.
      std::lock_guard<std::mutex> lock(engineMu_);
      engine_.tickAndLog(log_);
      if (!engine_.isAlive()) {
        // Auto-rebirth: fresh organism inherits the world, optionally with heredity
        log_.line(engine_.clock().now(), "death", "organism died; auto-rebirth");
        std::string err;
        if (!engine_.saveFile(savePath, err)) {
          std::fprintf(stderr, "warning: death save failed: %s\n", err.c_str());
        }
        log_.flush();

        // Generate new seed for fresh organism (entropy if not deterministic)
        const uint64_t newSeed = opts_.deterministic ? (opts_.seed != 0 ? opts_.seed : entropySeed())
                                                     : entropySeed();
        engine_.init(newSeed, opts_.deterministic, opts_.worldW, opts_.worldH);
        if (!opts_.policyPriorPath.empty() &&
            !engine_.loadPolicyPrior(opts_.policyPriorPath)) {
          std::fprintf(stderr, "warning: cannot load policy prior %s; using random init\n",
                       opts_.policyPriorPath.c_str());
        }
        if (!opts_.heredityPath.empty()) {
          engine_.setHeredityPath(opts_.heredityPath, opts_.heredityWeight);
          if (!engine_.loadHeredity()) {
            std::fprintf(stderr, "warning: could not load heredity from %s (starting fresh)\n",
                         opts_.heredityPath.c_str());
          } else {
            std::fprintf(stderr, "heredity loaded from %s (weight %.2f)\n",
                         opts_.heredityPath.c_str(), opts_.heredityWeight);
          }
        }
        engine_.setArchive(archive_.get());
        log_.line(engine_.clock().now(), "birth",
                  "auto-rebirth seed=%llu", static_cast<unsigned long long>(newSeed));
        lastAutosave = engine_.clock().now();
        std::fprintf(stderr, "auto-rebirth: new organism seed=%llu\n",
                     static_cast<unsigned long long>(newSeed));
      }
      if (engine_.clock().now() - lastAutosave >= 600) { // autosave every 10 sim-minutes
        lastAutosave = engine_.clock().now();
        std::string err;
        if (!engine_.saveFile(savePath, err)) {
          std::fprintf(stderr, "warning: autosave failed: %s\n", err.c_str());
        }
        log_.flush();
      }
    }
    // Real-time pacing: simSecondsPerWallSecond wall pacing (500x default); a constrained
    // client (Low/Medium fidelity) slows this so the sim covers less ground per wall-second.
    std::this_thread::sleep_for(std::chrono::microseconds(pacingUs));
  }
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    log_.flush();
    std::string err;
    engine_.saveFile(savePath, err);
  }
}

void Server::autosave() {
  std::string err;
  if (!engine_.saveFile(opts_.dataDir + "/save.snap", err)) {
    std::fprintf(stderr, "warning: save failed: %s\n", err.c_str());
  }
  log_.flush();
}

int64_t Server::currentConversation() {
  if (conversationId_ < 0 && archive_) {
    conversationId_ = archive_->createConversation("chat", 0);
  }
  return conversationId_;
}

std::string Server::statusJson() {
  std::lock_guard<std::mutex> lock(engineMu_);
  const auto& b = engine_.body();
  const auto& w = engine_.world().weather();
  const Vec2i p = engine_.world().organismPos();
  char buf[640];
  static const char* kSleepStageName[] = {"awake", "drowsy", "light_sleep", "deep_sleep", "rem"};
  std::snprintf(buf, sizeof(buf),
                "{\"day\":%lld,\"hour\":%.1f,\"awake\":%s,\"alive\":%s,"
                "\"sleepStage\":\"%s\","
                "\"energy\":%.1f,\"hunger\":%.1f,\"thirst\":%.1f,\"fatigue\":%.1f,"
                "\"sleepP\":%.1f,\"health\":%.1f,\"bodyTemp\":%.1f,\"weather\":\"%s\","
                "\"tempC\":%.1f,\"simTime\":%lld,"
                "\"preyNear\":%d,\"predatorsNear\":%d,\"predatorDist\":%d,"
                "\"rebirths\":%u}",
                static_cast<long long>(engine_.clock().day()),
                engine_.clock().hourOfDay(),
                b.isSleeping() ? "false" : "true",
                engine_.isAlive() ? "true" : "false",
                kSleepStageName[static_cast<uint8_t>(b.sleepStage())],
                b.energy(), b.hunger(), b.thirst(),
                b.fatigue(), b.sleepPressure(), b.health(), b.bodyTemp(),
                w.describe(), w.ambientTempC(engine_.clock()),
                static_cast<long long>(engine_.clock().now()),
                engine_.world().preyCount(p, Perception::kSightRadius),
                engine_.world().predatorCount(p, Perception::kSightRadius),
                [&] {
                  const WildlifeAgent* pr = engine_.world().nearestPredator(
                      p, Perception::kSightRadius);
                  return pr ? distCheb(pr->pos, p) : -1;
                }(),
                engine_.rebirthCount());
  return buf;
}

std::string Server::metricsJson() {
  std::lock_guard<std::mutex> lock(engineMu_);
  const auto& sched = engine_.scheduler();
  const auto& stats = engine_.stats();
  const auto& learnMetrics = engine_.learn().metrics();

  JsonValue doms = JsonValue::makeObject();
  static const char* kDomains[4] = {"World", "PhysioCognition", "NeuralMl",
                                    "MemoryConsolidation"};
  for (size_t i = 0; i < 4; ++i) {
    const auto& p = sched.profile(static_cast<WorkerDomain>(i));
    JsonValue d = JsonValue::makeObject();
    d.setNumber("samples", static_cast<double>(p.samples));
    d.setNumber("total_us", p.totalWallUs);
    d.setNumber("last_us", p.lastWallUs);
    d.setNumber("peak_us", p.peakWallUs);
    doms.set(kDomains[i], std::move(d));
  }
  JsonValue schedObj = JsonValue::makeObject();
  schedObj.setNumber("pending", static_cast<double>(sched.pendingCount()));
  schedObj.setNumber("messages", static_cast<double>(sched.messageCount()));
  schedObj.setNumber("total_wall_us", sched.totalWallUs());
  schedObj.set("domains", std::move(doms));

  JsonValue st = JsonValue::makeObject();
  st.setNumber("ticks_fine", static_cast<double>(stats.ticksFine));
  st.setNumber("ticks_coarse", static_cast<double>(stats.ticksCoarse));
  st.setNumber("ticks_sleep", static_cast<double>(stats.ticksSleep));
  st.setNumber("wander", static_cast<double>(stats.actionsWander));
  st.setNumber("rest", static_cast<double>(stats.actionsRest));
  st.setNumber("sleep", static_cast<double>(stats.actionsSleep));
  st.setNumber("observe", static_cast<double>(stats.actionsObserve));
  st.setNumber("forage", static_cast<double>(stats.actionsForage));
  st.setNumber("drink", static_cast<double>(stats.actionsDrink));
  st.setNumber("flee", static_cast<double>(stats.actionsFlee));
  st.setNumber("predator_attacks", static_cast<double>(stats.predatorAttacks));
  st.setNumber("berries_eaten", static_cast<double>(stats.berriesEaten));
  st.setNumber("drinks", static_cast<double>(stats.drinks));
  st.setNumber("wounds", static_cast<double>(stats.woundsSustained));
  st.setNumber("infections", static_cast<double>(stats.infections));

  JsonValue lm = JsonValue::makeObject();
  lm.setNumber("inferences", static_cast<double>(learnMetrics.inferences));
  lm.setNumber("updates", static_cast<double>(learnMetrics.updates));

  JsonValue root = JsonValue::makeObject();
  JsonValue fid = JsonValue::makeObject();
  fid.setNumber("level", static_cast<double>(fidelity_.level));
  fid.setNumber("sim_sec_per_wall_sec", fidelity_.simSecondsPerWallSecond);
  fid.setNumber("model_budget", fidelity_.modelBudgetScale);
  fid.setNumber("world_detail", fidelity_.worldDetailScale);
  fid.setBool("llm_reflection", fidelity_.llmReflectionEnabled);
  fid.setBool("defer_consolidation", fidelity_.deferConsolidation);
  root.set("scheduler", std::move(schedObj));
  root.set("stats", std::move(st));
  root.set("learner", std::move(lm));
  root.set("fidelity", std::move(fid));
  return root.dump();
}

std::string Server::sendMessage(const std::string& conversationIdStr,
                                const std::string& text, std::string& err) {
  const std::string trimmed = [&] {
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\n' || text[a] == '\r')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\n' || text[b - 1] == '\r'))
      --b;
    return text.substr(a, b - a);
  }();
  if (trimmed.empty()) {
    err = "empty message";
    return "{}";
  }

  int64_t convId = -1;
  if (!conversationIdStr.empty()) {
    convId = std::strtoll(conversationIdStr.c_str(), nullptr, 10);
  }
  if (convId <= 0) convId = currentConversation();

  // Snapshot the current state for the reply (grounded in real state).
  CognitiveSnapshot snap;
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    snap = makeSnapshot(engine_);
  }

  if (archive_) {
    // Title the conversation from its first user message (ChatGPT-style).
    if (archive_->listMessages(convId, 1).empty()) {
      std::string title = trimmed;
      if (title.size() > 40) title = title.substr(0, 40) + "...";
      archive_->setConversationTitle(convId, title);
    }
    archive_->appendMessage(convId, "user", trimmed, snap.simTime);
  }

  std::string reply;
  if (llm_ && llm_->enabled()) {
    ParsedMessage parsed;
    std::string raw;
    if (llm_->parse(trimmed, snap, parsed, raw) &&
        llm_->respond(trimmed, snap, parsed, reply, raw)) {
      // success path
    } else {
      reply = fallbackReply(snap, trimmed);
    }
  } else {
    reply = fallbackReply(snap, trimmed);
  }

  if (archive_) {
    archive_->appendMessage(convId, "organism", reply, snap.simTime);
  }

  std::string out = "{\"conversation_id\":" + std::to_string(convId) +
                    ",\"reply\":\"" + jsonEscape(reply) + "\"}";
  return out;
}

std::string Server::conversationsJson() {
  if (!archive_) return "[]";
  const auto convs = archive_->listConversations();
  std::string out = "[";
  bool first = true;
  for (const auto& c : convs) {
    if (!first) out += ",";
    first = false;
    out += "{\"id\":" + std::to_string(c.id) + ",\"title\":\"" + jsonEscape(c.title) +
           "\",\"created_at\":" + std::to_string(c.createdAt) + "}";
  }
  return out + "]";
}

std::string Server::newConversationJson() {
  if (!archive_) return "{\"error\":\"no archive\"}";
  const int64_t id = archive_->createConversation("New chat",
                                                  engine_.clock().now());
  if (id < 0) return "{\"error\":\"create failed\"}";
  return "{\"conversation_id\":" + std::to_string(id) + "}";
}

std::string Server::deleteConversationJson(const std::string& conversationIdStr) {
  const int64_t convId = std::strtoll(conversationIdStr.c_str(), nullptr, 10);
  if (archive_ && convId > 0) {
    archive_->deleteConversation(convId);
    if (conversationId_ == convId) conversationId_ = -1;
  }
  return "{\"ok\":true}";
}

std::string Server::resetWorldJson(const std::string& seedStr) {
  uint64_t seed = std::strtoull(seedStr.c_str(), nullptr, 10);
  if (seedStr.empty() || seed == 0) {
    std::random_device rd;
    seed = (static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
                                      .time_since_epoch()
                                      .count()) ^
            (static_cast<uint64_t>(rd()) << 1) ^ (static_cast<uint64_t>(getpid()) << 33));
  }
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    engine_.init(seed, opts_.deterministic, opts_.worldW, opts_.worldH);
    if (!opts_.policyPriorPath.empty()) {
      engine_.loadPolicyPrior(opts_.policyPriorPath);
    }
    engine_.setArchive(archive_.get());
    log_.line(engine_.clock().now(), "birth",
              "world reset seed=%llu (fresh organism, fresh world)",
              static_cast<unsigned long long>(seed));
    log_.flush();
    std::string err;
    if (!engine_.saveFile(opts_.dataDir + "/save.snap", err)) {
      std::fprintf(stderr, "warning: reset save failed: %s\n", err.c_str());
    }
  }
  return statusJson();
}

std::string Server::savePriorJson(const std::string& name) {
  std::string safe;
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-') {
      safe += c;
    }
  }
  if (safe.empty()) safe = "organism";
  const std::string dir = opts_.dataDir + "/priors";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = dir + "/prior_" + safe + ".eprp";
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    ok = engine_.savePolicyPrior(path);
  }
  if (ok) {
    log_.line(engine_.clock().now(), "prior_saved",
              "user saved current organism policy prior to %s", path.c_str());
    log_.flush();
    return "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\"}";
  }
  return "{\"ok\":false,\"error\":\"failed to write prior\"}";
}

std::string Server::messagesJson(const std::string& conversationIdStr,
                                 std::string& err) {
  if (!archive_) return "[]";
  const int64_t convId = std::strtoll(conversationIdStr.c_str(), nullptr, 10);
  if (convId <= 0) {
    err = "bad conversation_id";
    return "[]";
  }
  const auto msgs = archive_->listMessages(convId);
  std::string out = "[";
  bool first = true;
  for (const auto& m : msgs) {
    if (!first) out += ",";
    first = false;
    out += "{\"id\":" + std::to_string(m.id) + ",\"role\":\"" + jsonEscape(m.role) +
           "\",\"text\":\"" + jsonEscape(m.text) + "\",\"t\":" +
           std::to_string(m.t) + "}";
  }
  return out + "]";
}

std::string Server::snapshotJson() {
  std::lock_guard<std::mutex> lock(engineMu_);
  std::vector<uint8_t> blob = engine_.snapshot();
  std::string out = "{\"size\":" + std::to_string(blob.size()) + ",\"seed\":" +
                      std::to_string(engine_.masterSeed()) + "}";
  return out;
}

// Phase 12: Binary snapshot download (raw blob)
std::string Server::snapshotDownload() {
  std::lock_guard<std::mutex> lock(engineMu_);
  return std::string(reinterpret_cast<const char*>(engine_.snapshot().data()),
                     engine_.snapshot().size());
}

// Phase 12: Binary snapshot upload
std::string Server::snapshotUpload(const std::vector<uint8_t>& blob, std::string& err) {
  std::lock_guard<std::mutex> lock(engineMu_);
  if (!engine_.restore(blob, err)) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  // Persist to disk immediately so the new state survives restarts
  std::string saveErr;
  engine_.saveFile(opts_.dataDir + "/save.snap", saveErr);
  return "{\"ok\":true}";
}

// Phase 12: Delta sync - simple checkpoint/delta protocol
// Checkpoints are identified by sim-time (clock.now()). A delta is a list of
// (offset, new_bytes) patches from base checkpoint to current state.
struct Checkpoint {
  int64_t simTime;
  std::vector<uint8_t> blob;
  std::vector<uint8_t> fnv1a64; // 8-byte hash
};
static std::unordered_map<std::string, Checkpoint> checkpoints_;

std::string Server::checkpointCreateJson() {
  std::lock_guard<std::mutex> lock(engineMu_);
  std::vector<uint8_t> blob = engine_.snapshot();
  uint64_t hash = fnv1a64(blob.data(), blob.size());
  std::string id = std::to_string(engine_.clock().now());
  checkpoints_[id] = {engine_.clock().now(), std::move(blob),
                      std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&hash),
                                           reinterpret_cast<uint8_t*>(&hash) + 8)};
  // Keep only last 10 checkpoints to bound memory
  if (checkpoints_.size() > 10) {
    auto it = checkpoints_.begin();
    checkpoints_.erase(it);
  }
  return "{\"ok\":true,\"checkpoint_id\":\"" + id + "\"}";
}

std::string Server::checkpointDeltaJson(const std::string& baseCheckpointId) {
  std::lock_guard<std::mutex> lock(engineMu_);
  auto it = checkpoints_.find(baseCheckpointId);
  if (it == checkpoints_.end()) {
    return "{\"ok\":false,\"error\":\"checkpoint not found\"}";
  }
  std::vector<uint8_t> current = engine_.snapshot();
  const Checkpoint& base = it->second;
  const std::vector<uint8_t>& baseBlob = base.blob;
  
  // Compute delta as list of patches: each patch = {offset, length, new_data}
  // Use simple diff: find first differing byte, then last differing byte in contiguous run
  struct Patch {
    uint32_t offset;
    uint32_t length;
    std::vector<uint8_t> data;
  };
  std::vector<Patch> patches;
  
  size_t i = 0;
  while (i < current.size() && i < baseBlob.size()) {
    if (current[i] != baseBlob[i]) {
      size_t start = i;
      while (i < current.size() && i < baseBlob.size() && current[i] != baseBlob[i]) ++i;
      Patch p;
      p.offset = static_cast<uint32_t>(start);
      p.length = static_cast<uint32_t>(i - start);
      p.data.assign(current.begin() + start, current.begin() + i);
      patches.push_back(std::move(p));
    } else {
      ++i;
    }
  }
  // Handle trailing bytes if current is longer
  if (current.size() > baseBlob.size()) {
    Patch p;
    p.offset = static_cast<uint32_t>(baseBlob.size());
    p.length = static_cast<uint32_t>(current.size() - baseBlob.size());
    p.data.assign(current.begin() + baseBlob.size(), current.end());
    patches.push_back(std::move(p));
  }
  
  // Serialize delta: magic(4) + version(4) + base_hash(8) + patch_count(4) + patches...
  // Each patch: offset(4) + length(4) + data
  std::vector<uint8_t> delta;
  delta.insert(delta.end(), {'E','D','L','T'}); // magic
  uint32_t ver = 1;
  delta.insert(delta.end(), 
               reinterpret_cast<uint8_t*>(&ver), 
               reinterpret_cast<uint8_t*>(&ver) + 4);
  delta.insert(delta.end(), base.fnv1a64.begin(), base.fnv1a64.end());
  uint32_t patchCount = static_cast<uint32_t>(patches.size());
  delta.insert(delta.end(),
               reinterpret_cast<uint8_t*>(&patchCount),
               reinterpret_cast<uint8_t*>(&patchCount) + 4);
  for (const auto& p : patches) {
    delta.insert(delta.end(),
                 reinterpret_cast<const uint8_t*>(&p.offset),
                 reinterpret_cast<const uint8_t*>(&p.offset) + 4);
    delta.insert(delta.end(),
                 reinterpret_cast<const uint8_t*>(&p.length),
                 reinterpret_cast<const uint8_t*>(&p.length) + 4);
    delta.insert(delta.end(), p.data.begin(), p.data.end());
  }
  
  return std::string(reinterpret_cast<const char*>(delta.data()), delta.size());
}

std::string Server::applyDelta(const std::vector<uint8_t>& deltaBlob, std::string& err) {
  std::lock_guard<std::mutex> lock(engineMu_);
  
  if (deltaBlob.size() < 20) { // magic(4) + ver(4) + hash(8) + count(4) = 20
    err = "delta too small";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  
  // Parse delta
  if (deltaBlob[0] != 'E' || deltaBlob[1] != 'D' || deltaBlob[2] != 'L' || deltaBlob[3] != 'T') {
    err = "bad delta magic";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  uint32_t ver = *reinterpret_cast<const uint32_t*>(deltaBlob.data() + 4);
  if (ver != 1) {
    err = "unsupported delta version";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  // base hash at offset 8 (8 bytes)
  uint32_t patchCount = *reinterpret_cast<const uint32_t*>(deltaBlob.data() + 16);
  
  // Get current snapshot as base
  std::vector<uint8_t> current = engine_.snapshot();
  
  // Apply patches
  size_t pos = 20;
  for (uint32_t i = 0; i < patchCount; ++i) {
    if (pos + 8 > deltaBlob.size()) { err = "delta truncated"; return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}"; }
    uint32_t offset = *reinterpret_cast<const uint32_t*>(deltaBlob.data() + pos);
    pos += 4;
    uint32_t length = *reinterpret_cast<const uint32_t*>(deltaBlob.data() + pos);
    pos += 4;
    if (pos + length > deltaBlob.size()) { err = "delta truncated"; return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}"; }
    if (offset + length > current.size()) {
      // Extend if needed
      current.resize(offset + length);
    }
    std::memcpy(current.data() + offset, deltaBlob.data() + pos, length);
    pos += length;
  }
  
  // Restore the patched state
  if (!engine_.restore(current, err)) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  // Persist
  std::string saveErr;
  engine_.saveFile(opts_.dataDir + "/save.snap", saveErr);
  
  return "{\"ok\":true}";
}

// Phase 12: ComputeProfile endpoint - client reports capabilities, server selects backend/fidelity
std::string Server::computeProfileJson(const std::string& jsonBody, std::string& err) {
  std::lock_guard<std::mutex> lock(engineMu_);
  
  // Parse JSON for ComputeProfile fields
  JsonValue body;
  if (!jsonParse(jsonBody, body)) {
    err = "invalid JSON";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  
  ComputeProfile profile;
  profile.simdLevel = static_cast<SimdLevel>(body.num("simd_level", 0));
  profile.hasWasmSimd128 = body.num("wasm_simd128", 0) > 0.5;
  profile.threadSupport = static_cast<ThreadSupport>(body.num("thread_support", 0));
  profile.maxWorkers = static_cast<uint32_t>(body.num("max_workers", 1));
  profile.hasSharedArrayBuffer = body.num("shared_array_buffer", 0) > 0.5;
  profile.gpuBackend = static_cast<GpuBackend>(body.num("gpu_backend", 0));
  profile.hasWebGPU = body.num("webgpu", 0) > 0.5;
  profile.hasWebGL = body.num("webgl", 0) > 0.5;
  profile.maxMemoryBytes = static_cast<uint64_t>(body.num("max_memory_bytes", 64*1024*1024));
  profile.preferredMemoryBytes = static_cast<uint64_t>(body.num("preferred_memory_bytes", 256*1024*1024));
  profile.estimatedSimStepsPerSec = body.num("estimated_sim_steps_per_sec", 1000.0);
  profile.estimatedInferencesPerSec = body.num("estimated_inferences_per_sec", 100.0);
  profile.userAgent = body.str("user_agent", "");
  profile.platform = body.str("platform", "");
  
  // Select best backend
  ComputeProfileDetector detector;
  BackendSelection selection = detector.selectBackend(profile);
  
  // Auto-select fidelity level from profile
  FidelityLevel level = FidelityController::autoLevel(profile);
  fidelity_ = FidelityController::settingsFor(level);
  
  // Update pacing in sim loop will pick up new fidelity_ on next iteration
  
  JsonValue root = JsonValue::makeObject();
  root.setBool("ok", true);
  JsonValue sel = JsonValue::makeObject();
  sel.setNumber("backend", static_cast<double>(selection.type));
  sel.setString("reason", selection.reason);
  root.set("selection", std::move(sel));
  JsonValue fid = JsonValue::makeObject();
  fid.setNumber("level", static_cast<double>(level));
  fid.setNumber("sim_sec_per_wall_sec", fidelity_.simSecondsPerWallSecond);
  fid.setNumber("model_budget", fidelity_.modelBudgetScale);
  fid.setNumber("world_detail", fidelity_.worldDetailScale);
  fid.setBool("llm_reflection", fidelity_.llmReflectionEnabled);
  fid.setBool("defer_consolidation", fidelity_.deferConsolidation);
  root.set("fidelity", std::move(fid));
  
  // Phase 15: the client may explicitly disarm offload ("offload": false); otherwise
  // arm it only when the selected backend is SIMD-class (plain WASM is kept
  // server-side — it is not faster than the native server, so hosting stays put).
  if (body.find("offload") && !body.boolean("offload", true)) {
    clientComputing_.store(false);
  } else {
    maybeStartClientComputing(profile);
  }
  
  return root.dump();
}

// ---------------------------------------------------------------------------
// Phase 15 — Client-side offload foundation
// The server serves the compiled WASM module to the browser; when the client
// reports a capable ComputeProfile (WasmSimd/WasmSimdMt), the server stops its
// local tick loop and accepts tick-state returns from the client instead
// (client-authoritative compute, server persists — DESIGN §17).
// ---------------------------------------------------------------------------

void Server::maybeStartClientComputing(const ComputeProfile& profile) {
  ComputeProfileDetector detector;
  const BackendSelection sel = detector.selectBackend(profile);
  // Only SIMD-class backends actually carry the sim: plain WASM would be slower than
  // the native server, so there is no offload win for it (the selection is still
  // reported honestly to the client for diagnostics/benchmarks). Posting a weak
  // profile (or "offload":false in computeProfileJson) therefore disarms offload.
  const bool clientCanCompute =
      (sel.type == BackendType::WasmSimd || sel.type == BackendType::WasmSimdMt);
  clientComputing_.store(clientCanCompute);

  // The MT variant needs SharedArrayBuffer (COOP/COEP-isolated page) — only pick it
  // when the client's profile actually reports SAB, else prefer SIMD over plain.
  std::string wantDir = "build-wasm";
  if (sel.type == BackendType::WasmSimdMt && profile.hasSharedArrayBuffer) wantDir = "build-wasm-mt";
  else if (sel.type == BackendType::WasmSimd || sel.type == BackendType::WasmSimdMt)
    wantDir = "build-wasm-simd";
  const std::string searchDirs[] = {wantDir, "build-wasm-simd", "build-wasm"};
  for (const std::string& d : searchDirs) {
    const std::string w = d + "/bin/eidolon-worker.wasm";
    const std::string j = d + "/bin/eidolon-worker.js";
    if (std::filesystem::exists(w) && std::filesystem::exists(j)) {
      wasmPath_ = w;
      wasmJsPath_ = j;
      break;
    }
  }
  if (clientCanCompute) {
    lastClientContactMs_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  std::fprintf(stderr, "client offload: computing=%s wasm=%s\n",
               clientComputing_.load() ? "client" : "server",
               wasmPath_.empty() ? "(none found)" : wasmPath_.c_str());
}

std::string Server::wasmBinary() {
  if (wasmPath_.empty() || !std::filesystem::exists(wasmPath_)) return "";
  std::ifstream f(wasmPath_, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}

std::string Server::wasmJavaScript() {
  if (wasmJsPath_.empty() || !std::filesystem::exists(wasmJsPath_)) return "";
  std::ifstream f(wasmJsPath_, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}

std::string Server::clientSnapshotUpload(const std::vector<uint8_t>& blob, std::string& err) {
  std::lock_guard<std::mutex> lock(engineMu_);
  if (!clientComputing_.load()) {
    err = "server is currently hosting the sim; client offload not active";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  // Reconnect reconcile (DESIGN §18): if the server's headless fallback has run ahead of
  // a reconnecting client (sim-clock newer), reject the stale upload so it cannot roll
  // back forward progress. Only enforced in server-authoritative mode; in the default
  // client-authoritative mode the client's state is authoritative and accepted as-is.
  int64_t incomingTime = -1;
  const bool serverAuthoritative = opts_.worldAuthority == "server";
  if (serverAuthoritative && peekSnapshotSimTime(blob, incomingTime) &&
      incomingTime < engine_.clock().now()) {
    static std::atomic<uint64_t> staleRejects{0};
    ++staleRejects;
    err = "stale client snapshot (sim-time " + std::to_string(incomingTime) +
          " behind server " + std::to_string(engine_.clock().now()) + "); ignored";
    return "{\"ok\":false,\"stale\":true,\"rejected\":" +
           std::to_string(staleRejects.load()) + ",\"error\":\"" + jsonEscape(err) + "\"}";
  }
  if (!engine_.restore(blob, err)) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  ++clientSnapshotsReceived_;
  lastClientContactMs_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
  // In server-authoritative mode, persist immediately so the reconciled state survives a
  // crash before the next periodic autosave; client-authoritative defers to the loop.
  if (serverAuthoritative) {
    std::string saveErr;
    if (!engine_.saveFile(opts_.dataDir + "/save.snap", saveErr)) {
      std::fprintf(stderr, "warning: reconcile autosave failed: %s\n", saveErr.c_str());
    }
  }
  return "{\"ok\":true}";
}

// Future Directions: Internet access - search endpoint
std::string Server::browseSearchJson(const std::string& jsonBody, std::string& err) {
  if (!browser_ || !browser_->enabled()) {
    err = "internet access disabled";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  JsonValue body;
  if (!jsonParse(jsonBody, body)) {
    err = "invalid JSON";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  std::string query = body.str("query", "");
  if (query.empty()) {
    err = "empty query";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  uint32_t maxResults = static_cast<uint32_t>(body.num("max_results", browser_->config().maxResults));
  
  std::vector<WebSearchResult> results;
  std::string berr;
  try {
    if (!browser_->search(query, results, berr)) {
      err = berr.empty() ? "search failed" : berr;
      return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
    }
  } catch (const std::exception& e) {
    err = "search exception: " + std::string(e.what());
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  } catch (...) {
    err = "search unknown exception";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  if (results.size() > maxResults) results.resize(maxResults);
  
  JsonValue root = JsonValue::makeObject();
  root.setBool("ok", true);
  JsonValue arr = JsonValue::makeArray();
  for (const auto& r : results) {
    JsonValue item = JsonValue::makeObject();
    item.setString("title", r.title);
    item.setString("url", r.url);
    item.setString("snippet", r.snippet);
    arr.push(std::move(item));
  }
  root.set("results", std::move(arr));
  // Note: DuckDuckGo HTML scraping is currently limited by CAPTCHA on html.duckduckgo.com
  // For production use, configure --search-endpoint with a proper search API (SerpAPI, Brave, etc.)
  return root.dump();
}

// Future Directions: Internet access - fetch endpoint
std::string Server::browseFetchJson(const std::string& jsonBody, std::string& err) {
  if (!browser_ || !browser_->enabled()) {
    err = "internet access disabled";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  JsonValue body;
  if (!jsonParse(jsonBody, body)) {
    err = "invalid JSON";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  std::string url = body.str("url", "");
  if (url.empty()) {
    err = "empty url";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  
  WebFetchResult result;
  std::string berr;
  try {
    if (!browser_->fetch(url, result, berr)) {
      err = berr.empty() ? "fetch failed" : berr;
      return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
    }
  } catch (const std::exception& e) {
    err = "fetch exception: " + std::string(e.what());
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  } catch (...) {
    err = "fetch unknown exception";
    return "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}";
  }
  
  JsonValue root = JsonValue::makeObject();
  root.setBool("ok", result.success);
  root.setString("url", result.url);
  if (result.success) {
    root.setString("content", result.content);
  } else {
    root.setString("error", result.error);
  }
  return root.dump();
}

int Server::run() {
  simThread_ = std::thread([this] { simLoop(); });

  httplib::Server svr;
  // Phase 12 authentication/session: single-user shared secret. When --api-key is set,
  // reject mutating (POST) requests without a matching token. Read-only GET routes stay
  // open so the status/chat UI can still poll (the organism itself is not a secret; only
  // mutation is gated). Empty api key -> no auth (LAN trust model used by run_eidolon.sh).
  if (!opts_.apiKey.empty()) {
    const std::string expected = opts_.apiKey;
    svr.set_pre_routing_handler([expected](const httplib::Request& req, httplib::Response& res) {
      if (req.method != "POST") return httplib::Server::HandlerResponse::Unhandled;
      // Accept "Authorization: Bearer KEY" or "?key=KEY", both constant-time-ish compare.
      auto has = [&](const std::string& got) {
        if (got.size() != expected.size()) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < got.size(); ++i) diff |= static_cast<unsigned char>(got[i] ^ expected[i]);
        return diff == 0;
      };
      const std::string auth = req.get_header_value("Authorization");
      bool ok = auth.rfind("Bearer ", 0) == 0 && has(auth.substr(7));
      if (!ok && req.has_param("key")) ok = has(req.get_param_value("key"));
      if (!ok) {
        res.status = 401;
        res.set_content("{\"error\":\"unauthorized\"}", "application/json");
        return httplib::Server::HandlerResponse::Handled;
      }
      return httplib::Server::HandlerResponse::Unhandled;
    });
    std::fprintf(stderr, "auth: --api-key set, mutating endpoints require the token\n");
  }
  svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
    // COOP/COEP enable crossOriginIsolated (SharedArrayBuffer) so a capable client
    // can run the MT wasm worker later; harmless for same-origin fetches we use now.
    res.set_header("Cross-Origin-Opener-Policy", "same-origin");
    res.set_header("Cross-Origin-Embedder-Policy", "require-corp");
    res.set_content(kIndexHtml, "text/html");
  });
  svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(statusJson(), "application/json");
  });
  svr.Get("/api/metrics", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(metricsJson(), "application/json");
  });
  svr.Get("/api/conversations", [this](const httplib::Request&,
                                       httplib::Response& res) {
    res.set_content(conversationsJson(), "application/json");
  });
  svr.Post("/api/conversations/new", [this](const httplib::Request&,
                                            httplib::Response& res) {
    res.set_content(newConversationJson(), "application/json");
  });
  svr.Post("/api/conversations/delete",
           [this](const httplib::Request& req, httplib::Response& res) {
             JsonValue body;
             if (!jsonParse(req.body, body)) {
               res.status = 400;
               res.set_content("{\"error\":\"bad json\"}", "application/json");
               return;
             }
             res.set_content(deleteConversationJson(body.str("conversation_id")),
                             "application/json");
           });
  svr.Post("/api/world/reset", [this](const httplib::Request& req,
                                      httplib::Response& res) {
    JsonValue body;
    if (!jsonParse(req.body, body)) {
      res.status = 400;
      res.set_content("{\"error\":\"bad json\"}", "application/json");
      return;
    }
    res.set_content(resetWorldJson(body.str("seed")), "application/json");
  });
  svr.Post("/api/save-prior", [this](const httplib::Request& req,
                                     httplib::Response& res) {
    JsonValue body;
    if (!jsonParse(req.body, body)) {
      res.status = 400;
      res.set_content("{\"error\":\"bad json\"}", "application/json");
      return;
    }
    res.set_content(savePriorJson(body.str("name")), "application/json");
  });
  svr.Get("/api/messages", [this](const httplib::Request& req,
                                  httplib::Response& res) {
    std::string err;
    const std::string out = messagesJson(req.get_param_value("conversation_id"), err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  svr.Get("/api/snapshot", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(snapshotJson(), "application/json");
  });
  
  // Phase 12: Binary snapshot download (raw blob)
  svr.Get("/api/snapshot/download", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(snapshotDownload(), "application/octet-stream");
  });
  
  // Phase 12: Binary snapshot upload
  svr.Post("/api/snapshot/upload", [this](const httplib::Request& req, httplib::Response& res) {
    std::vector<uint8_t> blob(req.body.begin(), req.body.end());
    std::string err;
    std::string out = snapshotUpload(blob, err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  
  // Phase 12: Delta sync - checkpoint create
  svr.Post("/api/checkpoint/create", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(checkpointCreateJson(), "application/json");
  });
  
  // Phase 12: Delta sync - get delta from base checkpoint
  svr.Get("/api/checkpoint/delta", [this](const httplib::Request& req, httplib::Response& res) {
    const std::string baseId = req.get_param_value("base");
    if (baseId.empty()) {
      res.status = 400;
      res.set_content("{\"ok\":false,\"error\":\"missing base checkpoint id\"}", "application/json");
      return;
    }
    res.set_content(checkpointDeltaJson(baseId), "application/octet-stream");
  });
  
  // Phase 12: Delta sync - apply delta
  svr.Post("/api/checkpoint/apply", [this](const httplib::Request& req, httplib::Response& res) {
    std::vector<uint8_t> delta(req.body.begin(), req.body.end());
    std::string err;
    std::string out = applyDelta(delta, err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  
  // Phase 12: ComputeProfile endpoint
  svr.Post("/api/compute-profile", [this](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    std::string out = computeProfileJson(req.body, err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  
  // Phase 15: serve the WASM worker module + glue + worker script to the client.
  // CORP: same-origin lets these load under the COEP-isolated page.
  svr.Get("/api/wasm/core.wasm", [this](const httplib::Request&, httplib::Response& res) {
    const std::string out = wasmBinary();
    if (out.empty()) {
      res.status = 404;
      res.set_content("{\"error\":\"wasm binary not available\"}", "application/json");
      return;
    }
    res.set_header("Cross-Origin-Resource-Policy", "same-origin");
    res.set_content(out, "application/wasm");
  });
  svr.Get("/api/wasm/core.js", [this](const httplib::Request&, httplib::Response& res) {
    const std::string out = wasmJavaScript();
    if (out.empty()) {
      res.status = 404;
      res.set_content("{\"error\":\"wasm glue not available\"}", "application/json");
      return;
    }
    res.set_header("Cross-Origin-Resource-Policy", "same-origin");
    res.set_content(out, "text/javascript");
  });
  svr.Get("/api/wasm/worker.js", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Cross-Origin-Resource-Policy", "same-origin");
    res.set_content(kWorkerJs, "text/javascript");
  });
  
  // Phase 15: client returns its tick state (client-authoritative compute)
  svr.Post("/api/client/snapshot", [this](const httplib::Request& req, httplib::Response& res) {
    std::vector<uint8_t> blob(req.body.begin(), req.body.end());
    std::string err;
    std::string out = clientSnapshotUpload(blob, err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  
  // Future Directions: Internet access - search
  svr.Post("/api/browse/search", [this](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    std::string out = browseSearchJson(req.body, err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  
  // Future Directions: Internet access - fetch
  svr.Post("/api/browse/fetch", [this](const httplib::Request& req, httplib::Response& res) {
    std::string err;
    std::string out = browseFetchJson(req.body, err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  
  svr.Post("/api/send", [this](const httplib::Request& req, httplib::Response& res) {
    JsonValue body;
    if (!jsonParse(req.body, body)) {
      res.status = 400;
      res.set_content("{\"error\":\"bad json\"}", "application/json");
      return;
    }
    const std::string text = body.str("message");
    std::string err;
    const std::string out = sendMessage(body.str("conversation_id"), text, err);
    if (!err.empty()) {
      res.status = 400;
      res.set_content("{\"error\":\"" + jsonEscape(err) + "\"}", "application/json");
      return;
    }
    res.set_content(out, "application/json");
  });

  std::fprintf(stderr, "eidolon-server listening on http://%s:%d (llm=%s)\n",
               opts_.listenHost.c_str(), opts_.port,
               llm_ && llm_->enabled() ? opts_.llmEndpoint.c_str() : "offline");
  // Listen on a worker thread so requestStop() (SIGTERM/SIGINT) can tear down cleanly
  // and run the final save instead of losing up to an autosave interval.
  std::atomic<bool> httpDone = false;
  std::thread listenThread([&] {
    svr.listen(opts_.listenHost, opts_.port);
    httpDone.store(true);
  });
  while (!stop_.load() && !httpDone.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  svr.stop();
  listenThread.join();
  stop_.store(true);
  if (simThread_.joinable()) simThread_.join();
  return 0;
}

} // namespace eidolon