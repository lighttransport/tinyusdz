# SPDX-License-Identifier: Apache-2.0
# Copyright 2024 Light Transport Entertainment Inc.
#
# LightUSD - Lightweight USD library CMake configuration

cmake_minimum_required(VERSION 3.14)
project(lightusd VERSION 0.1.0 LANGUAGES C CXX)

# C++ standard - C++17 required for std::optional, std::string_view, if constexpr, etc.
# Upgraded to C++20 when coroutines are enabled
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(LIGHTUSD_BUILD_TESTS "Build LightUSD tests" ON)
option(LIGHTUSD_BUILD_EXAMPLES "Build LightUSD examples" ON)
option(LIGHTUSD_PRODUCTION_BUILD "Production build (disable debug output)" OFF)
option(LIGHTUSD_DEBUG_PRINT "Enable DCOUT debug macro (non-production only)" OFF)
option(LIGHTUSD_WASM_ASYNCIFY "Enable Asyncify for async JavaScript calls (Emscripten only)" OFF)
option(LIGHTUSD_WASM_JSPI "Enable JSPI for async JavaScript calls - more efficient than Asyncify (Emscripten only, requires browser support)" OFF)
option(LIGHTUSD_COROUTINE "Enable C++20 coroutine API for async operations (requires JSPI or Asyncify)" OFF)

# Coroutine support requires C++20 and either JSPI or Asyncify
if(LIGHTUSD_COROUTINE)
    if(NOT LIGHTUSD_WASM_JSPI AND NOT LIGHTUSD_WASM_ASYNCIFY)
        message(FATAL_ERROR "LIGHTUSD_COROUTINE requires either LIGHTUSD_WASM_JSPI or LIGHTUSD_WASM_ASYNCIFY to be enabled")
    endif()
    set(CMAKE_CXX_STANDARD 20)
    message(STATUS "LightUSD: C++20 coroutine API enabled")
endif()

# Source files
set(LIGHTUSD_SOURCES
    src/types.cc
    src/token.cc
    src/path.cc
    src/value.cc
    src/timesamples.cc
    src/attribute.cc
    src/relationship.cc
    src/property.cc
    src/variant.cc
    src/composition.cc
    src/layer.cc
    src/prim_index.cc
    src/prim.cc
    src/stage.cc
    src/clips.cc
    src/debug.cc
    src/usda_lexer.cc
    src/usda_parser.cc
    src/usda_reader.cc
    src/usda_writer.cc
    src/crate_format.cc
    src/usdc_reader.cc
    src/usdc_fetch_handlers.cc
    src/lz4_compression.cc
    src/integer_coding.cc
    src/lz4/lz4.c
    # PCP (Prim Cache Populate)
    src/layer_registry.cc
    src/pcp_layer_stack.cc
    src/pcp_prim_index.cc
    src/pcp_cache.cc
    # Render Data (WebGPU conversion)
    src/render_data.cc
    # Primvar support
    src/primvar.cc
    # Schema validation
    src/schema.cc
    src/schema_registry.cc
    # USDZ archive support
    src/usdz_archive.cc
    # Progressive/Streaming loader
    src/streaming_loader.cc
    # Async fetch (WASM/Asyncify)
    src/async_fetch.cc
    # LightEXR - minimal EXR loader
    src/lightexr.cc
    # LightHDR - minimal HDR/RGBE loader
    src/lighthdr.cc
)

