// World for Phase 2: tile grid with terrain, seeded generation, day/night and weather
// statistics, plus living resources: berry bushes (food) and drinkable water. Provides
// perception (sight/hearing radii → compact feature vector) for the organism's drives.
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

// Chebyshev distance (grid steps including diagonals).
inline int distCheb(Vec2i a, Vec2i b) {
  const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
  const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
  return dx > dy ? dx : dy;
}

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

// A berry bush: the organism's only food source in Phase 2. Berries regrow slowly.
struct Bush {
  Vec2i pos;
  double berries = 0.0;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

// Perception snapshot for the organism (Phase 2: no attention — compact fixed vector).
struct Perception {
  static constexpr int kSightRadius = 8;
  static constexpr int kHearingRadius = 16;
  static constexpr int kFeatures = 12;

  // Feature vector (normalized to ~[-1,1] / [0,1]); feature order is stable for learning.
  // [0] hourOfDay/24  [1] weather code (0=clear,1=rain,2=storm,3=snow) [2] ambient temp
  // normalized to [-1,1] (0°C→0, 25°C→1, -5°C→-0.2) [3] terrain code/4 [4] nearest bush
  // distance normalized (0=adjacent, 1=sight edge or beyond) [5..6] bush direction (±1)
  // [7] bush berry fullness [8] nearest water distance normalized [9..10] water direction
  // [11] bushes within sight (clamped 0..4 /4)
  double f[kFeatures] = {};

  double& operator[](size_t i) { return f[i]; }
  const double& operator[](size_t i) const { return f[i]; }
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

  const std::vector<Bush>& bushes() const { return bushes_; }

  // Perception.
  Perception perceive(Vec2i pos, const SimClock& c) const;

  // Returns true if `pos` is on a dry tile adjacent (incl. diagonal) to water.
  bool adjacentToWater(Vec2i pos) const;
  // Nearest bush with berries to `pos` within `radius` (or null). Foraging target.
  const Bush* nearestBush(Vec2i pos, int radius) const;
  // Consume up to `amount` berries from the bush at `pos`; returns berries eaten.
  double consumeBerries(Vec2i pos, double amount);

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  Grid grid_;
  Weather weather_;
  Vec2i pos_ = {0, 0};
  bool alive_ = true;
  std::vector<Bush> bushes_;
};

} // namespace eidolon