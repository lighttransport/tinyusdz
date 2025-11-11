cmake_minimum_required(VERSION 3.16)

project(pcp_example)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find TinyUSDZ
set(TINYUSDZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Include paths
include_directories(
    ${TINYUSDZ_ROOT}/src
    ${TINYUSDZ_ROOT}/src/tydra
    ${TINYUSDZ_ROOT}/deps
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# BLAKE3 library
add_library(blake3 STATIC
    ${TINYUSDZ_ROOT}/deps/blake3/blake3.c
    ${TINYUSDZ_ROOT}/deps/blake3/blake3_portable.c
)

# If SIMD optimizations are available
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_sources(blake3 PRIVATE
        ${TINYUSDZ_ROOT}/deps/blake3/blake3_sse2_x86-64_unix.S
        ${TINYUSDZ_ROOT}/deps/blake3/blake3_sse41_x86-64_unix.S
        ${TINYUSDZ_ROOT}/deps/blake3/blake3_avx2_x86-64_unix.S
        ${TINYUSDZ_ROOT}/deps/blake3/blake3_avx512_x86-64_unix.S
    )
endif()

# TinyUSDZ core library
file(GLOB TINYUSDZ_CORE_SOURCES
    ${TINYUSDZ_ROOT}/src/*.cc
    ${TINYUSDZ_ROOT}/src/tydra/*.cc
)

# Exclude test files and examples from core
list(FILTER TINYUSDZ_CORE_SOURCES EXCLUDE REGEX ".*test.*")
list(FILTER TINYUSDZ_CORE_SOURCES EXCLUDE REGEX ".*main.*")

add_library(tinyusdz_core STATIC ${TINYUSDZ_CORE_SOURCES})
target_link_libraries(tinyusdz_core PUBLIC blake3)

# PCP CLI executable
add_executable(pcp_cli
    pcp_cli.cc
)

# Link libraries
target_link_libraries(pcp_cli
    tinyusdz_core
    blake3
    pthread
)

# If using nlohmann/json (optional - can be header-only)
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(pcp_cli nlohmann_json::nlohmann_json)
else()
    # Use bundled json header
    target_include_directories(pcp_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Copy example files
file(COPY examples DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Create test USD files
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_scene.usda
"#usda 1.0

def Xform \"World\" {
    def Xform \"Props\" {
        def Xform \"Chair\" (
            instanceable = true
        ) {
            def Mesh \"Seat\" {}
            def Mesh \"Back\" {}
            def Mesh \"Legs\" {}
        }
    }
}
")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_reference.usda
"#usda 1.0

def Xform \"Model\" {
    def Mesh \"Geometry\" {
        int[] faceVertexCounts = [4, 4, 4]
        point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
    }
}
")

# Installation
install(TARGETS pcp_cli DESTINATION bin)
install(FILES composition_schema.json DESTINATION share/pcp)
install(DIRECTORY examples DESTINATION share/pcp)

# Add test target
enable_testing()

# Basic test
add_test(NAME pcp_basic
    COMMAND pcp_cli ${CMAKE_CURRENT_BINARY_DIR}/test_scene.usda
)

# Test with instructions
add_test(NAME pcp_instructions
    COMMAND pcp_cli
        ${CMAKE_CURRENT_BINARY_DIR}/test_scene.usda
        ${CMAKE_CURRENT_BINARY_DIR}/examples/basic_composition.json
)

# Test JSON output
add_test(NAME pcp_json_output
    COMMAND pcp_cli
        ${CMAKE_CURRENT_BINARY_DIR}/test_scene.usda
        --format json
)

# Print configuration
message(STATUS "PCP Example Configuration:")
message(STATUS "  TinyUSDZ root: ${TINYUSDZ_ROOT}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")

# Documentation
add_custom_target(pcp_docs
    COMMAND echo "Generating PCP documentation..."
    COMMAND doxygen ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
)