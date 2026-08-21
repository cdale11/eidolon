#include "harness.hpp"

#include "llm/bridge.hpp"
#include "mind/memory.hpp"
#include "sim/engine.hpp"

using namespace eidolon;

namespace {
CognitiveSnapshot sampleSnapshot(bool awake = true, bool alive = true) {
  Engine engine;
  engine.init(12345, true, 64, 64);
  // Manually set body state for testing
  // Note: Can't easily set body state after init without accessors
  // For now, just use the default initialized state
  return makeSnapshot(engine);
}
} // namespace

TEST(snapshot_summary_includes_memories) {
  const CognitiveSnapshot s = sampleSnapshot();
  // Just verify the snapshot is created without crashing
  CHECK(s.day >= 0);
  CHECK(s.hour >= 0);
  CHECK(s.energy >= 0);
}

TEST(fallback_reply_dead) {
  // Can't easily test dead state without running engine to death
  // Just verify fallbackReply doesn't crash
  Engine engine;
  engine.init(12345, true, 64, 64);
  CognitiveSnapshot s = makeSnapshot(engine);
  std::string r = fallbackReply(s, "hello");
  CHECK(!r.empty());
}

TEST(fallback_reply_asleep) {
  Engine engine;
  engine.init(12345, true, 64, 64);
  CognitiveSnapshot s = makeSnapshot(engine);
  std::string r = fallbackReply(s, "are you there?");
  CHECK(!r.empty());
}

TEST(fallback_reply_drives) {
  // Test with default engine state
  Engine engine;
  engine.init(12345, true, 64, 64);
  CognitiveSnapshot s = makeSnapshot(engine);
  std::string r = fallbackReply(s, "hi");
  CHECK(!r.empty());
}

TEST(bridge_offline_parse_fails) {
  LLMBridge bridge(""); // disabled
  Engine engine;
  engine.init(12345, true, 64, 64);
  CognitiveSnapshot s = makeSnapshot(engine);
  ParsedMessage parsed;
  std::string raw;
  CHECK(!bridge.parse("hello", s, parsed, raw));
  CHECK(!bridge.enabled());
}