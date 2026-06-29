# SPDX-License-Identifier: Apache-2.0
# Copyright 2024-Present Light Transport Entertainment Inc.
#
# Tydra Next - CMake build configuration

cmake_minimum_required(VERSION 3.10)

# Define tydra/next source files
set(TYDRA_NEXT_SOURCES
    render-data.cc
    scene-access.cc
    render-extract.cc
    render-converter.cc
)

set(TYDRA_NEXT_HEADERS
    chunked-array.hh
    render-data.hh
    scene-access.hh
    render-extract.hh
    render-converter.hh
)

# Prepend source directory to all files
list(TRANSFORM TYDRA_NEXT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM TYDRA_NEXT_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# Create static library
add_library(tydra_next STATIC
    ${TYDRA_NEXT_SOURCES}
    ${TYDRA_NEXT_HEADERS}
)

# Link to tinyusdz_next
target_link_libraries(tydra_next PUBLIC tinyusdz_next)

# Set C++14 standard
target_compile_features(tydra_next PUBLIC cxx_std_14)

# Set include directories
target_include_directories(tydra_next PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
    $<INSTALL_INTERFACE:include>
)

# Compiler warnings
if(MSVC)
    target_compile_options(tydra_next PRIVATE /W4)
else()
    target_compile_options(tydra_next PRIVATE -Wall -Wextra -Wpedantic)
endif()

# tydra_next uses no C++ RTTI and no C++ exceptions; disable both. The
# TINYUSDZ_CXX_RTTI / _EXCEPTIONS variables are inherited from the parent project
# when built as a subdirectory (undefined -> disabled when built standalone).
if(NOT TINYUSDZ_CXX_RTTI)
    if(MSVC)
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    else()
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
endif()
if(NOT TINYUSDZ_CXX_EXCEPTIONS)
    if(MSVC)
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->)
    else()
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    endif()
endif()

# Optional: Build test
option(TYDRA_NEXT_BUILD_TESTS "Build tests for Tydra Next" OFF)

if(TYDRA_NEXT_BUILD_TESTS)
    add_executable(test_tydra_next
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/tydra/next/test_tydra_next.cc
    )
    target_link_libraries(test_tydra_next PRIVATE tydra_next tinyusdz_next)
endif()
