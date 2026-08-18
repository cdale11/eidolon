#include "core/rng.hpp"

namespace eidolon {

Rng::Rng(uint64_t seed) noexcept {
  uint64_t z = seed;
  s_ = {splitmix64(z), splitmix64(z), splitmix64(z), splitmix64(z)};
  if (s_[0] == 0 && s_[1] == 0 && s_[2] == 0 && s_[3] == 0) s_[0] = 1;
}

Rng Rng::fromState(const std::array<uint64_t, 4>& s) noexcept {
  Rng r;
  r.s_ = s;
  return r;
}

uint64_t Rng::rotl(uint64_t x, int k) noexcept {
  return (x << k) | (x >> (64 - k));
}

uint64_t Rng::next() noexcept {
  const uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
  const uint64_t t = s_[1] << 17;
  s_[2] ^= s_[0];
  s_[3] ^= s_[1];
  s_[1] ^= s_[2];
  s_[0] ^= s_[3];
  s_[2] ^= t;
  s_[3] = rotl(s_[3], 45);
  return result;
}

uint64_t Rng::range(uint64_t n) noexcept {
  return n == 0 ? 0 : next() % n;
}

double Rng::unit() noexcept {
  // 53-bit mantissa for clean doubles.
  return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
}

double Rng::range(double a, double b) noexcept {
  return a + (b - a) * unit();
}

int Rng::irange(int a, int b) noexcept {
  if (b <= a) return a;
  const uint64_t span = static_cast<uint64_t>(b - a + 1);
  return a + static_cast<int>(range(span));
}

bool Rng::chance(double p) noexcept {
  return unit() < p;
}

uint64_t splitmix64(uint64_t& x) noexcept {
  uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

Rng subsystemStream(uint64_t masterSeed, Subsystem s) noexcept {
  uint64_t mix = masterSeed ^ 0xa0761d6478bd642fULL;
  mix ^= static_cast<uint64_t>(static_cast<uint32_t>(s));
  mix ^= mix << 7;
  return Rng(splitmix64(mix) ^ (mix >> 11));
}

} // namespace eidolon
