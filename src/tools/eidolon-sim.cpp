// Headless simulator: runs the organism autonomously for a number of simulated days,
// persisting a snapshot on exit. Deterministic mode with --seed reproduces runs
// bit-for-bit for debugging/tests.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

#include "sim/engine.hpp"
#include "store/sqlite_archive.hpp"

using namespace eidolon;

namespace {
std::atomic<bool> g_stop = false;

void handleSignal(int) {
  g_stop.store(true);
}

uint64_t entropySeed() {
  std::random_device rd;
  const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t s = static_cast<uint64_t>(t) ^ (static_cast<uint64_t>(rd()) << 1);
  s ^= static_cast<uint64_t>(getpid()) << 33;
  return s;
}

void printUsage(FILE* out, const char* prog) {
  std::fprintf(out,
               "usage: %s --data DIR [options]\n"
               "  --data DIR        run directory (snapshot, events.log, metrics.log)\n"
               "  --days N          simulated days to run (default 1)\n"
               "  --seed N          master seed (default: entropy; required with --deterministic)\n"
               "  --deterministic   disable entropy mixing for bit-exact replay\n"
               "  --world WxH       world grid size (default 128x128)\n"
               "  --status-interval S  seconds between status lines (default 600)\n"
               "  --archive         also archive memories/events to memory.db (SQLite)\n"
               "  --dump-experiences FILE  append teacher-training records (JSONL) per tick\n"
               "  --policy-prior FILE      seed the fresh policy with teacher-baked weights\n"
               "  --help            this message\n",
               prog);
}

bool parseWorldSize(const char* arg, int& w, int& h) {
  int a = 0, b = 0;
  if (std::sscanf(arg, "%dx%d", &a, &b) != 2 || a <= 0 || b <= 0) return false;
  w = a;
  h = b;
  return true;
}

} // namespace

