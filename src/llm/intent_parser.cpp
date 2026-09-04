// Intent parsing implementation
#include "llm/intent_parser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

namespace eidolon {

// Keyword sets for each intent type (all lowercase)
const std::vector<std::pair<UserIntentType, std::vector<std::string>>> IntentParser::INTENT_KEYWORDS = {
  {UserIntentType::GoToLocation, {"go to", "move to", "travel to", "walk to", "run to"}},
  {UserIntentType::FollowMe, {"follow me", "come here", "come with me"}},
  {UserIntentType::Explore, {"explore", "look around", "wander", "scout"}},
  {UserIntentType::Forage, {"forage", "find food", "get food", "get berries", "find berries", "eat", "hunt food"}},
  {UserIntentType::Drink, {"drink", "get water", "find water", "water", "thirsty"}},
  {UserIntentType::Rest, {"rest", "take a break", "relax", "pause"}},
  {UserIntentType::Sleep, {"sleep", "go to sleep", "go to bed", "nap"}},
  {UserIntentType::Flee, {"flee", "run away", "escape", "run", "get away"}},
  {UserIntentType::Avoid, {"avoid", "stay away from", "don't go near", "keep away from"}},
  {UserIntentType::Build, {"build", "make shelter", "construct", "build shelter", "make camp"}},
  {UserIntentType::Craft, {"craft", "make", "create", "forge", "build tool"}},
  {UserIntentType::Observe, {"look", "observe", "see", "what do you see", "look around"}},
  {UserIntentType::Status, {"status", "how are you", "health", "how do you feel", "condition"}},
  {UserIntentType::Greet, {"hello", "hi", "hey", "greetings"}},
  {UserIntentType::Thank, {"thanks", "thank you", "thx", "ty"}},
  {UserIntentType::Stop, {"stop", "halt", "freeze"}},
  {UserIntentType::Wait, {"wait", "stay", "hold on"}},
  {UserIntentType::Cancel, {"cancel", "never mind", "forget it", "ignore"}},
};

ParsedInstruction IntentParser::parse(const std::string& text) const {
  ParsedInstruction result;
  result.originalText = text;
  
  if (text.empty()) {
    result.intent = UserIntentType::None;
    result.confidence = 0.0f;
    return result;
  }
  
  // Normalize text
  std::string normalized = text;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), 
                 [](unsigned char c) { return std::tolower(c); });
  
  // Trim
  auto trim = [](std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) { s.clear(); return; }
    s = s.substr(start, end - start + 1);
  };
  trim(normalized);
  
  if (normalized.empty()) {
    result.intent = UserIntentType::None;
    result.confidence = 0.0f;
    return result;
  }
  
  // Find best matching intent
  UserIntentType bestIntent = UserIntentType::None;
  std::string bestKeyword;
  float bestConfidence = 0.0f;
  
  for (const auto& [intent, keywords] : INTENT_KEYWORDS) {
    for (const auto& keyword : keywords) {
      size_t pos = normalized.find(keyword);
      if (pos != std::string::npos) {
        // Calculate confidence based on position and keyword length
        // Earlier position + longer keyword = higher confidence
        float confidence = 1.0f - (static_cast<float>(pos) / static_cast<float>(normalized.length() + 1)) * 0.3f;
        confidence += static_cast<float>(keyword.length()) / 50.0f; // longer keywords = more specific
        
        // Exact match or start of sentence boost
        if (pos == 0) confidence += 0.2f;
        if (pos + keyword.length() == normalized.length() || 
            (pos + keyword.length() < normalized.length() && 
             (normalized[pos + keyword.length()] == ' ' || normalized[pos + keyword.length()] == ','))) {
          confidence += 0.15f;
        }
        
        if (confidence > bestConfidence) {
          bestConfidence = confidence;
          bestIntent = static_cast<UserIntentType>(intent);
          bestKeyword = keyword;
        }
      }
    }
  }
  
  if (bestIntent == UserIntentType::None) {
    result.intent = UserIntentType::None;
    result.confidence = 0.0f;
    return result;
  }
  
  result.intent = bestIntent;
  result.confidence = std::min(1.0f, bestConfidence);
  
  // Extract target/params
  auto [target, params] = extractParams(normalized, bestKeyword);
  result.target = target;
  result.params = params;
  
  return result;
}

