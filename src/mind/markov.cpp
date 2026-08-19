#include "mind/markov.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace eidolon {

template <size_t N>
MarkovChain<N>::MarkovChain(const std::array<std::array<float, N>, N>& transitions)
    : transitions_(transitions) {
  normalize();
}

template <size_t N>
void MarkovChain<N>::normalize() {
  for (size_t i = 0; i < N; ++i) {
    float sum = 0.0f;
    for (size_t j = 0; j < N; ++j) sum += transitions_[i][j];
    if (sum > 0.0f) {
      for (size_t j = 0; j < N; ++j) transitions_[i][j] /= sum;
    } else {
      for (size_t j = 0; j < N; ++j) transitions_[i][j] = 1.0f / N;
    }
  }
}

template <size_t N>
typename MarkovChain<N>::State MarkovChain<N>::step(State current, Rng& rng) const {
  const auto& row = transitions_[current];
  float r = static_cast<float>(rng.range(0.0, 1.0));
  float cum = 0.0f;
  for (size_t i = 0; i < N; ++i) {
    cum += row[i];
    if (r <= cum) return static_cast<State>(i);
  }
  return static_cast<State>(N - 1);
}

template <size_t N>
std::array<float, N> MarkovChain<N>::steady_state(int iterations) const {
  std::array<float, N> dist{};
  dist.fill(1.0f / N);
  
  for (int iter = 0; iter < iterations; ++iter) {
    std::array<float, N> next{};
    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j < N; ++j) {
        next[j] += dist[i] * transitions_[i][j];
      }
    }
    dist = next;
  }
  return dist;
}

template <size_t N>
void MarkovChain<N>::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(N));
  for (size_t i = 0; i < N; ++i) {
    for (size_t j = 0; j < N; ++j) {
      w.f32(transitions_[i][j]);
    }
  }
}

template <size_t N>
bool MarkovChain<N>::deserialize(struct BinaryReader& r) {
  uint8_t n;
  if (!r.u8(n) || n != N) return false;
  for (size_t i = 0; i < N; ++i) {
    for (size_t j = 0; j < N; ++j) {
      if (!r.f32(transitions_[i][j])) return false;
    }
  }
  return true;
}

MarkovChain<4> make_user_model_markov() {
  std::array<std::array<float, 4>, 4> t = {{
    {0.7f, 0.2f, 0.05f, 0.05f},
    {0.1f, 0.7f, 0.15f, 0.05f},
    {0.05f, 0.15f, 0.75f, 0.1f},
    {0.1f, 0.05f, 0.1f, 0.75f}
  }};
  return MarkovChain<4>(t);
}

MarkovChain<3> make_wildlife_social_markov() {
  std::array<std::array<float, 3>, 3> t = {{
    {0.7f, 0.2f, 0.1f},
    {0.1f, 0.8f, 0.1f},
    {0.2f, 0.1f, 0.7f}
  }};
  return MarkovChain<3>(t);
}

template class MarkovChain<4>;
template class MarkovChain<3>;

} // namespace eidolon