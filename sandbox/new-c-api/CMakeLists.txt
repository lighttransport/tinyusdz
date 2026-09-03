cmake_minimum_required(VERSION 3.10)
project(lightusd_c VERSION 1.0.0 LANGUAGES C CXX)

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

# Find LightUSD - adjust path as needed
set(LIGHTUSD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LIGHTUSD_SRC "${LIGHTUSD_ROOT}/src")

# Check that LightUSD source exists
if(NOT EXISTS "${LIGHTUSD_SRC}/lightusd.hh")
    message(FATAL_ERROR "LightUSD source not found at ${LIGHTUSD_SRC}")
endif()

# Create the C API library
add_library(lightusd_c
    lightusd_c.cpp
)

# Set public headers
set_target_properties(lightusd_c PROPERTIES
    PUBLIC_HEADER lightusd_c.h
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Include directories
target_include_directories(lightusd_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${LIGHTUSD_SRC}
        ${LIGHTUSD_ROOT}/src/external  # For dependencies
)

# Link with LightUSD
# Note: In a real build, we'd either:
# 1. Build LightUSD as a static library and link it
# 2. Include LightUSD sources directly
# For now, we'll compile key LightUSD sources directly

# Add LightUSD sources (simplified - real build would include all needed files)
set(LIGHTUSD_SOURCES
    ${LIGHTUSD_SRC}/lightusd.cc
    ${LIGHTUSD_SRC}/stage.cc
    ${LIGHTUSD_SRC}/prim-types.cc
    ${LIGHTUSD_SRC}/value-types.cc
    ${LIGHTUSD_SRC}/usdGeom.cc
    ${LIGHTUSD_SRC}/usdShade.cc
    ${LIGHTUSD_SRC}/usdSkel.cc
    ${LIGHTUSD_SRC}/usda-reader.cc
    ${LIGHTUSD_SRC}/usdc-reader.cc
    ${LIGHTUSD_SRC}/usdc-reader-property.cc
    ${LIGHTUSD_SRC}/usdc-reader-prim.cc
    ${LIGHTUSD_SRC}/usdc-reader-reconstruct.cc
    ${LIGHTUSD_SRC}/crate-reader.cc
    ${LIGHTUSD_SRC}/crate-reader-arrays.cc
    ${LIGHTUSD_SRC}/crate-reader-values.cc
    ${LIGHTUSD_SRC}/crate-reader-paths.cc
    ${LIGHTUSD_SRC}/ascii-parser.cc
    ${LIGHTUSD_SRC}/ascii-parser-props.cc
    ${LIGHTUSD_SRC}/ascii-parser-entry.cc
    ${LIGHTUSD_SRC}/asset-resolution.cc
    ${LIGHTUSD_SRC}/composition.cc
    ${LIGHTUSD_SRC}/composition-reconstruct.cc
    ${LIGHTUSD_SRC}/prim-reconstruct.cc
    ${LIGHTUSD_SRC}/prim-reconstruct-shader.cc
    ${LIGHTUSD_SRC}/path.cc
    ${LIGHTUSD_SRC}/str-util.cc
    ${LIGHTUSD_SRC}/io-util.cc
    ${LIGHTUSD_SRC}/math-util.cc
    ${LIGHTUSD_SRC}/tiny-format.cc
)

# Add sources to library
target_sources(lightusd_c PRIVATE ${LIGHTUSD_SOURCES})

# Compiler flags
target_compile_options(lightusd_c PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
)

# Add sanitizers if requested
if(ENABLE_SANITIZERS)
    target_compile_options(lightusd_c PRIVATE -fsanitize=address,undefined)
    target_link_options(lightusd_c PRIVATE -fsanitize=address,undefined)
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(lightusd_c PRIVATE
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Export symbols for shared library
if(BUILD_SHARED_LIBS)
    target_compile_definitions(lightusd_c PRIVATE LIGHTUSD_C_EXPORTS)
endif()

# Build examples
if(BUILD_EXAMPLES)
    # Basic example
    add_executable(example_basic example_basic.c)
    target_link_libraries(example_basic PRIVATE lightusd_c)
    target_compile_options(example_basic PRIVATE
        $<$<C_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<C_COMPILER_ID:Clang>:-Wall -Wextra>
    )

    # Mesh example
    add_executable(example_mesh example_mesh.c)
    target_link_libraries(example_mesh PRIVATE lightusd_c)
    target_link_libraries(example_mesh PRIVATE m)  # for math functions
    target_compile_options(example_mesh PRIVATE
        $<$<C_COMPILER_ID:GNU>:-Wall -Wextra>
        $<$<C_COMPILER_ID:Clang>:-Wall -Wextra>
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS lightusd_c
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lightusd
)

# Generate pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lightusd_c.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/lightusd_c.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lightusd_c.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Print configuration summary
message(STATUS "")
message(STATUS "LightUSD C API Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared library: ${BUILD_SHARED_LIBS}")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")