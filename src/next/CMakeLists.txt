# SPDX-License-Identifier: Apache-2.0
# Copyright 2024-Present Light Transport Entertainment Inc.
#
# TinyUSDZ Next - CMake build configuration
# New modular architecture with minimal template usage

cmake_minimum_required(VERSION 3.10)
project(tinyusdz_next C CXX)

# Define source files.
#
# next-core is intentionally split into groups so a lean "core" library
# (`next_core`: IO + composition + PCP, plus the schema/eval convenience layer
# by default) can be built and gated into the main TinyUSDZ build, while the
# C API / Python bindings stay layered on top as `tinyusdz_next`. The
# TINYUSDZ_NEXT_CORE_MINIMAL option (below) drops the schema/eval layer for the
# smallest possible library (WASM / embedded).

# Core: value types, layer/stage, USDA + USDC (crate) read/write, PCP +
# composition, low-memory flatten, asset resolution, and the high-level API.
# This is the irreducible IO + composition core (always in next_core).
set(NEXT_CORE_SOURCES
    # Type system
    types/type-info.cc
    types/token.cc
    types/value.cc
    types/value-view.cc
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

    # High-level readers
    reader/usda-reader.cc
    reader/usdc-reader.cc
    reader/usdz-reader.cc

    # Writers
    writer/dtoa.cc
    ../external/zmij/zmij.cc  # zmij usdcat SIMD fast path (used by writer/dtoa.cc)
    writer/value-printer.cc
    writer/prim-printer.cc
    writer/usda-writer.cc
    writer/usdc-writer.cc
    writer/usdz-writer.cc

    # Asset resolution (used by the high-level load API)
    resolver/asset-resolver.cc

    # Composition
    composition/composition.cc

    # Low-memory flatten pipeline
    pipeline/flatten.cc

    # PCP-style lazy composition cache (native to next)
    pcp/layer-registry.cc
    pcp/load-rules.cc
    pcp/cache.cc
    # cache.cc's Cache::Impl methods are split across these TUs (declared in
    # cache-internal.hh) so the ~6.5s cache TU compiles in parallel.
    pcp/cache-methods-warm.cc
    pcp/cache-methods-arc.cc
    pcp/cache-methods-compose.cc

    # High-level API
    tinyusdz-next.cc
)

# Schema convenience APIs + attribute evaluation. Included in next_core by
# default; dropped when TINYUSDZ_NEXT_CORE_MINIMAL is ON. Only the umbrella
# header (tinyusdz-next.hh) and the C API reference these.
set(NEXT_CORE_SCHEMA_EVAL_SOURCES
    # Schema convenience APIs
    schema/geom-mesh.cc
    schema/geom-point-instancer.cc
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
)

# C API. Layered on top of the core + schema/eval; not part of next_core.
set(NEXT_CAPI_SOURCES
    c-tinyusd-next.cc
)

# next_core: IO + composition (+ schema/eval unless minimal). This is the
# library the main TinyUSDZ build links when TINYUSDZ_WITH_NEXT_CORE is ON.
option(TINYUSDZ_NEXT_CORE_MINIMAL
    "next_core: IO + composition only (drop the schema/eval convenience layer); smallest lib / WASM" OFF)
if(NOT TINYUSDZ_NEXT_CORE_MINIMAL)
    list(APPEND NEXT_CORE_SOURCES ${NEXT_CORE_SCHEMA_EVAL_SOURCES})
endif()

# Define header files (for IDE project generation)
set(TINYUSDZ_NEXT_HEADERS
    # Support (C-style allocators / containers)
    support/arena.hh
    support/small_vector.hh

    # Filesystem utility (next_io; excluded from freestanding/WASM builds)
    io/file-util.hh

    # Type system
    types/type-id.hh
    types/type-info.hh
    types/token.hh
    types/value.hh
    types/value-view.hh
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

    # Asset resolution
    resolver/asset-resolver.hh

    # Composition
    composition/composition.hh

    # PCP-style lazy composition cache (native to next)
    pcp/arc-types.hh
    pcp/namespace-mapping.hh
    pcp/prim-index.hh
    pcp/layer-registry.hh
    pcp/cache-arc-expansion.inc
    pcp/cache.hh
    pcp/cache-internal.hh
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
    c-tinyusd-next.h
)

# LZ4 source (from existing TinyUSDZ)
set(LZ4_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lz4/lz4.c
)

