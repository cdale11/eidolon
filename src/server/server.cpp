#include "server/server.hpp"

#include <csignal>
#include <cstdio>
#include <filesystem>

#include "httplib.h"

namespace eidolon {

namespace {
const char* kIndexHtml = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Eidolon</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: system-ui, sans-serif; background: #111; color: #ddd;
         display: flex; height: 100vh; }
  #sidebar { width: 240px; border-right: 1px solid #333; padding: 12px; overflow-y: auto; }
  #sidebar h2 { font-size: 14px; color: #888; margin: 0 0 8px; }
  .conv { padding: 6px 8px; border-radius: 6px; cursor: pointer; font-size: 13px; }
  .conv:hover { background: #1c1c1c; }
  .conv.active { background: #263; }
  #main { flex: 1; display: flex; flex-direction: column; }
  #statusbar { padding: 4px 12px; font-size: 12px; color: #8a8; border-bottom: 1px solid #333; }
  #chat { flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 10px; }
  .msg { max-width: 70%; padding: 8px 12px; border-radius: 10px; white-space: pre-wrap; }
  .user { align-self: flex-end; background: #1e3a5f; }
  .organism { align-self: flex-start; background: #222; }
  #inputrow { display: flex; gap: 8px; padding: 12px; border-top: 1px solid #333; }
  #input { flex: 1; background: #1a1a1a; color: #ddd; border: 1px solid #333;
           border-radius: 8px; padding: 10px; }
  #send { background: #2c5; color: #fff; border: 0; border-radius: 8px; padding: 0 18px;
          cursor: pointer; }
  #send:disabled { opacity: .5; cursor: default; }
</style>
</head>
<body>
<div id="sidebar">
  <h2>Conversations</h2>
  <div id="convs"></div>
</div>
<div id="main">
  <div id="statusbar">connecting…</div>
  <div id="chat"></div>
  <div id="inputrow">
    <input id="input" placeholder="Message the organism…" autocomplete="off">
    <button id="send">Send</button>
  </div>
</div>
<script>
const chat = document.getElementById('chat');
const input = document.getElementById('input');
const sendBtn = document.getElementById('send');
const convsEl = document.getElementById('convs');
let convId = null;

async function refreshStatus() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    document.getElementById('statusbar').textContent =
      `day ${s.day} · hour ${s.hour.toFixed(1)} · ${s.awake ? 'awake' : 'asleep'} · ` +
      `energy ${s.energy.toFixed(0)} · hunger ${s.hunger.toFixed(0)} · ` +
      `thirst ${s.thirst.toFixed(0)} · health ${s.health.toFixed(0)} · ${s.weather} ${s.tempC.toFixed(1)}C`;
  } catch (e) {}
}
async function refreshConvs() {
  const r = await fetch('/api/conversations');
  const list = await r.json();
  convsEl.innerHTML = '';
  for (const c of list) {
    const el = document.createElement('div');
    el.className = 'conv' + (c.id === convId ? ' active' : '');
    el.textContent = c.title || `conversation ${c.id}`;
    el.onclick = () => selectConv(c.id);
    convsEl.appendChild(el);
  }
}
async function selectConv(id) {
  convId = id;
  await refreshConvs();
  const r = await fetch(`/api/messages?conversation_id=${id}`);
  const msgs = await r.json();
  chat.innerHTML = '';
  for (const m of msgs) {
    const el = document.createElement('div');
    el.className = 'msg ' + (m.role === 'user' ? 'user' : 'organism');
    el.textContent = m.text;
    chat.appendChild(el);
  }
  chat.scrollTop = chat.scrollHeight;
}
async function send() {
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  sendBtn.disabled = true;
  const el = document.createElement('div');
  el.className = 'msg user';
  el.textContent = text;
  chat.appendChild(el);
  const thinking = document.createElement('div');
  thinking.className = 'msg organism';
  thinking.textContent = '…';
  chat.appendChild(thinking);
  try {
    const r = await fetch('/api/send', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({message: text})
    });
    const j = await r.json();
    thinking.textContent = j.reply || j.error || '(no reply)';
    if (j.conversation_id && convId !== j.conversation_id) {
      convId = j.conversation_id;
      await refreshConvs();
    }
  } catch (e) {
    thinking.textContent = 'connection error';
  }
  sendBtn.disabled = false;
  chat.scrollTop = chat.scrollHeight;
}
sendBtn.onclick = send;
input.addEventListener('keydown', e => { if (e.key === 'Enter') send(); });
(async () => {
  await refreshConvs();
  if (convId === null && convsEl.children.length > 0) await selectConv(parseInt(convsEl.children[0].dataset.id || convsEl.children[0].textContent.split(' ')[1]));
  refreshStatus();
  setInterval(refreshStatus, 5000);
})();
</script>
</body>
</html>)html";

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}
} // namespace

