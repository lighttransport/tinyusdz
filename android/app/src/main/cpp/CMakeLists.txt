cmake_minimum_required(VERSION 3.4.1)

# Build the libhello-oboe library
add_library(hello-tinyusdz SHARED
        hello-tinyusdz.cc
        )

target_link_libraries(hello-tinyusdz android log)

# Enable optimization flags: if having problems with source level debugging,
# disable -Ofast ( and debug ), re-enable after done debugging.
target_compile_options(hello-tinyusdz PRIVATE -Wall -Werror "$<$<CONFIG:RELEASE>:-Ofast>")
