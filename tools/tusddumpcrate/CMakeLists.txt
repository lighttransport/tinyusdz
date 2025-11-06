cmake_minimum_required(VERSION 3.16)

# Tool: Structured Crate Dump (YAML/JSON)
# Dumps USDC crate file structure in YAML format for efficient debugging
add_executable(tusddumpcrate
  tusddumpcrate.cc
)

target_link_libraries(tusddumpcrate
  ${TINYUSDZ_TARGET_STATIC}
)

target_include_directories(tusddumpcrate PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}
)
