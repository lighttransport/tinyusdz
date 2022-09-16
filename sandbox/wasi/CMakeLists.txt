cmake_minimum_required(VERSION 3.19)

project(tinyusdz_wasi C CXX)
set(PROJECT_NAME tinyusdz_wasi)

set(MAIN_SOURCE main.cc)

option(WASI_SDK_PATH "wasi-sdk path")

set(CMAKE_SYSROOT "${WASI_SDK_PATH}/share/wasi-sysroot")

# C++ exceptions is not availble.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

list(
  APPEND
  TINYUSDZ_SOURCES
  ../../src/tinyusdz.cc
  ../../src/io-util.cc
  ../../src/prim-types.cc
  ../../src/pprinter.cc
  ../../src/value-pprint.cc
  ../../src/value-types.cc
  ../../src/primvar.cc
  ../../src/prim-types.cc
  ../../src/crate-format.cc
  ../../src/crate-reader.cc
  ../../src/crate-writer.cc
  ../../src/crate-pprint.cc
  ../../src/ascii-parser.cc
  ../../src/usda-reader.cc
  ../../src/usda-writer.cc
  ../../src/usdc-reader.cc
  ../../src/usdc-writer.cc
  ../../src/image-loader.cc
  ../../src/prim-reconstruct.cc
  ../../src/tiny-format.cc
  ../../src/xform.cc
  ../../src/usdGeom.cc
  ../../src/usdLux.cc
  ../../src/usdShade.cc
  ../../src/usdSkel.cc
  ../../src/usdMtlx.cc
  ../../src/usdObj.cc
  ../../src/tydra/render-data.cc
)

set(TINYUSDZ_DEP_SOURCES
  ../../src/integerCoding.cpp
  ../../src/lz4-compression.cc
  ../../src/lz4/lz4.c
  ../../src/external/fpng.cpp
  ../../src/external/string_id/string_id.cpp
  ../../src/external/string_id/error.cpp
  ../../src/external/string_id/database.cpp
  ../../src/external/staticstruct.cc
  ../../src/external/tinyxml2/tinyxml2.cpp
  )
  #../../src/external/ryu/ryu/s2d.c
  #../../src/external/ryu/ryu/s2f.c)



add_executable(${PROJECT_NAME} ${MAIN_SOURCE} ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ../../src/external/ryu)
target_include_directories(${PROJECT_NAME} PRIVATE ../../src/external/fast_float/include)
target_include_directories(${PROJECT_NAME} PRIVATE ../../src/)

#set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXCEPTIONS OFF)
