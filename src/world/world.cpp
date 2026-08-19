#include "world/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

#include "core/clock.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ---------------------------------------------------------------------------
// Grid generation with noise-field biomes
// ---------------------------------------------------------------------------

void eidolon::Grid::generate(int w, int h, eidolon::Rng& r) {
  w_ = w;
  h_ = h;
  tiles_.assign(static_cast<size_t>(w) * h, eidolon::Terrain::Plains);
  biomes_.assign(static_cast<size_t>(w) * h, eidolon::Biome::TemperatePlains);
  elevation_.assign(static_cast<size_t>(w) * h, 0.0f);
  temperature_.assign(static_cast<size_t>(w) * h, 0.0f);
  humidity_.assign(static_cast<size_t>(w) * h, 0.0f);

  eidolon::SimplexNoise elevationNoise(r.next());
  eidolon::SimplexNoise temperatureNoise(r.next());
  eidolon::SimplexNoise humidityNoise(r.next());

  // Frequency scale: ~25-tile features give the terrain real relief (hills, valleys,
  // steep faces). Phase 5 hazards (cliffs, damaging falls, deep water) need this.
  const float freqScale = 0.01f;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float nx = x * freqScale;
      float ny = y * freqScale;

      float elev = elevationNoise.fbm(nx, ny, 5, 0.5f, 2.0f);
      float temp = temperatureNoise.fbm(nx, ny, 4, 0.5f, 2.0f);
      float hum = humidityNoise.fbm(nx, ny, 4, 0.5f, 2.0f);

      elevation_[static_cast<size_t>(y) * w + x] = elev;
      temperature_[static_cast<size_t>(y) * w + x] = temp;
      humidity_[static_cast<size_t>(y) * w + x] = hum;

      eidolon::Terrain t = eidolon::Terrain::Plains;
      eidolon::Biome b = eidolon::Biome::TemperatePlains;

      if (elev > 0.6f) {
        t = eidolon::Terrain::Mountain;
        b = eidolon::Biome::Mountain;
      } else if (elev < -0.5f) {
        t = eidolon::Terrain::Water;
        b = eidolon::Biome::WaterBody;
      } else if (elev < -0.3f) {
        t = eidolon::Terrain::River;
        b = eidolon::Biome::River;
      } else if (hum > 0.5f && temp > 0.0f) {
        t = eidolon::Terrain::Forest;
        b = temp > 0.3f ? eidolon::Biome::Jungle : eidolon::Biome::TemperateForest;
      } else if (hum < -0.3f && temp > 0.2f) {
        t = eidolon::Terrain::Desert;
        b = eidolon::Biome::Desert;
      } else if (hum > 0.2f && temp < -0.2f) {
        t = eidolon::Terrain::Swamp;
        b = eidolon::Biome::Swamp;
      } else if (temp < -0.5f) {
        t = eidolon::Terrain::Tundra;
        b = eidolon::Biome::Tundra;
      } else if (hum > 0.0f) {
        t = eidolon::Terrain::Forest;
        b = eidolon::Biome::TemperateForest;
      } else {
        t = eidolon::Terrain::Plains;
        b = temp > 0.0f ? eidolon::Biome::Savannah : eidolon::Biome::TemperatePlains;
      }

      if (r.unit() < 0.05) {
        const eidolon::Terrain neighbors[5] = {eidolon::Terrain::Plains, eidolon::Terrain::Forest, eidolon::Terrain::Hills, eidolon::Terrain::Desert, eidolon::Terrain::Swamp};
        t = neighbors[r.range(5)];
      }

      tiles_[static_cast<size_t>(y) * w + x] = t;
      biomes_[static_cast<size_t>(y) * w + x] = b;
    }
  }

  for (int i = 0; i < w * h / 200; ++i) {
    int x = r.range(static_cast<uint64_t>(w));
    int y = r.range(static_cast<uint64_t>(h));
    if (elevation(x, y) > 0.2f && at(x, y) != eidolon::Terrain::Water) {
      int cx = x, cy = y;
      for (int step = 0; step < 50; ++step) {
        if (at(cx, cy) == eidolon::Terrain::Water) break;
        tiles_[static_cast<size_t>(cy) * w + cx] = eidolon::Terrain::River;
        biomes_[static_cast<size_t>(cy) * w + cx] = eidolon::Biome::River;

        float bestElev = elevation(cx, cy);
        int nx = cx, ny = cy;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (inBounds(cx + dx, cy + dy)) {
              float e = elevation(cx + dx, cy + dy);
              if (e < bestElev) {
                bestElev = e;
                nx = cx + dx;
                ny = cy + dy;
              }
            }
          }
        }
        if (nx == cx && ny == cy) break;
        cx = nx; cy = ny;
      }
    }
  }

  for (int pass = 0; pass < 2; ++pass) {
    std::vector<eidolon::Terrain> next = tiles_;
    std::vector<eidolon::Biome> nextB = biomes_;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        int counts[9] = {0};
        int bcounts[10] = {0};
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            eidolon::Terrain t = at(x + dx, y + dy);
            eidolon::Biome b = biome(x + dx, y + dy);
            counts[static_cast<int>(t)]++;
            bcounts[static_cast<int>(b)]++;
          }
        }
        int bestT = static_cast<int>(at(x, y));
        int bestB = static_cast<int>(biome(x, y));
        for (int k = 0; k < 9; ++k) if (counts[k] > counts[bestT]) bestT = k;
        for (int k = 0; k < 10; ++k) if (bcounts[k] > bcounts[bestB]) bestB = k;
        next[static_cast<size_t>(y) * w + x] = static_cast<eidolon::Terrain>(bestT);
        nextB[static_cast<size_t>(y) * w + x] = static_cast<eidolon::Biome>(bestB);
      }
    }
    tiles_ = std::move(next);
    biomes_ = std::move(nextB);
  }

  if (w >= 8 && h >= 8) {
    int cx = static_cast<int>(r.range(static_cast<uint64_t>(w - 6))) + 3;
    int cy = static_cast<int>(r.range(static_cast<uint64_t>(h - 6))) + 3;
    int radius = 1 + static_cast<int>(r.range(2));
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy <= radius * radius) {
          int tx = cx + dx, ty = cy + dy;
          if (inBounds(tx, ty)) {
            tiles_[static_cast<size_t>(ty) * w + tx] = eidolon::Terrain::Water;
            biomes_[static_cast<size_t>(ty) * w + tx] = eidolon::Biome::WaterBody;
          }
        }
      }
    }
  }
} // Grid::generate

