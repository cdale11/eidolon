// LLM bridge (DESIGN §14): OpenAI-compatible chat-completions client for parse/respond
// calls against llama.cpp llama-server (default) or any compatible endpoint. Never in
// the tick path; failures fall back to deterministic replies built from body state and
// memory. The LLM never mutates world state.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "mind/memory.hpp"
#include "mind/goal_emergence.hpp"
#include "mind/user_model.hpp"
#include "mind/wildlife_social.hpp"
#include "body/physiology.hpp"
#include "world/world.hpp"
#include "sim/engine.hpp"

namespace eidolon {

struct ParsedMessage {
  std::string intent;   // e.g. "greet", "question", "request", "smalltalk"
  std::string topic;    // referenced object/concept, if any
  std::string tone;     // e.g. "neutral", "warm", "concerned"
  bool referencesMemory = false; // user asked about events/memories
};

struct CognitiveSnapshot {
  // Core identity & time
  int64_t simTime = 0;
  bool alive = true;
  bool awake = true;
  int day = 0;
  double hour = 0;

  // Physiology (drives)
  double energy = 0, hunger = 0, thirst = 0, fatigue = 0;
  double sleepPressure = 0, bodyTemp = 0, health = 0;
  double pain = 0;

  // Position & environment
  int posX = 0, posY = 0;
  std::string terrain;  // underfoot
  std::string weather;  // "clear"/"rain"/"storm"/"snow"
  double ambientTempC = 0;

  // Nearby threats (within sight radius = 8 tiles)
  int predatorsNear = 0;
  int predatorDist = -1;  // -1 = none in sight
  int preyNear = 0;

  // Nearby resources (within sight radius)
  int waterDist = -1;     // -1 = none in sight
  int plantDist = -1;     // -1 = none in sight
  std::string plantType;  // "edible"/"toxic"/"medicinal"/"wood"/empty

  // Inventory & waterskin
  uint8_t waterCarried = 0;
  uint8_t waterCapacity = 0;

  // Current action/activity
  std::string currentAction;  // "wander"/"forage"/"drink"/"rest"/"sleep"/"flee"/"observe"/"farm"/"cook"/"craft"/"build"/"collectwater"/"preserve"

  // Threat level (0..1)
  double threatLevel = 0.0;

  // Active goals (from GoalEmergence)
  std::vector<std::string> activeGoals;

  // Personality & drives (summary)
  std::string personalitySummary;  // e.g. "cautious, curious, attached to user"
  std::string driveSummary;        // e.g. "hunger>thirst>safety>curiosity"

  // Social - User model
  double userTrust = 0.0;
  double userFamiliarity = 0.0;
  double userAffection = 0.0;
  bool userExpectsReturn = false;

  // Social - Wildlife
  std::string wildlifeSummary;  // e.g. "wolf familiar=0.2 fear=0.8"

  // Recent memories (last 6 episodes, compact)
  std::string recentMemorySummary;

  // Skills/competence
  std::string skillSummary;  // e.g. "forage=0.8, drink=0.6, craft=0.1"

  // Circadian & physiological tone (DESIGN future-direction: time-of-day awareness).
  // All fields are derived deterministically from the existing snapshot fields above;
  // no new persistent state, no extra LLM calls. They let the LLM (and the deterministic
  // fallback) ground the reply in the organism's actual circadian / drive state right now
  // — a 3am reply is groggy, a 3pm reply after a meal is calm, etc.
  std::string phaseOfDay;     // "deep_night"/"dawn"/"day"/"dusk"
  std::string timeOfDayPhrase; // "just before dawn", "early morning", "midday",
                              // "late afternoon", "evening", "night", "deep night"
  std::string seasonName;     // "spring"/"summer"/"autumn"/"winter"
  std::string physiologicalState; // "rested"/"drowsy"/"tired"/"exhausted"/"asleep"/
                                  // "pained"/"sick"/"fine"
  std::string primaryNeed;    // "thirsty"/"hungry"/"tired"/"fine" — most pressing need
  std::string circadianTone;  // one-word tone hint: "groggy"/"calm"/"alert"/"tense"/
                              // "agitated"/"peaceful"/"weary"
};

// Builds the comprehensive cognitive snapshot from engine state (~2-4 k tokens target).
CognitiveSnapshot makeSnapshot(const Engine& engine);

// Deterministic fallback reply generated purely from state (LLM down/garbage).
std::string fallbackReply(const CognitiveSnapshot& s, const std::string& userText);

class LLMBridge {
public:
  // `endpoint` like "http://127.0.0.1:8080/v1". Empty endpoint disables calls (offline).
  explicit LLMBridge(std::string endpoint, int timeoutMs = 10000)
      : endpoint_(std::move(endpoint)), timeoutMs_(timeoutMs) {}

  // Whether calls will actually hit the network.
  bool enabled() const { return !endpoint_.empty(); }

  // parse: user message → structured semantics. Returns false on failure.
  bool parse(const std::string& userText, const CognitiveSnapshot& s, ParsedMessage& out,
             std::string& raw);

  // respond: snapshot + parse → grounded natural language reply. Returns false on
  // failure (caller falls back).
  bool respond(const std::string& userText, const CognitiveSnapshot& s,
               const ParsedMessage& parsed, std::string& reply, std::string& raw);

  // Health: last call outcome (for observability).
  int64_t calls() const { return calls_; }
  int64_t failures() const { return failures_; }

private:
  bool chatComplete(const JsonValue& messages, int maxTokens, JsonValue& out);
  bool post(const std::string& body, std::string& response);

  std::string endpoint_;
  int timeoutMs_;
  int64_t calls_ = 0;
  int64_t failures_ = 0;
};

} // namespace eidolon