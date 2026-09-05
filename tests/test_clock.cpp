#include "harness.hpp"

#include "core/clock.hpp"

using namespace eidolon;

TEST(clock_advance_and_day) {
  SimClock c;
  c.advance(0);
  CHECK_EQ(c.day(), 0);
  c.advance(86400);
  CHECK_EQ(c.day(), 1);
  CHECK_EQ(c.secondsOfDay(), 0);
  c.advance(3661);
  CHECK_EQ(c.secondsOfDay(), 3661);
  CHECK(c.hourOfDay() > 1.0 && c.hourOfDay() < 1.1);
}

TEST(clock_daytime) {
  SimClock c;
  c.advance(7 * 3600); // 07:00
  CHECK(c.isDaytime());
  c.advance(-3600); // 06:00
  CHECK(c.isDaytime(6.0, 20.0));
  c.advance(-2 * 3600); // 04:00
  CHECK(!c.isDaytime());
}

TEST(clock_daylight_envelope) {
  SimClock c;
  c.set(0); // midnight
  CHECK(c.daylight() < 0.05);
  c.set(6 * 3600); // 06:00 dawn
  CHECK(c.daylight() > 0.45 && c.daylight() < 0.55);
  c.set(12 * 3600); // noon
  CHECK(c.daylight() > 0.95);
  c.set(18 * 3600); // 18:00 dusk
  CHECK(c.daylight() > 0.45 && c.daylight() < 0.55);
  c.set(24 * 3600); // next midnight
  CHECK(c.daylight() < 0.05);
  // diurnal: strictly more light at noon than at dawn or midnight
  double dawnLight, noonLight, midnightLight;
  c.set(6 * 3600); dawnLight = c.daylight();
  c.set(12 * 3600); noonLight = c.daylight();
  c.set(0); midnightLight = c.daylight();
  CHECK(noonLight > dawnLight);
  CHECK(dawnLight > midnightLight);
}

TEST(clock_season_cycles) {
  SimClock c;
  CHECK_EQ(c.seasonOfYear(), 0); // day 0 = spring
  c.advance(365 * 86400);
  CHECK_EQ(c.seasonOfYear(), 0);
  c.advance(92 * 86400);
  CHECK_EQ(c.seasonOfYear(), 1); // summer
}

TEST(event_queue_ordering) {
  EventQueue q;
  q.push({100, 2, 0});
  q.push({50, 1, 0});
  q.push({75, 3, 0});
  CHECK_EQ(q.nextDue(), 50);
  EventQueue::Event e;
  CHECK(q.popDue(50, e));
  CHECK_EQ(e.kind, 1);
  CHECK(!q.popDue(74, e));
  CHECK(q.popDue(75, e));
  CHECK_EQ(e.kind, 3);
  CHECK(q.popDue(100, e));
  CHECK_EQ(e.kind, 2);
  CHECK(!q.popDue(1000, e));
}

TEST(event_queue_capacity_bounded) {
  EventQueue q;
  for (int i = 0; i < 200; ++i) q.push({static_cast<int64_t>(i), 1, 0});
  CHECK(q.size() <= 64);
}
