cmake_minimum_required(VERSION 3.4.1)

option(TINYUSDZ_USE_USDOBJ "Build with built-in .obj support" On)

set(TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/../../../../../src/tinyusdz.cc
    ${PROJECT_SOURCE_DIR}/../../../../../src/prim-types.cc
    ${PROJECT_SOURCE_DIR}/../../../../../src/usda-parser.cc
    ${PROJECT_SOURCE_DIR}/../../../../../src/io-util.cc
    ${PROJECT_SOURCE_DIR}/../../../../../src/pprinter.cc
        )

if (TINYUSDZ_USE_USDOBJ)
  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/../../../../../src/usdObj.cc)
endif()


set(TINYUSDZ_DEP_SOURCES
        ${PROJECT_SOURCE_DIR}/../../../../../src/integerCoding.cpp
        ${PROJECT_SOURCE_DIR}/../../../../../src/lz4-compression.cc
        ${PROJECT_SOURCE_DIR}/../../../../../src/pxrLZ4/lz4.cpp
        ${PROJECT_SOURCE_DIR}/../../../../../src/external/ryu/ryu/s2d.c
        ${PROJECT_SOURCE_DIR}/../../../../../src/external/ryu/ryu/s2f.c
        )


# Reuse files from sdlviewer
set(USDVIEW_SOURCES
  ${PROJECT_SOURCE_DIR}/../../../../../examples/sdlviewer/simple-render.cc
  ${PROJECT_SOURCE_DIR}/../../../../../examples/common/matrix.cc
  ${PROJECT_SOURCE_DIR}/../../../../../examples/common/trackball.cc
  )

# Build the libhello-oboe library
add_library(hello-tinyusdz SHARED
        jni-tinyusdz.cc
        render-ctx.cc
        ${TINYUSDZ_SOURCES}
        ${TINYUSDZ_DEP_SOURCES}
        ${USDVIEW_SOURCES}
        )

target_link_libraries(hello-tinyusdz jnigraphics android log)

target_include_directories(hello-tinyusdz PRIVATE
        ${PROJECT_SOURCE_DIR}/../../../../../src/
        ${PROJECT_SOURCE_DIR}/../../../../../src/external/ryu

        # nanort, nanosg, etc
        ${PROJECT_SOURCE_DIR}/../../../../../examples/common/

        # sdlviewer example
        ${PROJECT_SOURCE_DIR}/../../../../../examples/sdlviewer/
        )

if (TINYUSDZ_USE_USDOBJ)
  target_compile_definitions(hello-tinyusdz PRIVATE "TINYUSDZ_USE_USDOBJ")
endif()

# Required to load .usd files from Android asset for demo purpose
# When you embed TinyUSDZ to your own app, you are better to load .usd files from a memory and turn this define off.
target_compile_definitions(hello-tinyusdz PRIVATE "TINYUSDZ_ANDROID_LOAD_FROM_ASSETS")

# Enable optimization flags: if having problems with source level debugging,
# disable -Ofast ( and debug ), re-enable after done debugging.
target_compile_options(hello-tinyusdz PRIVATE -Wall -Werror "$<$<CONFIG:RELEASE>:-Ofast>")
