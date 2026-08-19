#ifndef EIDOLON_MARKOV_HPP
#define EIDOLON_MARKOV_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "core/serialize.hpp"
#include "core/rng.hpp"

namespace eidolon {

// Deterministic Markov chain for state transitions
// Used for user model states and wildlife social states

template <size_t N>
class MarkovChain {
public:
  using State = uint8_t;
  
  MarkovChain() = default;
  explicit MarkovChain(const std::array<std::array<float, N>, N>& transitions);
  
  // Transition from current state using RNG
  State step(State current, Rng& rng) const;
  
  // Get transition probability from -> to
  float prob(State from, State to) const { return transitions_[from][to]; }
  
  // Set transition (will be normalized)
  void set(State from, State to, float p) {
    transitions_[from][to] = std::max(0.0f, p);
    normalize();
  }
  
  // Get steady-state distribution (power iteration)
  std::array<float, N> steady_state(int iterations = 1000) const;
  
  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  std::array<std::array<float, N>, N> transitions_{};
  
  void normalize();
};

// Predefined state enums for common models
enum class UserModelState : uint8_t {
  Stranger = 0,
  Familiar = 1,
  Trusted = 2,
  Feared = 3,
  Count = 4
};

enum class WildlifeSocialState : uint8_t {
  Neutral = 0,
  Friend = 1,
  Threat = 2,
  Count = 3
};

// Factory functions for standard chains
MarkovChain<static_cast<size_t>(UserModelState::Count)> make_user_model_markov();
MarkovChain<static_cast<size_t>(WildlifeSocialState::Count)> make_wildlife_social_markov();

// Higher-order Markov chain (n-gram) for sequences
template <size_t N, size_t Order>
class HigherOrderMarkovChain {
public:
  using State = uint8_t;
  using History = std::array<State, Order>;
  
  HigherOrderMarkovChain() = default;
  
  // Add observed transition
  void observe(const History& history, State next);
  
  // Predict next state given history
  State predict(const History& history, Rng& rng) const;
  
  // Get transition probabilities for a history
  std::array<float, 256> get_distribution(const History& history) const;
  
private:
  struct HistoryHash {
    size_t operator()(const History& hist) const noexcept {
      size_t h = 0;
      for (size_t i = 0; i < Order; ++i) {
        h = h * 31 + hist[i];
      }
      return h;
    }
  };
  
  std::unordered_map<History, std::array<uint32_t, 256>, HistoryHash> counts_;
  std::array<float, 256> uniform_{}; // fallback uniform distribution
};

// Continuous-time Markov chain (for async events)
template <size_t N>
class ContinuousTimeMarkovChain {
public:
  using State = uint8_t;
  
  ContinuousTimeMarkovChain() = default;
  explicit ContinuousTimeMarkovChain(const std::array<std::array<float, N>, N>& rates);
  
  // Sample next state and time until transition
  std::pair<State, float> step(State current, Rng& rng) const;
  
  // Get probability of being in each state at time t
  std::array<float, N> distribution_at(float t, State initial) const;
  
  // Set rate matrix
  void set_rates(const std::array<std::array<float, N>, N>& rates);
  
private:
  std::array<std::array<float, N>, N> rates_;
};

} // namespace eidolon

#endif // EIDOLON_MARKOV_HPP