# SPDX-License-Identifier: Apache-2.0
# LightUSD tests

add_executable(test_lightusd
    test_main.cc
)

target_link_libraries(test_lightusd PRIVATE lightusd)

add_test(NAME test_lightusd COMMAND test_lightusd)

# Test reader with external test files
add_executable(test_reader_files
    test_reader_files.cc
)
target_link_libraries(test_reader_files PRIVATE lightusd)