# Header files (for IDE support)
set(LIGHTUSD_HEADERS
    include/lightusd/lightusd.hh
    include/lightusd/types.hh
    include/lightusd/result.hh
    include/lightusd/cursor.hh
    include/lightusd/diagnostic.hh
    include/lightusd/debug.hh
    include/lightusd/token.hh
    include/lightusd/path.hh
    include/lightusd/value.hh
    include/lightusd/timesamples.hh
    include/lightusd/attribute.hh
    include/lightusd/relationship.hh
    include/lightusd/property.hh
    include/lightusd/variant.hh
    include/lightusd/composition.hh
    include/lightusd/layer.hh
    include/lightusd/prim_index.hh
    include/lightusd/prim.hh
    include/lightusd/stage.hh
    include/lightusd/clips.hh
    include/lightusd/typed_array.hh
    include/lightusd/stream_reader.hh
    include/lightusd/crate_format.hh
    include/lightusd/usda_lexer.hh
    include/lightusd/usda_parser.hh
    include/lightusd/usda_reader.hh
    include/lightusd/usda_writer.hh
    include/lightusd/usdc_reader.hh
    include/lightusd/usdc_fetch.hh
    include/lightusd/lz4_compression.hh
    include/lightusd/integer_coding.hh
    # PCP (Prim Cache Populate)
    include/lightusd/layer_registry.hh
    include/lightusd/pcp_layer_stack.hh
    include/lightusd/pcp_node.hh
    include/lightusd/pcp_prim_index.hh
    include/lightusd/pcp_cache.hh
    # Render Data (WebGPU conversion)
    include/lightusd/render_data.hh
    # Primvar support
    include/lightusd/primvar.hh
    # Schema validation
    include/lightusd/schema.hh
    include/lightusd/schema_registry.hh
    # USDZ archive support
    include/lightusd/usdz_archive.hh
    # Progressive/Streaming loader
    include/lightusd/streaming_loader.hh
    # Async fetch (WASM/Asyncify/JSPI)
    include/lightusd/async_fetch.hh
    # C++20 Coroutine support
    include/lightusd/coro_fetch.hh
    # LightEXR - minimal EXR loader
    include/lightusd/lightexr.hh
    # LightHDR - minimal HDR/RGBE loader
    include/lightusd/lighthdr.hh
)

# Library target
add_library(lightusd STATIC ${LIGHTUSD_SOURCES} ${LIGHTUSD_HEADERS})

