#ifndef EIDOLON_CELLULAR_AUTOMATA_HPP
#define EIDOLON_CELLULAR_AUTOMATA_HPP

#include <vector>
#include <cstdint>

#include "core/serialize.hpp"

namespace eidolon {

// Cellular automata for infection/disease dynamics (Phase 5 branch — deterministic
// generative systems, DESIGN §22). Operates over the world grid; infection spreads
// to adjacent tiles based on severity, immunity, and terrain (swamp/deep-water are
// natural disease vectors).
//
// Rules (Conway-style infection model):
// - Healthy tile with ≥1 infected neighbor and low immunity → becomes Infected.
// - Infected tile with strong immunity and no new exposure → recovers.
// - Deep-water / swamp tiles have a higher infection transmission rate.
// - The CA is fully deterministic (seedable through the engine's Rng stream).
class CellularAutomata {
public:
  enum class State : uint8_t { Healthy = 0, Infected = 1, Recovered = 2 };

  CellularAutomata() = default;
  CellularAutomata(int w, int h);

  int width() const { return w_; }
  int height() const { return h_; }

  void resize(int w, int h);
  void reset();

  State at(int x, int y) const;
  void setState(int x, int y, State s);

  // Advance the CA by one tick using infection/disease rules.
  // infectionRate: base probability of transmission (0..1).
  // immunityFactor: 0 (immune) reduces infection probability; 1 (no immunity) = full rate.
  // terrainFactor(x,y): 1.0 for normal, >1.0 for swamp/deep-water (disease vectors).
  void step(double infectionRate, double immunityFactor,
            const float* terrainFactor);

  // Count infected cells in a Chebyshev radius around a point.
  int infectedCountInRadius(int cx, int cy, int radius) const;

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  int w_ = 0;
  int h_ = 0;
  std::vector<State> grid_;
  std::vector<State> next_;
};

} // namespace eidolon

#endif // EIDOLON_CELLULAR_AUTOMATA_HPP
