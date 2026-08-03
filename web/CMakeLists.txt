# Assume this project is invoked by emcmake.
cmake_minimum_required(VERSION 3.16)

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake)
#list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake/sanitizers)
#find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)


option(TINYUSDZ_WASM64 "Use wasm64(memory64). Available only for Chrome and Firefox as of 2025 Jul" OFF)
option(TINYUSDZ_WASM_DEBUG "Enable WASM debug mode with source maps and assertions" OFF)
option(TINYUSDZ_WASM_SIMD "Enable WebAssembly SIMD (128-bit, widely supported since 2021)" ON)
option(TINYUSDZ_WASM_RELAXED_SIMD "Enable WebAssembly Relaxed SIMD (requires recent browsers)" OFF)
option(TINYUSDZ_WASM_COROUTINE "Enable C++20 coroutine-based async loading (allows browser repaint during loading)" ON)
option(TINYUSDZ_WASM_NEXT_ONLY "Build next-core + tydra-next only WASM module as tinyusdz_next" OFF)
option(TINYUSDZ_WASM_LEGACY_ONLY "Build the tinyusdz WASM module WITHOUT the next core (legacy loader only; drops nextFlatten*/RenderStream APIs)" OFF)

if (TINYUSDZ_WASM_NEXT_ONLY)
  if (TINYUSDZ_WASM64)
    set(BUILD_TARGET "tinyusdz_next_64")
  else()
    set(BUILD_TARGET "tinyusdz_next")
  endif()
elseif (TINYUSDZ_WASM_LEGACY_ONLY)
  # Distinct module name so the stripped variant never clobbers the default
  # legacy+next tinyusdz.js/wasm output.
  if (TINYUSDZ_WASM64)
    set(BUILD_TARGET "tinyusdz_legacy_64")
  else()
    set(BUILD_TARGET "tinyusdz_legacy")
  endif()
elseif (TINYUSDZ_WASM64)
  set(BUILD_TARGET "tinyusdz_64")
else()
  set(BUILD_TARGET "tinyusdz")
endif()

project(${BUILD_TARGET} CXX C)

include(CTest)
option(TINYUSDZ_WASM_BROWSER_REGRESSION_TESTS
       "Register host-browser WASM regression tests with CTest" ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "TinyUSDZ WASM: CMAKE_BUILD_TYPE not set; defaulting to MinSizeRel.")
  set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING
      "Build type (Debug|Release|RelWithDebInfo|MinSizeRel)" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
      "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Make C++20 default to use C++20 coroutine.
# Emscripten 4.0 or above supports(C++20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SIMD configuration
# WebAssembly SIMD is widely supported (Chrome 91+, Firefox 89+, Safari 16.4+, Edge 91+)
# See: https://caniuse.com/wasm-simd
set(TINYUSDZ_SIMD_FLAGS "")
if (TINYUSDZ_WASM_SIMD)
  message(STATUS "TinyUSDZ WASM: SIMD enabled (-msimd128)")
  set(TINYUSDZ_SIMD_FLAGS "-msimd128")
  if (TINYUSDZ_WASM_RELAXED_SIMD)
    message(STATUS "TinyUSDZ WASM: Relaxed SIMD enabled (-mrelaxed-simd)")
    set(TINYUSDZ_SIMD_FLAGS "${TINYUSDZ_SIMD_FLAGS} -mrelaxed-simd")
  endif()
else()
  # No wasm SIMD: also force the next parser's scanner onto its scalar path so
  # Backend() honestly reports "scalar" (the SSE2/NEON gates are inert on wasm
  # anyway, but keep one SIMD knob coherent across the build).
  add_compile_definitions(TINYUSDZ_DISABLE_SIMD)
endif()

set(TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS "")
if (TINYUSDZ_SIMD_FLAGS)
  separate_arguments(TINYUSDZ_SIMD_OPTIONS NATIVE_COMMAND "${TINYUSDZ_SIMD_FLAGS}")
  list(APPEND TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS ${TINYUSDZ_SIMD_OPTIONS})
endif()

if (TINYUSDZ_WASM64)
  list(APPEND TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS "-sMEMORY64=1")
endif()