target_include_directories(lightusd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Debug/Production build configuration
if(LIGHTUSD_PRODUCTION_BUILD)
    target_compile_definitions(lightusd PUBLIC LIGHTUSD_PRODUCTION_BUILD)
    message(STATUS "LightUSD: Production build enabled (debug output disabled)")
else()
    if(LIGHTUSD_DEBUG_PRINT)
        target_compile_definitions(lightusd PUBLIC LIGHTUSD_DEBUG_PRINT)
        message(STATUS "LightUSD: Debug print (DCOUT) enabled")
    endif()
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(lightusd PRIVATE /W4)
else()
    target_compile_options(lightusd PRIVATE -Wall -Wextra -Wpedantic)
endif()

# No exceptions, no RTTI
if(MSVC)
    # MSVC: Disable exceptions
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(lightusd PRIVATE /EHs-c- /GR-)
else()
    target_compile_options(lightusd PRIVATE -fno-exceptions -fno-rtti)
endif()

# Tests
if(LIGHTUSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(LIGHTUSD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================================================
# Emscripten WASM Builds
# ============================================================================
if(EMSCRIPTEN)
    # Common Emscripten link flags
    set(LIGHTUSD_WASM_COMMON_FLAGS "\
        -s MODULARIZE=1 \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s MAXIMUM_MEMORY=2GB \
        -s EXPORTED_RUNTIME_METHODS=['cwrap','ccall','UTF8ToString','addFunction'] \
        --bind \
        -s NO_EXIT_RUNTIME=1 \
        -s FILESYSTEM=0 \
    ")

    # ========================================================================
    # Main thread module (with optional Asyncify)
    # ========================================================================
    add_executable(lightusd_wasm
        web/binding.cc
    )

    target_link_libraries(lightusd_wasm PRIVATE lightusd)

    set(LIGHTUSD_MAIN_FLAGS "${LIGHTUSD_WASM_COMMON_FLAGS} \
        -s EXPORT_NAME='createLightUSD' \
        -s ENVIRONMENT='web' \
        -s RESERVED_FUNCTION_POINTERS=16 \
    ")

    # Async support: JSPI or Asyncify (mutually exclusive)
    if(LIGHTUSD_WASM_JSPI AND LIGHTUSD_WASM_ASYNCIFY)
        message(FATAL_ERROR "LIGHTUSD_WASM_JSPI and LIGHTUSD_WASM_ASYNCIFY are mutually exclusive. Choose one.")
    endif()

    if(LIGHTUSD_WASM_JSPI)
        # JSPI (JavaScript Promise Integration) - more efficient, requires browser support
        # Chrome 109+, Firefox 121+, Safari (behind flag)
        set(LIGHTUSD_MAIN_FLAGS "${LIGHTUSD_MAIN_FLAGS} \
            -s JSPI=1 \
            -s JSPI_IMPORTS=['js_fetch_asset_sync'] \
            -s JSPI_EXPORTS=['lightusd_load_usd_async','lightusd_fetch_texture_async'] \
            --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/jspi-imports.js \
        ")
        target_compile_definitions(lightusd_wasm PRIVATE LIGHTUSD_JSPI=1)
        target_compile_definitions(lightusd PRIVATE LIGHTUSD_JSPI=1)
        message(STATUS "LightUSD: JSPI enabled for main thread module (requires browser support)")
    elseif(LIGHTUSD_WASM_ASYNCIFY)
        # Asyncify - works everywhere but has overhead
        set(LIGHTUSD_MAIN_FLAGS "${LIGHTUSD_MAIN_FLAGS} \
            -s ASYNCIFY=1 \
            -s ASYNCIFY_STACK_SIZE=65536 \
            -s ASYNCIFY_IMPORTS=['js_fetch_asset','js_fetch_asset_sync'] \
            --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/asyncify-imports.js \
        ")
        target_compile_definitions(lightusd_wasm PRIVATE LIGHTUSD_ASYNCIFY=1)
        target_compile_definitions(lightusd PRIVATE LIGHTUSD_ASYNCIFY=1)
        message(STATUS "LightUSD: Asyncify enabled for main thread module")
    endif()

    # C++20 Coroutine support
    if(LIGHTUSD_COROUTINE)
        target_compile_definitions(lightusd_wasm PRIVATE LIGHTUSD_COROUTINE=1)
        target_compile_definitions(lightusd PRIVATE LIGHTUSD_COROUTINE=1)
        # Emscripten may need -fcoroutines flag for older versions
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "3.0")
            target_compile_options(lightusd PRIVATE -fcoroutines)
            target_compile_options(lightusd_wasm PRIVATE -fcoroutines)
        endif()
    endif()

    set_target_properties(lightusd_wasm PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "${LIGHTUSD_MAIN_FLAGS}"
    )

    # Production optimizations for main module
    if(LIGHTUSD_PRODUCTION_BUILD)
        set_property(TARGET lightusd_wasm APPEND_STRING PROPERTY LINK_FLAGS
            " -O3 -flto --closure 1"
        )
    else()
        set_property(TARGET lightusd_wasm APPEND_STRING PROPERTY LINK_FLAGS
            " -O2 -g -s ASSERTIONS=1"
        )
    endif()

    set_target_properties(lightusd_wasm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )

    # ========================================================================
    # Worker module (no Asyncify - uses message passing)
    # ========================================================================
    add_executable(lightusd_worker
        web/worker.cc
    )

    target_link_libraries(lightusd_worker PRIVATE lightusd)

    set_target_properties(lightusd_worker PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "${LIGHTUSD_WASM_COMMON_FLAGS} \
            -s EXPORT_NAME='createLightUSDWorker' \
            -s ENVIRONMENT='worker' \
        "
    )

    # Production optimizations for worker
    if(LIGHTUSD_PRODUCTION_BUILD)
        set_property(TARGET lightusd_worker APPEND_STRING PROPERTY LINK_FLAGS
            " -O3 -flto --closure 1"
        )
    else()
        set_property(TARGET lightusd_worker APPEND_STRING PROPERTY LINK_FLAGS
            " -O2 -g -s ASSERTIONS=1"
        )
    endif()

    set_target_properties(lightusd_worker PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )

    message(STATUS "LightUSD: Emscripten builds enabled (main + worker)")
endif()

# Install rules
include(GNUInstallDirs)

install(TARGETS lightusd
    EXPORT lightusd-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/lightusd
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT lightusd-targets
    FILE lightusd-targets.cmake
    NAMESPACE lightusd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lightusd
)
