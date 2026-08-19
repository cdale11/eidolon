#ifndef EIDOLON_REFLECTION_HPP
#define EIDOLON_REFLECTION_HPP

#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <cstdint>

#include "core/serialize.hpp"
#include "core/rng.hpp"
#include "mind/memory.hpp"

namespace eidolon {

class Archive;

// Slow-layer reflection system for life review and narrative summaries
// Rate-limited LLM calls; all reflections grounded in actual event timeline

struct ReflectionResult {
  std::string summary;           // narrative summary
  std::vector<std::string> keyEvents; // event references
  std::vector<std::string> insights;  // learned insights
  std::vector<std::string> changes;   // personality/behavior changes
  bool honestUncertainty = false;     // if asked about unrecorded events
  uint64_t generatedAtTick = 0;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct LifeReview {
  uint64_t periodStartTick = 0;
  uint64_t periodEndTick = 0;
  std::string narrative;         // LLM-generated life review
  std::vector<std::string> majorEvents;
  std::vector<std::string> drivePatterns;
  std::vector<std::string> socialPatterns;
  std::vector<std::string> learnedLessons;
  std::string selfAssessment;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class ReflectionSystem {
public:
  using LLMCallback = std::function<std::string(const std::string&)>;
  
  ReflectionSystem() = default;
  explicit ReflectionSystem(uint64_t seed);
  
  void set_llm_callback(LLMCallback cb) { llm_callback_ = std::move(cb); }
  bool has_llm_callback() const { return static_cast<bool>(llm_callback_); }
  
  // Generate a reflection on recent events (rate-limited)
  std::optional<ReflectionResult> reflect_on_recent_events(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick,
      uint64_t lookbackTicks = 86400); // default 1 day
  
  // Generate "what happened while you were away" summary
  std::optional<ReflectionResult> summarize_absence(
      const Archive& archive,
      uint64_t lastSeenTick,
      uint64_t currentTick);
  
  // Full life review (periodic, e.g., weekly)
  std::optional<LifeReview> generate_life_review(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick);
  
  // Answer a question about past events - grounded in actual timeline
  std::optional<std::string> answer_about_past(
      const Archive& archive,
      const MemoryRing& memory,
      const std::string& question,
      uint64_t currentTick);
  
  // Rate limiting: minimum ticks between LLM calls
  void set_min_ticks_between_llm(uint64_t ticks) { min_ticks_between_llm_ = ticks; }
  bool can_call_llm(uint64_t currentTick) const;
  
  // Serialize reflection history
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  uint64_t seed_ = 0;
  Rng rng_;
  LLMCallback llm_callback_;
  uint64_t last_llm_call_tick_ = 0;
  uint64_t min_ticks_between_llm_ = 86400; // 1 day default
  
  // Reflection history for continuity
  std::vector<ReflectionResult> reflection_history_;
  std::vector<LifeReview> life_reviews_;
  
  // Build prompt for recent events reflection
  std::string build_recent_events_prompt(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick,
      uint64_t lookbackTicks) const;
  
  // Build prompt for absence summary
  std::string build_absence_prompt(
      const Archive& archive,
      uint64_t lastSeenTick,
      uint64_t currentTick) const;
  
  // Build prompt for life review
  std::string build_life_review_prompt(
      const Archive& archive,
      const MemoryRing& memory,
      uint64_t currentTick) const;
  
  // Build prompt for question answering
  std::string build_question_prompt(
      const Archive& archive,
      const MemoryRing& memory,
      const std::string& question,
      uint64_t currentTick) const;
  
  // Parse LLM response into structured reflection
  std::optional<ReflectionResult> parse_reflection_response(
      const std::string& response, uint64_t tick) const;
  
  // Parse LLM response into life review
  std::optional<LifeReview> parse_life_review_response(
      const std::string& response, uint64_t tick) const;
  
  // Extract event references from archive
  std::vector<std::string> extract_recent_events(
      const Archive& archive,
      uint64_t startTick,
      uint64_t endTick) const;
  
  // Check if event timeline contains info about a topic
  bool has_event_about(const Archive& archive, const std::string& topic,
                       uint64_t startTick, uint64_t endTick) const;
};

} // namespace eidolon

#endif // EIDOLON_REFLECTION_HPP