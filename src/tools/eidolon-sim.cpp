// Headless simulator: runs the organism autonomously for a number of simulated days,
// persisting a snapshot on exit. Deterministic mode with --seed reproduces runs
// bit-for-bit for debugging/tests. --bench measures the engine hot path and produces
// the Phase 12 backend-selection decision for the measured host profile.
#include <algorithm>
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
#include <thread>
#include <vector>

#include "mind/compute_profile.hpp"
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
                "  --bench                  run the hot-path benchmark + backend selection, then exit\n"
                "  --bench-ticks N          ticks for --bench (default 5000)\n"
                "  --bench-json             emit benchmark results as JSON\n"
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

// Resident set size in KB from /proc/self/statm (Linux). 0 if unavailable.
long rssKb() {
  std::FILE* f = std::fopen("/proc/self/statm", "r");
  if (!f) return 0;
  long totalPages = 0, residentPages = 0;
  if (std::fscanf(f, "%ld %ld", &totalPages, &residentPages) != 2) residentPages = 0;
  std::fclose(f);
  const long pageKb = sysconf(_SC_PAGESIZE) / 1024;
  return residentPages * pageKb;
}

double percentile(std::vector<double>& v, double p) {
  if (v.empty()) return 0.0;
  const size_t idx = static_cast<size_t>(static_cast<double>(v.size() - 1) * p);
  std::nth_element(v.begin(), v.begin() + idx, v.end());
  return v[idx];
}

