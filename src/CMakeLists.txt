add_library(eidolon_core STATIC
  core/rng.cpp
  core/log.cpp
  core/serialize.cpp
  core/json.cpp
  core/detmath.cpp
  core/clock.hpp
  world/world.cpp
  world/wildlife.cpp
  world/cellular_automata.cpp
  world/voronoi.cpp
  world/reaction_diffusion.cpp
  world/boids.cpp
  world/markov.cpp
  world/lsystem.cpp
  world/procgen.cpp
  world/grammar.cpp
  body/physiology.cpp
  body/ode_tests.cpp
  body/skill.cpp
  body/crafting.cpp
  body/affordance.cpp
  body/construction.cpp
  mind/memory.cpp
  mind/memory_system.cpp
  mind/mlp.cpp
  mind/neuromod.cpp
  mind/value_net.cpp
  mind/threat_net.cpp
  mind/policy.cpp
  mind/attention.cpp
  mind/personality.cpp
  mind/learn.cpp
  mind/heredity.cpp
  mind/genetic_memory.cpp
  mind/world_predictor.cpp
  mind/goal_emergence.cpp
  mind/llm_planner.cpp
  mind/user_model.cpp
  mind/wildlife_social.cpp
  mind/attachment.cpp
  mind/belief_ising.cpp
  mind/compute_profile.cpp
  mind/compute_scheduler.cpp
  sim/engine.cpp)
target_link_libraries(eidolon_core PRIVATE eidolon_llm)
target_include_directories(eidolon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT EIDOLON_WASM_BUILD)
add_library(eidolon_llm STATIC
  llm/bridge.cpp
  llm/web_browser.cpp
  llm/intent_parser.cpp
  llm/instruction_learning.cpp)
target_link_libraries(eidolon_llm PRIVATE eidolon_core OpenSSL::SSL OpenSSL::Crypto)
else()
add_library(eidolon_llm STATIC
  llm/intent_parser.cpp
  llm/instruction_learning.cpp)
target_link_libraries(eidolon_llm PRIVATE eidolon_core)
endif()
target_include_directories(eidolon_llm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(eidolon_llm PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

if(NOT EIDOLON_WASM_BUILD)
add_library(eidolon_store STATIC
  store/sqlite_archive.cpp)
target_link_libraries(eidolon_store PRIVATE eidolon_core sqlite3)
target_include_directories(eidolon_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(NOT EIDOLON_WASM_BUILD)
add_executable(eidolon-sim tools/eidolon-sim.cpp)
target_link_libraries(eidolon-sim PRIVATE eidolon_core eidolon_store eidolon_llm)
set_target_properties(eidolon-sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

add_executable(eidolon-parity-dump ${CMAKE_CURRENT_LIST_DIR}/../tools/eidolon-parity-dump.cpp)
target_link_libraries(eidolon-parity-dump PRIVATE eidolon_core eidolon_llm)
set_target_properties(eidolon-parity-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Phase 15: WASM worker module — ReplicaCore driven by a browser Web Worker
# (client-side offload, DESIGN §17). No main(); loaded by the kWorkerJs script
# that calls the exported eidn_* API. MODULARIZE factory for worker/node use.
if(EIDOLON_WASM_BUILD)
  add_executable(eidolon-worker ${CMAKE_CURRENT_LIST_DIR}/../tools/eidolon-wasm-worker.cpp)
  target_link_libraries(eidolon-worker PRIVATE eidolon_core eidolon_llm)
  target_link_options(eidolon-worker PRIVATE
    --no-entry
    -sMODULARIZE=1
    -sEXPORT_ES6=0
    -sEXPORT_NAME=EidolonWorker
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sMAXIMUM_MEMORY=256MB
    -sSTACK_SIZE=5MB
    -sASSERTIONS=0
    -sSAFE_HEAP=0
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=HEAPU8)
  set_target_properties(eidolon-worker PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(NOT EIDOLON_WASM_BUILD)
add_library(eidolon_server STATIC
  server/server.cpp)
if(NOT EIDOLON_WASM_BUILD)
  target_link_libraries(eidolon_server PRIVATE eidolon_core eidolon_store eidolon_llm OpenSSL::SSL OpenSSL::Crypto)
else()
  target_link_libraries(eidolon_server PRIVATE eidolon_core eidolon_store eidolon_llm)
endif()
target_include_directories(eidolon_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(eidolon_server PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

add_executable(eidolon-server tools/eidolon-server.cpp)
target_link_libraries(eidolon-server PRIVATE eidolon_server eidolon_llm)
set_target_properties(eidolon-server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
