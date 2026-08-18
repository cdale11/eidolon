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
};

struct Message {
  int64_t id = 0;
  int64_t conversationId = 0;
  std::string role; // "user" | "organism"
  std::string text;
  int64_t t = 0;
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

  // Conversations.
  int64_t createConversation(const std::string& title, int64_t t);
  void appendMessage(int64_t conversationId, const std::string& role,
                     const std::string& text, int64_t t);
  std::vector<ConversationInfo> listConversations() const;
  std::vector<Message> listMessages(int64_t conversationId, int limit = 200) const;

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