cmake_minimum_required(VERSION 3.16)

# Tool: lusdzconvert
# Convert USD(A/C/Z) to an ARKit-friendly USDZ with texture resize/repack,
# composition flatten, and fast PNG encoding (fpnge).
add_executable(lusdzconvert
  lusdzconvert.cc
)

target_link_libraries(lusdzconvert
  ${LIGHTUSD_TARGET_STATIC}
)

target_include_directories(lusdzconvert PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}
)
