// World for Phase 5: tile grid with terrain, biomes, noise-field generation,
// day/night, seasons, weather, plants, water sources, and wildlife.
#ifndef EIDOLON_WORLD_HPP
#define EIDOLON_WORLD_HPP

#include <cstdint>
#include <vector>

#include "core/clock.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "world/noise.hpp"

namespace eidolon {

enum class Terrain : uint8_t {
  Plains = 0,
  Forest = 1,
  Water = 2,
  Hills = 3,
  Desert = 4,
  // Phase 5 additions
  Mountain = 5,
  Swamp = 6,
  Tundra = 7,
  River = 8,
};

enum class Biome : uint8_t {
  TemperatePlains = 0,
  TemperateForest = 1,
  BorealForest = 2,
  Tundra = 3,
  Desert = 4,
  Savannah = 5,
  Jungle = 5,  // Alias for tropical forest
  Mountain = 6,
  Swamp = 7,
  WaterBody = 8,
  River = 9,
};

struct Vec2i {
  int x = 0;
  int y = 0;
  bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Vec2i& o) const { return !(*this == o); }
};

inline int distCheb(Vec2i a, Vec2i b) {
  const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
  const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
  return dx > dy ? dx : dy;
}

class Grid {
public:
  Grid() = default;
  Grid(int w, int h) : w_(w), h_(h), tiles_(static_cast<size_t>(w) * h, Terrain::Plains),
                        biomes_(static_cast<size_t>(w) * h, Biome::TemperatePlains),
                        elevation_(static_cast<size_t>(w) * h, 0.0f),
                        temperature_(static_cast<size_t>(w) * h, 0.0f),
                        humidity_(static_cast<size_t>(w) * h, 0.0f) {}

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
  Biome biome(int x, int y) const {
    return inBounds(x, y) ? biomes_[static_cast<size_t>(y) * w_ + x] : Biome::WaterBody;
  }
  float elevation(int x, int y) const {
    return inBounds(x, y) ? elevation_[static_cast<size_t>(y) * w_ + x] : -1.0f;
  }
  float temperature(int x, int y) const {
    return inBounds(x, y) ? temperature_[static_cast<size_t>(y) * w_ + x] : 0.0f;
  }
  float humidity(int x, int y) const {
    return inBounds(x, y) ? humidity_[static_cast<size_t>(y) * w_ + x] : 0.0f;
  }
  bool walkable(int x, int y) const {
    Terrain t = at(x, y);
    return t != Terrain::Water && t != Terrain::River;
  }

  Vec2i randomWalkable(Rng& r) const;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

  uint64_t hash() const noexcept;

private:
  int w_ = 0;
  int h_ = 0;
  std::vector<Terrain> tiles_;
  std::vector<Biome> biomes_;
  std::vector<float> elevation_;
  std::vector<float> temperature_;
  std::vector<float> humidity_;
};

class Weather {
public:
  double ambientTempC(const SimClock& c) const noexcept;

  void update(const SimClock& c, int64_t dt, Rng& r);
  bool raining() const { return raining_; }
  bool snowing() const { return snowing_; }
  bool storming() const { return storming_; }
  const char* describe() const {
    if (storming_) return "storm";
    if (snowing_) return "snow";
    if (raining_) return "rain";
    return "clear";
  }
  double humidity() const { return humidity_; }
  double windSpeed() const { return windSpeed_; }
  int season() const { return season_; }

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  bool raining_ = false;
  bool snowing_ = false;
  bool storming_ = false;
  double humidity_ = 0.5;
  double windSpeed_ = 0.0;
  int season_ = 0;
  int64_t lastChange_ = -1800;
};

enum class PlantType : uint8_t {
  Edible = 0,
  Toxic = 1,
  Medicinal = 2,
  Wood = 3,
};

struct Plant {
  Vec2i pos;
  PlantType type = PlantType::Edible;
  double amount = 0.0;
  double maxAmount = 10.0;
  double regrowthRate = 0.01;
  double toxicity = 0.0;
  double medicinalValue = 0.0;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

enum class WaterType : uint8_t {
  River = 0,
  Lake = 1,
  Spring = 2,
  Pond = 3,
};

struct WaterSource {
  Vec2i pos;
  WaterType type = WaterType::River;
  double capacity = 100.0;
  double current = 100.0;
  double flowRate = 0.1;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};

struct Perception {
  static constexpr int kSightRadius = 8;
  static constexpr int kHearingRadius = 16;
  static constexpr int kFeatures = 20;

  double f[20] = {};

  double& operator[](size_t i) { return f[i]; }
  const double& operator[](size_t i) const { return f[i]; }
};

class World {
public:
  World() = default;

  void generate(int w, int h, Rng& r);
  bool update(const SimClock& c, int64_t dt, Rng& r);

  const Grid& grid() const { return grid_; }
  Grid& grid() { return grid_; }
  const Weather& weather() const { return weather_; }
  Weather& weather() { return weather_; }
  Vec2i organismPos() const { return pos_; }
  void setOrganismPos(Vec2i p) { pos_ = p; }
  bool organismAlive() const { return alive_; }
  void killOrganism() { alive_ = false; }

  const std::vector<Plant>& plants() const { return plants_; }
  const std::vector<WaterSource>& waterSources() const { return waterSources_; }

  Perception perceive(Vec2i pos, const SimClock& c) const;

  bool adjacentToWater(Vec2i pos) const;
  const Plant* nearestEdiblePlant(Vec2i pos, int radius) const;
  const WaterSource* nearestWaterSource(Vec2i pos, int radius) const;
  double consumePlant(Vec2i pos, double amount);
  double drinkFromSource(Vec2i pos, double amount);

  // Nearest walkable tile adjacent to a water/river tile (shore); {-1,-1} if none.
  Vec2i adjacentWalkable(Vec2i water) const;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  Grid grid_;
  Weather weather_;
  SimplexNoise elevationNoise_;
  SimplexNoise temperatureNoise_;
  SimplexNoise humidityNoise_;
  Vec2i pos_ = {0, 0};
  bool alive_ = true;
  std::vector<Plant> plants_;
  std::vector<WaterSource> waterSources_;
};

} // namespace eidolon

#endif // EIDOLON_WORLD_HPP
