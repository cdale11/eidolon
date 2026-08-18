// Tiny single-hidden-layer MLP with tanh hidden activation and configurable output
// activation (linear / tanh / sigmoid). Bounded and small (hidden <= kMaxHidden) so the
// hot path uses caller-provided stack scratch and never allocates. Weights are floats.
#pragma once

#include <cstdint>
#include <vector>

#include "core/rng.hpp"
#include "core/serialize.hpp"

namespace eidolon {

enum class OutAct : uint8_t { Linear = 0, Tanh = 1, Sigmoid = 2 };

class Mlp {
public:
  static constexpr int kMaxHidden = 64;

  Mlp() = default;
  Mlp(int inN, int hiddenN, int outN, OutAct outAct);

  int inputs() const { return inN_; }
  int hiddenN() const { return hiddenN_; }
  int outputs() const { return outN_; }

  void reset(Rng& r, float scale);

  // Forward pass. `hidden` must be caller scratch of >= hiddenN_ floats (or nullptr if
  // the result isn't needed); writes outN_ outputs to `out`.
  void forward(const float* in, float* hidden, float* out) const;

  // Semi-gradient update for a single output node. `gPre` is the gradient of the loss
  // w.r.t. the output's pre-activation (for Linear output = dL/dout; for Sigmoid with
  // binary cross-entropy = out - target). `hidden` must hold the activations from the
  // matching forward pass. Assumes outN_ == 1.
  void backprop(const float* in, const float* hidden, float gPre, float lr);

  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  int inN_ = 0;
  int hiddenN_ = 0;
  int outN_ = 0;
  OutAct outAct_ = OutAct::Linear;
  std::vector<float> w1_, b1_; // input->hidden, hidden bias
  std::vector<float> w2_, b2_; // hidden->output, output bias
};

} // namespace eidolon