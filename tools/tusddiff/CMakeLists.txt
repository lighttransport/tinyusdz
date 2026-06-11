# Tool: tusddiff
# Diff two USD(A/C/Z) files at the Layer / PrimSpec / Attribute level
# (text or JSON output). Files are loaded as Layers (pre-composition).
#
# Assume this cmake is called from tinyusdz root via add_subdirectory().
set(TUSDDIFF_TARGET "tusddiff")

add_executable(${TUSDDIFF_TARGET}
    tusddiff.cc
    )
add_sanitizers(${TUSDDIFF_TARGET})

target_include_directories(${TUSDDIFF_TARGET} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR})
target_link_libraries(${TUSDDIFF_TARGET} ${TINYUSDZ_TARGET_STATIC})

set_target_properties(${TUSDDIFF_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