int main(int argc, char** argv) {
  std::string dataDir = "data/runs/run1";
  double days = 1.0;
  bool haveSeed = false, deterministic = false;
  uint64_t seed = 0;
  int worldW = 128, worldH = 128;
  int64_t statusInterval = 600;
  bool archive = false;
  std::string dumpPath, priorPath;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires %s\n", a.c_str(), what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--data") dataDir = need("DIR");
    else if (a == "--days") days = std::atof(need("N"));
    else if (a == "--seed") {
      seed = std::strtoull(need("N"), nullptr, 0);
      haveSeed = true;
    } else if (a == "--deterministic") deterministic = true;
    else if (a == "--world") {
      if (!parseWorldSize(need("WxH"), worldW, worldH)) {
        std::fprintf(stderr, "error: bad --world size\n");
        return 2;
      }
    } else if (a == "--status-interval") statusInterval = std::atoll(need("S"));
    else if (a == "--archive") archive = true;
    else if (a == "--dump-experiences") dumpPath = need("FILE");
    else if (a == "--policy-prior") priorPath = need("FILE");
    else if (a == "--help") {
      printUsage(stdout, argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str());
      printUsage(stderr, argv[0]);
      return 2;
    }
  }

  if (deterministic && !haveSeed) {
    std::fprintf(stderr, "error: --deterministic requires --seed\n");
    return 2;
  }
  if (days <= 0.0) days = 1.0;

  std::error_code ec;
  std::filesystem::create_directories(dataDir, ec);
  if (ec) {
    std::fprintf(stderr, "error: cannot create %s: %s\n", dataDir.c_str(),
                 ec.message().c_str());
    return 1;
  }

  const std::string savePath = dataDir + "/save.snap";
  const std::string logPath = dataDir + "/events.log";
  const std::string metricsPath = dataDir + "/metrics.log";

  Engine engine;
  EventLog log;

  std::unique_ptr<SQLiteArchive> archiveStore;
  if (archive) {
    std::string err;
    archiveStore = std::make_unique<SQLiteArchive>(dataDir + "/memory.db", err);
    if (!archiveStore) {
      std::fprintf(stderr, "error: cannot open archive: %s\n", err.c_str());
      return 1;
    }
    engine.setArchive(archiveStore.get());
  }

  bool resumed = false;
  if (std::filesystem::exists(savePath)) {
    std::string err;
    if (!engine.loadFile(savePath, err)) {
      std::fprintf(stderr, "error: cannot load snapshot %s: %s\n", savePath.c_str(),
                   err.c_str());
      return 1;
    }
    resumed = true;
  } else {
    if (!haveSeed) seed = entropySeed();
    engine.init(seed, deterministic, worldW, worldH);
    if (!priorPath.empty()) {
      if (!engine.loadPolicyPrior(priorPath)) {
        std::fprintf(stderr, "error: cannot load policy prior %s\n", priorPath.c_str());
        return 1;
      }
      std::fprintf(stderr, "policy prior loaded from %s (online learning continues)\n",
                   priorPath.c_str());
    }
    std::fprintf(stderr, "fresh organism: seed=%llu deterministic=%d world=%dx%d\n",
                 static_cast<unsigned long long>(seed), deterministic ? 1 : 0, worldW,
                 worldH);
  }
  if (statusInterval > 0) engine.setStatusInterval(statusInterval);

  std::FILE* expOut = nullptr;
  if (!dumpPath.empty()) {
    expOut = std::fopen(dumpPath.c_str(), "w");
    if (!expOut) {
      std::fprintf(stderr, "error: cannot open experience dump %s\n", dumpPath.c_str());
      return 1;
    }
    engine.setExperienceOut(expOut);
  }

  if (!log.open(logPath)) {
    std::fprintf(stderr, "error: cannot open log %s\n", logPath.c_str());
    return 1;
  }

  if (resumed) {
    std::fprintf(stderr, "resumed organism at t=%lld (seed=%llu)\n",
                 static_cast<long long>(engine.clock().now()),
                 static_cast<unsigned long long>(engine.masterSeed()));
  } else {
    log.line(engine.clock().now(), "birth", "world seed=%llu",
             static_cast<unsigned long long>(engine.masterSeed()));
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  const int64_t target = engine.clock().now() + static_cast<int64_t>(days * 86400.0);
  int64_t nextProgress = ((engine.clock().now() / 3600) + 1) * 3600;
  std::string whyStopped = "completed";

  while (!g_stop.load() && engine.isAlive() && engine.clock().now() < target) {
    engine.tickAndLog(log);
    if (engine.clock().now() >= nextProgress) {
      nextProgress += 3600;
      std::fprintf(stderr, "t=%lld day=%lld status: energy=%.1f hunger=%.1f thirst=%.1f "
                           "sleeping=%d\n",
                   static_cast<long long>(engine.clock().now()),
                   static_cast<long long>(engine.clock().day()), engine.body().energy(),
                   engine.body().hunger(), engine.body().thirst(),
                   engine.body().isSleeping() ? 1 : 0);
    }
  }

  if (!engine.isAlive()) {
    whyStopped = "organism died";
  }
  log.flush();

  if (expOut) {
    std::fclose(expOut);
    engine.setExperienceOut(nullptr);
  }

  std::string err;
  if (!engine.saveFile(savePath, err)) {
    std::fprintf(stderr, "error: cannot save snapshot: %s\n", err.c_str());
  }

  FILE* m = std::fopen(metricsPath.c_str(), "w");
  if (m) {
    std::fprintf(m, "phase=3\n");
    std::fprintf(m, "seed=%llu\ndeterministic=%d\n",
                 static_cast<unsigned long long>(engine.masterSeed()),
                 engine.deterministic() ? 1 : 0);
    const LearnerMetrics lm = engine.learn().metrics();
    std::fprintf(m, "learnerInferences=%llu\nlearnerUpdates=%llu\n",
                 static_cast<unsigned long long>(lm.inferences),
                 static_cast<unsigned long long>(lm.updates));
    std::fprintf(m, "simTime=%lld\ndays=%.2f\n",
                 static_cast<long long>(engine.clock().now()), days);
    std::fprintf(m, "ticksFine=%llu\nticksCoarse=%llu\nticksSleep=%llu\n",
                 static_cast<unsigned long long>(engine.stats().ticksFine),
                 static_cast<unsigned long long>(engine.stats().ticksCoarse),
                 static_cast<unsigned long long>(engine.stats().ticksSleep));
    std::fprintf(m, "actionsWander=%llu\nactionsRest=%llu\nactionsSleep=%llu\nactionsObserve=%llu\n",
                 static_cast<unsigned long long>(engine.stats().actionsWander),
                 static_cast<unsigned long long>(engine.stats().actionsRest),
                 static_cast<unsigned long long>(engine.stats().actionsSleep),
                 static_cast<unsigned long long>(engine.stats().actionsObserve));
    std::fprintf(m, "energy=%.1f\nhunger=%.1f\nthirst=%.1f\nfatigue=%.1f\nsleepP=%.1f\nbodyTemp=%.1f\nhealth=%.1f\n",
                 engine.body().energy(), engine.body().hunger(), engine.body().thirst(),
                 engine.body().fatigue(), engine.body().sleepPressure(),
                 engine.body().bodyTemp(), engine.body().health());
    std::fprintf(m, "worldHash=%llu\n",
                 static_cast<unsigned long long>(engine.world().grid().hash()));
    std::fprintf(m, "stopped=%s\n", whyStopped.c_str());
    std::fclose(m);
  }

  std::fprintf(stderr, "%s: t=%lld (%s)\n",
               whyStopped == "completed" ? "done" : "stopped",
               static_cast<long long>(engine.clock().now()), whyStopped.c_str());
  log.close();
  return whyStopped == "completed" ? 0 : 0;
}
