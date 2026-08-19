#ifndef EIDOLON_AFFORDANCE_HPP
#define EIDOLON_AFFORDANCE_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <functional>

#include "core/vec2.hpp"
#include "core/serialize.hpp"
#include "body/crafting.hpp"
#include "body/skill.hpp"

namespace eidolon {

// Affordance discovery system for Phase 6: tool used in unexpected ways → new procedures.
// Detects when an organism uses a tool/object in a novel context and creates new procedures/recipes.

enum class AffordanceType : uint8_t {
  None = 0,
  // Tool affordances
  Cutting = 1,        // can cut things
  Digging = 2,        // can dig
  Hammering = 3,      // can hammer
  Piercing = 4,       // can pierce
  Chopping = 5,       // can chop
  Scraping = 6,       // can scrape
  Levering = 7,       // can lever/pry
  Carrying = 8,       // can carry
  // Environmental affordances
  Shelter = 9,        // provides shelter
  Water = 10,         // provides water
  Food = 11,          // provides food
  Warmth = 12,        // provides warmth
  Light = 13,         // provides light
  Storage = 14,       // provides storage
  Support = 15,       // provides structural support
  // Social affordances
  Communication = 16, // can communicate
  Trade = 17,         // can trade
  Teaching = 18,      // can teach
  // Compound affordances
  ToolMaking = 19,    // can make tools
  FireStarting = 20,  // can start fire
  Cooking = 21,       // can cook
  Count = 22
};

// Affordance: what actions an object/tool enables in a given context
struct Affordance {
  uint32_t id = 0;
  AffordanceType type = AffordanceType::None;
  ToolType tool = ToolType::None;           // tool that provides this affordance (if tool-based)
  MaterialType material = MaterialType::None; // material that provides this affordance (if material-based)
  std::string name;
  std::string description;
  std::vector<std::string> requiredContext; // context tags where this applies
  float baseEffectiveness = 1.0f;           // 0..1 how well it works
  uint32_t discoveryTick = 0;               // when discovered
  uint32_t usageCount = 0;                  // times used
  float confidence = 0.0f;                  // confidence in this affordance (0..1)

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Affordance discovery event
struct DiscoveryEvent {
  uint64_t tick = 0;
  AffordanceType type = AffordanceType::None;
  ToolType tool = ToolType::None;
  MaterialType material = MaterialType::None;
  std::string context;      // context where discovered
  float effectiveness = 0;  // observed effectiveness
  Vec2i position;           // where discovered

  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Affordance discovery system
class AffordanceSystem {
public:
  AffordanceSystem() = default;

  // Register a tool's known affordances
  void registerToolAffordances(ToolType tool, const std::vector<AffordanceType>& affordances);

  // Register a material's known affordances
  void registerMaterialAffordances(MaterialType material, const std::vector<AffordanceType>& affordances);

  // Attempt to discover new affordances from action in context
  // Returns true if a new affordance was discovered
  bool tryDiscoverAffordance(ToolType tool, MaterialType material, const std::string& context,
                             float observedEffectiveness, uint64_t tick, const Vec2i& pos, class Rng& rng);

  // Get affordances for a tool in a context
  std::vector<AffordanceType> getToolAffordances(ToolType tool, const std::string& context) const;

  // Get affordances for a material in a context
  std::vector<AffordanceType> getMaterialAffordances(MaterialType material, const std::string& context) const;

  // Get all discovered affordances
  const std::vector<DiscoveryEvent>& getDiscoveries() const { return discoveries_; }

  // Get affordance by ID
  const Affordance* getAffordance(uint32_t id) const;

  // Generate new procedure/recipe from discovered affordance
  // Returns recipe ID if new recipe created, 0 otherwise
  uint32_t generateProcedureFromAffordance(const DiscoveryEvent& discovery, class Rng& rng);

  // Serialize/deserialize
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);

private:
  std::unordered_map<ToolType, std::vector<AffordanceType>> toolAffordances_;
  std::unordered_map<MaterialType, std::vector<AffordanceType>> materialAffordances_;
  std::vector<Affordance> affordances_;
  std::vector<DiscoveryEvent> discoveries_;
  uint32_t nextAffordanceId_ = 1;
  uint32_t nextDiscoveryId_ = 1;

  // Initialize default tool/material affordances
  void initDefaultAffordances();

  // Context similarity check
  bool contextMatches(const std::vector<std::string>& required, const std::string& context) const;
};

} // namespace eidolon

#endif // EIDOLON_AFFORDANCE_HPP