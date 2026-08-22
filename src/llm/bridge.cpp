#include "llm/bridge.hpp"

#include <cstdio>
#include <cstring>

#include "httplib.h"
#include "sim/engine.hpp"
#include "mind/goal_emergence.hpp"
#include "mind/user_model.hpp"
#include "mind/wildlife_social.hpp"
#include "body/physiology.hpp"
#include "world/world.hpp"

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
    case EventKind::Attack: what = "attacked by a predator"; break;
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

// Reasoning-enabled models (DeepSeek, Nemotron, …) may emit a long chain-of-thought in
// `content` (or a separate `reasoning_content` field). For structured outputs we only
// care about the final answer, so extract the LAST balanced JSON object in the text —
// reasoning precedes the answer, and the answer is typically the final structured
// payload. Returns "" when no balanced object exists.
std::string extractJsonObject(const std::string& s) {
  std::string last;
  size_t pos = 0;
  while ((pos = s.find('{', pos)) != std::string::npos) {
    int depth = 0;
    bool inStr = false;
    size_t end = std::string::npos;
    for (size_t i = pos; i < s.size(); ++i) {
      const char c = s[i];
      if (inStr) {
        if (c == '\\') { ++i; }
        else if (c == '"') { inStr = false; }
        continue;
      }
      if (c == '"') inStr = true;
      else if (c == '{') ++depth;
      else if (c == '}') {
        if (--depth == 0) {
          end = i;
          break;
        }
      }
    }
    if (end != std::string::npos) last = s.substr(pos, end - pos + 1);
    pos = (end == std::string::npos) ? pos + 1 : end + 1;
  }
  return last;
}

// The final answer of a reasoning model lives in `message.content`. Some servers put the
// chain-of-thought in `reasoning_content` and leave `content` short, or null when the
// request ran out of tokens. Returns the content string (possibly empty).
std::string assistantContent(const JsonValue& message) {
  const JsonValue* c = message.find("content");
  if (!c || c->type() != JsonValue::Type::String) return "";
  return trim(c->asString());
}

// Helper: format action name
const char* actionName(Action a) {
  switch (a) {
    case Action::Wander: return "wander";
    case Action::Rest: return "rest";
    case Action::Sleep: return "sleep";
    case Action::Observe: return "observe";
    case Action::Forage: return "forage";
    case Action::Drink: return "drink";
    case Action::Flee: return "flee";
    case Action::Farm: return "farm";
    case Action::Cook: return "cook";
    case Action::Craft: return "craft";
    case Action::Build: return "build";
    case Action::CollectWater: return "collect water";
    case Action::Preserve: return "preserve";
  }
  return "unknown";
}

// Helper: format goal name
const char* goalName(GoalType g) {
  switch (g) {
    case GoalType::Survive: return "survive";
    case GoalType::FindFood: return "find food";
    case GoalType::FindWater: return "find water";
    case GoalType::Rest: return "rest";
    case GoalType::FleeThreat: return "flee threat";
    case GoalType::Explore: return "explore";
    case GoalType::BuildShelter: return "build shelter";
    case GoalType::CraftTool: return "craft tool";
    default: return "none";
  }
}

// Helper: format plant type
const char* plantTypeName(PlantType t) {
  switch (t) {
    case PlantType::Edible: return "edible";
    case PlantType::Toxic: return "toxic";
    case PlantType::Medicinal: return "medicinal";
    case PlantType::Wood: return "wood";
    default: return "unknown";
  }
}

// Time-of-day greeting slot for the fallback reply. The LLM is free to vary the wording;
// this is only used when the LLM is unavailable, so it must stay grounded and short.
const char* hSlot(double hour) {
  if (hour < 5.0) return "night";
  if (hour < 12.0) return "morning";
  if (hour < 17.0) return "afternoon";
  if (hour < 21.0) return "evening";
  return "night";
}

} // namespace

