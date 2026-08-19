#ifndef EIDOLON_LSYSTEM_HPP
#define EIDOLON_LSYSTEM_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

#include "core/vec2.hpp"

namespace eidolon {

// L-systems for procedural geometry (plants, bushes, branches, rivers, roads, roots).
// Deterministic, seeded, bit-exact replay. Turtle interpretation with depth cap.
// Phase 5 branch (DESIGN §22).

struct LSystemRule {
  char predecessor;
  std::string successor;
  float probability = 1.0f; // for stochastic L-systems
};

struct LSystem {
  std::string axiom;
  std::vector<LSystemRule> rules;
  float angle = 25.7f; // default angle in degrees
  int maxDepth = 5;
};

struct TurtleState {
  Vec2f pos;
  float heading; // radians, 0 = right, CCW positive
  float length;
  float width;
};

struct LSystemBranch {
  Vec2f start;
  Vec2f end;
  float width;
  int depth;
  int parentIdx = -1;
};

class LSystemInterpreter {
public:
  LSystemInterpreter() = default;
  explicit LSystemInterpreter(const LSystem& sys) : system_(sys) {}

  void setSystem(const LSystem& sys) { system_ = sys; }

  // Generate the string after n iterations
  std::string generate(int iterations) const;

  // Interpret as turtle graphics, producing branches
  // Returns vector of branches (start, end, width, depth, parentIdx)
  std::vector<LSystemBranch> interpret(
      const Vec2f& startPos, float startHeading, float startLength, float startWidth,
      int maxDepth = -1) const;

  // Interpret with stochastic rules (using RNG for probabilistic branching)
  std::vector<LSystemBranch> interpretStochastic(
      const Vec2f& startPos, float startHeading, float startLength, float startWidth,
      int maxDepth, class Rng& rng) const;

  // River/road network: interpret as connected graph
  // Returns nodes and edges for river/road networks
  struct Network {
    std::vector<Vec2f> nodes;
    std::vector<std::pair<int, int>> edges; // node indices
  };
  Network interpretNetwork(const Vec2f& startPos, float startHeading,
                           float startLength, float startWidth,
                           int maxDepth, class Rng& rng) const;

  const LSystem& system() const { return system_; }
  void setAngle(float a) { system_.angle = a; }
  void setMaxDepth(int d) { system_.maxDepth = d; }

private:
  LSystem system_;
};

// Predefined plant L-systems
LSystem makeFernLSystem();
LSystem makeBushLSystem();
LSystem makeTreeLSystem();
LSystem makeGrassLSystem();
LSystem makeRiverLSystem();
LSystem makeRoadLSystem();
LSystem makeRootLSystem();

} // namespace eidolon

#endif // EIDOLON_LSYSTEM_HPP