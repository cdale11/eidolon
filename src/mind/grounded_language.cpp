#include "mind/grounded_language.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace eidolon {

class Archive;

GroundedLanguage::GroundedLanguage(uint64_t seed) : seed_(seed), rng_(seed) {
  init_grammars();
}

void GroundedLanguage::init_grammars() {
  // Use the predefined utterance grammar from world/grammar.cpp
  utteranceEngine_ = GrammarEngine(makeUtteranceGrammar());
  eventEngine_ = GrammarEngine(makeEventGrammar());
  
  // Custom summary grammar for daily summaries
  Grammar summaryGrammar;
  summaryGrammar.startSymbol = "ROOT";
  summaryGrammar.productions = {
    {"ROOT", {GrammarSymbol::terminal("Today I "), GrammarSymbol::nonTerminal("ACTIVITY_SEQUENCE"), GrammarSymbol::terminal(".")}, 1.0f},
    
    {"ACTIVITY_SEQUENCE", {GrammarSymbol::nonTerminal("ACTIVITY")}, 0.3f},
    {"ACTIVITY_SEQUENCE", {GrammarSymbol::nonTerminal("ACTIVITY"), GrammarSymbol::terminal(", "), GrammarSymbol::nonTerminal("ACTIVITY")}, 0.4f},
    {"ACTIVITY_SEQUENCE", {GrammarSymbol::nonTerminal("ACTIVITY"), GrammarSymbol::terminal(", "), GrammarSymbol::nonTerminal("ACTIVITY"), GrammarSymbol::terminal(", and "), GrammarSymbol::nonTerminal("ACTIVITY")}, 0.3f},
    
    {"ACTIVITY", {GrammarSymbol::terminal("foraged for "), GrammarSymbol::nonTerminal("FOOD")}, 0.25f},
    {"ACTIVITY", {GrammarSymbol::terminal("drank from "), GrammarSymbol::nonTerminal("WATER_SOURCE")}, 0.2f},
    {"ACTIVITY", {GrammarSymbol::terminal("built "), GrammarSymbol::nonTerminal("STRUCTURE")}, 0.15f},
    {"ACTIVITY", {GrammarSymbol::terminal("crafted "), GrammarSymbol::nonTerminal("TOOL")}, 0.1f},
    {"ACTIVITY", {GrammarSymbol::terminal("slept at "), GrammarSymbol::nonTerminal("LOCATION")}, 0.15f},
    {"ACTIVITY", {GrammarSymbol::terminal("explored "), GrammarSymbol::nonTerminal("AREA")}, 0.1f},
    {"ACTIVITY", {GrammarSymbol::terminal("encountered "), GrammarSymbol::nonTerminal("WILDLIFE")}, 0.05f},
    
    {"FOOD", {GrammarSymbol::terminal("berries")}, 0.5f},
    {"FOOD", {GrammarSymbol::terminal("plants")}, 0.3f},
    {"FOOD", {GrammarSymbol::terminal("food")}, 0.2f},
    
    {"WATER_SOURCE", {GrammarSymbol::terminal("the river")}, 0.4f},
    {"WATER_SOURCE", {GrammarSymbol::terminal("a spring")}, 0.3f},
    {"WATER_SOURCE", {GrammarSymbol::terminal("a lake")}, 0.2f},
    {"WATER_SOURCE", {GrammarSymbol::terminal("a water source")}, 0.1f},
    
    {"STRUCTURE", {GrammarSymbol::terminal("a shelter")}, 0.4f},
    {"STRUCTURE", {GrammarSymbol::terminal("a campfire")}, 0.3f},
    {"STRUCTURE", {GrammarSymbol::terminal("a wall")}, 0.2f},
    {"STRUCTURE", {GrammarSymbol::terminal("a structure")}, 0.1f},
    
    {"TOOL", {GrammarSymbol::terminal("a stone knife")}, 0.3f},
    {"TOOL", {GrammarSymbol::terminal("a stone axe")}, 0.3f},
    {"TOOL", {GrammarSymbol::terminal("a spear")}, 0.2f},
    {"TOOL", {GrammarSymbol::terminal("a tool")}, 0.2f},
    
    {"LOCATION", {GrammarSymbol::terminal("home")}, 0.4f},
    {"LOCATION", {GrammarSymbol::terminal("my shelter")}, 0.3f},
    {"LOCATION", {GrammarSymbol::terminal("a safe spot")}, 0.2f},
    {"LOCATION", {GrammarSymbol::terminal("the cave")}, 0.1f},
    
    {"AREA", {GrammarSymbol::terminal("the forest")}, 0.3f},
    {"AREA", {GrammarSymbol::terminal("the valley")}, 0.2f},
    {"AREA", {GrammarSymbol::terminal("new territory")}, 0.2f},
    {"AREA", {GrammarSymbol::terminal("the ruins")}, 0.2f},
    {"AREA", {GrammarSymbol::terminal("the area")}, 0.1f},
    
    {"WILDLIFE", {GrammarSymbol::terminal("a wolf")}, 0.3f},
    {"WILDLIFE", {GrammarSymbol::terminal("a bear")}, 0.2f},
    {"WILDLIFE", {GrammarSymbol::terminal("a rabbit")}, 0.2f},
    {"WILDLIFE", {GrammarSymbol::terminal("a deer")}, 0.2f},
    {"WILDLIFE", {GrammarSymbol::terminal("wildlife")}, 0.1f},
  };
  summaryEngine_ = GrammarEngine(summaryGrammar);
}