# The OpenPBR visual parity test requires the combined module because it renders
# every fixture through both the legacy and next backends. It is a host-side
# Node/Puppeteer test even though this project is configured through emcmake.
if (BUILD_TESTING AND TINYUSDZ_WASM_BROWSER_REGRESSION_TESTS AND
    NOT TINYUSDZ_WASM_NEXT_ONLY AND NOT TINYUSDZ_WASM_LEGACY_ONLY)
  find_program(TINYUSDZ_NODE_EXECUTABLE NAMES node nodejs)
  find_program(TINYUSDZ_CHROME_EXECUTABLE
               NAMES google-chrome-stable google-chrome chromium chromium-browser)
  set(TINYUSDZ_OPENPBR_VISUAL_DIFF
      "${PROJECT_SOURCE_DIR}/js/cli/openpbr-visual-diff.mjs")
  set(TINYUSDZ_NEXT_WASM_JS
      "${PROJECT_SOURCE_DIR}/js/src/tinyusdz/tinyusdz_next.js")
  set(TINYUSDZ_NEXT_WASM_BINARY
      "${PROJECT_SOURCE_DIR}/js/src/tinyusdz/tinyusdz_next.wasm")
  if (TINYUSDZ_NODE_EXECUTABLE AND EXISTS "${TINYUSDZ_OPENPBR_VISUAL_DIFF}" AND
      EXISTS "${TINYUSDZ_NEXT_WASM_JS}" AND EXISTS "${TINYUSDZ_NEXT_WASM_BINARY}")
    add_test(
      NAME wasm-regression-openpbr-visual-diff
      COMMAND "${TINYUSDZ_NODE_EXECUTABLE}" "${TINYUSDZ_OPENPBR_VISUAL_DIFF}"
              --sw --dump-images --dump-composite
              --out "${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/openpbr-visual-diff")
    set_tests_properties(
      wasm-regression-openpbr-visual-diff
      PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/js"
        TIMEOUT 300
        RUN_SERIAL TRUE
        LABELS "wasm;browser;regression")
    if (TINYUSDZ_CHROME_EXECUTABLE)
      # npm installs used by CI may intentionally skip Puppeteer's browser
      # download. Prefer an installed browser when one is available so a plain
      # `ctest` invocation remains self-contained.
      set_tests_properties(
        wasm-regression-openpbr-visual-diff
        PROPERTIES
          ENVIRONMENT
            "PUPPETEER_EXECUTABLE_PATH=${TINYUSDZ_CHROME_EXECUTABLE}")
    endif()
  else()
    message(STATUS
      "TinyUSDZ WASM: skipping OpenPBR browser regression registration "
      "(Node, visual-diff CLI, or prebuilt next-only WASM module not found)")
  endif()

  # Color management has two complementary gates: direct legacy/next WASM
  # value parity, and float render-target pixel readback in a headless browser.
  # The browser runner starts its own loopback Vite server when no URL is given.
  set(TINYUSDZ_COLORSPACE_BACKEND_TEST
      "${PROJECT_SOURCE_DIR}/js/tests/colorspace-backend-parity.mjs")
  set(TINYUSDZ_COLORSPACE_PIXEL_TEST
      "${PROJECT_SOURCE_DIR}/js/tests/colorspace-render-batch.mjs")
  set(TINYUSDZ_LEGACY_WASM_JS
      "${PROJECT_SOURCE_DIR}/js/src/tinyusdz/tinyusdz.js")
  set(TINYUSDZ_LEGACY_WASM_BINARY
      "${PROJECT_SOURCE_DIR}/js/src/tinyusdz/tinyusdz.wasm")
  if (TINYUSDZ_NODE_EXECUTABLE AND
      EXISTS "${TINYUSDZ_COLORSPACE_BACKEND_TEST}" AND
      EXISTS "${TINYUSDZ_LEGACY_WASM_JS}" AND
      EXISTS "${TINYUSDZ_LEGACY_WASM_BINARY}" AND
      EXISTS "${TINYUSDZ_NEXT_WASM_JS}" AND
      EXISTS "${TINYUSDZ_NEXT_WASM_BINARY}")
    add_test(
      NAME wasm-regression-colorspace-backend-parity
      COMMAND "${TINYUSDZ_NODE_EXECUTABLE}"
              "${TINYUSDZ_COLORSPACE_BACKEND_TEST}")
    set_tests_properties(
      wasm-regression-colorspace-backend-parity
      PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/js"
        TIMEOUT 90
        LABELS "wasm;colorspace;regression")

    if (TINYUSDZ_CHROME_EXECUTABLE AND
        EXISTS "${TINYUSDZ_COLORSPACE_PIXEL_TEST}" AND
        EXISTS "${PROJECT_SOURCE_DIR}/js/node_modules/puppeteer/package.json" AND
        EXISTS "${PROJECT_SOURCE_DIR}/js/node_modules/vite/bin/vite.js")
      add_test(
        NAME wasm-regression-colorspace-pixel-readback
        COMMAND "${TINYUSDZ_NODE_EXECUTABLE}"
                "${TINYUSDZ_COLORSPACE_PIXEL_TEST}")
      set_tests_properties(
        wasm-regression-colorspace-pixel-readback
        PROPERTIES
          ENVIRONMENT
            "PUPPETEER_EXECUTABLE_PATH=${TINYUSDZ_CHROME_EXECUTABLE};TINYUSDZ_COLORSPACE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/colorspace"
          WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/js"
          TIMEOUT 120
          RUN_SERIAL TRUE
          LABELS "wasm;colorspace;browser;pixel;regression")
    endif()
  endif()

  if (TINYUSDZ_NODE_EXECUTABLE)
    add_test(
      NAME wasm-js-loader-texture-compression
      COMMAND "${TINYUSDZ_NODE_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/js/tests/loader-utils-texture-compression.mjs")
    add_test(
      NAME wasm-texture-compression-abi
      COMMAND "${TINYUSDZ_NODE_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/js/tests/texture-compression-wasm.mjs")
    add_test(
      NAME wasm-js-loader-basis-ktx2
      COMMAND "${TINYUSDZ_NODE_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/js/tests/loader-utils-basis-ktx2.mjs")
    if (TINYUSDZ_CHROME_EXECUTABLE)
      add_test(
        NAME wasm-texture-compression-three-ktx2
        COMMAND "${TINYUSDZ_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/js/tests/texture-compression-three-ktx2.mjs")
      set_tests_properties(
        wasm-texture-compression-three-ktx2
        PROPERTIES
          ENVIRONMENT
            "PUPPETEER_EXECUTABLE_PATH=${TINYUSDZ_CHROME_EXECUTABLE}"
          WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/js"
          TIMEOUT 90
          LABELS "wasm;texture;browser;regression")
    endif()
    set_tests_properties(
      wasm-js-loader-texture-compression wasm-texture-compression-abi
      wasm-js-loader-basis-ktx2
      PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/js"
        TIMEOUT 60
        LABELS "wasm;texture;regression")
  endif()
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")

  if (TINYUSDZ_WASM_DEBUG)
    # Enhanced debug mode with source maps
    if (TINYUSDZ_WASM64)
      set(CMAKE_CXX_FLAGS_DEBUG "-g -gsource-map -sMEMORY64 ${TINYUSDZ_SIMD_FLAGS}")
      set(CMAKE_C_FLAGS_DEBUG "-g -gsource-map -sMEMORY64 ${TINYUSDZ_SIMD_FLAGS}")
    else()
      set(CMAKE_CXX_FLAGS_DEBUG "-g -gsource-map ${TINYUSDZ_SIMD_FLAGS}")
      set(CMAKE_C_FLAGS_DEBUG "-g -gsource-map ${TINYUSDZ_SIMD_FLAGS}")
    endif()
  else()
    # Standard debug mode
    if (TINYUSDZ_WASM64)
      set(CMAKE_CXX_FLAGS_DEBUG "-g4 -sMEMORY64 ${TINYUSDZ_SIMD_FLAGS}")
      set(CMAKE_C_FLAGS_DEBUG "-g4 -sMEMORY64 ${TINYUSDZ_SIMD_FLAGS}")
    else()
      set(CMAKE_CXX_FLAGS_DEBUG "-g4 ${TINYUSDZ_SIMD_FLAGS}")
      set(CMAKE_C_FLAGS_DEBUG "-g4 ${TINYUSDZ_SIMD_FLAGS}")
    endif()
  endif()