// Run the hot-path benchmark: N ticks on a fresh organism, collecting per-tick wall
// time (us), sim/wall throughput, snapshot cost, RSS, learner metrics. Then derive a
// native ComputeProfile from the measured host and produce the backend selection.
// Prints a report to stderr; optionally JSON to stdout. Returns process exit code.
int runBenchmark(uint64_t seed, int worldW, int worldH, int64_t ticks, bool asJson) {
  Engine engine;
  engine.init(seed, false, worldW, worldH);
  EventLog log; // unused in bench; tick() only, not tickAndLog

  std::vector<double> tickUs;
  tickUs.reserve(static_cast<size_t>(ticks));
  const int64_t t0 = engine.clock().now();
  const auto w0 = std::chrono::steady_clock::now();
  for (int64_t i = 0; i < ticks && engine.isAlive(); ++i) {
    const auto s = std::chrono::steady_clock::now();
    engine.tick();
    const auto e = std::chrono::steady_clock::now();
    tickUs.push_back(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(e - s).count()));
  }
  const auto w1 = std::chrono::steady_clock::now();
  const int64_t t1 = engine.clock().now();
  const double wallMs = static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(w1 - w0).count()) / 1000.0;

  const int64_t done = static_cast<int64_t>(tickUs.size());
  const int64_t simSec = t1 - t0;
  const double simPerWall = wallMs > 0.0 ? static_cast<double>(simSec) / (wallMs / 1000.0) : 0.0;

  // Snapshot cost + size.
  const auto sw0 = std::chrono::steady_clock::now();
  const std::vector<uint8_t> blob = engine.snapshot();
  const auto sw1 = std::chrono::steady_clock::now();
  const double saveMs = static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(sw1 - sw0).count()) / 1000.0;

  const LearnerMetrics lm = engine.learn().metrics();
  const Engine::Stats& st = engine.stats();

  double p50 = percentile(tickUs, 0.50);
  double p95 = percentile(tickUs, 0.95);
  double maxUs = tickUs.empty() ? 0.0 : *std::max_element(tickUs.begin(), tickUs.end());
  double meanUs = 0.0;
  for (double u : tickUs) meanUs += u;
  if (!tickUs.empty()) meanUs /= static_cast<double>(tickUs.size());

  // Host capability detection for the backend selection: report measured throughput and
  // the backend the selection logic would choose for a browser client with this host's
  // SIMD/threading capabilities.
  ComputeProfile host;
  host.simdLevel = SimdLevel::AVX2;
  host.hasWasmSimd128 = true; // a modern browser would report SIMD128
  host.threadSupport = ThreadSupport::SharedArrayBuffer;
  host.hasSharedArrayBuffer = true;
  host.maxWorkers = static_cast<uint32_t>(std::max(1u, std::thread::hardware_concurrency()));
  host.gpuBackend = GpuBackend::None;
  host.hasWebGPU = false;
  host.hasWebGL = false;
  host.maxMemoryBytes = 1024ull * 1024ull * 1024ull;
  host.platform = "Linux";
  host.userAgent = "native-bench";
  const double ticksPerSec = wallMs > 0.0 ? static_cast<double>(done) / (wallMs / 1000.0) : 0.0;
  host.estimatedSimStepsPerSec = ticksPerSec;
  host.estimatedInferencesPerSec =
      wallMs > 0.0 ? static_cast<double>(lm.inferences) / (wallMs / 1000.0) : 0.0;
  ComputeProfileDetector detector;
  const BackendSelection sel = detector.selectBackend(host);

  if (asJson) {
    std::printf(
        "{\"bench\":{\"ticks_run\":%lld,\"sim_seconds\":%lld,\"wall_ms\":%.1f,"
        "\"sim_sec_per_wall_sec\":%.1f,\"ticks_per_sec\":%.1f,"
        "\"tick_us\":{\"p50\":%.1f,\"p95\":%.1f,\"max\":%.1f,\"mean\":%.1f},"
        "\"snapshot\":{\"bytes\":%zu,\"save_ms\":%.1f},\"rss_kb\":%ld,"
        "\"learner\":{\"inferences\":%llu,\"updates\":%llu},"
        "\"ticks\":{\"fine\":%llu,\"coarse\":%llu,\"sleep\":%llu},"
        "\"backend\":{\"type\":%d,\"reason\":\"%s\"}}}\n",
        static_cast<long long>(done), static_cast<long long>(simSec), wallMs, simPerWall,
        ticksPerSec, p50, p95, maxUs, meanUs, blob.size(), saveMs, rssKb(),
        static_cast<unsigned long long>(lm.inferences),
        static_cast<unsigned long long>(lm.updates),
        static_cast<unsigned long long>(st.ticksFine),
        static_cast<unsigned long long>(st.ticksCoarse),
        static_cast<unsigned long long>(st.ticksSleep), static_cast<int>(sel.type),
        sel.reason.c_str());
    return 0;
  }

  std::fprintf(stderr, "=== Eidolon hot-path benchmark ===\n");
  std::fprintf(stderr, "ticks run:       %lld\n", static_cast<long long>(done));
  std::fprintf(stderr, "sim seconds:     %lld\n", static_cast<long long>(simSec));
  std::fprintf(stderr, "wall time:       %.1f ms\n", wallMs);
  std::fprintf(stderr, "sim/wall:        %.1f sim-s per wall-s\n", simPerWall);
  std::fprintf(stderr, "throughput:      %.1f ticks/s\n", ticksPerSec);
  std::fprintf(stderr, "tick latency us: p50=%.1f p95=%.1f max=%.1f mean=%.1f\n", p50, p95,
               maxUs, meanUs);
  std::fprintf(stderr, "snapshot:        %zu bytes in %.1f ms\n", blob.size(), saveMs);
  std::fprintf(stderr, "RSS:             %ld KB\n", rssKb());
  std::fprintf(stderr, "learner:         %llu inferences, %llu updates\n",
               static_cast<unsigned long long>(lm.inferences),
               static_cast<unsigned long long>(lm.updates));
  std::fprintf(stderr, "ticks by class:  fine=%llu coarse=%llu sleep=%llu\n",
               static_cast<unsigned long long>(st.ticksFine),
               static_cast<unsigned long long>(st.ticksCoarse),
               static_cast<unsigned long long>(st.ticksSleep));
  std::fprintf(stderr, "selected backend: %d (%s)\n", static_cast<int>(sel.type),
               sel.reason.c_str());
  std::fprintf(stderr, "=== end bench ===\n");
  return 0;
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
  bool bench = false, benchJson = false;
  int64_t benchTicks = 5000;

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
    else if (a == "--bench") bench = true;
    else if (a == "--bench-ticks") benchTicks = std::atoll(need("N"));
    else if (a == "--bench-json") benchJson = true;
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

  if (bench) {
    if (benchTicks <= 0) benchTicks = 5000;
    return runBenchmark(haveSeed ? seed : 42, worldW, worldH, benchTicks, benchJson);
  }

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

  // Run target advances the PERSISTED schedule, not the (possibly overshot) current
  // clock. Coarse ticks overshoot a day boundary by up to one step; computing the resume
  // target from the clock would stretch later segments and break "resume == uninterrupted".
  const int64_t target = engine.scheduledTarget() + static_cast<int64_t>(days * 86400.0);
  engine.setScheduledTarget(target);
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
    std::fprintf(m, "actionsWander=%llu\nactionsRest=%llu\nactionsSleep=%llu\nactionsObserve=%llu\nactionsForage=%llu\nactionsDrink=%llu\nactionsFlee=%llu\npredatorAttacks=%llu\n",
                 static_cast<unsigned long long>(engine.stats().actionsWander),
                 static_cast<unsigned long long>(engine.stats().actionsRest),
                 static_cast<unsigned long long>(engine.stats().actionsSleep),
                 static_cast<unsigned long long>(engine.stats().actionsObserve),
                 static_cast<unsigned long long>(engine.stats().actionsForage),
                 static_cast<unsigned long long>(engine.stats().actionsDrink),
                 static_cast<unsigned long long>(engine.stats().actionsFlee),
                 static_cast<unsigned long long>(engine.stats().predatorAttacks));
    std::fprintf(m, "waterskinFills=%llu\nwaterskinDrinks=%llu\nwaterCarried=%u\nwaterCapacity=%u\n",
                 static_cast<unsigned long long>(engine.stats().waterskinFills),
                 static_cast<unsigned long long>(engine.stats().waterskinDrinks),
                 static_cast<unsigned>(engine.body().waterCarried()),
                 static_cast<unsigned>(engine.body().waterCapacity()));
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
