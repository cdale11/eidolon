#ifndef EIDOLON_CRAFTING_HPP
#define EIDOLON_CRAFTING_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

#include "core/vec2.hpp"
#include "core/serialize.hpp"
#include "body/skill.hpp"

namespace eidolon {

// Crafting system for Phase 6: learned recipes, procedural store, affordance discovery.
// Deterministic, seeded, serializable.

enum class MaterialType : uint8_t {
  None = 0,
  Stone = 1,
  Wood = 2,
  Vine = 3,
  Bone = 4,
  Hide = 5,
  Fiber = 6,
  Clay = 7,
  Sand = 8,
  Water = 9,
  Fire = 10,       // not a material per se, but a crafting requirement
  Count = 11
};

enum class ToolType : uint8_t {
  None = 0,
  // Basic tools
  StoneAxe = 1,
  StoneKnife = 2,
  Spear = 3,
  Hammer = 4,
  Chisel = 5,
  Needle = 6,
  // Advanced tools
  Bow = 7,
  FishingRod = 8,
  Basket = 9,
  Pot = 10,
  Count = 11
};

enum class StructureType : uint8_t {
  None = 0,
  Campfire = 1,
  LeanTo = 2,
  Wall = 3,
  Storage = 4,
  FarmPlot = 5,
  Shelter = 6,
  WallSection = 7,
  Roof = 8,
  Door = 9,
  Furnace = 10,
  Kiln = 11,
  Well = 12,
  Count = 13
};

struct RecipeIngredient {
  MaterialType material = MaterialType::None;
  uint32_t quantity = 1;
  bool consumed = true; // whether ingredient is consumed

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct Recipe {
  uint32_t id = 0;
  std::string name;
  ToolType tool = ToolType::None;       // required tool (None = no tool needed)
  SkillType requiredSkill = SkillType::None;
  float skillThreshold = 0.0f;          // minimum competence to attempt
  std::vector<RecipeIngredient> ingredients;
  MaterialType resultMaterial = MaterialType::None;
  ToolType resultTool = ToolType::None;
  StructureType resultStructure = StructureType::None;
  uint32_t resultQuantity = 1;
  float baseSuccessRate = 1.0f;         // base success rate before skill modifier
  float timeCost = 10.0f;               // sim-seconds to craft
  bool discovered = false;              // whether organism has discovered this recipe
  uint64_t discoverySeed = 0;           // seed for reproducible discovery

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Crafting context: available materials, tools, skills, environment
struct CraftingContext {
  std::unordered_map<MaterialType, uint32_t> availableMaterials;
  std::vector<ToolType> availableTools;
  const SkillStore* skills = nullptr;
  Vec2i position;                       // where crafting happens
  bool hasFire = false;
  bool hasWater = false;
  float weatherModifier = 1.0f;         // weather effect on success rate
};

// Crafting result
struct CraftingResult {
  bool success = false;
  MaterialType producedMaterial = MaterialType::None;
  uint32_t producedQuantity = 0;
  ToolType producedTool = ToolType::None;
  StructureType producedStructure = StructureType::None;
  float skillGain = 0.0f;               // skill experience gained
  std::string message;                  // description of result
  bool novelDiscovery = false;          // whether this was a new recipe discovery
};

// Crafting system: manages recipes, attempts, discovery
class CraftingSystem {
public:
  CraftingSystem() = default;

  // Add a known recipe
  void addRecipe(const Recipe& recipe);

  // Get recipe by ID
  const Recipe* getRecipe(uint32_t id) const;

  // Get all discovered recipes
  std::vector<const Recipe*> getDiscoveredRecipes() const;

  // Attempt to craft a recipe
  CraftingResult attemptCraft(uint32_t recipeId, const CraftingContext& ctx, class Rng& rng);

  // Try to discover new recipes from available materials (experimentation)
  void experiment(const CraftingContext& ctx, class Rng& rng);

  // Load evolved recipes from a GP artifact JSON file (python/teacher/gp_evolve.py
  // output). The file carries a "recipes" array in the flat schema below. Returns the
  // number of recipes imported (0 on missing/unparseable file).
  int loadEvolvedRecipes(const std::string& jsonPath);

  // Get all recipes that can be attempted with current context
  std::vector<const Recipe*> getAvailableRecipes(const CraftingContext& ctx) const;

  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  std::unordered_map<uint32_t, Recipe> recipes_;
  uint32_t nextRecipeId_ = 1;

  // Check if context has required materials
  bool hasMaterials(const Recipe& recipe, const CraftingContext& ctx) const;

  // Consume materials from context
  void consumeMaterials(Recipe& recipe, CraftingContext& ctx) const;

  // Try to discover a new recipe combination
  void tryDiscoverRecipe(const CraftingContext& ctx, class Rng& rng);
};

// Material inventory: tracks quantities of materials
class MaterialInventory {
public:
  MaterialInventory() { counts_.fill(0); }

  uint32_t count(MaterialType m) const {
    return counts_[static_cast<size_t>(m)];
  }

  void add(MaterialType m, uint32_t qty = 1) {
    counts_[static_cast<size_t>(m)] += qty;
  }

  bool remove(MaterialType m, uint32_t qty = 1) {
    size_t idx = static_cast<size_t>(m);
    if (counts_[idx] >= qty) {
      counts_[idx] -= qty;
      return true;
    }
    return false;
  }

  bool has(MaterialType m, uint32_t qty = 1) const {
    return counts_[static_cast<size_t>(m)] >= qty;
  }

  void serialize(struct BinaryWriter& w) const {
    w.u8(static_cast<uint8_t>(MaterialType::Count));
    for (size_t i = 0; i < static_cast<size_t>(MaterialType::Count); ++i) {
      w.u32(counts_[i]);
    }
  }

  bool deserialize(struct BinaryReader& r) {
    uint8_t count;
    if (!r.u8(count)) return false;
    for (size_t i = 0; i < static_cast<size_t>(MaterialType::Count); ++i) {
      if (!r.u32(counts_[i])) return false;
    }
    return true;
  }

private:
  std::array<uint32_t, static_cast<size_t>(MaterialType::Count)> counts_;
};

} // namespace eidolon

#endif // EIDOLON_CRAFTING_HPP