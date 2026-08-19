#include "mind/reflection.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace eidolon {

class Archive;

ReflectionSystem::ReflectionSystem(uint64_t seed) : seed_(seed), rng_(seed) {}

void ReflectionResult::serialize(struct BinaryWriter& w) const {
  w.u64(generatedAtTick);
  w.str(summary);
  w.u32(static_cast<uint32_t>(keyEvents.size()));
  for (const auto& e : keyEvents) w.str(e);
  w.u32(static_cast<uint32_t>(insights.size()));
  for (const auto& i : insights) w.str(i);
  w.u32(static_cast<uint32_t>(changes.size()));
  for (const auto& c : changes) w.str(c);
  w.u8(honestUncertainty ? 1 : 0);
}

bool ReflectionResult::deserialize(struct BinaryReader& r) {
  if (!r.u64(generatedAtTick) || !r.str(summary)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  keyEvents.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(keyEvents[i])) return false;
  if (!r.u32(n)) return false;
  insights.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(insights[i])) return false;
  if (!r.u32(n)) return false;
  changes.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(changes[i])) return false;
  uint8_t u;
  if (!r.u8(u)) return false;
  honestUncertainty = (u != 0);
  return true;
}

void LifeReview::serialize(struct BinaryWriter& w) const {
  w.u64(periodStartTick);
  w.u64(periodEndTick);
  w.str(narrative);
  w.u32(static_cast<uint32_t>(majorEvents.size()));
  for (const auto& e : majorEvents) w.str(e);
  w.u32(static_cast<uint32_t>(drivePatterns.size()));
  for (const auto& d : drivePatterns) w.str(d);
  w.u32(static_cast<uint32_t>(socialPatterns.size()));
  for (const auto& s : socialPatterns) w.str(s);
  w.u32(static_cast<uint32_t>(learnedLessons.size()));
  for (const auto& l : learnedLessons) w.str(l);
  w.str(selfAssessment);
}

bool LifeReview::deserialize(struct BinaryReader& r) {
  if (!r.u64(periodStartTick) || !r.u64(periodEndTick) || !r.str(narrative)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  majorEvents.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(majorEvents[i])) return false;
  if (!r.u32(n)) return false;
  drivePatterns.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(drivePatterns[i])) return false;
  if (!r.u32(n)) return false;
  socialPatterns.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(socialPatterns[i])) return false;
  if (!r.u32(n)) return false;
  learnedLessons.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(learnedLessons[i])) return false;
  if (!r.str(selfAssessment)) return false;
  return true;
}

std::optional<ReflectionResult> ReflectionSystem::reflect_on_recent_events(
    const Archive& archive,
    const MemoryRing& memory,
    uint64_t currentTick,
    uint64_t lookbackTicks) {
  
  if (!has_llm_callback() || !can_call_llm(currentTick)) {
    return std::nullopt;
  }
  
  (void)currentTick; (void)lookbackTicks;
  std::string prompt = build_recent_events_prompt(archive, memory, currentTick, lookbackTicks);
  
  std::string response = llm_callback_(prompt);
  last_llm_call_tick_ = currentTick;
  
  auto result = parse_reflection_response(response, currentTick);
  if (result) {
    reflection_history_.push_back(*result);
    if (reflection_history_.size() > 100) reflection_history_.erase(reflection_history_.begin());
  }
  return result;
}

std::optional<ReflectionResult> ReflectionSystem::summarize_absence(
    const Archive& archive,
    uint64_t lastSeenTick,
    uint64_t currentTick) {
  
  if (!has_llm_callback() || !can_call_llm(currentTick)) {
    return std::nullopt;
  }
  
  std::string prompt = build_absence_prompt(archive, lastSeenTick, currentTick);
  std::string response = llm_callback_(prompt);
  last_llm_call_tick_ = currentTick;
  
  auto result = parse_reflection_response(response, currentTick);
  if (result) {
    reflection_history_.push_back(*result);
  }
  return result;
}

