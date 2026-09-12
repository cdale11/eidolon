// Portable archive interface: the engine pushes memories and events here; the concrete
// implementation (SQLite on native, IndexedDB/OPFS on WASM later) lives behind the
// backend layer so ReplicaCore stays platform-independent (DESIGN §15/§17).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mind/memory.hpp"

namespace eidolon {

struct ArchivedEvent {
  int64_t t = 0;
  int16_t x = 0;
  int16_t y = 0;
  EventKind kind = EventKind::Birth;
  std::string type;
  std::string text;
  double importance = 0.0;
  uint8_t detail = 0;
  bool fromEpisode = false;
  // E3-slice-2b: attribution. 0 = the current individual's own lived
  // experience; otherwise the predecessor individual id this record was
  // inherited from (same convention as Episode::sourceIndividualId).
  int64_t sourceIndividualId = 0;
};

class Archive {
public:
  virtual ~Archive() = default;

  // Persist an episode (long-term memory).
  virtual void episode(const Episode& e) = 0;
  // Persist a timeline event line (e.g. "weather: rain").
  virtual void event(int64_t t, const char* type, const char* text) = 0;
  // Query a bounded chronological slice of durable timeline facts.
  virtual std::vector<ArchivedEvent> timeline(int64_t startTick, int64_t endTick,
                                              size_t limit = 128) const = 0;
};

} // namespace eidolon
