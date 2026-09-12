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

TEST(sqlite_archive_internet_resources) {
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());

  const int64_t id = a.recordInternetResource(123, "https://example.test/a", "Example",
                                             "A useful resource about survival.",
                                             "user-approved");
  CHECK(id >= 0);
  CHECK_EQ(a.internetResourceCount(), 1);

  const auto resources = a.listInternetResources();
  CHECK_EQ(resources.size(), 1u);
  CHECK_EQ(resources[0].url, std::string("https://example.test/a"));
  CHECK_EQ(resources[0].title, std::string("Example"));
  CHECK_EQ(resources[0].source, std::string("user-approved"));
  CHECK(resources[0].content.find("survival") != std::string::npos);
}

TEST(sqlite_archive_recent_messages_tail) {
  // Q1: the prompt-history query returns the TAIL chronologically, unlike
  // listMessages (oldest-first LIMIT = the head).
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());

  const int64_t cid = a.createConversation("tail chat", 0);
  for (int i = 0; i < 6; ++i) {
    a.appendMessage(cid, i % 2 == 0 ? "user" : "organism",
                    "msg" + std::to_string(i), i);
  }
  const auto tail = a.listRecentMessages(cid, 4);
  CHECK_EQ(tail.size(), 4u);
  CHECK_EQ(tail[0].text, std::string("msg2"));
  CHECK_EQ(tail[3].text, std::string("msg5"));
  CHECK_EQ(tail[3].role, std::string("organism"));
  // Oversized limit just returns everything, still chronological.
  CHECK_EQ(a.listRecentMessages(cid, 100).size(), 6u);
}

TEST(sqlite_archive_conversation_attribution) {
  // E3-slice-2a: conversations and organism rows carry the speaker's id; user
  // rows carry 0 (the human persists across generations).
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());

  const int64_t speaker = 123456789012345LL;
  const int64_t cid = a.createConversation("gen1 chat", 10, speaker);
  CHECK(cid > 0);
  a.appendMessage(cid, "user", "hello", 11, 0);
  a.appendMessage(cid, "organism", "hi", 12, speaker);
  const int64_t cid2 = a.createConversation("gen2 chat", 20, speaker + 1);
  a.appendMessage(cid2, "organism", "new me", 21, speaker + 1);

  const auto msgs = a.listMessages(cid);
  CHECK_EQ(msgs.size(), 2u);
  CHECK_EQ(msgs[0].individualId, 0);
  CHECK_EQ(msgs[1].individualId, speaker);
  CHECK_EQ(a.listRecentMessages(cid, 10)[1].individualId, speaker);

  const auto mine = a.listConversationsByIndividual(speaker);
  CHECK_EQ(mine.size(), 1u);
  CHECK_EQ(mine[0].id, cid);
  CHECK_EQ(mine[0].individualId, speaker);
  const auto theirs = a.listConversationsByIndividual(speaker + 1);
  CHECK_EQ(theirs.size(), 1u);
  CHECK_EQ(theirs[0].id, cid2);
  CHECK(a.listConversationsByIndividual(999).empty());
  // Default (legacy) callers still compile and read back as unattributed.
  const int64_t cid3 = a.createConversation("legacy", 30);
  a.appendMessage(cid3, "user", "old", 31);
  CHECK_EQ(a.listMessages(cid3)[0].individualId, 0);
}

TEST(sqlite_archive_v2_to_v3_migration) {
  // A pre-attribution (v2) database keeps every row and reads back with
  // individual_id 0; new attributed writes work alongside.
  const std::string path = tmpDbPath();
  {
    sqlite3* db = nullptr;
    CHECK_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    const char* schema =
        "CREATE TABLE episodes (t INTEGER, x INTEGER, y INTEGER, kind INTEGER, "
        "importance REAL, detail INTEGER);"
        "CREATE TABLE events (t INTEGER, type TEXT, text TEXT);"
        "CREATE TABLE conversations (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, created_at INTEGER);"
        "CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "conversation_id INTEGER, role TEXT, text TEXT, t INTEGER);"
        "INSERT INTO conversations (title, created_at) VALUES ('old chat', 5);"
        "INSERT INTO messages (conversation_id, role, text, t) VALUES "
        "(1, 'user', 'hi', 6);"
        "PRAGMA user_version=2;";
    char* msg = nullptr;
    CHECK_EQ(sqlite3_exec(db, schema, nullptr, nullptr, &msg), SQLITE_OK);
    sqlite3_close(db);
  }
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());
  const auto convs = a.listConversations();
  CHECK_EQ(convs.size(), 1u);
  CHECK_EQ(convs[0].title, std::string("old chat"));
  CHECK_EQ(convs[0].individualId, 0);
  const auto msgs = a.listMessages(1);
  CHECK_EQ(msgs.size(), 1u);
  CHECK_EQ(msgs[0].text, std::string("hi"));
  CHECK_EQ(msgs[0].individualId, 0);
  // Post-migration writes carry attribution.
  const int64_t cid = a.createConversation("new chat", 7, 42);
  a.appendMessage(cid, "organism", "me", 8, 42);
  CHECK_EQ(a.listConversationsByIndividual(42).size(), 1u);
  CHECK_EQ(a.listMessages(cid)[0].individualId, 42);
}

