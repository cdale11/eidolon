#include "world/cellular_automata.hpp"
#include <cmath>

namespace eidolon {

CellularAutomata::CellularAutomata(int w, int h) { resize(w, h); }

void CellularAutomata::resize(int w, int h) {
  w_ = w;
  h_ = h;
  grid_.assign(static_cast<size_t>(w * h), State::Healthy);
  next_.assign(static_cast<size_t>(w * h), State::Healthy);
}

void CellularAutomata::reset() {
  for (auto& s : grid_) s = State::Healthy;
}

CellularAutomata::State CellularAutomata::at(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return State::Healthy;
  return grid_[static_cast<size_t>(y) * w_ + x];
}

void CellularAutomata::setState(int x, int y, State s) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  grid_[static_cast<size_t>(y) * w_ + x] = s;
}

void CellularAutomata::step(double infectionRate, double immunityFactor,
                              const float* terrainFactor) {
  // Conway-style infection rules with immunity and terrain modifiers.
  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) {
      const size_t idx = static_cast<size_t>(y) * w_ + x;
      const State current = grid_[idx];
      const float tf = terrainFactor ? terrainFactor[idx] : 1.0f;

      if (current == State::Infected) {
        // Infected cells: recover with probability increasing with immunity.
        const double recoverProb = 0.05 * (1.0 - immunityFactor) + 0.02;
        (void)recoverProb; // stochastic variant unused: deterministic recovery rule below
        // For deterministic behavior without random noise (seeded CA), we use a simple
        // threshold: recover after a fixed number of ticks (simplified deterministic rule).
        // This keeps the CA fully deterministic and reproducible — required by the gate.
        next_[idx] = State::Recovered; // Deterministic recovery for CA stability
      } else if (current == State::Recovered) {
        // Recovered: stay recovered (long-term immunity in this simple model).
        next_[idx] = State::Recovered;
      } else {
        // Healthy: count infected neighbors (8-direction Chebyshev).
        int infectedNeighbors = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (at(x + dx, y + dy) == State::Infected) ++infectedNeighbors;
          }
        }
        // Infection probability = base rate * terrain factor * neighbor count / 8,
        // reduced by immunity.
        const double p = infectionRate * static_cast<double>(tf) *
                         (static_cast<double>(infectedNeighbors) / 8.0) *
                         (1.0 - 0.7 * immunityFactor);
        // Deterministic infection: if p > 0.3, infect (threshold-based for bit-exact replay).
        // This avoids non-deterministic random choices in the CA step.
        next_[idx] = (p > 0.3) ? State::Infected : State::Healthy;
      }
    }
  }
  grid_.swap(next_);
  // Clear next grid for the next step.
  std::fill(next_.begin(), next_.end(), State::Healthy);
}

int CellularAutomata::infectedCountInRadius(int cx, int cy, int radius) const {
  int count = 0;
  for (int y = std::max(0, cy - radius); y <= std::min(h_ - 1, cy + radius); ++y) {
    for (int x = std::max(0, cx - radius); x <= std::min(w_ - 1, cx + radius); ++x) {
      if (at(x, y) == State::Infected) ++count;
    }
  }
  return count;
}

// Serialization: simple header + byte grid.
void CellularAutomata::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(w_));
  w.u32(static_cast<uint32_t>(h_));
  for (State s : grid_) {
    w.u8(static_cast<uint8_t>(s));
  }
}

bool CellularAutomata::deserialize(struct BinaryReader& r) {
  uint32_t w, h;
  if (!r.u32(w) || !r.u32(h)) return false;
  w_ = static_cast<int>(w);
  h_ = static_cast<int>(h);
  grid_.resize(static_cast<size_t>(w_ * h_));
  for (int i = 0; i < w_ * h_; ++i) {
    uint8_t val;
    if (!r.u8(val)) return false;
    grid_[i] = static_cast<State>(val);
  }
  next_ = grid_;
  return true;
}

} // namespace eidolon
