#ifndef EIDOLON_LLM_PLANNER_HPP
#define EIDOLON_LLM_PLANNER_HPP

#include <vector>
#include <string>
#include <array>
#include <optional>
#include <functional>

#include "mind/world_predictor.hpp"
#include "mind/goal_emergence.hpp"
#include "core/rng.hpp"
#include "llm/bridge.hpp"

namespace eidolon {

// LLM-assisted high-level plan proposals
// The LLM proposes high-level plans which are validated and executed by the runtime only
// The LLM never runs in the hot path; it's called occasionally for strategic planning

struct LLMPlanProposal {
  std::string description;           // Human-readable plan description
  std::vector<std::string> steps;    // High-level steps (e.g., "go to water", "build shelter")
  float estimated_value = 0.0f;      // Estimated utility
  float confidence = 0.0f;           // LLM's confidence in the plan
  bool valid = false;                // Set after validation
};

class LLMPlanner {
public:
  // Callback type for LLM queries: prompt -> response
  using LLMCallback = std::function<std::string(const std::string&)>;

  LLMPlanner() = default;
  explicit LLMPlanner(LLMCallback llm_callback);

  void set_callback(LLMCallback cb) { llm_callback_ = std::move(cb); }

  // Request a high-level plan from LLM given current state and goals
  // Returns empty optional if LLM unavailable or response invalid
  std::optional<LLMPlanProposal> propose_plan(
      const Physiology& body,
      const class World& world,
      const std::vector<Goal>& active_goals,
      const std::vector<Opportunity>& opportunities,
      class Rng& rng);

  // Validate an LLM plan proposal using the WorldPredictor
  // Returns true if plan is feasible and improves expected value
  bool validate_plan(const LLMPlanProposal& proposal,
                     const std::array<float, 43>& current_features,
                     const WorldPredictor& predictor,
                     class Rng& rng) const;

  // Convert high-level plan to action sequence for execution
  std::vector<uint8_t> compile_plan(const LLMPlanProposal& plan,
                                    const std::array<float, 43>& current_features,
                                    const WorldPredictor& predictor,
                                    const Planner& planner,
                                    class Rng& rng) const;

  bool has_callback() const { return static_cast<bool>(llm_callback_); }

private:
  std::function<std::string(const std::string&)> llm_callback_;

  // Build prompt for LLM
  std::string build_prompt(const Physiology& body,
                           const class World& /*world*/,
                           const std::vector<Goal>& active_goals,
                           const std::vector<Opportunity>& opportunities) const;

  // Parse LLM response into structured plan
  std::optional<LLMPlanProposal> parse_response(const std::string& response) const;
};

} // namespace eidolon

#endif // EIDOLON_LLM_PLANNER_HPP