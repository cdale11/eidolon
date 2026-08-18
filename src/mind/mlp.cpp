#include "mind/mlp.hpp"

#include <cmath>
#include <cstring>

namespace eidolon {

Mlp::Mlp(int inN, int hiddenN, int outN, OutAct outAct)
    : inN_(inN),
      hiddenN_(hiddenN),
      outN_(outN),
      outAct_(outAct),
      w1_(static_cast<size_t>(inN) * hiddenN),
      b1_(static_cast<size_t>(hiddenN)),
      w2_(static_cast<size_t>(hiddenN) * outN),
      b2_(static_cast<size_t>(outN)) {}

void Mlp::reset(Rng& r, float scale) {
  for (float& v : w1_) v = static_cast<float>(r.range(-scale, scale));
  for (float& v : w2_) v = static_cast<float>(r.range(-scale, scale));
  for (float& v : b1_) v = static_cast<float>(r.range(-scale, scale));
  for (float& v : b2_) v = 0.0f;
}

void Mlp::forward(const float* in, float* hidden, float* out) const {
  for (int j = 0; j < hiddenN_; ++j) {
    float z = b1_[static_cast<size_t>(j)];
    const float* w = &w1_[static_cast<size_t>(j) * inN_];
    for (int i = 0; i < inN_; ++i) z += w[i] * in[i];
    if (hidden) hidden[j] = std::tanh(z);
  }
  for (int o = 0; o < outN_; ++o) {
    float z = b2_[static_cast<size_t>(o)];
    const float* w = &w2_[static_cast<size_t>(o) * hiddenN_];
    for (int j = 0; j < hiddenN_; ++j) z += w[j] * (hidden ? hidden[j] : 0.0f);
    if (outAct_ == OutAct::Sigmoid) {
      z = 1.0f / (1.0f + std::exp(-z));
    } else if (outAct_ == OutAct::Tanh) {
      z = std::tanh(z);
    }
    out[o] = z;
  }
}

void Mlp::backprop(const float* in, const float* hidden, float gPre, float lr) {
  for (int j = 0; j < hiddenN_; ++j) {
    const float h = hidden[j];
    const float dw2 = lr * gPre * h;
    const float gh = gPre * w2_[static_cast<size_t>(j)] * (1.0f - h * h);
    w2_[static_cast<size_t>(j)] += dw2;
    b1_[static_cast<size_t>(j)] += lr * gh;
    float* w = &w1_[static_cast<size_t>(j) * inN_];
    for (int i = 0; i < inN_; ++i) w[i] += lr * gh * in[i];
  }
  b2_[0] += lr * gPre;
}

void Mlp::serialize(BinaryWriter& w) const {
  w.u16(static_cast<uint16_t>(inN_));
  w.u16(static_cast<uint16_t>(hiddenN_));
  w.u16(static_cast<uint16_t>(outN_));
  w.u8(static_cast<uint8_t>(outAct_));
  w.u32(static_cast<uint32_t>(w1_.size()));
  w.u32(static_cast<uint32_t>(b1_.size()));
  w.u32(static_cast<uint32_t>(w2_.size()));
  w.u32(static_cast<uint32_t>(b2_.size()));
  for (float v : w1_) w.f32(v);
  for (float v : b1_) w.f32(v);
  for (float v : w2_) w.f32(v);
  for (float v : b2_) w.f32(v);
}

bool Mlp::deserialize(BinaryReader& r) {
  uint16_t inN, hiddenN, outN;
  uint8_t outAct;
  if (!r.u16(inN) || !r.u16(hiddenN) || !r.u16(outN) || !r.u8(outAct) || outAct > 2) {
    return false;
  }
  uint32_t n1, nb1, n2, nb2;
  if (!r.u32(n1) || !r.u32(nb1) || !r.u32(n2) || !r.u32(nb2)) return false;
  if (n1 != static_cast<uint32_t>(inN) * hiddenN || nb1 != hiddenN ||
      n2 != static_cast<uint32_t>(hiddenN) * outN || nb2 != outN) {
    return false;
  }
  inN_ = inN;
  hiddenN_ = hiddenN;
  outN_ = outN;
  outAct_ = static_cast<OutAct>(outAct);
  w1_.resize(n1);
  b1_.resize(nb1);
  w2_.resize(n2);
  b2_.resize(nb2);
  for (float& v : w1_) {
    if (!r.f32(v)) return false;
  }
  for (float& v : b1_) {
    if (!r.f32(v)) return false;
  }
  for (float& v : w2_) {
    if (!r.f32(v)) return false;
  }
  for (float& v : b2_) {
    if (!r.f32(v)) return false;
  }
  return true;
}

} // namespace eidolon