else()

  add_compile_options(${TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS})

  if (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    # Keep CMake's default MinSizeRel flags off the command line. CMake uses
    # -Os for MinSizeRel, which would otherwise mix with the explicit -Oz.
    set(CMAKE_CXX_FLAGS_MINSIZEREL "")
    set(CMAKE_C_FLAGS_MINSIZEREL "")

    add_compile_options("-Oz")
    add_compile_definitions(NDEBUG)
  endif()
endif()

if (NOT EMSCRIPTEN)
  message(FATAL "Must be compiled with emscripten")
endif()


option(TINYUSDZ_WASM_DEMODEV "Demo developer mode(install WASM modules to demo/node_modules/tinyusdz." OFF)
if (TINYUSDZ_WASM_DEMODEV)
  message(STATUS "demodev build")
endif()

# WASM/browser builds intentionally keep C++ threading disabled by default. Do
# not pull in Emscripten pthreads/SharedArrayBuffer unless a future explicit
# browser-threading build option opts into it.
set(TINYUSDZ_ENABLE_THREAD OFF CACHE BOOL "Disable std::thread paths for default WASM builds" FORCE)

if (TINYUSDZ_WASM_NEXT_ONLY)
  message(STATUS "TinyUSDZ WASM: building next-core + tydra-next only module")
  set(TINYUSDZ_CXX_RTTI OFF CACHE BOOL "Disable RTTI in next/tydra-next libs" FORCE)
  set(TINYUSDZ_CXX_EXCEPTIONS OFF CACHE BOOL "Disable C++ exceptions in next/tydra-next libs" FORCE)
  set(TINYUSDZ_NEXT_ENABLE_THREAD OFF CACHE BOOL "Disable next std::thread paths for WASM" FORCE)

  add_subdirectory(${PROJECT_SOURCE_DIR}/../src/next tinyusdz_next_build EXCLUDE_FROM_ALL)

  add_executable(tinyusdz_next_wasm binding-next.cc)
  # tinysubdiv (tsd): dependency-free subdivision streaming for SubdivStreamer.
  target_sources(tinyusdz_next_wasm PRIVATE
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-validate.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-topology.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-bilinear.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-catmark.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-loop.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-fvar.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-refine.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-stream.cc
    ${PROJECT_SOURCE_DIR}/../src/tsd/tsd-limit.cc)
  target_link_libraries(tinyusdz_next_wasm PRIVATE tydra_next tinyusdz_next)
  target_include_directories(tinyusdz_next_wasm PRIVATE "${PROJECT_SOURCE_DIR}/../src/")
  target_compile_features(tinyusdz_next_wasm PRIVATE cxx_std_17)
  target_compile_options(tinyusdz_next_wasm
                         PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
  target_compile_definitions(tinyusdz_next_wasm
                             PRIVATE JSON_NOEXCEPTION=1
                                     TINYUSDZ_CXX_EXCEPTIONS=0)
  if (TINYUSDZ_WASM64)
    target_compile_definitions(tinyusdz_next_wasm PRIVATE TINYUSDZ_WASM_MEMORY64=1)
  endif()

  if (TINYUSDZ_WASM_DEMODEV)
    set(OUTPUT_WASM_DIR ${PROJECT_SOURCE_DIR}/demo/node_modules/tinyusdz/)
  else()
    set(OUTPUT_WASM_DIR ${PROJECT_SOURCE_DIR}/js/src/tinyusdz/)
  endif()

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(TINYUSDZ_NEXT_EMCC_LINK_FLAGS "-g4 -sENVIRONMENT='web,worker,node' \
     -sMALLOC=emmalloc \
     -sSTACK_SIZE=512000 \
     -sALLOW_MEMORY_GROWTH=1 \
     -sMEMORY_GROWTH_GEOMETRIC_STEP=0.1 \
     -sFILESYSTEM=0 \
     -sWASM_BIGINT=1 \
     -sMODULARIZE=1 -sEXPORT_ES6 \
     -sINVOKE_RUN=0 --bind ")
  else()
    if (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
      set(TINYUSDZ_NEXT_EMCC_LINK_OPT_FLAGS "-Oz ")
    else()
      set(TINYUSDZ_NEXT_EMCC_LINK_OPT_FLAGS "")
    endif()
    set(TINYUSDZ_NEXT_EMCC_LINK_FLAGS "${TINYUSDZ_NEXT_EMCC_LINK_OPT_FLAGS}-sENVIRONMENT='web,worker,node' \
     -sMALLOC=emmalloc \
     -sSTACK_SIZE=512000 \
     -sALLOW_MEMORY_GROWTH=1 \
     -sMEMORY_GROWTH_GEOMETRIC_STEP=0.1 \
     -sFILESYSTEM=0 \
     -sSAFE_HEAP=0 \
     -sWASM_BIGINT=1 \
     -sMODULARIZE=1 -sEXPORT_ES6 \
     -sINVOKE_RUN=0 --bind ")
  endif()
  if (TINYUSDZ_SIMD_FLAGS)
    string(APPEND TINYUSDZ_NEXT_EMCC_LINK_FLAGS " ${TINYUSDZ_SIMD_FLAGS}")
  endif()
  if (TINYUSDZ_WASM64)
    string(APPEND TINYUSDZ_NEXT_EMCC_LINK_FLAGS " -sASSERTIONS=2 -sMEMORY64 -sMAXIMUM_MEMORY=16GB -sMALLOC=dlmalloc")
  else()
    string(APPEND TINYUSDZ_NEXT_EMCC_LINK_FLAGS " -sASSERTIONS=1")
  endif()
  string(APPEND TINYUSDZ_NEXT_EMCC_LINK_FLAGS " -sEXPORTED_RUNTIME_METHODS=['HEAPU8']")

  set_target_properties(
      tinyusdz_next_wasm
      PROPERTIES OUTPUT_NAME ${BUILD_TARGET}
                 SUFFIX ".js"
                 RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_WASM_DIR}
                 LINK_FLAGS "${TINYUSDZ_NEXT_EMCC_LINK_FLAGS}")
  return()
endif()

# [tinyusdz]
add_subdirectory(../  tinyusdz)

# [tinyusdz next] Low-memory lazy-ValueRep flatten pipeline. Compiled directly
# into the WASM module (not the standalone next lib) so it shares the main lib's
# lz4 (avoids duplicate LZ4_* symbols). Only the sources the flatten pipeline
# needs are listed; unused code is stripped by wasm-ld DCE.
set(TINYUSDZ_NEXT_DIR "${PROJECT_SOURCE_DIR}/../src/next")
list(
  APPEND
  NEXT_SOURCES
  ${TINYUSDZ_NEXT_DIR}/types/type-info.cc
  ${TINYUSDZ_NEXT_DIR}/types/value.cc
  ${TINYUSDZ_NEXT_DIR}/types/spline.cc
  ${TINYUSDZ_NEXT_DIR}/types/interpolation.cc
  ${TINYUSDZ_NEXT_DIR}/writer/dtoa.cc
  # value-printer defines next::PrintValue, referenced by the crate
  # reader/writer (spline customData round-trip) — without it wasm-ld fails
  # with an undefined-symbol error on a clean build.
  ${TINYUSDZ_NEXT_DIR}/writer/value-printer.cc
  ${TINYUSDZ_NEXT_DIR}/prim/path.cc
  ${TINYUSDZ_NEXT_DIR}/prim/attribute.cc
  ${TINYUSDZ_NEXT_DIR}/prim/prim.cc
  ${TINYUSDZ_NEXT_DIR}/layer/array-edit.cc
  ${TINYUSDZ_NEXT_DIR}/layer/property-index.cc
  ${TINYUSDZ_NEXT_DIR}/layer/prim-spec.cc
  ${TINYUSDZ_NEXT_DIR}/layer/layer.cc
  ${TINYUSDZ_NEXT_DIR}/stage/stage.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-format.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-data-source.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-arrays.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-decode.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-api.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-sections.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-stage.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-unpack.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader-vectors.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-writer-types.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-writer.cc
  ${TINYUSDZ_NEXT_DIR}/crate/variant-holders.cc
  ${TINYUSDZ_NEXT_DIR}/parser/lexer.cc
  ${TINYUSDZ_NEXT_DIR}/parser/value-parser.cc
  ${TINYUSDZ_NEXT_DIR}/parser/value-parser-types.cc
  ${TINYUSDZ_NEXT_DIR}/parser/ascii-parser.cc
  ${TINYUSDZ_NEXT_DIR}/parser/ascii-parser-metadata.cc
  ${TINYUSDZ_NEXT_DIR}/parser/ascii-parser-prim.cc
  ${TINYUSDZ_NEXT_DIR}/parser/ascii-parser-timesamples.cc
  ${TINYUSDZ_NEXT_DIR}/parser/ascii-parser-utils.cc
  ${TINYUSDZ_NEXT_DIR}/parser/ascii-parser-variants.cc
  ${TINYUSDZ_NEXT_DIR}/parser/simd-scan.cc
  ${TINYUSDZ_NEXT_DIR}/composition/composition.cc
  ${TINYUSDZ_NEXT_DIR}/resolver/asset-resolver.cc
  ${TINYUSDZ_NEXT_DIR}/pipeline/flatten.cc
  ${TINYUSDZ_NEXT_DIR}/pcp/layer-registry.cc
  ${TINYUSDZ_NEXT_DIR}/reader/usda-reader.cc
  ${TINYUSDZ_NEXT_DIR}/reader/usdc-reader.cc
  ${TINYUSDZ_NEXT_DIR}/reader/usdz-reader.cc
  ${TINYUSDZ_NEXT_DIR}/schema/geom-mesh.cc
  ${TINYUSDZ_NEXT_DIR}/schema/geom-xform.cc
  ${TINYUSDZ_NEXT_DIR}/schema/usd-shade.cc
  ${TINYUSDZ_NEXT_DIR}/eval/attribute-eval.cc
  ${TINYUSDZ_NEXT_DIR}/eval/value-clip.cc
  ${TINYUSDZ_NEXT_DIR}/schema/schema-registry.cc
)

list(
  APPEND
  SOURCES
  binding.cc
)
# Default module = legacy + next (nextFlatten* lazy pipeline + RenderStream).
# TINYUSDZ_WASM_LEGACY_ONLY=ON strips the next core: NEXT_SOURCES are not
# compiled and binding.cc's next-dependent APIs compile out (guarded by
# TINYUSDZ_WASM_WITH_NEXT).
if (NOT TINYUSDZ_WASM_LEGACY_ONLY)
  list(APPEND SOURCES ${NEXT_SOURCES})
endif()


add_executable(${BUILD_TARGET} ${SOURCES})
add_sanitizers(${BUILD_TARGET})

target_compile_options(${BUILD_TARGET} PRIVATE ${EXT_COMPILE_OPTIONS})
target_compile_options(${BUILD_TARGET}
                       PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
                               $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
target_compile_definitions(${BUILD_TARGET}
                           PRIVATE JSON_NOEXCEPTION=1
                                   TINYUSDZ_CXX_EXCEPTIONS=0
                                   TINYUSDZ_CXX_RTTI=0
                                   EMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0)
if (NOT TINYUSDZ_WASM_LEGACY_ONLY)
  target_compile_definitions(${BUILD_TARGET} PRIVATE TINYUSDZ_WASM_WITH_NEXT=1)
endif()
if (TINYUSDZ_WASM64)
  target_compile_definitions(${BUILD_TARGET} PRIVATE TINYUSDZ_WASM_MEMORY64=1)
endif()

# Pass through compile definitions for feature flags
if (TINYUSDZ_WITH_EXR)
  target_compile_definitions(${BUILD_TARGET} PRIVATE TINYUSDZ_WITH_EXR=1)
endif()

# C++20 coroutine support for async loading
if (TINYUSDZ_WASM_COROUTINE)
  message(STATUS "TinyUSDZ WASM: C++20 coroutine async loading enabled")
  target_compile_definitions(${BUILD_TARGET} PRIVATE TINYUSDZ_USE_COROUTINE=1)
endif()

# tinyusdz dir
target_include_directories(${BUILD_TARGET}
                           PRIVATE "${PROJECT_SOURCE_DIR}/../src/")


target_link_libraries(${BUILD_TARGET} PRIVATE tinyusdz::tinyusdz_static
                                              ${EXT_LIBRARIES} PUBLIC ${CMAKE_DL_LIBS})

# The main WASM module exposes the same Basis-free `uni` scene-texture path as
# the standalone texcomp demo. Referencing textools directly keeps its public
# headers visible to binding.cc and prevents the transcoders from being stripped.
if (TINYUSDZ_WITH_TEXTOOLS)
  target_compile_definitions(${BUILD_TARGET} PRIVATE TINYUSDZ_WITH_TEXTOOLS=1)
  target_link_libraries(${BUILD_TARGET} PRIVATE tinyusdz_textools)
endif()

source_group("Source Files" FILES ${SOURCES})

if (EMSCRIPTEN)

    if (TINYUSDZ_WASM_DEMODEV)
      set(OUTPUT_WASM_DIR ${PROJECT_SOURCE_DIR}/demo/node_modules/tinyusdz/)
    else()
      set(OUTPUT_WASM_DIR ${PROJECT_SOURCE_DIR}/js/src/tinyusdz/)
    endif()

    set_target_properties(
        ${BUILD_TARGET}
        PROPERTIES OUTPUT_NAME ${BUILD_TARGET}
                   SUFFIX ".js"
                   RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_WASM_DIR})
    #set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()


# for debugging
# --memoryprofiler 
# --source-map-base http://localhost:8021/ 

# flags = try to lower heap memory use

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (TINYUSDZ_WASM_DEBUG)
    # Enhanced debug mode with source maps, stack traces, and runtime checks
    set(TINYUSDZ_EMCC_LINK_FLAGS "-g -gsource-map -sENVIRONMENT='web,worker,node' \
     -sMALLOC=emmalloc \
     -sSTACK_SIZE=1024000 \
     -sALLOW_MEMORY_GROWTH=1 \
     -sFILESYSTEM=0 \
     -sSAFE_HEAP=1 \
     -sSTACK_OVERFLOW_CHECK=2 \
     -sASSERTIONS=2 \
     -sWASM_BIGINT=1 \
     -sMODULARIZE=1 -sEXPORT_ES6 \
     -sINVOKE_RUN=0 --bind \
     --source-map-base http://localhost:5173/ ")
  else()
    # Standard debug mode
    set(TINYUSDZ_EMCC_LINK_FLAGS "-g4 -sENVIRONMENT='web,worker,node' \
     -sMALLOC=emmalloc \
     -sSTACK_SIZE=1024000 \
     -sALLOW_MEMORY_GROWTH=1 \
     -sFILESYSTEM=0 \
     -sSAFE_HEAP=1 \
     -sWASM_BIGINT=1 \
     -sMODULARIZE=1 -sEXPORT_ES6 \
     -sINVOKE_RUN=0 --bind \
     --source-map-base http://localhost:5173/ ")
  endif()
else()
  if (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(TINYUSDZ_EMCC_LINK_OPT_FLAGS "-Oz ")
  else()
    set(TINYUSDZ_EMCC_LINK_OPT_FLAGS "")
  endif()

  set(TINYUSDZ_EMCC_LINK_FLAGS "${TINYUSDZ_EMCC_LINK_OPT_FLAGS}-sENVIRONMENT='web,worker,node' \
   -sMALLOC=emmalloc \
   -sSTACK_SIZE=512000 \
   -sALLOW_MEMORY_GROWTH=1 \
   -sFILESYSTEM=0 \
   -sSAFE_HEAP=0 \
   -sWASM_BIGINT=1 \
   -sMODULARIZE=1 -sEXPORT_ES6 \
   -sINVOKE_RUN=0 --bind ")
endif()

if (TINYUSDZ_SIMD_FLAGS)
  string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " ${TINYUSDZ_SIMD_FLAGS}")
endif()

if (TINYUSDZ_WASM64)
  # assertion=1 cause runtime error(Cannot mix BigInt and ... in assert()), so use 2
  # MAXIMUM_MEMORY=16GB: only reserves address space (ALLOW_MEMORY_GROWTH grows
  # on demand); the in-heap folder flatten of a multi-GB UE-export scene (e.g.
  # a 2.5 GB source folder) needs ~10 GB.
  # MALLOC=dlmalloc: emmalloc asserts (debug_region_is_consistent in
  # emmalloc_free) once the wasm64 heap grows past ~8 GB; dlmalloc handles the
  # large-heap case. (The later -sMALLOC overrides the emmalloc set in the
  # common flags above.)
  string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " -sASSERTIONS=2 -sMEMORY64 -sMAXIMUM_MEMORY=16GB -sMALLOC=dlmalloc")
else()
  string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " -sASSERTIONS=1 ")
endif()

# Export heap views for zero-copy streaming and typed pointer descriptors used
# by the web loader fast path.
string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " -sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAP8','HEAP16','HEAPU32','HEAPF32']")

message(STATUS ${TINYUSDZ_EMCC_LINK_FLAGS})

set_target_properties(${BUILD_TARGET} PROPERTIES LINK_FLAGS "${TINYUSDZ_EMCC_LINK_FLAGS}")

# ENVIRONMENT=web
# SINGLE_FILE=1
