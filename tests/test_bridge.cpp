#include "harness.hpp"

#include "llm/bridge.hpp"
#include "mind/memory.hpp"

using namespace eidolon;

namespace {
CognitiveSnapshot sampleSnapshot(bool awake = true, bool alive = true) {
  MemoryRing mem;
  for (int i = 0; i < 3; ++i) {
    Episode e;
    e.t = 100 + i;
    e.kind = EventKind::Forage;
    e.importance = 0.5;
    mem.add(e);
  }
  return makeSnapshot(12345, alive, awake, 60, 40, 30, 20, 10, 36.6, 100, 2, 14.5,
                      "rain", "plains", 9.5, mem);
}
} // namespace

TEST(snapshot_summary_includes_memories) {
  const CognitiveSnapshot s = sampleSnapshot();
  CHECK_EQ(s.day, 2);
  CHECK_EQ(s.hour, 14.5);
  CHECK_EQ(s.weather, std::string("rain"));
  CHECK(s.recentMemorySummary.find("foraged") != std::string::npos);
}

TEST(fallback_reply_dead) {
  const CognitiveSnapshot s = sampleSnapshot(false, false);
  CHECK(fallbackReply(s, "hello").find("no longer alive") != std::string::npos);
}

TEST(fallback_reply_asleep) {
  const CognitiveSnapshot s = sampleSnapshot(false);
  CHECK(fallbackReply(s, "are you there?").find("asleep") != std::string::npos);
}

TEST(fallback_reply_drives) {
  // Thirst dominates.
  MemoryRing mem;
  CognitiveSnapshot s = makeSnapshot(0, true, true, 60, 10, 80, 5, 5, 36.6, 100, 0, 12,
                                     "clear", "plains", 12, mem);
  CHECK(fallbackReply(s, "hi").find("thirsty") != std::string::npos);
  // Hungry.
  s = makeSnapshot(0, true, true, 60, 70, 10, 5, 5, 36.6, 100, 0, 12, "clear", "plains",
                   12, mem);
  CHECK(fallbackReply(s, "hi").find("hungry") != std::string::npos);
  // Tired.
  s = makeSnapshot(0, true, true, 60, 10, 10, 80, 5, 36.6, 100, 0, 12, "clear", "plains",
                   12, mem);
  CHECK(fallbackReply(s, "hi").find("tired") != std::string::npos);
  // Healthy: grounded state summary.
  s = makeSnapshot(0, true, true, 60, 10, 10, 10, 5, 36.6, 100, 0, 12, "clear", "plains",
                   12, mem);
  const std::string r = fallbackReply(s, "hi");
  CHECK(r.find("Weather is clear") != std::string::npos);
}

TEST(bridge_offline_parse_fails) {
  LLMBridge bridge(""); // disabled
  MemoryRing mem;
  CognitiveSnapshot s = makeSnapshot(0, true, true, 60, 10, 10, 10, 5, 36.6, 100, 0, 12,
                                     "clear", "plains", 12, mem);
  ParsedMessage parsed;
  std::string raw;
  CHECK(!bridge.parse("hello", s, parsed, raw));
  CHECK(!bridge.enabled());
}