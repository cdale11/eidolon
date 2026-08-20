# Emscripten toolchain file for Eidolon WASM builds
# Usage: cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/emscripten.cmake ...

set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_SYSTEM_VERSION 1)

# Get EMSDK path from environment or use default
if(NOT DEFINED ENV{EMSDK})
  set(ENV{EMSDK} "/tmp/emsdk")
endif()
set(EMSCRIPTEN $ENV{EMSDK}/upstream/emscripten)

# Compiler settings
set(CMAKE_C_COMPILER ${EMSCRIPTEN}/emcc)
set(CMAKE_CXX_COMPILER ${EMSCRIPTEN}/em++)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# Find Emscripten tools
find_program(EMCC emcc PATHS ${EMSCRIPTEN} NO_DEFAULT_PATH)
find_program(EMAR emar PATHS ${EMSCRIPTEN} NO_DEFAULT_PATH)
find_program(EMLINK emcc PATHS ${EMSCRIPTEN} NO_DEFAULT_PATH)

set(CMAKE_AR ${EMAR} CACHE FILEPATH "Emscripten archiver" FORCE)
set(CMAKE_RANLIB ${EMAR} CACHE FILEPATH "Emscripten ranlib" FORCE)
set(CMAKE_LINKER ${EMLINK} CACHE FILEPATH "Emscripten linker" FORCE)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Emscripten-specific flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -std=c++17 -sWASM=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sALLOW_MEMORY_GROWTH=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sEXPORT_ES6=0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sMODULARIZE=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sEXPORT_NAME=EidolonCore")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sENVIRONMENT=web,worker,node")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sINITIAL_MEMORY=64MB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sMAXIMUM_MEMORY=256MB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sSTACK_SIZE=5MB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sASSERTIONS=0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sSAFE_HEAP=0")

# Exception handling - disable for performance
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

# pthreads support (for multithreaded builds)
# Note: Requires SharedArrayBuffer and proper COOP/COEP headers
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -sPROXY_TO_PTHREAD=0")

# SIMD support
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msimd128")

# Disable features not available in WASM
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEIDOLON_WASM_BUILD=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEIDOLON_NO_SQLITE=1")

# Output settings
set(CMAKE_EXECUTABLE_SUFFIX ".js")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".wasm")

# Disable shared libraries (not well supported)
set(BUILD_SHARED_LIBS OFF)

# Find Emscripten's built-in libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)