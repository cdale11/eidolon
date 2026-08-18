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

  // Berry bushes: ~1 per 128 tiles, only on walkable non-desert land, never adjacent to
  // the spawn so the organism must explore a little before its first meal.
  bushes_.clear();
  const int target = static_cast<int>(static_cast<uint64_t>(w) * h / 128);
  int guard = target * 8 + 64;
  while (static_cast<int>(bushes_.size()) < target && guard-- > 0) {
    const Vec2i p = grid_.randomWalkable(r);
    if (grid_.at(p.x, p.y) == Terrain::Desert) continue;
    if (distCheb(p, pos_) <= 2) continue;
    bool clash = false;
    for (const Bush& b : bushes_) {
      if (distCheb(b.pos, p) <= 1) {
        clash = true;
        break;
      }
    }
    if (clash) continue;
    Bush b;
    b.pos = p;
    b.berries = 4.0 + r.unit() * 6.0; // 4..10 berries
    bushes_.push_back(b);
  }
}

bool World::update(const SimClock& c, int64_t dt, Rng& r) {
  (void)c;
  (void)r;
  const bool wasRaining = weather_.raining();
  const bool wasSnowing = weather_.snowing();
  const bool wasStorming = weather_.storming();
  weather_.update(c, r);

  // Berry regrowth: +1 berry per bush per 1.5 sim-hours, capped at 10.
  const double regrow = static_cast<double>(dt) / 5400.0;
  for (Bush& b : bushes_) {
    if (b.berries < 10.0) b.berries = std::min(10.0, b.berries + regrow);
  }

  return weather_.raining() != wasRaining || weather_.snowing() != wasSnowing ||
         weather_.storming() != wasStorming;
}

bool World::adjacentToWater(Vec2i pos) const {
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (grid_.at(pos.x + dx, pos.y + dy) == Terrain::Water) return true;
    }
  }
  return false;
}

const Bush* World::nearestBush(Vec2i pos, int radius) const {
  const Bush* best = nullptr;
  int bestDist = radius + 1;
  for (const Bush& b : bushes_) {
    if (b.berries < 1.0) continue; // less than one berry: not worth a visit
    const int d = distCheb(b.pos, pos);
    if (d <= radius && d < bestDist) {
      best = &b;
      bestDist = d;
    }
  }
  return best;
}

double World::consumeBerries(Vec2i pos, double amount) {
  for (Bush& b : bushes_) {
    if (b.pos == pos && b.berries >= 1.0) {
      const double eaten = std::min(amount, b.berries);
      b.berries -= eaten;
      return eaten;
    }
  }
  return 0.0;
}

Perception World::perceive(Vec2i pos, const SimClock& c) const {
  Perception p;
  p[0] = c.hourOfDay() / 24.0;
  const Weather& w = weather_;
  p[1] = w.storming() ? 2.0 : (w.snowing() ? 3.0 : (w.raining() ? 1.0 : 0.0));
  p[2] = (w.ambientTempC(c) + 5.0) / 30.0; // -5..25 C → 0..1
  p[3] = static_cast<double>(static_cast<int>(grid_.at(pos.x, pos.y))) / 4.0;

  const int sight = Perception::kSightRadius;
  const Bush* bush = nearestBush(pos, sight);
  if (bush) {
    const int d = distCheb(bush->pos, pos);
    p[4] = static_cast<double>(d) / static_cast<double>(sight);
    p[5] = static_cast<double>(bush->pos.x > pos.x ? 1 : (bush->pos.x < pos.x ? -1 : 0));
    p[6] = static_cast<double>(bush->pos.y > pos.y ? 1 : (bush->pos.y < pos.y ? -1 : 0));
    p[7] = bush->berries / 10.0;
  } else {
    p[4] = 1.0; // no food in sight
    p[5] = 0.0;
    p[6] = 0.0;
    p[7] = 0.0;
  }

  // Nearest water: scan a square ring outward up to sight radius.
  int waterDist = sight + 1;
  Vec2i waterDir{0, 0};
  for (int y = pos.y - sight; y <= pos.y + sight; ++y) {
    for (int x = pos.x - sight; x <= pos.x + sight; ++x) {
      if (grid_.at(x, y) != Terrain::Water) continue;
      const int d = distCheb({x, y}, pos);
      if (d < waterDist) {
        waterDist = d;
        waterDir = {x > pos.x ? 1 : (x < pos.x ? -1 : 0),
                    y > pos.y ? 1 : (y < pos.y ? -1 : 0)};
      }
    }
  }
  if (waterDist <= sight) {
    p[8] = static_cast<double>(waterDist) / static_cast<double>(sight);
    p[9] = static_cast<double>(waterDir.x);
    p[10] = static_cast<double>(waterDir.y);
  } else {
    p[8] = 1.0;
    p[9] = 0.0;
    p[10] = 0.0;
  }

  int count = 0;
  for (const Bush& b : bushes_) {
    if (b.berries > 0.0 && distCheb(b.pos, pos) <= sight) ++count;
  }
  p[11] = static_cast<double>(std::min(count, 4)) / 4.0;
  return p;
}

void Bush::serialize(BinaryWriter& w) const {
  w.i64(pos.x);
  w.i64(pos.y);
  w.f64(berries);
}

bool Bush::deserialize(BinaryReader& r) {
  int64_t x, y;
  if (!r.i64(x) || !r.i64(y) || !r.f64(berries)) return false;
  pos = {static_cast<int>(x), static_cast<int>(y)};
  return true;
}

void World::serialize(BinaryWriter& w) const {
  grid_.serialize(w);
  weather_.serialize(w);
  w.i64(pos_.x);
  w.i64(pos_.y);
  w.u8(alive_ ? 1 : 0);
  w.u64(static_cast<uint64_t>(bushes_.size()));
  for (const Bush& b : bushes_) b.serialize(w);
}

bool World::deserialize(BinaryReader& r) {
  if (!grid_.deserialize(r) || !weather_.deserialize(r)) return false;
  int64_t x, y;
  uint8_t alive;
  if (!r.i64(x) || !r.i64(y) || !r.u8(alive)) return false;
  pos_ = {static_cast<int>(x), static_cast<int>(y)};
  alive_ = alive != 0;
  uint64_t n;
  if (!r.u64(n) || n > (1u << 24)) return false;
  bushes_.resize(static_cast<size_t>(n));
  for (Bush& b : bushes_) {
    if (!b.deserialize(r)) return false;
  }
  return true;
}

} // namespace eidolon