eidolon::Vec2i eidolon::Grid::randomWalkable(eidolon::Rng& r) const {
  for (int attempt = 0; attempt < 64; ++attempt) {
    int x = static_cast<int>(r.range(static_cast<uint64_t>(w_)));
    int y = static_cast<int>(r.range(static_cast<uint64_t>(h_)));
    if (walkable(x, y)) return {x, y};
  }
  return {w_ / 2, h_ / 2};
}

void eidolon::Grid::serialize(eidolon::BinaryWriter& w) const {
  w.i64(w_);
  w.i64(h_);
  w.u64(static_cast<uint64_t>(tiles_.size()));
  w.bytes(tiles_.data(), tiles_.size());
  w.bytes(biomes_.data(), biomes_.size());
  w.bytes(elevation_.data(), elevation_.size() * sizeof(float));
  w.bytes(temperature_.data(), temperature_.size() * sizeof(float));
  w.bytes(humidity_.data(), humidity_.size() * sizeof(float));
}

bool eidolon::Grid::deserialize(eidolon::BinaryReader& r) {
  int64_t w, h;
  uint64_t n;
  if (!r.i64(w) || !r.i64(h) || !r.u64(n)) return false;
  if (w <= 0 || h <= 0 || n != static_cast<uint64_t>(w * h) || n > (1u << 28)) return false;
  w_ = static_cast<int>(w);
  h_ = static_cast<int>(h);
  tiles_.resize(static_cast<size_t>(n));
  biomes_.resize(static_cast<size_t>(n));
  elevation_.resize(static_cast<size_t>(n));
  temperature_.resize(static_cast<size_t>(n));
  humidity_.resize(static_cast<size_t>(n));
  if (!r.bytes(tiles_.data(), tiles_.size())) return false;
  if (!r.bytes(biomes_.data(), biomes_.size())) return false;
  if (!r.bytes(elevation_.data(), elevation_.size() * sizeof(float))) return false;
  if (!r.bytes(temperature_.data(), temperature_.size() * sizeof(float))) return false;
  if (!r.bytes(humidity_.data(), humidity_.size() * sizeof(float))) return false;
  return true;
}