TEST(sqlite_archive_episode_source_attribution) {
  // E3-slice-2b: archived episodes keep their source individual; the timeline
  // exposes it so inherited records stay distinguishable from lived ones.
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());

  Episode own;
  own.t = 100;
  own.kind = EventKind::Forage;
  own.importance = 0.7;
  a.episode(own);
  Episode inherited;
  inherited.t = 50;
  inherited.kind = EventKind::Drink;
  inherited.importance = 0.9;
  inherited.sourceIndividualId = 777;
  a.episode(inherited);

  const auto timeline = a.timeline(0, 200);
  CHECK_EQ(timeline.size(), 2u);
  CHECK_EQ(timeline[0].sourceIndividualId, 777);
  CHECK_EQ(timeline[1].sourceIndividualId, 0);
  CHECK_EQ(timeline[0].kind, EventKind::Drink);
}

TEST(sqlite_archive_v3_to_v4_migration) {
  // Pre-source (v3) episode rows survive the migration and read back as 0
  // (own experience); new attributed writes work alongside.
  const std::string path = tmpDbPath();
  {
    sqlite3* db = nullptr;
    CHECK_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    const char* schema =
        "CREATE TABLE episodes (t INTEGER, x INTEGER, y INTEGER, kind INTEGER, "
        "importance REAL, detail INTEGER);"
        "CREATE TABLE events (t INTEGER, type TEXT, text TEXT);"
        "CREATE TABLE conversations (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, created_at INTEGER, individual_id INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "conversation_id INTEGER, role TEXT, text TEXT, t INTEGER, "
        "individual_id INTEGER NOT NULL DEFAULT 0);"
        "INSERT INTO episodes (t, x, y, kind, importance, detail) VALUES "
        "(10, 1, 2, 1, 0.5, 0);"
        "PRAGMA user_version=3;";
    char* msg = nullptr;
    CHECK_EQ(sqlite3_exec(db, schema, nullptr, nullptr, &msg), SQLITE_OK);
    sqlite3_close(db);
  }
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());
  const auto timeline = a.timeline(0, 100);
  CHECK_EQ(timeline.size(), 1u);
  CHECK_EQ(timeline[0].sourceIndividualId, 0);
  CHECK_EQ(a.episodeCount(), 1);
}

TEST(sqlite_archive_v4_to_v5_migration) {
  // Pre-world (v4) conversation rows survive and read back with world_id 0.
  const std::string path = tmpDbPath();
  {
    sqlite3* db = nullptr;
    CHECK_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    const char* schema =
        "CREATE TABLE episodes (t INTEGER, x INTEGER, y INTEGER, kind INTEGER, "
        "importance REAL, detail INTEGER, source INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE events (t INTEGER, type TEXT, text TEXT);"
        "CREATE TABLE conversations (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "title TEXT, created_at INTEGER, individual_id INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "conversation_id INTEGER, role TEXT, text TEXT, t INTEGER, "
        "individual_id INTEGER NOT NULL DEFAULT 0);"
        "INSERT INTO conversations (title, created_at, individual_id) VALUES "
        "('old chat', 5, 11);"
        "PRAGMA user_version=4;";
    char* msg = nullptr;
    CHECK_EQ(sqlite3_exec(db, schema, nullptr, nullptr, &msg), SQLITE_OK);
    sqlite3_close(db);
  }
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());
  const auto convs = a.listConversations();
  CHECK_EQ(convs.size(), 1u);
  CHECK_EQ(convs[0].individualId, 11);
  CHECK_EQ(convs[0].worldId, 0);
}

TEST(sqlite_archive_predecessor_conversations_world_scoped) {
  // E3-slice-2c: same world + other owner = predecessor; other worlds and the
  // speaker itself are excluded, newest-first.
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  CHECK(err.empty());

  const int64_t me = 100, pred = 99, other = 101;
  const int64_t world = 7, otherWorld = 8;
  a.createConversation("mine", 1, me, world);
  const int64_t oldPred = a.createConversation("pred old", 2, pred, world);
  const int64_t newPred = a.createConversation("pred new", 3, pred, world);
  a.createConversation("other world", 4, other, otherWorld);

  const auto preds = a.listPredecessorConversations(world, me);
  CHECK_EQ(preds.size(), 2u);
  CHECK_EQ(preds[0].id, newPred); // newest first
  CHECK_EQ(preds[1].id, oldPred);
  CHECK(a.listPredecessorConversations(world, pred).empty() == false); // mine shows for them
  CHECK(a.listPredecessorConversations(otherWorld, other).empty());    // alone there
  CHECK(a.listPredecessorConversations(world, 424242).size() == 3u);   // stranger: all owned
}

TEST(grounded_past_tense_qualifies_inherited_records) {
  // E3-slice-2c: timeline answers built from inherited episodes must say so —
  // never read as autobiography.
  const std::string path = tmpDbPath();
  std::string err;
  SQLiteArchive a(path, err);
  Episode inherited;
  inherited.t = 10;
  inherited.kind = EventKind::Drink;
  inherited.importance = 0.9;
  inherited.sourceIndividualId = 555;
  a.episode(inherited);

  MemoryRing memory;
  GroundedLanguage grounded(7);
  auto reply = grounded.answer_about_past(a, memory, "did you drink water?", 1000);
  CHECK(reply.has_value());
  CHECK(reply->text.find("predecessor") != std::string::npos);

  // Own-only records carry no such qualifier.
  const std::string path2 = tmpDbPath();
  SQLiteArchive b(path2, err);
  Episode own;
  own.t = 10;
  own.kind = EventKind::Drink;
  own.importance = 0.9;
  b.episode(own);
  auto reply2 = grounded.answer_about_past(b, memory, "did you drink water?", 1000);
  CHECK(reply2.has_value());
  CHECK(reply2->text.find("predecessor") == std::string::npos);
}
#endif
