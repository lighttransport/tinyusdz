cmake_minimum_required(VERSION 3.10)
project(static_any_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_static_any test_static_any.cc)
target_include_directories(test_static_any PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})