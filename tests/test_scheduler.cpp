// ComputeScheduler tests: priority ordering, FIFO tie-break, message ring,
// background budget gating, serialization round-trip.
#include "harness.hpp"
#include "mind/compute_scheduler.hpp"

#include <cstring>

using namespace eidolon;

TEST(compute_scheduler_priority_order) {
  ComputeScheduler s;
  // Submit out of priority order; expect Responsive first, then Normal, then Idle.
  WorkItem a;
  a.priority = WorkPriority::Normal;
  a.domain = WorkerDomain::World;
  a.kind = 1;
  a.submitTick = 100;
  WorkItem b;
  b.priority = WorkPriority::Responsive;
  b.domain = WorkerDomain::NeuralMl;
  b.kind = 2;
  b.submitTick = 50;
  WorkItem c;
  c.priority = WorkPriority::Idle;
  c.domain = WorkerDomain::MemoryConsolidation;
  c.kind = 3;
  c.submitTick = 10;
  CHECK(s.submit(a));
  CHECK(s.submit(b));
  CHECK(s.submit(c));
  CHECK_EQ(s.pendingCount(), 3u);
  CHECK(s.pendingPriority() == WorkPriority::Responsive);

  WorkItem out;
  CHECK(s.next(out));
  CHECK_EQ(out.kind, 2u);
  CHECK(out.priority == WorkPriority::Responsive);

  CHECK(s.next(out));
  CHECK_EQ(out.kind, 1u);
  CHECK(out.priority == WorkPriority::Normal);

  CHECK(s.next(out));
  CHECK_EQ(out.kind, 3u);
  CHECK(out.priority == WorkPriority::Idle);

  CHECK(!s.next(out));
  CHECK_EQ(s.pendingCount(), 0u);
  CHECK(s.pendingPriority() == WorkPriority::Idle);
}

TEST(compute_scheduler_fifo_tiebreak) {
  ComputeScheduler s;
  WorkItem a;
  a.priority = WorkPriority::Background;
  a.submitTick = 30;
  a.kind = 7;
  WorkItem b;
  b.priority = WorkPriority::Background;
  b.submitTick = 10;
  b.kind = 8;
  CHECK(s.submit(a));
  CHECK(s.submit(b));
  WorkItem out;
  CHECK(s.next(out));
  CHECK_EQ(out.kind, 8u); // earlier submitTick wins
}

TEST(compute_scheduler_message_ring) {
  ComputeScheduler s;
  WorkerMessage m;
  m.from = WorkerDomain::World;
  m.to = WorkerDomain::MemoryConsolidation;
  m.kind = 42;
  m.a = 1;
  m.b = 2;
  m.tick = 999;
  CHECK(s.post(m));
  WorkerMessage out;
  CHECK(s.poll(out));
  CHECK_EQ(out.kind, 42u);
  CHECK_EQ(out.tick, 999u);
  CHECK_EQ(out.a, 1u);
  CHECK(!s.poll(out)); // empty now
  CHECK_EQ(s.messageCount(), 0u);
}

TEST(compute_scheduler_overflow_drops_oldest) {
  ComputeScheduler s;
  for (uint32_t i = 0; i < ComputeScheduler::kMaxMessages + 5; ++i) {
    WorkerMessage m;
    m.kind = static_cast<uint8_t>(i);
    CHECK(s.post(m));
  }
  CHECK_EQ(s.messageCount(), ComputeScheduler::kMaxMessages);
  WorkerMessage out;
  // Oldest (kind 5..) dropped; first available is kind 5 (5 dropped).
  CHECK(s.poll(out));
  CHECK_EQ(out.kind, 5u);
}

TEST(compute_scheduler_background_budget) {
  ComputeScheduler s;
  // No profile history yet -> no lastWallUs to charge -> allowed.
  CHECK(s.backgroundAllowed(WorkerDomain::MemoryConsolidation, 1000.0));
  // Charge a large sample; then a tiny budget should deny.
  s.recordTick(WorkerDomain::MemoryConsolidation, 5000.0);
  CHECK(!s.backgroundAllowed(WorkerDomain::MemoryConsolidation, 100.0));
  // A generous budget still allows.
  CHECK(s.backgroundAllowed(WorkerDomain::MemoryConsolidation, 10000.0));
  CHECK(s.profile(WorkerDomain::MemoryConsolidation).samples == 1);
}

TEST(compute_scheduler_serialize_roundtrip) {
  ComputeScheduler s;
  WorkItem a;
  a.priority = WorkPriority::Responsive;
  a.domain = WorkerDomain::NeuralMl;
  a.kind = 11;
  a.submitTick = 1234;
  WorkItem b;
  b.priority = WorkPriority::Background;
  b.domain = WorkerDomain::MemoryConsolidation;
  b.kind = 12;
  b.submitTick = 5678;
  CHECK(s.submit(a));
  CHECK(s.submit(b));
  WorkerMessage m;
  m.kind = 9;
  m.tick = 77;
  CHECK(s.post(m));

  BinaryWriter w;
  s.serialize(w);
  ComputeScheduler t;
  BinaryReader r(w.data());
  CHECK(t.deserialize(r));
  CHECK_EQ(t.pendingCount(), 2u);
  CHECK_EQ(t.messageCount(), 1u);

  // Priority ordering preserved across serialization.
  WorkItem out;
  CHECK(t.next(out));
  CHECK_EQ(out.kind, 11u);
  CHECK(t.next(out));
  CHECK_EQ(out.kind, 12u);
  WorkerMessage mout;
  CHECK(t.poll(mout));
  CHECK_EQ(mout.kind, 9u);
}