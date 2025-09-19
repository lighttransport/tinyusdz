cmake_minimum_required(VERSION 3.10)
project(tinysubdiv CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options for SIMD support
option(TINYSUBDIV_USE_SSE "Enable SSE optimizations" OFF)
option(TINYSUBDIV_USE_AVX "Enable AVX optimizations" OFF)
option(TINYSUBDIV_USE_AVX512 "Enable AVX512 optimizations" OFF)
option(TINYSUBDIV_USE_NEON "Enable ARM NEON optimizations" OFF)
option(TINYSUBDIV_BUILD_EXAMPLES "Build example programs" ON)
option(TINYSUBDIV_BUILD_TESTS "Build unit tests" ON)

# Library sources
set(TINYSUBDIV_SOURCES
    tinysubdiv.cc
    tinysubdiv-simd.cc
    usd-subdiv.cc
)

# Create library
add_library(tinysubdiv STATIC ${TINYSUBDIV_SOURCES})

# Set include directories
target_include_directories(tinysubdiv PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Platform detection and SIMD flags
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    # x86/x64 architecture
    include(CheckCXXCompilerFlag)

    if(TINYSUBDIV_USE_SSE)
        check_cxx_compiler_flag("-msse4.1" COMPILER_SUPPORTS_SSE41)
        if(COMPILER_SUPPORTS_SSE41)
            target_compile_options(tinysubdiv PRIVATE -msse4.1)
            target_compile_definitions(tinysubdiv PUBLIC TINYSUBDIV_USE_SSE)
            message(STATUS "SSE 4.1 support enabled")
        endif()
    endif()

    if(TINYSUBDIV_USE_AVX)
        check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
        if(COMPILER_SUPPORTS_AVX2)
            target_compile_options(tinysubdiv PRIVATE -mavx2)
            target_compile_definitions(tinysubdiv PUBLIC TINYSUBDIV_USE_AVX)
            message(STATUS "AVX2 support enabled")
        endif()
    endif()

    if(TINYSUBDIV_USE_AVX512)
        check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512)
        if(COMPILER_SUPPORTS_AVX512)
            target_compile_options(tinysubdiv PRIVATE -mavx512f -mavx512dq)
            target_compile_definitions(tinysubdiv PUBLIC TINYSUBDIV_USE_AVX512)
            message(STATUS "AVX512 support enabled")
        endif()
    endif()

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(arm)|(ARM)|(aarch64)|(AARCH64)")
    # ARM architecture
    if(TINYSUBDIV_USE_NEON)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64)|(AARCH64)")
            # ARM64 has NEON by default
            target_compile_definitions(tinysubdiv PUBLIC TINYSUBDIV_USE_NEON)
            message(STATUS "NEON support enabled (ARM64)")
        else()
            # ARM32 might need explicit flags
            check_cxx_compiler_flag("-mfpu=neon" COMPILER_SUPPORTS_NEON)
            if(COMPILER_SUPPORTS_NEON)
                target_compile_options(tinysubdiv PRIVATE -mfpu=neon)
                target_compile_definitions(tinysubdiv PUBLIC TINYSUBDIV_USE_NEON)
                message(STATUS "NEON support enabled (ARM32)")
            endif()
        endif()
    endif()
endif()

# Build examples
if(TINYSUBDIV_BUILD_EXAMPLES)
    add_executable(tinysubdiv_example example.cc)
    target_link_libraries(tinysubdiv_example PRIVATE tinysubdiv)

    # Enable optimizations for benchmarking
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        target_compile_options(tinysubdiv_example PRIVATE -O3)
    endif()
endif()

# Build tests
if(TINYSUBDIV_BUILD_TESTS)
    add_executable(tinysubdiv_test test.cc)
    target_link_libraries(tinysubdiv_test PRIVATE tinysubdiv)

    add_executable(tinysubdiv_loop_test loop_test.cc)
    target_link_libraries(tinysubdiv_loop_test PRIVATE tinysubdiv)

    add_executable(tinysubdiv_usd_test usd_subdiv_test.cc)
    target_link_libraries(tinysubdiv_usd_test PRIVATE tinysubdiv)
endif()

# Installation
install(TARGETS tinysubdiv
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    tinysubdiv.hh
    chunked-array.hh
    usd-subdiv.hh
    DESTINATION include/tinysubdiv
)

# Print configuration summary
message(STATUS "")
message(STATUS "TinySubdiv Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  SSE Support: ${TINYSUBDIV_USE_SSE}")
message(STATUS "  AVX Support: ${TINYSUBDIV_USE_AVX}")
message(STATUS "  AVX512 Support: ${TINYSUBDIV_USE_AVX512}")
message(STATUS "  NEON Support: ${TINYSUBDIV_USE_NEON}")
message(STATUS "  Build Examples: ${TINYSUBDIV_BUILD_EXAMPLES}")
message(STATUS "  Build Tests: ${TINYSUBDIV_BUILD_TESTS}")
message(STATUS "")