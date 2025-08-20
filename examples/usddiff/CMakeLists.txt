# SPDX-License-Identifier: Apache 2.0
# Copyright 2025-Present Light Transport Entertainment, Inc.

cmake_minimum_required(VERSION 3.5.1)

set(BUILD_TARGET "usddiff")

project(${BUILD_TARGET} CXX)

# options
option(TINYUSDZ_WITH_USDDIFF "Build usddiff example" ON)

if(NOT TINYUSDZ_WITH_USDDIFF)
  return()
endif()

# Use C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# local files
set(SOURCES
  usddiff-main.cc
)

set(TINYUSDZ_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TINYUSDZ_SOURCES_DIR "${TINYUSDZ_ROOT_DIR}/src")

# Include directories
include_directories("${TINYUSDZ_ROOT_DIR}")
include_directories("${TINYUSDZ_SOURCES_DIR}")

# Create executable
add_executable(${BUILD_TARGET} ${SOURCES})

# Link with tinyusdz library
if(TARGET tinyusdz_static)
  target_link_libraries(${BUILD_TARGET} tinyusdz_static)
elseif(TARGET tinyusdz)
  target_link_libraries(${BUILD_TARGET} tinyusdz)
else()
  # If we're building as a standalone project, try to find the library
  find_library(TINYUSDZ_LIB tinyusdz PATHS "${TINYUSDZ_ROOT_DIR}/build" NO_DEFAULT_PATH)
  if(TINYUSDZ_LIB)
    target_link_libraries(${BUILD_TARGET} ${TINYUSDZ_LIB})
  else()
    message(FATAL_ERROR "Could not find tinyusdz library")
  endif()
endif()

# Set compiler-specific options
if(CMAKE_COMPILER_IS_GNUCXX)
  target_compile_options(${BUILD_TARGET} PRIVATE -Wall -Wextra)
endif()

if(MSVC)
  target_compile_options(${BUILD_TARGET} PRIVATE /W3)
endif()

# Installation
install(TARGETS ${BUILD_TARGET} DESTINATION bin)