GroundedUtterance GroundedLanguage::make_uncertainty_response(uint64_t tick) const {
  GroundedUtterance u;
  u.text = "I don't have a clear memory of that.";
  u.honestUncertainty = true;
  u.generatedAtTick = tick;
  return u;
}

std::vector<GroundedLanguage::ExtractedEvent> GroundedLanguage::extract_events(
    const Archive& /*archive*/,
    uint64_t /*startTick*/,
    uint64_t /*endTick*/) const {
  // In a full implementation, this would query the Archive for events in the time range
  // For now, return empty - the engine will integrate this
  std::vector<ExtractedEvent> events;
  return events;
}

std::string GroundedLanguage::event_kind_to_terminal(EventKind kind) const {
  switch (kind) {
    case EventKind::Forage: return "foraged";
    case EventKind::Drink: return "drank";
    case EventKind::Sleep: return "slept";
    case EventKind::Wake: return "woke";
    case EventKind::Attack: return "was attacked";
    case EventKind::Birth: return "was born";
    case EventKind::Death: return "died";
    case EventKind::Weather: return "experienced weather";
    case EventKind::NearDeath: return "nearly died";
    default: return "did something";
  }
}

std::string GroundedLanguage::action_to_terminal(uint8_t action) const {
  switch (action) {
    case 0: return "foraged";   // Forage
    case 1: return "drank";     // Drink
    case 2: return "rested";    // Rest
    case 3: return "wandered";  // Wander
    case 4: return "observed";  // Observe
    case 5: return "fled";      // Flee
    default: return "acted";
  }
}

std::string GroundedLanguage::outcome_to_terminal(Outcome outcome) const {
  switch (outcome) {
    case Outcome::Success: return "successfully";
    case Outcome::Partial: return "partially";
    case Outcome::Failure: return "unsuccessfully";
    case Outcome::Interrupted: return "but was interrupted";
    default: return "";
  }
}

std::vector<GroundedLanguage::ExtractedEvent> GroundedLanguage::select_relevant_events(
    const std::vector<ExtractedEvent>& allEvents,
    const std::string& question) const {
  
  std::string lowerQ = question;
  std::transform(lowerQ.begin(), lowerQ.end(), lowerQ.begin(), ::tolower);
  
  std::vector<std::string> keywords;
  if (lowerQ.find("food") != std::string::npos || lowerQ.find("eat") != std::string::npos || lowerQ.find("forage") != std::string::npos) keywords.push_back("food");
  if (lowerQ.find("water") != std::string::npos || lowerQ.find("drink") != std::string::npos) keywords.push_back("water");
  if (lowerQ.find("sleep") != std::string::npos || lowerQ.find("rest") != std::string::npos) keywords.push_back("rest");
  if (lowerQ.find("shelter") != std::string::npos || lowerQ.find("build") != std::string::npos) keywords.push_back("shelter");
  if (lowerQ.find("craft") != std::string::npos || lowerQ.find("tool") != std::string::npos) keywords.push_back("tool");
  if (lowerQ.find("wolf") != std::string::npos || lowerQ.find("predator") != std::string::npos || lowerQ.find("attack") != std::string::npos) keywords.push_back("predator");
  if (lowerQ.find("explore") != std::string::npos || lowerQ.find("wander") != std::string::npos) keywords.push_back("explore");
  
  if (keywords.empty()) return allEvents;
  
  std::vector<ExtractedEvent> relevant;
  for (const auto& e : allEvents) {
    std::string eventDesc = event_kind_to_terminal(e.kind);
    std::transform(eventDesc.begin(), eventDesc.end(), eventDesc.begin(), ::tolower);
    
    for (const auto& kw : keywords) {
      if (eventDesc.find(kw) != std::string::npos) {
        relevant.push_back(e);
        break;
      }
    }
  }
  
  return relevant.empty() ? allEvents : relevant;
}

