#include "body/construction.hpp"
#include <algorithm>
#include <cmath>

namespace eidolon {

void Structure::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.u8(static_cast<uint8_t>(type));
  w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.x)));
  w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.y)));
  w.u8(rotation);
  w.u8(static_cast<uint8_t>(state));
  w.u32(progress);
  w.u32(maxProgress);
  w.u32(health);
  w.u32(maxHealth);
  w.u64(createdAt);
  w.u64(completedAt);
  w.u32(static_cast<uint32_t>(builderIds.size()));
  for (uint32_t bid : builderIds) w.u32(bid);
  w.u32(static_cast<uint32_t>(occupiedTiles.size()));
  for (const auto& t : occupiedTiles) {
    w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&t.x)));
    w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&t.y)));
  }
}

bool Structure::deserialize(struct BinaryReader& r) {
  if (!r.u32(id)) return false;
  uint8_t t, rot, s;
  if (!r.u8(t) ||
      !r.u32(*reinterpret_cast<uint32_t*>(&position.x)) ||
      !r.u32(*reinterpret_cast<uint32_t*>(&position.y)) ||
      !r.u8(rot) || !r.u8(s) || !r.u32(progress) || !r.u32(maxProgress) ||
      !r.u32(health) || !r.u32(maxHealth) || !r.u64(createdAt) ||
      !r.u64(completedAt))
    return false;
  type = static_cast<StructureType>(t);
  rotation = rot;
  state = static_cast<StructureState>(s);
  uint32_t n;
  if (!r.u32(n)) return false;
  builderIds.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u32(builderIds[i])) return false;
  }
  if (!r.u32(n)) return false;
  occupiedTiles.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u32(*reinterpret_cast<uint32_t*>(&occupiedTiles[i].x)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&occupiedTiles[i].y))) return false;
  }
  return true;
}

void StructureBlueprint::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(type));
  w.str(name);
  w.u32(static_cast<uint32_t>(materials.size()));
  for (const auto& m : materials) m.serialize(w);
  w.u8(static_cast<uint8_t>(requiredTool));
  w.u8(static_cast<uint8_t>(requiredSkill));
  w.f32(skillThreshold);
  w.u32(maxProgress);
  w.u32(maxHealth);
  w.u32(static_cast<uint32_t>(footprint.size()));
  for (const auto& f : footprint) {
    w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&f.x)));
    w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&f.y)));
  }
  w.u8(providesShelter ? 1 : 0);
  w.u8(providesStorage ? 1 : 0);
  w.u8(providesWarmth ? 1 : 0);
  w.u32(storageCapacity);
  w.u8(isWalkable ? 1 : 0);
}

bool StructureBlueprint::deserialize(struct BinaryReader& r) {
  uint8_t t;
  if (!r.u8(t) || !r.str(name)) return false;
  type = static_cast<StructureType>(t);
  uint32_t n;
  if (!r.u32(n)) return false;
  materials.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!materials[i].deserialize(r)) return false;
  }
  uint8_t rt, rs;
  if (!r.u8(rt) || !r.u8(rs) || !r.f32(skillThreshold) ||
      !r.u32(maxProgress) || !r.u32(maxHealth)) return false;
  requiredTool = static_cast<ToolType>(rt);
  requiredSkill = static_cast<SkillType>(rs);
  if (!r.u32(n)) return false;
  footprint.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u32(*reinterpret_cast<uint32_t*>(&footprint[i].x)) ||
        !r.u32(*reinterpret_cast<uint32_t*>(&footprint[i].y))) return false;
  }
  uint8_t ps, pst, pw, iw;
  if (!r.u8(ps) || !r.u8(pst) || !r.u8(pw) || !r.u32(storageCapacity) || !r.u8(iw))
    return false;
  providesShelter = ps != 0;
  providesStorage = pst != 0;
  providesWarmth = pw != 0;
  isWalkable = iw != 0;
  return true;
}

