cmake_minimum_required(VERSION 3.16)

# Tool: next_usdzconvert
# USD -> USDZ where the FLATTEN is done by the freestanding next-core + next_io
# stack (mmap file I/O, lazy arrays, parallel compose), then handed to the shared
# texture resize/encode + zip packager (usdz::Convert). Requires the next-core
# gate (TINYUSDZ_WITH_NEXT_CORE) so pipeline::FlattenUSDFileToUSDC is linkable.
add_executable(next_usdzconvert
  next_usdzconvert.cc
)

target_link_libraries(next_usdzconvert
  ${TINYUSDZ_TARGET_STATIC}
  next_core
)

target_include_directories(next_usdzconvert PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/next
  ${PROJECT_SOURCE_DIR}
)
