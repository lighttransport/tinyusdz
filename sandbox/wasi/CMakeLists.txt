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
  ../../src/primvar.cc
  ../../src/usda-parser.cc
  ../../src/usda-writer.cc
  ../../src/image-loader.cc
  ../../src/usdObj.cc
)

set(TINYUSDZ_DEP_SOURCES
  ../../src/integerCoding.cpp
  ../../src/lz4-compression.cc
  ../../src/pxrLZ4/lz4.cpp
  ../../src/external/fpng.cpp
  ../../src/external/staticstruct.cc
  ../../src/external/tinyxml2/tinyxml2.cpp
  ../../src/external/ryu/ryu/s2d.c
  ../../src/external/ryu/ryu/s2f.c)



add_executable(${PROJECT_NAME} ${MAIN_SOURCE} ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ../../src/external/ryu)

#set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXCEPTIONS OFF)
