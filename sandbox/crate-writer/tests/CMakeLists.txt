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

# Build crate-writer library objects to link
add_library(crate_writer_objs OBJECT
    ${CRATE_WRITER_ROOT}/src/crate-writer.cc
    ${TINYUSDZ_ROOT}/src/timesamples.cc
    ${TINYUSDZ_ROOT}/src/prim-types.cc
    ${TINYUSDZ_ROOT}/src/value-types.cc
    ${TINYUSDZ_ROOT}/src/lz4-compression.cc
    ${TINYUSDZ_ROOT}/src/crate-format.cc
    ${TINYUSDZ_ROOT}/src/linear-algebra.cc
    ${TINYUSDZ_ROOT}/src/tiny-format.cc
    ${TINYUSDZ_ROOT}/src/str-util.cc
    ${TINYUSDZ_ROOT}/src/xform.cc
    ${TINYUSDZ_ROOT}/src/integerCoding.cpp
)

target_include_directories(crate_writer_objs PRIVATE
    ${CRATE_WRITER_ROOT}/include
    ${PATH_SORT_ROOT}/include
    ${TINYUSDZ_ROOT}/src
    ${TINYUSDZ_ROOT}/src/external
)

target_compile_options(crate_writer_objs PRIVATE
    -Wall
    -Wno-deprecated
    -O2
    -g
)

# Build path-sort library objects
add_library(path_sort_objs OBJECT
    ${PATH_SORT_ROOT}/src/path_sort.cc
    ${PATH_SORT_ROOT}/src/tree_encode.cc
)

target_include_directories(path_sort_objs PRIVATE
    ${PATH_SORT_ROOT}/include
)

target_compile_options(path_sort_objs PRIVATE
    -Wall
    -O2
    -g
)

# Link libraries
target_link_libraries(test_roundtrip
    crate_writer_objs
    lz4_objs
    path_sort_objs
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
