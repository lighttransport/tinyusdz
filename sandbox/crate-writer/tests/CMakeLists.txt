cmake_minimum_required(VERSION 3.16)
project(crate_writer_tests C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Paths
set(OPENUSD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../aousd/dist" CACHE PATH "Path to OpenUSD installation")
set(TINYUSDZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "Path to TinyUSDZ source")
set(CRATE_WRITER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Path to crate-writer")
set(PATH_SORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../path-sort-and-encode-crate" CACHE PATH "Path to path-sort library")

# Find TBB (required by OpenUSD)
find_package(TBB REQUIRED)

# Find Python (required by OpenUSD)
find_package(Python3 COMPONENTS Development REQUIRED)

# Find Python includes (required by OpenUSD)
execute_process(
    COMMAND python3-config --includes
    OUTPUT_VARIABLE PYTHON_INCLUDES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "-I" "" PYTHON_INCLUDES "${PYTHON_INCLUDES}")
string(REPLACE " " ";" PYTHON_INCLUDE_DIRS "${PYTHON_INCLUDES}")

# Add OpenUSD include directories
include_directories(
    ${OPENUSD_ROOT}/include
    ${OPENUSD_ROOT}/include/boost-1_82
    ${PYTHON_INCLUDE_DIRS}
)

# Add TinyUSDZ and crate-writer include directories
# Note: crate-writer include must come first to override src/crate-writer.hh
include_directories(
    ${CRATE_WRITER_ROOT}/include
    ${PATH_SORT_ROOT}/include
    ${TINYUSDZ_ROOT}/src
    ${TINYUSDZ_ROOT}/src/external
)

# Link directories
link_directories(
    ${OPENUSD_ROOT}/lib
)

# Create executable
add_executable(test_roundtrip
    test_roundtrip.cpp
    stub-to-string.cc
)

# Compiler flags
target_compile_options(test_roundtrip PRIVATE
    -Wall
    -Wno-deprecated
    -Wno-deprecated-declarations
    -O2
    -g
)

# Preprocessor definitions (disable Python support for testing)
target_compile_definitions(test_roundtrip PRIVATE
    PXR_PYTHON_SUPPORT_ENABLED=0
)

# Link OpenUSD libraries (order matters!)
# Note: OpenUSD libraries are prefixed with 'usd_'
set(USD_LIBS
    usd_usd
    usd_usdGeom
    usd_usdShade
    usd_usdLux
    usd_sdf
    usd_pcp
    usd_kind
    usd_usdUtils
    usd_gf
    usd_tf
    usd_js
    usd_ts
    usd_work
    usd_plug
    usd_trace
    usd_arch
    usd_vt
    usd_ar
    usd_python
)

# Build LZ4 library (C code)
add_library(lz4_objs OBJECT
    ${TINYUSDZ_ROOT}/src/lz4/lz4.c
)

target_compile_options(lz4_objs PRIVATE
    -O2
    -g
)

# Build crate-writer as a subdirectory
set(BUILD_CRATE_WRITER_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CRATE_WRITER_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/crate-writer-build)

# Link libraries
target_link_libraries(test_roundtrip
    crate-writer
    ${USD_LIBS}
    TBB::tbb
    Python3::Python
    pthread
    dl
    m
)

# Set RPATH for runtime
set_target_properties(test_roundtrip PROPERTIES
    INSTALL_RPATH "${OPENUSD_ROOT}/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# ==============================================================================
# OpenUSD Deduplication Verification Test
# This test creates a USD file with deduplication enabled and is designed to be
# verified by OpenUSD tools (usdcat, Python API)
# ==============================================================================

add_executable(test_openusd_dedup_verify
    test_openusd_dedup_verify.cc
)

target_compile_options(test_openusd_dedup_verify PRIVATE
    -Wall
    -Wno-deprecated
    -Wno-deprecated-declarations
    -O2
    -g
)

target_include_directories(test_openusd_dedup_verify PRIVATE
    ${CRATE_WRITER_ROOT}/include
    ${PATH_SORT_ROOT}/include
    ${TINYUSDZ_ROOT}/src
    ${TINYUSDZ_ROOT}/src/external
)

# Link against crate-writer (no OpenUSD needed for this test)
target_link_libraries(test_openusd_dedup_verify
    crate-writer
    pthread
    dl
    m
)

# Print configuration
message(STATUS "")
message(STATUS "Crate Writer Test Configuration:")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  OpenUSD Root: ${OPENUSD_ROOT}")
message(STATUS "  TinyUSDZ Root: ${TINYUSDZ_ROOT}")
message(STATUS "  Crate Writer Root: ${CRATE_WRITER_ROOT}")
message(STATUS "")
