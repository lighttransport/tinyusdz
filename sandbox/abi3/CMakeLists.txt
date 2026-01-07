# SPDX-License-Identifier: Apache 2.0
#
# CMake build for TinyUSDZ Python ABI3 binding
#
# This builds a Python extension module using the stable ABI (limited API)
# for Python 3.10+, without requiring Python development headers at build time.

cmake_minimum_required(VERSION 3.15)
project(tinyusdz_abi3 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Options
option(BUILD_SHARED_LIBS "Build shared library" ON)

# Find Python (runtime only, no dev headers needed)
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

# Detect platform
if(WIN32)
    set(PYTHON_EXT_SUFFIX ".pyd")
    set(PYTHON_LIB_NAME "python3")
elseif(APPLE)
    set(PYTHON_EXT_SUFFIX ".so")
    set(PYTHON_LIB_NAME "python3")
else()
    set(PYTHON_EXT_SUFFIX ".so")
    set(PYTHON_LIB_NAME "python3")
endif()

# TinyUSDZ C API library
set(TINYUSDZ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(TINYUSDZ_SRC_DIR ${TINYUSDZ_ROOT}/src)

# Build or link against TinyUSDZ C API
# For this experiment, we'll compile the C API directly
add_library(tinyusdz_c STATIC
    ${TINYUSDZ_SRC_DIR}/c-tinyusd.cc
    ${TINYUSDZ_SRC_DIR}/tinyusdz.cc
    ${TINYUSDZ_SRC_DIR}/stage.cc
    ${TINYUSDZ_SRC_DIR}/prim-types.cc
    ${TINYUSDZ_SRC_DIR}/value-types.cc
    # Add more sources as needed...
)

target_include_directories(tinyusdz_c PUBLIC
    ${TINYUSDZ_SRC_DIR}
)

target_compile_definitions(tinyusdz_c PRIVATE
    TINYUSDZ_PRODUCTION_BUILD=1
)

# Python ABI3 extension module
add_library(tinyusdz_abi3 MODULE
    src/tinyusdz_abi3.c
)

target_include_directories(tinyusdz_abi3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${TINYUSDZ_SRC_DIR}
)

# Define Py_LIMITED_API for stable ABI
target_compile_definitions(tinyusdz_abi3 PRIVATE
    Py_LIMITED_API=0x030a0000
)

# Link against TinyUSDZ C API
target_link_libraries(tinyusdz_abi3 PRIVATE
    tinyusdz_c
)

# On Windows, link against python3.lib
# On Unix, we don't need to link against Python (loaded dynamically)
if(WIN32)
    # Find Python library
    find_library(PYTHON3_LIB
        NAMES python310 python311 python312 python313 python3
        HINTS ${Python3_LIBRARY_DIRS}
    )
    if(PYTHON3_LIB)
        target_link_libraries(tinyusdz_abi3 PRIVATE ${PYTHON3_LIB})
    endif()
endif()

# Set output name and properties
set_target_properties(tinyusdz_abi3 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "tinyusdz_abi3"
    SUFFIX ${PYTHON_EXT_SUFFIX}
)

# Remove 'lib' prefix on Unix
if(UNIX)
    set_target_properties(tinyusdz_abi3 PROPERTIES PREFIX "")
endif()

# Installation
install(TARGETS tinyusdz_abi3
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
)

# Print configuration
message(STATUS "TinyUSDZ ABI3 Binding Configuration:")
message(STATUS "  Python version: ${Python3_VERSION}")
message(STATUS "  Python executable: ${Python3_EXECUTABLE}")
message(STATUS "  Extension suffix: ${PYTHON_EXT_SUFFIX}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
