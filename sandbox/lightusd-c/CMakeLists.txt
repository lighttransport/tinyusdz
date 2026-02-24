cmake_minimum_required(VERSION 3.10)
project(lightusd-c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Strict warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
elseif(MSVC)
  add_compile_options(/W4)
endif()

# Sanitizers
option(LUSD_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(LUSD_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
option(LUSD_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)

if(LUSD_SANITIZE_ADDRESS)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
endif()

if(LUSD_SANITIZE_UNDEFINED)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
  endif()
endif()

if(LUSD_SANITIZE_THREAD)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
  endif()
endif()

# Public headers
set(LUSD_PUBLIC_HEADERS
  include/lightusd/lightusd-c.h
  include/lightusd/lusd_platform.h
  include/lightusd/lusd_version.h
  include/lightusd/lusd_result.h
  include/lightusd/lusd_handles.h
  include/lightusd/lusd_enums.h
  include/lightusd/lusd_structs.h
  include/lightusd/lusd_allocator.h
  include/lightusd/lusd_instance.h
  include/lightusd/lusd_stage.h
  include/lightusd/lusd_prim.h
  include/lightusd/lusd_attribute.h
  include/lightusd/lusd_relationship.h
  include/lightusd/lusd_property.h
  include/lightusd/lusd_value.h
  include/lightusd/lusd_path.h
  include/lightusd/lusd_token.h
  include/lightusd/lusd_timesamples.h
  include/lightusd/lusd_composition.h
  include/lightusd/lusd_layer.h
  include/lightusd/lusd_write.h
  include/lightusd/lusd_stream.h
  include/lightusd/lusd_schema.h
  include/lightusd/lusd_arena.h
  include/lightusd/lusd_diagnostics.h
)

# Source files
set(LUSD_SOURCES
  # Phase 1: Foundation
  src/lusd_result.c
  src/lusd_allocator.c
  src/lusd_instance.c
  src/lusd_string.c
  src/lusd_hash.c
  src/lusd_handle_table.c
  # Phase 2: Core types
  src/lusd_token.c
  src/lusd_path.c
  src/lusd_value.c
  src/lusd_value_array.c
  src/lusd_half.c
  # Stubs (future phases)
  src/lusd_stage.c
  src/lusd_prim.c
  src/lusd_attribute.c
  src/lusd_relationship.c
  src/lusd_property.c
  src/lusd_timesamples.c
  src/lusd_composition.c
  src/lusd_layer.c
  src/lusd_write_usda.c
  src/lusd_read_usda.c
  src/lusd_read_usdc.c
  src/lusd_read_usdz.c
  src/lusd_stream.c
  src/lusd_schema.c
  src/lusd_arena.c
  src/lusd_diagnostics.c
  src/lusd_lz4.c
  # Phase 5: Native USDC reader
  src/lusd_usdc_reader.c
)

# Optional tinyusdz backend: replaces C stubs for stage/prim/attribute with
# a C++ bridge that calls tinyusdz to actually parse USD files.
# Set LUSD_TINYUSDZ_DIR to the tinyusdz source root before add_subdirectory.
option(LUSD_TINYUSDZ_DIR
  "Path to tinyusdz source directory (enables tinyusdz USD backend)" "")

if(LUSD_TINYUSDZ_DIR)
  # Remove the pure-C stubs that the C++ bridge replaces
  list(REMOVE_ITEM LUSD_SOURCES
    src/lusd_stage.c
    src/lusd_prim.c
    src/lusd_attribute.c
  )
  # Add the C++ bridge
  list(APPEND LUSD_SOURCES src/lusd_stage_tinyusdz.cc)
endif()

# Static library
add_library(lightusd-c STATIC ${LUSD_SOURCES} ${LUSD_PUBLIC_HEADERS})
target_include_directories(lightusd-c
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Math library on Unix
if(UNIX)
  target_link_libraries(lightusd-c PRIVATE m)
endif()

# tinyusdz backend: add includes and link
if(LUSD_TINYUSDZ_DIR)
  target_include_directories(lightusd-c PRIVATE
    "${LUSD_TINYUSDZ_DIR}/src"
    "${LUSD_TINYUSDZ_DIR}/src/external"
  )
  target_link_libraries(lightusd-c PRIVATE tinyusdz_static)
  # The library now contains C++ translation units, ensure the linker knows
  set_target_properties(lightusd-c PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Tests
option(LUSD_BUILD_TESTS "Build lightusd-c tests" ON)
if(LUSD_BUILD_TESTS)
  add_executable(test_lightusd_c tests/test_main.c)
  target_link_libraries(test_lightusd_c PRIVATE lightusd-c)
endif()

# Examples
option(LUSD_BUILD_EXAMPLES "Build lightusd-c examples" ON)
if(LUSD_BUILD_EXAMPLES)
  add_executable(lightusd_c_basic examples/basic.c)
  target_link_libraries(lightusd_c_basic PRIVATE lightusd-c)
endif()

# ============================================================================
# Lydra — Minimal OpenGL/Vulkan-friendly data conversion utilities
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LYDRA_HEADERS
  lydra/lydra.hh
  lydra/lydra_mesh.hh
  lydra/lydra_transform.hh
  lydra/lydra_material.hh
  lydra/lydra_scene.hh
  lydra/lydra_c.h
)

set(LYDRA_SOURCES
  lydra/lydra_mesh.cc
  lydra/lydra_transform.cc
  lydra/lydra_material.cc
  lydra/lydra_scene.cc
  lydra/lydra_c.cc
)

add_library(lydra STATIC ${LYDRA_SOURCES} ${LYDRA_HEADERS})
target_include_directories(lydra
  PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lydra>
  # Privileged access to lightusd-c internal headers for value materialization
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(lydra PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(lydra PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
elseif(MSVC)
  target_compile_options(lydra PRIVATE /W4)
endif()

# Math library on Unix
if(UNIX)
  target_link_libraries(lydra PRIVATE m)
endif()

# Lydra tests
option(LYDRA_BUILD_TESTS "Build lydra tests" ON)
if(LYDRA_BUILD_TESTS)
  # C++ tests
  add_executable(test_lydra lydra/tests/test_lydra.cc)
  target_link_libraries(test_lydra PRIVATE lydra)
  set_target_properties(test_lydra PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
  )

  # C tests (links lydra which is C++ so needs CXX linker)
  add_executable(test_lydra_c lydra/tests/test_lydra_c.c)
  target_link_libraries(test_lydra_c PRIVATE lydra)
  target_include_directories(test_lydra_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lydra)
  set_target_properties(test_lydra_c PROPERTIES LINKER_LANGUAGE CXX)
endif()
