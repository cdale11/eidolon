#include "server/server.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <thread>

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
  :root { color-scheme: light; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
         sans-serif; background: #fff; color: #0d0d0d; display: flex; height: 100vh; }
  #sidebar { width: 260px; background: #f7f7f8; border-right: 1px solid #e5e5e5;
             display: flex; flex-direction: column; }
  #sidehead { padding: 12px; display: flex; flex-direction: column; gap: 8px;
              border-bottom: 1px solid #ececec; }
  #newchat { width: 100%; padding: 10px 12px; border: 1px solid #d9d9e3; border-radius: 8px;
             background: #fff; color: #0d0d0d; font-size: 14px; cursor: pointer; }
  #newchat:hover { background: #f0f0f4; }
  #newworld { width: 100%; padding: 8px 12px; border: 1px solid #d9d9e3; border-radius: 8px;
              background: #ececf1; color: #0d0d0d; font-size: 13px; cursor: pointer; }
  #newworld:hover { background: #e3e3ea; }
  #saveprior { width: 100%; padding: 8px 12px; margin-top: 4px; border: 1px dashed #10a37f;
    border-radius: 8px; background: #f7f7f8; color: #10a37f; cursor: pointer; }
  #saveprior:hover { background: #e7f7f2; }
  #convlist { flex: 1; overflow-y: auto; padding: 8px; }
  .conv { display: flex; align-items: center; padding: 8px 10px; border-radius: 8px;
          cursor: pointer; font-size: 13px; gap: 6px; color: #0d0d0d; }
  .conv:hover { background: #ececf1; }
  .conv.active { background: #e0e0ea; }
  .conv .title { flex: 1; overflow: hidden; text-overflow: ellipsis;
                 white-space: nowrap; }
  .conv .del { border: 0; background: none; cursor: pointer; color: #8e8ea0;
               font-size: 14px; padding: 0 2px; border-radius: 4px; }
  .conv .del:hover { color: #d0312d; background: #fff; }
  #main { flex: 1; display: flex; flex-direction: column; min-width: 0; }
  #statusbar { padding: 8px 16px; font-size: 12px; color: #6b6b70;
               border-bottom: 1px solid #ececec; background: #fafafa; }
  #chat { flex: 1; overflow-y: auto; background: #f9f9f9; }
  #chat .col { max-width: 760px; margin: 0 auto; padding: 24px 16px;
               display: flex; flex-direction: column; gap: 16px; }
  .msg { display: flex; gap: 12px; align-items: flex-start; }
  .msg.user { flex-direction: row-reverse; }
  .avatar { width: 30px; height: 30px; border-radius: 50%; flex: none;
            display: flex; align-items: center; justify-content: center;
            font-size: 13px; font-weight: 600; }
  .msg.user .avatar { background: #10a37f; color: #fff; }
  .msg.organism .avatar { background: #ececf1; color: #0d0d0d; }
  .bubble { padding: 10px 14px; border-radius: 14px; white-space: pre-wrap;
            line-height: 1.5; font-size: 14px; max-width: 100%; }
  .msg.user .bubble { background: #0d0d0d; color: #fff;
                      border-bottom-right-radius: 4px; }
  .msg.organism .bubble { background: #fff; color: #0d0d0d; border: 1px solid #ececec;
                          border-bottom-left-radius: 4px; }
  #inputrow { padding: 12px 16px; background: #fff; border-top: 1px solid #ececec; }
  #inputwrap { max-width: 760px; margin: 0 auto; display: flex; gap: 8px;
               align-items: flex-end; }
  #input { flex: 1; resize: none; background: #f7f7f8; color: #0d0d0d;
           border: 1px solid #e5e5e5; border-radius: 12px; padding: 12px 14px;
           font-family: inherit; font-size: 14px; line-height: 1.4; max-height: 200px;
           outline: none; }
  #input:focus { border-color: #10a37f; background: #fff; }
  #send { background: #10a37f; color: #fff; border: 0; border-radius: 12px;
          padding: 12px 18px; cursor: pointer; font-size: 14px; font-weight: 600; }
  #send:hover { background: #0e8f6f; }
  #send:disabled { opacity: .5; cursor: default; }
  .typing { opacity: .6; }
  @media (max-width: 700px) { #sidebar { width: 200px; } }
</style>
</head>
<body>
<div id="sidebar">
  <div id="sidehead">
    <button id="newchat">+ New chat</button>
    <button id="newworld">Restart world (fresh organism)</button>
    <button id="saveprior" title="Save this organism's learned behaviour as a prior for future runs">Save this organism as prior</button>
  </div>
  <div id="convlist"></div>
</div>
<div id="main">
  <div id="statusbar">connecting…</div>
  <div id="chat"><div class="col" id="chatcol"></div></div>
  <div id="inputrow">
    <div id="inputwrap">
      <textarea id="input" rows="1" placeholder="Message the organism…" autocomplete="off"></textarea>
      <button id="send">Send</button>
    </div>
  </div>
</div>
<script>
const chat = document.getElementById('chat');
const chatcol = document.getElementById('chatcol');
const input = document.getElementById('input');
const sendBtn = document.getElementById('send');
const convsEl = document.getElementById('convlist');
let convId = null;

function esc(s) {
  const d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}
async function refreshStatus() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    document.getElementById('statusbar').textContent =
      `Day ${s.day} · hour ${s.hour.toFixed(1)} · ${s.awake ? 'awake' : 'asleep'} · ` +
      `energy ${s.energy.toFixed(0)} · hunger ${s.hunger.toFixed(0)} · ` +
      `thirst ${s.thirst.toFixed(0)} · health ${s.health.toFixed(0)} · ` +
      `${s.weather} ${s.tempC.toFixed(1)}C`;
  } catch (e) {}
}
async function refreshConvs() {
  try {
    const r = await fetch('/api/conversations');
    const list = await r.json();
    convsEl.innerHTML = '';
    for (const c of list) {
      const el = document.createElement('div');
      el.className = 'conv' + (c.id === convId ? ' active' : '');
      el.dataset.id = c.id;
      const title = document.createElement('span');
      title.className = 'title';
      title.textContent = c.title || `conversation ${c.id}`;
      const del = document.createElement('button');
      del.className = 'del';
      del.textContent = '✕';
      del.title = 'Delete chat';
      del.onclick = async (e) => {
        e.stopPropagation();
        if (!confirm('Delete this chat?')) return;
        await fetch('/api/conversations/delete', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({conversation_id: String(c.id)})
        });
        if (convId === c.id) { convId = null; chatcol.innerHTML = ''; }
        await refreshConvs();
      };
      el.onclick = () => selectConv(c.id);
      el.appendChild(title);
      el.appendChild(del);
      convsEl.appendChild(el);
    }
  } catch (e) {}
}
function addMsg(role, text) {
  const wrap = document.createElement('div');
  wrap.className = 'msg ' + role;
  const av = document.createElement('div');
  av.className = 'avatar';
  av.textContent = role === 'user' ? 'You' : 'E';
  const b = document.createElement('div');
  b.className = 'bubble';
  b.textContent = text;
  wrap.appendChild(av);
  wrap.appendChild(b);
  chatcol.appendChild(wrap);
  chat.scrollTop = chat.scrollHeight;
  return b;
}
async function selectConv(id) {
  convId = id;
  await refreshConvs();
  const r = await fetch(`/api/messages?conversation_id=${id}`);
  const msgs = await r.json();
  chatcol.innerHTML = '';
  for (const m of msgs) addMsg(m.role === 'user' ? 'user' : 'organism', m.text);
}
async function newChat() {
  const r = await fetch('/api/conversations/new', {method: 'POST'});
  const j = await r.json();
  if (j.conversation_id) {
    convId = j.conversation_id;
    chatcol.innerHTML = '';
    await refreshConvs();
  }
}
async function resetWorld() {
   if (!confirm('Start a fresh world with a brand-new organism? Current life and world are replaced.')) return;
   await fetch('/api/world/reset', {
     method: 'POST', headers: {'Content-Type': 'application/json'}, body: '{}'
   });
   await refreshStatus();
   await newChat();
 }
