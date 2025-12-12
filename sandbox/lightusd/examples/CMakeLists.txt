# SPDX-License-Identifier: Apache-2.0
# LightUSD examples

add_executable(example_basic
    basic.cc
)

target_link_libraries(example_basic PRIVATE lightusd)
