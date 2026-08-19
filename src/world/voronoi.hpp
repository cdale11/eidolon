#ifndef EIDOLON_VORONOI_HPP
#define EIDOLON_VORONOI_HPP

#include <vector>
#include <cstdint>
#include <limits>

#include "core/vec2.hpp"
#include "core/rng.hpp"

namespace eidolon {

// Deterministic Voronoi/Delaunay for biome/territory tessellation,
// settlement placement, landmark connectivity (DESIGN §22, Phase 5 branch).
// Uses Fortune's algorithm (sweep-line) for O(n log n) Voronoi, then
// dual graph gives Delaunay triangulation. Fully seeded, bit-exact replay.

struct VoronoiSite {
  Vec2f pos;
  int id = 0;
  uint32_t color = 0; // biome/territory color
};

struct VoronoiEdge {
  Vec2f a, b; // edge endpoints
  int siteA = -1, siteB = -1; // adjacent sites
};

struct VoronoiCell {
  int siteId = -1;
  std::vector<Vec2f> vertices; // CCW polygon vertices
  std::vector<int> neighborSites; // adjacent site IDs
};

// Compute Voronoi diagram for a set of sites within [0,W]x[0,H].
// Returns cells (one per site) and edges.
void voronoi(const std::vector<VoronoiSite>& sites, int W, int H,
             std::vector<VoronoiCell>& cells,
             std::vector<VoronoiEdge>& edges);

// Delaunay triangulation: dual of Voronoi. Returns triangle indices (triples of site IDs).
void delaunay(const std::vector<VoronoiSite>& sites, const std::vector<VoronoiEdge>& edges,
              std::vector<std::array<int, 3>>& triangles);

// Sample Poisson-disc points (blue noise) for site placement: good for biome seeds,
// wildlife den placement, etc. Returns sites with random IDs/colors.
std::vector<VoronoiSite> poissonDiscSamples(int W, int H, float minDist, int maxSamples, Rng& r);

// Assign each grid cell to its nearest Voronoi site (territory/biome assignment).
// Output: siteIdGrid[y * W + x] = site ID (or -1 if outside).
void assignTerritories(const std::vector<VoronoiSite>& sites, int W, int H,
                       std::vector<int>& siteIdGrid);

} // namespace eidolon

#endif // EIDOLON_VORONOI_HPP