std::pair<std::string, std::vector<std::string>> IntentParser::extractParams(
    const std::string& text, const std::string& matchedKeyword) {
  std::string target;
  std::vector<std::string> params;
  
  // Find the keyword position
  size_t pos = text.find(matchedKeyword);
  if (pos == std::string::npos) {
    return {target, params};
  }
  
  // Extract text after the keyword
  size_t afterPos = pos + matchedKeyword.length();
  std::string remaining;
  if (afterPos < text.length()) {
    remaining = text.substr(afterPos);
  }
  
  // Trim leading punctuation/whitespace
  size_t start = remaining.find_first_not_of(" \t,.:;");
  if (start != std::string::npos) {
    remaining = remaining.substr(start);
  }
  
  if (!remaining.empty()) {
    // The whole remaining text is the target
    target = remaining;
    
    // Also split into params by common separators
    std::regex sepRe("[,;]|\\s+and\\s+");
    std::sregex_token_iterator it(remaining.begin(), remaining.end(), sepRe, -1);
    std::sregex_token_iterator end;
    for (; it != end; ++it) {
      std::string param = it->str();
      // Trim
      size_t pStart = param.find_first_not_of(" \t");
      size_t pEnd = param.find_last_not_of(" \t");
      if (pStart != std::string::npos) {
        param = param.substr(pStart, pEnd - pStart + 1);
        if (!param.empty()) params.push_back(param);
      }
    }
  }
  
  return {target, params};
}

ParsedInstruction IntentParser::validate(const ParsedInstruction& instr, 
                                         const ValidationContext& ctx) const {
  ParsedInstruction result = instr;
  result.valid = true;
  
  if (!result.valid) return result; // Already invalid
  
  // Check confidence threshold
  if (instr.confidence < 0.3f) {
    result.valid = false;
    result.validationError = "Low confidence in parsed intent";
    return result;
  }
  
  // State-based validation
  switch (instr.intent) {
    case UserIntentType::Sleep:
      if (ctx.isSleeping) {
        result.valid = false;
        result.validationError = "Already sleeping";
      }
      // Fall through to rest check
      [[fallthrough]];
    case UserIntentType::Rest:
      if (ctx.isResting || ctx.isSleeping) {
        result.valid = false;
        result.validationError = "Already resting";
      }
      if (ctx.energy > 90.0f && ctx.fatigue < 10.0f) {
        result.valid = false;
        result.validationError = "Already well-rested";
      }
      break;
      
    case UserIntentType::Flee:
      if (!ctx.predatorNearby && ctx.predatorDist < 0) {
        // Allow but warn - fleeing without threat wastes energy
      }
      if (ctx.energy < 20.0f) {
        result.valid = false;
        result.validationError = "Too exhausted to flee";
      }
      // Fall through to movement energy check
      [[fallthrough]];
    case UserIntentType::FollowMe:
    case UserIntentType::GoToLocation:
      if (ctx.energy < 10.0f) {
        result.valid = false;
        result.validationError = "Too exhausted to move";
      }
      // Additional GoToLocation validation
      if (instr.intent == UserIntentType::GoToLocation && instr.target.empty()) {
        result.valid = false;
        result.validationError = "No destination specified";
      }
      break;
      
    case UserIntentType::Forage:
      if (ctx.hunger < 10.0f) {
        result.valid = false;
        result.validationError = "Not hungry enough to forage";
      }
      break;
      
    case UserIntentType::Drink:
      if (ctx.thirst < 10.0f) {
        result.valid = false;
        result.validationError = "Not thirsty enough to drink";
      }
      break;
      
    case UserIntentType::Build:
      if (ctx.energy < 30.0f) {
        result.valid = false;
        result.validationError = "Too exhausted to build";
      }
      break;
      
    case UserIntentType::Craft:
      if (ctx.energy < 20.0f) {
        result.valid = false;
        result.validationError = "Too exhausted to craft";
      }
      break;
      
    default:
      break;
  }
  
  // General health check
  if (ctx.health <= 0.0f) {
    result.valid = false;
    result.validationError = "Organism is dead";
  }
  
  return result;
}

} // namespace eidolon