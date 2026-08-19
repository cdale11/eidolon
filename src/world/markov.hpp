#ifndef EIDOLON_MARKOV_HPP
#define EIDOLON_MARKOV_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <unordered_map>

#include "core/rng.hpp"
#include "core/serialize.hpp"

namespace eidolon {

// Deterministic Markov models for:
// - Weather transitions
// - Wildlife behavioral states (Forage/Flee/Rest/Hunt/Wander)
// - Organism sleep/wake/active state machine
// - Skill-stage progression
// Phase 5 branch (DESIGN §22): explicit chains, inspectable, testable, tunable.

template <size_t N>
class MarkovChain {
public:
  using State = uint8_t;

  MarkovChain() = default;
  explicit MarkovChain(const std::array<std::array<float, N>, N>& transitions)
      : transitions_(transitions) {
    normalize();
  }

  // Transition from current state using RNG.
  State step(State current, Rng& rng) const {
    const auto& row = transitions_[current];
    float r = static_cast<float>(rng.range(0.0, 1.0));
    float cum = 0.0f;
    for (size_t i = 0; i < N; ++i) {
      cum += row[i];
      if (r <= cum) return static_cast<State>(i);
    }
    return static_cast<State>(N - 1);
  }

  // Get transition probability from -> to.
  float prob(State from, State to) const {
    return transitions_[from][to];
  }

  // Set transition (will be normalized).
  void set(State from, State to, float p) {
    transitions_[from][to] = std::max(0.0f, p);
    normalize();
  }

  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const {
    w.u8(static_cast<uint8_t>(N));
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j) {
        w.f32(transitions_[i][j]);
      }
    }
  }

  bool deserialize(struct BinaryReader& r) {
    uint8_t n;
    if (!r.u8(n) || n != N) return false;
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j) {
        if (!r.f32(transitions_[i][j])) return false;
      }
    }
    return true;
  }

private:
  std::array<std::array<float, N>, N> transitions_{};

  void normalize() {
    for (size_t i = 0; i < N; ++i) {
      float sum = 0.0f;
      for (size_t j = 0; j < N; ++j) sum += transitions_[i][j];
      if (sum <= 0) {
        // Uniform fallback
        for (size_t j = 0; j < N; ++j) transitions_[i][j] = 1.0f / N;
      } else {
        for (size_t j = 0; j < N; ++j) transitions_[i][j] /= sum;
      }
    }
  }
};

// Predefined state enums for common Markov chains
enum class WeatherState : uint8_t {
  Clear = 0,
  Rain = 1,
  Storm = 2,
  Snow = 3,
  Count = 4
};

enum class WildlifeBehavior : uint8_t {
  Forage = 0,
  Flee = 1,
  Rest = 2,
  Hunt = 3,
  Wander = 4,
  Count = 5
};

enum class SleepState : uint8_t {
  Awake = 0,
  Drowsy = 1,
  Sleep = 2,
  Wake = 3,
  Count = 4
};

enum class SkillStage : uint8_t {
  Novice = 0,
  Apprentice = 1,
  Journeyman = 2,
  Expert = 3,
  Master = 4,
  Count = 5
};

// Factory functions for standard chains (tuned parameters)
MarkovChain<static_cast<size_t>(WeatherState::Count)> makeWeatherMarkov();
MarkovChain<static_cast<size_t>(WildlifeBehavior::Count)> makeWildlifeBehaviorMarkov();
MarkovChain<static_cast<size_t>(SleepState::Count)> makeSleepMarkov();
MarkovChain<static_cast<size_t>(SkillStage::Count)> makeSkillProgressionMarkov();

} // namespace eidolon

#endif // EIDOLON_MARKOV_HPP