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
  // Simulate some life so bushes have been consumed/regrown.
  SimClock c;
  a.update(c, 3600, ra);
  a.consumeBerries(a.bushes()[0].pos, 3.0);
  a.update(c, 7200, ra);
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK_EQ(a.grid().hash(), b.grid().hash());
  CHECK(a.organismPos() == b.organismPos());
  CHECK_EQ(a.bushes().size(), b.bushes().size());
  for (size_t i = 0; i < a.bushes().size(); ++i) {
    CHECK(a.bushes()[i].pos == b.bushes()[i].pos);
    CHECK_EQ(a.bushes()[i].berries, b.bushes()[i].berries);
  }
}

TEST(world_generation_places_bushes) {
  World w;
  Rng r(123);
  w.generate(128, 128, r);
  CHECK(w.bushes().size() >= 16); // ~64 expected at 1/256 density
  for (const Bush& b : w.bushes()) {
    CHECK(w.grid().walkable(b.pos.x, b.pos.y));
    CHECK(w.grid().at(b.pos.x, b.pos.y) != Terrain::Desert);
    CHECK(b.berries > 0.0 && b.berries <= 10.0);
  }
}

TEST(world_berries_regrow_capped) {
  World w;
  Rng r(5);
  w.generate(32, 32, r);
  SimClock c;
  w.update(c, 10 * 86400, r); // 10 days
  for (const Bush& b : w.bushes()) CHECK_EQ(b.berries, 10.0);
}

TEST(world_consume_and_regrow) {
  World w;
  Rng r(9);
  w.generate(32, 32, r);
  const Bush& b0 = w.bushes()[0];
  const double before = b0.berries;
  const double eaten = w.consumeBerries(b0.pos, 5.0);
  CHECK(eaten > 0.0 && eaten <= 5.0);
  SimClock c;
  w.update(c, 5400, r); // 1.5 hours → +1 berry
  CHECK_EQ(w.bushes()[0].berries, before - eaten + 1.0);
}

TEST(world_adjacent_to_water) {
  World w;
  Rng r(11);
  w.generate(64, 64, r);
  // Find a water tile with a walkable neighbor and check adjacency from it.
  bool checked = false;
  for (int y = 1; y < 63 && !checked; ++y) {
    for (int x = 1; x < 63; ++x) {
      if (w.grid().at(x, y) == Terrain::Water && w.grid().walkable(x, y - 1)) {
        CHECK(w.adjacentToWater({x, y - 1}));
        checked = true;
        break;
      }
    }
  }
  CHECK(checked);
  // Deep in walkable land, not adjacent.
  Vec2i p = w.organismPos();
  CHECK(!w.adjacentToWater(p));
}

TEST(world_nearest_bush_visibility) {
  World w;
  Rng r(13);
  w.generate(64, 64, r);
  const Bush& b = w.bushes()[0];
  const Bush* found = w.nearestBush(b.pos, 0);
  CHECK(found != nullptr && found->pos == b.pos);
  CHECK(w.nearestBush(b.pos, 0) != nullptr);
}

TEST(perception_feature_vector_shape) {
  World w;
  Rng r(21);
  w.generate(64, 64, r);
  SimClock c;
  c.set(43200); // noon
  const Perception p = w.perceive(w.organismPos(), c);
  CHECK_EQ(Perception::kFeatures, 12);
  CHECK(p[0] >= 0.0 && p[0] <= 1.0); // hour
  CHECK(p[1] >= 0.0 && p[1] <= 3.0); // weather code
  CHECK(p[2] >= 0.0 && p[2] <= 1.0); // temp
  CHECK(p[3] >= 0.0 && p[3] <= 1.0); // terrain
  CHECK(p[4] >= 0.0 && p[4] <= 1.0); // food distance
  CHECK(p[5] >= -1.0 && p[5] <= 1.0);
  CHECK(p[6] >= -1.0 && p[6] <= 1.0);
  CHECK(p[8] >= 0.0 && p[8] <= 1.0); // water distance
  CHECK(p[11] >= 0.0 && p[11] <= 1.0);
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
