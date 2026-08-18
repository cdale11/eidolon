// Simulation engine: orchestrates world → body → decision → action → log per tick with
// adaptive step sizes. Phase 1 uses a heuristic decision policy; learning lands in
// Phase 3. The tick path is allocation-light and noexcept.
#pragma once

#include <cstdint>
#include <string>

#include "body/physiology.hpp"
#include "core/clock.hpp"
#include "core/log.hpp"
#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "mind/memory.hpp"
#include "world/world.hpp"

namespace eidolon {

enum class Action : uint8_t {
  Wander = 0,
  Rest = 1,
  Sleep = 2,
  Observe = 3,
  Forage = 4,
  Drink = 5,
};

class Engine {
public:
  struct Stats {
    uint64_t ticksFine = 0;
    uint64_t ticksCoarse = 0;
    uint64_t ticksSleep = 0;
    uint64_t actionsWander = 0;
    uint64_t actionsRest = 0;
    uint64_t actionsSleep = 0;
    uint64_t actionsObserve = 0;
    uint64_t actionsForage = 0;
    uint64_t actionsDrink = 0;
    uint64_t berriesEaten = 0;
    uint64_t drinks = 0;
  };

  Engine() = default;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Fresh organism. `masterSeed` drives every subsystem stream.
  void init(uint64_t masterSeed, bool deterministic, int worldW, int worldH);
  bool isAlive() const { return world_.organismAlive() && body_.alive(); }

  // Advance the simulation by one tick (step size chosen adaptively). noexcept hot path.
  // Returns the action chosen this tick.
  Action tick() noexcept;

  // tick() plus per-tick logging (state transitions, status lines, events). Used by
  // runDays and by the headless CLI so both trace identically.
  void tickAndLog(EventLog& log) noexcept;

  // Run until `days` simulated days have elapsed (or the organism dies). Returns false
  // only if the organism died before the end.
  bool runDays(double days, EventLog& log, std::string& whyStopped);

  const SimClock& clock() const { return clock_; }
  const World& world() const { return world_; }
  const Physiology& body() const { return body_; }
  const MemoryRing& memory() const { return memory_; }
  const Stats& stats() const { return stats_; }
  uint64_t masterSeed() const { return masterSeed_; }
  bool deterministic() const { return deterministic_; }

  // Snapshot: serialize/restore the entire engine state (identity-preserving).
  std::vector<uint8_t> snapshot() const;
  bool restore(const std::vector<uint8_t>& blob, std::string& err);

  bool saveFile(const std::string& path, std::string& err) const;
  bool loadFile(const std::string& path, std::string& err);

  void setStatusInterval(int64_t seconds) { statusInterval_ = seconds; }

  // Movement helpers (deterministic greedy best-step with random escape fallback).
  // Try to move one tile toward `target` (see moveToward in engine.cpp). Returns true
  // if the organism moved.
  bool moveToward(Vec2i target) noexcept;

private:
  void stepClock(StepKind kind) noexcept;
  void logStatus(EventLog& log) noexcept;
  Action decide() noexcept;
  void execute(Action a) noexcept;
  void checkEvents(EventLog* log) noexcept;
  void recordEpisode(EventKind kind, uint8_t detail, double importance) noexcept;

  void serializeState(BinaryWriter& w) const;
  bool deserializeState(BinaryReader& r, std::string& err);

  SimClock clock_;
  World world_;
  Physiology body_;
  MemoryRing memory_;
  EventQueue events_;
  Stats stats_;
  uint64_t masterSeed_ = 0;
  bool deterministic_ = false;
  bool died_ = false;
  int64_t lastStatusAt_ = 0;
  int64_t statusInterval_ = 600; // sim-seconds between status lines
  int prevMode_ = 0; // last logged life mode: 0=active,1=rest,2=sleep
  bool resting_ = false; // hysteresis for rest mode (prevents boundary oscillation)
  // Per-subsystem RNG streams (isolated so subsystem randomness never perturbs others).
  Rng rngWorld_, rngWeather_, rngBody_, rngCognition_, rngEvents_;
};

} // namespace eidolon