uint64_t eidolon::Grid::hash() const noexcept {
  uint64_t h = 1469598103934665603ULL;
  const auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 0x100000001b3ULL;
  };
  mix(static_cast<uint64_t>(w_));
  mix(static_cast<uint64_t>(h_));
  for (const eidolon::Terrain t : tiles_) mix(static_cast<uint64_t>(static_cast<uint8_t>(t)));
  return h;
}

// ---------------------------------------------------------------------------
// Weather with seasons
// ---------------------------------------------------------------------------

double eidolon::Weather::ambientTempC(const eidolon::SimClock& c) const noexcept {
  const double seasonal = 10.0 + 15.0 * std::cos(2.0 * kPi * (c.yearFraction() - 0.25));
  const double diurnal = 4.0 * std::sin(2.0 * kPi * (c.hourOfDay() - 9.0) / 24.0);
  double t = seasonal + diurnal;
  if (raining_ || storming_) t -= 3.0;
  if (snowing_) t -= 6.0;
  return t;
}

void eidolon::Weather::update(const eidolon::SimClock& c, int64_t dt, eidolon::Rng& r) {
  (void)dt;
  season_ = static_cast<int>(c.yearFraction() * 4) % 4;

  const int64_t kCooldown = 1800;
  if (c.now() - lastChange_ < kCooldown) return;
  if (!r.chance(0.05)) return;
  lastChange_ = c.now();

  const double temp = ambientTempC(c);
  double rainChance = 0.3, snowChance = 0.0, stormChance = 0.1;
  if (season_ == 3) { snowChance = 0.4; rainChance = 0.2; }
  else if (season_ == 0) { rainChance = 0.5; stormChance = 0.15; }
  else if (season_ == 1) { rainChance = 0.4; stormChance = 0.2; }
  else { rainChance = 0.4; stormChance = 0.15; }

  if (temp <= 1.0) {
    snowing_ = r.unit() < snowChance + 0.3;
    raining_ = !snowing_ && r.unit() < rainChance;
  } else {
    raining_ = r.unit() < rainChance;
    snowing_ = false;
  }
  storming_ = (raining_ || snowing_) && r.unit() < stormChance;

  humidity_ = 0.5 + 0.3 * std::sin(2.0 * kPi * c.yearFraction());
  if (raining_ || storming_) humidity_ = std::min(1.0, humidity_ + 0.3);
  windSpeed_ = 2.0 + 3.0 * r.unit();
  if (storming_) windSpeed_ += 5.0;
}

void eidolon::Weather::serialize(eidolon::BinaryWriter& w) const {
  w.u8(raining_ ? 1 : 0);
  w.u8(snowing_ ? 1 : 0);
  w.u8(storming_ ? 1 : 0);
  w.f64(humidity_);
  w.f64(windSpeed_);
  w.u32(static_cast<uint32_t>(season_));
  w.i64(lastChange_);
}

bool eidolon::Weather::deserialize(eidolon::BinaryReader& r) {
  uint8_t a, b, c;
  uint32_t s;
  int64_t lastChange;
  if (!r.u8(a) || !r.u8(b) || !r.u8(c) || !r.f64(humidity_) ||
      !r.f64(windSpeed_) || !r.u32(s) || !r.i64(lastChange))
    return false;
  raining_ = a != 0;
  snowing_ = b != 0;
  storming_ = c != 0;
  season_ = static_cast<int>(s);
  lastChange_ = lastChange;
  return true;
}