CognitiveSnapshot makeSnapshot(const Engine& engine) {
  CognitiveSnapshot s;
  const auto& b = engine.body();
  const auto& w = engine.world().weather();
  const auto& mem = engine.memory();
  const Vec2i pos = engine.world().organismPos();
  const auto& grid = engine.world().grid();

  // Core identity & time
  s.simTime = engine.clock().now();
  s.alive = engine.isAlive();
  s.awake = !b.isSleeping();
  s.day = static_cast<int>(engine.clock().day());
  s.hour = engine.clock().hourOfDay();

  // Physiology
  s.energy = b.energy();
  s.hunger = b.hunger();
  s.thirst = b.thirst();
  s.fatigue = b.fatigue();
  s.sleepPressure = b.sleepPressure();
  s.bodyTemp = b.bodyTemp();
  s.health = b.health();
  s.pain = b.pain();

  // Position & environment
  s.posX = pos.x;
  s.posY = pos.y;
  s.terrain = std::to_string(static_cast<int>(grid.at(pos.x, pos.y)));
  s.weather = w.describe();
  s.ambientTempC = w.ambientTempC(engine.clock());

  // Nearby threats (within sight radius = 8 tiles)
  const int sightRadius = 8;
  s.predatorsNear = engine.world().predatorCount(pos, sightRadius);
  const WildlifeAgent* predator = engine.world().nearestPredator(pos, sightRadius);
  s.predatorDist = predator ? distCheb(predator->pos, pos) : -1;
  s.preyNear = engine.world().preyCount(pos, sightRadius);

  // Nearby resources
  const WaterSource* water = engine.world().nearestWaterSource(pos, sightRadius);
  s.waterDist = water ? distCheb(water->pos, pos) : -1;
  const Plant* plant = engine.world().nearestEdiblePlant(pos, sightRadius);
  s.plantDist = plant ? distCheb(plant->pos, pos) : -1;
  s.plantType = plant ? plantTypeName(plant->type) : "";

  // Inventory & waterskin
  s.waterCarried = b.waterCarried();
  s.waterCapacity = b.waterCapacity();

  // Current action (from last tick's decision). The engine persists this in its
  // snapshot (v10+) so a resumed run continues with the correct chat-grounding
  // action; see src/sim/engine.cpp::serializeState.
  s.currentAction = actionName(engine.lastAction());

  // Threat level
  s.threatLevel = engine.learn().threatEstimate();

  // Active goals (from GoalEmergence)
  const auto& goals = engine.goalEmergence().active_goals();
  for (const auto& goal : goals) {
    if (goal.priority > 0.1f) { // Only significant goals
      s.activeGoals.push_back(goalName(goal.type));
    }
  }

  // Personality & drives (summary)
  const auto& latent = engine.learn().personality();
  const auto& drives = engine.learn().driveWeights();
  
  // Personality summary from latent vector
  char persBuf[256];
  std::snprintf(persBuf, sizeof(persBuf),
    "rewardSens=%.2f threatSens=%.2f noveltySens=%.2f socialSens=%.2f impulsivity=%.2f persistence=%.2f",
    latent.value(PersonalityLatent::kRewardSensitivity),
    latent.value(PersonalityLatent::kThreatSensitivity),
    latent.value(PersonalityLatent::kNoveltySensitivity),
    latent.value(PersonalityLatent::kSocialSensitivity),
    latent.value(PersonalityLatent::kImpulsivity),
    latent.value(PersonalityLatent::kPersistence));
  s.personalitySummary = persBuf;

  // Drive summary
  char driveBuf[256];
  std::snprintf(driveBuf, sizeof(driveBuf),
    "hunger=%.2f thirst=%.2f rest=%.2f energy=%.2f curiosity=%.2f",
    drives.hunger, drives.thirst, drives.rest, drives.energy, drives.curiosity);
  s.driveSummary = driveBuf;

  // Social - User model
  const auto& userModel = engine.userModel();
  s.userTrust = userModel.trust;
  s.userFamiliarity = userModel.familiarity;
  s.userAffection = userModel.affection;
  s.userExpectsReturn = false; // placeholder

  // Social - Wildlife
  char wildBuf[256];
  // Find nearest wildlife agent for summary
  const WildlifeAgent* nearestWolf = engine.world().nearestPredator(pos, 16);
  if (nearestWolf) {
    const auto* ws = engine.wildlifeSocial().get_profile(nearestWolf->id);
    if (ws) {
      std::snprintf(wildBuf, sizeof(wildBuf), "wolf familiar=%.2f fear=%.2f threat=%.2f",
                    ws->familiarity, ws->fear, ws->threat_level);
    } else {
      std::snprintf(wildBuf, sizeof(wildBuf), "wolf no social profile");
    }
  } else {
    std::snprintf(wildBuf, sizeof(wildBuf), "no nearby wildlife tracked");
  }
  s.wildlifeSummary = wildBuf;

  // Recent memories (last 6 episodes, compact)
  const auto& eps = mem.episodes();
  const size_t from = eps.size() > 6 ? eps.size() - 6 : 0;
  std::string summary;
  for (size_t i = from; i < eps.size(); ++i) {
    if (i > from) summary += "; ";
    summary += episodeText(eps[i]);
  }
  s.recentMemorySummary = summary;

  // Skills/competence
  // TODO: Add skill summary when skill system exposes it
  s.skillSummary = "forage=0.8 drink=0.6 craft=0.1"; // placeholder

  // --- Circadian & physiological tone (DESIGN future direction: time-of-day awareness) ---
  // All deterministic, derived from the snapshot fields above. No new state, no extra LLM.
  const double h = s.hour;
  if (!s.awake) {
    s.phaseOfDay = "asleep";
  } else if (h < 5.0) {
    s.phaseOfDay = "deep_night";
  } else if (h < 7.5) {
    s.phaseOfDay = "dawn";
  } else if (h < 18.0) {
    s.phaseOfDay = "day";
  } else if (h < 20.5) {
    s.phaseOfDay = "dusk";
  } else {
    s.phaseOfDay = "night";
  }
  if (h < 4.0) s.timeOfDayPhrase = "deep night";
  else if (h < 6.0) s.timeOfDayPhrase = "just before dawn";
  else if (h < 8.0) s.timeOfDayPhrase = "early morning";
  else if (h < 11.0) s.timeOfDayPhrase = "mid-morning";
  else if (h < 14.0) s.timeOfDayPhrase = "midday";
  else if (h < 17.0) s.timeOfDayPhrase = "afternoon";
  else if (h < 19.0) s.timeOfDayPhrase = "late afternoon";
  else if (h < 21.0) s.timeOfDayPhrase = "evening";
  else s.timeOfDayPhrase = "night";
  switch (w.season()) {
    case 0: s.seasonName = "spring"; break;
    case 1: s.seasonName = "summer"; break;
    case 2: s.seasonName = "autumn"; break;
    case 3: s.seasonName = "winter"; break;
    default: s.seasonName = "spring"; break;
  }

  // Physiological state — the salient aspect of how the body feels right now.
  if (!s.awake) {
    s.physiologicalState = "asleep";
  } else if (b.sick()) {
    s.physiologicalState = "sick";
  } else if (s.pain > 40.0) {
    s.physiologicalState = "pained";
  } else if (s.sleepPressure >= 85.0) {
    s.physiologicalState = "exhausted";
  } else if (s.sleepPressure >= 55.0 || s.fatigue >= 60.0) {
    s.physiologicalState = "tired";
  } else if (s.sleepPressure >= 30.0 || s.fatigue >= 40.0) {
    s.physiologicalState = "drowsy";
  } else if (s.energy >= 80.0 && s.sleepPressure < 10.0) {
    s.physiologicalState = "rested";
  } else {
    s.physiologicalState = "fine";
  }

  // Primary need — whichever drive is closest to its critical threshold right now.
  if (s.thirst > 55.0) s.primaryNeed = "thirsty";
  else if (s.hunger > 50.0) s.primaryNeed = "hungry";
  else if (s.fatigue > 60.0 || s.sleepPressure > 55.0) s.primaryNeed = "tired";
  else s.primaryNeed = "fine";

  // One-word tone hint derived from circadian + drive state + threat.
  // The LLM is asked to mirror this tone in 1-3 sentences.
  if (!s.awake) {
    s.circadianTone = "unconscious";
  } else if (s.predatorDist >= 0 && s.predatorDist <= 3) {
    s.circadianTone = "terrified";
  } else if (s.threatLevel >= 0.65 && s.predatorDist >= 0) {
    s.circadianTone = "tense";
  } else if (s.primaryNeed == "thirsty" || s.primaryNeed == "hungry") {
    s.circadianTone = "restless";
  } else if (s.physiologicalState == "sick" || s.physiologicalState == "pained") {
    s.circadianTone = "weary";
  } else if (s.physiologicalState == "exhausted" || s.physiologicalState == "tired") {
    s.circadianTone = (h >= 5.0 && h < 8.0) ? "groggy" : "weary";
  } else if (s.physiologicalState == "drowsy") {
    s.circadianTone = (h >= 22.0 || h < 5.0) ? "drowsy" : "calm";
  } else if (s.phaseOfDay == "deep_night") {
    s.circadianTone = "peaceful";
  } else if (s.phaseOfDay == "dawn" || s.phaseOfDay == "dusk") {
    s.circadianTone = "alert";
  } else if (s.physiologicalState == "rested") {
    s.circadianTone = "calm";
  } else {
    s.circadianTone = "alert";
  }

  return s;
}

