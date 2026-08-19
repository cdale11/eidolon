#ifndef EIDOLON_GROUNDED_LANGUAGE_HPP
#define EIDOLON_GROUNDED_LANGUAGE_HPP

#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <cstdint>

#include "core/serialize.hpp"
#include "core/rng.hpp"
#include "world/grammar.hpp"
#include "mind/memory.hpp"

namespace eidolon {

class Archive;
class MemoryRing;

// Grounded language generation using formal grammars
// All utterances are deterministically generated from the organism's actual
// event log and state - no LLM required, fully reproducible from seed.

struct GroundedUtterance {
  std::string text;              // generated utterance
  std::vector<std::string> sourceEvents; // event IDs that grounded this
  std::vector<std::string> templatesUsed; // grammar productions used
  uint64_t generatedAtTick = 0;
  bool honestUncertainty = false; // if asked about unrecorded events
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct DailySummary {
  uint64_t day = 0;
  std::string summary;           // "Today I foraged for berries, drank from the river, and built a shelter."
  std::vector<std::string> keyEvents;
  std::vector<std::string> drives;
  uint64_t generatedAtTick = 0;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class GroundedLanguage {
public:
  using LLMCallback = std::function<std::string(const std::string&)>;
  
  GroundedLanguage() = default;
  explicit GroundedLanguage(uint64_t seed);
  
  void set_seed(uint64_t s) { seed_ = s; rng_ = Rng(s); }
  
  // Generate a grounded response to "what did you do today?"
  std::optional<GroundedUtterance> answer_what_did_you_do(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick);
  
  // Generate a grounded daily summary (for reflection/end of day)
  std::optional<DailySummary> generate_daily_summary(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick);
  
  // Generate a grounded response to a specific question about past
  std::optional<GroundedUtterance> answer_about_past(
      const Archive& archive,
      const MemoryRing& memory,
      const std::string& question,
      uint64_t currentTick);
  
  // Generate a grounded greeting based on current state
  std::optional<GroundedUtterance> generate_greeting(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick);
  
  // Generate a grounded statement about current needs
  std::optional<GroundedUtterance> generate_need_statement(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick);
  
  // Generate a grounded observation about world
  std::optional<GroundedUtterance> generate_observation(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick);
  
  // Check if we have events about a topic (for honest uncertainty)
  bool has_events_about(const Archive& archive, const std::string& topic,
                        uint64_t startTick, uint64_t endTick) const;
  
  // Serialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  uint64_t seed_ = 0;
  mutable Rng rng_;
  GrammarEngine utteranceEngine_;
  GrammarEngine eventEngine_;
  GrammarEngine summaryEngine_;
  
  // Initialize grammar engines with predefined grammars
  void init_grammars();
  
  // Extract events from archive for a time range
  struct ExtractedEvent {
    std::string description;
    uint64_t tick;
    int16_t x, y;
    EventKind kind;
    uint8_t action;
    Outcome outcome;
    double importance;
  };
  std::vector<ExtractedEvent> extract_events(
      const Archive& archive,
      uint64_t startTick,
      uint64_t endTick) const;
  
  // Build daily summary from events
  std::string build_summary_from_events(
      const std::vector<ExtractedEvent>& events) const;
  
  // Build utterance from template + events
  std::string build_utterance_from_template(
      const std::vector<ExtractedEvent>& events) const;
  
  // Map event kinds to grammar terminals
  std::string event_kind_to_terminal(EventKind kind) const;
  std::string action_to_terminal(uint8_t action) const;
  std::string outcome_to_terminal(Outcome outcome) const;
  
  // Select events relevant to a question topic
  std::vector<ExtractedEvent> select_relevant_events(
      const std::vector<ExtractedEvent>& allEvents,
      const std::string& question) const;
  
  // Generate honest uncertainty response
  GroundedUtterance make_uncertainty_response(uint64_t tick) const;
};

} // namespace eidolon

#endif // EIDOLON_GROUNDED_LANGUAGE_HPP