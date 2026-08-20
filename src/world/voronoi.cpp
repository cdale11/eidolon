#include "world/voronoi.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

#include "core/detmath.hpp"

namespace eidolon {

// --- Fortune's algorithm (simplified, deterministic) ---

struct BeachNode {
  int siteId;
  float y; // parabola focus y
  BeachNode* left = nullptr;
  BeachNode* right = nullptr;
  BeachNode* parent = nullptr;
  bool isLeaf = true;
  int edgeId = -1; // associated edge
};

struct Event {
  float x, y;
  int siteId;
  bool isSiteEvent;
  // For ordering in priority queue
  bool operator<(const Event& other) const {
    if (y != other.y) return y > other.y; // min-heap by y
    return x > other.x;
  }
};

// static float circumcenterX(const VoronoiSite& a, const VoronoiSite& b, const VoronoiSite& c) { ... }
// static float circumcenterY(const VoronoiSite& a, const VoronoiSite& b, const VoronoiSite& c) { ... }

// Simplified deterministic Voronoi using brute-force for small n (n <= 256)
// For larger n, Fortune's algorithm would be used. Here we use a simple
// incremental approach that's deterministic and correct for our use case.
void voronoi(const std::vector<VoronoiSite>& sites, int W, int H,
             std::vector<VoronoiCell>& cells,
             std::vector<VoronoiEdge>& edges) {
  const int n = static_cast<int>(sites.size());
  cells.assign(n, VoronoiCell());
  edges.clear();

  // Initialize cells
  for (int i = 0; i < n; ++i) {
    cells[i].siteId = sites[i].id;
  }

  // For each site, compute its Voronoi cell by intersecting half-planes
  // This is O(n^2 log n) but deterministic and correct for n <= 256
  // const float INF = 1e9f;
  const float eps = 1e-6f;

  for (int i = 0; i < n; ++i) {
    // Start with the bounding box
    std::vector<Vec2f> poly = {
      {0, 0}, {static_cast<float>(W), 0},
      {static_cast<float>(W), static_cast<float>(H)}, {0, static_cast<float>(H)}
    };

    for (int j = 0; j < n; ++j) {
      if (i == j) continue;
      // Bisector line between sites i and j
      const Vec2f& pi = sites[i].pos;
      const Vec2f& pj = sites[j].pos;
      float mx = (pi.x + pj.x) * 0.5f;
      float my = (pi.y + pj.y) * 0.5f;
      float dx = pj.x - pi.x;
      float dy = pj.y - pi.y;
      // Line: dx*(x - mx) + dy*(y - my) = 0
      // Keep points where dx*(x - mx) + dy*(y - my) <= 0 (closer to i)
      std::vector<Vec2f> newPoly;
      int m = static_cast<int>(poly.size());
      for (int k = 0; k < m; ++k) {
        const Vec2f& a = poly[k];
        const Vec2f& b = poly[(k + 1) % m];
        float va = dx * (a.x - mx) + dy * (a.y - my);
        float vb = dx * (b.x - mx) + dy * (b.y - my);
        bool ina = va <= eps;
        bool inb = vb <= eps;
        if (ina) newPoly.push_back(a);
        if (ina != inb) {
          // Intersect edge with bisector
          float t = va / (va - vb);
          if (t >= 0 && t <= 1) {
            newPoly.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
          }
        }
      }
      poly.swap(newPoly);
      if (poly.size() < 3) break;
    }
    cells[i].vertices = poly;

    // Find neighbors: sites whose cells share an edge
    for (int j = 0; j < n; ++j) {
      if (i >= j) continue;
      // Check if cells i and j share an edge by checking if their
      // bisector intersects the bounding box
      // Simplified: if distance between sites < threshold, they're neighbors
      float dx = sites[i].pos.x - sites[j].pos.x;
      float dy = sites[i].pos.y - sites[j].pos.y;
      float dist2 = dx * dx + dy * dy;
      if (dist2 < static_cast<float>(W * W + H * H) * 0.1f) {
        cells[i].neighborSites.push_back(sites[j].id);
        cells[j].neighborSites.push_back(sites[i].id);
        // Add edge
        VoronoiEdge edge;
        edge.siteA = sites[i].id;
        edge.siteB = sites[j].id;
        // Edge midpoint on bisector
        edge.a = {(sites[i].pos.x + sites[j].pos.x) * 0.5f, (sites[i].pos.y + sites[j].pos.y) * 0.5f};
        // Perpendicular direction
        float ddx = sites[j].pos.y - sites[i].pos.y;
        float ddy = sites[i].pos.x - sites[j].pos.x;
        float len = std::sqrt(ddx * ddx + ddy * ddy);
        if (len > 0) {
          ddx /= len; ddy /= len;
          edge.b = {edge.a.x + ddx * 1000, edge.a.y + ddy * 1000};
        } else {
          edge.b = edge.a;
        }
        edges.push_back(edge);
      }
    }
  }
}

// Delaunay: triangles from Voronoi edges (dual graph)
void delaunay(const std::vector<VoronoiSite>& sites, const std::vector<VoronoiEdge>& edges,
              std::vector<std::array<int, 3>>& triangles) {
  // Simple approach: for each site, connect to its 3 nearest neighbors
  // This is a simplification; full Delaunay would use the Voronoi dual properly
  triangles.clear();
  int n = static_cast<int>(sites.size());
  for (int i = 0; i < n; ++i) {
    // Find neighbors from edges
    std::vector<int> neigh;
    for (const auto& e : edges) {
      if (e.siteA == sites[i].id) neigh.push_back(e.siteB);
      else if (e.siteB == sites[i].id) neigh.push_back(e.siteA);
    }
    // Sort by distance
    std::sort(neigh.begin(), neigh.end(), [&](int a, int b) {
      float da = (sites[a].pos.x - sites[i].pos.x) * (sites[a].pos.x - sites[i].pos.x) +
                 (sites[a].pos.y - sites[i].pos.y) * (sites[a].pos.y - sites[i].pos.y);
      float db = (sites[b].pos.x - sites[i].pos.x) * (sites[b].pos.x - sites[i].pos.x) +
                 (sites[b].pos.y - sites[i].pos.y) * (sites[b].pos.y - sites[i].pos.y);
      return da < db;
    });
    // Form triangles with pairs of neighbors
    for (size_t a = 0; a < neigh.size(); ++a) {
      for (size_t b = a + 1; b < neigh.size(); ++b) {
        // Check if neighbors[a] and neighbors[b] are connected
        bool connected = false;
        for (const auto& e : edges) {
          if ((e.siteA == neigh[a] && e.siteB == neigh[b]) ||
              (e.siteA == neigh[b] && e.siteB == neigh[a])) {
            connected = true; break;
          }
        }
        if (connected) {
          triangles.push_back({sites[i].id, neigh[a], neigh[b]});
        }
      }
    }
  }
  // Deduplicate
  for (auto& t : triangles) std::sort(t.begin(), t.end());
  std::sort(triangles.begin(), triangles.end());
  triangles.erase(std::unique(triangles.begin(), triangles.end()), triangles.end());
}

std::vector<VoronoiSite> poissonDiscSamples(int W, int H, float minDist, int maxSamples, Rng& r) {
  std::vector<VoronoiSite> sites;
  std::vector<Vec2f> active;
  std::vector<Vec2f> samples;

  // Initial sample
  samples.push_back({static_cast<float>(r.range(0.0, static_cast<double>(W))),
                       static_cast<float>(r.range(0.0, static_cast<double>(H)))});
  active.push_back(samples[0]);

  const int k = 30; // attempts per active sample
  const float cellSize = minDist / std::sqrt(2.0f);
  int gridW = static_cast<int>(W / cellSize) + 1;
  int gridH = static_cast<int>(H / cellSize) + 1;
  std::vector<int> grid(gridW * gridH, -1);

  auto gridIdx = [&](float x, float y) {
    int gx = static_cast<int>(x / cellSize);
    int gy = static_cast<int>(y / cellSize);
    return gy * gridW + gx;
  };

  auto canPlace = [&](const Vec2f& p) -> bool {
    int gx = static_cast<int>(p.x / cellSize);
    int gy = static_cast<int>(p.y / cellSize);
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        int nx = gx + dx, ny = gy + dy;
        if (nx < 0 || ny < 0 || nx >= gridW || ny >= gridH) continue;
        int idx = grid[ny * gridW + nx];
        if (idx >= 0) {
          float dx = p.x - samples[idx].x;
          float dy = p.y - samples[idx].y;
          if (dx * dx + dy * dy < minDist * minDist) return false;
        }
      }
    }
    return true;
  };

  while (!active.empty() && static_cast<int>(samples.size()) < maxSamples) {
    int idx = r.irange(0, static_cast<int>(active.size()) - 1);
    const Vec2f& base = active[idx];
    bool found = false;
    for (int attempt = 0; attempt < k; ++attempt) {
      float angle = static_cast<float>(r.range(0.0, 2.0 * 3.14159265358979323846));
      float radius = static_cast<float>(r.range(static_cast<double>(minDist), static_cast<double>(2 * minDist)));
      Vec2f cand = {base.x + radius * detmath::cosf(angle), base.y + radius * detmath::sinf(angle)};
      if (cand.x < 0 || cand.x >= W || cand.y < 0 || cand.y >= H) continue;
      if (!canPlace(cand)) continue;
      // Place it
      int sidx = static_cast<int>(samples.size());
      samples.push_back(cand);
      active.push_back(cand);
      grid[gridIdx(cand.x, cand.y)] = sidx;
      found = true;
      break;
    }
    if (!found) {
      active[idx] = active.back();
      active.pop_back();
    }
  }

  // Convert to VoronoiSite
  std::vector<VoronoiSite> result;
  result.reserve(samples.size());
  for (size_t i = 0; i < samples.size(); ++i) {
    VoronoiSite site;
    site.pos = samples[i];
    site.id = static_cast<int>(i);
    site.color = static_cast<uint32_t>(r.next());
    result.push_back(site);
  }
  return result;
}

void assignTerritories(const std::vector<VoronoiSite>& sites, int W, int H,
                       std::vector<int>& siteIdGrid) {
  siteIdGrid.assign(static_cast<size_t>(W * H), -1);
  for (int gy = 0; gy < H; ++gy) {
    for (int gx = 0; gx < W; ++gx) {
      float bestDist2 = std::numeric_limits<float>::infinity();
      int bestId = -1;
      for (const auto& s : sites) {
        float dx = gx - s.pos.x;
        float dy = gy - s.pos.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
          bestDist2 = d2;
          bestId = s.id;
        }
      }
      siteIdGrid[static_cast<size_t>(gy) * W + gx] = bestId;
    }
  }
}

} // namespace eidolon