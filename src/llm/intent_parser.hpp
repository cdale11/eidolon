// Intent parsing for user instructions (DESIGN §Future: Learning from User Speech).
// Maps free-form text instructions to structured goal actions that the organism
// can pursue through its normal planning loop. Never injected as prompt text.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace eidolon {

// User instruction intent types mapped to organism actions
enum class UserIntentType : uint8_t {
  None = 0,
  // Navigation / movement
  GoToLocation = 1,      // "go to [location]", "move to [x,y]"
  FollowMe = 2,          // "follow me", "come here"
  Explore = 3,           // "explore", "look around"
  // Resource gathering
  Forage = 4,            // "find food", "get berries", "forage"
  Drink = 5,             // "get water", "drink", "find water"
  // Rest / survival
  Rest = 6,              // "rest", "take a break"
  Sleep = 7,             // "sleep", "go to sleep"
  // Safety
  Flee = 8,              // "run away", "flee", "escape"
  Avoid = 9,             // "avoid [threat]", "stay away from [thing]"
  // Construction / crafting
  Build = 10,            // "build [structure]", "make shelter"
  Craft = 11,            // "craft [item]", "make [tool]"
  // Information
  Observe = 12,          // "look", "observe", "what do you see"
  Status = 13,           // "how are you", "status", "health"
  // Social
  Greet = 14,            // "hello", "hi"
  Thank = 15,            // "thanks", "thank you"
  // Control
  Stop = 16,             // "stop", "halt"
  Wait = 17,             // "wait", "stay"
  Cancel = 18,           // "cancel", "never mind"
};

// Parsed user instruction with validation metadata
struct ParsedInstruction {
  UserIntentType intent = UserIntentType::None;
  float confidence = 0.0f;           // 0.0 - 1.0
  std::string target;                // location, item, entity reference
  std::vector<std::string> params;   // additional parameters
  bool valid = false;                // passed validation
  std::string validationError;       // if !valid
  std::string originalText;          // original user input
};

// Context for validation (current organism state)
struct ValidationContext {
  // Current organism state
  float health = 100.0f;
  float hunger = 0.0f;
  float thirst = 0.0f;
  float fatigue = 0.0f;
  float energy = 100.0f;
  bool isSleeping = false;
  bool isResting = false;
  int64_t simTime = 0;
  int day = 0;
  float hour = 0.0f;
  
  // World state
  int worldW = 128;
  int worldH = 128;
  int posX = 0;
  int posY = 0;
  
  // Nearby threats/resources (set by caller if available)
  bool predatorNearby = false;
  int predatorDist = -1;
  bool waterNearby = false;
  int waterDist = -1;
  bool foodNearby = false;
  int foodDist = -1;
};

// Intent parser with rule-based + keyword matching
class IntentParser {
public:
  IntentParser() = default;

  // Parse user text into structured instruction
  ParsedInstruction parse(const std::string& text) const;

  // Validate instruction against current state
  ParsedInstruction validate(const ParsedInstruction& instr, 
                             const ValidationContext& ctx) const;

  // Parse + validate in one step
  ParsedInstruction parseAndValidate(const std::string& text,
                                     const ValidationContext& ctx) const {
    auto parsed = parse(text);
    return validate(parsed, ctx);
  }

private:
  // Keyword sets for each intent (lowercase)
  static const std::vector<std::pair<UserIntentType, std::vector<std::string>>> INTENT_KEYWORDS;
  
  // Extract target/parameters from text after intent keyword
  static std::pair<std::string, std::vector<std::string>> extractParams(
      const std::string& text, const std::string& matchedKeyword);
};

} // namespace eidolon