void eidolon::Plant::serialize(eidolon::BinaryWriter& w) const {
  w.i64(pos.x);
  w.i64(pos.y);
  w.u8(static_cast<uint8_t>(type));
  w.f64(amount);
  w.f64(maxAmount);
  w.f64(regrowthRate);
  w.f64(toxicity);
  w.f64(medicinalValue);
}

bool eidolon::Plant::deserialize(eidolon::BinaryReader& r) {
  int64_t x, y;
  uint8_t t;
  if (!r.i64(x) || !r.i64(y) || !r.u8(t) || !r.f64(amount) ||
      !r.f64(maxAmount) || !r.f64(regrowthRate) || !r.f64(toxicity) || !r.f64(medicinalValue))
    return false;
  pos = {static_cast<int>(x), static_cast<int>(y)};
  type = static_cast<eidolon::PlantType>(t);
  return true;
}

void eidolon::WaterSource::serialize(eidolon::BinaryWriter& w) const {
  w.i64(pos.x);
  w.i64(pos.y);
  w.u8(static_cast<uint8_t>(type));
  w.f64(capacity);
  w.f64(current);
  w.f64(flowRate);
}

bool eidolon::WaterSource::deserialize(eidolon::BinaryReader& r) {
  int64_t x, y;
  uint8_t t;
  if (!r.i64(x) || !r.i64(y) || !r.u8(t) || !r.f64(capacity) ||
      !r.f64(current) || !r.f64(flowRate))
    return false;
  pos = {static_cast<int>(x), static_cast<int>(y)};
  type = static_cast<eidolon::WaterType>(t);
  return true;
}

