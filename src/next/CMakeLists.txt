# SPDX-License-Identifier: Apache-2.0
# Copyright 2024-Present Light Transport Entertainment Inc.
#
# TinyUSDZ Next - CMake build configuration
# New modular architecture with minimal template usage

cmake_minimum_required(VERSION 3.10)
project(tinyusdz_next C CXX)

# Define source files
set(TINYUSDZ_NEXT_SOURCES
    # Type system
    types/type-info.cc
    types/value.cc

    # Prim types
    prim/path.cc
    prim/attribute.cc
    prim/prim.cc

    # Layer (unified PrimSpec storage)
    layer/property-index.cc
    layer/prim-spec.cc
    layer/layer.cc

    # Stage (composed scene)
    stage/stage.cc

    # ASCII parser
    parser/lexer.cc
    parser/value-parser.cc
    parser/ascii-parser.cc

    # Crate (binary) reader/writer
    crate/crate-format.cc
    crate/crate-reader.cc
    crate/crate-writer.cc

    # High-level readers
    reader/usda-reader.cc
    reader/usdc-reader.cc

    # Writers
    writer/value-printer.cc
    writer/prim-printer.cc
    writer/usda-writer.cc
    writer/usdc-writer.cc

    # Schema convenience APIs
    schema/geom-mesh.cc
    schema/geom-xform.cc
)

# Define header files (for IDE project generation)
set(TINYUSDZ_NEXT_HEADERS
    # Type system
    types/type-id.hh
    types/type-info.hh
    types/value.hh

    # Prim types
    prim/path.hh
    prim/attribute.hh
    prim/prim.hh

    # Layer (unified PrimSpec storage)
    layer/property-index.hh
    layer/prim-spec.hh
    layer/layer.hh

    # Stage (composed scene)
    stage/stage.hh

    # ASCII parser
    parser/lexer.hh
    parser/value-parser.hh
    parser/ascii-parser.hh

    # Crate (binary) reader/writer
    crate/crate-format.hh
    crate/crate-reader.hh
    crate/crate-writer.hh
    crate/stream-reader.hh

    # High-level readers
    reader/usda-reader.hh
    reader/usdc-reader.hh

    # Writers
    writer/value-printer.hh
    writer/prim-printer.hh
    writer/usda-writer.hh
    writer/usdc-writer.hh

    # Schema convenience APIs
    schema/geom-mesh.hh
    schema/geom-xform.hh
)

# LZ4 source (from existing TinyUSDZ)
set(LZ4_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lz4/lz4.c
)

# Prepend source directory to all files
list(TRANSFORM TINYUSDZ_NEXT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM TINYUSDZ_NEXT_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# Create static library
add_library(tinyusdz_next STATIC
    ${TINYUSDZ_NEXT_SOURCES}
    ${TINYUSDZ_NEXT_HEADERS}
    ${LZ4_SOURCES}
)

# Set C++14 standard
target_compile_features(tinyusdz_next PUBLIC cxx_std_14)

# Set include directories
target_include_directories(tinyusdz_next PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

# Include LZ4 header directory
target_include_directories(tinyusdz_next PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../lz4
)

# Compiler warnings
if(MSVC)
    target_compile_options(tinyusdz_next PRIVATE /W4)
else()
    target_compile_options(tinyusdz_next PRIVATE -Wall -Wextra -Wpedantic)
    # Disable some warnings for LZ4 C code
    set_source_files_properties(${LZ4_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
endif()

# Optional: Create test executable
option(TINYUSDZ_NEXT_BUILD_TESTS "Build tests for TinyUSDZ Next" OFF)

if(TINYUSDZ_NEXT_BUILD_TESTS)
    add_executable(test_tinyusdz_next
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_next.cc
    )
    target_link_libraries(test_tinyusdz_next PRIVATE tinyusdz_next)

    add_executable(test_usdc_reader
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdc.cc
    )
    target_link_libraries(test_usdc_reader PRIVATE tinyusdz_next)

    add_executable(test_layer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_layer.cc
    )
    target_link_libraries(test_layer PRIVATE tinyusdz_next)

    add_executable(test_stage
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_stage.cc
    )
    target_link_libraries(test_stage PRIVATE tinyusdz_next)

    add_executable(test_writer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_writer.cc
    )
    target_link_libraries(test_writer PRIVATE tinyusdz_next)

    add_executable(test_usdc_writer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdc_writer.cc
    )
    target_link_libraries(test_usdc_writer PRIVATE tinyusdz_next)
endif()

# Tydra Next (render data conversion)
# Define tydra/next source files
set(TYDRA_NEXT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.cc
)

set(TYDRA_NEXT_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/chunked-array.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.hh
)

add_library(tydra_next STATIC
    ${TYDRA_NEXT_SOURCES}
    ${TYDRA_NEXT_HEADERS}
)

target_link_libraries(tydra_next PUBLIC tinyusdz_next)
target_compile_features(tydra_next PUBLIC cxx_std_14)

target_include_directories(tydra_next PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(tydra_next PRIVATE /W4)
else()
    target_compile_options(tydra_next PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tydra Next test
if(TINYUSDZ_NEXT_BUILD_TESTS)
    add_executable(test_tydra_next
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/tydra/next/test_tydra_next.cc
    )
    target_link_libraries(test_tydra_next PRIVATE tydra_next tinyusdz_next)
endif()
