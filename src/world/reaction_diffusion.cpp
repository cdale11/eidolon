#include "world/reaction_diffusion.hpp"
#include <algorithm>
#include <cmath>
#include <random>

#include "core/serialize.hpp"

namespace eidolon {

ReactionDiffusion::ReactionDiffusion(int w, int h, const ReactionDiffusionParams& p)
    : params_(p) {
  resize(w, h);
}

void ReactionDiffusion::resize(int w, int h) {
  w_ = w;
  h_ = h;
  size_t n = static_cast<size_t>(w * h);
  u_.assign(n, 1.0);
  v_.assign(n, 0.0);
  uNext_.assign(n, 1.0);
  vNext_.assign(n, 0.0);
}

void ReactionDiffusion::reset() {
  // Random perturbation in center region
  std::mt19937 rng(42); // deterministic seed for reset
  std::uniform_real_distribution<double> dist(-0.01, 0.01);
  for (int y = h_ / 4; y < 3 * h_ / 4; ++y) {
    for (int x = w_ / 4; x < 3 * w_ / 4; ++x) {
      size_t idx = static_cast<size_t>(y) * w_ + x;
      u_[idx] = 1.0 + dist(rng);
      v_[idx] = dist(rng);
    }
  }
}

void ReactionDiffusion::step() {
  const double Du = params_.Du;
  const double Dv = params_.Dv;
  const double F = params_.F;
  const double k = params_.k;
  const double dt = params_.dt;

  for (int y = 1; y < h_ - 1; ++y) {
    for (int x = 1; x < w_ - 1; ++x) {
      size_t idx = static_cast<size_t>(y) * w_ + x;
      double u = u_[idx];
      double v = v_[idx];

      // Laplacian (5-point stencil)
      double lu = u_[(y - 1) * w_ + x] + u_[(y + 1) * w_ + x] +
                  u_[y * w_ + (x - 1)] + u_[y * w_ + (x + 1)] - 4 * u;
      double lv = v_[(y - 1) * w_ + x] + v_[(y + 1) * w_ + x] +
                  v_[y * w_ + (x - 1)] + v_[y * w_ + (x + 1)] - 4 * v;

      double uv2 = u * v * v;
      double du = Du * lu - uv2 + F * (1 - u);
      double dv = Dv * lv + uv2 - (F + k) * v;

      uNext_[idx] = u + du * dt;
      vNext_[idx] = v + dv * dt;
    }
  }
  // Boundary: Neumann (no flux) - copy edge values
  for (int x = 0; x < w_; ++x) {
    uNext_[x] = uNext_[w_ + x];
    uNext_[(h_ - 1) * w_ + x] = uNext_[(h_ - 2) * w_ + x];
    vNext_[x] = vNext_[w_ + x];
    vNext_[(h_ - 1) * w_ + x] = vNext_[(h_ - 2) * w_ + x];
  }
  for (int y = 0; y < h_; ++y) {
    uNext_[y * w_] = uNext_[y * w_ + 1];
    uNext_[y * w_ + w_ - 1] = uNext_[y * w_ + w_ - 2];
    vNext_[y * w_] = vNext_[y * w_ + 1];
    vNext_[y * w_ + w_ - 1] = vNext_[y * w_ + w_ - 2];
  }
  u_.swap(uNext_);
  v_.swap(vNext_);
}

void ReactionDiffusion::run() {
  for (uint32_t i = 0; i < params_.maxIter; ++i) {
    step();
  }
}

double ReactionDiffusion::u(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0.0;
  return u_[static_cast<size_t>(y) * w_ + x];
}

double ReactionDiffusion::v(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0.0;
  return v_[static_cast<size_t>(y) * w_ + x];
}

void ReactionDiffusion::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(w_));
  w.u32(static_cast<uint32_t>(h_));
  w.u64(static_cast<uint64_t>(u_.size()));
  for (double val : u_) w.f64(val);
  for (double val : v_) w.f64(val);
  w.f64(params_.Du);
  w.f64(params_.Dv);
  w.f64(params_.F);
  w.f64(params_.k);
  w.f64(params_.dt);
  w.u32(static_cast<uint32_t>(params_.maxIter));
}

