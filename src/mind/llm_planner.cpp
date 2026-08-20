#include "mind/llm_planner.hpp"
#include <algorithm>
#include <sstream>
#include <regex>

namespace eidolon {

LLMPlanner::LLMPlanner(LLMCallback llm_callback) : llm_callback_(std::move(llm_callback)) {}

std::optional<LLMPlanProposal> LLMPlanner::propose_plan(
    const Physiology& body,
    const class World& world,
    const std::vector<Goal>& active_goals,
    const std::vector<Opportunity>& opportunities,
    class Rng& /*rng*/) {
  if (!llm_callback_) return std::nullopt;

  std::string prompt = build_prompt(body, world, active_goals, opportunities);
  std::string response = llm_callback_(prompt);

  if (response.empty()) return std::nullopt;

  return parse_response(response);
}

bool LLMPlanner::validate_plan(const LLMPlanProposal& proposal,
                               const std::array<float, 43>& /*current_features*/,
                               const WorldPredictor& /*predictor*/,
                               class Rng& /*rng*/) const {
  if (!proposal.valid) return false;

  // Quick feasibility check: simulate first step
  if (proposal.steps.empty()) return false;

  // For now, accept all plans with confidence > 0.5
  return proposal.confidence > 0.5f;
}

std::vector<uint8_t> LLMPlanner::compile_plan(const LLMPlanProposal& /*plan*/,
                                              const std::array<float, 43>& /*current_features*/,
                                              const WorldPredictor& /*predictor*/,
                                              const Planner& /*planner*/,
                                              class Rng& /*rng*/) const {
  // For now, delegate to the greedy planner
  // In a full implementation, this would parse the high-level steps into concrete actions
  std::vector<uint8_t> actions;
  return actions;
}

std::string LLMPlanner::build_prompt(const Physiology& body,
                                     const class World& /*world*/,
                                     const std::vector<Goal>& active_goals,
                                     const std::vector<Opportunity>& opportunities) const {
  std::ostringstream oss;
  oss << "You are an AI assisting a virtual organism in a survival simulation.\n";
  oss << "Current state:\n";
  oss << "  Health: " << body.health() << "/100\n";
  oss << "  Hunger: " << body.hunger() << "/100\n";
  oss << "  Thirst: " << body.thirst() << "/100\n";
  oss << "  Fatigue: " << body.fatigue() << "/100\n";
  oss << "  Energy: " << body.energy() << "/100\n";

  if (!active_goals.empty()) {
    oss << "Active goals:\n";
    for (const auto& g : active_goals) {
      oss << "  - " << static_cast<int>(g.type) << " (priority: " << g.priority << ")\n";
    }
  }

  if (!opportunities.empty()) {
    oss << "Nearby opportunities:\n";
    for (const auto& opp : opportunities) {
      oss << "  - " << static_cast<int>(opp.type) << " at (" << opp.position.x << "," << opp.position.y
          << ") value=" << opp.value << "\n";
    }
  }

  oss << "\nPropose a high-level plan (max 5 steps) to achieve the most important goal.\n";
  oss << "Format:\n";
  oss << "PLAN:\n";
  oss << "1. step description\n";
  oss << "2. step description\n";
  oss << "CONFIDENCE: 0.0-1.0\n";

  return oss.str();
}

std::optional<LLMPlanProposal> LLMPlanner::parse_response(const std::string& response) const {
  LLMPlanProposal proposal;
  proposal.valid = false;

  // Simple parsing: look for "PLAN:" and "CONFIDENCE:"
  std::istringstream iss(response);
  std::string line;
  bool in_plan = false;

  while (std::getline(iss, line)) {
    if (line.find("PLAN:") != std::string::npos) {
      in_plan = true;
      continue;
    }
    if (line.find("CONFIDENCE:") != std::string::npos) {
      std::string conf_str = line.substr(line.find(":") + 1);
      char* endptr = nullptr;
      float val = std::strtof(conf_str.c_str(), &endptr);
      if (endptr != conf_str.c_str() && *endptr == '\0') {
        proposal.confidence = val;
      } else {
        proposal.confidence = 0.0f;
      }
      in_plan = false;
      continue;
    }
    if (in_plan && !line.empty()) {
      // Remove leading numbers and dots
      std::string step = line;
      std::regex re("^\\s*\\d+[.)]\\s*");
      step = std::regex_replace(step, re, "");
      if (!step.empty()) {
        proposal.steps.push_back(step);
      }
    }
  }

  if (!proposal.steps.empty() && proposal.confidence > 0.0f) {
    proposal.valid = true;
    proposal.description = "LLM-proposed plan with " + std::to_string(proposal.steps.size()) + " steps";
  }

  return proposal.valid ? std::make_optional(proposal) : std::nullopt;
}

} // namespace eidolon