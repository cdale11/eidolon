#include "body/crafting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/json.hpp"

namespace eidolon {

void RecipeIngredient::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(material));
  w.u32(quantity);
  w.u8(consumed ? 1 : 0);
}

bool RecipeIngredient::deserialize(struct BinaryReader& r) {
  uint8_t m, c;
  if (!r.u8(m) || !r.u32(quantity) || !r.u8(c)) return false;
  material = static_cast<MaterialType>(m);
  consumed = c != 0;
  return true;
}

void Recipe::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.str(name);
  w.u8(static_cast<uint8_t>(tool));
  w.u8(static_cast<uint8_t>(requiredSkill));
  w.f32(skillThreshold);
  w.u32(static_cast<uint32_t>(ingredients.size()));
  for (const auto& ing : ingredients) ing.serialize(w);
  w.u8(static_cast<uint8_t>(resultMaterial));
  w.u8(static_cast<uint8_t>(resultTool));
  w.u8(static_cast<uint8_t>(resultStructure));
  w.u32(resultQuantity);
  w.f32(baseSuccessRate);
  w.f32(timeCost);
  w.u8(discovered ? 1 : 0);
  w.u64(discoverySeed);
}

bool Recipe::deserialize(struct BinaryReader& r) {
  if (!r.u32(id) || !r.str(name)) return false;
  uint8_t t, s, rm, rt, rs, d;
  if (!r.u8(t) || !r.u8(s) || !r.f32(skillThreshold)) return false;
  tool = static_cast<ToolType>(t);
  requiredSkill = static_cast<SkillType>(s);
  uint32_t n;
  if (!r.u32(n)) return false;
  ingredients.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!ingredients[i].deserialize(r)) return false;
  }
  if (!r.u8(rm) || !r.u8(rt) || !r.u8(rs) || !r.u32(resultQuantity) ||
      !r.f32(baseSuccessRate) || !r.f32(timeCost) || !r.u8(d) || !r.u64(discoverySeed))
    return false;
  resultMaterial = static_cast<MaterialType>(rm);
  resultTool = static_cast<ToolType>(rt);
  resultStructure = static_cast<StructureType>(rs);
  discovered = d != 0;
  return true;
}

void CraftingSystem::addRecipe(const Recipe& recipe) {
  Recipe r = recipe;
  r.id = nextRecipeId_++;
  recipes_[r.id] = r;
}

const Recipe* CraftingSystem::getRecipe(uint32_t id) const {
  auto it = recipes_.find(id);
  return it != recipes_.end() ? &it->second : nullptr;
}

std::vector<const Recipe*> CraftingSystem::getDiscoveredRecipes() const {
  std::vector<const Recipe*> result;
  for (const auto& kv : recipes_) {
    if (kv.second.discovered) result.push_back(&kv.second);
  }
  return result;
}

bool CraftingSystem::hasMaterials(const Recipe& recipe, const CraftingContext& ctx) const {
  for (const auto& ing : recipe.ingredients) {
    auto it = ctx.availableMaterials.find(ing.material);
    if (it == ctx.availableMaterials.end() || it->second < ing.quantity) {
      return false;
    }
  }
  return true;
}

void CraftingSystem::consumeMaterials(Recipe& recipe, CraftingContext& ctx) const {
  for (const auto& ing : recipe.ingredients) {
    if (ing.consumed) {
      auto it = ctx.availableMaterials.find(ing.material);
      if (it != ctx.availableMaterials.end()) {
        it->second = (it->second > ing.quantity) ? it->second - ing.quantity : 0;
      }
    }
  }
}