# Prepend source directory to all files
list(TRANSFORM NEXT_CORE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM NEXT_CAPI_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM TINYUSDZ_NEXT_HEADERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# Common per-target C++ flags for the next libraries: no RTTI, no exceptions
# (C++ sources only so the vendored C code does not warn), and warning level.
# TINYUSDZ_CXX_RTTI / _EXCEPTIONS are inherited from the parent project when
# built as a subdirectory, and undefined (-> disabled) when built standalone.
function(_next_apply_cxx_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(NOT TINYUSDZ_CXX_RTTI)
        if(MSVC)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
        else()
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
        endif()
    endif()
    if(NOT TINYUSDZ_CXX_EXCEPTIONS)
        if(MSVC)
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->)
        else()
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
        endif()
    endif()
endfunction()

# ------------------------------------------------------------------
# next_core: the lean IO + composition core (this is what the main
# TinyUSDZ build links behind TINYUSDZ_WITH_NEXT_CORE).
# ------------------------------------------------------------------
add_library(next_core STATIC
    ${NEXT_CORE_SOURCES}
    ${TINYUSDZ_NEXT_HEADERS}
    ${LZ4_SOURCES}
)

# C++17 (matches the parent project). The coding style inside next_core is
# deliberately C-like (POD data + free functions, minimal std::string/std::vector
# churn), but C++17 is retained for surrounding/helper/external code such as the
# vendored dragonbox dtoa (writer/dtoa.cc), which has a zmij SIMD fast path.
target_compile_features(next_core PUBLIC cxx_std_17)

if(TINYUSDZ_NEXT_CORE_MINIMAL)
    # Umbrella header (tinyusdz-next.hh) guards its schema/eval includes on this.
    target_compile_definitions(next_core PUBLIC TINYUSDZ_NEXT_CORE_MINIMAL)
endif()

# Silence warnings from the vendored zmij translation unit (third-party code:
# __int128 pedantic, sign-compare, unused params — not ours to fix).
if(MSVC)
    set_source_files_properties(../external/zmij/zmij.cc PROPERTIES COMPILE_FLAGS "/w")
else()
    set_source_files_properties(../external/zmij/zmij.cc PROPERTIES COMPILE_FLAGS "-w")
endif()

target_include_directories(next_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(next_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../lz4
)
_next_apply_cxx_flags(next_core)
if(NOT MSVC)
    # Disable some warnings for LZ4 C code
    set_source_files_properties(${LZ4_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough")
endif()

# Optional threading: enables std::thread parallel paths in next. Keep this OFF
# for WASM/browser builds so they do not require Emscripten pthreads /
# SharedArrayBuffer.
option(TINYUSDZ_NEXT_ENABLE_THREAD "Enable std::thread parallel paths in next (default OFF; not for default WASM)" OFF)
if(TINYUSDZ_NEXT_ENABLE_THREAD)
    find_package(Threads REQUIRED)
    target_compile_definitions(next_core PUBLIC TINYUSDZ_ENABLE_THREAD)
    target_link_libraries(next_core PUBLIC Threads::Threads)
endif()

# ------------------------------------------------------------------
# next_io: the filesystem utility (mmap / whole-file read-write / path helpers),
# ported self-contained from legacy src/io-util.cc into tinyusdz::next::io. It is
# the ONLY next component that performs raw filesystem I/O. next-core proper is
# memory-only (freestanding); when TINYUSDZ_NEXT_ENABLE_FILEIO is ON, next_core
# also compiles its file-based Load*/Write* entry points and links next_io. The
# option is forced OFF for the freestanding profiles (minimal / Emscripten /
# WASI), so those builds carry no filesystem syscalls at all.
# ------------------------------------------------------------------
if(TINYUSDZ_NEXT_CORE_MINIMAL OR EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(_next_fileio_default OFF)
else()
    set(_next_fileio_default ON)
endif()
option(TINYUSDZ_NEXT_ENABLE_FILEIO
    "Build next_io + next-core's file-based entry points (OFF = memory-only freestanding core)"
    ${_next_fileio_default})

if(TINYUSDZ_NEXT_ENABLE_FILEIO)
    add_library(next_io STATIC ${CMAKE_CURRENT_SOURCE_DIR}/io/file-util.cc)
    target_compile_features(next_io PUBLIC cxx_std_17)
    target_include_directories(next_io PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
    )
    _next_apply_cxx_flags(next_io)
    target_compile_definitions(next_core PUBLIC TINYUSDZ_NEXT_ENABLE_FILEIO)
    target_link_libraries(next_core PUBLIC next_io)
endif()

# ------------------------------------------------------------------
# Optional drop-in allocator for the CLI tools. The compose+stream path is
# allocation-heavy (millions of small PrimSpec/Src allocations across threads);
# glibc's ptmalloc is a bottleneck there. Linking tcmalloc/jemalloc cut Caldera
# ~20% total / ~22% RSS and Island ~22% (byte-identical -- an allocator does not
# change output). This links the allocator FIRST into the tool executables only;
# the library never forces an allocator (apps choose). Equivalent at runtime to
# `LD_PRELOAD=libtcmalloc_minimal.so.4`. See doc/large-scene.md §8.1.
# ------------------------------------------------------------------
option(TINYUSDZ_NEXT_WITH_TCMALLOC
    "Link a drop-in allocator (jemalloc/tcmalloc/mimalloc) into the next CLI tools (faster compose+write)" OFF)
set(TINYUSDZ_NEXT_ALLOCATOR_LIB "")
set(TINYUSDZ_NEXT_ALLOCATOR_IS_JEMALLOC OFF)
if(TINYUSDZ_NEXT_WITH_TCMALLOC)
    # Allocator ranking from measurement (doc/large-scene.md §8.1; byte-identical
    # -- an allocator does not change output). jemalloc is the PREFERRED default:
    #   1. jemalloc -- MEASURED FASTEST (Caldera 4.11s/2.08GiB -20% vs tcmalloc AND
    #      lower RSS; Island 9.37s) once decay-purging is disabled. That config
    #      (dirty_decay_ms:-1,muzzy_decay_ms:-1) is BAKED into the tool via
    #      jemalloc's static `malloc_conf` global under the
    #      TINYUSDZ_NEXT_ALLOC_JEMALLOC define -- no env var needed. Source vendored
    #      at src/external/jemalloc (provenance only; a system/prebuilt libjemalloc
    #      is linked). (Its heap layout once exposed a crate-reader data race that
    #      dropped prim subtrees; that race is FIXED -- see crate num_fieldsets_.)
    #   2. tcmalloc_minimal -- robust, zero-config runner-up; sped up compose AND
    #      write. Often ships only the versioned runtime (libtcmalloc_minimal.so.N)
    #      with no -dev `.so` symlink, so also find_file that and link by full path.
    #   3. mimalloc -- fast only with MIMALLOC_PURGE_DELAY=-1 (default purges, is
    #      slower than tcmalloc) and costs +1GiB RSS; last resort.
    # (tbbmalloc barely beat glibc and snmalloc was mid-pack -- not searched.)
    find_library(TINYUSDZ_NEXT_ALLOC_JEMALLOC NAMES jemalloc)
    if(TINYUSDZ_NEXT_ALLOC_JEMALLOC)
        set(TINYUSDZ_NEXT_ALLOCATOR_LIB "${TINYUSDZ_NEXT_ALLOC_JEMALLOC}")
        set(TINYUSDZ_NEXT_ALLOCATOR_IS_JEMALLOC ON)
    else()
        find_library(TINYUSDZ_NEXT_ALLOC_TCMALLOC NAMES tcmalloc_minimal tcmalloc)
        if(NOT TINYUSDZ_NEXT_ALLOC_TCMALLOC)
            find_file(TINYUSDZ_NEXT_ALLOC_TCMALLOC
                NAMES libtcmalloc_minimal.so.4 libtcmalloc.so.4
                PATHS /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib64 /usr/lib /lib)
        endif()
        if(TINYUSDZ_NEXT_ALLOC_TCMALLOC)
            set(TINYUSDZ_NEXT_ALLOCATOR_LIB "${TINYUSDZ_NEXT_ALLOC_TCMALLOC}")
        else()
            find_library(TINYUSDZ_NEXT_ALLOC_MIMALLOC NAMES mimalloc)
            if(TINYUSDZ_NEXT_ALLOC_MIMALLOC)
                set(TINYUSDZ_NEXT_ALLOCATOR_LIB "${TINYUSDZ_NEXT_ALLOC_MIMALLOC}")
            endif()
        endif()
    endif()
    if(TINYUSDZ_NEXT_ALLOCATOR_LIB)
        message(STATUS "next: linking allocator ${TINYUSDZ_NEXT_ALLOCATOR_LIB} into CLI tools")
    else()
        message(WARNING "TINYUSDZ_NEXT_WITH_TCMALLOC=ON but no tcmalloc/mimalloc/jemalloc found; using system malloc")
    endif()
endif()

# ------------------------------------------------------------------
# tinyusdz_next: the full library (core + C API). This retains the historical
# target name that the tests, tydra_next, and the Python module link against.
# The C API depends on the schema/eval convenience layer, so it is only built
# when that layer is present (non-minimal). When minimal, tinyusdz_next is an
# interface alias for next_core so existing link references keep working.
# ------------------------------------------------------------------
if(TINYUSDZ_NEXT_CORE_MINIMAL)
    add_library(tinyusdz_next INTERFACE)
    target_link_libraries(tinyusdz_next INTERFACE next_core)
else()
    add_library(tinyusdz_next STATIC ${NEXT_CAPI_SOURCES})
    target_link_libraries(tinyusdz_next PUBLIC next_core)
    _next_apply_cxx_flags(tinyusdz_next)
endif()

# Optional: Create test executables.
#
# `next` is a stable standalone CMake project. It is not added to the top-level
# build by default, so full regression runs must build this project separately
# and run its ctests. Keep the tests in a Debug build: many test programs use
# assert(), which is compiled out under NDEBUG.
option(TINYUSDZ_NEXT_BUILD_TESTS "Build tests for stable TinyUSDZ Next" OFF)

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

    add_executable(test_arena
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_arena.cc
    )
    target_link_libraries(test_arena PRIVATE tinyusdz_next)

    add_executable(test_token
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_token.cc
    )
    target_link_libraries(test_token PRIVATE tinyusdz_next)

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

    add_executable(test_dtoa
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_dtoa.cc
    )
    target_link_libraries(test_dtoa PRIVATE tinyusdz_next)

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

    add_executable(test_lazy_timesamples
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_lazy_timesamples.cc
    )
    target_link_libraries(test_lazy_timesamples PRIVATE tinyusdz_next)

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

    add_executable(test_next_io
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/test_next_io.cc
    )
    target_link_libraries(test_next_io PRIVATE tinyusdz_next)

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
    # Allocator (if enabled) must precede the rest so it wins symbol resolution.
    target_link_libraries(next_usdcat PRIVATE
        ${TINYUSDZ_NEXT_ALLOCATOR_LIB} tinyusdz_next)
    # When jemalloc is the linked allocator, bake its no-purge config into the
    # tool via jemalloc's static `malloc_conf` global (see next_usdcat.cc) so the
    # write-phase decay-purge stall is avoided without any MALLOC_CONF env var.
    if(TINYUSDZ_NEXT_ALLOCATOR_IS_JEMALLOC)
        target_compile_definitions(next_usdcat PRIVATE TINYUSDZ_NEXT_ALLOC_JEMALLOC=1)
    endif()

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
    # The bulk of the code lives in next_core now; instrument it (and the thin
    # C API in tinyusdz_next) so coverage-guided fuzzing explores the whole lib.
    target_compile_options(next_core PRIVATE ${NEXT_FUZZ_LIB_FLAGS})
    target_compile_options(tinyusdz_next PRIVATE ${NEXT_FUZZ_LIB_FLAGS})
    foreach(fz next_usdc next_usda next_compose next_dtoa)
        add_executable(fuzz_${fz}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fuzzer/${fz}_fuzzmain.cc
        )
        target_link_libraries(fuzz_${fz} PRIVATE tinyusdz_next)
        target_compile_options(fuzz_${fz} PRIVATE ${NEXT_FUZZ_BIN_FLAGS})
        target_link_options(fuzz_${fz} PRIVATE ${NEXT_FUZZ_BIN_FLAGS})
    endforeach()
endif()

# ============================================================
# ThreadSanitizer regression tests (opt-in)
# ============================================================
# Guards the data races fixed in the crate reader (async fieldsets decode vs
# ReadSpecs) and the PropNameTable/TfToken frozen fast path. Enabling this
# instruments next_core (and, via PUBLIC link flags, everything that links it)
# with -fsanitize=thread and registers the TSan tests, so a `ctest` run FAILS if
# a race is reintroduced. Requires threads; use a dedicated build dir:
#   cmake -S src/next -B build-next-tsan -DTINYUSDZ_NEXT_BUILD_TESTS=ON \
#         -DTINYUSDZ_NEXT_ENABLE_THREAD=ON -DTINYUSDZ_NEXT_BUILD_TSAN_TESTS=ON
option(TINYUSDZ_NEXT_BUILD_TSAN_TESTS
    "Instrument with -fsanitize=thread and register the TSan regression tests" OFF)
if(TINYUSDZ_NEXT_BUILD_TSAN_TESTS)
    if(NOT TINYUSDZ_NEXT_ENABLE_THREAD)
        message(FATAL_ERROR "TINYUSDZ_NEXT_BUILD_TSAN_TESTS requires TINYUSDZ_NEXT_ENABLE_THREAD=ON")
    endif()
    if(NOT TINYUSDZ_NEXT_BUILD_TESTS)
        message(FATAL_ERROR "TINYUSDZ_NEXT_BUILD_TSAN_TESTS requires TINYUSDZ_NEXT_BUILD_TESTS=ON")
    endif()
    set(NEXT_TSAN_FLAGS -fsanitize=thread -g -fno-omit-frame-pointer)
    # PUBLIC so every consumer (tools + tests) links the TSan runtime and is
    # instrumented -- a TSan program must be built whole.
    target_compile_options(next_core PUBLIC ${NEXT_TSAN_FLAGS})
    target_link_options(next_core PUBLIC ${NEXT_TSAN_FLAGS})
    add_executable(tsan_propname
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/next/tsan_propname.cc)
    target_link_libraries(tsan_propname PRIVATE tinyusdz_next)
    # Some kernels make TSan abort at startup with "unexpected memory mapping"
    # (high-entropy ASLR); run under `setarch -R` (ADDR_NO_RANDOMIZE) when
    # available. A detected race aborts nonzero -> the test FAILS.
    find_program(NEXT_SETARCH setarch)
    if(NEXT_SETARCH)
        add_test(NAME next_test_tsan_propname
            COMMAND ${NEXT_SETARCH} -R $<TARGET_FILE:tsan_propname>)
    else()
        add_test(NAME next_test_tsan_propname COMMAND tsan_propname)
    endif()
    set_tests_properties(next_test_tsan_propname PROPERTIES
        ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66"
        LABELS "next;tsan")
endif()

# Tydra Next (render data conversion)
# Define tydra/next source files
set(TYDRA_NEXT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-extract.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/materialx.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../mtlx-xml-tokenizer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../mtlx-xml-parser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../mtlx-dom.cc
)

set(TYDRA_NEXT_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/chunked-array.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-data.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/scene-access.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-extract.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/render-converter.hh
    ${CMAKE_CURRENT_SOURCE_DIR}/../tydra/next/materialx.hh
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
    add_test(NAME next_test_arena COMMAND test_arena
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_token COMMAND test_token
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
    add_test(NAME next_test_dtoa COMMAND test_dtoa
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
    add_test(NAME next_test_lazy_timesamples COMMAND test_lazy_timesamples
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_schemas COMMAND test_schemas
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_schemas_ext COMMAND test_schemas_ext
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdcat_roundtrip COMMAND test_usdcat_roundtrip
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdz_writer COMMAND test_usdz_writer
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_next_io COMMAND test_next_io
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_composition COMMAND test_composition
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_crash_regressions COMMAND test_crash_regressions
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_usdc_malformed COMMAND test_usdc_malformed
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_load_rules COMMAND test_load_rules
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_test_tydra COMMAND test_tydra_next
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME next_benchmark COMMAND benchmark_next
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set(TINYUSDZ_NEXT_SMOKE_TESTS
        next_test_tinyusdz
        next_test_arena
        next_test_token
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
        next_test_lazy_timesamples
        next_test_schemas
        next_test_schemas_ext
        next_test_usdcat_roundtrip
        next_test_usdz_writer
        next_test_next_io
        next_test_composition
        next_test_crash_regressions
        next_test_usdc_malformed
        next_test_load_rules
        next_test_tydra)
    set_tests_properties(${TINYUSDZ_NEXT_SMOKE_TESTS}
        PROPERTIES LABELS "next")
    set_tests_properties(next_benchmark
        PROPERTIES LABELS "next;benchmark")

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
        set_tests_properties(next_corpus_parse
            PROPERTIES LABELS "next;corpus")
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