std::optional<LifeReview> ReflectionSystem::generate_life_review(
    const Archive& archive,
    const MemoryRing& memory,
    uint64_t currentTick) {
  
  if (!has_llm_callback() || !can_call_llm(currentTick)) {
    return std::nullopt;
  }
  
  uint64_t periodStart = life_reviews_.empty() ? 0 : life_reviews_.back().periodEndTick;
  std::string prompt = build_life_review_prompt(archive, memory, currentTick);
  std::string response = llm_callback_(prompt);
  last_llm_call_tick_ = currentTick;
  
  auto result = parse_life_review_response(response, currentTick);
  if (result) {
    result->periodStartTick = periodStart;
    result->periodEndTick = currentTick;
    life_reviews_.push_back(*result);
  }
  return result;
}

std::optional<std::string> ReflectionSystem::answer_about_past(
    const Archive& archive,
    const MemoryRing& memory,
    const std::string& question,
    uint64_t currentTick) {
  
  if (!has_llm_callback() || !can_call_llm(currentTick)) {
    return std::nullopt;
  }
  
  // Check if we have relevant events in our timeline
  std::string prompt = build_question_prompt(archive, memory, question, currentTick);
  std::string response = llm_callback_(prompt);
  last_llm_call_tick_ = currentTick;
  
  // Parse and check for honest uncertainty
  auto result = parse_reflection_response(response, currentTick);
  if (result) {
    if (result->honestUncertainty) {
      return "I don't have a record of that in my experience.";
    }
    return result->summary;
  }
  return std::nullopt;
}

bool ReflectionSystem::can_call_llm(uint64_t currentTick) const {
  if (last_llm_call_tick_ == 0) return true;
  return (currentTick - last_llm_call_tick_) >= min_ticks_between_llm_;
}

std::string ReflectionSystem::build_recent_events_prompt(
    const Archive& archive,
    const MemoryRing& memory,
    uint64_t currentTick,
    uint64_t lookbackTicks) const {
  (void)memory;
  
  std::ostringstream oss;
  oss << "You are an autonomous organism reflecting on your recent experience.\n";
  oss << "Current tick: " << currentTick << " (day " << (currentTick / 86400) << ")\n";
  oss << "Lookback window: " << lookbackTicks << " ticks\n\n";
  
  // Extract recent events
  auto events = extract_recent_events(archive, currentTick - lookbackTicks, currentTick);
  if (!events.empty()) {
    oss << "Recent event log:\n";
    for (const auto& e : events) {
      oss << "  " << e << "\n";
    }
  } else {
    oss << "Recent event log: (no recorded events in this window)\n";
  }
  
  // Add memory summary
  oss << "\nRecent memories (from episodic ring):\n";
  const auto& episodes = memory.episodes();
  size_t start = (episodes.size() > 5) ? episodes.size() - 5 : 0;
  for (size_t i = start; i < episodes.size(); ++i) {
    const auto& ep = episodes[i];
    oss << "  " << ep.t << ": " << static_cast<int>(ep.action) << " -> " 
        << static_cast<int>(ep.outcome) 
        << " (importance: " << ep.importance << ")\n";
  }
  
  oss << "\nProvide a reflection with:\n";
  oss << "1. A brief narrative summary (2-3 sentences)\n";
  oss << "2. Key events that stood out\n";
  oss << "3. Any insights or patterns you notice\n";
  oss << "4. Any changes in your behavior or understanding\n";
  oss << "Format as JSON with fields: summary, keyEvents[], insights[], changes[]\n";
  
  return oss.str();
}

std::string ReflectionSystem::build_absence_prompt(
    const Archive& archive,
    uint64_t lastSeenTick,
    uint64_t currentTick) const {
  
  std::ostringstream oss;
  oss << "You are an autonomous organism. A user you know has returned after being away.\n";
  oss << "They were last present at tick " << lastSeenTick 
      << " (day " << (lastSeenTick / 86400) << ")\n";
  oss << "Current tick: " << currentTick << " (day " << (currentTick / 86400) << ")\n";
  oss << "They ask: \"What happened while I was away?\"\n\n";
  
  auto events = extract_recent_events(archive, lastSeenTick, currentTick);
  if (!events.empty()) {
    oss << "Event log during their absence:\n";
    for (const auto& e : events) {
      oss << "  " << e << "\n";
    }
  } else {
    oss << "Event log: (no significant events recorded during absence)\n";
  }
  
  oss << "\nProvide a natural, conversational summary of what happened.\n";
  oss << "Focus on events that would be meaningful to someone who knows you.\n";
  oss << "Format as JSON with fields: summary, keyEvents[], insights[], changes[]\n";
  
  return oss.str();
}

