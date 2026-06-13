# SPDX-License-Identifier: Apache-2.0
# Copyright 2024-Present Light Transport Entertainment Inc.
#
# TinyUSDZ Next - CMake build configuration
# New modular architecture with minimal template usage

cmake_minimum_required(VERSION 3.10)
project(tinyusdz_next C CXX)

# Define source files
set(TINYUSDZ_NEXT_SOURCES
    # Type system
    types/type-info.cc
    types/value.cc
    types/interpolation.cc

    # Prim types
    prim/path.cc
    prim/attribute.cc
    prim/prim.cc

    # Layer (unified PrimSpec storage)
    layer/property-index.cc
    layer/prim-spec.cc
    layer/layer.cc

    # Stage (composed scene)
    stage/stage.cc

    # ASCII parser
    parser/lexer.cc
    parser/value-parser.cc
    parser/ascii-parser.cc

    # Crate (binary) reader/writer
    crate/crate-format.cc
    crate/crate-data-source.cc
    crate/crate-reader.cc
    crate/crate-writer.cc

    # High-level readers
    reader/usda-reader.cc
    reader/usdc-reader.cc
    reader/usdz-reader.cc

    # Writers
    writer/value-printer.cc
    writer/prim-printer.cc
    writer/usda-writer.cc
    writer/usdc-writer.cc
    writer/usdz-writer.cc

    # Schema convenience APIs
    schema/geom-mesh.cc
    schema/geom-xform.cc
    schema/usd-lux.cc
    schema/usd-geom-camera.cc
    schema/usd-shade.cc
    schema/usd-skel.cc
    schema/physics-scene.cc
    schema/physics-api.cc
    schema/physics-joint.cc
    schema/physics-collision.cc
    schema/usd-ar.cc
    schema/usd-media.cc
    schema/usd-mtlx.cc

    # Attribute evaluation
    eval/attribute-eval.cc

    # Asset resolution
    resolver/asset-resolver.cc

    # Composition
    composition/composition.cc

    # Low-memory flatten pipeline
    pipeline/flatten.cc

    # PCP-style lazy composition cache (native to next)
    pcp/layer-registry.cc
    pcp/load-rules.cc
    pcp/cache.cc

    # High-level API
    tinyusdz-next.cc
    c-tinyusd-next.cc
)

# Define header files (for IDE project generation)
set(TINYUSDZ_NEXT_HEADERS
    # Type system
    types/type-id.hh
    types/type-info.hh
    types/value.hh
    types/interpolation.hh

    # Prim types
    prim/path.hh
    prim/attribute.hh
    prim/prim.hh

    # Layer (unified PrimSpec storage)
    layer/property-index.hh
    layer/prim-spec.hh
    layer/layer.hh

    # Stage (composed scene)
    stage/stage.hh

    # ASCII parser
    parser/lexer.hh
    parser/value-parser.hh
    parser/ascii-parser.hh

    # Crate (binary) reader/writer
    crate/crate-format.hh
    crate/crate-reader.hh
    crate/crate-writer.hh
    crate/stream-reader.hh

    # High-level readers
    reader/usda-reader.hh
    reader/usdc-reader.hh

    # Writers
    writer/value-printer.hh
    writer/prim-printer.hh
    writer/usda-writer.hh
    writer/usdc-writer.hh
    writer/usdz-writer.hh

    # Schema convenience APIs
    schema/geom-mesh.hh
    schema/geom-xform.hh
    schema/usd-lux.hh
    schema/usd-geom-camera.hh
    schema/usd-shade.hh
    schema/usd-skel.hh
    schema/physics-scene.hh
    schema/physics-api.hh
    schema/physics-joint.hh
    schema/physics-collision.hh
    schema/usd-ar.hh
    schema/usd-media.hh
    schema/usd-mtlx.hh
    schema/usdPhysics.hh

    # Attribute evaluation
    eval/attribute-eval.hh

    # Asset resolution
    resolver/asset-resolver.hh

    # Composition
    composition/composition.hh

    # PCP-style lazy composition cache (native to next)
    pcp/arc-types.hh
    pcp/namespace-mapping.hh
    pcp/prim-index.hh
    pcp/layer-registry.hh
    pcp/cache.hh

    # High-level API
    tinyusdz-next.hh
    compat.hh
    c-tinyusd-next.h
)

