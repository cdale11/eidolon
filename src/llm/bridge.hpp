// LLM bridge (DESIGN §14): OpenAI-compatible chat-completions client for parse/respond
// calls against llama.cpp llama-server (default) or any compatible endpoint. Never in
// the tick path; failures fall back to deterministic replies built from body state and
// memory. The LLM never mutates world state.
#pragma once

#include <cstdint>
#include <string>

#include "core/json.hpp"
#include "mind/memory.hpp"

namespace eidolon {

struct ParsedMessage {
  std::string intent;   // e.g. "greet", "question", "request", "smalltalk"
  std::string topic;    // referenced object/concept, if any
  std::string tone;     // e.g. "neutral", "warm", "concerned"
  bool referencesMemory = false; // user asked about events/memories
};

struct CognitiveSnapshot {
  int64_t simTime = 0;
  bool alive = true;
  bool awake = true;
  double energy = 0, hunger = 0, thirst = 0, fatigue = 0;
  double sleepPressure = 0, bodyTemp = 0, health = 0;
  int day = 0;
  double hour = 0;
  std::string weather;  // "clear"/"rain"/"storm"/"snow"
  std::string terrain;  // underfoot
  double ambientTempC = 0;
  std::string recentMemorySummary; // last few episodes, compact
};

// Builds the compact cognitive snapshot from engine state (1–2 k tokens target).
CognitiveSnapshot makeSnapshot(int64_t simTime, bool alive, bool awake, double energy,
                               double hunger, double thirst, double fatigue,
                               double sleepPressure, double bodyTemp, double health,
                               int day, double hour, const char* weather,
                               const char* terrain, double ambientTempC,
                               const MemoryRing& memory);

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