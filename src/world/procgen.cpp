#include "world/procgen.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

// Name component tables
const std::vector<std::string> g_ruinPrefixes = {
  "Ancient", "Forgotten", "Lost", "Sunken", "Broken", "Silent",
  "Crumbling", "Hollow", "Faded", "Weathered", "Timeworn", "Desolate"
};
const std::vector<std::string> g_ruinSuffixes = {
  "Keep", "Tower", "Temple", "Sanctum", "Citadel", "Fortress",
  "Hall", "Chamber", "Vault", "Crypt", "Archive", "Monument"
};
const std::vector<std::string> g_placePrefixes = {
  "Valley", "Hollow", "Grove", "Clearing", "Ridge", "Bluff",
  "Basin", "Dale", "Fen", "Moor", "Heath", "Reach"
};
const std::vector<std::string> g_placeSuffixes = {
  "of Echoes", "of Whispers", "of Shadows", "of Light",
  "of the Ancients", "of the Fallen", "of Memories",
  "of the Wild", "of Stones", "of Waters"
};
const std::vector<std::string> g_landmarkNames = {
  "Standing Stone", "Ancient Well", "Druid Circle", "Fairy Ring",
  "Old Oak", "Watcher Tree", "Spirit Spring", "Bone Pit",
  "Rune Stone", "Cairn", "Menhir", "Dolmen"
};

