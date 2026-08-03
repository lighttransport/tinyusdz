# SPDX-License-Identifier: Apache-2.0
# Copyright 2024-Present Light Transport Entertainment Inc.
#
# TinyUSDZ Next - CMake build configuration
# New modular architecture with minimal template usage

cmake_minimum_required(VERSION 3.10)
project(tinyusdz_next C CXX)

# Define source files
set(TINYUSDZ_NEXT_SOURCES
    ../color-transform.cc
    # Type system
    types/type-info.cc
    types/value.cc
    types/spline.cc
    types/value-view.cc
    types/interpolation.cc

    # Prim types
    prim/path.cc
    prim/attribute.cc
    prim/prim.cc

    # Layer (unified PrimSpec storage)
    layer/property-index.cc
    layer/prim-spec.cc
    layer/array-edit.cc
    layer/layer.cc
    layer/asset-anchor.cc

    # Stage (composed scene)
    stage/stage.cc

    # ASCII parser
    parser/lexer.cc
    parser/value-parser.cc
    parser/value-parser-types.cc
    parser/ascii-parser.cc
    parser/ascii-parser-metadata.cc
    parser/ascii-parser-prim.cc
    parser/ascii-parser-timesamples.cc
    parser/ascii-parser-utils.cc
    parser/ascii-parser-variants.cc
    parser/simd-scan.cc

    # Crate (binary) reader/writer
    crate/crate-format.cc
    crate/crate-data-source.cc
    crate/crate-reader-arrays.cc
    crate/crate-reader-decode.cc
    crate/crate-reader-api.cc
    crate/crate-reader.cc
    crate/crate-reader-sections.cc
    crate/crate-reader-stage.cc
    crate/crate-reader-unpack.cc
    crate/crate-reader-vectors.cc
    crate/crate-writer-types.cc
    crate/crate-writer.cc
    crate/variant-holders.cc

    # High-level readers
    reader/usda-reader.cc
    reader/usdc-reader.cc
    reader/usdz-reader.cc

    # Writers
    writer/dtoa.cc
    writer/value-printer.cc
    writer/prim-printer.cc
    writer/usda-writer.cc
    writer/usdc-writer.cc
    writer/usdz-writer.cc

    # Schema convenience APIs
    schema/geom-mesh.cc
    schema/schema-registry.cc
    schema/geom-point-instancer.cc
    schema/geom-xform.cc
    schema/color-space.cc
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
    eval/value-clip.cc

    # Asset resolution
    resolver/asset-resolver.cc

    # Composition
    composition/expression-variables.cc
    composition/composition.cc

    # Layer / PrimSpec diff
    diff/layer-diff.cc

    # Low-memory flatten pipeline
    pipeline/flatten.cc

    # PCP-style lazy composition cache (native to next)
    pcp/layer-registry.cc
    # MaterialX XML stack (dependency-free): layer-registry synthesizes
    # /MaterialX layers from referenced .mtlx documents.
    ../mtlx-xml-tokenizer.cc
    ../mtlx-xml-parser.cc
    ../mtlx-dom.cc
    pcp/load-rules.cc
    pcp/cache.cc

    # AOUSD Core validation
    validation/usd-validation.cc

    # High-level API
    tinyusdz-next.cc
)