std::string ReflectionSystem::build_life_review_prompt(
    const Archive& archive,
    const MemoryRing& memory,
    uint64_t currentTick) const {
  (void)memory;
  
  std::ostringstream oss;
  oss << "You are an autonomous organism conducting a life review.\n";
  oss << "Current tick: " << currentTick << " (day " << (currentTick / 86400) << ")\n";
  oss << "This is a periodic deep reflection on your existence so far.\n\n";
  
  auto events = extract_recent_events(archive, 0, currentTick);
  if (!events.empty()) {
    oss << "Full event log (key moments):\n";
    // Include only major events (every Nth to keep prompt bounded)
    for (size_t i = 0; i < events.size(); i += std::max<size_t>(1, events.size() / 20)) {
      oss << "  " << events[i] << "\n";
    }
  }
  
  oss << "\nProvide a life review covering:\n";
  oss << "1. Narrative arc of your existence so far\n";
  oss << "2. Major events that shaped you\n";
  oss << "3. Patterns in your drives and needs\n";
  oss << "4. Social interactions and relationships\n";
  oss << "5. Lessons learned\n";
  oss << "6. Honest self-assessment\n";
  oss << "Format as JSON with fields: narrative, majorEvents[], drivePatterns[], socialPatterns[], learnedLessons[], selfAssessment\n";
  
  return oss.str();
}

std::string ReflectionSystem::build_question_prompt(
    const Archive& archive,
    const MemoryRing& memory,
    const std::string& question,
    uint64_t currentTick) const {
  (void)memory;
  
  std::ostringstream oss;
  oss << "You are an autonomous organism answering a question about your past.\n";
  oss << "Current tick: " << currentTick << "\n";
  oss << "Question: \"" << question << "\"\n\n";
  
  // Check if we have relevant events
  // Simple keyword extraction from question
  std::string lowerQ = question;
  std::transform(lowerQ.begin(), lowerQ.end(), lowerQ.begin(), ::tolower);
  
  // Extract potential topics (simplified)
  std::vector<std::string> topics;
  if (lowerQ.find("wolf") != std::string::npos || lowerQ.find("predator") != std::string::npos) topics.push_back("predator");
  if (lowerQ.find("food") != std::string::npos || lowerQ.find("eat") != std::string::npos || lowerQ.find("forage") != std::string::npos) topics.push_back("food");
  if (lowerQ.find("water") != std::string::npos || lowerQ.find("drink") != std::string::npos) topics.push_back("water");
  if (lowerQ.find("sleep") != std::string::npos || lowerQ.find("rest") != std::string::npos) topics.push_back("sleep");
  if (lowerQ.find("shelter") != std::string::npos || lowerQ.find("build") != std::string::npos) topics.push_back("shelter");
  if (lowerQ.find("craft") != std::string::npos || lowerQ.find("tool") != std::string::npos) topics.push_back("craft");
  
  bool hasRelevantEvents = false;
  for (const auto& topic : topics) {
    if (has_event_about(archive, topic, 0, currentTick)) {
      hasRelevantEvents = true;
      break;
    }
  }
  
  if (hasRelevantEvents) {
    oss << "Relevant events from your timeline:\n";
    for (const auto& topic : topics) {
      auto events = extract_recent_events(archive, 0, currentTick);
      for (const auto& e : events) {
        std::string lowerE = e;
        std::transform(lowerE.begin(), lowerE.end(), lowerE.begin(), ::tolower);
        if (lowerE.find(topic) != std::string::npos) {
          oss << "  " << e << "\n";
        }
      }
    }
    oss << "\nAnswer based on your actual recorded experience.\n";
  } else {
    oss << "Your event log does not contain information about this topic.\n";
    oss << "\nIf you don't have a record of this, respond with honest uncertainty.\n";
  }
  
  oss << "Format as JSON with fields: summary, keyEvents[], insights[], changes[], honestUncertainty (boolean)\n";
  
  return oss.str();
}

