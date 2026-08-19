#include "world/lsystem.hpp"
#include <cmath>
#include <stack>
#include <random>

#include "core/rng.hpp"

namespace eidolon {

std::string LSystemInterpreter::generate(int iterations) const {
  std::string current = system_.axiom;
  for (int i = 0; i < iterations; ++i) {
    std::string next;
    next.reserve(current.size() * 3);
    for (char c : current) {
      bool matched = false;
      for (const auto& rule : system_.rules) {
        if (rule.predecessor == c) {
          // Stochastic rule: apply with probability
          // For deterministic generation, we always apply the first matching rule
          next += rule.successor;
          matched = true;
          break;
        }
      }
      if (!matched) next += c;
    }
    current.swap(next);
  }
  return current;
}

std::vector<LSystemBranch> LSystemInterpreter::interpret(
    const Vec2f& startPos, float startHeading, float startLength, float startWidth,
    int maxDepth) const {
  std::string generated = generate(system_.maxDepth);
  if (maxDepth >= 0 && maxDepth < system_.maxDepth) {
    generated = generate(maxDepth);
  }

  std::vector<LSystemBranch> branches;
  branches.reserve(generated.size() / 2);

  struct StackEntry {
    Vec2f pos;
    float heading;
    float length;
    float width;
    int depth;
    int parentIdx;
  };

  std::stack<StackEntry> st;
  st.push({startPos, startHeading, startLength, startWidth, 0, -1});
  int currentIdx = -1;

  float angleRad = system_.angle * 3.14159265358979323846f / 180.0f;

  for (char c : generated) {
    if (st.empty()) break;
    auto state = st.top(); st.pop();

    switch (c) {
      case 'F': // Forward with drawing
      case 'G': // Forward without drawing (for river/road)
      case 'f': // Forward with drawing, smaller
      {
        Vec2f endPos = {
          state.pos.x + state.length * std::cos(state.heading),
          state.pos.y + state.length * std::sin(state.heading)
        };
        if (c != 'G') { // G = move without drawing
          branches.push_back({
            state.pos, endPos, state.width, state.depth, state.parentIdx
          });
          currentIdx = static_cast<int>(branches.size()) - 1;
        }
        // Continue forward with reduced length/width
        st.push({
          endPos, state.heading,
          state.length * 0.7f, state.width * 0.7f,
          state.depth + 1, (c == 'G') ? state.parentIdx : currentIdx
        });
        break;
      }
      case '+': // Turn right
        st.push({state.pos, state.heading + angleRad, state.length, state.width, state.depth, state.parentIdx});
        break;
      case '-': // Turn left
        st.push({state.pos, state.heading - angleRad, state.length, state.width, state.depth, state.parentIdx});
        break;
      case '[': // Push state
        st.push(state);
        break;
      case ']': // Pop state (already handled by stack pop above)
        break;
      default:
        // Unknown symbol - ignore or could add custom handling
        st.push(state);
        break;
    }
  }
  return branches;
}

std::vector<LSystemBranch> LSystemInterpreter::interpretStochastic(
    const Vec2f& startPos, float startHeading, float startLength, float startWidth,
    int maxDepth, class Rng& rng) const {
  // For stochastic L-systems, we'd need probabilistic rule selection.
  // This is a placeholder - the deterministic version above is used for now.
  // Full stochastic version would use rng.range() to select rules probabilistically.
  (void)rng;
  return interpret(startPos, startHeading, startLength, startWidth, maxDepth);
}

LSystemInterpreter::Network LSystemInterpreter::interpretNetwork(
    const Vec2f& startPos, float startHeading, float startLength, float startWidth,
    int maxDepth, class Rng& rng) const {
  Network net;
  std::string generated = generate(maxDepth >= 0 ? maxDepth : system_.maxDepth);

  struct NetState {
    Vec2f pos;
    float heading;
    float length;
    int nodeIdx;
  };

  std::vector<NetState> stack;
  stack.push_back({startPos, startHeading, startLength, 0});
  net.nodes.push_back(startPos);

  float angleRad = system_.angle * 3.14159265358979323846f / 180.0f;

  for (char c : generated) {
    if (stack.empty()) break;
    auto state = stack.back();
    stack.pop_back();

    switch (c) {
      case 'F':
      case 'G': {
        Vec2f endPos = {
          state.pos.x + state.length * std::cos(state.heading),
          state.pos.y + state.length * std::sin(state.heading)
        };
        int newNodeIdx = static_cast<int>(net.nodes.size());
        net.nodes.push_back(endPos);
        net.edges.emplace_back(state.nodeIdx, newNodeIdx);

        // Branch with probability for river networks
        float branchProb = rng.range(0.0, 1.0);
        if (branchProb < 0.3f) {
          // Add branch
          float newHeading = state.heading + (rng.range(0.0, 1.0) < 0.5 ? 1.0 : -1.0) * angleRad * 0.5f;
          stack.push_back({endPos, newHeading, state.length * 0.7f, newNodeIdx});
        }
        // Continue main
        stack.push_back({endPos, state.heading, state.length * 0.9f, newNodeIdx});
        break;
      }
      case '+':
        stack.push_back({state.pos, state.heading + angleRad, state.length, state.nodeIdx});
        break;
      case '-':
        stack.push_back({state.pos, state.heading - angleRad, state.length, state.nodeIdx});
        break;
      case '[':
        stack.push_back(state);
        break;
      case ']':
        // Pop handled by stack pop
        break;
      default:
        break;
    }
  }
  return net;
}

// Predefined L-systems

LSystem makeFernLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "FF+[+F-F-F]-[-F+F+F]"}};
  sys.angle = 22.5f;
  sys.maxDepth = 5;
  return sys;
}

LSystem makeBushLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "FF+[+F-F-F]-[-F+F+F]"}};
  sys.angle = 25.0f;
  sys.maxDepth = 4;
  return sys;
}

LSystem makeTreeLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "FF-[-F+F+F]+[+F-F-F]"}};
  sys.angle = 20.0f;
  sys.maxDepth = 6;
  return sys;
}

LSystem makeGrassLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "F[+F]F[-F]F"}};
  sys.angle = 25.0f;
  sys.maxDepth = 3;
  return sys;
}

LSystem makeRiverLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "F+F-F-F+F"}};
  sys.angle = 90.0f; // 90-degree turns for river-like patterns
  sys.maxDepth = 4;
  return sys;
}

LSystem makeRoadLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "F+F-F-F+F"}};
  sys.angle = 90.0f;
  sys.maxDepth = 3;
  return sys;
}

LSystem makeRootLSystem() {
  LSystem sys;
  sys.axiom = "F";
  sys.rules = {{'F', "F[+F]F[-F]F"}};
  sys.angle = 30.0f;
  sys.maxDepth = 4;
  return sys;
}

} // namespace eidolon