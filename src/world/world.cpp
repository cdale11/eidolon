#include "world/world.hpp"

#include <cmath>
#include <cstdint>

namespace eidolon {

void Grid::generate(int w, int h, Rng& r) {
  w_ = w;
  h_ = h;
  tiles_.assign(static_cast<size_t>(w) * h, Terrain::Plains);

  // Fill with weighted random terrain, then smooth so biomes form blobs.
  for (auto& t : tiles_) {
    const double roll = r.unit();
    if (roll < 0.40) t = Terrain::Plains;
    else if (roll < 0.65) t = Terrain::Forest;
    else if (roll < 0.80) t = Terrain::Hills;
    else if (roll < 0.92) t = Terrain::Water;
    else t = Terrain::Desert;
  }

  for (int pass = 0; pass < 3; ++pass) {
    std::vector<Terrain> next = tiles_;
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        int counts[5] = {0, 0, 0, 0, 0};
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const Terrain t = at(x + dx, y + dy);
            counts[static_cast<int>(t)]++;
          }
        }
        int best = static_cast<int>(at(x, y));
        for (int k = 0; k < 5; ++k) {
          if (counts[k] > counts[best]) best = k;
        }
        next[static_cast<size_t>(y) * w_ + x] = static_cast<Terrain>(best);
      }
    }
    tiles_ = std::move(next);
  }

  // Guarantee at least one patch of water exists somewhere reachable (world richness).
  if (w_ >= 8 && h_ >= 8) {
    const int cx = static_cast<int>(r.range(static_cast<uint64_t>(w_ - 6))) + 3;
    const int cy = static_cast<int>(r.range(static_cast<uint64_t>(h_ - 6))) + 3;
    const int radius = 1 + static_cast<int>(r.range(2));
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy <= radius * radius) {
          const int tx = cx + dx, ty = cy + dy;
          if (inBounds(tx, ty)) tiles_[static_cast<size_t>(ty) * w_ + tx] = Terrain::Water;
        }
      }
    }
  }
}

Vec2i Grid::randomWalkable(Rng& r) const {
  for (int attempt = 0; attempt < 64; ++attempt) {
    const int x = static_cast<int>(r.range(static_cast<uint64_t>(w_)));
    const int y = static_cast<int>(r.range(static_cast<uint64_t>(h_)));
    if (walkable(x, y)) return {x, y};
  }
  return {w_ / 2, h_ / 2};
}

void Grid::serialize(BinaryWriter& w) const {
  w.i64(w_);
  w.i64(h_);
  w.u64(static_cast<uint64_t>(tiles_.size()));
  w.bytes(tiles_.data(), tiles_.size());
}

bool Grid::deserialize(BinaryReader& r) {
  int64_t w, h;
  uint64_t n;
  if (!r.i64(w) || !r.i64(h) || !r.u64(n)) return false;
  if (w <= 0 || h <= 0 || n != static_cast<uint64_t>(w * h) || n > (1u << 28)) return false;
  w_ = static_cast<int>(w);
  h_ = static_cast<int>(h);
  tiles_.resize(static_cast<size_t>(n));
  return r.bytes(tiles_.data(), tiles_.size());
}

uint64_t Grid::hash() const noexcept {
  uint64_t h = 1469598103934665603ULL;
  const auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 0x100000001b3ULL;
  };
  mix(static_cast<uint64_t>(w_));
  mix(static_cast<uint64_t>(h_));
  for (const Terrain t : tiles_) mix(static_cast<uint64_t>(t));
  return h;
}

// ---------------------------------------------------------------------------

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double Weather::ambientTempC(const SimClock& c) const noexcept {
  // Seasonal: 10..25 C with a winter trough of -5 C (day 0 = spring, mild 10 C).
  // Diurnal: ±4 C. Rain/storms cool, snow cools more.
  const double seasonal = 10.0 + 15.0 * std::cos(2.0 * kPi * (c.yearFraction() - 0.25));
  const double diurnal = 4.0 * std::sin(2.0 * kPi * (c.hourOfDay() - 9.0) / 24.0);
  double t = seasonal + diurnal;
  if (raining_ || storming_) t -= 3.0;
  if (snowing_) t -= 6.0;
  return t;
}

void Weather::update(const SimClock& c, Rng& r) {
  // Weather changes are gated by a cooldown so conditions persist for a while.
  const int64_t kCooldown = 1800; // sim-seconds (30 min)
  if (c.now() - lastChange_ < kCooldown) return;
  if (!r.chance(0.05)) return;
  lastChange_ = c.now();
  const double temp = ambientTempC(c);
  const double roll = r.unit();
  if (temp <= 1.0) {
    snowing_ = roll < 0.7;
    raining_ = !snowing_ && roll < 0.85;
  } else {
    raining_ = roll < 0.6;
    snowing_ = false;
  }
  storming_ = raining_ && r.chance(0.25);
}

void Weather::serialize(BinaryWriter& w) const {
  w.u8(raining_ ? 1 : 0);
  w.u8(snowing_ ? 1 : 0);
  w.u8(storming_ ? 1 : 0);
  w.i64(lastChange_);
}

bool Weather::deserialize(BinaryReader& r) {
  uint8_t a, b, c;
  int64_t lastChange;
  if (!r.u8(a) || !r.u8(b) || !r.u8(c) || !r.i64(lastChange)) return false;
  raining_ = a != 0;
  snowing_ = b != 0;
  storming_ = c != 0;
  lastChange_ = lastChange;
  return true;
}

// ---------------------------------------------------------------------------

void World::generate(int w, int h, Rng& r) {
  grid_.generate(w, h, r);
  pos_ = grid_.randomWalkable(r);
}

bool World::update(const SimClock& c, int64_t dt, Rng& r) {
  (void)dt;
  const bool wasRaining = weather_.raining();
  const bool wasSnowing = weather_.snowing();
  const bool wasStorming = weather_.storming();
  weather_.update(c, r);
  return weather_.raining() != wasRaining || weather_.snowing() != wasSnowing ||
         weather_.storming() != wasStorming;
}

void World::serialize(BinaryWriter& w) const {
  grid_.serialize(w);
  weather_.serialize(w);
  w.i64(pos_.x);
  w.i64(pos_.y);
  w.u8(alive_ ? 1 : 0);
}

bool World::deserialize(BinaryReader& r) {
  if (!grid_.deserialize(r) || !weather_.deserialize(r)) return false;
  int64_t x, y;
  uint8_t alive;
  if (!r.i64(x) || !r.i64(y) || !r.u8(alive)) return false;
  pos_ = {static_cast<int>(x), static_cast<int>(y)};
  alive_ = alive != 0;
  return true;
}

} // namespace eidolon
