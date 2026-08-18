add_library(eidolon_core STATIC
  core/rng.cpp
  core/log.cpp
  core/serialize.cpp
  core/json.cpp
  core/clock.hpp
  world/world.cpp
  body/physiology.cpp
  mind/memory.cpp
  mind/mlp.cpp
  mind/neuromod.cpp
  mind/value_net.cpp
  mind/threat_net.cpp
  mind/policy.cpp
  mind/attention.cpp
  mind/personality.cpp
  mind/learn.cpp
  sim/engine.cpp)
target_include_directories(eidolon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(eidolon_llm STATIC
  llm/bridge.cpp)
target_link_libraries(eidolon_llm PRIVATE eidolon_core)
target_include_directories(eidolon_llm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(eidolon_llm PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

add_library(eidolon_store STATIC
  store/sqlite_archive.cpp)
target_link_libraries(eidolon_store PRIVATE eidolon_core sqlite3)
target_include_directories(eidolon_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(eidolon-sim tools/eidolon-sim.cpp)
target_link_libraries(eidolon-sim PRIVATE eidolon_core eidolon_store)
set_target_properties(eidolon-sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(eidolon_server STATIC
  server/server.cpp)
target_link_libraries(eidolon_server PRIVATE eidolon_core eidolon_store eidolon_llm)
target_include_directories(eidolon_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(eidolon_server PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

add_executable(eidolon-server tools/eidolon-server.cpp)
target_link_libraries(eidolon-server PRIVATE eidolon_server eidolon_llm)
set_target_properties(eidolon-server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)