CraftingResult CraftingSystem::attemptCraft(uint32_t recipeId, const CraftingContext& ctx, class Rng& rng) {
  CraftingResult result;
  auto it = recipes_.find(recipeId);
  if (it == recipes_.end()) {
    result.message = "Unknown recipe";
    return result;
  }

  Recipe& recipe = it->second;
  CraftingContext mutableCtx = ctx; // copy for material consumption

  // Check materials
  if (!hasMaterials(recipe, ctx)) {
    result.message = "Insufficient materials";
    return result;
  }

  // Check tool requirement
  if (recipe.tool != ToolType::None) {
    bool hasTool = false;
    for (ToolType t : ctx.availableTools) {
      if (t == recipe.tool) { hasTool = true; break; }
    }
    if (!hasTool) {
      result.message = "Missing required tool";
      return result;
    }
  }

  // Check skill requirement
  float skillLevel = 0.0f;
  if (recipe.requiredSkill != SkillType::None && ctx.skills) {
    skillLevel = ctx.skills->skill(recipe.requiredSkill).mean();
  }
  if (skillLevel < recipe.skillThreshold) {
    result.message = "Insufficient skill";
    return result;
  }

  // Calculate success probability
  float successProb = recipe.baseSuccessRate;
  if (recipe.requiredSkill != SkillType::None && ctx.skills) {
    // Skill modifier: up to 2x base rate at max skill
    successProb *= (0.5f + 1.5f * skillLevel);
  }
  // Weather modifier
  successProb *= ctx.weatherModifier;
  successProb = std::clamp(successProb, 0.05f, 1.0f);

  // Roll for success
  bool success = rng.range(0.0, 1.0) < successProb;

  result.success = success;
  result.skillGain = success ? 0.02f : 0.01f; // small skill gain either way

  if (success) {
    // Consume materials
    for (const auto& ing : recipe.ingredients) {
      if (ing.consumed) {
        // In a real implementation, we'd modify the actual inventory
      }
    }

    // Produce result
    if (recipe.resultMaterial != MaterialType::None) {
      result.producedMaterial = recipe.resultMaterial;
      result.producedQuantity = recipe.resultQuantity;
    }
    if (recipe.resultTool != ToolType::None) {
      result.producedTool = recipe.resultTool;
    }
    if (recipe.resultStructure != StructureType::None) {
      result.producedStructure = recipe.resultStructure;
    }

    result.message = "Crafted " + recipe.name;
    if (!recipe.discovered) {
      recipe.discovered = true;
      result.novelDiscovery = true;
      result.message += " (NEW DISCOVERY!)";
    }
  } else {
    result.message = "Crafting failed: " + recipe.name;
    // Partial skill gain on failure
    result.skillGain = 0.005f;
  }

  return result;
}

void CraftingSystem::experiment(const CraftingContext& ctx, class Rng& rng) {
  // Try combining random pairs of available materials to discover new recipes
  std::vector<MaterialType> mats;
  for (const auto& kv : ctx.availableMaterials) {
    if (kv.second > 0) mats.push_back(kv.first);
  }

  if (mats.size() < 2) return;

  for (int attempt = 0; attempt < 5; ++attempt) {
    size_t i = rng.irange(0, mats.size() - 1);
    size_t j = rng.irange(0, mats.size() - 1);
    if (i == j) continue;

    tryDiscoverRecipe(ctx, rng);
  }
}

std::vector<const Recipe*> CraftingSystem::getAvailableRecipes(const CraftingContext& ctx) const {
  std::vector<const Recipe*> result;
  for (const auto& kv : recipes_) {
    const Recipe& r = kv.second;
    if (!r.discovered) continue;
    if (!hasMaterials(r, ctx)) continue;
    if (r.tool != ToolType::None) {
      bool hasTool = false;
      for (ToolType t : ctx.availableTools) {
        if (t == r.tool) { hasTool = true; break; }
      }
      if (!hasTool) continue;
    }
    if (r.requiredSkill != SkillType::None && ctx.skills) {
      if (ctx.skills->skill(r.requiredSkill).mean() < r.skillThreshold) continue;
    }
    result.push_back(&r);
  }
  return result;
}

void CraftingSystem::tryDiscoverRecipe(const CraftingContext& ctx, class Rng& rng) {
  // Simple discovery: try combining two materials
  std::vector<MaterialType> mats;
  for (const auto& kv : ctx.availableMaterials) {
    if (kv.second > 0) mats.push_back(kv.first);
  }
  if (mats.size() < 2) return;

  MaterialType m1 = mats[rng.irange(0, mats.size() - 1)];
  MaterialType m2 = mats[rng.irange(0, mats.size() - 1)];
  if (m1 == m2) return;

  // Check if this combination already has a recipe
  for (const auto& kv : recipes_) {
    const Recipe& r = kv.second;
    if (r.ingredients.size() == 2) {
      bool match = (r.ingredients[0].material == m1 && r.ingredients[1].material == m2) ||
                   (r.ingredients[0].material == m2 && r.ingredients[1].material == m1);
      if (match) return; // already known
    }
  }

  // Create new recipe (simplified discovery)
  Recipe newRecipe;
  newRecipe.name = "Discovered: " + std::to_string(static_cast<int>(m1)) + "+" + std::to_string(static_cast<int>(m2));
  newRecipe.ingredients = {
    {m1, 1, true},
    {m2, 1, true}
  };
  newRecipe.resultMaterial = MaterialType::None; // placeholder
  newRecipe.baseSuccessRate = 0.3f;
  newRecipe.discovered = true;
  newRecipe.discoverySeed = rng.next();
  addRecipe(newRecipe);
}

void CraftingSystem::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(recipes_.size()));
  for (const auto& kv : recipes_) {
    kv.second.serialize(w);
  }
  w.u32(nextRecipeId_);
}

