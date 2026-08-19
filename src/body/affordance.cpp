#include "body/affordance.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace eidolon {

void Affordance::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.u8(static_cast<uint8_t>(type));
  w.u8(static_cast<uint8_t>(tool));
  w.u8(static_cast<uint8_t>(material));
  w.str(name);
  w.str(description);
  w.u32(static_cast<uint32_t>(requiredContext.size()));
  for (const auto& ctx : requiredContext) w.str(ctx);
  w.f32(baseEffectiveness);
  w.u32(discoveryTick);
  w.u32(usageCount);
  w.f32(confidence);
}

bool Affordance::deserialize(struct BinaryReader& r) {
  if (!r.u32(id)) return false;
  uint8_t t, tl, ml;
  if (!r.u8(t) || !r.u8(tl) || !r.u8(ml) || !r.str(name) || !r.str(description)) return false;
  type = static_cast<AffordanceType>(t);
  tool = static_cast<ToolType>(tl);
  material = static_cast<MaterialType>(ml);
  uint32_t n;
  if (!r.u32(n)) return false;
  requiredContext.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.str(requiredContext[i])) return false;
  }
  if (!r.f32(baseEffectiveness) || !r.u32(discoveryTick) || !r.u32(usageCount) || !r.f32(confidence))
    return false;
  return true;
}

void DiscoveryEvent::serialize(struct BinaryWriter& w) const {
  w.u64(tick);
  w.u8(static_cast<uint8_t>(type));
  w.u8(static_cast<uint8_t>(tool));
  w.u8(static_cast<uint8_t>(material));
  w.str(context);
  w.f32(effectiveness);
  w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.x)));
  w.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.y)));
}

bool DiscoveryEvent::deserialize(struct BinaryReader& r) {
  if (!r.u64(tick)) return false;
  uint8_t t, tl, ml;
  if (!r.u8(t) || !r.u8(tl) || !r.u8(ml) || !r.str(context) || !r.f32(effectiveness) ||
      !r.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.x))) ||
      !r.u32(*reinterpret_cast<uint32_t*>(const_cast<int*>(&position.y))))
    return false;
  type = static_cast<AffordanceType>(t);
  tool = static_cast<ToolType>(tl);
  material = static_cast<MaterialType>(ml);
  return true;
}


void AffordanceSystem::initDefaultAffordances() {
  // Tool affordances
  registerToolAffordances(ToolType::StoneAxe, {AffordanceType::Cutting, AffordanceType::Chopping, AffordanceType::Hammering});
  registerToolAffordances(ToolType::StoneKnife, {AffordanceType::Cutting, AffordanceType::Scraping, AffordanceType::Piercing});
  registerToolAffordances(ToolType::Spear, {AffordanceType::Piercing, AffordanceType::Food});
  registerToolAffordances(ToolType::Hammer, {AffordanceType::Hammering, AffordanceType::Levering});
  registerToolAffordances(ToolType::Chisel, {AffordanceType::Cutting, AffordanceType::Scraping, AffordanceType::ToolMaking});
  registerToolAffordances(ToolType::Needle, {AffordanceType::Piercing, AffordanceType::Scraping});
  registerToolAffordances(ToolType::Bow, {AffordanceType::Food});
  registerToolAffordances(ToolType::FishingRod, {AffordanceType::Food});
  registerToolAffordances(ToolType::Basket, {AffordanceType::Carrying, AffordanceType::Storage});
  registerToolAffordances(ToolType::Pot, {AffordanceType::Cooking, AffordanceType::Storage});

  // Material affordances
  registerMaterialAffordances(MaterialType::Stone, {AffordanceType::Hammering, AffordanceType::Cutting, AffordanceType::Support});
  registerMaterialAffordances(MaterialType::Wood, {AffordanceType::Chopping, AffordanceType::Carrying, AffordanceType::Support, AffordanceType::FireStarting});
  registerMaterialAffordances(MaterialType::Vine, {AffordanceType::Carrying, AffordanceType::ToolMaking});
  registerMaterialAffordances(MaterialType::Bone, {AffordanceType::Piercing, AffordanceType::Cutting, AffordanceType::ToolMaking});
  registerMaterialAffordances(MaterialType::Hide, {AffordanceType::Carrying, AffordanceType::Shelter, AffordanceType::Warmth});
  registerMaterialAffordances(MaterialType::Fiber, {AffordanceType::Carrying, AffordanceType::ToolMaking});
  registerMaterialAffordances(MaterialType::Clay, {AffordanceType::Storage, AffordanceType::Cooking});
  registerMaterialAffordances(MaterialType::Sand, {AffordanceType::Support});
  registerMaterialAffordances(MaterialType::Water, {AffordanceType::Food, AffordanceType::Cooking});
  registerMaterialAffordances(MaterialType::Fire, {AffordanceType::Warmth, AffordanceType::Light, AffordanceType::Cooking, AffordanceType::FireStarting});
}

