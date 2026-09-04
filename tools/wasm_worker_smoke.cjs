// Node smoke/parity test for the eidolon-worker WASM module (Phase 15).
//
// Usage: node tools/wasm_worker_smoke.cjs [path/to/eidolon-worker.js] [ticks] [seed]
//
// Prints `parity:<seed>:<ticks>:<simtime>:<fnv1a64>` — the same digest line the
// native eidolon-parity-dump prints for the same seed/ticks (bit-exact WASM<->native
// parity invariant, DESIGN §17 / ROADMAP Phase 12). Also verifies the WASM-side
// resume invariant: snapshot at tick/2, continue, restore into a fresh engine, re-run
// the remaining ticks — the two final snapshots must be byte-identical.
'use strict';
const path = require('path');

function fnv1a64(buf) {
  let h = 0xcbf29ce484222325n;
  for (const b of buf) {
    h ^= BigInt(b);
    h = (h * 0x100000001b3n) & 0xffffffffffffffffn;
  }
  return h;
}

(async () => {
  const modPath = path.resolve(process.argv[2] || 'build-wasm/bin/eidolon-worker.js');
  const ticks = parseInt(process.argv[3] || '1000', 10);
  const seedArg = process.argv[4] || '42';

  const EidolonWorker = require(modPath);
  const M = await EidolonWorker();
  const fail = (msg) => { console.error('FAIL: ' + msg); process.exit(1); };

  function snapshotOf(eng) {
    const size = M._eidn_snapshot_size(eng);
    if (!size) fail('snapshot_size returned 0');
    const ptr = M._malloc(size);
    const n = M._eidn_snapshot(eng, ptr, size);
    if (n !== size) fail(`snapshot wrote ${n} of ${size}`);
    const out = Buffer.from(M.HEAPU8.slice(ptr, ptr + size));
    M._free(ptr);
    return out;
  }
  function restoreInto(eng, blob) {
    const ptr = M._malloc(blob.length);
    M.HEAPU8.set(blob, ptr);
    const ok = M._eidn_restore(eng, ptr, blob.length);
    M._free(ptr);
    if (!ok) fail('restore returned 0');
  }
  function tickSome(eng, n) {
    for (let i = 0; i < n && M._eidn_alive(eng); ++i) M._eidn_tick(eng);
  }

  // Straight run
  const e1 = M._eidn_new();
  if (!e1) fail('new returned null');
  if (!M._eidn_init(e1, BigInt(seedArg), 1, 128, 128)) fail('init failed');
  const half = Math.floor(ticks / 2);
  tickSome(e1, half);
  const midA = snapshotOf(e1);
  tickSome(e1, ticks - half);
  const simtime = Number(M._eidn_simtime(e1));
  const full = snapshotOf(e1);

  // Resume: fresh engine restored from the midpoint must land byte-identical
  const e2 = M._eidn_new();
  if (!e2) fail('new(2) returned null');
  restoreInto(e2, midA);
  tickSome(e2, ticks - half);
  const resumed = snapshotOf(e2);

  M._eidn_free(e1);
  M._eidn_free(e2);

  const digest = fnv1a64(full);
  console.log(`parity:${seedArg}:${ticks}:${simtime}:${digest}`);
  if (Buffer.compare(full, resumed) !== 0) fail('resume parity: midpoint snapshot + remaining ticks diverged');
  console.error('wasm-resume: identical');
})().catch((e) => { console.error(e); process.exit(1); });