bool CraftingSystem::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  recipes_.clear();
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    Recipe rec;
    if (!rec.deserialize(r)) return false;
    recipes_[rec.id] = rec;
  }
  if (!r.u32(nextRecipeId_)) return false;
  return true;
}

namespace {
// Map a material name string to its MaterialType enum ("" => None). Returns None for
// unknown names so the importer can skip malformed entries without failing hard.
MaterialType materialFromName(const std::string& name) {
  if (name == "Stone") return MaterialType::Stone;
  if (name == "Wood") return MaterialType::Wood;
  if (name == "Vine") return MaterialType::Vine;
  if (name == "Bone") return MaterialType::Bone;
  if (name == "Hide") return MaterialType::Hide;
  if (name == "Fiber") return MaterialType::Fiber;
  if (name == "Clay") return MaterialType::Clay;
  if (name == "Sand") return MaterialType::Sand;
  if (name == "Water") return MaterialType::Water;
  if (name == "Fire") return MaterialType::Fire;
  return MaterialType::None;
}
ToolType toolFromName(const std::string& name) {
  if (name == "StoneAxe") return ToolType::StoneAxe;
  if (name == "StoneKnife") return ToolType::StoneKnife;
  if (name == "Spear") return ToolType::Spear;
  if (name == "Hammer") return ToolType::Hammer;
  if (name == "Chisel") return ToolType::Chisel;
  if (name == "Needle") return ToolType::Needle;
  if (name == "Bow") return ToolType::Bow;
  if (name == "FishingRod") return ToolType::FishingRod;
  if (name == "Basket") return ToolType::Basket;
  if (name == "Pot") return ToolType::Pot;
  return ToolType::None;
}
StructureType structureFromName(const std::string& name) {
  if (name == "Campfire") return StructureType::Campfire;
  if (name == "LeanTo") return StructureType::LeanTo;
  if (name == "Wall") return StructureType::Wall;
  if (name == "Storage") return StructureType::Storage;
  if (name == "FarmPlot") return StructureType::FarmPlot;
  if (name == "Shelter") return StructureType::Shelter;
  if (name == "WallSection") return StructureType::WallSection;
  if (name == "Roof") return StructureType::Roof;
  if (name == "Door") return StructureType::Door;
  if (name == "Furnace") return StructureType::Furnace;
  if (name == "Kiln") return StructureType::Kiln;
  if (name == "Well") return StructureType::Well;
  return StructureType::None;
}
} // namespace

int CraftingSystem::loadEvolvedRecipes(const std::string& jsonPath) {
  std::FILE* f = std::fopen(jsonPath.c_str(), "rb");
  if (!f) return 0;
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
  std::fclose(f);

  JsonValue doc;
  if (!jsonParse(text, doc)) return 0;
  const JsonValue* recipes = doc.find("recipes");
  if (!recipes || recipes->type() != JsonValue::Type::Array) return 0;

  int imported = 0;
  for (const auto& r : recipes->asArray()) {
    if (r.type() != JsonValue::Type::Object) continue;
    const std::string name = r.str("name");
    Recipe rec;
    rec.name = name;
    rec.resultTool = toolFromName(r.str("result_tool"));
    rec.resultStructure = structureFromName(r.str("result_structure"));
    rec.resultMaterial = materialFromName(r.str("result_material"));
    if (rec.resultTool == ToolType::None && rec.resultStructure == StructureType::None &&
        rec.resultMaterial == MaterialType::None) {
      continue; // no usable result -> skip
    }
    rec.resultQuantity = static_cast<uint32_t>(r.num("result_quantity", 1.0));
    rec.baseSuccessRate = static_cast<float>(r.num("base_success_rate", 1.0));
    rec.timeCost = static_cast<float>(r.num("time_cost", 10.0));
    rec.discovered = true;
    rec.discoverySeed = static_cast<uint64_t>(rec.id);
    const JsonValue* ings = r.find("ingredients");
    if (ings && ings->type() == JsonValue::Type::Array) {
      for (const auto& ing : ings->asArray()) {
        RecipeIngredient ri;
        ri.material = materialFromName(ing.str("material"));
        ri.quantity = static_cast<uint32_t>(ing.num("quantity", 1.0));
        ri.consumed = ing.boolean("consumed", true);
        if (ri.material == MaterialType::None) continue; // sub-recipe refs skipped
        rec.ingredients.push_back(ri);
      }
    }
    if (rec.ingredients.empty()) continue; // no concrete ingredients -> unusable
    addRecipe(rec);
    ++imported;
  }
  return imported;
}

} // namespace eidolon