std::optional<ReflectionResult> ReflectionSystem::parse_reflection_response(
    const std::string& response, uint64_t tick) const {
  
  ReflectionResult result;
  result.generatedAtTick = tick;
  
  // Simple JSON parsing (reusing the JSON parser from core)
  // For now, extract key fields with basic string parsing
  std::string lower = response;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  
  // Try to extract summary
  size_t sumPos = lower.find("\"summary\"");
  if (sumPos != std::string::npos) {
    size_t colon = response.find(':', sumPos);
    size_t quote1 = response.find('"', colon);
    size_t quote2 = response.find('"', quote1 + 1);
    if (quote1 != std::string::npos && quote2 != std::string::npos) {
      result.summary = response.substr(quote1 + 1, quote2 - quote1 - 1);
    }
  }
  
  // Extract honestUncertainty
  size_t uncPos = lower.find("\"honestuncertainty\"");
  if (uncPos != std::string::npos) {
    size_t colon = response.find(':', uncPos);
    if (colon != std::string::npos) {
      std::string after = response.substr(colon + 1);
      std::transform(after.begin(), after.end(), after.begin(), ::tolower);
      result.honestUncertainty = after.find("true") < after.find("false");
    }
  }
  
  // If no structured parse worked, use whole response as summary
  if (result.summary.empty()) {
    result.summary = response.substr(0, std::min<size_t>(response.size(), 500));
  }
  
  return result;
}

std::optional<LifeReview> ReflectionSystem::parse_life_review_response(
    const std::string& response, uint64_t tick) const {
  
  LifeReview result;
  result.periodEndTick = tick;
  
  std::string lower = response;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  
  // Extract narrative
  size_t narPos = lower.find("\"narrative\"");
  if (narPos != std::string::npos) {
    size_t colon = response.find(':', narPos);
    size_t quote1 = response.find('"', colon);
    size_t quote2 = response.find('"', quote1 + 1);
    if (quote1 != std::string::npos && quote2 != std::string::npos) {
      result.narrative = response.substr(quote1 + 1, quote2 - quote1 - 1);
    }
  }
  
  // Extract selfAssessment
  size_t saPos = lower.find("\"selfassessment\"");
  if (saPos != std::string::npos) {
    size_t colon = response.find(':', saPos);
    size_t quote1 = response.find('"', colon);
    size_t quote2 = response.find('"', quote1 + 1);
    if (quote1 != std::string::npos && quote2 != std::string::npos) {
      result.selfAssessment = response.substr(quote1 + 1, quote2 - quote1 - 1);
    }
  }
  
  if (result.narrative.empty()) {
    result.narrative = response.substr(0, std::min<size_t>(response.size(), 1000));
  }
  
  return result;
}

std::vector<std::string> ReflectionSystem::extract_recent_events(
    const Archive& archive,
    uint64_t startTick,
    uint64_t endTick) const {
  
  std::vector<std::string> events;
  // Archive would have a method to query events by time range
  // This is a placeholder - actual implementation depends on Archive interface
  // For now return empty - the Archive integration will be done in engine
  (void)archive; (void)startTick; (void)endTick;
  return events;
}

bool ReflectionSystem::has_event_about(const Archive& archive, const std::string& topic,
                                       uint64_t startTick, uint64_t endTick) const {
  (void)archive; (void)topic; (void)startTick; (void)endTick;
  return false;
}

void ReflectionSystem::serialize(struct BinaryWriter& w) const {
  w.u64(seed_);
  w.u64(last_llm_call_tick_);
  w.u64(min_ticks_between_llm_);
  w.u32(static_cast<uint32_t>(reflection_history_.size()));
  for (const auto& r : reflection_history_) r.serialize(w);
  w.u32(static_cast<uint32_t>(life_reviews_.size()));
  for (const auto& l : life_reviews_) l.serialize(w);
}

bool ReflectionSystem::deserialize(struct BinaryReader& r) {
  if (!r.u64(seed_) || !r.u64(last_llm_call_tick_) || !r.u64(min_ticks_between_llm_)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  reflection_history_.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!reflection_history_[i].deserialize(r)) return false;
  if (!r.u32(n)) return false;
  life_reviews_.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!life_reviews_[i].deserialize(r)) return false;
  return true;
}

} // namespace eidolon