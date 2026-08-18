#include "harness.hpp"

#include "core/rng.hpp"

using namespace eidolon;

TEST(rng_same_seed_same_stream) {
  Rng a(42), b(42);
  for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next());
}

TEST(rng_different_seed_differs) {
  Rng a(42), b(43);
  bool differed = false;
  for (int i = 0; i < 100; ++i) {
    if (a.next() != b.next()) { differed = true; break; }
  }
  CHECK(differed);
}

TEST(rng_range_bounds) {
  Rng r(7);
  for (int i = 0; i < 10000; ++i) {
    CHECK(r.range(10) < 10);
    const int v = r.irange(-5, 5);
    CHECK(v >= -5 && v <= 5);
    const double u = r.unit();
    CHECK(u >= 0.0 && u < 1.0);
    const double d = r.range(-2.0, 3.0);
    CHECK(d >= -2.0 && d <= 3.0);
  }
}

TEST(rng_subsystem_streams_independent) {
  const Rng w1 = subsystemStream(42, Subsystem::World);
  const Rng w2 = subsystemStream(42, Subsystem::World);
  const Rng b1 = subsystemStream(42, Subsystem::Body);
  Rng w1c = w1, w2c = w2, b1c = b1;
  CHECK(w1c.next() == w2c.next());
  bool differed = false;
  for (int i = 0; i < 100; ++i) {
    if (w1c.next() != b1c.next()) { differed = true; break; }
  }
  CHECK(differed);
}

TEST(rng_state_roundtrip) {
  Rng a(99);
  for (int i = 0; i < 37; ++i) a.next();
  Rng b = Rng::fromState(a.state());
  for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
}
