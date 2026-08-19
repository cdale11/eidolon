// Wildlife ABM: rabbits (prey) and wolves (predators) with Markov behavioural-state
// chains, Boids flocking, hunger/energy drives, and predator attacks on the organism.
#include "world/wildlife.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/clock.hpp"
#include "world/world.hpp"

namespace eidolon {

namespace {

// State order: Forage, Flee, Rest, Hunt, Wander.
constexpr double kRabbitMarkov[5][5] = {
    {0.50, 0.10, 0.15, 0.00, 0.25}, // Forage
    {0.30, 0.30, 0.10, 0.00, 0.30}, // Flee
    {0.40, 0.05, 0.30, 0.00, 0.25}, // Rest
    {0.30, 0.20, 0.10, 0.00, 0.40}, // Hunt (unused: rabbits never hunt)
    {0.35, 0.15, 0.10, 0.00, 0.40}, // Wander
};
constexpr double kWolfMarkov[5][5] = {
    {0.40, 0.05, 0.15, 0.30, 0.10}, // Forage
    {0.10, 0.30, 0.20, 0.20, 0.20}, // Flee
    {0.30, 0.05, 0.35, 0.20, 0.10}, // Rest
    {0.10, 0.05, 0.10, 0.60, 0.15}, // Hunt
    {0.25, 0.05, 0.10, 0.35, 0.25}, // Wander
};

// Per-species parameters (tiles / wildlife step at kInterval sim-seconds).
constexpr int kRabbitSpeed = 2;
constexpr int kWolfSpeed = 3;
constexpr int kRabbitSenseRadius = 12;
constexpr int kWolfSenseRadius = 14;
constexpr int kRabbitFleeRadius = 8;  // wolves trigger rabbit flee
constexpr int kRabbitFearRadius = 8;  // the organism triggers rabbit flee
constexpr int kWolfHuntRadius = 10;   // rabbits targeted for hunting
constexpr int kRabbitBoidsRadius = 6;
constexpr int kWolfBoidsRadius = 10;
constexpr double kRabbitBoidsSep = 1.0;
constexpr double kRabbitBoidsCoh = 0.4;
constexpr double kWolfBoidsSep = 0.8;
constexpr double kWolfBoidsCoh = 0.3;
// Drives (per wildlife step).
constexpr double kRabbitHungerRate = 0.3;
constexpr double kRabbitEnergyRate = 0.15;
constexpr double kWolfHungerRate = 0.6;
constexpr double kWolfEnergyRate = 0.25;
constexpr double kRabbitEatHunger = 70.0; // graze only when this hungry
constexpr double kRabbitEatAmount = 0.5;
constexpr double kHuntHunger = 30.0;      // wolf hunts rabbits above this hunger
constexpr double kAttackHunger = 55.0;    // wolf attacks the organism above this
constexpr int64_t kAttackCooldown = 60;   // sim-seconds between attacks
constexpr double kAttackDamageMin = 4.0;
constexpr double kAttackDamageRange = 5.0;

const double* markovRow(Species s, AnimalState st) {
  return (s == Species::Wolf ? kWolfMarkov : kRabbitMarkov)[static_cast<int>(st)];
}

AnimalState markovNext(const double* row, Rng& r) {
  const double roll = r.unit();
  double acc = 0.0;
  for (int i = 0; i < 5; ++i) {
    acc += row[i];
    if (roll < acc) return static_cast<AnimalState>(i);
  }
  return AnimalState::Wander;
}

Rng agentStream(uint64_t wildlifeSeed, uint32_t id) {
  uint64_t s = wildlifeSeed;
  s ^= static_cast<uint64_t>(id) * 0x9E3779B97F4A7C15ULL;
  s = splitmix64(s) ^ splitmix64(s);
  return Rng(s ^ 0xD1B54A32D192ED03ULL);
}

// Deterministic greedy single-tile step maximizing alignment with (dx, dy); random
// escape when no useful tile exists. Skips water/river and out-of-bounds tiles.
// A tile a wildlife agent can step onto: in bounds, walkable, and not a cliff.
bool stepWalkable(const Grid& g, int fx, int fy, int nx, int ny) {
  return g.inBounds(nx, ny) && g.walkable(nx, ny) && !g.cliffBetween(fx, fy, nx, ny);
}

bool stepToward(const Grid& g, Vec2i& pos, double dx, double dy, Rng& rng) {
  const int off[9][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0},
                         {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
  const double goalMag = std::sqrt(dx * dx + dy * dy);
  if (goalMag < 0.05) {
    if (rng.chance(0.2)) {
      for (int tries = 0; tries < 4; ++tries) {
        const int nx = pos.x + rng.irange(-1, 1);
        const int ny = pos.y + rng.irange(-1, 1);
        if (stepWalkable(g, pos.x, pos.y, nx, ny)) {
          pos = {nx, ny};
          return true;
        }
      }
    }
    return false;
  }
  int bestIdx = -1;
  double bestScore = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < 9; ++i) {
    const int nx = pos.x + off[i][0];
    const int ny = pos.y + off[i][1];
    if (!stepWalkable(g, pos.x, pos.y, nx, ny)) continue;
    const double s = dx * static_cast<double>(off[i][0]) +
                     dy * static_cast<double>(off[i][1]);
    if (s > bestScore) {
      bestScore = s;
      bestIdx = i;
    }
  }
  if (bestIdx <= 0) { // only the stay tile is usable
    for (int tries = 0; tries < 4; ++tries) {
      const int nx = pos.x + rng.irange(-1, 1);
      const int ny = pos.y + rng.irange(-1, 1);
      if (stepWalkable(g, pos.x, pos.y, nx, ny)) {
        pos = {nx, ny};
        return true;
      }
    }
    return false;
  }
  pos = {pos.x + off[bestIdx][0], pos.y + off[bestIdx][1]};
  return true;
}

} // namespace

void WildlifeAgent::serialize(BinaryWriter& w) const {
  w.u32(id);
  w.u8(static_cast<uint8_t>(species));
  w.i64(pos.x);
  w.i64(pos.y);
  w.f64(energy);
  w.f64(hunger);
  w.f64(fear);
  w.u8(static_cast<uint8_t>(state));
  w.i64(stateSince);
  w.i64(attackCooldownUntil);
  const std::array<uint64_t, 4> s = rng.state();
  w.u64(s[0]);
  w.u64(s[1]);
  w.u64(s[2]);
  w.u64(s[3]);
  w.u8(alive ? 1 : 0);
}

bool WildlifeAgent::deserialize(BinaryReader& r) {
  uint32_t idv;
  uint8_t spec, st, alv;
  int64_t x, y, since, cd;
  double e, hg, fr;
  std::array<uint64_t, 4> s;
  if (!r.u32(idv) || !r.u8(spec) || !r.i64(x) || !r.i64(y) || !r.f64(e) ||
      !r.f64(hg) || !r.f64(fr) || !r.u8(st) || !r.i64(since) || !r.i64(cd) ||
      !r.u64(s[0]) || !r.u64(s[1]) || !r.u64(s[2]) || !r.u64(s[3]) || !r.u8(alv))
    return false;
  id = idv;
  species = static_cast<Species>(spec);
  pos = {static_cast<int>(x), static_cast<int>(y)};
  energy = e;
  hunger = hg;
  fear = fr;
  state = static_cast<AnimalState>(st);
  stateSince = since;
  attackCooldownUntil = cd;
  rng = Rng::fromState(s);
  alive = alv != 0;
  return true;
}

void Wildlife::spawn(const Grid& g, Rng& r, Vec2i spawn) {
  agents_.clear();
  gridW_ = g.width();
  gridH_ = g.height();
  wildlifeSeed_ = r.next();

  const int area = gridW_ * gridH_;
  int nRabbit = std::max(2, area / 2500);
  int nWolf = std::max(1, area / 8000);

  uint32_t nextId = 0;
  auto place = [&](Species s, int count) {
    for (int i = 0; i < count; ++i) {
      Vec2i p = g.randomWalkable(r);
      if (distCheb(p, spawn) <= 6) {
        for (int attempt = 0; attempt < 32; ++attempt) {
          const Vec2i q = g.randomWalkable(r);
          if (distCheb(q, spawn) > 6) {
            p = q;
            break;
          }
        }
      }
      WildlifeAgent a;
      a.id = nextId++;
      a.species = s;
      a.pos = p;
      a.energy = 60.0 + a.rng.unit() * 40.0;
      a.hunger = a.rng.unit() * 40.0;
      a.state = (s == Species::Wolf) ? AnimalState::Forage : AnimalState::Wander;
      a.rng = agentStream(wildlifeSeed_, a.id);
      agents_.push_back(a);
    }
  };
  place(Species::Rabbit, nRabbit);
  place(Species::Wolf, nWolf);
}

void Wildlife::rebuildHash() {
  const int nCellsX = (gridW_ + kCellSize - 1) / kCellSize;
  const int nCellsY = (gridH_ + kCellSize - 1) / kCellSize;
  const size_t nCells = static_cast<size_t>(nCellsX) * nCellsY;
  cellHead_.assign(nCells, -1);
  cellNext_.assign(agents_.size(), -1);
  for (size_t i = 0; i < agents_.size(); ++i) {
    if (!agents_[i].alive) continue;
    const int cx = agents_[i].pos.x / kCellSize;
    const int cy = agents_[i].pos.y / kCellSize;
    const int cell = cy * nCellsX + cx;
    cellNext_[i] = cellHead_[static_cast<size_t>(cell)];
    cellHead_[static_cast<size_t>(cell)] = static_cast<int>(i);
  }
}

int Wildlife::neighbors(Vec2i pos, int radius, int* out, int outSize) const {
  const int nCellsX = (gridW_ + kCellSize - 1) / kCellSize;
  const int cx0 = std::max(0, (pos.x - radius) / kCellSize);
  const int cx1 = std::min(nCellsX - 1, (pos.x + radius) / kCellSize);
  const int nCellsY = (gridH_ + kCellSize - 1) / kCellSize;
  const int cy0 = std::max(0, (pos.y - radius) / kCellSize);
  const int cy1 = std::min(nCellsY - 1, (pos.y + radius) / kCellSize);
  int count = 0;
  for (int cy = cy0; cy <= cy1 && count < outSize; ++cy) {
    for (int cx = cx0; cx <= cx1 && count < outSize; ++cx) {
      const int cell = cy * nCellsX + cx;
      for (int idx = cellHead_[static_cast<size_t>(cell)]; idx >= 0 && count < outSize;
           idx = cellNext_[static_cast<size_t>(idx)]) {
        const WildlifeAgent& a = agents_[static_cast<size_t>(idx)];
        if (a.alive && distCheb(a.pos, pos) <= radius) {
          out[count++] = idx;
        }
      }
    }
  }
  return count;
}

const WildlifeAgent* Wildlife::nearestPrey(Vec2i pos, int radius) const {
  const WildlifeAgent* best = nullptr;
  int bestDist = radius + 1;
  for (const WildlifeAgent& a : agents_) {
    if (!a.alive || a.species != Species::Rabbit) continue;
    const int d = distCheb(a.pos, pos);
    if (d <= radius && d < bestDist) {
      best = &a;
      bestDist = d;
    }
  }
  return best;
}

const WildlifeAgent* Wildlife::nearestPredator(Vec2i pos, int radius) const {
  const WildlifeAgent* best = nullptr;
  int bestDist = radius + 1;
  for (const WildlifeAgent& a : agents_) {
    if (!a.alive || a.species != Species::Wolf) continue;
    const int d = distCheb(a.pos, pos);
    if (d <= radius && d < bestDist) {
      best = &a;
      bestDist = d;
    }
  }
  return best;
}

int Wildlife::preyCount(Vec2i pos, int radius) const {
  int n = 0;
  for (const WildlifeAgent& a : agents_) {
    if (a.alive && a.species == Species::Rabbit && distCheb(a.pos, pos) <= radius) ++n;
  }
  return n;
}

int Wildlife::predatorCount(Vec2i pos, int radius) const {
  int n = 0;
  for (const WildlifeAgent& a : agents_) {
    if (a.alive && a.species == Species::Wolf && distCheb(a.pos, pos) <= radius) ++n;
  }
  return n;
}

void Wildlife::update(World& w, int64_t now, int64_t dt, bool organismAlive,
                      Vec2i organismPos, WorldUpdate& out) {
  accum_ += dt;
  if (accum_ < kInterval) return;
  accum_ -= kInterval;
  step(w, now, organismAlive, organismPos, out);
}

namespace {
// A wolf bite. Satiates the wolf (hunger drops well below the attack threshold) so it
// disengages instead of camping a stationary target.
void attackOrganism(WildlifeAgent& a, int64_t now, WorldUpdate& out) {
  const double dmg = kAttackDamageMin + a.rng.unit() * kAttackDamageRange;
  a.attackCooldownUntil = now + kAttackCooldown;
  a.hunger = std::max(0.0, a.hunger - 45.0);
  out.attacked = true;
  out.attackDamage = std::max(out.attackDamage, dmg);
  out.attackerSpecies = static_cast<uint8_t>(Species::Wolf);
}
} // namespace

void Wildlife::step(World& w, int64_t now, bool organismAlive,
                    Vec2i organismPos, WorldUpdate& out) {
  const Grid& g = w.grid();
  rebuildHash();

  // Phase 1 (simultaneous): sense -> decide -> goal direction (+ Boids). Uses the
  // hash snapshot of positions so every agent reacts to the same world state.
  targets_.assign(agents_.size(), {0.0, 0.0});
  int nb[64];
  for (size_t ai = 0; ai < agents_.size(); ++ai) {
    WildlifeAgent& a = agents_[ai];
    if (!a.alive) continue;

    const bool isWolf = a.species == Species::Wolf;
    const int senseRadius = isWolf ? kWolfSenseRadius : kRabbitSenseRadius;
    const int boidsRadius = isWolf ? kWolfBoidsRadius : kRabbitBoidsRadius;
    const double boidsSep = isWolf ? kWolfBoidsSep : kRabbitBoidsSep;
    const double boidsCoh = isWolf ? kWolfBoidsCoh : kRabbitBoidsCoh;

    // Drives: hunger/energy decay each step.
    a.hunger = std::min(100.0, a.hunger + (isWolf ? kWolfHungerRate : kRabbitHungerRate));
    a.energy = std::max(0.0, a.energy - (isWolf ? kWolfEnergyRate : kRabbitEnergyRate));

    // Sense.
    int threatDist = std::numeric_limits<int>::max();
    const WildlifeAgent* threat = nullptr;
    if (!isWolf) {
      const WildlifeAgent* wolf = nearestPredator(a.pos, senseRadius);
      if (wolf) {
        threat = wolf;
        threatDist = distCheb(wolf->pos, a.pos);
      }
      const int od = organismAlive ? distCheb(organismPos, a.pos)
                                   : std::numeric_limits<int>::max();
      if (organismAlive && od < threatDist) threatDist = od;
      a.fear = threatDist <= kRabbitFearRadius
                   ? std::min(1.0, 1.0 - static_cast<double>(threatDist) /
                                                static_cast<double>(kRabbitFearRadius + 1))
                   : std::max(0.0, a.fear - 0.2);
    } else {
      const WildlifeAgent* prey = nearestPrey(a.pos, senseRadius);
      const int pd = prey ? distCheb(prey->pos, a.pos) : std::numeric_limits<int>::max();
      const int od = organismAlive ? distCheb(organismPos, a.pos)
                                   : std::numeric_limits<int>::max();
      threatDist = std::min(pd, od);
      a.fear = 0.0;
    }

    // Decide: Markov transition + reactive overrides.
    AnimalState next = markovNext(markovRow(a.species, a.state), a.rng);
    if (isWolf) {
      // Wolves prefer rabbits; they only stalk the organism when properly hungry.
      const bool preyNear = nearestPrey(a.pos, kWolfHuntRadius) != nullptr;
      if (a.hunger > kAttackHunger && threatDist <= kWolfHuntRadius) next = AnimalState::Hunt;
      else if (a.hunger > kHuntHunger && preyNear) next = AnimalState::Hunt;
    } else {
      if ((threat && threatDist <= kRabbitFleeRadius) ||
          (organismAlive && threatDist <= kRabbitFearRadius)) {
        next = AnimalState::Flee;
      } else if (next == AnimalState::Hunt) {
        next = AnimalState::Forage;
      }
    }
    a.state = next;
    a.stateSince = now;

    // Goal direction from state.
    double gx = 0.0, gy = 0.0, goalWeight = 1.0;
    switch (a.state) {
      case AnimalState::Forage: {
        const Plant* pl = w.nearestEdiblePlant(a.pos, senseRadius);
        if (pl) {
          const int d = std::max(1, distCheb(pl->pos, a.pos));
          gx = static_cast<double>(pl->pos.x - a.pos.x) / d;
          gy = static_cast<double>(pl->pos.y - a.pos.y) / d;
        } else {
          gx = a.rng.unit() * 2.0 - 1.0;
          gy = a.rng.unit() * 2.0 - 1.0;
          goalWeight = 0.4;
        }
        break;
      }
      case AnimalState::Flee: {
        int tx = organismPos.x, ty = organismPos.y;
        if (threat) { tx = threat->pos.x; ty = threat->pos.y; }
        const int d = std::max(1, distCheb({tx, ty}, a.pos));
        gx = static_cast<double>(a.pos.x - tx) / d;
        gy = static_cast<double>(a.pos.y - ty) / d;
        goalWeight = 1.4;
        break;
      }
      case AnimalState::Hunt: {
        int tx = -1, ty = -1;
        const WildlifeAgent* prey = nearestPrey(a.pos, senseRadius);
        if (prey) { tx = prey->pos.x; ty = prey->pos.y; }
        else if (organismAlive) { tx = organismPos.x; ty = organismPos.y; }
        if (tx >= 0) {
          const int d = std::max(1, distCheb({tx, ty}, a.pos));
          gx = static_cast<double>(tx - a.pos.x) / d;
          gy = static_cast<double>(ty - a.pos.y) / d;
        }
        goalWeight = 1.5;
        break;
      }
      case AnimalState::Rest:
        gx = gy = 0.0;
        goalWeight = 0.0;
        break;
      case AnimalState::Wander:
        gx = a.rng.unit() * 2.0 - 1.0;
        gy = a.rng.unit() * 2.0 - 1.0;
        goalWeight = 0.5;
        break;
    }

    // Boids (same-species flocking): separation + cohesion.
    double sepX = 0.0, sepY = 0.0, cohX = 0.0, cohY = 0.0;
    const int n = neighbors(a.pos, boidsRadius, nb, 64);
    int m = 0;
    for (int i = 0; i < n; ++i) {
      const WildlifeAgent& b = agents_[static_cast<size_t>(nb[i])];
      if (b.species != a.species || !b.alive || b.id == a.id) continue;
      const int d = std::max(1, distCheb(b.pos, a.pos));
      const double inv = 1.0 / static_cast<double>(d);
      sepX -= static_cast<double>(b.pos.x - a.pos.x) * inv;
      sepY -= static_cast<double>(b.pos.y - a.pos.y) * inv;
      cohX += static_cast<double>(b.pos.x);
      cohY += static_cast<double>(b.pos.y);
      ++m;
    }
    if (m > 0) {
      cohX = cohX / static_cast<double>(m) - static_cast<double>(a.pos.x);
      cohY = cohY / static_cast<double>(m) - static_cast<double>(a.pos.y);
    }

    targets_[ai].x = sepX * boidsSep + cohX * boidsCoh + gx * goalWeight;
    targets_[ai].y = sepY * boidsSep + cohY * boidsCoh + gy * goalWeight;
  }

  // Phase 2 (sequential): act - move, eat, kill, attack, starve.
  for (size_t ai = 0; ai < agents_.size(); ++ai) {
    WildlifeAgent& a = agents_[ai];
    if (!a.alive) continue;
    const bool isWolf = a.species == Species::Wolf;
    const int speed = isWolf ? kWolfSpeed : kRabbitSpeed;

    // A wolf attacks the moment it is within reach (including mid-charge), rather than
    // only after finishing its steps - otherwise a 3-step charge would overshoot past a
    // fleeing organism and rarely ever land a bite.
    for (int s = 0; s < speed; ++s) {
      if (isWolf && organismAlive && a.hunger > kAttackHunger &&
          distCheb(a.pos, organismPos) <= 1 && now >= a.attackCooldownUntil) {
        attackOrganism(a, now, out);
        break;
      }
      stepToward(g, a.pos, targets_[ai].x, targets_[ai].y, a.rng);
    }

    if (isWolf) {
      // Eat adjacent rabbits (preferred prey). Satiates the wolf substantially.
      for (WildlifeAgent& prey : agents_) {
        if (prey.alive && prey.species == Species::Rabbit && prey.id != a.id &&
            distCheb(prey.pos, a.pos) <= 1) {
          prey.alive = false;
          a.hunger = std::max(0.0, a.hunger - 35.0);
          a.energy = std::min(100.0, a.energy + 12.0);
          break;
        }
      }
    } else {
      // Rabbits graze adjacent edible plants, but only when hungry and only a little
      // (keeps plant pressure light so the organism can feed in the same world).
      if (a.hunger > kRabbitEatHunger) {
        const Plant* pl = w.nearestEdiblePlant(a.pos, 1);
        if (pl) {
          const double eaten = w.consumePlant(pl->pos, kRabbitEatAmount);
          if (eaten > 0.0) {
            a.hunger = std::max(0.0, a.hunger - 15.0);
            a.energy = std::min(100.0, a.energy + 3.0);
          }
        }
      }
    }

    // Starvation / exhaustion kills.
    if (a.energy <= 0.0 || a.hunger >= 100.0) a.alive = false;
  }
}

void Wildlife::serialize(BinaryWriter& w) const {
  w.u64(wildlifeSeed_);
  w.i64(accum_);
  w.i64(gridW_);
  w.i64(gridH_);
  w.u64(static_cast<uint64_t>(agents_.size()));
  for (const WildlifeAgent& a : agents_) a.serialize(w);
}

bool Wildlife::deserialize(BinaryReader& r) {
  uint64_t seed;
  int64_t accum, gw, gh;
  uint64_t n;
  if (!r.u64(seed) || !r.i64(accum) || !r.i64(gw) || !r.i64(gh) || !r.u64(n) ||
      n > (1u << 20))
    return false;
  wildlifeSeed_ = seed;
  accum_ = accum;
  gridW_ = static_cast<int>(gw);
  gridH_ = static_cast<int>(gh);
  agents_.resize(static_cast<size_t>(n));
  for (WildlifeAgent& a : agents_) {
    if (!a.deserialize(r)) return false;
  }
  return true;
}

} // namespace eidolon