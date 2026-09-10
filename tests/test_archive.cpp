#ifndef EIDOLON_WASM_BUILD
#include "harness.hpp"

#include <sqlite3.h>
#include <unistd.h>

#include <cstdio>

#include "store/sqlite_archive.hpp"
#include "mind/grounded_language.hpp"

using namespace eidolon;

namespace {
std::string tmpDbPath() {
  static int n = 0;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "/tmp/eidolon_test_%d_%d.db", static_cast<int>(::getpid()), n++);
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
  const auto timeline = a.timeline(0, 200);
  CHECK_EQ(timeline.size(), 2u);
  CHECK_EQ(timeline[0].kind, EventKind::Forage);
  CHECK_EQ(timeline[1].kind, EventKind::Weather);
}

TEST(sqlite_archive_backs_grounded_past_tense_reply) {
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  Episode forage;
  forage.t = 100;
  forage.kind = EventKind::Forage;
  forage.importance = 0.7;
  a.episode(forage);
  a.event(120, "drink", "water=8.0");

  MemoryRing memory;
  GroundedLanguage grounded(7);
  auto reply = grounded.answer_what_did_you_do(a, memory, 1000);
  CHECK(reply.has_value());
  CHECK(reply->text.find("foraging") != std::string::npos);
  CHECK(reply->text.find("drink") != std::string::npos);
  CHECK(!reply->honestUncertainty);
  CHECK_EQ(reply->sourceEvents.size(), 2u);
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
#endif
