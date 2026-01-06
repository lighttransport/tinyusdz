cmake_minimum_required(VERSION 3.10)
project(sorted_hash_grid)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_sorted_hash_grid
    test_sorted_hash_grid.cc
)

add_executable(test_city_distribution
    test_city_distribution.cc
)

target_include_directories(test_sorted_hash_grid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(test_city_distribution PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(test_sorted_hash_grid PRIVATE /W4)
    target_compile_options(test_city_distribution PRIVATE /W4)
else()
    target_compile_options(test_sorted_hash_grid PRIVATE -Wall -Wextra -O2)
    target_compile_options(test_city_distribution PRIVATE -Wall -Wextra -O2)
endif()

enable_testing()
add_test(NAME sorted_hash_grid_test COMMAND test_sorted_hash_grid)
add_test(NAME city_distribution_test COMMAND test_city_distribution)