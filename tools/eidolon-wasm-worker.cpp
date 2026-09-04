// Phase 15: WASM worker entry point — a tiny C API around ReplicaCore's Engine so a
// browser Web Worker can run the deterministic tick loop itself (client-side offload,
// DESIGN §17). No main(): the module is loaded by the worker script (kWorkerJs) which
// drives init/restore → tick → snapshot → (post to server) from JS.
//
// All functions are noexcept-style: no exceptions (the wasm build uses -fno-exceptions),
// no allocation churn on the tick path (snapshot bytes are written into caller memory).
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <emscripten.h>

#include "sim/engine.hpp"

using eidolon::Engine;

extern "C" {

EMSCRIPTEN_KEEPALIVE void* eidn_new() {
  return new (std::nothrow) Engine();
}

EMSCRIPTEN_KEEPALIVE void eidn_free(void* e) {
  delete static_cast<Engine*>(e);
}

// Fresh organism. deterministic must be 1 whenever snapshots will ever be compared
// or resumed across hosts (native <-> wasm parity invariant).
EMSCRIPTEN_KEEPALIVE int eidn_init(void* e, unsigned long long seed, int deterministic,
                                   int worldW, int worldH) {
  if (!e) return 0;
  static_cast<Engine*>(e)->init(seed, deterministic != 0, worldW, worldH);
  return 1;
}

EMSCRIPTEN_KEEPALIVE int eidn_restore(void* e, const uint8_t* data, size_t len) {
  if (!e || !data || len == 0) return 0;
  std::string err;
  const std::vector<uint8_t> blob(data, data + len);
  return static_cast<Engine*>(e)->restore(blob, err) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void eidn_tick(void* e) {
  if (e) static_cast<Engine*>(e)->tick();
}

EMSCRIPTEN_KEEPALIVE int eidn_alive(void* e) {
  return e && static_cast<Engine*>(e)->isAlive() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE long long eidn_simtime(void* e) {
  return e ? static_cast<long long>(static_cast<Engine*>(e)->clock().now()) : -1;
}

EMSCRIPTEN_KEEPALIVE unsigned long long eidn_seed(void* e) {
  return e ? static_cast<unsigned long long>(static_cast<Engine*>(e)->masterSeed()) : 0;
}

// Query the current snapshot size, then fetch the bytes with eidn_snapshot into a
// caller-provided buffer of at least that size.
EMSCRIPTEN_KEEPALIVE size_t eidn_snapshot_size(void* e) {
  return e ? static_cast<Engine*>(e)->snapshot().size() : 0;
}

EMSCRIPTEN_KEEPALIVE size_t eidn_snapshot(void* e, uint8_t* out, size_t cap) {
  if (!e || !out) return 0;
  const std::vector<uint8_t> blob = static_cast<Engine*>(e)->snapshot();
  if (blob.size() > cap) return 0;
  std::memcpy(out, blob.data(), blob.size());
  return blob.size();
}

} // extern "C"
