# C++ unit tests (minimal embedded harness — no external test framework).
add_executable(eidolon_tests test_main.cpp)
target_link_libraries(eidolon_tests PRIVATE eidolon_core)

enable_testing()
add_test(NAME eidolon_tests COMMAND eidolon_tests)