void eidolon::World::generate(int w, int h, eidolon::Rng& r) {
  grid_.generate(w, h, r);
  infectionCA_.resize(w, h);
  pos_ = grid_.randomWalkable(r);

  plants_.clear();
  waterSources_.clear();

  // Guaranteed starter edible plant within sight of spawn, placed first so the
  // organism always has a first meal reachable and tests can rely on plants()[0].
  for (int d = 4; d <= 8; ++d) {
    bool placed = false;
    for (int dy = -d; dy <= d && !placed; ++dy) {
      for (int dx = -d; dx <= d && !placed; ++dx) {
        if (dx == 0 && dy == 0) continue;
        if (dx * dx + dy * dy > d * d) continue;
        const eidolon::Vec2i p = {pos_.x + dx, pos_.y + dy};
        if (grid_.inBounds(p.x, p.y) && grid_.walkable(p.x, p.y) &&
            grid_.at(p.x, p.y) != eidolon::Terrain::Desert) {
          eidolon::Plant pl;
          pl.pos = p;
          pl.type = eidolon::PlantType::Edible;
          pl.maxAmount = 10.0;
          pl.amount = 8.0;
          pl.regrowthRate = 1.0;
          pl.toxicity = 0.0;
          pl.medicinalValue = 0.0;
          plants_.push_back(pl);
          placed = true;
        }
      }
    }
    if (placed) break;
  }

  int targetPlants = static_cast<int>(static_cast<uint64_t>(w) * h / 100);
  int guard = targetPlants * 10;
  while (static_cast<int>(plants_.size()) < targetPlants && guard-- > 0) {
    eidolon::Vec2i p = grid_.randomWalkable(r);
    if (!grid_.walkable(p.x, p.y)) continue;
    if (distCheb(p, pos_) <= 3) continue;

    eidolon::Biome b = grid_.biome(p.x, p.y);
    eidolon::PlantType type;
    double toxicity = 0.0, medicinal = 0.0;

    if (b == eidolon::Biome::Jungle || b == eidolon::Biome::TemperateForest) {
      double roll = r.unit();
      if (roll < 0.6) type = eidolon::PlantType::Edible;
      else if (roll < 0.8) { type = eidolon::PlantType::Medicinal; medicinal = 0.5 + r.unit() * 0.5; }
      else { type = eidolon::PlantType::Toxic; toxicity = 0.3 + r.unit() * 0.4; }
    } else if (b == eidolon::Biome::Swamp) {
      double roll = r.unit();
      if (roll < 0.4) type = eidolon::PlantType::Edible;
      else if (roll < 0.6) { type = eidolon::PlantType::Medicinal; medicinal = 0.3 + r.unit() * 0.4; }
      else { type = eidolon::PlantType::Toxic; toxicity = 0.4 + r.unit() * 0.5; }
    } else if (b == eidolon::Biome::Desert) {
      double roll = r.unit();
      if (roll < 0.3) type = eidolon::PlantType::Edible;
      else { type = eidolon::PlantType::Toxic; toxicity = 0.5 + r.unit() * 0.3; }
    } else if (b == eidolon::Biome::Mountain) {
      double roll = r.unit();
      if (roll < 0.4) type = eidolon::PlantType::Edible;
      else if (roll < 0.6) { type = eidolon::PlantType::Medicinal; medicinal = 0.4 + r.unit() * 0.3; }
      else type = eidolon::PlantType::Wood;
    } else {
      double roll = r.unit();
      if (roll < 0.7) type = eidolon::PlantType::Edible;
      else if (roll < 0.9) { type = eidolon::PlantType::Medicinal; medicinal = 0.2 + r.unit() * 0.3; }
      else { type = eidolon::PlantType::Toxic; toxicity = 0.2 + r.unit() * 0.3; }
    }

    eidolon::Plant pl;
    pl.pos = p;
    pl.type = type;
    pl.maxAmount = 5.0 + r.unit() * 15.0;
    pl.amount = pl.maxAmount * (0.5 + r.unit() * 0.5);
    pl.regrowthRate = 0.5 + r.unit() * 1.0;
    pl.toxicity = toxicity;
    pl.medicinalValue = medicinal;
    plants_.push_back(pl);
  }

  int riverCount = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (grid_.at(x, y) == eidolon::Terrain::River) {
        if (r.unit() < 0.1 && riverCount < w * h / 500) {
          const eidolon::Vec2i shore = adjacentWalkable({x, y});
          if (shore.x >= 0) {
            eidolon::WaterSource ws;
            ws.pos = shore;
            ws.type = eidolon::WaterType::River;
            ws.capacity = 50.0 + r.unit() * 50.0;
            ws.current = ws.capacity;
            ws.flowRate = 0.5 + r.unit() * 1.0;
            waterSources_.push_back(ws);
            riverCount++;
          }
        }
      }
    }
  }

  for (int y = 0; y < h; y += 8) {
    for (int x = 0; x < w; x += 8) {
      if (grid_.at(x, y) == eidolon::Terrain::Water && r.unit() < 0.3) {
        const eidolon::Vec2i shore = adjacentWalkable({x, y});
        if (shore.x >= 0) {
          eidolon::WaterSource ws;
          ws.pos = shore;
          ws.type = eidolon::WaterType::Lake;
          ws.capacity = 200.0 + r.unit() * 300.0;
          ws.current = ws.capacity;
          ws.flowRate = 0.1 + r.unit() * 0.2;
          waterSources_.push_back(ws);
        }
      }
    }
  }

  int springCount = w * h / 2000;
  for (int i = 0; i < springCount; ++i) {
    eidolon::Vec2i p = grid_.randomWalkable(r);
    if (grid_.elevation(p.x, p.y) > 0.1 && r.unit() < 0.5) {
      eidolon::WaterSource ws;
      ws.pos = p;
      ws.type = eidolon::WaterType::Spring;
      ws.capacity = 20.0 + r.unit() * 30.0;
      ws.current = ws.capacity;
      ws.flowRate = 0.3 + r.unit() * 0.5;
      waterSources_.push_back(ws);
    }
  }

  wildlife_.spawn(grid_, r, pos_);
}

