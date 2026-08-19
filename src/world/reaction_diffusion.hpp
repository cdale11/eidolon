#ifndef EIDOLON_REACTION_DIFFUSION_HPP
#define EIDOLON_REACTION_DIFFUSION_HPP

#include <vector>
#include <cstdint>

#include "core/vec2.hpp"

namespace eidolon {

// Reaction-diffusion (Gray-Scott / Turing patterns) for terrain texture
// patterns: mineral veins, fertile-soil gradients, biological pattern
// formation. Stable explicit Euler with capped iterations (DESIGN §22,
// Phase 5 branch). Fully deterministic, seeded, bit-exact replay.

struct ReactionDiffusionParams {
  double Du = 0.16;      // diffusion rate U
  double Dv = 0.08;      // diffusion rate V
  double F = 0.035;      // feed rate
  double k = 0.065;      // kill rate
  double dt = 1.0;       // time step
  uint32_t maxIter = 1000;    // max iterations (capped for stability)
};

class ReactionDiffusion {
public:
  ReactionDiffusion() = default;
  ReactionDiffusion(int w, int h, const ReactionDiffusionParams& p = {});

  void resize(int w, int h);
  void reset(); // random initial perturbation

  // Step the simulation by one iteration (explicit Euler).
  void step();

  // Run to completion (maxIter steps or until convergence).
  void run();

  // Get U and V concentrations at (x, y).
  double u(int x, int y) const;
  double v(int x, int y) const;

  // Access grids directly for serialization / seeding.
  const std::vector<double>& getU() const { return u_; }
  const std::vector<double>& getV() const { return v_; }
  std::vector<double>& getU() { return u_; }
  std::vector<double>& getV() { return v_; }

  void setParams(const ReactionDiffusionParams& p) { params_ = p; }

  // Serialize/deserialize for snapshot.
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  int w_ = 0;
  int h_ = 0;
  std::vector<double> u_, v_, uNext_, vNext_;
  ReactionDiffusionParams params_;
};

// Generate mineral vein patterns: high U = mineral concentration.
// Uses seeded RD with parameters tuned for vein-like structures.
std::vector<uint8_t> generateMineralVeins(int W, int H, uint64_t seed);

// Generate fertile soil gradients: high U = fertility.
std::vector<uint8_t> generateFertileSoil(int W, int H, uint64_t seed);

// Generate biological coat patterns (for wildlife).
std::vector<uint8_t> generateCoatPattern(int W, int H, uint64_t seed);

} // namespace eidolon

#endif // EIDOLON_REACTION_DIFFUSION_HPP