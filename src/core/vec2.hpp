// Small integer 2D vector + Chebyshev distance, shared by the world and wildlife.
#ifndef EIDOLON_VEC2_HPP
#define EIDOLON_VEC2_HPP

namespace eidolon {

struct Vec2i {
  int x = 0;
  int y = 0;
  bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Vec2i& o) const { return !(*this == o); }
};

inline int distCheb(Vec2i a, Vec2i b) {
  const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
  const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
  return dx > dy ? dx : dy;
}

} // namespace eidolon

#endif // EIDOLON_VEC2_HPP