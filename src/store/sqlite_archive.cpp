#include "store/sqlite_archive.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <cstring>

namespace eidolon {

namespace {
constexpr int kSchemaVersion = 1;
}

SQLiteArchive::SQLiteArchive(const std::string& path, std::string& err) {
  if (sqlite3_open_v2(path.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    err = std::string("sqlite open failed: ") + (db_ ? sqlite3_errmsg(db_) : "unknown");
    db_ = nullptr;
    return;
  }
  if (!exec("PRAGMA journal_mode=WAL")) {
    err = "sqlite: cannot enable WAL mode";
    sqlite3_close(db_);
    db_ = nullptr;
    return;
  }
  exec("PRAGMA synchronous=NORMAL");
  migrate();
  ensureSchema();
}

SQLiteArchive::~SQLiteArchive() {
  if (db_) sqlite3_close(db_);
}

bool SQLiteArchive::exec(const char* sql) {
  char* msg = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &msg);
  if (rc != SQLITE_OK) {
    std::fprintf(stderr, "sqlite exec failed: %s (%s)\n", sql, msg ? msg : "?");
    sqlite3_free(msg);
    return false;
  }
  return true;
}

bool SQLiteArchive::prepare(const char* sql, sqlite3_stmt** stmt) const {
  return sqlite3_prepare_v2(db_, sql, -1, stmt, nullptr) == SQLITE_OK;
}

void SQLiteArchive::runStatement(sqlite3_stmt* stmt) {
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::fprintf(stderr, "sqlite step failed: %s\n", sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

void SQLiteArchive::migrate() {
  int version = 0;
  sqlite3_stmt* stmt = nullptr;
  if (prepare("PRAGMA user_version", &stmt) && stmt) {
    if (sqlite3_step(stmt) == SQLITE_ROW) version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
  }
  if (version == 0) {
    // Fresh database: create the full schema.
    ensureSchema();
    exec("PRAGMA user_version=1");
  } else if (version < kSchemaVersion) {
    // Future migrations append here (e.g. ALTER TABLE ... for v2).
    exec("PRAGMA user_version=1");
  }
}

void SQLiteArchive::ensureSchema() {
  exec("CREATE TABLE IF NOT EXISTS episodes ("
       "t INTEGER, x INTEGER, y INTEGER, kind INTEGER, importance REAL, detail INTEGER)");
  exec("CREATE TABLE IF NOT EXISTS events (t INTEGER, type TEXT, text TEXT)");
  exec("CREATE TABLE IF NOT EXISTS conversations ("
       "id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, created_at INTEGER)");
  exec("CREATE TABLE IF NOT EXISTS messages ("
       "id INTEGER PRIMARY KEY AUTOINCREMENT, conversation_id INTEGER, role TEXT, "
       "text TEXT, t INTEGER)");
  exec("CREATE INDEX IF NOT EXISTS idx_episodes_t ON episodes(t)");
  exec("CREATE INDEX IF NOT EXISTS idx_events_t ON events(t)");
  exec("CREATE INDEX IF NOT EXISTS idx_messages_conv ON messages(conversation_id)");
}

void SQLiteArchive::episode(const Episode& e) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("INSERT INTO episodes VALUES (?,?,?,?,?,?)", &stmt) || !stmt) return;
  sqlite3_bind_int64(stmt, 1, e.t);
  sqlite3_bind_int(stmt, 2, e.x);
  sqlite3_bind_int(stmt, 3, e.y);
  sqlite3_bind_int(stmt, 4, static_cast<int>(e.kind));
  sqlite3_bind_double(stmt, 5, e.importance);
  sqlite3_bind_int(stmt, 6, e.detail);
  runStatement(stmt);
}

void SQLiteArchive::event(int64_t t, const char* type, const char* text) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("INSERT INTO events VALUES (?,?,?)", &stmt) || !stmt) return;
  sqlite3_bind_int64(stmt, 1, t);
  sqlite3_bind_text(stmt, 2, type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, text, -1, SQLITE_TRANSIENT);
  runStatement(stmt);
}

int64_t SQLiteArchive::createConversation(const std::string& title, int64_t t) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return -1;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("INSERT INTO conversations (title, created_at) VALUES (?,?)", &stmt) ||
      !stmt) {
    return -1;
  }
  sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, t);
  runStatement(stmt);
  return sqlite3_last_insert_rowid(db_);
}

void SQLiteArchive::appendMessage(int64_t conversationId, const std::string& role,
                                  const std::string& text, int64_t t) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("INSERT INTO messages (conversation_id, role, text, t) VALUES (?,?,?,?)",
               &stmt) ||
      !stmt) {
    return;
  }
  sqlite3_bind_int64(stmt, 1, conversationId);
  sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, t);
  runStatement(stmt);
}

void SQLiteArchive::setConversationTitle(int64_t conversationId,
                                         const std::string& title) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("UPDATE conversations SET title=? WHERE id=?", &stmt) || !stmt) return;
  sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, conversationId);
  runStatement(stmt);
}

void SQLiteArchive::deleteConversation(int64_t conversationId) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  if (prepare("DELETE FROM messages WHERE conversation_id=?", &stmt) && stmt) {
    sqlite3_bind_int64(stmt, 1, conversationId);
    runStatement(stmt);
  }
  if (prepare("DELETE FROM conversations WHERE id=?", &stmt) && stmt) {
    sqlite3_bind_int64(stmt, 1, conversationId);
    runStatement(stmt);
  }
}

std::vector<ConversationInfo> SQLiteArchive::listConversations() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<ConversationInfo> out;
  if (!db_) return out;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("SELECT id, title, created_at FROM conversations ORDER BY id DESC",
               &stmt) ||
      !stmt) {
    return out;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ConversationInfo c;
    c.id = sqlite3_column_int64(stmt, 0);
    const unsigned char* title = sqlite3_column_text(stmt, 1);
    c.title = title ? reinterpret_cast<const char*>(title) : "";
    c.createdAt = sqlite3_column_int64(stmt, 2);
    out.push_back(c);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<Message> SQLiteArchive::listMessages(int64_t conversationId, int limit) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<Message> out;
  if (!db_) return out;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("SELECT id, conversation_id, role, text, t FROM messages "
               "WHERE conversation_id=? ORDER BY id ASC LIMIT ?",
               &stmt) ||
      !stmt) {
    return out;
  }
  sqlite3_bind_int64(stmt, 1, conversationId);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Message m;
    m.id = sqlite3_column_int64(stmt, 0);
    m.conversationId = sqlite3_column_int64(stmt, 1);
    const unsigned char* role = sqlite3_column_text(stmt, 2);
    m.role = role ? reinterpret_cast<const char*>(role) : "";
    const unsigned char* text = sqlite3_column_text(stmt, 3);
    m.text = text ? reinterpret_cast<const char*>(text) : "";
    m.t = sqlite3_column_int64(stmt, 4);
    out.push_back(m);
  }
  sqlite3_finalize(stmt);
  return out;
}

int64_t SQLiteArchive::episodeCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return -1;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("SELECT COUNT(*) FROM episodes", &stmt) || !stmt) return -1;
  int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

int64_t SQLiteArchive::eventCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return -1;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("SELECT COUNT(*) FROM events", &stmt) || !stmt) return -1;
  int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

} // namespace eidolon