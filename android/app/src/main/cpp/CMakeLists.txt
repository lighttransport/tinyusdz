cmake_minimum_required(VERSION 3.4.1)

set(TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/../../../../../src/tinyusdz.cc
    ${PROJECT_SOURCE_DIR}/../../../../../src/prim-types.cc
    ${PROJECT_SOURCE_DIR}/../../../../../src/usda-parser.cc
        )

set(TINYUSDZ_DEP_SOURCES
        ${PROJECT_SOURCE_DIR}/../../../../../src/integerCoding.cpp
        ${PROJECT_SOURCE_DIR}/../../../../../src/lz4-compression.cc
        ${PROJECT_SOURCE_DIR}/../../../../../src/pxrLZ4/lz4.cpp
        ${PROJECT_SOURCE_DIR}/../../../../../src/external/ryu/ryu/s2d.c
        ${PROJECT_SOURCE_DIR}/../../../../../src/external/ryu/ryu/s2f.c
        )


# Build the libhello-oboe library
add_library(hello-tinyusdz SHARED
        hello-tinyusdz.cc
        ${TINYUSDZ_SOURCES}
        ${TINYUSDZ_DEP_SOURCES}
        )

target_link_libraries(hello-tinyusdz android log)
target_include_directories(hello-tinyusdz PRIVATE
          ${PROJECT_SOURCE_DIR}/../../../../../src/
        ${PROJECT_SOURCE_DIR}/../../../../../src/external/ryu
        )

# Enable optimization flags: if having problems with source level debugging,
# disable -Ofast ( and debug ), re-enable after done debugging.
target_compile_options(hello-tinyusdz PRIVATE -Wall -Werror "$<$<CONFIG:RELEASE>:-Ofast>")
