#include "world/boids.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "world/world.hpp"

namespace eidolon {

void Boids::addBoid(const Boid& b) {
  boids_.push_back(b);
}

void Boids::removeBoid(int index) {
  if (index >= 0 && index < static_cast<int>(boids_.size())) {
    boids_.erase(boids_.begin() + index);
  }
}

void Boids::buildSpatialHash(float cellSize) {
  spatialCellSize_ = cellSize;
  // Determine grid bounds from boids
  float minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
  for (const auto& b : boids_) {
    minX = std::min(minX, b.pos.x);
    minY = std::min(minY, b.pos.y);
    maxX = std::max(maxX, b.pos.x);
    maxY = std::max(maxY, b.pos.y);
  }
  spatialW_ = static_cast<int>((maxX - minX) / cellSize) + 2;
  spatialH_ = static_cast<int>((maxY - minY) / cellSize) + 2;
  spatialHash_.assign(static_cast<size_t>(spatialW_ * spatialH_), {});

  for (size_t i = 0; i < boids_.size(); ++i) {
    int gx = static_cast<int>((boids_[i].pos.x - minX) / cellSize);
    int gy = static_cast<int>((boids_[i].pos.y - minY) / cellSize);
    gx = std::max(0, std::min(spatialW_ - 1, gx));
    gy = std::max(0, std::min(spatialH_ - 1, gy));
    spatialHash_[static_cast<size_t>(gy) * spatialW_ + gx].push_back(static_cast<int>(i));
  }
}

template <typename Grid>
void Boids::update(const Grid& grid, float dt) {
  if (boids_.empty()) return;

  // Build spatial hash for O(neighbours) queries
  buildSpatialHash(params_.alignmentRadius);

  for (size_t i = 0; i < boids_.size(); ++i) {
    Boid& b = boids_[i];
    Vec2f separation{0, 0};
    Vec2f alignment{0, 0};
    Vec2f cohesion{0, 0};
    int sepCount = 0, aliCount = 0, cohCount = 0;

    // Spatial hash query
    int gx = static_cast<int>((b.pos.x) / spatialCellSize_);
    int gy = static_cast<int>((b.pos.y) / spatialCellSize_);

    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        int nx = gx + dx, ny = gy + dy;
        if (nx < 0 || ny < 0 || nx >= spatialW_ || ny >= spatialH_) continue;
        const auto& cell = spatialHash_[static_cast<size_t>(ny) * spatialW_ + nx];
        for (int idx : cell) {
          if (static_cast<size_t>(idx) == i) continue;
          const Boid& other = boids_[idx];
          if (other.flockId != b.flockId) continue; // only same flock

          float dx = b.pos.x - other.pos.x;
          float dy = b.pos.y - other.pos.y;
          float dist2 = dx * dx + dy * dy;
          float dist = std::sqrt(dist2);

          // Separation: steer away from close neighbors
          if (dist > 0 && dist < params_.separationRadius) {
            float weight = (params_.separationRadius - dist) / params_.separationRadius;
            separation.x += (dx / dist) * weight;
            separation.y += (dy / dist) * weight;
            sepCount++;
          }

          // Alignment: match velocity with neighbors
          if (dist < params_.alignmentRadius) {
            alignment.x += other.vel.x;
            alignment.y += other.vel.y;
            aliCount++;
          }

          // Cohesion: steer toward average position
          if (dist < params_.cohesionRadius) {
            cohesion.x += other.pos.x;
            cohesion.y += other.pos.y;
            cohCount++;
          }
        }
      }
    }

    // Apply separation
    if (sepCount > 0) {
      separation.x /= sepCount;
      separation.y /= sepCount;
      float sepLen = std::sqrt(separation.x * separation.x + separation.y * separation.y);
      if (sepLen > 0) {
        separation.x = (separation.x / sepLen) * params_.maxSpeed - b.vel.x;
        separation.y = (separation.y / sepLen) * params_.maxSpeed - b.vel.y;
      }
    }

    // Apply alignment
    if (aliCount > 0) {
      alignment.x /= aliCount;
      alignment.y /= aliCount;
      float aliLen = std::sqrt(alignment.x * alignment.x + alignment.y * alignment.y);
      if (aliLen > 0) {
        alignment.x = (alignment.x / aliLen) * params_.maxSpeed - b.vel.x;
        alignment.y = (alignment.y / aliLen) * params_.maxSpeed - b.vel.y;
      }
    }

    // Apply cohesion
    if (cohCount > 0) {
      cohesion.x = cohesion.x / cohCount - b.pos.x;
      cohesion.y = cohesion.y / cohCount - b.pos.y;
      float cohLen = std::sqrt(cohesion.x * cohesion.x + cohesion.y * cohesion.y);
      if (cohLen > 0) {
        cohesion.x = (cohesion.x / cohLen) * params_.maxSpeed - b.vel.x;
        cohesion.y = (cohesion.y / cohLen) * params_.maxSpeed - b.vel.y;
      }
    }

