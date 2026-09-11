#include "store/sqlite_archive.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace eidolon {

namespace {
constexpr int kSchemaVersion = 2;

EventKind kindFromType(const std::string& type) {
  if (type == "forage") return EventKind::Forage;
  if (type == "drink") return EventKind::Drink;
  if (type == "attack") return EventKind::Attack;
  if (type == "weather") return EventKind::Weather;
  if (type == "sleep") return EventKind::Sleep;
  if (type == "wake") return EventKind::Wake;
  if (type == "death") return EventKind::Death;
  if (type == "illness") return EventKind::Illness;
  if (type == "recovery") return EventKind::Recovery;
  if (type == "tamed") return EventKind::Tamed;
  return EventKind::Birth;
}

std::string typeFromKind(EventKind kind) {
  switch (kind) {
    case EventKind::Forage: return "forage";
    case EventKind::Drink: return "drink";
    case EventKind::Sleep: return "sleep";
    case EventKind::Wake: return "wake";
    case EventKind::Attack: return "attack";
    case EventKind::Birth: return "birth";
    case EventKind::Death: return "death";
    case EventKind::Weather: return "weather";
    case EventKind::NearDeath: return "near_death";
    case EventKind::Illness: return "illness";
    case EventKind::Recovery: return "recovery";
    case EventKind::Tamed: return "tamed";
    default: return "event";
  }
}
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
    exec("PRAGMA user_version=2");
  } else if (version < kSchemaVersion) {
    ensureSchema();
    exec("PRAGMA user_version=2");
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
  exec("CREATE TABLE IF NOT EXISTS internet_resources ("
       "id INTEGER PRIMARY KEY AUTOINCREMENT, t INTEGER, url TEXT UNIQUE, title TEXT, "
       "content TEXT, source TEXT)");
  exec("CREATE INDEX IF NOT EXISTS idx_episodes_t ON episodes(t)");
  exec("CREATE INDEX IF NOT EXISTS idx_events_t ON events(t)");
  exec("CREATE INDEX IF NOT EXISTS idx_messages_conv ON messages(conversation_id)");
  exec("CREATE INDEX IF NOT EXISTS idx_internet_resources_t ON internet_resources(t)");
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

std::vector<ArchivedEvent> SQLiteArchive::timeline(int64_t startTick, int64_t endTick,
                                                   size_t limit) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<ArchivedEvent> out;
  if (!db_ || limit == 0 || endTick < startTick) return out;
  const int sqlLimit = static_cast<int>(std::min<size_t>(limit, 512));

  sqlite3_stmt* stmt = nullptr;
  if (prepare("SELECT t, x, y, kind, importance, detail FROM episodes "
              "WHERE t>=? AND t<=? ORDER BY t ASC LIMIT ?", &stmt) && stmt) {
    sqlite3_bind_int64(stmt, 1, startTick);
    sqlite3_bind_int64(stmt, 2, endTick);
    sqlite3_bind_int(stmt, 3, sqlLimit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ArchivedEvent e;
      e.t = sqlite3_column_int64(stmt, 0);
      e.x = static_cast<int16_t>(sqlite3_column_int(stmt, 1));
      e.y = static_cast<int16_t>(sqlite3_column_int(stmt, 2));
      e.kind = static_cast<EventKind>(sqlite3_column_int(stmt, 3));
      e.importance = sqlite3_column_double(stmt, 4);
      e.detail = static_cast<uint8_t>(sqlite3_column_int(stmt, 5));
      e.type = typeFromKind(e.kind);
      e.text = e.type;
      e.fromEpisode = true;
      out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (out.size() < static_cast<size_t>(sqlLimit) &&
      prepare("SELECT t, type, text FROM events WHERE t>=? AND t<=? ORDER BY t ASC LIMIT ?",
              &stmt) && stmt) {
    sqlite3_bind_int64(stmt, 1, startTick);
    sqlite3_bind_int64(stmt, 2, endTick);
    sqlite3_bind_int(stmt, 3, sqlLimit - static_cast<int>(out.size()));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ArchivedEvent e;
      e.t = sqlite3_column_int64(stmt, 0);
      const unsigned char* type = sqlite3_column_text(stmt, 1);
      const unsigned char* text = sqlite3_column_text(stmt, 2);
      e.type = type ? reinterpret_cast<const char*>(type) : "event";
      e.text = text ? reinterpret_cast<const char*>(text) : "";
      e.kind = kindFromType(e.type);
      e.fromEpisode = false;
      out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
  }

  std::sort(out.begin(), out.end(), [](const ArchivedEvent& a, const ArchivedEvent& b) {
    if (a.t != b.t) return a.t < b.t;
    return a.fromEpisode && !b.fromEpisode;
  });
  if (out.size() > static_cast<size_t>(sqlLimit)) out.resize(static_cast<size_t>(sqlLimit));
  return out;
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

int64_t SQLiteArchive::recordInternetResource(int64_t t, const std::string& url,
                                              const std::string& title,
                                              const std::string& content,
                                              const std::string& source) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_ || url.empty() || content.empty()) return -1;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("INSERT INTO internet_resources (t, url, title, content, source) "
               "VALUES (?,?,?,?,?) "
               "ON CONFLICT(url) DO UPDATE SET "
               "t=excluded.t, title=excluded.title, content=excluded.content, "
               "source=excluded.source",
               &stmt) || !stmt) {
    return -1;
  }
  sqlite3_bind_int64(stmt, 1, t);
  sqlite3_bind_text(stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, source.c_str(), -1, SQLITE_TRANSIENT);
  runStatement(stmt);
  return sqlite3_last_insert_rowid(db_);
}

std::vector<InternetResource> SQLiteArchive::listInternetResources(int limit) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<InternetResource> out;
  if (!db_ || limit <= 0) return out;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("SELECT id, t, url, title, content, source FROM internet_resources "
               "ORDER BY t DESC LIMIT ?",
               &stmt) || !stmt) {
    return out;
  }
  sqlite3_bind_int(stmt, 1, std::min(limit, 500));
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    InternetResource r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.t = sqlite3_column_int64(stmt, 1);
    const unsigned char* url = sqlite3_column_text(stmt, 2);
    const unsigned char* title = sqlite3_column_text(stmt, 3);
    const unsigned char* content = sqlite3_column_text(stmt, 4);
    const unsigned char* source = sqlite3_column_text(stmt, 5);
    r.url = url ? reinterpret_cast<const char*>(url) : "";
    r.title = title ? reinterpret_cast<const char*>(title) : "";
    r.content = content ? reinterpret_cast<const char*>(content) : "";
    r.source = source ? reinterpret_cast<const char*>(source) : "";
    out.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return out;
}

int64_t SQLiteArchive::internetResourceCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return -1;
  sqlite3_stmt* stmt = nullptr;
  if (!prepare("SELECT COUNT(*) FROM internet_resources", &stmt) || !stmt) return -1;
  int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
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