async function savePrior() {
   const name = prompt('Name this organism (letters, numbers, - and _). It becomes a reusable prior '
     + 'loaded on future fresh runs:');
   if (name == null) return;
   const r = await fetch('/api/save-prior', {
     method: 'POST', headers: {'Content-Type': 'application/json'},
     body: JSON.stringify({name: name.trim()})
   });
   const j = await r.json();
   alert(j.ok ? 'Saved prior to ' + j.path : 'Save failed: ' + (j.error || 'unknown error'));
 }
function autoGrow() {
  input.style.height = 'auto';
  input.style.height = Math.min(input.scrollHeight, 200) + 'px';
}
async function send() {
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  autoGrow();
  sendBtn.disabled = true;
  addMsg('user', text);
  const thinking = addMsg('organism', '…');
  thinking.classList.add('typing');
  try {
    const r = await fetch('/api/send', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({message: text, conversation_id: convId ? String(convId) : ''})
    });
    const j = await r.json();
    thinking.textContent = j.reply || j.error || '(no reply)';
    thinking.classList.remove('typing');
    if (j.conversation_id && convId !== j.conversation_id) {
      convId = j.conversation_id;
      await refreshConvs();
    }
  } catch (e) {
    thinking.textContent = 'connection error';
    thinking.classList.remove('typing');
  }
  sendBtn.disabled = false;
  chat.scrollTop = chat.scrollHeight;
}
sendBtn.onclick = send;
document.getElementById('newchat').onclick = newChat;
document.getElementById('newworld').onclick = resetWorld;
document.getElementById('saveprior').onclick = savePrior;
input.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); }
});
input.addEventListener('input', autoGrow);
(async () => {
  await refreshConvs();
  if (convId === null && convsEl.children.length > 0) {
    await selectConv(parseInt(convsEl.children[0].dataset.id));
  } else if (convsEl.children.length === 0) {
    await newChat();
  }
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
      if (!opts_.policyPriorPath.empty() &&
          !engine_.loadPolicyPrior(opts_.policyPriorPath)) {
        std::fprintf(stderr, "warning: cannot load policy prior %s; using random init\n",
                     opts_.policyPriorPath.c_str());
      } else if (!opts_.policyPriorPath.empty()) {
        std::fprintf(stderr, "policy prior loaded from %s (online learning continues)\n",
                     opts_.policyPriorPath.c_str());
      }
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
  const Vec2i p = engine_.world().organismPos();
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "{\"day\":%lld,\"hour\":%.1f,\"awake\":%s,\"alive\":%s,"
                "\"energy\":%.1f,\"hunger\":%.1f,\"thirst\":%.1f,\"fatigue\":%.1f,"
                "\"sleepP\":%.1f,\"health\":%.1f,\"bodyTemp\":%.1f,\"weather\":\"%s\","
                "\"tempC\":%.1f,\"simTime\":%lld,"
                "\"preyNear\":%d,\"predatorsNear\":%d,\"predatorDist\":%d}",
                static_cast<long long>(engine_.clock().day()),
                engine_.clock().hourOfDay(),
                b.isSleeping() ? "false" : "true",
                engine_.isAlive() ? "true" : "false", b.energy(), b.hunger(), b.thirst(),
                b.fatigue(), b.sleepPressure(), b.health(), b.bodyTemp(),
                w.describe(), w.ambientTempC(engine_.clock()),
                static_cast<long long>(engine_.clock().now()),
                engine_.world().preyCount(p, Perception::kSightRadius),
                engine_.world().predatorCount(p, Perception::kSightRadius),
                [&] {
                  const WildlifeAgent* pr = engine_.world().nearestPredator(
                      p, Perception::kSightRadius);
                  return pr ? distCheb(pr->pos, p) : -1;
                }());
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
    // Title the conversation from its first user message (ChatGPT-style).
    if (archive_->listMessages(convId, 1).empty()) {
      std::string title = trimmed;
      if (title.size() > 40) title = title.substr(0, 40) + "...";
      archive_->setConversationTitle(convId, title);
    }
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

std::string Server::newConversationJson() {
  if (!archive_) return "{\"error\":\"no archive\"}";
  const int64_t id = archive_->createConversation("New chat",
                                                  engine_.clock().now());
  if (id < 0) return "{\"error\":\"create failed\"}";
  return "{\"conversation_id\":" + std::to_string(id) + "}";
}

std::string Server::deleteConversationJson(const std::string& conversationIdStr) {
  const int64_t convId = std::strtoll(conversationIdStr.c_str(), nullptr, 10);
  if (archive_ && convId > 0) {
    archive_->deleteConversation(convId);
    if (conversationId_ == convId) conversationId_ = -1;
  }
  return "{\"ok\":true}";
}

std::string Server::resetWorldJson(const std::string& seedStr) {
  uint64_t seed = std::strtoull(seedStr.c_str(), nullptr, 10);
  if (seedStr.empty() || seed == 0) {
    std::random_device rd;
    seed = (static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
                                      .time_since_epoch()
                                      .count()) ^
            (static_cast<uint64_t>(rd()) << 1) ^ (static_cast<uint64_t>(getpid()) << 33));
  }
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    engine_.init(seed, opts_.deterministic, opts_.worldW, opts_.worldH);
    if (!opts_.policyPriorPath.empty()) {
      engine_.loadPolicyPrior(opts_.policyPriorPath);
    }
    engine_.setArchive(archive_.get());
    log_.line(engine_.clock().now(), "birth",
              "world reset seed=%llu (fresh organism, fresh world)",
              static_cast<unsigned long long>(seed));
    log_.flush();
    std::string err;
    if (!engine_.saveFile(opts_.dataDir + "/save.snap", err)) {
      std::fprintf(stderr, "warning: reset save failed: %s\n", err.c_str());
    }
  }
  return statusJson();
}