StructureManager::StructureManager() {
  initDefaultBlueprints();
}

std::vector<Vec2i> StructureManager::computeFootprint(StructureType type, Vec2i pos, uint8_t /*rotation*/) const {
  auto it = blueprints_.find(type);
  if (it == blueprints_.end()) return {};

  std::vector<Vec2i> footprint;
  const auto& bp = it->second;
  footprint.reserve(bp.footprint.size());

  // Simple rotation: 0=0°, 1=90°, 2=180°, 3=270°
  // For simplicity, just return the base footprint without rotation for now
  for (const auto& offset : bp.footprint) {
    footprint.push_back({pos.x + offset.x, pos.y + offset.y});
  }
  return footprint;
}

bool StructureManager::canPlaceAt(StructureType type, Vec2i pos, uint8_t rotation) const {
  auto footprint = computeFootprint(type, pos, rotation);
  for (size_t i = 0; i < footprint.size(); ++i) {
    const auto& tile = footprint[i];
    // Check bounds and walkable - simplified
    (void)tile;
  }
  return true; // simplified
}

uint32_t StructureManager::placeStructure(StructureType type, Vec2i position, uint8_t rotation,
                                          uint64_t currentTick, uint32_t builderId) {
  Structure s;
  s.id = nextId_++;
  s.type = type;
  s.position = position;
  s.rotation = rotation;
  s.state = StructureState::Planned;
  s.progress = 0;
  s.createdAt = currentTick;
  s.builderIds.push_back(builderId);
  s.occupiedTiles = computeFootprint(type, position, rotation);

  auto it = blueprints_.find(type);
  if (it != blueprints_.end()) {
    s.maxProgress = it->second.maxProgress;
    s.maxHealth = it->second.maxHealth;
    s.health = it->second.maxHealth;
  }

  uint32_t id = s.id;
  structures_[id] = s;
  return id;
}

bool StructureManager::workOnStructure(uint32_t structureId, uint32_t workerId, float workAmount,
                                       const SkillStore* skills, class Rng& /*rng*/) {
  auto it = structures_.find(structureId);
  if (it == structures_.end()) return false;

  Structure& s = it->second;
  if (s.state == StructureState::Complete || s.state == StructureState::Ruined) return false;

  auto bpIt = blueprints_.find(s.type);
  if (bpIt != blueprints_.end()) {
    const auto& bp = bpIt->second;
    if (bp.requiredSkill != SkillType::None && skills) {
      float skill = skills->skill(bp.requiredSkill).mean();
      if (skill < bp.skillThreshold) return false;
      workAmount *= (0.5f + 1.5f * skill);
    }
  }

  s.progress = std::min(s.maxProgress, s.progress + static_cast<uint32_t>(workAmount));

  if (std::find(s.builderIds.begin(), s.builderIds.end(), workerId) == s.builderIds.end()) {
    s.builderIds.push_back(workerId);
  }

  if (s.progress >= s.maxProgress) {
    s.state = StructureState::Complete;
    s.health = s.maxHealth;
  } else {
    s.state = StructureState::Building;
  }

  return true;
}

bool StructureManager::repairStructure(uint32_t structureId, uint32_t /*workerId*/,
                                       const MaterialInventory& /*materials*/, class Rng& /*rng*/) {
  auto it = structures_.find(structureId);
  if (it == structures_.end()) return false;

  Structure& s = it->second;
  if (s.state == StructureState::Ruined) return false;
  if (s.health >= s.maxHealth) return true;

  uint32_t repairAmount = std::min(s.maxHealth - s.health, 20u);
  s.health += repairAmount;

  if (s.state == StructureState::Damaged && s.health > s.maxHealth / 2) {
    s.state = StructureState::Complete;
  }

  return true;
}