eidolon::WorldUpdate eidolon::World::update(const eidolon::SimClock& c, int64_t dt, eidolon::Rng& r) {
  eidolon::WorldUpdate out;
  const bool wasRaining = weather_.raining();
  const bool wasSnowing = weather_.snowing();
  const bool wasStorming = weather_.storming();
  weather_.update(c, dt, r);

  for (eidolon::Plant& pl : plants_) {
    if (pl.amount < pl.maxAmount) {
      pl.amount = std::min(pl.maxAmount, pl.amount + pl.regrowthRate * dt / 5400.0);
    }
  }

  for (eidolon::WaterSource& ws : waterSources_) {
    if (ws.current < ws.capacity) {
      ws.current = std::min(ws.capacity, ws.current + ws.flowRate * dt);
    }
  }

  // Wildlife advances on its own throttle (kInterval sim-seconds).
  wildlife_.update(*this, c.now(), dt, alive_, pos_, out);

  // Phase 5 branch: cellular automata for infection/disease spread (DESIGN §22).
  // Update CA with infection rate, immunity, and terrain factors (swamp/deep-water = higher).
  static constexpr double kBaseInfectionRate = 0.15;
  static constexpr double kImmunityFactor = 0.5;
  std::vector<float> terrainFactor(grid_.width() * grid_.height(), 1.0f);
  for (int y = 0; y < grid_.height(); ++y) {
    for (int x = 0; x < grid_.width(); ++x) {
      size_t idx = static_cast<size_t>(y) * grid_.width() + x;
      if (grid_.deepWater(x, y) || grid_.at(x, y) == Terrain::Swamp) {
        terrainFactor[idx] = 2.0f;
      }
    }
  }
  infectionCA_.step(kBaseInfectionRate, kImmunityFactor, terrainFactor.data());

  out.weatherChanged = weather_.raining() != wasRaining || weather_.snowing() != wasSnowing ||
                       weather_.storming() != wasStorming;
  return out;
}

bool eidolon::World::adjacentToWater(eidolon::Vec2i pos) const {
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      eidolon::Terrain t = grid_.at(pos.x + dx, pos.y + dy);
      if (t == eidolon::Terrain::Water || t == eidolon::Terrain::River) return true;
    }
  }
  return false;
}

const eidolon::Plant* eidolon::World::nearestEdiblePlant(eidolon::Vec2i pos, int radius) const {
  const eidolon::Plant* best = nullptr;
  int bestDist = radius + 1;
  for (const eidolon::Plant& pl : plants_) {
    if (pl.amount < 1.0) continue;
    if (pl.type != eidolon::PlantType::Edible && pl.type != eidolon::PlantType::Medicinal) continue;
    int d = distCheb(pl.pos, pos);
    if (d <= radius && d < bestDist) {
      best = &pl;
      bestDist = d;
    }
  }
  return best;
}

const eidolon::WaterSource* eidolon::World::nearestWaterSource(eidolon::Vec2i pos, int radius) const {
  const eidolon::WaterSource* best = nullptr;
  int bestDist = radius + 1;
  for (const eidolon::WaterSource& ws : waterSources_) {
    if (ws.current < 1.0) continue;
    int d = distCheb(ws.pos, pos);
    if (d <= radius && d < bestDist) {
      best = &ws;
      bestDist = d;
    }
  }
  return best;
}

double eidolon::World::consumePlant(eidolon::Vec2i pos, double amount) {
  for (eidolon::Plant& pl : plants_) {
    if (pl.pos == pos && pl.amount >= 1.0) {
      double eaten = std::min(amount, pl.amount);
      pl.amount -= eaten;
      return eaten;
    }
  }
  return 0.0;
}

double eidolon::World::drinkFromSource(eidolon::Vec2i pos, double amount) {
  // The organism may stand on any shore of a lake/river while the registered source sits
  // on one particular shore tile, so search a generous radius (a water body is one
  // source). Sources are consumed by the organism, wildlife, and natural flow.
  eidolon::WaterSource* best = nullptr;
  int bestDist = 16;
  for (eidolon::WaterSource& ws : waterSources_) {
    if (ws.current < 1.0) continue;
    const int d = distCheb(ws.pos, pos);
    if (d < bestDist) {
      best = &ws;
      bestDist = d;
    }
  }
  if (!best) return 0.0;
  const double drank = std::min(amount, best->current);
  best->current -= drank;
  return drank;
}