    // Obstacle avoidance (terrain/cliffs/water)
    Vec2f obstacleAvoid{0, 0};
    int obstCount = 0;
    int checkRadius = static_cast<int>(params_.obstacleAvoidanceRadius);
    for (int dy = -checkRadius; dy <= checkRadius; ++dy) {
      for (int dx = -checkRadius; dx <= checkRadius; ++dx) {
        int tx = static_cast<int>(b.pos.x) + dx;
        int ty = static_cast<int>(b.pos.y) + dy;
        if (!grid.inBounds(tx, ty)) continue;
        if (!grid.walkable(tx, ty)) {
          float dx = b.pos.x - tx;
          float dy = b.pos.y - ty;
          float dist2 = dx * dx + dy * dy;
          float dist = std::sqrt(dist2);
          if (dist > 0 && dist < params_.obstacleAvoidanceRadius) {
            float weight = (params_.obstacleAvoidanceRadius - dist) / params_.obstacleAvoidanceRadius;
            obstacleAvoid.x += (dx / dist) * weight;
            obstacleAvoid.y += (dy / dist) * weight;
            obstCount++;
          }
        }
      }
    }
    if (obstCount > 0) {
      obstacleAvoid.x /= obstCount;
      obstacleAvoid.y /= obstCount;
      float oaLen = std::sqrt(obstacleAvoid.x * obstacleAvoid.x + obstacleAvoid.y * obstacleAvoid.y);
      if (oaLen > 0) {
        obstacleAvoid.x = (obstacleAvoid.x / oaLen) * params_.maxSpeed;
        obstacleAvoid.y = (obstacleAvoid.y / oaLen) * params_.maxSpeed;
      }
    }

    // Combine forces
    b.acc.x = separation.x * params_.separationWeight +
              alignment.x * params_.alignmentWeight +
              cohesion.x * params_.cohesionWeight +
              obstacleAvoid.x * params_.obstacleAvoidanceWeight;
    b.acc.y = separation.y * params_.separationWeight +
              alignment.y * params_.alignmentWeight +
              cohesion.y * params_.cohesionWeight +
              obstacleAvoid.y * params_.obstacleAvoidanceWeight;

    // Limit force
    float accLen = std::sqrt(b.acc.x * b.acc.x + b.acc.y * b.acc.y);
    if (accLen > params_.maxForce) {
      b.acc.x = (b.acc.x / accLen) * params_.maxForce;
      b.acc.y = (b.acc.y / accLen) * params_.maxForce;
    }

    // Update velocity
    b.vel.x += b.acc.x * dt;
    b.vel.y += b.acc.y * dt;

    // Limit speed
    float velLen = std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
    if (velLen > params_.maxSpeed) {
      b.vel.x = (b.vel.x / velLen) * params_.maxSpeed;
      b.vel.y = (b.vel.y / velLen) * params_.maxSpeed;
    }

    // Update position
    b.pos.x += b.vel.x * dt;
    b.pos.y += b.vel.y * dt;
  }
}

// Explicit instantiation for common grid types
template void Boids::update<class Grid>(const Grid& grid, float dt);

// Serialization
void Boids::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(boids_.size()));
  for (const auto& b : boids_) {
    w.f32(b.pos.x);
    w.f32(b.pos.y);
    w.f32(b.vel.x);
    w.f32(b.vel.y);
    w.f32(b.acc.x);
    w.f32(b.acc.y);
    w.u32(static_cast<uint32_t>(b.species));
    w.u32(static_cast<uint32_t>(b.flockId));
  }
  w.f32(params_.separationRadius);
  w.f32(params_.alignmentRadius);
  w.f32(params_.cohesionRadius);
  w.f32(params_.separationWeight);
  w.f32(params_.alignmentWeight);
  w.f32(params_.cohesionWeight);
  w.f32(params_.maxSpeed);
  w.f32(params_.maxForce);
  w.f32(params_.obstacleAvoidanceWeight);
  w.f32(params_.obstacleAvoidanceRadius);
}

bool Boids::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  boids_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.f32(boids_[i].pos.x) || !r.f32(boids_[i].pos.y) ||
        !r.f32(boids_[i].vel.x) || !r.f32(boids_[i].vel.y) ||
        !r.f32(boids_[i].acc.x) || !r.f32(boids_[i].acc.y) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&boids_[i].species)) || !r.u32(*reinterpret_cast<uint32_t*>(&boids_[i].flockId)))
      return false;
  }
  if (!r.f32(params_.separationRadius) || !r.f32(params_.alignmentRadius) ||
      !r.f32(params_.cohesionRadius) || !r.f32(params_.separationWeight) ||
      !r.f32(params_.alignmentWeight) || !r.f32(params_.cohesionWeight) ||
      !r.f32(params_.maxSpeed) || !r.f32(params_.maxForce) ||
      !r.f32(params_.obstacleAvoidanceWeight) || !r.f32(params_.obstacleAvoidanceRadius))
    return false;
  return true;
}

} // namespace eidolon