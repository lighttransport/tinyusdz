cmake_minimum_required(VERSION 3.10)
project(subdivision_surface CXX)

# C++14 required (matching TinyUSDZ requirements)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Security and optimization flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  add_compile_options(-fstack-protector-strong)
  add_compile_options(-D_FORTIFY_SOURCE=2)
endif()

# Library sources
set(SUBDIV_SOURCES
  subdivision.cc
)

set(SUBDIV_HEADERS
  subdivision.hh
  primvar-interpolation.hh
)

# Static library
add_library(subdivision_surface STATIC ${SUBDIV_SOURCES})
target_include_directories(subdivision_surface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Test executable
add_executable(test_subdiv test_subdiv.cc)
target_link_libraries(test_subdiv subdivision_surface)

# Example executables
add_executable(example_simple example_simple.cc)
target_link_libraries(example_simple subdivision_surface)

add_executable(example_bilinear example_bilinear.cc)
target_link_libraries(example_bilinear subdivision_surface)

# Installation
install(TARGETS subdivision_surface
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)

install(FILES ${SUBDIV_HEADERS}
        DESTINATION include/tinyusdz/subdiv)

# Enable testing
enable_testing()
add_test(NAME subdivision_tests COMMAND test_subdiv)

# Print configuration
message(STATUS "Subdivision Surface Library Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