eidolon::Vec2i eidolon::World::adjacentWalkable(eidolon::Vec2i water) const {
  const int dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
  const int dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  for (int i = 0; i < 8; ++i) {
    const int x = water.x + dxs[i];
    const int y = water.y + dys[i];
    if (grid_.inBounds(x, y) && grid_.walkable(x, y)) return {x, y};
  }
  return {-1, -1};
}

const eidolon::WildlifeAgent* eidolon::World::nearestPrey(eidolon::Vec2i pos, int radius) const {
  return wildlife_.nearestPrey(pos, radius);
}

const eidolon::WildlifeAgent* eidolon::World::nearestPredator(eidolon::Vec2i pos, int radius) const {
  return wildlife_.nearestPredator(pos, radius);
}

int eidolon::World::preyCount(eidolon::Vec2i pos, int radius) const {
  return wildlife_.preyCount(pos, radius);
}

int eidolon::World::predatorCount(eidolon::Vec2i pos, int radius) const {
  return wildlife_.predatorCount(pos, radius);
}

eidolon::Perception eidolon::World::perceive(eidolon::Vec2i pos, const eidolon::SimClock& c) const {
  eidolon::Perception p;
  const eidolon::Weather& w = weather_;
  const eidolon::Grid& g = grid_;

  p[0] = c.hourOfDay() / 24.0;
  p[1] = w.storming() ? 3.0 : (w.snowing() ? 2.0 : (w.raining() ? 1.0 : 0.0));
  p[2] = (w.ambientTempC(c) + 5.0) / 30.0;
  p[3] = static_cast<double>(w.season()) / 4.0;
  p[4] = static_cast<double>(static_cast<int>(g.at(pos.x, pos.y))) / 8.0;

  const int sight = eidolon::Perception::kSightRadius;
  const eidolon::Plant* plant = nearestEdiblePlant(pos, sight);
  if (plant) {
    int d = distCheb(plant->pos, pos);
    p[5] = static_cast<double>(d) / static_cast<double>(sight);
    p[6] = static_cast<double>(plant->pos.x > pos.x ? 1 : (plant->pos.x < pos.x ? -1 : 0));
    p[7] = static_cast<double>(plant->pos.y > pos.y ? 1 : (plant->pos.y < pos.y ? -1 : 0));
    p[8] = plant->amount / plant->maxAmount;
  } else {
    p[5] = 1.0; p[6] = 0.0; p[7] = 0.0; p[8] = 0.0;
  }

  const eidolon::WaterSource* water = nearestWaterSource(pos, sight);
  if (water) {
    int d = distCheb(water->pos, pos);
    p[9] = static_cast<double>(d) / static_cast<double>(sight);
    p[10] = static_cast<double>(water->pos.x > pos.x ? 1 : (water->pos.x < pos.x ? -1 : 0));
    p[11] = static_cast<double>(water->pos.y > pos.y ? 1 : (water->pos.y < pos.y ? -1 : 0));
  } else {
    p[9] = 1.0; p[10] = 0.0; p[11] = 0.0;
  }

  int plantCount = 0;
  const eidolon::Plant* toxicPlant = nullptr;
  int bestToxicDist = sight + 1;
  const eidolon::Plant* medPlant = nullptr;
  int bestMedDist = sight + 1;
  for (const eidolon::Plant& pl : plants_) {
    if (pl.amount <= 0.0) continue;
    int d = distCheb(pl.pos, pos);
    if (d <= sight) {
      plantCount++;
      if (pl.type == eidolon::PlantType::Toxic && d < bestToxicDist) {
        bestToxicDist = d; toxicPlant = &pl;
      }
      if (pl.type == eidolon::PlantType::Medicinal && d < bestMedDist) {
        bestMedDist = d; medPlant = &pl;
      }
    }
  }
  p[12] = static_cast<double>(std::min(plantCount, 8)) / 8.0;

  if (toxicPlant) {
    int d = distCheb(toxicPlant->pos, pos);
    p[13] = static_cast<double>(d) / static_cast<double>(sight);
    p[14] = static_cast<double>(toxicPlant->pos.x > pos.x ? 1 : (toxicPlant->pos.x < pos.x ? -1 : 0));
    p[15] = static_cast<double>(toxicPlant->pos.y > pos.y ? 1 : (toxicPlant->pos.y < pos.y ? -1 : 0));
  } else {
    p[13] = 1.0; p[14] = 0.0; p[15] = 0.0;
  }

  if (medPlant) {
    int d = distCheb(medPlant->pos, pos);
    p[16] = static_cast<double>(d) / static_cast<double>(sight);
  } else {
    p[16] = 1.0;
  }

  p[17] = (grid_.elevation(pos.x, pos.y) + 1.0f) * 0.5f;
  p[18] = 0.5f;
  p[19] = (grid_.humidity(pos.x, pos.y) + 1.0f) * 0.5f;

  // Wildlife channels 20..27 (Phase 5): nearest prey/predator distance+dx+dy, counts.
  const eidolon::WildlifeAgent* prey = nearestPrey(pos, sight);
  if (prey) {
    const int d = distCheb(prey->pos, pos);
    p[20] = static_cast<double>(d) / static_cast<double>(sight);
    p[21] = static_cast<double>(prey->pos.x > pos.x ? 1 : (prey->pos.x < pos.x ? -1 : 0));
    p[22] = static_cast<double>(prey->pos.y > pos.y ? 1 : (prey->pos.y < pos.y ? -1 : 0));
  } else {
    p[20] = 1.0; p[21] = 0.0; p[22] = 0.0;
  }

  const eidolon::WildlifeAgent* predator = nearestPredator(pos, sight);
  if (predator) {
    const int d = distCheb(predator->pos, pos);
    p[23] = static_cast<double>(d) / static_cast<double>(sight);
    p[24] = static_cast<double>(predator->pos.x > pos.x ? 1 : (predator->pos.x < pos.x ? -1 : 0));
    p[25] = static_cast<double>(predator->pos.y > pos.y ? 1 : (predator->pos.y < pos.y ? -1 : 0));
  } else {
    p[23] = 1.0; p[24] = 0.0; p[25] = 0.0;
  }

  p[26] = static_cast<double>(std::min(wildlife_.preyCount(pos, sight), 8)) / 8.0;
  p[27] = static_cast<double>(std::min(wildlife_.predatorCount(pos, sight), 4)) / 4.0;

  return p;
}

