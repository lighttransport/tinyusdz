# Benchmark harness: tinyusdz vs OpenUSD (pxr).
#
# Configure example:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release \
#         -Dpxr_DIR=/path/to/OpenUSD/install (or set CMAKE_PREFIX_PATH)
#
# bench_openusd is only built when the pxr package is found.
cmake_minimum_required(VERSION 3.16)
project(tinyusdz-vs-openusd-benchmark CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# tinyusdz does not set the C++ standard when built via add_subdirectory;
# the pcp-2026 branch requires C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- tinyusdz (built from the repository root) -------------------------------
set(TINYUSDZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TINYUSDZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINYUSDZ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${TINYUSDZ_ROOT}" tinyusdz EXCLUDE_FROM_ALL)

add_executable(bench_tinyusdz bench_tinyusdz.cc)
target_include_directories(bench_tinyusdz PRIVATE "${TINYUSDZ_ROOT}/src")
target_link_libraries(bench_tinyusdz PRIVATE tinyusdz_static)
target_compile_features(bench_tinyusdz PRIVATE cxx_std_14)

# --- OpenUSD -----------------------------------------------------------------
find_package(pxr CONFIG QUIET)
if(pxr_FOUND)
  add_executable(bench_openusd bench_openusd.cc)
  target_link_libraries(bench_openusd PRIVATE usd sdf tf)
  target_compile_features(bench_openusd PRIVATE cxx_std_17)
else()
  message(WARNING
    "pxr (OpenUSD) not found - bench_openusd will not be built. "
    "Pass -Dpxr_DIR=<OpenUSD install dir> or set CMAKE_PREFIX_PATH.")
endif()
