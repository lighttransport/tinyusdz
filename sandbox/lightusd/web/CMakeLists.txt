# SPDX-License-Identifier: Apache-2.0
# Copyright 2024 Light Transport Entertainment Inc.
#
# LightUSD - WebAssembly build configuration
# Build with: emcmake cmake -Bbuild && cmake --build build

cmake_minimum_required(VERSION 3.16)

option(LIGHTUSD_WASM64 "Use wasm64 (memory64). Requires Chrome 133+ or Firefox" OFF)
option(LIGHTUSD_WASM_DEBUG "Enable WASM debug mode with source maps" OFF)

if (LIGHTUSD_WASM64)
    set(BUILD_TARGET "lightusd_64")
else()
    set(BUILD_TARGET "lightusd")
endif()

project(${BUILD_TARGET} CXX C)

# Use C++14 to match the library
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Require Emscripten
if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "LightUSD WASM build requires Emscripten. Use: emcmake cmake ...")
endif()

# Build type configuration
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (LIGHTUSD_WASM_DEBUG)
        # Enhanced debug mode with source maps
        if (LIGHTUSD_WASM64)
            set(CMAKE_CXX_FLAGS_DEBUG "-g -gsource-map -sMEMORY64")
            set(CMAKE_C_FLAGS_DEBUG "-g -gsource-map -sMEMORY64")
        else()
            set(CMAKE_CXX_FLAGS_DEBUG "-g -gsource-map")
            set(CMAKE_C_FLAGS_DEBUG "-g -gsource-map")
        endif()
    else()
        # Standard debug mode
        if (LIGHTUSD_WASM64)
            set(CMAKE_CXX_FLAGS_DEBUG "-g4 -sMEMORY64")
            set(CMAKE_C_FLAGS_DEBUG "-g4 -sMEMORY64")
        else()
            set(CMAKE_CXX_FLAGS_DEBUG "-g4")
            set(CMAKE_C_FLAGS_DEBUG "-g4")
        endif()
    endif()
elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    # Optimize for size
    if (LIGHTUSD_WASM64)
        set(CMAKE_CXX_FLAGS_MINSIZEREL "-O3 -Oz -DNDEBUG -sMEMORY64")
        set(CMAKE_C_FLAGS_MINSIZEREL "-O3 -Oz -DNDEBUG -sMEMORY64")
    else()
        set(CMAKE_CXX_FLAGS_MINSIZEREL "-O3 -Oz -DNDEBUG")
        set(CMAKE_C_FLAGS_MINSIZEREL "-O3 -Oz -DNDEBUG")
    endif()
endif()

# Add main LightUSD library (disable tests/examples for WASM)
set(LIGHTUSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIGHTUSD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIGHTUSD_PRODUCTION_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/lightusd)

# JavaScript bindings source
set(BINDING_SOURCES
    binding.cc
)

# Create executable (outputs .js and .wasm)
add_executable(${BUILD_TARGET} ${BINDING_SOURCES})

# Include directories
target_include_directories(${BUILD_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Link LightUSD library
target_link_libraries(${BUILD_TARGET} PRIVATE lightusd)

# WASM64 compile flags
if (LIGHTUSD_WASM64)
    set_target_properties(${BUILD_TARGET} PROPERTIES COMPILE_FLAGS "-sMEMORY64=1")
    target_compile_definitions(${BUILD_TARGET} PRIVATE LIGHTUSD_WASM_MEMORY64=1)
endif()

# Output to js/src/lightusd/
set(OUTPUT_WASM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/js/src/lightusd/)

set_target_properties(${BUILD_TARGET} PROPERTIES
    OUTPUT_NAME ${BUILD_TARGET}
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_WASM_DIR}
)

# Emscripten link flags
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (LIGHTUSD_WASM_DEBUG)
        set(LIGHTUSD_EMCC_LINK_FLAGS "-g -gsource-map -sENVIRONMENT='web,worker,node' \
            -sMALLOC=emmalloc \
            -sSTACK_SIZE=1024000 \
            -sALLOW_MEMORY_GROWTH=1 \
            -sFILESYSTEM=0 \
            -sSAFE_HEAP=1 \
            -sSTACK_OVERFLOW_CHECK=2 \
            -sASSERTIONS=2 \
            -sWASM_BIGINT=1 \
            -sMODULARIZE=1 -sEXPORT_ES6 \
            -sINVOKE_RUN=0 --bind \
            --source-map-base http://localhost:5173/ ")
    else()
        set(LIGHTUSD_EMCC_LINK_FLAGS "-g4 -sENVIRONMENT='web,worker,node' \
            -sMALLOC=emmalloc \
            -sSTACK_SIZE=1024000 \
            -sALLOW_MEMORY_GROWTH=1 \
            -sFILESYSTEM=0 \
            -sSAFE_HEAP=1 \
            -sWASM_BIGINT=1 \
            -sMODULARIZE=1 -sEXPORT_ES6 \
            -sINVOKE_RUN=0 --bind \
            --source-map-base http://localhost:5173/ ")
    endif()
else()
    # Release/MinSizeRel
    set(LIGHTUSD_EMCC_LINK_FLAGS "-Oz -sENVIRONMENT='web,worker,node' \
        -sMALLOC=emmalloc \
        -sSTACK_SIZE=512000 \
        -sALLOW_MEMORY_GROWTH=1 \
        -sFILESYSTEM=0 \
        -sSAFE_HEAP=0 \
        -sWASM_BIGINT=1 \
        -sMODULARIZE=1 -sEXPORT_ES6 \
        -sINVOKE_RUN=0 --bind ")
endif()

# WASM64 specific link flags
if (LIGHTUSD_WASM64)
    string(APPEND LIGHTUSD_EMCC_LINK_FLAGS " -sASSERTIONS=2 -sMEMORY64 -sMAXIMUM_MEMORY=8GB")
else()
    string(APPEND LIGHTUSD_EMCC_LINK_FLAGS " -sASSERTIONS=1 ")
endif()

message(STATUS "LightUSD WASM link flags: ${LIGHTUSD_EMCC_LINK_FLAGS}")
set_target_properties(${BUILD_TARGET} PROPERTIES LINK_FLAGS "${LIGHTUSD_EMCC_LINK_FLAGS}")
