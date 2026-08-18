#include "mind/memory.hpp"

#include <algorithm>

namespace eidolon {

void MemoryRing::add(Episode e) {
  if (cap_ == 0) return;
  if (eps_.size() == cap_) {
    // Evict the oldest (importance-based retention lands with consolidation in Phase 4).
    eps_.erase(eps_.begin());
  }
  eps_.push_back(e);
}

size_t MemoryRing::countKind(EventKind k) const {
  size_t n = 0;
  for (const Episode& e : eps_) {
    if (e.kind == k) ++n;
  }
  return n;
}

void MemoryRing::serialize(BinaryWriter& w) const {
  w.u64(static_cast<uint64_t>(cap_));
  w.u64(static_cast<uint64_t>(eps_.size()));
  for (const Episode& e : eps_) {
    w.i64(e.t);
    w.i64(e.x);
    w.i64(e.y);
    w.u8(static_cast<uint8_t>(e.kind));
    w.f64(e.importance);
    w.u8(e.detail);
  }
}

bool MemoryRing::deserialize(BinaryReader& r) {
  uint64_t cap, n;
  if (!r.u64(cap) || !r.u64(n) || cap == 0 || n > cap) return false;
  cap_ = static_cast<size_t>(cap);
  eps_.clear();
  eps_.reserve(static_cast<size_t>(n));
  for (uint64_t i = 0; i < n; ++i) {
    Episode e;
    int64_t x, y;
    uint8_t kind, detail;
    if (!r.i64(e.t) || !r.i64(x) || !r.i64(y) || !r.u8(kind) || !r.f64(e.importance) ||
        !r.u8(detail)) {
      return false;
    }
    e.x = static_cast<int16_t>(x);
    e.y = static_cast<int16_t>(y);
    e.kind = static_cast<EventKind>(kind);
    e.detail = detail;
    eps_.push_back(e);
  }
  return true;
}

} // namespace eidolon