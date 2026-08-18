// Portable archive interface: the engine pushes memories and events here; the concrete
// implementation (SQLite on native, IndexedDB/OPFS on WASM later) lives behind the
// backend layer so ReplicaCore stays platform-independent (DESIGN §15/§17).
#pragma once

#include <cstdint>

#include "mind/memory.hpp"

namespace eidolon {

class Archive {
public:
  virtual ~Archive() = default;

  // Persist an episode (long-term memory).
  virtual void episode(const Episode& e) = 0;
  // Persist a timeline event line (e.g. "weather: rain").
  virtual void event(int64_t t, const char* type, const char* text) = 0;
};

} // namespace eidolon