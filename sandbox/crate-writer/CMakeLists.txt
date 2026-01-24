cmake_minimum_required(VERSION 3.16)

project(crate-writer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link TinyUSDZ library from parent build
set(TINYUSDZ_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build")
set(TINYUSDZ_STATIC_LIB "${TINYUSDZ_BUILD_DIR}/libtinyusdz_static.a")

if(NOT EXISTS "${TINYUSDZ_STATIC_LIB}")
  message(FATAL_ERROR "TinyUSDZ static library not found at: ${TINYUSDZ_STATIC_LIB}\nPlease build TinyUSDZ first: cd ../../build && make tinyusdz_static")
endif()

message(STATUS "Using TinyUSDZ library: ${TINYUSDZ_STATIC_LIB}")

# ============================================================================
# Experimental USDC Crate Writer Library
# ============================================================================

# Core writer library
add_library(crate-writer STATIC
  src/crate-writer.cc
  src/stage-converter.cc
)

target_include_directories(crate-writer PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src      # For crate-format.hh, prim-types.hh
  ${CMAKE_CURRENT_SOURCE_DIR}/../../         # For tinyusdz includes
  ${CMAKE_CURRENT_SOURCE_DIR}/../path-sort-and-encode-crate/include  # For path encoding
)

# Link with path encoding library
add_subdirectory(../path-sort-and-encode-crate ${CMAKE_CURRENT_BINARY_DIR}/path-sort-and-encode-crate)

target_link_libraries(crate-writer
  crate-encoding
  ${TINYUSDZ_STATIC_LIB}
)

# ============================================================================
# Examples
# ============================================================================

option(BUILD_CRATE_WRITER_EXAMPLES "Build crate-writer examples" ON)

if(BUILD_CRATE_WRITER_EXAMPLES)
  # Example: Basic usage
  add_executable(example_write
    examples/example_write.cc
  )

  target_link_libraries(example_write
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(example_write PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Example: Minimal simple usage (inline values only)
  add_executable(simple_write
    examples/simple_write.cc
  )

  target_link_libraries(simple_write
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(simple_write PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Example: Batch USDA to USDC conversion
  add_executable(batch_convert_usda
    examples/batch_convert_usda.cc
  )

  target_link_libraries(batch_convert_usda
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(batch_convert_usda PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Example: Validate USDC files
  add_executable(validate_usdc
    examples/validate_usdc.cc
  )

  target_link_libraries(validate_usdc
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(validate_usdc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Example: Test Layer/PrimSpec Conversion
  add_executable(test_layer_conversion
    examples/test_layer_conversion.cc
  )

  target_link_libraries(test_layer_conversion
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_layer_conversion PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )
endif()

# ============================================================================
# Tests
# ============================================================================

option(BUILD_CRATE_WRITER_TESTS "Build crate-writer tests" ON)

if(BUILD_CRATE_WRITER_TESTS)
  # Test: TinyUSDZ integer compression
  add_executable(test_integer_compression
    tests/test_integer_compression.cc
  )

  target_link_libraries(test_integer_compression
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_integer_compression PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Test: Round-trip validation (write → read → verify)
  add_executable(test_roundtrip
    tests/test_roundtrip.cc
  )

  target_link_libraries(test_roundtrip
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_roundtrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Test: Comprehensive feature demonstration
  add_executable(test_comprehensive
    tests/test_comprehensive.cc
  )

  target_link_libraries(test_comprehensive
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_comprehensive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Test: Additional test scenes (deduplication, compression, etc.)
  add_executable(test_scenes
    tests/test_scenes.cc
  )

  target_link_libraries(test_scenes
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_scenes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Test: TimeSamples (animation) support
  add_executable(test_timesamples
    tests/test_timesamples.cc
  )

  target_link_libraries(test_timesamples
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_timesamples PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

  # Test: OpenUSD integer compression (optional, requires OpenUSD)
  find_path(OPENUSD_INCLUDE_DIR pxr/usd/sdf/integerCoding.h
    PATHS
      ${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist_monolithic/include
      /usr/local/include
  )

  find_library(OPENUSD_SDF_LIB NAMES usd_sdf sdf
    PATHS
      ${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist/lib
      ${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist_monolithic/lib
      /usr/local/lib
  )

  find_library(OPENUSD_TF_LIB NAMES usd_tf tf
    PATHS
      ${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist/lib
      ${CMAKE_CURRENT_SOURCE_DIR}/../../aousd/dist_monolithic/lib
      /usr/local/lib
  )

  if(OPENUSD_INCLUDE_DIR AND OPENUSD_SDF_LIB AND OPENUSD_TF_LIB)
    message(STATUS "Found OpenUSD, building OpenUSD integer compression test")

    add_executable(test_openusd_integer_compression
      tests/test_openusd_integer_compression.cc
    )

    target_link_libraries(test_openusd_integer_compression
      ${OPENUSD_SDF_LIB}
      ${OPENUSD_TF_LIB}
    )

    target_include_directories(test_openusd_integer_compression PRIVATE
      ${OPENUSD_INCLUDE_DIR}
    )
  else()
    message(STATUS "OpenUSD not found, skipping OpenUSD integer compression test")
  endif()

  # Test: OpenUSD deduplication verification
  add_executable(test_openusd_dedup_verify
    tests/test_openusd_dedup_verify.cc
  )

  target_link_libraries(test_openusd_dedup_verify
    crate-writer
    crate-encoding
    ${TINYUSDZ_STATIC_LIB}
  )

  target_include_directories(test_openusd_dedup_verify PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
  )

endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "============================================================")
message(STATUS "Experimental Crate Writer Configuration")
message(STATUS "============================================================")
message(STATUS "  Core library: crate-writer (always built)")
message(STATUS "  Dependencies: crate-encoding (path sorting/encoding)")
if(BUILD_CRATE_WRITER_EXAMPLES)
  message(STATUS "  Examples: example_write (enabled)")
else()
  message(STATUS "  Examples: (disabled, use -DBUILD_CRATE_WRITER_EXAMPLES=ON)")
endif()
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  make                    # Build all targets")
message(STATUS "")
message(STATUS "Run examples:")
if(BUILD_CRATE_WRITER_EXAMPLES)
  message(STATUS "  ./example_write         # Basic usage example")
endif()
message(STATUS "============================================================")
message(STATUS "")