void AffordanceSystem::registerToolAffordances(ToolType tool, const std::vector<AffordanceType>& affordances) {
  toolAffordances_[tool] = affordances;
}

void AffordanceSystem::registerMaterialAffordances(MaterialType material, const std::vector<AffordanceType>& affordances) {
  materialAffordances_[material] = affordances;
}

bool AffordanceSystem::contextMatches(const std::vector<std::string>& required, const std::string& context) const {
  if (required.empty()) return true;
  for (const auto& req : required) {
    if (context.find(req) != std::string::npos) return true;
  }
  return false;
}

bool AffordanceSystem::tryDiscoverAffordance(ToolType tool, MaterialType material, const std::string& context,
                                             float observedEffectiveness, uint64_t tick, const Vec2i& /*pos*/, class Rng& /*rng*/) {
  // Check if this tool/material/context combination is already known
  // For simplicity, we'll create a new affordance if effectiveness is notable
  if (observedEffectiveness < 0.3f) return false; // not notable enough

  // Check if we already have this affordance for this tool/material
  std::string key = std::to_string(static_cast<int>(tool)) + "_" +
                    std::to_string(static_cast<int>(material)) + "_" + context;

  // Create new affordance
  Affordance a;
  a.id = nextAffordanceId_++;
  // Determine type from tool/material combination
  if (tool != ToolType::None) {
    a.tool = tool;
    // Infer type from tool's known affordances
    auto it = toolAffordances_.find(tool);
    if (it != toolAffordances_.end() && !it->second.empty()) {
      a.type = it->second[0];
    }
  } else if (material != MaterialType::None) {
    a.material = material;
    auto it = materialAffordances_.find(material);
    if (it != materialAffordances_.end() && !it->second.empty()) {
      a.type = it->second[0];
    }
  }
  a.name = "Discovered: " + (tool != ToolType::None ? std::to_string(static_cast<int>(tool)) : "material") +
           " in " + context;
  a.description = "Discovered affordance through experimentation";
  a.requiredContext.push_back(context);
  a.baseEffectiveness = observedEffectiveness;
  a.discoveryTick = static_cast<uint32_t>(tick);
  a.confidence = observedEffectiveness;

  affordances_.push_back(a);

  // Record discovery event
  DiscoveryEvent de;
  de.tick = 0; // would be set by caller
  de.type = a.type;
  de.tool = tool;
  de.material = material;
  de.context = context;
  de.effectiveness = observedEffectiveness;
  de.position = Vec2i{0, 0}; // would be set by caller
  discoveries_.push_back(de);

  return true;
}

std::vector<AffordanceType> AffordanceSystem::getToolAffordances(ToolType tool, const std::string& context) const {
  auto it = toolAffordances_.find(tool);
  if (it == toolAffordances_.end()) return {};

  std::vector<AffordanceType> result;
  // Check if we have a discovered affordance matching this
  for (const auto& aff : affordances_) {
    if (aff.tool == tool && aff.type == it->second[0]) {
      if (contextMatches(aff.requiredContext, context)) {
        result.push_back(aff.type);
        break;
      }
    }
  }
  if (result.empty() && it != toolAffordances_.end()) {
    // Return base affordances if no context-specific ones
    return it->second;
  }
  return result;
}

std::vector<AffordanceType> AffordanceSystem::getMaterialAffordances(MaterialType material, const std::string& /*context*/) const {
  auto it = materialAffordances_.find(material);
  if (it == materialAffordances_.end()) return {};
  return it->second;
}

const Affordance* AffordanceSystem::getAffordance(uint32_t id) const {
  for (const auto& a : affordances_) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

uint32_t AffordanceSystem::generateProcedureFromAffordance(const DiscoveryEvent& discovery, class Rng& rng) {
  // This would integrate with the crafting system to create a new recipe
  // For now, return 0 (no recipe generated)
  (void)discovery;
  (void)rng;
  return 0;
}

void AffordanceSystem::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(affordances_.size()));
  for (const auto& a : affordances_) a.serialize(w);
  w.u32(static_cast<uint32_t>(discoveries_.size()));
  for (const auto& d : discoveries_) d.serialize(w);
  w.u32(nextAffordanceId_);
  w.u32(nextDiscoveryId_);
}

bool AffordanceSystem::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  affordances_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!affordances_[i].deserialize(r)) return false;
  }
  if (!r.u32(n)) return false;
  discoveries_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!discoveries_[i].deserialize(r)) return false;
  }
  if (!r.u32(nextAffordanceId_) || !r.u32(nextDiscoveryId_)) return false;
  return true;
}

} // namespace eidolon