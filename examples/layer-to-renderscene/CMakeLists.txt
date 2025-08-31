cmake_minimum_required(VERSION 3.16)

set(BUILD_TARGET "layer_to_renderscene_example")
project(${BUILD_TARGET} CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif()

set(TINYUSDZ_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tinyusdz.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tinyusdz.hh
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/stage.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/prim-types.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/prim-types.hh
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/value-types.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/value-types.hh
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/value-pprint.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/pprinter.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/usdGeom.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/usdShade.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/usdSkel.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/usda-reader.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/usdc-reader.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/crate-reader.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ascii-parser.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ascii-parser-basetype.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ascii-parser-timesamples.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/composition.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/asset-resolution.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/str-util.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/io-util.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/prim-reconstruct.cc
)

set(TYDRA_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tydra/render-data.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tydra/layer-to-renderscene.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tydra/scene-access.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/tydra/shader-network.cc
)

add_executable(${BUILD_TARGET}
  main.cc
  ${TINYUSDZ_SOURCES}
  ${TYDRA_SOURCES}
)

target_include_directories(${BUILD_TARGET} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/external
)

target_compile_definitions(${BUILD_TARGET} PRIVATE
  TINYUSDZ_WITH_TYDRA=1
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(${BUILD_TARGET} PRIVATE -Wall -Wextra)
endif()

if(MSVC)
  target_compile_options(${BUILD_TARGET} PRIVATE /W4)
endif()