std::string Server::savePriorJson(const std::string& name) {
  std::string safe;
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-') {
      safe += c;
    }
  }
  if (safe.empty()) safe = "organism";
  const std::string dir = opts_.dataDir + "/priors";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = dir + "/prior_" + safe + ".eprp";
  bool ok = false;
  {
    std::lock_guard<std::mutex> lock(engineMu_);
    ok = engine_.savePolicyPrior(path);
  }
  if (ok) {
    log_.line(engine_.clock().now(), "prior_saved",
              "user saved current organism policy prior to %s", path.c_str());
    log_.flush();
    return "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\"}";
  }
  return "{\"ok\":false,\"error\":\"failed to write prior\"}";
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
  svr.Post("/api/conversations/new", [this](const httplib::Request&,
                                            httplib::Response& res) {
    res.set_content(newConversationJson(), "application/json");
  });
  svr.Post("/api/conversations/delete",
           [this](const httplib::Request& req, httplib::Response& res) {
             JsonValue body;
             if (!jsonParse(req.body, body)) {
               res.status = 400;
               res.set_content("{\"error\":\"bad json\"}", "application/json");
               return;
             }
             res.set_content(deleteConversationJson(body.str("conversation_id")),
                             "application/json");
           });
  svr.Post("/api/world/reset", [this](const httplib::Request& req,
                                      httplib::Response& res) {
    JsonValue body;
    if (!jsonParse(req.body, body)) {
      res.status = 400;
      res.set_content("{\"error\":\"bad json\"}", "application/json");
      return;
    }
    res.set_content(resetWorldJson(body.str("seed")), "application/json");
  });
  svr.Post("/api/save-prior", [this](const httplib::Request& req,
                                     httplib::Response& res) {
    JsonValue body;
    if (!jsonParse(req.body, body)) {
      res.status = 400;
      res.set_content("{\"error\":\"bad json\"}", "application/json");
      return;
    }
    res.set_content(savePriorJson(body.str("name")), "application/json");
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