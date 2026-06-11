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
option(TINYUSDZ_WASM_COROUTINE "Enable C++20 coroutine-based async loading (allows browser repaint during loading)" OFF)

if (TINYUSDZ_WASM64)
  set(BUILD_TARGET "tinyusdz_64")
else()
  set(BUILD_TARGET "tinyusdz")
endif()

project(${BUILD_TARGET} CXX C)

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
endif()

set(TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS "")
if (TINYUSDZ_SIMD_FLAGS)
  separate_arguments(TINYUSDZ_SIMD_OPTIONS NATIVE_COMMAND "${TINYUSDZ_SIMD_FLAGS}")
  list(APPEND TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS ${TINYUSDZ_SIMD_OPTIONS})
endif()

if (TINYUSDZ_WASM64)
  list(APPEND TINYUSDZ_WASM_COMMON_COMPILE_OPTIONS "-sMEMORY64=1")
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
  ${TINYUSDZ_NEXT_DIR}/types/interpolation.cc
  ${TINYUSDZ_NEXT_DIR}/prim/path.cc
  ${TINYUSDZ_NEXT_DIR}/prim/attribute.cc
  ${TINYUSDZ_NEXT_DIR}/prim/prim.cc
  ${TINYUSDZ_NEXT_DIR}/layer/property-index.cc
  ${TINYUSDZ_NEXT_DIR}/layer/prim-spec.cc
  ${TINYUSDZ_NEXT_DIR}/layer/layer.cc
  ${TINYUSDZ_NEXT_DIR}/stage/stage.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-format.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-data-source.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-reader.cc
  ${TINYUSDZ_NEXT_DIR}/crate/crate-writer.cc
  ${TINYUSDZ_NEXT_DIR}/composition/composition.cc
  ${TINYUSDZ_NEXT_DIR}/resolver/asset-resolver.cc
  ${TINYUSDZ_NEXT_DIR}/pipeline/flatten.cc
  ${TINYUSDZ_NEXT_DIR}/reader/usdc-reader.cc
  ${TINYUSDZ_NEXT_DIR}/schema/geom-mesh.cc
  ${TINYUSDZ_NEXT_DIR}/schema/usd-shade.cc
  ${TINYUSDZ_NEXT_DIR}/eval/attribute-eval.cc
)

list(
  APPEND
  SOURCES
  binding.cc
  ${NEXT_SOURCES}
)


add_executable(${BUILD_TARGET} ${SOURCES})
add_sanitizers(${BUILD_TARGET})

target_compile_options(${BUILD_TARGET} PRIVATE ${EXT_COMPILE_OPTIONS})
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
     -sDISABLE_EXCEPTION_CATCHING=0 \
     -sNO_DISABLE_EXCEPTION_CATCHING \
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
     -sDISABLE_EXCEPTION_CATCHING=0 \
     -sNO_DISABLE_EXCEPTION_CATCHING \
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
  string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " -sASSERTIONS=2 -sMEMORY64 -sMAXIMUM_MEMORY=8GB")
else()
  string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " -sASSERTIONS=1 ")
endif()

# Export HEAPU8 for zero-copy streaming transfer from JS to WASM
string(APPEND TINYUSDZ_EMCC_LINK_FLAGS " -sEXPORTED_RUNTIME_METHODS=['HEAPU8']")

message(STATUS ${TINYUSDZ_EMCC_LINK_FLAGS})

set_target_properties(${BUILD_TARGET} PROPERTIES LINK_FLAGS "${TINYUSDZ_EMCC_LINK_FLAGS}")

# ENVIRONMENT=web
# SINGLE_FILE=1
