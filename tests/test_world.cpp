#include "harness.hpp"

#include "core/rng.hpp"
#include "world/world.hpp"

using namespace eidolon;

TEST(world_generation_deterministic) {
  World a, b;
  Rng ra(123), rb(123);
  a.generate(64, 64, ra);
  b.generate(64, 64, rb);
  CHECK_EQ(a.grid().hash(), b.grid().hash());
}

TEST(world_generation_differs_by_seed) {
  World a, b;
  Rng ra(123), rb(124);
  a.generate(64, 64, ra);
  b.generate(64, 64, rb);
  CHECK(a.grid().hash() != b.grid().hash());
}

TEST(world_generation_contains_water) {
  World w;
  Rng r(123);
  w.generate(64, 64, r);
  bool foundWater = false;
  for (int y = 0; y < 64 && !foundWater; ++y) {
    for (int x = 0; x < 64; ++x) {
      if (w.grid().at(x, y) == Terrain::Water) { foundWater = true; break; }
    }
  }
  CHECK(foundWater);
}

TEST(world_spawn_is_walkable) {
  World w;
  Rng r(7);
  w.generate(128, 128, r);
  const Vec2i p = w.organismPos();
  CHECK(w.grid().inBounds(p.x, p.y));
  CHECK(w.grid().walkable(p.x, p.y));
}

TEST(world_out_of_bounds_is_water) {
  World w;
  Rng r(1);
  w.generate(16, 16, r);
  CHECK_EQ(w.grid().at(-1, 0), Terrain::Water);
  CHECK_EQ(w.grid().at(0, -1), Terrain::Water);
  CHECK_EQ(w.grid().at(16, 0), Terrain::Water);
  CHECK_EQ(w.grid().at(0, 16), Terrain::Water);
}

TEST(world_serialize_roundtrip) {
  World a, b;
  Rng ra(42), rb(42);
  a.generate(48, 48, ra);
  b.generate(48, 48, rb);
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK_EQ(a.grid().hash(), b.grid().hash());
  CHECK(a.organismPos() == b.organismPos());
}

TEST(weather_temperature_bounded) {
  Weather w;
  SimClock c;
  for (int64_t t = 0; t < 4 * 365 * 86400; t += 3600) {
    c.set(t);
    const double temp = w.ambientTempC(c);
    CHECK(temp >= -15.0 && temp <= 40.0);
  }
}

TEST(weather_serdes_roundtrip) {
  Weather a, b;
  SimClock c;
  Rng r(3);
  for (int i = 0; i < 100; ++i) a.update(c, r);
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK_EQ(a.raining(), b.raining());
  CHECK_EQ(a.snowing(), b.snowing());
  CHECK_EQ(a.storming(), b.storming());
}
