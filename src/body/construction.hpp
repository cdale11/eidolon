#ifndef EIDOLON_CONSTRUCTION_HPP
#define EIDOLON_CONSTRUCTION_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

#include "core/vec2.hpp"
#include "core/serialize.hpp"
#include "body/crafting.hpp"

namespace eidolon {

// Construction system for Phase 6: persistent structures on grid.
// Simple, robust implementation.

enum class StructureState : uint8_t {
  None = 0,
  Planned = 1,
  Building = 2,
  Complete = 3,
  Damaged = 4,
  Ruined = 5
};

struct Structure {
  uint32_t id = 0;
  StructureType type = StructureType::None;
  Vec2i position;                 // grid position
  uint8_t rotation = 0;           // 0..3 (90-degree increments)
  StructureState state = StructureState::Planned;
  uint32_t progress = 0;          // construction progress (0..100)
  uint32_t maxProgress = 100;     // required progress to complete
  uint32_t health = 100;          // structure health (0 = ruined)
  uint32_t maxHealth = 100;
  uint64_t createdAt = 0;         // sim tick when placed
  uint64_t completedAt = 0;       // sim tick when completed
  std::vector<uint32_t> builderIds; // organisms that contributed
  std::vector<Vec2i> occupiedTiles; // tiles occupied by this structure

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Structure blueprint: template for building a structure type
struct StructureBlueprint {
  StructureType type = StructureType::None;
  std::string name;
  std::vector<RecipeIngredient> materials; // required materials
  ToolType requiredTool = ToolType::None;
  SkillType requiredSkill = SkillType::None;
  float skillThreshold = 0.0f;
  uint32_t maxProgress = 100;
  uint32_t maxHealth = 100;
  std::vector<Vec2i> footprint; // relative tile offsets from origin
  bool providesShelter = false;
  bool providesStorage = false;
  bool providesWarmth = false;
  uint32_t storageCapacity = 0;
  bool isWalkable = false;

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Structure manager: handles placement, building, repair, destruction
class StructureManager {
public:
  StructureManager();

  // Place a new structure (planned state)
  uint32_t placeStructure(StructureType type, Vec2i position, uint8_t rotation,
                          uint64_t currentTick, uint32_t builderId);

  // Work on a structure (add progress)
  bool workOnStructure(uint32_t structureId, uint32_t workerId, float workAmount,
                       const SkillStore* skills, class Rng& rng);

  // Repair a damaged structure
  bool repairStructure(uint32_t structureId, uint32_t workerId,
                       const MaterialInventory& materials, class Rng& rng);

  // Damage a structure
  void damageStructure(uint32_t structureId, uint32_t damage);

  // Remove a structure (salvage materials)
  bool removeStructure(uint32_t structureId, MaterialInventory& outMaterials);

  // Get structure by ID
  const Structure* getStructure(uint32_t id) const;
  Structure* getStructure(uint32_t id);

  // Get all structures at a position
  std::vector<uint32_t> structuresAt(Vec2i pos) const;

  // Get all structures of a type
  std::vector<uint32_t> structuresOfType(StructureType type) const;

  // Update structures (decay, weather effects)
  void update(uint64_t currentTick);

  // Get blueprint for a structure type
  const StructureBlueprint* getBlueprint(StructureType type) const;

  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  std::unordered_map<uint32_t, Structure> structures_;
  uint32_t nextId_ = 1;
  std::unordered_map<StructureType, StructureBlueprint> blueprints_;

  void initDefaultBlueprints();
  bool canPlaceAt(StructureType type, Vec2i pos, uint8_t rotation) const;
  std::vector<Vec2i> computeFootprint(StructureType type, Vec2i pos, uint8_t rotation) const;
};

// Construction site: temporary structure being built
struct ConstructionSite {
  uint32_t structureId = 0;
  Vec2i position;
  uint64_t startedAt = 0;
  std::vector<uint32_t> workers;

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

} // namespace eidolon

#endif // EIDOLON_CONSTRUCTION_HPP