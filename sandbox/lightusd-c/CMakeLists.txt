cmake_minimum_required(VERSION 3.10)
project(lightusd-c VERSION 0.1.0 LANGUAGES C)

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
)

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
