# SPDX-License-Identifier: Apache-2.0
# Copyright 2024 Light Transport Entertainment Inc.
#
# LightUSD - Lightweight USD library CMake configuration

cmake_minimum_required(VERSION 3.14)
project(lightusd VERSION 0.1.0 LANGUAGES C CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(LIGHTUSD_BUILD_TESTS "Build LightUSD tests" ON)
option(LIGHTUSD_BUILD_EXAMPLES "Build LightUSD examples" ON)
option(LIGHTUSD_PRODUCTION_BUILD "Production build (disable debug output)" OFF)
option(LIGHTUSD_DEBUG_PRINT "Enable DCOUT debug macro (non-production only)" OFF)

# Source files
set(LIGHTUSD_SOURCES
    src/types.cc
    src/token.cc
    src/path.cc
    src/value.cc
    src/timesamples.cc
    src/attribute.cc
    src/relationship.cc
    src/property.cc
    src/variant.cc
    src/composition.cc
    src/layer.cc
    src/prim_index.cc
    src/prim.cc
    src/stage.cc
    src/clips.cc
    src/debug.cc
    src/usda_lexer.cc
    src/usda_parser.cc
    src/usda_reader.cc
    src/usda_writer.cc
    src/crate_format.cc
    src/usdc_reader.cc
    src/usdc_fetch_handlers.cc
    src/lz4_compression.cc
    src/integer_coding.cc
    src/lz4/lz4.c
)

# Header files (for IDE support)
set(LIGHTUSD_HEADERS
    include/lightusd/lightusd.hh
    include/lightusd/types.hh
    include/lightusd/result.hh
    include/lightusd/cursor.hh
    include/lightusd/diagnostic.hh
    include/lightusd/debug.hh
    include/lightusd/token.hh
    include/lightusd/path.hh
    include/lightusd/value.hh
    include/lightusd/timesamples.hh
    include/lightusd/attribute.hh
    include/lightusd/relationship.hh
    include/lightusd/property.hh
    include/lightusd/variant.hh
    include/lightusd/composition.hh
    include/lightusd/layer.hh
    include/lightusd/prim_index.hh
    include/lightusd/prim.hh
    include/lightusd/stage.hh
    include/lightusd/clips.hh
    include/lightusd/typed_array.hh
    include/lightusd/stream_reader.hh
    include/lightusd/crate_format.hh
    include/lightusd/usda_lexer.hh
    include/lightusd/usda_parser.hh
    include/lightusd/usda_reader.hh
    include/lightusd/usda_writer.hh
    include/lightusd/usdc_reader.hh
    include/lightusd/usdc_fetch.hh
    include/lightusd/lz4_compression.hh
    include/lightusd/integer_coding.hh
)

# Library target
add_library(lightusd STATIC ${LIGHTUSD_SOURCES} ${LIGHTUSD_HEADERS})

target_include_directories(lightusd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Debug/Production build configuration
if(LIGHTUSD_PRODUCTION_BUILD)
    target_compile_definitions(lightusd PUBLIC LIGHTUSD_PRODUCTION_BUILD)
    message(STATUS "LightUSD: Production build enabled (debug output disabled)")
else()
    if(LIGHTUSD_DEBUG_PRINT)
        target_compile_definitions(lightusd PUBLIC LIGHTUSD_DEBUG_PRINT)
        message(STATUS "LightUSD: Debug print (DCOUT) enabled")
    endif()
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(lightusd PRIVATE /W4)
else()
    target_compile_options(lightusd PRIVATE -Wall -Wextra -Wpedantic)
endif()

# No exceptions, no RTTI
if(MSVC)
    # MSVC: Disable exceptions
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(lightusd PRIVATE /EHs-c- /GR-)
else()
    target_compile_options(lightusd PRIVATE -fno-exceptions -fno-rtti)
endif()

# Tests
if(LIGHTUSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(LIGHTUSD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install rules
include(GNUInstallDirs)

install(TARGETS lightusd
    EXPORT lightusd-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/lightusd
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT lightusd-targets
    FILE lightusd-targets.cmake
    NAMESPACE lightusd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lightusd
)
