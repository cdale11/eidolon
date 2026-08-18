#include "llm/bridge.hpp"

#include <cstdio>
#include <cstring>

#include "httplib.h"

namespace eidolon {

namespace {
constexpr int kMaxReplyChars = 4096;

std::string episodeText(const Episode& e) {
  const char* what = "event";
  switch (e.kind) {
    case EventKind::Birth: what = "birth"; break;
    case EventKind::Forage: what = "foraged"; break;
    case EventKind::Drink: what = "drank water"; break;
    case EventKind::Sleep: what = "fell asleep"; break;
    case EventKind::Wake: what = "woke up"; break;
    case EventKind::Weather: what = "weather change"; break;
    case EventKind::NearDeath: what = "critical health"; break;
    case EventKind::Death: what = "death"; break;
  }
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s (t=%lld)", what, static_cast<long long>(e.t));
  return buf;
}

std::string trim(std::string s) {
  const auto issp = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  size_t a = 0, b = s.size();
  while (a < b && issp(s[a])) ++a;
  while (b > a && issp(s[b - 1])) --b;
  return s.substr(a, b - a);
}
} // namespace

CognitiveSnapshot makeSnapshot(int64_t simTime, bool alive, bool awake, double energy,
                               double hunger, double thirst, double fatigue,
                               double sleepPressure, double bodyTemp, double health,
                               int day, double hour, const char* weather,
                               const char* terrain, double ambientTempC,
                               const MemoryRing& memory) {
  CognitiveSnapshot s;
  s.simTime = simTime;
  s.alive = alive;
  s.awake = awake;
  s.energy = energy;
  s.hunger = hunger;
  s.thirst = thirst;
  s.fatigue = fatigue;
  s.sleepPressure = sleepPressure;
  s.bodyTemp = bodyTemp;
  s.health = health;
  s.day = day;
  s.hour = hour;
  s.weather = weather ? weather : "clear";
  s.terrain = terrain ? terrain : "plains";
  s.ambientTempC = ambientTempC;

  const auto& eps = memory.episodes();
  const size_t from = eps.size() > 6 ? eps.size() - 6 : 0;
  std::string summary;
  for (size_t i = from; i < eps.size(); ++i) {
    if (i > from) summary += "; ";
    summary += episodeText(eps[i]);
  }
  s.recentMemorySummary = summary;
  return s;
}

std::string fallbackReply(const CognitiveSnapshot& s, const std::string& userText) {
  (void)userText;
  char buf[512];
  if (!s.alive) return "I am no longer alive. My last memory is my death.";
  if (!s.awake) {
    std::snprintf(buf, sizeof(buf),
                  "I am asleep right now (sleep pressure %.0f). I cannot answer until I "
                  "wake.",
                  s.sleepPressure);
    return buf;
  }
  if (s.health < 20.0) {
    std::snprintf(buf, sizeof(buf), "I am in critical condition (health %.0f).",
                  s.health);
    return buf;
  }
  if (s.thirst > 55.0) {
    std::snprintf(buf, sizeof(buf), "I am very thirsty (%.0f) and need water.",
                  s.thirst);
    return buf;
  }
  if (s.hunger > 50.0) {
    std::snprintf(buf, sizeof(buf), "I am hungry (%.0f) and should look for berries.",
                  s.hunger);
    return buf;
  }
  if (s.fatigue > 60.0) {
    std::snprintf(buf, sizeof(buf), "I am tired (fatigue %.0f) and need to rest.",
                  s.fatigue);
    return buf;
  }
  std::snprintf(buf, sizeof(buf),
                "Day %d, hour %.1f. Weather is %s, %.1f C. Energy %.0f, health %.0f.",
                s.day, s.hour, s.weather.c_str(), s.ambientTempC, s.energy, s.health);
  return buf;
}

bool LLMBridge::post(const std::string& body, std::string& response) {
  if (endpoint_.empty()) return false;
  httplib::Client cli(endpoint_.c_str());
  cli.set_connection_timeout(timeoutMs_ / 1000, timeoutMs_ % 1000);
  cli.set_read_timeout(timeoutMs_ / 1000, timeoutMs_ % 1000);
  httplib::Headers headers = {{"Content-Type", "application/json"}};
  auto res = cli.Post("/chat/completions", headers, body, "application/json");
  if (!res || res->status != 200) return false;
  response = res->body;
  return true;
}

bool LLMBridge::chatComplete(const JsonValue& messages, int maxTokens, JsonValue& out) {
  ++calls_;
  JsonValue req = JsonValue::makeObject();
  req.setString("model", "local");
  req.set("messages", messages);
  req.setNumber("max_tokens", maxTokens);
  req.setNumber("temperature", 0.7);
  std::string respBody;
  if (!post(req.dump(), respBody)) {
    ++failures_;
    return false;
  }
  JsonValue parsed;
  if (!jsonParse(respBody, parsed)) {
    ++failures_;
    return false;
  }
  const JsonValue* choices = parsed.find("choices");
  if (!choices || choices->type() != JsonValue::Type::Array ||
      choices->asArray().empty()) {
    ++failures_;
    return false;
  }
  out = choices->asArray()[0];
  return true;
}

bool LLMBridge::parse(const std::string& userText, const CognitiveSnapshot& s,
                      ParsedMessage& out, std::string& raw) {
  JsonValue sys = JsonValue::makeObject();
  sys.setString("role", "system");
  sys.setString(
      "content",
      "You classify one user message for an autonomous organism. Return ONLY a JSON "
      "object with keys: intent (string: greet|question|request|smalltalk|other), "
      "topic (string, what the message is about, or empty), tone "
      "(string: neutral|warm|concerned|playful|other), references_memory (bool: true "
      "only if the user asks about past events the organism may remember). Do not "
      "invent events.");
  JsonValue user = JsonValue::makeObject();
  user.setString("role", "user");
  JsonValue payload = JsonValue::makeObject();
  payload.setString("time", std::to_string(s.simTime));
  payload.setString("message", userText);
  user.setString("content", payload.dump());
  JsonValue msgs = JsonValue::makeArray();
  msgs.push(std::move(sys));
  msgs.push(std::move(user));

  JsonValue choice;
  if (!chatComplete(msgs, 200, choice)) return false;
  const JsonValue* msg = choice.find("message");
  if (!msg) return false;
  raw = trim(msg->str("content"));
  // Strip code fences if the model wraps the JSON.
  if (raw.size() >= 2 && raw.front() == '`') {
    size_t a = raw.find('{');
    size_t b = raw.rfind('}');
    if (a != std::string::npos && b != std::string::npos && b > a) {
      raw = raw.substr(a, b - a + 1);
    }
  }
  JsonValue parsed;
  if (!jsonParse(raw, parsed)) return false;
  out.intent = parsed.str("intent", "other");
  out.topic = parsed.str("topic");
  out.tone = parsed.str("tone", "neutral");
  out.referencesMemory = parsed.boolean("references_memory", false);
  return true;
}

bool LLMBridge::respond(const std::string& userText, const CognitiveSnapshot& s,
                        const ParsedMessage& parsed, std::string& reply,
                        std::string& raw) {
  JsonValue sys = JsonValue::makeObject();
  sys.setString("role", "system");
  sys.setString(
      "content",
      "You are the language interface of an autonomous simulated organism. You may "
      "only speak from the provided state snapshot. Never invent events, memories, "
      "goals or relationships. If asked about something not in the snapshot or "
      "memories, say you do not remember it. Keep replies short (1-3 sentences).");
  JsonValue user = JsonValue::makeObject();
  user.setString("role", "user");
  JsonValue payload = JsonValue::makeObject();
  payload.setString("state",
                    "time=" + std::to_string(s.simTime) +
                        " day=" + std::to_string(s.day) +
                        " hour=" + std::to_string(s.hour) +
                        " awake=" + (s.awake ? "yes" : "no") +
                        " energy=" + std::to_string(static_cast<int>(s.energy)) +
                        " hunger=" + std::to_string(static_cast<int>(s.hunger)) +
                        " thirst=" + std::to_string(static_cast<int>(s.thirst)) +
                        " fatigue=" + std::to_string(static_cast<int>(s.fatigue)) +
                        " health=" + std::to_string(static_cast<int>(s.health)) +
                        " weather=" + s.weather + " temp=" +
                        std::to_string(s.ambientTempC) +
                        " terrain=" + s.terrain +
                        " recent_memories=[" + s.recentMemorySummary + "]");
  payload.setString("user_intent", parsed.intent);
  payload.setString("user_topic", parsed.topic);
  payload.setString("message", userText);
  user.setString("content", payload.dump());
  JsonValue msgs = JsonValue::makeArray();
  msgs.push(std::move(sys));
  msgs.push(std::move(user));

  JsonValue choice;
  if (!chatComplete(msgs, 1024, choice)) return false;
  const JsonValue* msg = choice.find("message");
  if (!msg) return false;
  raw = trim(msg->str("content"));
  reply = raw.substr(0, kMaxReplyChars);
  return !reply.empty();
}

} // namespace eidolon