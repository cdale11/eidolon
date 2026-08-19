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
  SimClock c;
  a.update(c, 3600, ra);
  const Plant* p0 = a.nearestEdiblePlant(a.organismPos(), 8);
  if (p0) a.consumePlant(p0->pos, 3.0);
  a.update(c, 7200, ra);
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { a.serialize(w); });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return b.deserialize(r); }, err);
  CHECK(ok);
  CHECK_EQ(a.grid().hash(), b.grid().hash());
  CHECK(a.organismPos() == b.organismPos());
  CHECK_EQ(a.plants().size(), b.plants().size());
  for (size_t i = 0; i < a.plants().size(); ++i) {
    CHECK(a.plants()[i].pos == b.plants()[i].pos);
    CHECK_EQ(a.plants()[i].amount, b.plants()[i].amount);
  }
}

TEST(grid_serialize_roundtrip_full_fidelity) {
  // Regression: Grid::serialize/deserialize passed the ELEMENT count to
  // bytes() (which takes a BYTE count), so the last 3/4 of each float grid
  // (elevation/temperature/humidity) was dropped. A resumed process saw zeros
  // there and diverged from the uninterrupted run. Verify every element
  // survives a default-constructed round-trip.
  Grid a, b;
  Rng r(7);
  a.generate(64, 64, r);
  BinaryWriter w;
  a.serialize(w);
  BinaryReader rd(w.data());
  CHECK(b.deserialize(rd));
  CHECK(rd.done());
  CHECK_EQ(a.width(), b.width());
  CHECK_EQ(a.height(), b.height());
  const int w_ = a.width(), h_ = a.height();
  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) {
      CHECK(a.at(x, y) == b.at(x, y));
      CHECK(a.biome(x, y) == b.biome(x, y));
      CHECK_EQ(a.elevation(x, y), b.elevation(x, y));
      CHECK_EQ(a.temperature(x, y), b.temperature(x, y));
      CHECK_EQ(a.humidity(x, y), b.humidity(x, y));
    }
  }
  // Spot-check a high index explicitly: the last element of each float grid.
  CHECK_EQ(a.elevation(w_ - 1, h_ - 1), b.elevation(w_ - 1, h_ - 1));
  CHECK_EQ(a.humidity(w_ - 1, h_ - 1), b.humidity(w_ - 1, h_ - 1));
}

TEST(world_generation_places_plants) {
  World w;
  Rng r(123);
  w.generate(128, 128, r);
  CHECK(w.plants().size() >= 16); // ~164 expected at 1/100 density
  for (const Plant& pl : w.plants()) {
    CHECK(w.grid().walkable(pl.pos.x, pl.pos.y));
    CHECK(w.grid().at(pl.pos.x, pl.pos.y) != Terrain::Desert);
    CHECK(pl.amount > 0.0 && pl.amount <= pl.maxAmount);
    CHECK(pl.type == PlantType::Edible || pl.type == PlantType::Toxic || pl.type == PlantType::Medicinal || pl.type == PlantType::Wood);
  }
}

TEST(world_plants_regrow_capped) {
  World w;
  Rng r(5);
  w.generate(32, 32, r);
  SimClock c;
  w.update(c, 10 * 86400, r); // 10 days (wildlife grazes some plants)
  bool anyLiving = false;
  for (const Plant& pl : w.plants()) {
    CHECK(pl.amount <= pl.maxAmount + 1e-9); // regrowth never exceeds the cap
    if (pl.amount >= 1.0) anyLiving = true;
  }
  CHECK(anyLiving); // the plant population survives wildlife grazing
}

TEST(world_consume_and_regrow) {
  World w;
  Rng r(9);
  w.generate(32, 32, r);
  const Plant* p0 = w.nearestEdiblePlant(w.organismPos(), 8);
  CHECK(p0 != nullptr);
  const double before = p0->amount;
  const double eaten = w.consumePlant(p0->pos, 5.0);
  CHECK(eaten > 0.0 && eaten <= 5.0);
  SimClock c;
  w.update(c, 5400, r); // 1.5 hours → +1 berry
  CHECK_EQ(w.plants()[0].amount, before - eaten + 1.0);
}

TEST(world_adjacent_to_water) {
  World w;
  Rng r(11);
  w.generate(64, 64, r);
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
  Vec2i p = w.organismPos();
  CHECK(!w.adjacentToWater(p));
}

TEST(world_nearest_plant_visibility) {
  World w;
  Rng r(13);
  w.generate(64, 64, r);
  const Plant* pl = w.nearestEdiblePlant(w.organismPos(), 8);
  CHECK(pl != nullptr);
  const Plant* found = w.nearestEdiblePlant(pl->pos, 0);
  CHECK(found != nullptr && found->pos == pl->pos);
  CHECK(w.nearestEdiblePlant(pl->pos, 0) != nullptr);
}

TEST(perception_feature_vector_shape) {
  World w;
  Rng r(21);
  w.generate(64, 64, r);
  SimClock c;
  c.set(43200); // noon
  const Perception p = w.perceive(w.organismPos(), c);
  CHECK_EQ(Perception::kFeatures, 28);
  CHECK(p[0] >= 0.0 && p[0] <= 1.0); // hour
  CHECK(p[1] >= 0.0 && p[1] <= 3.0); // weather code
  CHECK(p[2] >= 0.0 && p[2] <= 1.0); // temp
  CHECK(p[3] >= 0.0 && p[3] <= 1.0); // season
  CHECK(p[4] >= 0.0 && p[4] <= 1.0); // terrain
  CHECK(p[5] >= 0.0 && p[5] <= 1.0); // food distance
  CHECK(p[6] >= -1.0 && p[6] <= 1.0);
  CHECK(p[7] >= -1.0 && p[7] <= 1.0);
  CHECK(p[8] >= 0.0 && p[8] <= 1.0); // food fullness
  CHECK(p[9] >= 0.0 && p[9] <= 1.0); // water distance
  CHECK(p[10] >= -1.0 && p[10] <= 1.0);
  CHECK(p[11] >= -1.0 && p[11] <= 1.0);
  CHECK(p[12] >= 0.0 && p[12] <= 1.0); // plants in sight
  CHECK(p[20] >= 0.0 && p[20] <= 1.0); // prey distance
  CHECK(p[21] >= -1.0 && p[21] <= 1.0);
  CHECK(p[22] >= -1.0 && p[22] <= 1.0);
  CHECK(p[23] >= 0.0 && p[23] <= 1.0); // predator distance
  CHECK(p[24] >= -1.0 && p[24] <= 1.0);
  CHECK(p[25] >= -1.0 && p[25] <= 1.0);
  CHECK(p[26] >= 0.0 && p[26] <= 1.0); // prey count
  CHECK(p[27] >= 0.0 && p[27] <= 1.0); // predator count
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
  for (int i = 0; i < 100; ++i) a.update(c, 3600, r);
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