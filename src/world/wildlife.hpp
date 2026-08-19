// Wildlife: autonomous prey (rabbits) and predators (wolves) - the only other
// autonomous population besides the organism (DESIGN §4). Formalised as the canonical
// ABM loop (sense -> decide -> act) with per-agent RNG streams derived from the wildlife
// seed and agent id (§22 ABM), explicit Markov behavioural-state chains (§22 Markov),
// and Boids flocking (separation/alignment/cohesion + obstacle avoidance, §22 Boids).
//
// The organism perceives wildlife through Perception channels and can be attacked by
// predators (pain/health damage -> threat learning). All dynamics are deterministic and
// serialized inside the World snapshot.
#ifndef EIDOLON_WILDLIFE_HPP
#define EIDOLON_WILDLIFE_HPP

#include <cstdint>
#include <vector>

#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "core/vec2.hpp"

namespace eidolon {

class World;
struct WorldUpdate;

// Species of a wildlife agent.
enum class Species : uint8_t { Rabbit = 0, Wolf = 1 };

// Markov behavioural states (§22 Markov). Rows of the per-species transition matrix are
// constants of the sim; each agent's RNG picks the next state from the current row.
enum class AnimalState : uint8_t {
  Forage = 0,
  Flee = 1,
  Rest = 2,
  Hunt = 3,
  Wander = 4,
};

// One autonomous wildlife agent. `rng` is a per-agent stream derived from
// (wildlifeSeed, id) so wildlife randomness never perturbs other subsystem streams.
struct WildlifeAgent {
  uint32_t id = 0;
  Species species = Species::Rabbit;
  Vec2i pos = {0, 0};
  double energy = 100.0; // 0..100
  double hunger = 0.0;   // 0..100
  double fear = 0.0;     // 0..1 fear of the organism
  AnimalState state = AnimalState::Wander;
  int64_t stateSince = 0;
  int64_t attackCooldownUntil = 0; // predators only: earliest time of next attack
  Rng rng; // per-agent stream (persisted for bit-exact restore)
  bool alive = true;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);
};
class Wildlife {
public:
  static constexpr int kCellSize = 8; // spatial-hash cell width (tiles)
  static constexpr int kInterval = 5; // sim-seconds between wildlife steps

  // Spawn a deterministic population (counts scale with grid area, keep agents a
  // minimum distance from the organism spawn). Draws the wildlife stream seed from r.
  void spawn(const class Grid& g, Rng& r, Vec2i spawn);

  // Advance the population on a throttle (every kInterval sim-seconds). Accumulates
  // `dt` internally; when the interval elapses runs one wildlife step (sense -> decide
  // -> act). Reports predator attacks on the organism via `out`.
  void update(World& w, int64_t now, int64_t dt, bool organismAlive,
              Vec2i organismPos, WorldUpdate& out);

  const std::vector<WildlifeAgent>& agents() const { return agents_; }
  std::vector<WildlifeAgent>& agents() { return agents_; }
  uint64_t wildlifeSeed() const { return wildlifeSeed_; }

  const WildlifeAgent* nearestPrey(Vec2i pos, int radius) const;
  const WildlifeAgent* nearestPredator(Vec2i pos, int radius) const;
  int preyCount(Vec2i pos, int radius) const;
  int predatorCount(Vec2i pos, int radius) const;

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  void rebuildHash();
  void step(World& w, int64_t now, bool organismAlive, Vec2i organismPos,
            WorldUpdate& out);
  // Collect indices of alive agents within `radius` (Chebyshev) of `pos` into `out`.
  // Returns the number collected (<= outSize). Deterministic order.
  int neighbors(Vec2i pos, int radius, int* out, int outSize) const;

  std::vector<WildlifeAgent> agents_;
  std::vector<int> cellHead_; // per-cell head index into cellNext_
  std::vector<int> cellNext_; // linked list: agent index -> next
  struct Vec2d {
    double x = 0.0;
    double y = 0.0;
  };
  std::vector<Vec2d> targets_; // per-agent goal direction (phase 1 -> phase 2)
  int gridW_ = 0;
  int gridH_ = 0;
  uint64_t wildlifeSeed_ = 0;
  int64_t accum_ = 0; // seconds accumulated toward the next wildlife step
};

} // namespace eidolon

#endif // EIDOLON_WILDLIFE_HPP