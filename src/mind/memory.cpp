#include "mind/memory.hpp"

#include <algorithm>

namespace eidolon {

void MemoryRing::add(Episode e) {
  if (cap_ == 0) return;
  if (eps_.size() == cap_) {
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
    w.u8(e.action);
    w.u8(static_cast<uint8_t>(e.participants));
    w.u8(static_cast<uint8_t>(e.outcome));
    w.f32(e.prediction);
    w.f32(e.predictionError);
    w.f32(e.emotionalValence);
    w.f32(e.socialRelevance);
    w.u8(static_cast<uint8_t>(e.relevance));
    w.f64(e.importance);
    w.u8(e.detail);
    w.u32(e.rehearsalCount);
    w.u8(e.consolidated ? 1 : 0);
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
    uint8_t kind, action, participants, outcome, relevance, detail, consolidated;
    if (!r.i64(e.t) || !r.i64(x) || !r.i64(y) || !r.u8(kind) || !r.u8(action) ||
        !r.u8(participants) || !r.u8(outcome) || !r.f32(e.prediction) ||
        !r.f32(e.predictionError) || !r.f32(e.emotionalValence) ||
        !r.f32(e.socialRelevance) || !r.u8(relevance) || !r.f64(e.importance) ||
        !r.u8(detail) || !r.u32(e.rehearsalCount) || !r.u8(consolidated)) {
      return false;
    }
    e.x = static_cast<int16_t>(x);
    e.y = static_cast<int16_t>(y);
    e.kind = static_cast<EventKind>(kind);
    e.action = action;
    e.participants = static_cast<Participant>(participants);
    e.outcome = static_cast<Outcome>(outcome);
    e.relevance = static_cast<Relevance>(relevance);
    e.detail = detail;
    e.consolidated = consolidated != 0;
    eps_.push_back(e);
  }
  return true;
}

} // namespace eidolon