std::string GroundedLanguage::build_summary_from_events(
    const std::vector<ExtractedEvent>& events) const {
  
  if (events.empty()) {
    return "Today I rested and conserved energy.";
  }
  
  // Use the summary grammar to generate a summary
  auto derivation = summaryEngine_.deriveStochastic(rng_);
  std::string result = derivation.result;
  
  // Replace placeholders with actual event data where possible
  // For now, return the grammar-generated result
  return result;
}

std::string GroundedLanguage::build_utterance_from_template(
    const std::vector<ExtractedEvent>& /*events*/) const {
  
  // For now, use the utterance grammar directly
  auto derivation = utteranceEngine_.deriveStochastic(rng_);
  return derivation.result;
}

std::optional<GroundedUtterance> GroundedLanguage::answer_what_did_you_do(
    const Archive& archive,
    const MemoryRing& memory,
    uint64_t currentTick) {
  
  uint64_t dayStart = (currentTick / 86400) * 86400;
  auto events = extract_events(archive, dayStart, currentTick);
  
  // Also get recent memories
  const auto& episodes = memory.episodes();
  size_t start = (episodes.size() > 10) ? episodes.size() - 10 : 0;
  auto recentMemories = std::vector<const Episode*>();
  for (size_t i = start; i < episodes.size(); ++i) {
    recentMemories.push_back(&episodes[i]);
  }
  
  if (events.empty() && recentMemories.empty()) {
    return make_uncertainty_response(currentTick);
  }
  
  GroundedUtterance u;
  u.generatedAtTick = currentTick;
  
  // Build a grounded response using the grammar
  std::string summary = build_summary_from_events(events);
  
  // Also generate via utterance grammar for variety
  auto utterance = build_utterance_from_template(events);
  
  // Combine: use the summary as primary, utterance as alternative
  if (!summary.empty() && summary != "Today I .") {
    u.text = summary;
  } else {
    u.text = utterance;
  }
  
  // Record source events
  for (const auto& e : events) {
    u.sourceEvents.push_back(event_kind_to_terminal(e.kind) + " at tick " + std::to_string(e.tick));
  }
  
  return u;
}

std::optional<DailySummary> GroundedLanguage::generate_daily_summary(
    const Archive& archive,
    const MemoryRing& memory,
    uint64_t currentTick) {
  
  uint64_t dayStart = (currentTick / 86400) * 86400;
  auto events = extract_events(archive, dayStart, currentTick);
  
  DailySummary ds;
  ds.day = currentTick / 86400;
  ds.generatedAtTick = currentTick;
  
  ds.summary = build_summary_from_events(events);
  
  for (const auto& e : events) {
    ds.keyEvents.push_back(event_kind_to_terminal(e.kind) + " at " + std::to_string(e.tick));
  }
  
  // Extract drives from memories
  const auto& episodes = memory.episodes();
  size_t start = (episodes.size() > 5) ? episodes.size() - 5 : 0;
  for (size_t i = start; i < episodes.size(); ++i) {
    const auto& ep = episodes[i];
    if (ep.importance > 0.3) {
      ds.drives.push_back(action_to_terminal(ep.action) + " (importance: " + std::to_string(ep.importance) + ")");
    }
  }
  
  return ds;
}

std::optional<GroundedUtterance> GroundedLanguage::answer_about_past(
    const Archive& archive,
    const MemoryRing& /*memory*/,
    const std::string& question,
    uint64_t currentTick) {
  
  auto allEvents = extract_events(archive, 0, currentTick);
  auto relevantEvents = select_relevant_events(allEvents, question);
  
  // Check if we have any relevant events
  if (relevantEvents.empty()) {
    return make_uncertainty_response(currentTick);
  }
  
  GroundedUtterance u;
  u.generatedAtTick = currentTick;
  
  // Build response from relevant events
  std::ostringstream oss;
  oss << "I remember ";
  
  if (relevantEvents.size() == 1) {
    const auto& e = relevantEvents[0];
    oss << event_kind_to_terminal(e.kind) << " " << outcome_to_terminal(e.outcome);
    if (e.tick > 0) {
      oss << " on day " << (e.tick / 86400);
    }
  } else {
    oss << relevantEvents.size() << " times when I ";
    // Group by kind
    std::unordered_map<EventKind, int> kindCounts;
    for (const auto& e : relevantEvents) kindCounts[e.kind]++;
    
    bool first = true;
    for (const auto& [kind, count] : kindCounts) {
      if (!first) oss << ", ";
      first = false;
      oss << event_kind_to_terminal(kind) << " " << count << " time" << (count > 1 ? "s" : "");
    }
  }
  oss << ".";
  
  u.text = oss.str();
  
  for (const auto& e : relevantEvents) {
    u.sourceEvents.push_back(event_kind_to_terminal(e.kind) + " at tick " + std::to_string(e.tick));
  }
  
  return u;
}

