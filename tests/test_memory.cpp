#include "harness.hpp"

#include "core/serialize.hpp"
#include "mind/memory.hpp"

using namespace eidolon;

TEST(memory_ring_bounded) {
  MemoryRing m(4);
  for (int i = 0; i < 10; ++i) {
    Episode e;
    e.t = i;
    e.kind = EventKind::Forage;
    e.importance = 0.5;
    m.add(e);
  }
  CHECK_EQ(m.size(), 4u);
  CHECK_EQ(m.episodes()[0].t, 6); // oldest evicted
  CHECK_EQ(m.episodes()[3].t, 9);
}

TEST(memory_ring_count_kind) {
  MemoryRing m;
  for (int i = 0; i < 3; ++i) {
    Episode e;
    e.kind = EventKind::Forage;
    m.add(e);
  }
  Episode e;
  e.kind = EventKind::Drink;
  m.add(e);
  CHECK_EQ(m.countKind(EventKind::Forage), 3u);
  CHECK_EQ(m.countKind(EventKind::Drink), 1u);
  CHECK_EQ(m.countKind(EventKind::Sleep), 0u);
}

TEST(memory_ring_last_and_serdes) {
  MemoryRing a(8);
  for (int i = 0; i < 5; ++i) {
    Episode e;
    e.t = 100 + i;
    e.x = static_cast<int16_t>(10 + i);
    e.y = static_cast<int16_t>(20);
    e.kind = i % 2 == 0 ? EventKind::Forage : EventKind::NearDeath;
    e.importance = 0.1 + 0.2 * i;
    e.detail = static_cast<uint8_t>(i * 3);
    a.add(e);
  }
  const Episode* last = a.last();
  CHECK(last != nullptr && last->t == 104 && last->importance >= 0.9);

  MemoryRing b;
  std::vector<uint8_t> blob =
      packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    CHECK_EQ(a.episodes()[i].t, b.episodes()[i].t);
    CHECK_EQ(a.episodes()[i].x, b.episodes()[i].x);
    CHECK_EQ(a.episodes()[i].y, b.episodes()[i].y);
    CHECK_EQ(a.episodes()[i].kind, b.episodes()[i].kind);
    CHECK_EQ(a.episodes()[i].importance, b.episodes()[i].importance);
    CHECK_EQ(a.episodes()[i].detail, b.episodes()[i].detail);
  }
}