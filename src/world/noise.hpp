// Noise fields (simplex noise) for deterministic procedural generation.
// Used by world generation for elevation, climate, biome boundaries, resource density.
#pragma once

#include <cstdint>
#include <array>
#include <cmath>

namespace eidolon {

// Simple 2D simplex noise implementation (deterministic, fast, no allocations).
// Based on Ken Perlin's simplex noise with integer lattice.
class SimplexNoise {
public:
  // seed: arbitrary 64-bit value
  explicit SimplexNoise(uint64_t seed = 0) { setSeed(seed); }

  void setSeed(uint64_t seed) {
    // Fisher-Yates shuffle of permutation table
    for (int i = 0; i < 256; ++i) perm_[i] = i;
    // Deterministic shuffle from seed
    uint64_t s = seed;
    for (int i = 255; i > 0; --i) {
      s = s * 6364136223846793005ULL + 1442695040888963407ULL;
      int j = static_cast<int>(s % (i + 1));
      std::swap(perm_[i], perm_[j]);
    }
    // Duplicate for wraparound
    for (int i = 0; i < 256; ++i) perm_[i + 256] = perm_[i];
    // Gradient vectors (12 directions for 2D)
    static constexpr int grads[12][2] = {
      {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
      {1, 0}, {-1, 0}, {1, 0}, {-1, 0},
      {0, 1}, {0, -1}, {0, 1}, {0, -1}
    };
    for (int i = 0; i < 12; ++i) {
      grad3_[i][0] = static_cast<float>(grads[i][0]);
      grad3_[i][1] = static_cast<float>(grads[i][1]);
    }
  }

  // 2D noise in range [-1, 1], deterministic for given seed
  float noise(float x, float y) const {
    static constexpr float F2 = 0.366025403f;  // (sqrt(3) - 1) / 2
    static constexpr float G2 = 0.211324865f;  // (3 - sqrt(3)) / 6

    // Skew input to simplex grid
    float s = (x + y) * F2;
    int i = fastFloor(x + s);
    int j = fastFloor(y + s);

    float t = (i + j) * G2;
    float X0 = i - t;
    float Y0 = j - t;
    float x0 = x - X0;
    float y0 = y - Y0;

    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; }
    else { i1 = 0; j1 = 1; }

    float x1 = x0 - i1 + G2;
    float y1 = y0 - j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float y2 = y0 - 1.0f + 2.0f * G2;

    int ii = i & 255;
    int jj = j & 255;

    int gi0 = perm_[ii + perm_[jj]] % 12;
    int gi1 = perm_[ii + i1 + perm_[jj + j1]] % 12;
    int gi2 = perm_[ii + 1 + perm_[jj + 1]] % 12;

    float n0, n1, n2;
    float t0 = 0.5f - x0 * x0 - y0 * y0;
    if (t0 < 0) n0 = 0.0f;
    else {
      t0 *= t0;
      n0 = t0 * t0 * dot(grad3_[gi0], x0, y0);
    }

    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 < 0) n1 = 0.0f;
    else {
      t1 *= t1;
      n1 = t1 * t1 * dot(grad3_[gi1], x1, y1);
    }

    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 < 0) n2 = 0.0f;
    else {
      t2 *= t2;
      n2 = t2 * t2 * dot(grad3_[gi2], x2, y2);
    }

    return 70.0f * (n0 + n1 + n2); // Scale to [-1, 1] approximately
  }

  // Octave noise (fractal Brownian motion)
  float fbm(float x, float y, int octaves = 4, float persistence = 0.5f, float lacunarity = 2.0f) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
      value += amplitude * noise(x * frequency, y * frequency);
      maxValue += amplitude;
      amplitude *= persistence;
      frequency *= lacunarity;
    }
    return value / maxValue; // Normalize to [-1, 1]
  }

  // Ridged multifractal (good for mountains/ridges)
  float ridged(float x, float y, int octaves = 4, float persistence = 0.5f, float lacunarity = 2.0f) const {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; ++i) {
      float n = noise(x * frequency, y * frequency);
      float r = 1.0f - std::abs(n);
      r *= r; // Sharp ridges
      value += amplitude * r;
      amplitude *= persistence;
      frequency *= lacunarity;
    }
    return value;
  }

private:
  static inline int fastFloor(float x) { return x > 0 ? static_cast<int>(x) : static_cast<int>(x) - 1; }
  static inline float dot(const float g[2], float x, float y) { return g[0] * x + g[1] * y; }

  int perm_[512];
  float grad3_[12][2];
};

// Voronoi diagram (nearest site) with Manhattan/Chebyshev distance.
// For biome/territory tessellation.
class Voronoi {
public:
  struct Site {
    int x, y;
    int id;
  };

  explicit Voronoi(int w, int h) : w_(w), h_(h) {}

  // Add a site
  void addSite(int x, int y, int id) {
    sites_.push_back({x, y, id});
  }

  // Compute nearest site for all cells (Chebyshev distance)
  void compute(int* outIds) const {
    for (int y = 0; y < h_; ++y) {
      for (int x = 0; x < w_; ++x) {
        int bestId = -1;
        int bestDist = INT32_MAX;
        for (const auto& s : sites_) {
          int dx = x - s.x;
          int dy = y - s.y;
          int dist = dx > dy ? dx : dy; // Chebyshev
          if (dist < bestDist) {
            bestDist = dist;
            bestId = s.id;
          }
        }
        outIds[y * w_ + x] = bestId;
      }
    }
  }

private:
  int w_, h_;
  std::vector<Site> sites_;
};

} // namespace eidolon
