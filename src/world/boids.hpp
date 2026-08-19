#ifndef EIDOLON_BOIDS_HPP
#define EIDOLON_BOIDS_HPP

#include <vector>
#include <cstdint>

#include "core/vec2.hpp"

namespace eidolon {

// Flocking / Boids for collective wildlife behavior: bird flocks, prey herds,
// wolf packs (separation / alignment / cohesion + obstacle avoidance).
// O(neighbours) per-agent update, no full-grid scan. Deterministic, seeded.
// Phase 5 branch (DESIGN §22).

struct BoidParams {
  float separationRadius = 5.0f;
  float alignmentRadius = 8.0f;
  float cohesionRadius = 12.0f;
  float separationWeight = 1.5f;
  float alignmentWeight = 1.0f;
  float cohesionWeight = 1.0f;
  float maxSpeed = 3.0f;
  float maxForce = 0.5f;
  float obstacleAvoidanceWeight = 2.0f;
  float obstacleAvoidanceRadius = 3.0f;
};

struct Boid {
  Vec2f pos;
  Vec2f vel;
  Vec2f acc;
  int species = 0; // 0=bird, 1=prey herd, 2=wolf pack
  int flockId = 0; // flock identifier
};

class Boids {
public:
  Boids() = default;
  Boids(const BoidParams& p) : params_(p) {}

  void addBoid(const Boid& b);
  void removeBoid(int index);
  size_t size() const { return boids_.size(); }
  const Boid& operator[](size_t i) const { return boids_[i]; }
  Boid& operator[](size_t i) { return boids_[i]; }

  // Update all boids for one simulation step.
  // grid: for obstacle avoidance (terrain/cliffs/water).
  // dt: time step in simulation seconds.
  template <typename Grid>
  void update(const Grid& grid, float dt);

  // Get neighbors within radius (spatial hash for O(neighbours)).
  void buildSpatialHash(float cellSize);

  void setParams(const BoidParams& p) { params_ = p; }
  const BoidParams& params() const { return params_; }

  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  BoidParams params_;
  std::vector<Boid> boids_;
  std::vector<std::vector<int>> spatialHash_;
  float spatialCellSize_ = 10.0f;
  int spatialW_ = 0, spatialH_ = 0;
};

} // namespace eidolon

#endif // EIDOLON_BOIDS_HPP