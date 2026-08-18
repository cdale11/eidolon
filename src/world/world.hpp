// Minimal world for Phase 1: tile grid with terrain, seeded generation, day/night and
// weather statistics (temperature, precipitation). Entities/flora/fauna arrive in later
// phases; the spatial index stub keeps the API stable.
#pragma once

#include <cstdint>
#include <vector>

#include "core/clock.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"

namespace eidolon {

enum class Terrain : uint8_t {
  Plains = 0,
  Forest = 1,
  Water = 2,
  Hills = 3,
  Desert = 4,
};

struct Vec2i {
  int x = 0;
  int y = 0;
  bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Vec2i& o) const { return !(*this == o); }
};

class Grid {
public:
  Grid() = default;
  Grid(int w, int h) : w_(w), h_(h), tiles_(static_cast<size_t>(w) * h, Terrain::Plains) {}

  void generate(int w, int h, Rng& r);
  void generate(Rng& r) { generate(w_, h_, r); }

  int width() const { return w_; }
  int height() const { return h_; }
  bool inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < w_ && y < h_;
  }
  Terrain at(int x, int y) const {
    return inBounds(x, y) ? tiles_[static_cast<size_t>(y) * w_ + x] : Terrain::Water;
  }
  bool walkable(int x, int y) const { return at(x, y) != Terrain::Water; }

  // Random walkable cell (used for spawn/exploration).
  Vec2i randomWalkable(Rng& r) const;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

  // Deterministic hash of tile contents (for tests).
  uint64_t hash() const noexcept;

private:
  int w_ = 0;
  int h_ = 0;
  std::vector<Terrain> tiles_;
};

class Weather {
public:
  // Ambient temperature at a sim time: seasonal baseline + diurnal swing + weather
  // modifiers. Deterministic in sim time (no RNG).
  double ambientTempC(const SimClock& c) const noexcept;

  void update(const SimClock& c, Rng& r); // state transitions (rain/storm), cooldown-gated
  bool raining() const { return raining_; }
  bool snowing() const { return snowing_; }
  bool storming() const { return storming_; }
  const char* describe() const {
    if (storming_) return "storm";
    if (snowing_) return "snow";
    if (raining_) return "rain";
    return "clear";
  }

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  bool raining_ = false;
  bool snowing_ = false;
  bool storming_ = false;
  int64_t lastChange_ = -1800; // sim-time of last state change (cooldown gating)
};

class World {
public:
  World() = default;

  void generate(int w, int h, Rng& r);
  // Returns true if the weather state changed this update (event-worthy).
  bool update(const SimClock& c, int64_t dt, Rng& r);

  const Grid& grid() const { return grid_; }
  Grid& grid() { return grid_; }
  const Weather& weather() const { return weather_; }
  Weather& weather() { return weather_; }
  Vec2i organismPos() const { return pos_; }
  void setOrganismPos(Vec2i p) { pos_ = p; }
  bool organismAlive() const { return alive_; }
  void killOrganism() { alive_ = false; }

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  Grid grid_;
  Weather weather_;
  Vec2i pos_ = {0, 0};
  bool alive_ = true;
};

} // namespace eidolon