Server::Server(Options opts) : opts_(std::move(opts)) {
  std::error_code ec;
  std::filesystem::create_directories(opts_.dataDir, ec);
  std::string err;
  archive_ = std::make_unique<SQLiteArchive>(opts_.dataDir + "/memory.db", err);
  if (!archive_) {
    std::fprintf(stderr, "warning: archive unavailable: %s\n", err.c_str());
  }
  llm_ = std::make_unique<LLMBridge>(opts_.llmEndpoint, opts_.llmTimeoutMs);
}

Server::~Server() {
  stop_.store(true);
  if (simThread_.joinable()) simThread_.join();
}

void Server::simLoop() {
  const std::string savePath = opts_.dataDir + "/save.snap";
  const std::string logPath = opts_.dataDir + "/events.log";
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    bool resumed = false;
    if (std::filesystem::exists(savePath)) {
      std::string err;
      if (engine_.loadFile(savePath, err)) {
        resumed = true;
      } else {
        std::fprintf(stderr, "warning: cannot load snapshot (%s); starting fresh\n",
                     err.c_str());
      }
    }
    if (!resumed) {
      engine_.init(opts_.seed, opts_.deterministic, opts_.worldW, opts_.worldH);
      std::fprintf(stderr, "fresh organism: seed=%llu world=%dx%d\n",
                   static_cast<unsigned long long>(engine_.masterSeed()),
                   opts_.worldW, opts_.worldH);
    }
    engine_.setArchive(archive_.get());
    if (!log_.open(logPath)) {
      std::fprintf(stderr, "warning: cannot open event log %s\n", logPath.c_str());
    }
    if (!resumed) {
      log_.line(engine_.clock().now(), "birth", "world seed=%llu",
                static_cast<unsigned long long>(engine_.masterSeed()));
    }
  }
  int64_t lastAutosave = 0;
  while (!stop_.load()) {
    {
      // Scope the lock so HTTP handler threads always get a window to acquire the
      // mutex; holding it across the pacing sleep below would starve them out.
      std::lock_guard<std::mutex> lock(engineMu_);
      engine_.tickAndLog(log_);
      if (engine_.clock().now() - lastAutosave >= 600) { // autosave every 10 sim-minutes
        lastAutosave = engine_.clock().now();
        std::string err;
        if (!engine_.saveFile(savePath, err)) {
          std::fprintf(stderr, "warning: autosave failed: %s\n", err.c_str());
        }
        log_.flush();
      }
    }
    // Real-time pacing: 1 sim-second per 2ms wall (500x speed); capped so the server
    // can keep up with a long unattended run.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    log_.flush();
    std::string err;
    engine_.saveFile(savePath, err);
  }
}

void Server::autosave() {
  std::string err;
  if (!engine_.saveFile(opts_.dataDir + "/save.snap", err)) {
    std::fprintf(stderr, "warning: save failed: %s\n", err.c_str());
  }
  log_.flush();
}

int64_t Server::currentConversation() {
  if (conversationId_ < 0 && archive_) {
    conversationId_ = archive_->createConversation("chat", 0);
  }
  return conversationId_;
}

std::string Server::statusJson() {
  std::lock_guard<std::mutex> lock(engineMu_);
  const auto& b = engine_.body();
  const auto& w = engine_.world().weather();
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "{\"day\":%lld,\"hour\":%.1f,\"awake\":%s,\"alive\":%s,"
                "\"energy\":%.1f,\"hunger\":%.1f,\"thirst\":%.1f,\"fatigue\":%.1f,"
                "\"sleepP\":%.1f,\"health\":%.1f,\"bodyTemp\":%.1f,\"weather\":\"%s\","
                "\"tempC\":%.1f,\"simTime\":%lld}",
                static_cast<long long>(engine_.clock().day()),
                engine_.clock().hourOfDay(),
                b.isSleeping() ? "false" : "true",
                engine_.isAlive() ? "true" : "false", b.energy(), b.hunger(), b.thirst(),
                b.fatigue(), b.sleepPressure(), b.health(), b.bodyTemp(),
                w.describe(), w.ambientTempC(engine_.clock()),
                static_cast<long long>(engine_.clock().now()));
  return buf;
}

