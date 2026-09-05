// Adaptive simulation clock + fixed-size event queue.
// Sim time is integer seconds since the start of the organism's life.
#pragma once

#include <cmath>
#include <cstdint>
#include <queue>

namespace eidolon {

class SimClock {
public:
  void advance(int64_t dt) noexcept { t_ += dt; }
  int64_t now() const noexcept { return t_; }
  void set(int64_t t) noexcept { t_ = t; }

  int64_t day() const noexcept { return t_ / 86400; }
  // Seconds since midnight, [0, 86400).
  int64_t secondsOfDay() const noexcept { return t_ % 86400; }
  double hourOfDay() const noexcept { return static_cast<double>(secondsOfDay()) / 3600.0; }
  bool isDaytime(double dawn = 6.0, double dusk = 20.0) const noexcept {
    const double h = hourOfDay();
    return h >= dawn && h < dusk;
  }
  // Smooth 0..1 light envelope (0 = midnight, 1 = solar noon, 0.5 = 6am/6pm dawn-dusk
  // crossing). Cosine envelope so day/night ramps smoothly; used by the circadian
  // scheduler to bias sleep/activity toward the organism's diurnal phase.
  double daylight() const noexcept {
    return 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 * hourOfDay() / 24.0));
  }
  int seasonOfYear(int yearDays = 365) const noexcept { // 0=spring,1=summer,2=autumn,3=winter
    const int d = static_cast<int>(day()) % yearDays;
    return static_cast<int>(static_cast<long long>(d) * 4 / yearDays);
  }
  double yearFraction(int yearDays = 365) const noexcept { // [0,1) within the year
    return static_cast<double>(day() % yearDays) / static_cast<double>(yearDays);
  }

private:
  int64_t t_ = 0;
};

// Adaptive step sizes: fine during active/high-arousal states, coarse during routine,
// sleep-sized while asleep. Values in simulated seconds.
enum class StepKind : uint32_t { Fine = 1, Coarse = 10, Sleep = 30 };

// Event queue: bounded ring of scheduled events {time, kind, payload}.
class EventQueue {
public:
  struct Event {
    int64_t at = 0;
    uint16_t kind = 0;
    uint16_t payload = 0;
  };

  void push(const Event& e) noexcept {
    if (size_ < kCapacity) {
      buf_[size_++] = e;
      if (size_ > 1) { // crude order maintenance (small N)
        std::size_t i = size_ - 1;
        while (i > 0 && buf_[i].at < buf_[i - 1].at) {
          Event tmp = buf_[i];
          buf_[i] = buf_[i - 1];
          buf_[i - 1] = tmp;
          --i;
        }
      }
    }
  }
  // Pop the earliest event if it is due at or before `now`. Returns false if empty/not due.
  bool popDue(int64_t now, Event& out) noexcept {
    if (size_ == 0 || buf_[0].at > now) return false;
    out = buf_[0];
    --size_;
    if (size_ > 0) { // keep the ring sorted: shift the tail down
      for (std::size_t i = 0; i < size_; ++i) buf_[i] = buf_[i + 1];
    }
    return true;
  }
  int64_t nextDue() const noexcept {
    return size_ == 0 ? -1 : buf_[0].at;
  }
  std::size_t size() const noexcept { return size_; }

private:
  static constexpr std::size_t kCapacity = 64;
  Event buf_[kCapacity];
  std::size_t size_ = 0;
};

} // namespace eidolon
