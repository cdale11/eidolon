cmake_minimum_required(VERSION 3.20)
project(eidolon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# OpenSSL support for httplib (HTTPS) - not needed for WASM build
if(NOT EIDOLON_WASM_BUILD)
  find_package(OpenSSL REQUIRED)
  add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT=1)
endif()

# Disable FP contraction (FMA) so native and WASM builds execute identical IEEE
# arithmetic — required for bit-exact native/WASM parity (see core/detmath).
add_compile_options(-ffp-contract=off)

add_subdirectory(src)
add_subdirectory(tests)