std::string fallbackReply(const CognitiveSnapshot& s, const std::string& userText) {
  (void)userText;
  char buf[768];
  if (!s.alive) return "I am no longer alive. My last memory is my death.";
  if (!s.awake) {
    // Circadian awareness: an asleep reply should reflect the time of day, not just a
    // generic "I'm asleep". Late-night replies feel different from late-afternoon naps.
    std::snprintf(buf, sizeof(buf),
                  "It is %s and I am asleep (%s, sleep pressure %.0f). I cannot answer "
                  "until I wake.",
                  s.timeOfDayPhrase.c_str(), s.seasonName.c_str(), s.sleepPressure);
    return buf;
  }
  if (s.health < 20.0) {
    std::snprintf(buf, sizeof(buf),
                  "I am in critical condition this %s (health %.0f). I can barely hold a "
                  "thought.",
                  s.timeOfDayPhrase.c_str(), s.health);
    return buf;
  }
  if (s.predatorDist >= 0 && s.predatorDist <= 3) {
    std::snprintf(buf, sizeof(buf),
                  "There is a predator %d tiles away in this %s %s — I have to flee!",
                  s.predatorDist, s.timeOfDayPhrase.c_str(), s.seasonName.c_str());
    return buf;
  }
  if (s.thirst > 55.0) {
    std::snprintf(buf, sizeof(buf),
                  "I am very thirsty (%.0f) this %s. I need to find water before anything "
                  "else.",
                  s.thirst, s.timeOfDayPhrase.c_str());
    return buf;
  }
  if (s.hunger > 50.0) {
    std::snprintf(buf, sizeof(buf),
                  "I am hungry (%.0f) this %s. I should look for berries.",
                  s.hunger, s.timeOfDayPhrase.c_str());
    return buf;
  }
  if (s.fatigue > 60.0 || s.sleepPressure >= 55.0) {
    std::snprintf(buf, sizeof(buf),
                  "I am tired (%s, fatigue %.0f, sleep pressure %.0f). I need to rest.",
                  s.physiologicalState.c_str(), s.fatigue, s.sleepPressure);
    return buf;
  }
  if (s.physiologicalState == "sick") {
    std::snprintf(buf, sizeof(buf),
                  "I am unwell this %s (a fever I think). My thoughts are slow.",
                  s.timeOfDayPhrase.c_str());
    return buf;
  }
  if (s.physiologicalState == "pained") {
    std::snprintf(buf, sizeof(buf),
                  "I am in pain this %s (%.0f). It is hard to focus.",
                  s.timeOfDayPhrase.c_str(), s.pain);
    return buf;
  }
  // Healthy + awake — open with a circadian phrase so the reply carries the time of day.
  const char* opening = "alert";
  if (s.physiologicalState == "drowsy") opening = "a little drowsy";
  else if (s.physiologicalState == "rested") opening = "well-rested";
  else if (s.physiologicalState == "fine") opening = "steady";
  std::snprintf(buf, sizeof(buf),
                "Good %s. It is %s in %s, weather %s (%.1f C). I am %s at (%d,%d). "
                "Energy %.0f, health %.0f, hunger %.0f, thirst %.0f, water %d/%d.",
                (hSlot(s.hour)), s.timeOfDayPhrase.c_str(), s.seasonName.c_str(),
                s.weather.c_str(), s.ambientTempC, opening, s.posX, s.posY,
                s.energy, s.health, s.hunger, s.thirst,
                s.waterCarried, s.waterCapacity);
  return buf;
}

