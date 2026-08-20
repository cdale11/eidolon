// ComputeScheduler: coordinates optional/background work around the single-threaded
// deterministic tick (Phase 11). It never reorders the tick itself (bit-exact
// determinism invariant); it decides WHEN deferrable work — memory consolidation,
// reflection, planning search, LLM-assisted proposals — may run, in priority order:
// responsiveness (chat) > active sim > background consolidation > idle.
//
// It also carries per-domain profiling counters (world / physiology-cognition /
// neural-ML / memory-consolidation) and compact message passing (fixed-size ring, no
// big buffer transfers) for the diagnostics panel and the client-compute handoff.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "core/serialize.hpp"

namespace eidolon {

// Priority classes for schedulable work (higher = more urgent).
enum class WorkPriority : uint8_t {
  Idle = 0,          // opportunistic, runs only when nothing else pending
  Background = 1,    // consolidation, rehearsal, pruning
  Normal = 2,        // active simulation steps
  Responsive = 3,    // chat / user-facing replies
};

// Worker separation domains (DESIGN §17): each maps to a logical compute worker.
enum class WorkerDomain : uint8_t {
  World = 0,             // terrain, wildlife, weather, plants
  PhysioCognition = 1,   // body, perception, decision
  NeuralMl = 2,          // value/threat/policy/attention/neuromod updates
  MemoryConsolidation = 3, // sleep consolidation, archival, pruning
  Count = 4,
};

// A unit of deferrable work submitted to the scheduler.
struct WorkItem {
  WorkerDomain domain = WorkerDomain::MemoryConsolidation;
  WorkPriority priority = WorkPriority::Background;
  uint32_t kind = 0;         // opaque: caller-defined work kind
  uint64_t submitTick = 0;   // sim-clock when submitted (FIFO tie-break)
};

// Compact message between worker domains (no big buffer transfers). Fixed size.
struct WorkerMessage {
  WorkerDomain from = WorkerDomain::World;
  WorkerDomain to = WorkerDomain::World;
  uint8_t kind = 0;        // opaque message kind
  uint32_t a = 0;          // small payload fields
  uint32_t b = 0;
  uint64_t tick = 0;
};

// Per-domain profiling accumulators (diagnostics panel feed).
struct DomainProfile {
  uint64_t samples = 0;       // number of profiled executions
  double totalWallUs = 0.0;   // cumulative wall time (us)
  double lastWallUs = 0.0;    // last execution time (us)
  double peakWallUs = 0.0;    // peak single execution (us)
};

class ComputeScheduler {
public:
  static constexpr uint32_t kMaxQueued = 64;   // fixed capacity, no heap churn
  static constexpr uint32_t kMaxMessages = 32; // fixed message ring

  // Submit deferrable work. Drops (returns false) only when the queue is full.
  bool submit(WorkItem item);

  // Pop the highest-priority pending item (FIFO within a priority). Returns false if
  // nothing is pending.
  bool next(WorkItem& out);

  // Highest priority among currently pending items, or Idle if none.
  WorkPriority pendingPriority() const;
  uint32_t pendingCount() const { return pendingCount_; }

  // Whether a worker `domain` may run a background slice now: its spend in the rolling
  // 1s wall window stays within `budgetUs`. Deterministic in *policy*, not wall timing —
  // used only to gate deferrable work, never tick semantics.
  bool backgroundAllowed(WorkerDomain domain, double budgetUs) const;
  void recordTick(WorkerDomain domain, double wallUs);

  // Compact message passing between domains (fixed ring, drop-oldest when full).
  bool post(WorkerMessage msg);
  bool poll(WorkerMessage& out);
  uint32_t messageCount() const { return messageCount_; }

  // Profiling snapshots.
  const DomainProfile& profile(WorkerDomain d) const {
    return profiles_[static_cast<size_t>(d)];
  }
  double totalWallUs() const {
    double t = 0.0;
    for (const auto& p : profiles_) t += p.totalWallUs;
    return t;
  }

  void reset();

  // Serialization (schedule state only; profiles are ephemeral diagnostics).
  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

  std::string toString() const;

private:
  std::array<WorkItem, kMaxQueued> queue_ = {};
  uint32_t pendingCount_ = 0;
  std::array<WorkerMessage, kMaxMessages> messages_ = {};
  uint32_t messageCount_ = 0;
  std::array<DomainProfile, static_cast<size_t>(WorkerDomain::Count)> profiles_ = {};
  // Rolling window accounting for backgroundAllowed (per-domain window start / spent).
  // mutable: backgroundAllowed() is a const query that lazily rolls the window.
  mutable std::array<uint64_t, static_cast<size_t>(WorkerDomain::Count)> windowStartUs_ = {};
  mutable std::array<double, static_cast<size_t>(WorkerDomain::Count)> windowSpentUs_ = {};
  uint64_t lastWallUs_ = 0; // last recordTick() wall clock (us)
};

} // namespace eidolon