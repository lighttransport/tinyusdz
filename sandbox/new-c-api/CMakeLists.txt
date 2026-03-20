cmake_minimum_required(VERSION 3.10)
project(tinyusdz_c VERSION 1.0.0 LANGUAGES C CXX)

# C99 standard for the API
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# C++17 for the implementation
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_SHARED_LIBS "Build shared library" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

# Find TinyUSDZ - adjust path as needed
set(TINYUSDZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TINYUSDZ_SRC "${TINYUSDZ_ROOT}/src")

# Check that TinyUSDZ source exists
if(NOT EXISTS "${TINYUSDZ_SRC}/tinyusdz.hh")
    message(FATAL_ERROR "TinyUSDZ source not found at ${TINYUSDZ_SRC}")
endif()

# Create the C API library
add_library(tinyusdz_c
    tinyusdz_c.cpp
)

# Set public headers
set_target_properties(tinyusdz_c PROPERTIES
    PUBLIC_HEADER tinyusdz_c.h
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Include directories
target_include_directories(tinyusdz_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${TINYUSDZ_SRC}
        ${TINYUSDZ_ROOT}/src/external  # For dependencies
)

# Link with TinyUSDZ
# Note: In a real build, we'd either:
# 1. Build TinyUSDZ as a static library and link it
# 2. Include TinyUSDZ sources directly
# For now, we'll compile key TinyUSDZ sources directly

# Add TinyUSDZ sources (simplified - real build would include all needed files)
set(TINYUSDZ_SOURCES
    ${TINYUSDZ_SRC}/tinyusdz.cc
    ${TINYUSDZ_SRC}/stage.cc
    ${TINYUSDZ_SRC}/prim-types.cc
    ${TINYUSDZ_SRC}/value-types.cc
    ${TINYUSDZ_SRC}/usdGeom.cc
    ${TINYUSDZ_SRC}/usdShade.cc
    ${TINYUSDZ_SRC}/usdSkel.cc
    ${TINYUSDZ_SRC}/usda-reader.cc
    ${TINYUSDZ_SRC}/usdc-reader.cc
    ${TINYUSDZ_SRC}/usdc-reader-property.cc
    ${TINYUSDZ_SRC}/usdc-reader-prim.cc
    ${TINYUSDZ_SRC}/usdc-reader-reconstruct.cc
    ${TINYUSDZ_SRC}/crate-reader.cc
    ${TINYUSDZ_SRC}/ascii-parser.cc
    ${TINYUSDZ_SRC}/asset-resolution.cc
    ${TINYUSDZ_SRC}/composition.cc
    ${TINYUSDZ_SRC}/composition-reconstruct.cc
    ${TINYUSDZ_SRC}/prim-reconstruct.cc
    ${TINYUSDZ_SRC}/path.cc
    ${TINYUSDZ_SRC}/str-util.cc
    ${TINYUSDZ_SRC}/io-util.cc
    ${TINYUSDZ_SRC}/math-util.cc
    ${TINYUSDZ_SRC}/tiny-format.cc
)

# Add sources to library
target_sources(tinyusdz_c PRIVATE ${TINYUSDZ_SOURCES})

# Compiler flags
target_compile_options(tinyusdz_c PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
)

# Add sanitizers if requested
if(ENABLE_SANITIZERS)
    target_compile_options(tinyusdz_c PRIVATE -fsanitize=address,undefined)
    target_link_options(tinyusdz_c PRIVATE -fsanitize=address,undefined)
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(tinyusdz_c PRIVATE
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Export symbols for shared library
if(BUILD_SHARED_LIBS)
    target_compile_definitions(tinyusdz_c PRIVATE TINYUSDZ_C_EXPORTS)
endif()

# Build examples
if(BUILD_EXAMPLES)
    # Basic example
    add_executable(example_basic example_basic.c)
    target_link_libraries(example_basic PRIVATE tinyusdz_c)
    target_compile_options(example_basic PRIVATE
        $<$<C_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<C_COMPILER_ID:Clang>:-Wall -Wextra>
    )

    # Mesh example
    add_executable(example_mesh example_mesh.c)
    target_link_libraries(example_mesh PRIVATE tinyusdz_c)
    target_link_libraries(example_mesh PRIVATE m)  # for math functions
    target_compile_options(example_mesh PRIVATE
        $<$<C_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<C_COMPILER_ID:Clang>:-Wall -Wextra>
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS tinyusdz_c
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tinyusdz
)

# Generate pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyusdz_c.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/tinyusdz_c.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tinyusdz_c.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Print configuration summary
message(STATUS "")
message(STATUS "TinyUSDZ C API Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared library: ${BUILD_SHARED_LIBS}")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")