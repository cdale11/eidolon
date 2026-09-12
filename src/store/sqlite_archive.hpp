// SQLite archive: durable store for memories, timeline events and conversations.
// WAL mode, schema versioning with migrations, thread-safe (server writes from the sim
// thread while HTTP handlers read). Lives outside ReplicaCore (DESIGN §15).
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "mind/archive.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace eidolon {

struct ConversationInfo {
  int64_t id = 0;
  std::string title;
  int64_t createdAt = 0;
  // E3-slice-2a: owning individual (as int64 bit pattern of the uint64 id).
  // A successor never inherits the predecessor's chat as lived experience: on
  // succession the server starts a fresh conversation under the new id, while
  // old ones stay readable as attributed predecessor history.
  int64_t individualId = 0;
  // E3-slice-2c: world scope. A world reset starts a new lineage; chats from
  // other worlds are never cited as "my predecessor's".
  int64_t worldId = 0;
};

struct Message {
  int64_t id = 0;
  int64_t conversationId = 0;
  std::string role; // "user" | "organism"
  std::string text;
  int64_t t = 0;
  // E3-slice-2a: writer attribution. Organism rows carry the speaker's
  // individual id; user rows carry 0 — the human persists across generations.
  int64_t individualId = 0;
};

struct InternetResource {
  int64_t id = 0;
  int64_t t = 0;
  std::string url;
  std::string title;
  std::string content;
  std::string source;
};

class SQLiteArchive : public Archive {
public:
  explicit SQLiteArchive(const std::string& path, std::string& err);
  ~SQLiteArchive();

  SQLiteArchive(const SQLiteArchive&) = delete;
  SQLiteArchive& operator=(const SQLiteArchive&) = delete;

  // Archive interface.
  void episode(const Episode& e) override;
  void event(int64_t t, const char* type, const char* text) override;
  std::vector<ArchivedEvent> timeline(int64_t startTick, int64_t endTick,
                                      size_t limit = 128) const override;

  // Conversations. individualId is the uint64 Engine id stored as int64 bits
  // (equality-preserving); 0 = legacy/unattributed. User message rows should
  // pass 0 — the human persists across generations.
  int64_t createConversation(const std::string& title, int64_t t,
                             int64_t individualId = 0, int64_t worldId = 0);
  void appendMessage(int64_t conversationId, const std::string& role,
                     const std::string& text, int64_t t, int64_t individualId = 0);
  void setConversationTitle(int64_t conversationId, const std::string& title);
  void deleteConversation(int64_t conversationId);
  std::vector<ConversationInfo> listConversations() const;
  // E3-slice-2a: attributed retrieval — all conversations owned by one
  // individual, oldest-first, so a successor can read (not relive) a
  // predecessor's dialogue history.
  std::vector<ConversationInfo> listConversationsByIndividual(int64_t individualId,
                                                              int limit = 50) const;
  // E3-slice-2c: predecessor retrieval — same world, owned by someone else,
  // newest-first, so the successor can cite (never relive) prior dialogue.
  std::vector<ConversationInfo> listPredecessorConversations(int64_t worldId,
                                                             int64_t excludeIndividualId,
                                                             int limit = 5) const;
  std::vector<Message> listMessages(int64_t conversationId, int limit = 200) const;
  // Q1: most recent messages, returned oldest-first (chronological) for prompt
  // history. listMessages returns oldest-first with LIMIT, i.e. the FIRST rows —
  // useless as a conversation tail, hence this dedicated query.
  std::vector<Message> listRecentMessages(int64_t conversationId, int limit = 12) const;

  // Internet learning corpus: approved/fetched resources the organism has read.
  int64_t recordInternetResource(int64_t t, const std::string& url,
                                 const std::string& title,
                                 const std::string& content,
                                 const std::string& source);
  std::vector<InternetResource> listInternetResources(int limit = 100) const;
  int64_t internetResourceCount() const;

  // Observability.
  int64_t episodeCount() const;
  int64_t eventCount() const;

private:
  bool exec(const char* sql);
  bool prepare(const char* sql, sqlite3_stmt** stmt) const;
  void migrate();
  void ensureSchema();
  void runStatement(sqlite3_stmt* stmt); // step + finalize, reset on failure

  sqlite3* db_ = nullptr;
  mutable std::mutex mu_;
};

} // namespace eidolon
