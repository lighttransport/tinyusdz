cmake_minimum_required(VERSION 3.16)

# Tool: Structured Crate Dump (YAML/JSON)
# Dumps USDC crate file structure in YAML format for efficient debugging
add_executable(lusddumpcrate
  lusddumpcrate.cc
)

target_link_libraries(lusddumpcrate
  ${LIGHTUSD_TARGET_STATIC}
)

target_include_directories(lusddumpcrate PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}
)
