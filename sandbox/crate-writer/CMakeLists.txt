cmake_minimum_required(VERSION 3.16)

project(crate-writer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Experimental USDC Crate Writer Library
# ============================================================================

# Core writer library
add_library(crate-writer STATIC
  src/crate-writer.cc
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
  )

  target_include_directories(example_write PRIVATE
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
