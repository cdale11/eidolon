// eidolon-server: runs the organism's simulation on a background thread and serves the
// chat UI + API. The browser is a client, not the host: disconnecting never stops the
// sim (DESIGN §1, §16). LLM calls go through LLMBridge with deterministic fallbacks.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/clock.hpp"
#include "core/log.hpp"
#include "llm/bridge.hpp"
#include "sim/engine.hpp"
#include "store/sqlite_archive.hpp"

namespace eidolon {

class Server {
public:
  struct Options {
    std::string dataDir = "data/runs/server";
    std::string listenHost = "127.0.0.1";
    int port = 8081;
    uint64_t seed = 42;
    bool deterministic = false;
    int worldW = 128, worldH = 128;
    std::string policyPriorPath; // optional teacher-baked policy prior for fresh organisms
    std::string llmEndpoint; // empty = offline
    int llmTimeoutMs = 10000;
  };

  explicit Server(Options opts);
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Starts the sim thread and the HTTP listener (blocking). Returns 0 on clean exit.
  int run();

  // Graceful stop (SIGTERM/SIGINT): stops HTTP + sim loop, final save runs.
  void requestStop() { stop_.store(true); }

  // API surface (called from HTTP handlers on server threads).
  std::string statusJson();
  std::string sendMessage(const std::string& conversationIdStr, const std::string& text,
                          std::string& err);
  std::string conversationsJson();
  std::string messagesJson(const std::string& conversationIdStr, std::string& err);
  std::string newConversationJson();
  std::string deleteConversationJson(const std::string& conversationIdStr);
  std::string resetWorldJson(const std::string& seedStr);
  std::string snapshotJson();
  std::string savePriorJson(const std::string& name);

private:
  void simLoop();
  void autosave();
  int64_t currentConversation();

  Options opts_;
  Engine engine_;
  EventLog log_;
  std::unique_ptr<SQLiteArchive> archive_;
  std::unique_ptr<LLMBridge> llm_;
  std::atomic<bool> stop_ = false;
  std::thread simThread_;

  mutable std::mutex engineMu_; // guards engine_ (sim thread vs HTTP handlers)
  int64_t conversationId_ = -1;
};

} // namespace eidolon