# Define header files (for IDE project generation)
set(TINYUSDZ_NEXT_HEADERS
    # Type system
    types/type-id.hh
    types/type-info.hh
    types/value.hh
    types/value-view.hh
    types/interpolation.hh

    # Prim types
    prim/path.hh
    prim/identifier.hh
    prim/attribute.hh
    prim/prim.hh

    # Composition policy
    composition/expression-variables.hh

    # Layer (unified PrimSpec storage)
    layer/property-index.hh
    layer/prim-spec.hh
    layer/layer.hh

    # Stage (composed scene)
    stage/stage.hh
    stage/change-set.hh

    # Color management shared with render consumers
    ../color-transform.hh
    schema/color-space.hh

    # ASCII parser
    parser/lexer.hh
    parser/value-parser.hh
    parser/value-parser-arrays.inc
    parser/value-parser-dict.inc
    parser/value-parser-numeric.hh
    parser/value-parser-scalars.inc
    parser/ascii-parser-internal.hh
    parser/ascii-parser.hh

    # Crate (binary) reader/writer
    crate/crate-format.hh
    crate/crate-reader-internal.hh
    crate/crate-reader.hh
    crate/crate-writer-fields.inc
    crate/crate-writer-impl.inc
    crate/crate-writer-sections.inc
    crate/crate-writer-tables.inc
    crate/crate-writer-values.inc
    crate/crate-writer-write.inc
    crate/crate-writer-types.hh
    crate/crate-writer.hh
    crate/stream-reader.hh
    crate/stream-writer.hh

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
    schema/schema-registry.hh
    schema/geom-point-instancer.hh
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
    eval/value-clip.hh

    # Asset resolution
    resolver/asset-resolver.hh

    # Composition
    composition/composition.hh

    # Layer / PrimSpec diff
    diff/layer-diff.hh

    # PCP-style lazy composition cache (native to next)
    pcp/arc-types.hh
    pcp/namespace-mapping.hh
    pcp/prim-index.hh
    pcp/layer-registry.hh

    # AOUSD Core validation
    validation/usd-validation.hh
    pcp/cache-arc-expansion.inc
    pcp/cache.hh
    pcp/cache-compose.inc
    pcp/cache-invalidation.inc
    pcp/cache-layer-stack.inc
    pcp/cache-lock.hh
    pcp/cache-parallel-warm.inc
    pcp/cache-specs-instances.inc
    pcp/cache-stage-build.inc
    pcp/cache-utils.hh

    # High-level API
    tinyusdz-next.hh
    compat.hh
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

# C++17 (matches the parent project). The USDA writer formats floats via the
# vendored dragonbox dtoa (writer/dtoa.cc).
target_compile_features(tinyusdz_next PUBLIC cxx_std_17)

# Optional threading: enables std::thread parallel paths in next. Keep this OFF
# for WASM/browser builds so they do not require Emscripten pthreads /
# SharedArrayBuffer.
option(TINYUSDZ_NEXT_ENABLE_THREAD "Enable std::thread parallel paths in next (default OFF; not for default WASM)" OFF)
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
    # Older MSVC toolset versions in some environments can trip STL/compiler
    # version checks; this keeps compatibility with CI/toolchain combinations that
    # still target this project.
    target_compile_definitions(tinyusdz_next PRIVATE _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
else()
    target_compile_options(tinyusdz_next PRIVATE -Wall -Wextra -Wpedantic)
    # Disable some warnings for LZ4 C code
    set_source_files_properties(${LZ4_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
endif()

# tinyusdz_next uses no C++ RTTI and no C++ exceptions; disable both (C++ sources
# only so the vendored C code does not warn). TINYUSDZ_CXX_RTTI / _EXCEPTIONS are
# inherited from the parent project when built as a subdirectory, and undefined
# (-> disabled) when built standalone.
if(NOT TINYUSDZ_CXX_RTTI)
    if(MSVC)
        target_compile_options(tinyusdz_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    else()
        target_compile_options(tinyusdz_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
endif()
if(NOT TINYUSDZ_CXX_EXCEPTIONS)
    if(MSVC)
        target_compile_options(tinyusdz_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->)
    else()
        target_compile_options(tinyusdz_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    endif()
endif()

# Optional: Create test executables.
#
# `next` is a stable standalone CMake project. It is not added to the top-level
# build by default, so full regression runs must build this project separately
# and run its ctests. Keep the tests in a Debug build: many test programs use
# assert(), which is compiled out under NDEBUG.
option(TINYUSDZ_NEXT_BUILD_TESTS "Build tests for stable TinyUSDZ Next" OFF)
set(TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT "" CACHE PATH
    "External AOUSD Core supplemental release checkout")

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

    add_executable(bench_extref_compose
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/bench_extref_compose.cc
    )
    target_link_libraries(bench_extref_compose PRIVATE tinyusdz_next)

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

    add_executable(test_layer_diff
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_layer_diff.cc
    )
    target_link_libraries(test_layer_diff PRIVATE tinyusdz_next)

    add_executable(test_memory_flatten
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_memory_flatten.cc
    )
    target_link_libraries(test_memory_flatten PRIVATE tinyusdz_next)

    add_executable(test_validation
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_validation.cc
    )
    target_link_libraries(test_validation PRIVATE tinyusdz_next)

    add_executable(test_aousd_conformance
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_aousd_conformance.cc
    )
    target_link_libraries(test_aousd_conformance PRIVATE tinyusdz_next)

    # Sampled-value oracle for the AOUSD supplemental value_resolution cases
    # (self-skips with exit 77 when the corpus is unavailable).
    add_executable(test_aousd_value_resolution
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_aousd_value_resolution.cc
    )
    target_link_libraries(test_aousd_value_resolution PRIVATE tinyusdz_next)

    # Self-contained value-resolution edge matrix (pxr-baked expectations in
    # tests/next/generated/vr-edge-expected.inc; never skips).
    add_executable(test_vr_edge_matrix
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_vr_edge_matrix.cc
    )
    target_link_libraries(test_vr_edge_matrix PRIVATE tinyusdz_next)
    target_include_directories(test_vr_edge_matrix PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next)

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

    add_executable(test_usdc_malformed
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_usdc_malformed.cc
    )
    target_link_libraries(test_usdc_malformed PRIVATE tinyusdz_next)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-extract.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-session.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/texture-cache.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/materialx.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/urdf-to-usd.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/mikktspace/mikktspace.c
)

set(TYDRA_NEXT_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/chunked-array.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-extract.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-session.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/materialx.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/urdf-to-usd.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/urdf-payload.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../mtlx-xml-tokenizer.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../mtlx-xml-parser.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../mtlx-dom.hh
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
    target_compile_definitions(tydra_next PRIVATE _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
else()
    target_compile_options(tydra_next PRIVATE -Wall -Wextra -Wpedantic)
endif()

# tydra_next uses no C++ RTTI and no C++ exceptions; disable both (this is the
# tydra_next definition actually used when src/next is built; the standalone
# src/tydra/next/CMakeLists.txt carries the same flags).
if(NOT TINYUSDZ_CXX_RTTI)
    if(MSVC)
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    else()
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
endif()
if(NOT TINYUSDZ_CXX_EXCEPTIONS)
    if(MSVC)
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->)
    else()
        target_compile_options(tydra_next PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    endif()
endif()

# ============================================================
# C API (tinyusdz_c): FFI layer over tinyusdz_next + tydra_next.
# Used by the Python binding and any other language binding.
# ============================================================
add_library(tinyusdz_c STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../c-api/tinyusdz-c.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../c-api/tinyusdz-render-c.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../c-api/tinyusdz-c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../c-api/tinyusdz-render-c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../c-api/c-internal.hh
)
target_link_libraries(tinyusdz_c PUBLIC tinyusdz_next tydra_next)
target_compile_features(tinyusdz_c PUBLIC cxx_std_17)
target_include_directories(tinyusdz_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../c-api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)
if(MSVC)
    target_compile_options(tinyusdz_c PRIVATE /W4)
else()
    target_compile_options(tinyusdz_c PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(NOT TINYUSDZ_CXX_RTTI)
    if(MSVC)
        target_compile_options(tinyusdz_c PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
    else()
        target_compile_options(tinyusdz_c PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    endif()
endif()
if(NOT TINYUSDZ_CXX_EXCEPTIONS)
    if(MSVC)
        target_compile_options(tinyusdz_c PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->)
    else()
        target_compile_options(tinyusdz_c PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
    endif()
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

    # C API smoke test (pure C11 translation unit)
    add_executable(test_tinyusdz_c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/c-api/test_tinyusdz_c.c
    )
    target_link_libraries(test_tinyusdz_c PRIVATE tinyusdz_c)
    set_target_properties(test_tinyusdz_c PROPERTIES C_STANDARD 11)
    add_test(NAME next_test_c_api COMMAND test_tinyusdz_c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/usda/cube.usda
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
    add_test(NAME next_test_usdc_malformed COMMAND test_usdc_malformed
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_load_rules COMMAND test_load_rules
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_layer_diff COMMAND test_layer_diff
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_memory_flatten COMMAND test_memory_flatten
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_validation COMMAND test_validation
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_aousd_conformance COMMAND test_aousd_conformance
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_tydra COMMAND test_tydra_next
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_benchmark COMMAND benchmark_next
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set(TINYUSDZ_NEXT_SMOKE_TESTS
        next_test_tinyusdz
        next_test_layer
        next_test_stage
        next_test_pcp
        next_test_pcp_parallel
        next_test_writer
        next_test_usdc_writer
        next_test_usdc_reader
        next_test_usdc_roundtrip
        next_test_complex_usda
        next_test_roundtrip_fidelity
        next_test_crate_dict
        next_test_crate_alloc_guard
        next_test_lazy_array
        next_test_schemas
        next_test_schemas_ext
        next_test_usdcat_roundtrip
        next_test_usdz_writer
        next_test_composition
        next_test_crash_regressions
        next_test_usdc_malformed
        next_test_load_rules
        next_test_layer_diff
        next_test_memory_flatten
        next_test_validation
        next_test_aousd_conformance
        next_test_tydra)
    set_tests_properties(${TINYUSDZ_NEXT_SMOKE_TESTS}
        PROPERTIES LABELS "next")
    set_tests_properties(next_benchmark
        PROPERTIES LABELS "next;benchmark")

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME next_aousd_generated_tables
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/generate-next-aousd-tables.py
                    --root ${CMAKE_CURRENT_SOURCE_DIR}/../.. --check)
        set_tests_properties(next_aousd_generated_tables
            PROPERTIES LABELS "next;aousd;generated")
    endif()
    if(NOT TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT AND
       DEFINED ENV{AOUSD_CORE_SUPPLEMENTAL_ROOT})
        set(TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT
            "$ENV{AOUSD_CORE_SUPPLEMENTAL_ROOT}")
    endif()
    if(Python3_Interpreter_FOUND AND TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT AND
       EXISTS "${TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT}/composition")
        add_test(
            NAME next_aousd_supplemental
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/run-aousd-supplemental.py
                    --suite-root "${TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT}"
                    --next-usdcat "$<TARGET_FILE:next_usdcat>"
                    --aousd-test "$<TARGET_FILE:test_aousd_conformance>"
                    --aousd-value-test "$<TARGET_FILE:test_aousd_value_resolution>"
                    --max-composition-fail 0)
        set_tests_properties(next_aousd_supplemental
            PROPERTIES LABELS "next;aousd;supplemental" TIMEOUT 600)
    endif()
    # Sampled-value oracle also runs as its own ctest entry; the binary
    # self-skips (exit 77) when the corpus is missing.
    add_test(NAME next_test_aousd_value_resolution
        COMMAND test_aousd_value_resolution "${TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(next_test_aousd_value_resolution
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "next;aousd")

    add_test(NAME next_test_vr_edge_matrix
        COMMAND test_vr_edge_matrix "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(next_test_vr_edge_matrix
        PROPERTIES LABELS "next;aousd;pxrdiff")

    # usd-wg asset-corpus regression gate (mirrors the top-level
    # corpus-parse-native test, but drives next_usdcat). Registered only when
    # node and the corpus checkout are present; set USD_WG_ASSETS_DIR to point
    # at a usd-wg/assets clone.
    find_program(TINYUSDZ_NEXT_NODE_EXECUTABLE NAMES node nodejs)
    if(TINYUSDZ_NEXT_NODE_EXECUTABLE)
        add_test(
            NAME next_compare_usda_asset_paths
            COMMAND ${TINYUSDZ_NEXT_NODE_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test-compare-usda-asset-paths.cjs)
        set_tests_properties(next_compare_usda_asset_paths
            PROPERTIES LABELS "next;pxrdiff")
    endif()
    # Pinned OpenUSD oracle for the pxr differential gates below. Set
    # TINYUSDZ_PXR_USDCAT to relocate it.
    if(DEFINED ENV{TINYUSDZ_PXR_USDCAT})
        set(NEXT_PXR_USDCAT "$ENV{TINYUSDZ_PXR_USDCAT}")
    else()
        set(NEXT_PXR_USDCAT "/mnt/nvme02/work/tinyusdz-repo/OpenUSD/dist/bin/usdcat")
    endif()
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
        set_tests_properties(next_corpus_parse
            PROPERTIES LABELS "next;corpus")

        # Compose differential over the same corpus: flatten every asset and
        # cross-check against the pinned OpenUSD usdcat (needs the legacy
        # build's tusddiff for the layer re-serialization diff). Ratchets
        # (2026-07-15 baseline: MATCH 267, DIFFER 2 + DIFFERR 9 = 11; the
        # DIFFERR set is the LEGACY tusddiff parser failing on pxr output
        # forms, not next divergences): small headroom only — raising either
        # number requires justification.
        if(EXISTS "${NEXT_PXR_USDCAT}" AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../build/tusddiff")
            add_test(
                NAME next_corpus_flatten_compare
                COMMAND ${TINYUSDZ_NEXT_NODE_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/parse-asset-corpus.mjs
                        --assets "${NEXT_CORPUS_ASSETS_DIR}"
                        --mode flatten
                        --tusdcat "$<TARGET_FILE:next_usdcat>"
                        --serialize-args "--rewrite-layer --openusd-compat"
                        --compare
                        --usdcat "${NEXT_PXR_USDCAT}"
                        --tusddiff "${CMAKE_CURRENT_SOURCE_DIR}/../../build/tusddiff"
                        --out "${CMAKE_CURRENT_BINARY_DIR}/corpus-flatten-results"
                        --max-fail 3
                        --max-differ 15
            )
            set_tests_properties(next_corpus_flatten_compare
                PROPERTIES LABELS "next;corpus;pxrdiff" TIMEOUT 1800)
        endif()
    endif()

    # next-vs-pxr flatten differential gate: next_usdcat -f vs the PINNED
    # OpenUSD 26.05 usdcat --flatten, compared semantically. Ratchet is 0:
    # any divergence not recorded (with reason) in
    # tests/next/next-pxr-flatten-xfail.txt fails; entries that start
    # passing are reported as XPASS for pruning.
    if(TINYUSDZ_NEXT_NODE_EXECUTABLE AND EXISTS "${NEXT_PXR_USDCAT}")
        set(NEXT_PXR_FLATTEN_ARGS
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/run-next-pxr-flatten-diff.mjs
            --next-usdcat "$<TARGET_FILE:next_usdcat>"
            --usdcat "${NEXT_PXR_USDCAT}")
        if(TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT)
            list(APPEND NEXT_PXR_FLATTEN_ARGS
                 --suite-root "${TINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT}")
        endif()
        add_test(NAME next_pxr_flatten_diff
            COMMAND ${TINYUSDZ_NEXT_NODE_EXECUTABLE} ${NEXT_PXR_FLATTEN_ARGS})
        set_tests_properties(next_pxr_flatten_diff
            PROPERTIES LABELS "next;pxrdiff" TIMEOUT 900)
    endif()
endif()

# ============================================================
# Python extension module (optional)
# ============================================================

# (The Python binding is built by setup.py at the repo root; see
#  src/python/py-*.c and src/c-api/.)