std::string Server::sendMessage(const std::string& conversationIdStr,
                                const std::string& text, std::string& err) {
  const std::string trimmed = [&] {
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\n' || text[a] == '\r')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\n' || text[b - 1] == '\r'))
      --b;
    return text.substr(a, b - a);
  }();
  if (trimmed.empty()) {
    err = "empty message";
    return "{}";
  }

  int64_t convId = -1;
  if (!conversationIdStr.empty()) {
    convId = std::strtoll(conversationIdStr.c_str(), nullptr, 10);
  }
  if (convId <= 0) convId = currentConversation();

  // Snapshot the current state for the reply (grounded in real state).
  CognitiveSnapshot snap;
  MemoryRing memCopy;
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    const auto& b = engine_.body();
    const auto& w = engine_.world().weather();
    memCopy = engine_.memory(); // copy hot memory (bounded, small)
    snap = makeSnapshot(engine_.clock().now(), engine_.isAlive(), !b.isSleeping(),
                        b.energy(), b.hunger(), b.thirst(), b.fatigue(),
                        b.sleepPressure(), b.bodyTemp(), b.health(),
                        static_cast<int>(engine_.clock().day()),
                        engine_.clock().hourOfDay(), w.describe(),
                        "plains", w.ambientTempC(engine_.clock()), memCopy);
  }

  if (archive_) {
    archive_->appendMessage(convId, "user", trimmed, snap.simTime);
  }

  std::string reply;
  if (llm_ && llm_->enabled()) {
    ParsedMessage parsed;
    std::string raw;
    if (llm_->parse(trimmed, snap, parsed, raw) &&
        llm_->respond(trimmed, snap, parsed, reply, raw)) {
      // success path
    } else {
      reply = fallbackReply(snap, trimmed);
    }
  } else {
    reply = fallbackReply(snap, trimmed);
  }

  if (archive_) {
    archive_->appendMessage(convId, "organism", reply, snap.simTime);
  }

  std::string out = "{\"conversation_id\":" + std::to_string(convId) +
                    ",\"reply\":\"" + jsonEscape(reply) + "\"}";
  return out;
}

std::string Server::conversationsJson() {
  if (!archive_) return "[]";
  const auto convs = archive_->listConversations();
  std::string out = "[";
  bool first = true;
  for (const auto& c : convs) {
    if (!first) out += ",";
    first = false;
    out += "{\"id\":" + std::to_string(c.id) + ",\"title\":\"" + jsonEscape(c.title) +
           "\",\"created_at\":" + std::to_string(c.createdAt) + "}";
  }
  return out + "]";
}

std::string Server::messagesJson(const std::string& conversationIdStr,
                                 std::string& err) {
  if (!archive_) return "[]";
  const int64_t convId = std::strtoll(conversationIdStr.c_str(), nullptr, 10);
  if (convId <= 0) {
    err = "bad conversation_id";
    return "[]";
  }
  const auto msgs = archive_->listMessages(convId);
  std::string out = "[";
  bool first = true;
  for (const auto& m : msgs) {
    if (!first) out += ",";
    first = false;
    out += "{\"id\":" + std::to_string(m.id) + ",\"role\":\"" + jsonEscape(m.role) +
           "\",\"text\":\"" + jsonEscape(m.text) + "\",\"t\":" +
           std::to_string(m.t) + "}";
  }
  return out + "]";
}

std::string Server::snapshotJson() {
  std::lock_guard<std::mutex> lock(engineMu_);
  std::vector<uint8_t> blob = engine_.snapshot();
  std::string out = "{\"size\":" + std::to_string(blob.size()) + ",\"seed\":" +
                     std::to_string(engine_.masterSeed()) + "}";
  return out;
}

int Server::run() {
  simThread_ = std::thread([this] { simLoop(); });

  httplib::Server svr;
  svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(kIndexHtml, "text/html");
  });
  svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(statusJson(), "application/json");
  });
  svr.Get("/api/conversations", [this](const httplib::Request&,
                                       httplib::Response& res) {
    res.set_content(conversationsJson(), "application/json");
  });
  svr.Get("/api/messages", [this](const httplib::Request& req,
                                  httplib::Response& res) {
    std::string err;
    const std::string out = messagesJson(req.get_param_value("conversation_id"), err);
    if (!err.empty()) res.status = 400;
    res.set_content(out, "application/json");
  });
  svr.Get("/api/snapshot", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(snapshotJson(), "application/json");
  });
  svr.Post("/api/send", [this](const httplib::Request& req, httplib::Response& res) {
    JsonValue body;
    if (!jsonParse(req.body, body)) {
      res.status = 400;
      res.set_content("{\"error\":\"bad json\"}", "application/json");
      return;
    }
    const std::string text = body.str("message");
    std::string err;
    const std::string out = sendMessage(body.str("conversation_id"), text, err);
    if (!err.empty()) {
      res.status = 400;
      res.set_content("{\"error\":\"" + jsonEscape(err) + "\"}", "application/json");
      return;
    }
    res.set_content(out, "application/json");
  });

  std::fprintf(stderr, "eidolon-server listening on http://%s:%d (llm=%s)\n",
               opts_.listenHost.c_str(), opts_.port,
               llm_ && llm_->enabled() ? opts_.llmEndpoint.c_str() : "offline");
  // Listen on a worker thread so requestStop() (SIGTERM/SIGINT) can tear down cleanly
  // and run the final save instead of losing up to an autosave interval.
  std::atomic<bool> httpDone = false;
  std::thread listenThread([&] {
    svr.listen(opts_.listenHost, opts_.port);
    httpDone.store(true);
  });
  while (!stop_.load() && !httpDone.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  svr.stop();
  listenThread.join();
  stop_.store(true);
  if (simThread_.joinable()) simThread_.join();
  return 0;
}

} // namespace eidolon