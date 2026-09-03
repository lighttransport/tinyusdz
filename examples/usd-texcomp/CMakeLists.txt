# Assume this cmake is called from lightusd root(../../)
# usd-texcomp: author GPU-compressed (.ktx2) texture companions + customData hints.
# Requires the vendored textools (tir / texcomp / texpipe).
set(EXAMPLE_TARGET "usd-texcomp")

add_executable(${EXAMPLE_TARGET} usd-texcomp-main.cc)
add_sanitizers(${EXAMPLE_TARGET})

target_include_directories(${EXAMPLE_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${EXAMPLE_TARGET} lightusd_static lightusd_textools)

# Zstd-supercompress the written .ktx2 (supercompressionScheme 2). The core lib
# defines this PRIVATEly, so the example needs its own copy; ZSTD_compress itself
# comes from lightusd_static (src/external/zstd.c).
if(LIGHTUSD_WITH_ZSTD_COMPRESSION)
  target_compile_definitions(${EXAMPLE_TARGET} PRIVATE LIGHTUSD_WITH_ZSTD_COMPRESSION)
endif()

set_target_properties(${EXAMPLE_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
