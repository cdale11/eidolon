// GP-evolved recipe import tests: CraftingSystem::loadEvolvedRecipes parses the
// python/teacher/gp_evolve.py flat recipe artifact and registers usable recipes.
#include "harness.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "body/crafting.hpp"

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