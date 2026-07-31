# Assume this cmake is called from tinyusdz root(../../)
set(EXAMPLE_TARGET "tydra_to_renderscene")

set(TINYUSDZ_TYDRA_CONVERT_SOURCES
    to-renderscene-main.cc
    )

add_executable(${EXAMPLE_TARGET} ${TINYUSDZ_TYDRA_CONVERT_SOURCES})
add_sanitizers(${EXAMPLE_TARGET})

target_include_directories(${EXAMPLE_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
# Allocator (if enabled) must precede the rest so it wins symbol resolution.
target_link_libraries(${EXAMPLE_TARGET} PRIVATE ${TINYUSDZ_TOOLS_ALLOCATOR_LIB} tinyusdz_static)

# `tydra/next` converter symbols are only required/available when the next large-
# scene path is built.
if(TINYUSDZ_USE_NEXT_PCP_LARGE_SCENE)
  if(NOT TARGET tinyusdz_next)
    add_subdirectory(${PROJECT_SOURCE_DIR}/src/next ${CMAKE_BINARY_DIR}/tydra_to_renderscene_next_build)
  endif()
  if(NOT TARGET tydra_next)
    add_subdirectory(${PROJECT_SOURCE_DIR}/src/tydra/next ${CMAKE_BINARY_DIR}/tydra_to_renderscene_tydra_next_build)
  endif()
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE tinyusdz_next tydra_next)
endif()

set_target_properties(${EXAMPLE_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
