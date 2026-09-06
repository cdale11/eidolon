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
#include "llm/web_browser.hpp"
#include "mind/compute_profile.hpp"
#include "sim/engine.hpp"
#include "store/sqlite_archive.hpp"

namespace eidolon {

class Server {
public:
  struct Options {
    std::string dataDir = "data/runs/server";
    std::string listenHost = "127.0.0.1";
    int port = 8081;
    uint64_t seed = 0; // 0 = derive from entropy (system clock / pid / rd) at fresh start
    bool deterministic = false;
    int worldW = 128, worldH = 128;
    std::string policyPriorPath; // optional teacher-baked policy prior for fresh organisms
    std::string heredityPath;    // optional heredity file for inheritance across deaths
    float heredityWeight = 0.7f; // inheritance weight 0.0..1.0
    std::string dumpExperiencesPath; // optional path to dump teacher-training records (JSONL)
    std::string llmEndpoint; // empty = offline
    int llmTimeoutMs = 10000;
    // Adaptive fidelity (Phase 11): 0 = auto (from compute profile), else explicit level
    // 1..3 for Low/Medium/High. Only affects pacing/model budget/world detail, never the
    // deterministic tick semantics.
    int fidelityLevel = 0;
    // Internet access (Future Directions): configurable, user-gated browsing.
    // Results become content the organism reads/learns from via normal memory pipeline.
    bool internetEnabled = false;
    std::string searchEndpoint; // optional custom search endpoint
    std::string searchApiKey;   // optional API key (format "apiKey:cx" for Google)
    SearchEngine searchEngine = SearchEngine::SearXNG;  // default: SearXNG (free, no API key)
    uint32_t maxSearchResults = 5;
    uint32_t maxFetchChars = 8000;
    uint32_t browseTimeoutMs = 10000;
    // Phase 12: authentication/session — a single-user shared secret. Empty disables
    // auth (LAN trust model, per run_eidolon.sh). When set, mutating endpoints require
    // the matching `Authorization: Bearer` (or `?key=`) token. The organism is
    // single-user by invariant, so this is a shared secret, not multi-tenant accounts.
    std::string apiKey;
    // Phase 12: world-state authority. "client" (default) = client-authoritative
    // cognition/learning for the single-user organism; "server" = server-authoritative
    // world (cognition stays client-side) for future shared-world deployments.
    std::string worldAuthority = "client";
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
  std::string metricsJson();
  std::string sendMessage(const std::string& conversationIdStr, const std::string& text,
                          std::string& err);
  std::string conversationsJson();
  std::string messagesJson(const std::string& conversationIdStr, std::string& err);
  std::string newConversationJson();
  std::string deleteConversationJson(const std::string& conversationIdStr);
  std::string resetWorldJson(const std::string& seedStr);
  std::string snapshotJson();
  
  // Phase 12: Binary snapshot download/upload (client-authoritative persistence)
  std::string snapshotDownload();  // Returns raw binary blob
  std::string snapshotUpload(const std::vector<uint8_t>& blob, std::string& err);
  
  // Phase 12: Delta sync protocol (compact binary deltas)
  std::string checkpointCreateJson();  // Returns checkpoint ID
  std::string checkpointDeltaJson(const std::string& baseCheckpointId);  // Returns delta from base
  std::string applyDelta(const std::vector<uint8_t>& deltaBlob, std::string& err);
  
  // Phase 12: ComputeProfile handling (client reports capabilities)
  std::string computeProfileJson(const std::string& jsonBody, std::string& err);
  // Phase 15: client offload endpoints (raw content; caller sets status/type)
  std::string wasmBinary();         // empty if unavailable (404)
  std::string wasmJavaScript();     // empty if unavailable (404)
  std::string clientSnapshotUpload(const std::vector<uint8_t>& blob, std::string& err);
  void maybeStartClientComputing(const ComputeProfile& profile);

  // Future Directions: Internet access (configurable, user-gated)
  std::string browseSearchJson(const std::string& jsonBody, std::string& err);
  std::string browseFetchJson(const std::string& jsonBody, std::string& err);

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
  std::unique_ptr<WebBrowser> browser_;
  std::atomic<bool> stop_ = false;
  std::thread simThread_;

  mutable std::mutex engineMu_; // guards engine_ (sim thread vs HTTP handlers)
  int64_t conversationId_ = -1;
  FidelitySettings fidelity_; // resolved at simLoop start (metrics/status report)
  // Phase 15: client-side offload
  std::atomic<bool> clientComputing_{false};
  std::string wasmPath_;
  std::string wasmJsPath_;
  // metrics
  uint64_t clientSnapshotsReceived_{0};
  // last wall ms a client snapshot arrived (0 = never); simLoop resumes the sim
  // itself once a claiming client has been silent for >15s (continuity invariant).
  std::atomic<int64_t> lastClientContactMs_{0};
};

} // namespace eidolon