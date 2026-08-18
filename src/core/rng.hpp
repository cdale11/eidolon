// xoshiro256++ PRNG with per-subsystem splitmix-derived streams.
// Seedable and persistable so deterministic replay reproduces runs bit-for-bit.
#pragma once

#include <array>
#include <cstdint>

namespace eidolon {

// xoshiro256++ (Vigna). Full state persists in snapshots.
class Rng {
public:
  Rng() = default;
  explicit Rng(uint64_t seed) noexcept;
  static Rng fromState(const std::array<uint64_t, 4>& s) noexcept;

  std::array<uint64_t, 4> state() const noexcept { return s_; }

  uint64_t next() noexcept;
  uint64_t range(uint64_t n) noexcept; // uniform in [0, n)
  double unit() noexcept;              // uniform in [0, 1)
  double range(double a, double b) noexcept; // uniform in [a, b)
  int irange(int a, int b) noexcept;         // inclusive ints in [a, b]
  bool chance(double p) noexcept;            // true with probability p

private:
  std::array<uint64_t, 4> s_ = {0, 0, 0, 0};
  static uint64_t rotl(uint64_t x, int k) noexcept;
};

// splitmix64 next-value; feed state var by reference.
uint64_t splitmix64(uint64_t& x) noexcept;

// Fixed subsystem identities; each gets an independent stream so randomness in one
// subsystem never perturbs another (keeps replay stable and isolated).
enum class Subsystem : uint32_t {
  World = 0,
  Weather,
  Body,
  Cognition,
  Learning,
  Memory,
  Language,
  Events,
  Count,
};

// Deterministic stream for a subsystem, derived from the master seed.
Rng subsystemStream(uint64_t masterSeed, Subsystem s) noexcept;

} // namespace eidolon