void StructureManager::damageStructure(uint32_t structureId, uint32_t damage) {
  auto it = structures_.find(structureId);
  if (it == structures_.end()) return;

  Structure& s = it->second;
  if (s.health <= damage) {
    s.health = 0;
    s.state = StructureState::Ruined;
  } else {
    s.health -= damage;
    if (s.health < s.maxHealth / 2) {
      s.state = StructureState::Damaged;
    }
  }
}

bool StructureManager::removeStructure(uint32_t structureId, MaterialInventory& outMaterials) {
  auto it = structures_.find(structureId);
  if (it == structures_.end()) return false;

  auto bpIt = blueprints_.find(it->second.type);
  if (bpIt != blueprints_.end()) {
    for (const auto& ing : bpIt->second.materials) {
      if (ing.consumed) {
        outMaterials.add(ing.material, ing.quantity / 2); // 50% salvage
      }
    }
  }

  structures_.erase(it);
  return true;
}

const Structure* StructureManager::getStructure(uint32_t id) const {
  auto it = structures_.find(id);
  return it != structures_.end() ? &it->second : nullptr;
}

Structure* StructureManager::getStructure(uint32_t id) {
  auto it = structures_.find(id);
  return it != structures_.end() ? &it->second : nullptr;
}

std::vector<uint32_t> StructureManager::structuresAt(Vec2i pos) const {
  std::vector<uint32_t> result;
  for (const auto& kv : structures_) {
    for (const auto& tile : kv.second.occupiedTiles) {
      if (tile == pos) {
        result.push_back(kv.first);
        break;
      }
    }
  }
  return result;
}

std::vector<uint32_t> StructureManager::structuresOfType(StructureType type) const {
  std::vector<uint32_t> result;
  for (const auto& kv : structures_) {
    if (kv.second.type == type) result.push_back(kv.first);
  }
  return result;
}

void StructureManager::update(uint64_t currentTick) {
  for (auto& kv : structures_) {
    Structure& s = kv.second;
    if (s.state == StructureState::Complete && s.health > 0) {
      if ((currentTick - s.completedAt) > 86400 * 30) { // 30 days
        s.health = (s.health > 5) ? s.health - 1 : 0;
        if (s.health < s.maxHealth / 2) s.state = StructureState::Damaged;
        if (s.health == 0) s.state = StructureState::Ruined;
      }
    }
  }
}

const StructureBlueprint* StructureManager::getBlueprint(StructureType type) const {
  auto it = blueprints_.find(type);
  return it != blueprints_.end() ? &it->second : nullptr;
}

void StructureManager::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(structures_.size()));
  for (const auto& kv : structures_) {
    kv.second.serialize(w);
  }
  w.u32(nextId_);
  w.u32(static_cast<uint32_t>(blueprints_.size()));
  for (const auto& kv : blueprints_) {
    kv.second.serialize(w);
  }
}

bool StructureManager::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  structures_.clear();
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    Structure s;
    if (!s.deserialize(r)) return false;
    structures_[s.id] = s;
  }
  if (!r.u32(nextId_)) return false;
  if (!r.u32(n)) return false;
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    StructureBlueprint bp;
    if (!bp.deserialize(r)) return false;
    blueprints_[bp.type] = bp;
  }
  return true;
}

void ConstructionSite::serialize(struct BinaryWriter& w) const {
  w.u32(structureId);
  w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.x)));
  w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.y)));
  w.u64(startedAt);
  w.u32(static_cast<uint32_t>(workers.size()));
  for (uint32_t workerId : workers) w.u32(workerId);
}

bool ConstructionSite::deserialize(struct BinaryReader& r) {
  if (!r.u32(structureId) ||
      !r.u32(*reinterpret_cast<uint32_t*>(&position.x)) ||
      !r.u32(*reinterpret_cast<uint32_t*>(&position.y)) ||
      !r.u64(startedAt)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  workers.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.u32(workers[i])) return false;
  }
  return true;
}

} // namespace eidolon