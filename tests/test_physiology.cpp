#include "harness.hpp"

#include "body/physiology.hpp"

using namespace eidolon;

TEST(physiology_energy_depletes_awake) {
  Physiology p;
  p.reset();
  const double e0 = p.energy();
  p.update(60.0, 20.0, Activity::Move);
  CHECK(p.energy() < e0);
}

TEST(physiology_hunger_thirst_rise) {
  Physiology p;
  p.reset();
  p.update(600.0, 20.0, Activity::Observe);
  CHECK(p.hunger() > 0.0);
  CHECK(p.thirst() > 0.0);
}

TEST(physiology_sleep_restores) {
  Physiology p;
  p.reset();
  p.update(3600.0 * 12, 20.0, Activity::Move); // exhaust: long awake day
  const double e0 = p.energy();
  const double s0 = p.sleepPressure();
  p.setSleeping(true);
  p.update(3600.0 * 8, 20.0, Activity::Sleep);
  CHECK(p.energy() > e0);
  CHECK(p.sleepPressure() < s0);
  CHECK(p.isSleeping());
}

TEST(physiology_fatigue_activity) {
  Physiology p;
  p.reset();
  p.update(300.0, 20.0, Activity::Move);
  const double fMove = p.fatigue();
  p.reset();
  p.update(300.0, 20.0, Activity::Sleep);
  CHECK(fMove > p.fatigue());
}

TEST(physiology_extreme_cold_damages_health) {
  Physiology p;
  p.reset();
  p.update(3600.0 * 24, -30.0, Activity::Rest);
  CHECK(p.bodyTemp() < 36.0);
  CHECK(p.health() < 100.0);
}

TEST(physiology_bounded_and_alive) {
  Physiology p;
  p.reset();
  for (int i = 0; i < 10000; ++i) {
    p.update(60.0, 25.0, Activity::Move);
    CHECK(p.energy() >= 0.0 && p.energy() <= 100.0);
    CHECK(p.hunger() >= 0.0 && p.hunger() <= 100.0);
    CHECK(p.thirst() >= 0.0 && p.thirst() <= 100.0);
    CHECK(p.health() >= 0.0 && p.health() <= 100.0);
    CHECK(p.bodyTemp() >= 20.0 && p.bodyTemp() <= 44.0);
  }
  CHECK(!p.alive()); // eventually starved/dehydrated
}

TEST(physiology_serdes_roundtrip) {
  Physiology a, b;
  a.reset();
  a.update(3600.0 * 5, 18.0, Activity::Move);
  a.setSleeping(true);
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK(a.energy() == b.energy());
  CHECK(a.hunger() == b.hunger());
  CHECK(a.thirst() == b.thirst());
  CHECK(a.sleepPressure() == b.sleepPressure());
  CHECK(a.health() == b.health());
  CHECK(a.isSleeping() == b.isSleeping());
}