void eidolon::World::serialize(eidolon::BinaryWriter& w) const {
  grid_.serialize(w);
  weather_.serialize(w);
  w.i64(pos_.x);
  w.i64(pos_.y);
  w.u8(alive_ ? 1 : 0);
  w.u64(static_cast<uint64_t>(plants_.size()));
  for (const eidolon::Plant& pl : plants_) pl.serialize(w);
  w.u64(static_cast<uint64_t>(waterSources_.size()));
  for (const eidolon::WaterSource& ws : waterSources_) ws.serialize(w);
  wildlife_.serialize(w);
  infectionCA_.serialize(w);
}

bool eidolon::World::deserialize(eidolon::BinaryReader& r) {
  if (!grid_.deserialize(r) || !weather_.deserialize(r)) return false;
  int64_t x, y;
  uint8_t alive;
  if (!r.i64(x) || !r.i64(y) || !r.u8(alive)) return false;
  pos_ = {static_cast<int>(x), static_cast<int>(y)};
  alive_ = alive != 0;
  uint64_t n;
  if (!r.u64(n) || n > (1u << 24)) return false;
  plants_.resize(static_cast<size_t>(n));
  for (eidolon::Plant& pl : plants_) {
    if (!pl.deserialize(r)) return false;
  }
  if (!r.u64(n) || n > (1u << 24)) return false;
  waterSources_.resize(static_cast<size_t>(n));
  for (eidolon::WaterSource& ws : waterSources_) {
    if (!ws.deserialize(r)) return false;
  }
  if (!wildlife_.deserialize(r)) return false;
  if (!infectionCA_.deserialize(r)) return false;
  return true;
}
