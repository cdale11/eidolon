#include "mind/compute_scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace eidolon {

namespace {
uint64_t nowUs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}
} // namespace

bool ComputeScheduler::submit(WorkItem item) {
  if (pendingCount_ >= kMaxQueued) return false;
  queue_[pendingCount_++] = item;
  return true;
}

bool ComputeScheduler::next(WorkItem& out) {
  if (pendingCount_ == 0) return false;
  // Select the highest-priority item; FIFO among equals (earliest submitTick wins).
  uint32_t best = 0;
  for (uint32_t i = 1; i < pendingCount_; ++i) {
    const WorkItem& a = queue_[best];
    const WorkItem& b = queue_[i];
    if (static_cast<uint8_t>(b.priority) > static_cast<uint8_t>(a.priority)) {
      best = i;
    } else if (b.priority == a.priority && b.submitTick < a.submitTick) {
      best = i;
    }
  }
  out = queue_[best];
  // Compact removal: shift the tail down (bounded, small queue).
  for (uint32_t i = best + 1; i < pendingCount_; ++i) queue_[i - 1] = queue_[i];
  --pendingCount_;
  return true;
}

WorkPriority ComputeScheduler::pendingPriority() const {
  WorkPriority p = WorkPriority::Idle;
  for (uint32_t i = 0; i < pendingCount_; ++i) {
    if (static_cast<uint8_t>(queue_[i].priority) > static_cast<uint8_t>(p))
      p = queue_[i].priority;
  }
  return p;
}

bool ComputeScheduler::backgroundAllowed(WorkerDomain domain, double budgetUs) const {
  const size_t d = static_cast<size_t>(domain);
  const uint64_t now = nowUs();
  // Roll the 1s window if it has elapsed.
  if (windowStartUs_[d] == 0 || now - windowStartUs_[d] >= 1000000) {
    windowStartUs_[d] = now;
    windowSpentUs_[d] = 0.0;
  }
  return windowSpentUs_[d] + profiles_[d].lastWallUs <= budgetUs;
}

void ComputeScheduler::recordTick(WorkerDomain domain, double wallUs) {
  DomainProfile& p = profiles_[static_cast<size_t>(domain)];
  p.samples++;
  p.totalWallUs += wallUs;
  p.lastWallUs = wallUs;
  p.peakWallUs = std::max(p.peakWallUs, wallUs);
  // Accumulate into the domain's window spent budget.
  const size_t d = static_cast<size_t>(domain);
  const uint64_t now = nowUs();
  if (windowStartUs_[d] == 0 || now - windowStartUs_[d] >= 1000000 /* 1s window */) {
    windowStartUs_[d] = now;
    windowSpentUs_[d] = 0.0;
  }
  windowSpentUs_[d] += wallUs;
  lastWallUs_ = now;
}

bool ComputeScheduler::post(WorkerMessage msg) {
  if (messageCount_ >= kMaxMessages) {
    // Drop-oldest: shift the ring down one slot.
    for (uint32_t i = 1; i < messageCount_; ++i) messages_[i - 1] = messages_[i];
    --messageCount_;
  }
  messages_[messageCount_++] = msg;
  return true;
}

bool ComputeScheduler::poll(WorkerMessage& out) {
  if (messageCount_ == 0) return false;
  out = messages_[0];
  for (uint32_t i = 1; i < messageCount_; ++i) messages_[i - 1] = messages_[i];
  --messageCount_;
  return true;
}

void ComputeScheduler::reset() {
  pendingCount_ = 0;
  messageCount_ = 0;
  profiles_ = {};
  windowStartUs_ = {};
  windowSpentUs_ = {};
  lastWallUs_ = 0;
}

void ComputeScheduler::serialize(BinaryWriter& w) const {
  w.u32(pendingCount_);
  for (uint32_t i = 0; i < pendingCount_; ++i) {
    w.u8(static_cast<uint8_t>(queue_[i].domain));
    w.u8(static_cast<uint8_t>(queue_[i].priority));
    w.u32(queue_[i].kind);
    w.u64(queue_[i].submitTick);
  }
  w.u32(messageCount_);
  for (uint32_t i = 0; i < messageCount_; ++i) {
    w.u8(static_cast<uint8_t>(messages_[i].from));
    w.u8(static_cast<uint8_t>(messages_[i].to));
    w.u8(messages_[i].kind);
    w.u32(messages_[i].a);
    w.u32(messages_[i].b);
    w.u64(messages_[i].tick);
  }
}

bool ComputeScheduler::deserialize(BinaryReader& r) {
  uint32_t n = 0;
  if (!r.u32(n) || n > kMaxQueued) return false;
  pendingCount_ = n;
  for (uint32_t i = 0; i < pendingCount_; ++i) {
    uint8_t dom, pri;
    if (!r.u8(dom) || !r.u8(pri)) return false;
    queue_[i].domain = static_cast<WorkerDomain>(dom);
    queue_[i].priority = static_cast<WorkPriority>(pri);
    if (!r.u32(queue_[i].kind)) return false;
    if (!r.u64(queue_[i].submitTick)) return false;
  }
  if (!r.u32(n) || n > kMaxMessages) return false;
  messageCount_ = n;
  for (uint32_t i = 0; i < messageCount_; ++i) {
    uint8_t from, to, kind;
    if (!r.u8(from) || !r.u8(to) || !r.u8(kind)) return false;
    messages_[i].from = static_cast<WorkerDomain>(from);
    messages_[i].to = static_cast<WorkerDomain>(to);
    messages_[i].kind = kind;
    if (!r.u32(messages_[i].a)) return false;
    if (!r.u32(messages_[i].b)) return false;
    if (!r.u64(messages_[i].tick)) return false;
  }
  return true;
}

std::string ComputeScheduler::toString() const {
  static const char* kDomains[static_cast<size_t>(WorkerDomain::Count)] = {
      "World", "PhysioCognition", "NeuralMl", "MemoryConsolidation"};
  std::ostringstream oss;
  oss << "ComputeScheduler: pending=" << pendingCount_ << " messages=" << messageCount_
      << "\n";
  for (size_t i = 0; i < static_cast<size_t>(WorkerDomain::Count); ++i) {
    const DomainProfile& p = profiles_[i];
    oss << "  " << kDomains[i] << ": samples=" << p.samples
        << " total_us=" << static_cast<uint64_t>(p.totalWallUs)
        << " last_us=" << static_cast<uint64_t>(p.lastWallUs)
        << " peak_us=" << static_cast<uint64_t>(p.peakWallUs) << "\n";
  }
  return oss.str();
}

} // namespace eidolon