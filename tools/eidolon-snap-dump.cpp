// Diagnostic: dump the packed engine snapshot to a binary file so native and
// WASM blobs can be diffed byte-for-byte to locate the diverging subsystem.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/serialize.hpp"
#include "sim/engine.hpp"

using namespace eidolon;

int main(int argc, char** argv) {
  uint64_t seed = 42;
  int64_t ticks = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--ticks" && i + 1 < argc) ticks = std::atoll(argv[++i]);
  }
  Engine engine;
  engine.init(seed, true, 128, 128);
  for (int64_t i = 0; i < ticks && engine.isAlive(); ++i) engine.tick();
  const std::vector<uint8_t> blob = engine.snapshot();
  FILE* f = std::fopen("snap.bin", "wb");
  std::fwrite(blob.data(), 1, blob.size(), f);
  std::fclose(f);
  std::fprintf(stderr, "wrote %zu bytes, simtime %lld\n", blob.size(),
               static_cast<long long>(engine.clock().now()));
  return 0;
}