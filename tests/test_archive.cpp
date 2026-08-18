#include "harness.hpp"

#include <sqlite3.h>

#include <cstdio>

#include "store/sqlite_archive.hpp"

using namespace eidolon;

namespace {
std::string tmpDbPath() {
  static int n = 0;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "/tmp/eidolon_test_%d.db", n++);
  std::remove(buf);
  std::remove((std::string(buf) + "-wal").c_str());
  std::remove((std::string(buf) + "-shm").c_str());
  return buf;
}
} // namespace

TEST(sqlite_archive_wal_and_version) {
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());
  sqlite3* db = nullptr;
  CHECK_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
  CHECK_EQ(sqlite3_exec(db, "PRAGMA journal_mode", nullptr, nullptr, nullptr),
           SQLITE_OK);
  char* wal = nullptr;
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &stmt, nullptr);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    wal = reinterpret_cast<char*>(sqlite3_malloc(8));
    std::snprintf(wal, 8, "%s", sqlite3_column_text(stmt, 0));
  }
  sqlite3_finalize(stmt);
  CHECK_EQ(std::string(wal ? wal : "?"), std::string("wal"));
  sqlite3_free(wal);
  sqlite3_close(db);
}

TEST(sqlite_archive_episodes_events) {
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  Episode e;
  e.t = 100;
  e.x = 3;
  e.y = 4;
  e.kind = EventKind::Forage;
  e.importance = 0.7;
  e.detail = 5;
  a.episode(e);
  a.event(150, "weather", "rain");
  CHECK_EQ(a.episodeCount(), 1);
  CHECK_EQ(a.eventCount(), 1);
}

TEST(sqlite_archive_conversations) {
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  const int64_t cid = a.createConversation("first chat", 1000);
  CHECK(cid > 0);
  a.appendMessage(cid, "user", "hello", 1001);
  a.appendMessage(cid, "organism", "hi there", 1002);
  a.appendMessage(cid, "user", "how are you?", 1003);

  const auto convs = a.listConversations();
  CHECK_EQ(convs.size(), 1u);
  CHECK_EQ(convs[0].title, std::string("first chat"));

  const auto msgs = a.listMessages(cid);
  CHECK_EQ(msgs.size(), 3u);
  CHECK_EQ(msgs[0].role, std::string("user"));
  CHECK_EQ(msgs[0].text, std::string("hello"));
  CHECK_EQ(msgs[2].role, std::string("user"));
  CHECK_EQ(msgs[2].text, std::string("how are you?"));
}