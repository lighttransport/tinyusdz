# Lean "next" WASM module: USD load/compose + tydra_next RenderScene extraction
# built on next_core + tydra_next ONLY (no legacy tinyusdz_static). This is a
# SEPARATE target from the full web/ module (web/CMakeLists.txt is untouched);
# it produces tinyusdz_next.{js,wasm} for the three.js data path.
#
# Configure/build (emscripten):
#   source <emsdk>/emsdk_env.sh
#   emcmake cmake -S web/next -B web/build_next -DCMAKE_BUILD_TYPE=MinSizeRel
#   cmake --build web/build_next -j
# Output: web/js/src/tinyusdz/tinyusdz_next.{js,wasm}
cmake_minimum_required(VERSION 3.16)
project(tinyusdz_next_wasm CXX C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Match the main WASM module: -Oz + NDEBUG, no exceptions, scalar SIMD path.
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  set(CMAKE_CXX_FLAGS_MINSIZEREL "")
  set(CMAKE_C_FLAGS_MINSIZEREL "")
  add_compile_options(-Oz)
  add_compile_definitions(NDEBUG)
endif()
add_compile_options(-msimd128)
add_compile_definitions(TINYUSDZ_DISABLE_SIMD JSON_NOEXCEPTION=1)
# -fno-exceptions everywhere; RTTI stays ON for the embind TU (embind needs
# type_info names) and is disabled on next_core / tydra_next's own sources below.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)

# next_core / tydra_next read these to also drop RTTI/exceptions on their own
# sources and the schema/eval convenience layer stays IN (tydra_next uses it).
set(TINYUSDZ_CXX_RTTI OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_CXX_EXCEPTIONS OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_NEXT_CORE_MINIMAL OFF CACHE BOOL "" FORCE)

# EXCLUDE_FROM_ALL: only the libs we link (next_core, tydra_next) get built;
# tests / C API / python stay out. src/next defines next_core, tinyusdz_next
# AND tydra_next.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../src/next next_core_build EXCLUDE_FROM_ALL)

# EXR (tinyexr v3, pure-C11 backend) + HDR (stb_image) decode for texture
# images. Compiled straight into this module — the ONLY codecs it carries; the
# rest of image handling stays out of the lean core. The v3 SIMD TUs guard on
# arch and compile to no-ops on wasm, so the whole src/ glob is safe.
set(TINYEXR_V3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/external/tinyexr)
file(GLOB TINYEXR_V3_SOURCES ${TINYEXR_V3_DIR}/src/*.c)
# Minimal codec set: HTJ2K (EXR_NO_JPH) and ZSTD (EXR_NO_ZSTD) compiled out —
# the v3 core is self-contained (its own DEFLATE via fpnge), no miniz/zlib.
set_source_files_properties(${TINYEXR_V3_SOURCES} PROPERTIES
  INCLUDE_DIRECTORIES "${TINYEXR_V3_DIR}/include;${TINYEXR_V3_DIR}/src"
  COMPILE_DEFINITIONS "EXR_NO_JPH;EXR_NO_ZSTD")

# Target name differs from the `tinyusdz_next` library target defined in
# src/next; OUTPUT_NAME still emits tinyusdz_next.{js,wasm}.
add_executable(tinyusdz_next_module binding-next.cc stb_hdr.cc ${TINYEXR_V3_SOURCES})
target_link_libraries(tinyusdz_next_module PRIVATE tydra_next tinyusdz_next)
target_include_directories(tinyusdz_next_module PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  ${TINYEXR_V3_DIR}/include)
target_compile_definitions(tinyusdz_next_module PRIVATE TINYUSDZ_NEXT_WITH_EXR=1)

# Emit next to the existing module so the JS side imports it the same way.
set(OUTPUT_WASM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../js/src/tinyusdz)

set_target_properties(tinyusdz_next_module PROPERTIES
  OUTPUT_NAME tinyusdz_next
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_WASM_DIR}
  LINK_FLAGS "-Oz -msimd128 -sENVIRONMENT=web,worker,node -sMALLOC=emmalloc \
-sSTACK_SIZE=512000 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 -sWASM_BIGINT=1 \
-sMODULARIZE=1 -sEXPORT_ES6 -sINVOKE_RUN=0 --bind -sASSERTIONS=1 \
-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAP8','HEAP16','HEAPU32','HEAPF32']")