std::optional<GroundedUtterance> GroundedLanguage::generate_greeting(
    const Archive& /*archive*/,
    const MemoryRing& memory,
    uint64_t currentTick) {
  
  // Generate a greeting using the utterance grammar
  auto derivation = utteranceEngine_.deriveStochastic(rng_);
  std::string greeting = derivation.result;
  
  // Add current state
  const auto& episodes = memory.episodes();
  size_t start = (episodes.size() > 3) ? episodes.size() - 3 : 0;
  if (start < episodes.size()) {
    const auto& ep = episodes[start];
    greeting += " I've been " + action_to_terminal(ep.action) + " lately.";
  }
  
  GroundedUtterance u;
  u.text = greeting;
  u.generatedAtTick = currentTick;
  return u;
}

std::optional<GroundedUtterance> GroundedLanguage::generate_need_statement(
    const Archive& /*archive*/,
    const MemoryRing& memory,
    uint64_t currentTick) {
  
  // Use the utterance grammar's STATEMENT -> "I need " + NEED
  auto derivation = utteranceEngine_.deriveStochastic(rng_);
  std::string statement = derivation.result;
  
  // Make it more grounded by checking actual state from recent memories
  const auto& episodes = memory.episodes();
  size_t start = (episodes.size() > 5) ? episodes.size() - 5 : 0;
  (void)start; // will be used below
  bool hungry = false, thirsty = false, tired = false;
  
  for (size_t i = start; i < episodes.size(); ++i) {
    const auto& ep = episodes[i];
    if (ep.action == 0) hungry = true;      // Forage
    if (ep.action == 1) thirsty = true;     // Drink
    if (ep.action == 2) tired = true;       // Rest
  }
  (void)hungry; (void)thirsty; (void)tired; // used for future grounded responses
  
  GroundedUtterance u;
  u.text = statement;
  u.generatedAtTick = currentTick;
  return u;
}

std::optional<GroundedUtterance> GroundedLanguage::generate_observation(
    const Archive& /*archive*/,
    const MemoryRing& /*memory*/,
    uint64_t currentTick) {
  
  // Use the utterance grammar's STATEMENT -> "The " + OBSERVATION
  auto derivation = utteranceEngine_.deriveStochastic(rng_);
  std::string observation = derivation.result;
  
  GroundedUtterance u;
  u.text = observation;
  u.generatedAtTick = currentTick;
  return u;
}

bool GroundedLanguage::has_events_about(const Archive& archive, const std::string& topic,
                                        uint64_t startTick, uint64_t endTick) const {
  auto events = extract_events(archive, startTick, endTick);
  auto relevant = select_relevant_events(events, topic);
  return !relevant.empty();
}

void GroundedUtterance::serialize(struct BinaryWriter& w) const {
  w.u64(generatedAtTick);
  w.str(text);
  w.u32(static_cast<uint32_t>(sourceEvents.size()));
  for (const auto& e : sourceEvents) w.str(e);
  w.u32(static_cast<uint32_t>(templatesUsed.size()));
  for (const auto& t : templatesUsed) w.str(t);
  w.u8(honestUncertainty ? 1 : 0);
}

bool GroundedUtterance::deserialize(struct BinaryReader& r) {
  if (!r.u64(generatedAtTick) || !r.str(text)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  sourceEvents.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(sourceEvents[i])) return false;
  if (!r.u32(n)) return false;
  templatesUsed.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(templatesUsed[i])) return false;
  uint8_t u;
  if (!r.u8(u)) return false;
  honestUncertainty = (u != 0);
  return true;
}

void DailySummary::serialize(struct BinaryWriter& w) const {
  w.u64(day);
  w.str(summary);
  w.u32(static_cast<uint32_t>(keyEvents.size()));
  for (const auto& e : keyEvents) w.str(e);
  w.u32(static_cast<uint32_t>(drives.size()));
  for (const auto& d : drives) w.str(d);
  w.u64(generatedAtTick);
}

bool DailySummary::deserialize(struct BinaryReader& r) {
  if (!r.u64(day) || !r.str(summary)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  keyEvents.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(keyEvents[i])) return false;
  if (!r.u32(n)) return false;
  drives.resize(n);
  for (uint32_t i = 0; i < n; ++i) if (!r.str(drives[i])) return false;
  if (!r.u64(generatedAtTick)) return false;
  return true;
}

void GroundedLanguage::serialize(struct BinaryWriter& w) const {
  w.u64(seed_);
  // Note: GrammarEngine state is not serialized (rebuilt from seed)
}

bool GroundedLanguage::deserialize(struct BinaryReader& r) {
  if (!r.u64(seed_)) return false;
  rng_ = Rng(seed_);
  init_grammars();
  return true;
}

} // namespace eidolon