// GP-evolved recipe import tests: CraftingSystem::loadEvolvedRecipes parses the
// python/teacher/gp_evolve.py flat recipe artifact and registers usable recipes.
#include "harness.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "body/crafting.hpp"
#include "body/construction.hpp"
#include "body/affordance.hpp"

using namespace eidolon;

TEST(crafting_import_evolved_recipes) {
  // Write a minimal GP artifact JSON (flat recipe schema) to a temp file.
  const char* json =
      "{"
      "  \"recipes\": ["
      "    {\"name\": \"Spear\", \"result_tool\": \"Spear\", \"result_quantity\": 1,"
      "     \"base_success_rate\": 1.0, \"time_cost\": 125.0,"
      "     \"ingredients\": ["
      "       {\"material\": \"Wood\", \"quantity\": 2, \"consumed\": true},"
      "       {\"material\": \"Stone\", \"quantity\": 1, \"consumed\": true},"
      "       {\"material\": \"Vine\", \"quantity\": 1, \"consumed\": true}"
      "     ]},"
      "    {\"name\": \"Bogus\", \"result_tool\": \"\", \"result_structure\": \"\","
      "     \"result_material\": \"\", \"ingredients\": []}"
      "  ]"
      "}";
  char pathbuf[128];
  std::snprintf(pathbuf, sizeof(pathbuf), "/tmp/eidolon_gp_recipes_%d.json",
                static_cast<int>(getpid()));
  std::FILE* f = std::fopen(pathbuf, "wb");
  CHECK(f != nullptr);
  if (f) {
    std::fwrite(json, 1, std::strlen(json), f);
    std::fclose(f);
  }

  CraftingSystem cs;
  const int imported = cs.loadEvolvedRecipes(pathbuf);
  std::remove(pathbuf);

  // Only the Spear recipe is usable (the Bogus entry has no result and no ingredients).
  CHECK_EQ(imported, 1);
  const Recipe* spear = cs.getRecipe(1);
  CHECK(spear != nullptr);
  if (spear) {
    CHECK(spear->resultTool == ToolType::Spear);
    CHECK_EQ(spear->ingredients.size(), 3u);
    CHECK(spear->ingredients[0].material == MaterialType::Wood);
    CHECK_EQ(spear->ingredients[0].quantity, 2u);
    CHECK(spear->ingredients[1].material == MaterialType::Stone);
    CHECK(spear->discovered);
  }
}

TEST(crafting_import_missing_file) {
  CraftingSystem cs;
  CHECK_EQ(cs.loadEvolvedRecipes("/tmp/definitely_missing_gp_artifacts.json"), 0);
  CHECK_EQ(cs.loadEvolvedRecipes("/tmp/"), 0);
}

TEST(structure_manager_count_and_well_lookup_roundtrip) {
  StructureManager sm;
  const uint32_t id = sm.placeStructure(StructureType::Well, Vec2i{4, 5}, 0, 123, 0);
  CHECK_EQ(sm.count(), 1u);
  const auto at = sm.structuresAt(Vec2i{4, 5});
  CHECK_EQ(at.size(), 1u);
  CHECK_EQ(at[0], id);

  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) {
    sm.serialize(w);
  });
  StructureManager restored;
  std::string err;
  CHECK(unpackSnapshot(blob, kSnapshotVersion,
                       [&](BinaryReader& r) { return restored.deserialize(r); }, err));
  CHECK_EQ(restored.count(), 1u);
  const Structure* well = restored.getStructure(id);
  CHECK(well != nullptr);
  if (well) {
    CHECK(well->type == StructureType::Well);
    CHECK(well->position == (Vec2i{4, 5}));
  }
}

TEST(construction_signed_coordinates_roundtrip) {
  Structure s;
  s.id = 7;
  s.type = StructureType::Shelter;
  s.position = Vec2i{-3, 9};
  s.occupiedTiles.push_back(Vec2i{-3, 9});
  s.occupiedTiles.push_back(Vec2i{-2, 9});
  BinaryWriter w;
  s.serialize(w);
  BinaryReader r(w.data());
  Structure out;
  CHECK(out.deserialize(r));
  CHECK(out.position == (Vec2i{-3, 9}));
  CHECK_EQ(out.occupiedTiles.size(), 2u);
  CHECK(out.occupiedTiles[0] == (Vec2i{-3, 9}));
  CHECK(out.occupiedTiles[1] == (Vec2i{-2, 9}));
}

TEST(affordance_discovery_signed_coordinates_roundtrip) {
  DiscoveryEvent d;
  d.tick = 99;
  d.type = AffordanceType::Water;
  d.context = "shore";
  d.position = Vec2i{-8, 12};
  BinaryWriter w;
  d.serialize(w);
  BinaryReader r(w.data());
  DiscoveryEvent out;
  CHECK(out.deserialize(r));
  CHECK(out.position == (Vec2i{-8, 12}));
}