bool LLMBridge::post(const std::string& body, std::string& response) {
  if (endpoint_.empty()) return false;
  // httplib expects base URL without path; handle /v1 suffix correctly
  std::string baseUrl = endpoint_;
  std::string path = "/v1/chat/completions";
  if (baseUrl.size() >= 3 && baseUrl.substr(baseUrl.size() - 3) == "/v1") {
    baseUrl = baseUrl.substr(0, baseUrl.size() - 3);
    path = "/chat/completions";
  }
  std::fprintf(stderr, "LLMBridge: POST to %s%s\n", baseUrl.c_str(), path.c_str());
  std::fflush(stderr);
  httplib::Client cli(baseUrl.c_str());
  cli.set_connection_timeout(timeoutMs_ / 1000, timeoutMs_ % 1000);
  cli.set_read_timeout(timeoutMs_ / 1000, timeoutMs_ % 1000);
  httplib::Headers headers = {{"Content-Type", "application/json"}};
  auto res = cli.Post(path.c_str(), headers, body, "application/json");
  if (!res) {
    std::fprintf(stderr, "LLMBridge: POST failed - no response (error: %d)\n", static_cast<int>(res.error()));
    std::fflush(stderr);
    return false;
  }
  std::fprintf(stderr, "LLMBridge: POST status: %d\n", res->status);
  std::fflush(stderr);
  if (res->status != 200) {
    std::fprintf(stderr, "LLMBridge: POST failed - status %d: %s\n", res->status, res->body.c_str());
    std::fflush(stderr);
    return false;
  }
  response = res->body;
  return true;
}