bool ReactionDiffusion::deserialize(struct BinaryReader& r) {
  uint32_t w, h;
  if (!r.u32(w) || !r.u32(h)) return false;
  resize(static_cast<int>(w), static_cast<int>(h));
  uint64_t n;
  if (!r.u64(n)) return false;
  u_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.f64(u_[i])) return false;
  }
  v_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.f64(v_[i])) return false;
  }
  if (!r.f64(params_.Du) || !r.f64(params_.Dv) ||
      !r.f64(params_.F) || !r.f64(params_.k) ||
      !r.f64(params_.dt) || !r.u32(params_.maxIter))
    return false;
  return true;
}

std::vector<uint8_t> generateMineralVeins(int W, int H, uint64_t seed) {
  ReactionDiffusionParams params;
  params.Du = 0.14;
  params.Dv = 0.06;
  params.F = 0.025;
  params.k = 0.055;
  params.dt = 1.0;
  params.maxIter = 2000;

  ReactionDiffusion rd(W, H, params);
  // Seed with deterministic perturbation
  std::mt19937 rng(static_cast<unsigned int>(seed));
  std::uniform_real_distribution<double> dist(-0.05, 0.05);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      size_t idx = static_cast<size_t>(y) * W + x;
      rd.getU()[idx] = 1.0 + dist(rng);
      rd.getV()[idx] = dist(rng);
    }
  }
  rd.run();

  std::vector<uint8_t> result(static_cast<size_t>(W * H));
  double minU = 1.0, maxU = 0.0;
  for (size_t i = 0; i < rd.getU().size(); ++i) {
    minU = std::min(minU, rd.getU()[i]);
    maxU = std::max(maxU, rd.getU()[i]);
  }
  double range = maxU - minU;
  if (range < 1e-6) range = 1.0;
  for (size_t i = 0; i < rd.getU().size(); ++i) {
    result[i] = static_cast<uint8_t>(255.0 * (rd.getU()[i] - minU) / range);
  }
  return result;
}

std::vector<uint8_t> generateFertileSoil(int W, int H, uint64_t seed) {
  ReactionDiffusionParams params;
  params.Du = 0.16;
  params.Dv = 0.08;
  params.F = 0.04;
  params.k = 0.06;
  params.dt = 1.0;
  params.maxIter = 1500;

  ReactionDiffusion rd(W, H, params);
  std::mt19937 rng(static_cast<unsigned int>(seed));
  std::uniform_real_distribution<double> dist(-0.02, 0.02);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      size_t idx = static_cast<size_t>(y) * W + x;
      rd.getU()[idx] = 1.0 + dist(rng);
      rd.getV()[idx] = dist(rng);
    }
  }
  rd.run();

  std::vector<uint8_t> result(static_cast<size_t>(W * H));
  double minU = 1.0, maxU = 0.0;
  for (size_t i = 0; i < rd.getU().size(); ++i) {
    minU = std::min(minU, rd.getU()[i]);
    maxU = std::max(maxU, rd.getU()[i]);
  }
  double range = maxU - minU;
  if (range < 1e-6) range = 1.0;
  for (size_t i = 0; i < rd.getU().size(); ++i) {
    result[i] = static_cast<uint8_t>(255.0 * (rd.getU()[i] - minU) / range);
  }
  return result;
}

std::vector<uint8_t> generateCoatPattern(int W, int H, uint64_t seed) {
  ReactionDiffusionParams params;
  params.Du = 0.1;
  params.Dv = 0.05;
  params.F = 0.03;
  params.k = 0.062;
  params.dt = 1.0;
  params.maxIter = 3000;

  ReactionDiffusion rd(W, H, params);
  std::mt19937 rng(static_cast<unsigned int>(seed));
  std::uniform_real_distribution<double> dist(-0.1, 0.1);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      size_t idx = static_cast<size_t>(y) * W + x;
      rd.getU()[idx] = 1.0 + dist(rng);
      rd.getV()[idx] = dist(rng);
    }
  }
  rd.run();

  std::vector<uint8_t> result(static_cast<size_t>(W * H));
  double minU = 1.0, maxU = 0.0;
  for (size_t i = 0; i < rd.getU().size(); ++i) {
    minU = std::min(minU, rd.getU()[i]);
    maxU = std::max(maxU, rd.getU()[i]);
  }
  double range = maxU - minU;
  if (range < 1e-6) range = 1.0;
  for (size_t i = 0; i < rd.getU().size(); ++i) {
    result[i] = static_cast<uint8_t>(255.0 * (rd.getU()[i] - minU) / range);
  }
  return result;
}

} // namespace eidolon