# LZ4 source (from existing TinyUSDZ)
set(LZ4_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lz4/lz4.c
)

# Prepend source directory to all files
list(TRANSFORM TINYUSDZ_NEXT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM TINYUSDZ_NEXT_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# Create static library
add_library(tinyusdz_next STATIC
    ${TINYUSDZ_NEXT_SOURCES}
    ${TINYUSDZ_NEXT_HEADERS}
    ${LZ4_SOURCES}
)

# Set C++14 standard
target_compile_features(tinyusdz_next PUBLIC cxx_std_14)

# Optional threading: enables the parallel layer-prefetch path in next::pcp.
option(TINYUSDZ_NEXT_ENABLE_THREAD "Enable std::thread parallel paths in next" OFF)
if(TINYUSDZ_NEXT_ENABLE_THREAD)
    find_package(Threads REQUIRED)
    target_compile_definitions(tinyusdz_next PUBLIC TINYUSDZ_ENABLE_THREAD)
    target_link_libraries(tinyusdz_next PUBLIC Threads::Threads)
endif()

# Set include directories
target_include_directories(tinyusdz_next PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

# Include LZ4 header directory
target_include_directories(tinyusdz_next PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../lz4
)

# Compiler warnings
if(MSVC)
    target_compile_options(tinyusdz_next PRIVATE /W4)
else()
    target_compile_options(tinyusdz_next PRIVATE -Wall -Wextra -Wpedantic)
    # Disable some warnings for LZ4 C code
    set_source_files_properties(${LZ4_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
endif()

# Optional: Create test executable
#
# EXPERIMENTAL / under-construction module. `next` is a standalone CMake project
# (not added by the top-level CMakeLists) and its tests are OFF by default, so it
# is intentionally excluded from the main native regression suite (`build/` ctest)
# and from the Pixar comparison runner. Its tests may be unstable and use bare
# assert() (no-ops under NDEBUG). Build/run them on demand only — see
# doc/testing-cpp.md ("Experimental `next` library tests"). Do NOT add `next` to
# the full regression gate without first hardening the suite.
option(TINYUSDZ_NEXT_BUILD_TESTS "Build tests for TinyUSDZ Next (experimental; excluded from main regression)" OFF)

if(TINYUSDZ_NEXT_BUILD_TESTS)
    # The next tests validate behavior with assert(); a Release build (-DNDEBUG)
    # would compile those out and make the tests vacuous. Re-enable asserts for
    # everything defined below (directory-scoped; -UNDEBUG is appended after the
    # build-type flags so it wins). The library above keeps its NDEBUG/-O flags.
    add_compile_options(-UNDEBUG)

    add_executable(test_tinyusdz_next
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_next.cc
    )
    target_link_libraries(test_tinyusdz_next PRIVATE tinyusdz_next)

    add_executable(test_usdc_reader
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdc.cc
    )
    target_link_libraries(test_usdc_reader PRIVATE tinyusdz_next)

    add_executable(test_layer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_layer.cc
    )
    target_link_libraries(test_layer PRIVATE tinyusdz_next)

    add_executable(test_stage
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_stage.cc
    )
    target_link_libraries(test_stage PRIVATE tinyusdz_next)

    add_executable(test_pcp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_pcp.cc
    )
    target_link_libraries(test_pcp PRIVATE tinyusdz_next)

    add_executable(test_pcp_parallel
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_pcp_parallel.cc
    )
    target_link_libraries(test_pcp_parallel PRIVATE tinyusdz_next)

    add_executable(test_writer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_writer.cc
    )
    target_link_libraries(test_writer PRIVATE tinyusdz_next)

    add_executable(test_usdc_writer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdc_writer.cc
    )
    target_link_libraries(test_usdc_writer PRIVATE tinyusdz_next)

    add_executable(test_usdc_roundtrip
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdc_roundtrip.cc
    )
    target_link_libraries(test_usdc_roundtrip PRIVATE tinyusdz_next)

    add_executable(test_complex_usda
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_complex_usda.cc
    )
    target_link_libraries(test_complex_usda PRIVATE tinyusdz_next)

    add_executable(test_roundtrip_fidelity
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_roundtrip_fidelity.cc
    )
    target_link_libraries(test_roundtrip_fidelity PRIVATE tinyusdz_next)

    add_executable(test_crate_dict
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_crate_dict.cc
    )
    target_link_libraries(test_crate_dict PRIVATE tinyusdz_next)

    add_executable(test_crate_alloc_guard
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_crate_alloc_guard.cc
    )
    target_link_libraries(test_crate_alloc_guard PRIVATE tinyusdz_next)

    add_executable(test_lazy_array
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_lazy_array.cc
    )
    target_link_libraries(test_lazy_array PRIVATE tinyusdz_next)

    add_executable(bench_lazy_mem
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/bench_lazy_mem.cc
    )
    target_link_libraries(bench_lazy_mem PRIVATE tinyusdz_next)

    add_executable(bench_pcp_compose
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/bench_pcp_compose.cc
    )
    target_link_libraries(bench_pcp_compose PRIVATE tinyusdz_next)

    add_executable(test_schemas
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_schemas.cc
    )
    target_link_libraries(test_schemas PRIVATE tinyusdz_next)

    add_executable(test_schemas_ext
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_schemas_ext.cc
    )
    target_link_libraries(test_schemas_ext PRIVATE tinyusdz_next)

    add_executable(test_usdcat_roundtrip
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdcat_roundtrip.cc
    )
    target_link_libraries(test_usdcat_roundtrip PRIVATE tinyusdz_next)

    add_executable(test_usdz_writer
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdz_writer.cc
    )
    target_link_libraries(test_usdz_writer PRIVATE tinyusdz_next)

    add_executable(test_composition
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_composition.cc
    )
    target_link_libraries(test_composition PRIVATE tinyusdz_next)

    add_executable(test_load_rules
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_load_rules.cc
    )
    target_link_libraries(test_load_rules PRIVATE tinyusdz_next)

    # tusdcat-compatible CLI: drives the usd-wg asset-corpus regression gate
    # (tests/parse-asset-corpus.mjs --tusdcat $<TARGET_FILE:next_usdcat>).
    add_executable(next_usdcat
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/next_usdcat.cc
    )
    target_link_libraries(next_usdcat PRIVATE tinyusdz_next)

    # Replays minimized fuzzer-found crash inputs (run under ASAN/UBSAN).
    add_executable(test_crash_regressions
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_crash_regressions.cc
    )
    target_link_libraries(test_crash_regressions PRIVATE tinyusdz_next)
    target_compile_definitions(test_crash_regressions PRIVATE
        CRASH_REGRESSION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/crash_regressions")
endif()

# ============================================================
# libFuzzer harnesses (clang only)
# ============================================================
option(TINYUSDZ_NEXT_BUILD_FUZZERS "Build libFuzzer harnesses for TinyUSDZ Next (requires clang)" OFF)

if(TINYUSDZ_NEXT_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "TINYUSDZ_NEXT_BUILD_FUZZERS requires clang (-fsanitize=fuzzer)")
    endif()
    # The library must carry fuzzer coverage instrumentation too (no-link
    # variant), otherwise only the tiny harness body is explored.
    set(NEXT_FUZZ_LIB_FLAGS -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all)
    set(NEXT_FUZZ_BIN_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_compile_options(tinyusdz_next PRIVATE ${NEXT_FUZZ_LIB_FLAGS})
    foreach(fz next_usdc next_usda next_compose)
        add_executable(fuzz_${fz}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fuzzer/${fz}_fuzzmain.cc
        )
        target_link_libraries(fuzz_${fz} PRIVATE tinyusdz_next)
        target_compile_options(fuzz_${fz} PRIVATE ${NEXT_FUZZ_BIN_FLAGS})
        target_link_options(fuzz_${fz} PRIVATE ${NEXT_FUZZ_BIN_FLAGS})
    endforeach()
endif()

# Tydra Next (render data conversion)
# Define tydra/next source files
set(TYDRA_NEXT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/materialx.cc
)

set(TYDRA_NEXT_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/chunked-array.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/materialx.hh
)

add_library(tydra_next STATIC
    ${TYDRA_NEXT_SOURCES}
    ${TYDRA_NEXT_HEADERS}
)

target_link_libraries(tydra_next PUBLIC tinyusdz_next)
target_compile_features(tydra_next PUBLIC cxx_std_14)

target_include_directories(tydra_next PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(tydra_next PRIVATE /W4)
else()
    target_compile_options(tydra_next PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tydra Next test
if(TINYUSDZ_NEXT_BUILD_TESTS)
    add_executable(test_tydra_next
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/tydra/next/test_tydra_next.cc
    )
    target_link_libraries(test_tydra_next PRIVATE tydra_next tinyusdz_next)

    # Performance benchmark
    add_executable(benchmark_next
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/benchmark_next.cc
    )
    target_link_libraries(benchmark_next PRIVATE tinyusdz_next)

    # Register tests with ctest
    enable_testing()

    add_test(NAME next_test_tinyusdz COMMAND test_tinyusdz_next
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_layer COMMAND test_layer
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_stage COMMAND test_stage
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_pcp COMMAND test_pcp
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_pcp_parallel COMMAND test_pcp_parallel
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_writer COMMAND test_writer
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdc_writer COMMAND test_usdc_writer
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdc_reader COMMAND test_usdc_reader
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdc_roundtrip COMMAND test_usdc_roundtrip
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_complex_usda COMMAND test_complex_usda
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_roundtrip_fidelity COMMAND test_roundtrip_fidelity
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_crate_dict COMMAND test_crate_dict
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_crate_alloc_guard COMMAND test_crate_alloc_guard
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_lazy_array COMMAND test_lazy_array
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_schemas COMMAND test_schemas
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_schemas_ext COMMAND test_schemas_ext
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdcat_roundtrip COMMAND test_usdcat_roundtrip
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdz_writer COMMAND test_usdz_writer
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_composition COMMAND test_composition
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_crash_regressions COMMAND test_crash_regressions
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_load_rules COMMAND test_load_rules
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_benchmark COMMAND benchmark_next
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # usd-wg asset-corpus regression gate (mirrors the top-level
    # corpus-parse-native test, but drives next_usdcat). Registered only when
    # node and the corpus checkout are present; set USD_WG_ASSETS_DIR to point
    # at a usd-wg/assets clone.
    find_program(TINYUSDZ_NEXT_NODE_EXECUTABLE NAMES node nodejs)
    if(DEFINED ENV{USD_WG_ASSETS_DIR})
        set(NEXT_CORPUS_ASSETS_DIR "$ENV{USD_WG_ASSETS_DIR}")
    else()
        set(NEXT_CORPUS_ASSETS_DIR "/mnt/nvme02/work/usd/assets")
    endif()
    if(TINYUSDZ_NEXT_NODE_EXECUTABLE AND EXISTS "${NEXT_CORPUS_ASSETS_DIR}")
        add_test(
            NAME next_corpus_parse
            COMMAND ${TINYUSDZ_NEXT_NODE_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/parse-asset-corpus.mjs
                    --assets "${NEXT_CORPUS_ASSETS_DIR}"
                    --mode load
                    --tusdcat "$<TARGET_FILE:next_usdcat>"
                    --out "${CMAKE_CURRENT_BINARY_DIR}/corpus-parse-results"
                    # Regression ratchet: 0 of 280 corpus files fail to parse
                    # today (some USDC files load with warnings on unsupported
                    # binary value types — see corpus-parse-results/summary.md).
                    # A tiny headroom guards against environment differences;
                    # raising it requires justification.
                    --max-fail 3
        )
    endif()
endif()

# ============================================================
# Python extension module (optional)
# ============================================================

option(TINYUSDZ_NEXT_BUILD_PYTHON "Build Python extension for TinyUSDZ Next" OFF)

if(TINYUSDZ_NEXT_BUILD_PYTHON)
    find_package(Python3 3.10 COMPONENTS Interpreter Development REQUIRED)

    # Determine extension suffix
    if(WIN32)
        set(PYTHON_EXT_SUFFIX ".pyd")
    else()
        set(PYTHON_EXT_SUFFIX ".so")
    endif()

    add_library(_next_core MODULE
        ${CMAKE_CURRENT_SOURCE_DIR}/python/module.c
    )

    target_include_directories(_next_core PRIVATE
        ${Python3_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(_next_core PRIVATE
        tinyusdz_next
    )

    set_target_properties(_next_core PROPERTIES
        PREFIX ""
        SUFFIX ${PYTHON_EXT_SUFFIX}
    )

    # Install to python package directory
    install(TARGETS _next_core
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/python/next/tinyusdz_next
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/python/next/tinyusdz_next
    )

    message(STATUS "TinyUSDZ Next Python extension: enabled")
    message(STATUS "  Python version: ${Python3_VERSION}")
    message(STATUS "  Python include: ${Python3_INCLUDE_DIRS}")
endif()