namespace eidolon {

ProceduralGenerator::ProceduralGenerator(const World& world, uint64_t seed)
    : world_(&world), seed_(seed), rng_(seed) {}


bool ProceduralGenerator::canPlaceAt(const Vec2i& pos, int radius,
                                     const std::vector<Vec2i>& existing) const {
  if (!world_->grid().inBounds(pos.x, pos.y)) return false;
  if (!world_->grid().walkable(pos.x, pos.y)) return false;

  // Check distance from existing
  for (const auto& e : existing) {
    int dx = pos.x - e.x;
    int dy = pos.y - e.y;
    if (dx * dx + dy * dy < radius * radius) return false;
  }
  return true;
}

std::string ProceduralGenerator::generateLandmarkName(LandmarkType type) {
  if (type == LandmarkType::Ruin) return generateRuinName();
  if (type == LandmarkType::Shrine) return "Shrine of " + g_landmarkNames[rng_.irange(0, g_landmarkNames.size() - 1)];
  if (type == LandmarkType::Cave) return "Cave of " + g_placeSuffixes[rng_.irange(0, g_placeSuffixes.size() - 1)];
  if (type == LandmarkType::AncientTree) return g_landmarkNames[rng_.irange(0, g_landmarkNames.size() - 1)];
  if (type == LandmarkType::StoneCircle) return "Circle of " + g_placeSuffixes[rng_.irange(0, g_placeSuffixes.size() - 1)];
  if (type == LandmarkType::BurialMound) return "Mound of " + g_landmarkNames[rng_.irange(0, g_landmarkNames.size() - 1)];
  if (type == LandmarkType::Spring) return "Spring of " + g_placeSuffixes[rng_.irange(0, g_placeSuffixes.size() - 1)];
  return g_landmarkNames[rng_.irange(0, g_landmarkNames.size() - 1)];
}

std::string ProceduralGenerator::generateRuinName() {
  return g_ruinPrefixes[rng_.irange(0, g_ruinPrefixes.size() - 1)] + " " +
         g_ruinSuffixes[rng_.irange(0, g_ruinSuffixes.size() - 1)];
}

std::string ProceduralGenerator::generatePlaceName() {
  return g_placePrefixes[rng_.irange(0, g_placePrefixes.size() - 1)] + " " +
         g_placeSuffixes[rng_.irange(0, g_placeSuffixes.size() - 1)];
}

std::vector<Landmark> ProceduralGenerator::generateLandmarks(int count, int minDist) {
  landmarks_.clear();
  std::vector<Vec2i> placed;
  LandmarkType types[] = {
    LandmarkType::Ruin, LandmarkType::Shrine, LandmarkType::Cave,
    LandmarkType::AncientTree, LandmarkType::StoneCircle,
    LandmarkType::BurialMound, LandmarkType::Spring
  };

  int attempts = 0;
  const int maxAttempts = count * 50;
  int typeIdx = 0;

  while (static_cast<int>(landmarks_.size()) < count && attempts < maxAttempts) {
    ++attempts;
    int w = world_->grid().width();
    int h = world_->grid().height();

    // Prefer walkable areas near interesting terrain
    int x = rng_.irange(5, w - 5);
    int y = rng_.irange(5, h - 5);
    Vec2i pos{x, y};

    if (!canPlaceAt(pos, minDist, placed)) continue;

    LandmarkType type = types[typeIdx % 7];
    typeIdx++;

    Landmark lm;
    lm.type = type;
    lm.pos = pos;
    lm.name = generateLandmarkName(type);
    lm.tags = static_cast<uint32_t>(ObjectTag::Landmark);
    lm.size = rng_.irange(1, 3);
    lm.importance = rng_.irange(10, 100);
    lm.seed = rng_.next();

    // Add semantic tags based on type
    switch (type) {
      case LandmarkType::Ruin:
        lm.tags |= static_cast<uint32_t>(ObjectTag::Exploration) | static_cast<uint32_t>(ObjectTag::Danger);
        break;
      case LandmarkType::Shrine:
        lm.tags |= static_cast<uint32_t>(ObjectTag::Social) | static_cast<uint32_t>(ObjectTag::Safe);
        break;
      case LandmarkType::Cave:
        lm.tags |= static_cast<uint32_t>(ObjectTag::Exploration) | static_cast<uint32_t>(ObjectTag::Danger) | static_cast<uint32_t>(ObjectTag::Resource);
        break;
      case LandmarkType::Spring:
        lm.tags |= static_cast<uint32_t>(ObjectTag::Water) | static_cast<uint32_t>(ObjectTag::Safe);
        break;
      case LandmarkType::AncientTree:
        lm.tags |= static_cast<uint32_t>(ObjectTag::Landmark) | static_cast<uint32_t>(ObjectTag::Exploration);
        break;
      default:
        break;
    }

    landmarks_.push_back(lm);
    placed.push_back(pos);
  }
  return landmarks_;
}

std::vector<Ruin> ProceduralGenerator::generateRuins(int count, int minDist) {
  ruins_.clear();
  std::vector<Vec2i> placed;
  int attempts = 0;
  const int maxAttempts = count * 50;

  while (static_cast<int>(ruins_.size()) < count && attempts < maxAttempts) {
    ++attempts;
    int w = world_->grid().width();
    int h = world_->grid().height();

    int x = rng_.irange(10, w - 10);
    int y = rng_.irange(10, h - 10);
    Vec2i pos{x, y};

    if (!canPlaceAt(pos, minDist, placed)) continue;

    Ruin r;
    r.pos = pos;
    r.name = generateRuinName();
    r.radius = rng_.irange(3, 8);
    r.tags = static_cast<uint32_t>(ObjectTag::Landmark) |
             static_cast<uint32_t>(ObjectTag::Exploration) |
             static_cast<uint32_t>(ObjectTag::Danger);
    r.roomCount = rng_.irange(3, 12);
    r.depth = rng_.irange(0, 2); // 0=surface, 1=one level down, 2=deep
    r.entrances.push_back(pos);

    ruins_.push_back(r);
    placed.push_back(pos);
  }
  return ruins_;
}

std::vector<NamedPlace> ProceduralGenerator::generateNamedPlaces(int count) {
  namedPlaces_.clear();
  std::vector<Vec2i> placed;
  int attempts = 0;
  const int maxAttempts = count * 30;

  while (static_cast<int>(namedPlaces_.size()) < count && attempts < maxAttempts) {
    ++attempts;
    int w = world_->grid().width();
    int h = world_->grid().height();

    int x = rng_.irange(15, w - 15);
    int y = rng_.irange(15, h - 15);
    Vec2i pos{x, y};

    if (!canPlaceAt(pos, 25, placed)) continue;

    NamedPlace np;
    np.name = generatePlaceName();
    np.center = pos;
    np.radius = rng_.irange(8, 20);
    np.tags = static_cast<uint32_t>(ObjectTag::Exploration) | static_cast<uint32_t>(ObjectTag::Social);
    np.description = "A place known as " + np.name;

    namedPlaces_.push_back(np);
    placed.push_back(pos);
  }
  return namedPlaces_;
}

void ProceduralGenerator::tagObjects() {
  // Tag plants, water sources, etc. based on nearby landmarks
  for (auto& lm : landmarks_) {
    (void)lm;
    // Could tag nearby plants as medicinal/edible based on landmark type
    // This would iterate over world_->plants() and world_->waterSources()
  }
}

uint32_t ProceduralGenerator::getSemanticTags(const Vec2i& pos, int radius) const {
  uint32_t tags = 0;
  for (const auto& lm : landmarks_) {
    int dx = pos.x - lm.pos.x;
    int dy = pos.y - lm.pos.y;
    if (dx * dx + dy * dy <= radius * radius) {
      tags |= lm.tags;
    }
  }
  for (const auto& r : ruins_) {
    int dx = pos.x - r.pos.x;
    int dy = pos.y - r.pos.y;
    if (dx * dx + dy * dy <= radius * radius) {
      tags |= r.tags;
    }
  }
  for (const auto& np : namedPlaces_) {
    int dx = pos.x - np.center.x;
    int dy = pos.y - np.center.y;
    if (dx * dx + dy * dy <= np.radius * np.radius) {
      tags |= np.tags;
    }
  }
  return tags;
}

void ProceduralGenerator::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(landmarks_.size()));
  for (const auto& lm : landmarks_) {
    w.u32(lm.pos.x);
    w.u32(lm.pos.y);
    w.u8(static_cast<uint8_t>(lm.type));
    w.str(lm.name);
    w.u32(lm.tags);
    w.u32(lm.size);
    w.u32(lm.importance);
    w.u64(lm.seed);
  }
  w.u32(static_cast<uint32_t>(ruins_.size()));
  for (const auto& r : ruins_) {
    w.u32(r.pos.x);
    w.u32(r.pos.y);
    w.str(r.name);
    w.u32(r.radius);
    w.u32(r.tags);
    w.u32(r.roomCount);
    w.u32(r.depth);
    w.u32(static_cast<uint32_t>(r.entrances.size()));
    for (const auto& e : r.entrances) {
      w.u32(e.x);
      w.u32(e.y);
    }
  }
  w.u32(static_cast<uint32_t>(namedPlaces_.size()));
  for (const auto& np : namedPlaces_) {
    w.str(np.name);
    w.u32(np.center.x);
    w.u32(np.center.y);
    w.u32(np.radius);
    w.u32(np.tags);
    w.str(np.description);
  }
}

