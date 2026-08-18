add_library(eidolon_core STATIC
  core/rng.cpp
  core/log.cpp
  core/serialize.cpp
  core/clock.hpp
  world/world.cpp
  body/physiology.cpp
  sim/engine.cpp)
target_include_directories(eidolon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(eidolon-sim tools/eidolon-sim.cpp)
target_link_libraries(eidolon-sim PRIVATE eidolon_core)
set_target_properties(eidolon-sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)