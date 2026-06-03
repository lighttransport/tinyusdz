cmake_minimum_required(VERSION 3.16)

# Tool: tusdzconvert
# Convert USD(A/C/Z) to an ARKit-friendly USDZ with texture resize/repack,
# composition flatten, and fast PNG encoding (fpnge).
add_executable(tusdzconvert
  tusdzconvert.cc
)

target_link_libraries(tusdzconvert
  ${TINYUSDZ_TARGET_STATIC}
)

target_include_directories(tusdzconvert PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}
)
