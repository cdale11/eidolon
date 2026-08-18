// Learning model interface: every model reports metrics (inference/update counts for
// observability, DESIGN §8) and persists itself inside the engine snapshot.
#pragma once

#include <cstdint>

#include "core/serialize.hpp"

namespace eidolon {

struct LearnerMetrics {
  uint64_t inferences = 0;
  uint64_t updates = 0;

  LearnerMetrics& operator+=(const LearnerMetrics& o) {
    inferences += o.inferences;
    updates += o.updates;
    return *this;
  }
};

class Learner {
public:
  virtual ~Learner() = default;
  virtual const LearnerMetrics& metrics() const = 0;
  virtual void serialize(BinaryWriter& w) const = 0;
  virtual bool deserialize(BinaryReader& r) = 0;
};

} // namespace eidolon