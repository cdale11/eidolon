#ifndef EIDOLON_PROCGEN_HPP
#define EIDOLON_PROCGEN_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <array>

#include "core/vec2.hpp"
#include "core/rng.hpp"
#include "world/world.hpp"

namespace eidolon {

// Procedural generation for ruins, landmarks, named places, semantically tagged objects.
// Extends existing seedable world gen. Fully deterministic, bit-exact replay.
// Phase 5 branch (DESIGN §22).

enum class LandmarkType : uint8_t {
  None = 0,
  Ruin = 1,
  Shrine = 2,
  Cave = 3,
  Spring = 4,
  AncientTree = 5,
  StoneCircle = 6,
  BurialMound = 7,
  Shipwreck = 8,
  Campfire = 9,
  Signpost = 10,
};

enum class ObjectTag : uint32_t {
  None = 0,
  // Semantic tags for memory ground truth
  Edible = 1 << 0,
  Medicinal = 1 << 1,
  Toxic = 1 << 2,
  Tool = 1 << 3,
  Weapon = 1 << 4,
  Shelter = 1 << 5,
  Water = 1 << 6,
  Fire = 1 << 7,
  Danger = 1 << 8,
  Safe = 1 << 9,
  Social = 1 << 10,
  Exploration = 1 << 11,
  Resource = 1 << 12,
  Landmark = 1 << 13,
  Hidden = 1 << 14,
  Quest = 1 << 15,
};

struct Landmark {
  LandmarkType type = LandmarkType::None;
  Vec2i pos;
  std::string name;
  uint32_t tags = 0; // ObjectTag bitmask
  int size = 1; // radius in tiles
  int importance = 0; // 0-100
  uint64_t seed = 0; // for deterministic sub-generation
};

struct Ruin {
  Vec2i pos;
  int radius = 5;
  std::string name;
  uint32_t tags = 0;
  int roomCount = 0;
  int depth = 0; // 0 = surface, >0 = underground levels
  std::vector<Vec2i> entrances;
};

struct NamedPlace {
  std::string name;
  Vec2i center;
  int radius = 10;
  uint32_t tags = 0;
  std::string description;
};

class ProceduralGenerator {
public:
  ProceduralGenerator() = default;
  explicit ProceduralGenerator(const World& world, uint64_t seed);

  void setWorld(const World& world) { world_ = &world; }
  void setSeed(uint64_t seed) { seed_ = seed; rng_ = Rng(seed); }

  // Generate landmarks across the world
  std::vector<Landmark> generateLandmarks(int count, int minDist = 20);

  // Generate ruins at specific locations or randomly
  std::vector<Ruin> generateRuins(int count, int minDist = 30);

  // Generate named places (regions with semantic meaning)
  std::vector<NamedPlace> generateNamedPlaces(int count);

  // Tag existing objects (plants, water sources, etc.) with semantic tags
  void tagObjects();

  // Get semantic tags for a position (from nearby landmarks/objects)
  uint32_t getSemanticTags(const Vec2i& pos, int radius) const;

  // Serialize all generated content
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  const World* world_ = nullptr;
  uint64_t seed_ = 0;
  Rng rng_;

  std::vector<Landmark> landmarks_;
  std::vector<Ruin> ruins_;
  std::vector<NamedPlace> namedPlaces_;

  // Name generation
  std::string generateLandmarkName(LandmarkType type);
  std::string generateRuinName();
  std::string generatePlaceName();

  // Check if position is valid for placement
  bool canPlaceAt(const Vec2i& pos, int radius, const std::vector<Vec2i>& existing) const;
};

// Procedural name components
extern const std::vector<std::string> g_ruinPrefixes;
extern const std::vector<std::string> g_ruinSuffixes;
extern const std::vector<std::string> g_placePrefixes;
extern const std::vector<std::string> g_placeSuffixes;
extern const std::vector<std::string> g_landmarkNames;

} // namespace eidolon

#endif // EIDOLON_PROCGEN_HPP