bool ProceduralGenerator::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  landmarks_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u32(*reinterpret_cast<uint32_t*>(&landmarks_[i].pos.x)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&landmarks_[i].pos.y)) ||
        !r.u8(reinterpret_cast<uint8_t&>(landmarks_[i].type)) ||
        !r.str(landmarks_[i].name) || !r.u32(landmarks_[i].tags) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&landmarks_[i].size)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&landmarks_[i].importance)) ||
        !r.u64(landmarks_[i].seed))
      return false;
  }
  if (!r.u32(n)) return false;
  ruins_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].pos.x)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].pos.y)) ||
        !r.str(ruins_[i].name) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].radius)) ||
        !r.u32(ruins_[i].tags) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].roomCount)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].depth)))
      return false;
    uint32_t m;
    if (!r.u32(m)) return false;
    ruins_[i].entrances.resize(static_cast<size_t>(m));
    for (size_t j = 0; j < static_cast<size_t>(m); ++j) {
      if (!r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].entrances[j].x)) ||
          !r.u32(*reinterpret_cast<uint32_t*>(&ruins_[i].entrances[j].y)))
        return false;
    }
  }
  if (!r.u32(n)) return false;
  namedPlaces_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.str(namedPlaces_[i].name) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&namedPlaces_[i].center.x)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&namedPlaces_[i].center.y)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&namedPlaces_[i].radius)) ||
        !r.u32(namedPlaces_[i].tags) || !r.str(namedPlaces_[i].description))
      return false;
  }
  return true;
}

} // namespace eidolon