add_executable(eidolon_tests
  test_main.cpp
  test_rng.cpp
  test_serialize.cpp
  test_clock.cpp
  test_world.cpp
  test_physiology.cpp
  test_memory.cpp
  test_archive.cpp
  test_json.cpp
  test_bridge.cpp
  test_engine.cpp
  test_learn.cpp
  test_wildlife.cpp
  test_phase5.cpp
  test_ode.cpp)
target_link_libraries(eidolon_tests PRIVATE eidolon_core eidolon_store eidolon_llm)
target_include_directories(eidolon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_test(NAME eidolon_tests COMMAND eidolon_tests)