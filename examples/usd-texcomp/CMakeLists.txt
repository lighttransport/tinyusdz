# Assume this cmake is called from tinyusdz root(../../)
# usd-texcomp: author GPU-compressed (.ktx2) texture companions + customData hints.
# Requires the vendored textools (tir / texcomp / texpipe).
set(EXAMPLE_TARGET "usd-texcomp")

add_executable(${EXAMPLE_TARGET} usd-texcomp-main.cc)
add_sanitizers(${EXAMPLE_TARGET})

target_include_directories(${EXAMPLE_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${EXAMPLE_TARGET} tinyusdz_static tinyusdz_textools)

set_target_properties(${EXAMPLE_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
