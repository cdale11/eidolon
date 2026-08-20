// Phase 12 parity harness: runs a deterministic seeded scenario and prints a stable
// digest of the final individual state. The SAME source is compiled natively
// (eidolon-sim or a parity build) and to WASM; matching digests prove the WASM core
// reproduces the native core bit-for-bit (DESIGN §17, ROADMAP Phase 12 gate).
//
// Output: `parity:<seed>:<ticks>:<simtime>:<fnv1a64 of packed snapshot>`
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/serialize.hpp"
#include "sim/engine.hpp"

using namespace eidolon;

int main(int argc, char** argv) {
  uint64_t seed = 42;
  int64_t ticks = 1000;
  int worldW = 128, worldH = 128;
  bool dumpBlob = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--ticks" && i + 1 < argc) ticks = std::atoll(argv[++i]);
    else if (a == "--world" && i + 1 < argc)
      std::sscanf(argv[++i], "%dx%d", &worldW, &worldH);
    else if (a == "--dump-blob") dumpBlob = true;
    else if (a == "--help") {
      std::fprintf(stderr,
                   "usage: parity-dump [--seed N] [--ticks N] [--world WxH] [--dump-blob]\n");
      return 0;
    }
  }

  Engine engine;
  engine.init(seed, true, worldW, worldH);
  for (int64_t i = 0; i < ticks && engine.isAlive(); ++i) {
    engine.tick();
  }

  const std::vector<uint8_t> blob = engine.snapshot();
  if (dumpBlob) {
    FILE* f = std::fopen("snap.bin", "wb");
    if (f) {
      std::fwrite(blob.data(), 1, blob.size(), f);
      std::fclose(f);
    }
  }
  const uint64_t digest = fnv1a64(blob.data(), blob.size());
  std::printf("parity:%llu:%lld:%lld:%llu\n",
              static_cast<unsigned long long>(seed),
              static_cast<long long>(ticks),
              static_cast<long long>(engine.clock().now()),
              static_cast<unsigned long long>(digest));
  return 0;
}