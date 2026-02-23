# SPDX-License-Identifier: Apache-2.0
# LightUSD examples

add_executable(example_basic
    basic.cc
)
target_link_libraries(example_basic PRIVATE lightusd)

add_executable(example_async_loader
    async_loader.cc
)
target_link_libraries(example_async_loader PRIVATE lightusd)