bool LLMBridge::chatComplete(const JsonValue& messages, int maxTokens, JsonValue& out) {
  ++calls_;
  JsonValue req = JsonValue::makeObject();
  // Use actual model name for Nemotron 3 Nano (llama.cpp server returns full path as model ID)
  req.setString("model", "/home/umang/llama.cpp/NVIDIA-Nemotron3-Nano-4B-Q4_K_M.gguf");
  req.set("messages", messages);
  req.setNumber("max_tokens", maxTokens);
  req.setNumber("temperature", 0.7);
  // Nemotron-style reasoning models ramble for ~hundreds of tokens before answering,
  // blowing the latency budget on the iGPU. Disable the [THINK] phase for chat;
  // structured thinking is not needed for these short classify/respond calls.
  JsonValue kwargs = JsonValue::makeObject();
  kwargs.setBool("enable_thinking", false);
  req.set("chat_template_kwargs", kwargs);
  std::string respBody;
  if (!post(req.dump(), respBody)) {
    std::fprintf(stderr, "LLMBridge: POST failed\n");
    std::fflush(stderr);
    ++failures_;
    return false;
  }
  JsonValue parsed;
  if (!jsonParse(respBody, parsed)) {
    std::fprintf(stderr, "LLMBridge: JSON parse failed: %s\n", respBody.c_str());
    std::fflush(stderr);
    ++failures_;
    return false;
  }
  const JsonValue* choices = parsed.find("choices");
  if (!choices || choices->type() != JsonValue::Type::Array ||
      choices->asArray().empty()) {
    std::fprintf(stderr, "LLMBridge: No choices in response\n");
    std::fflush(stderr);
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
  
  char stateBuf[2048];
  std::snprintf(stateBuf, sizeof(stateBuf),
    "time=%lld day=%d hour=%.1f awake=%s "
    "energy=%.0f hunger=%.0f thirst=%.0f fatigue=%.0f health=%.0f "
    "threatLevel=%.2f predatorDist=%d waterDist=%d "
    "currentAction=%s",
    static_cast<long long>(s.simTime), s.day, s.hour, s.awake ? "yes" : "no",
    s.energy, s.hunger, s.thirst, s.fatigue, s.health,
    s.threatLevel, s.predatorDist, s.waterDist,
    s.currentAction.c_str()
  );
  
  payload.setString("state", stateBuf);
  payload.setString("message", userText);
  user.setString("content", payload.dump());
  JsonValue msgs = JsonValue::makeArray();
  msgs.push(std::move(sys));
  msgs.push(std::move(user));

  JsonValue choice;
  if (!chatComplete(msgs, 200, choice)) return false;
  const JsonValue* msg = choice.find("message");
  if (!msg) return false;
  // Reasoning models may bury the JSON under a long chain-of-thought; the answer is the
  // last/outermost object in `content`. Extract it wherever it appears.
  raw = extractJsonObject(assistantContent(*msg));
  if (raw.empty()) return false;
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
      "memories, say you do not remember it. Keep replies short (1-3 sentences).\n"
      "TIME-OF-DAY AWARENESS: your reply must reflect the organism's circadian and "
      "physiological state. Use the phaseOfDay, timeOfDayPhrase, seasonName, "
      "physiologicalState, primaryNeed and circadianTone fields to set the tone. "
      "Examples: an asleep organism cannot answer (one short sleep line); an exhausted "
      "organism at 3am is groggy; a thirsty organism mentions thirst first; a "
      "well-rested organism at midday is calm and clear. The tone is more important "
      "than the words — speak as the organism feels RIGHT NOW, not as a generic "
      "assistant.");
  JsonValue user = JsonValue::makeObject();
  user.setString("role", "user");
  JsonValue payload = JsonValue::makeObject();
  
  // Build comprehensive state string
  char stateBuf[4096];
  std::snprintf(stateBuf, sizeof(stateBuf),
    "time=%lld day=%d hour=%.1f awake=%s "
    "pos=(%d,%d) terrain=%s weather=%s temp=%.1fC "
    "energy=%.0f hunger=%.0f thirst=%.0f fatigue=%.0f health=%.0f pain=%.0f sleepP=%.0f "
    "water=%d/%d "
    "threatLevel=%.2f "
    "predatorsNear=%d predatorDist=%d preyNear=%d "
    "waterDist=%d plantDist=%d plantType=%s "
    "currentAction=%s "
    "activeGoals=%s "
    "personality=[%s] "
    "drives=[%s] "
    "userTrust=%.2f userFamiliarity=%.2f userAffection=%.2f userExpectsReturn=%s "
    "wildlife=[%s] "
    "skills=[%s] "
    "recentMemories=[%s] "
    "circadian=[phase=%s phrase=\"%s\" season=%s] "
    "physiological=[state=%s primaryNeed=%s tone=%s]",
    static_cast<long long>(s.simTime), s.day, s.hour, s.awake ? "yes" : "no",
    s.posX, s.posY, s.terrain.c_str(), s.weather.c_str(), s.ambientTempC,
    s.energy, s.hunger, s.thirst, s.fatigue, s.health, s.pain, s.sleepPressure,
    s.waterCarried, s.waterCapacity,
    s.threatLevel,
    s.predatorsNear, s.predatorDist, s.preyNear,
    s.waterDist, s.plantDist, s.plantType.c_str(),
    s.currentAction.c_str(),
    s.activeGoals.empty() ? "none" : s.activeGoals[0].c_str(), // first goal
    s.personalitySummary.c_str(),
    s.driveSummary.c_str(),
    s.userTrust, s.userFamiliarity, s.userAffection, s.userExpectsReturn ? "yes" : "no",
    s.wildlifeSummary.c_str(),
    s.skillSummary.c_str(),
    s.recentMemorySummary.c_str(),
    s.phaseOfDay.c_str(), s.timeOfDayPhrase.c_str(), s.seasonName.c_str(),
    s.physiologicalState.c_str(), s.primaryNeed.c_str(), s.circadianTone.c_str()
  );
  
  payload.setString("state", stateBuf);
  payload.setString("user_intent", parsed.intent);
  payload.setString("user_topic", parsed.topic);
  payload.setString("message", userText);
  user.setString("content", payload.dump());
  JsonValue msgs = JsonValue::makeArray();
  msgs.push(std::move(sys));
  msgs.push(std::move(user));

  JsonValue choice;
  if (!chatComplete(msgs, 1024, choice)) {
    std::fprintf(stderr, "LLMBridge: chatComplete failed in respond\n");
    return false;
  }
  const JsonValue* msg = choice.find("message");
  if (!msg) {
    std::fprintf(stderr, "LLMBridge: No message in choice\n");
    return false;
  }
  raw = assistantContent(*msg);
  if (raw.empty()) {
    std::fprintf(stderr, "LLMBridge: Empty raw response\n");
    return false;
  }
  // If the reply hides reasoning before the answer (some servers stream it into content),
  // keep only the trailing prose: drop leading '...' thinking blocks and code fences.
  const size_t fenceA = raw.find("```");
  if (fenceA != std::string::npos) {
    const size_t fenceB = raw.rfind("```");
    if (fenceB > fenceA) raw = raw.substr(fenceA + 3, fenceB - fenceA - 3);
  }
  reply = trim(raw).substr(0, kMaxReplyChars);
  std::fprintf(stderr, "LLMBridge: Reply: %s\n", reply.c_str());
  return !reply.empty();
}

} // namespace eidolon