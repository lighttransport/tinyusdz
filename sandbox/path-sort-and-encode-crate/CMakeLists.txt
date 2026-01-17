cmake_minimum_required(VERSION 3.16)

project(path-sort-validation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Modular Library (for integration with other projects)
# ============================================================================

# Core library - no external dependencies
add_library(crate-encoding STATIC
  src/path_sort.cc
  src/tree_encode.cc
)

target_include_directories(crate-encoding PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================================
# Validation and Testing (requires OpenUSD)
# ============================================================================

option(BUILD_VALIDATE_PATH_SORT "Build validate-path-sort executable (requires OpenUSD)" OFF)

# Build legacy path-sort library (for backwards compatibility, no OpenUSD dependency)
add_library(path-sort STATIC
  path-sort.cc
  path-sort-api.cc
  tree-encode.cc
)

target_include_directories(path-sort PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build tree encoding test (no OpenUSD dependency)
add_executable(test-tree-encode
  test-tree-encode.cc
)

target_link_libraries(test-tree-encode
  path-sort
)

if(BUILD_VALIDATE_PATH_SORT)
  # Find OpenUSD
  find_package(pxr REQUIRED)

  # Path to OpenUSD installation (use dist or dist_monolithic)
  set(USD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist")

  if(NOT EXISTS ${USD_ROOT})
    set(USD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist_monolithic")
  endif()

  message(STATUS "USD_ROOT: ${USD_ROOT}")

  # Include directories - Put USD_ROOT first to override system headers
  target_include_directories(path-sort PUBLIC
    ${USD_ROOT}/include
  )

  # Link directories
  link_directories(${USD_ROOT}/lib)

  # Build validation executable
  add_executable(validate-path-sort
    validate-path-sort.cc
  )

  target_link_libraries(validate-path-sort
    path-sort
    ${USD_ROOT}/lib/libusd_sdf.so
    ${USD_ROOT}/lib/libusd_tf.so
    ${USD_ROOT}/lib/libusd_vt.so
    ${USD_ROOT}/lib/libusd_ar.so
    ${USD_ROOT}/lib/libusd_arch.so
    ${USD_ROOT}/lib/libusd_gf.so
    ${USD_ROOT}/lib/libusd_js.so
    ${USD_ROOT}/lib/libusd_kind.so
    ${USD_ROOT}/lib/libusd_plug.so
    ${USD_ROOT}/lib/libusd_trace.so
    ${USD_ROOT}/lib/libusd_work.so
    pthread
    dl
    ${USD_ROOT}/lib/libtbb.so
  )

  # Set RPATH for runtime
  set_target_properties(validate-path-sort PROPERTIES
    BUILD_RPATH "${USD_ROOT}/lib"
    INSTALL_RPATH "${USD_ROOT}/lib"
  )

  message(STATUS "  Run path sorting validation: ./validate-path-sort")
endif()

message(STATUS "Configuration complete!")
message(STATUS "  Build all: make")
message(STATUS "  Run tree encoding tests: ./test-tree-encode")
