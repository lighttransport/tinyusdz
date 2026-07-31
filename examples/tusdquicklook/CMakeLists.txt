# tusdquicklook minimal portable USD Quick Look viewer.
#
# Deliberately independent of examples/tusdview: no ImGui, no GLFW, no glslang,
# no volk/cuew. The UI is the vendored software-rendered lightui; the default
# renderer is the CPU ray tracer built on the vendored lightrt C API.

set(EXAMPLE_TARGET tusdquicklook)

# Vendored software UI (builds a static `lightui` target; guarded internally
# against double-add).
add_subdirectory(${PROJECT_SOURCE_DIR}/examples/common/lightui
                 ${CMAKE_CURRENT_BINARY_DIR}/lightui)

set(TUSDQUICKLOOK_SOURCES
    main.cc
    app.cc
    ui.cc
    options.cc
    browser.cc
    budget.cc
    ql_scene.cc
    loader.cc
    image_decode.cc
    camera.cc
    render/shade.cc
    render/cpu_rt.cc
    render/gl_raster.cc
    render/gl_context_egl.cc
)

# lightrt's CPU BVH kernel (vendored at src/external/lightrt). The tinyusdz-local
# indexed build keeps the indexed data instead of a vertex soup, which is what
# lets a preview BLAS stay inside the memory budget.
set(TUSDQUICKLOOK_LIGHTRT_DIR ${PROJECT_SOURCE_DIR}/src/external/lightrt)
list(APPEND TUSDQUICKLOOK_SOURCES
    ${TUSDQUICKLOOK_LIGHTRT_DIR}/lightrt_c.c
    ${TUSDQUICKLOOK_LIGHTRT_DIR}/lightrt_c_tri.c
)

add_executable(${EXAMPLE_TARGET} ${TUSDQUICKLOOK_SOURCES})
add_sanitizers(${EXAMPLE_TARGET})

target_include_directories(${EXAMPLE_TARGET} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/examples/common
    ${PROJECT_SOURCE_DIR}/src/external/lightrt
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  # VS2026 toolchain headers guard C11/C++ runtime interop with version checks
  # that are stricter than current compiler support.
  target_compile_options(${EXAMPLE_TARGET} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>
  )
  target_compile_definitions(${EXAMPLE_TARGET} PRIVATE _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()

if(TINYUSDZ_BUILD_QUICKLOOK AND NOT TINYUSDZ_BUILD_QUICKLOOK_WITH_TEXTOOLS)
  # Keep quicklook lightweight by default: avoid building tinyusdz_textools
  # unless explicitly requested.
  if (TINYUSDZ_WITH_TEXTOOLS)
    message(STATUS "tusdquicklook: forcing TINYUSDZ_WITH_TEXTOOLS OFF (pass -DTINYUSDZ_BUILD_QUICKLOOK_WITH_TEXTOOLS=ON to enable)")
  endif()
  set(TINYUSDZ_WITH_TEXTOOLS OFF CACHE BOOL "Build vendored tir/texcomp/texpipe/envmap texture tools" FORCE)
endif()

# The `next` lazy loader + tydra_next streaming converter are the whole reason
# this app can hold a small memory budget. Build them on demand, the same way
# tools/tusdrender does.
set(TINYUSDZ_NEXT_ENABLE_THREAD ON CACHE BOOL "" FORCE)
if(NOT TARGET tinyusdz_next)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/next
                   ${CMAKE_BINARY_DIR}/tusdquicklook_next_build)
endif()

target_link_libraries(${EXAMPLE_TARGET} PRIVATE
    lightui
    ${TINYUSDZ_TOOLS_ALLOCATOR_LIB}
    tinyusdz_next
    tydra_next
)
# tydra_next's lazy image decoding uses symbols from the main static library;
# keep that archive last so single-pass static linkers resolve it.
target_link_libraries(${EXAMPLE_TARGET} PRIVATE tinyusdz_static)

find_package(Threads REQUIRED)
target_link_libraries(${EXAMPLE_TARGET} PRIVATE Threads::Threads)

set_target_properties(${EXAMPLE_TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Headless smoke test. Runs with no display and no GPU (that is the whole point
# of --screenshot), so it is safe in CI.
if(TINYUSDZ_BUILD_TESTS AND UNIX)
  add_test(NAME example-tusdquicklook-smoke
           COMMAND ${CMAKE_COMMAND} -E env bash
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-quicklook-smoke.sh
                   $<TARGET_FILE:${EXAMPLE_TARGET}>
                   ${PROJECT_SOURCE_DIR})
  set_tests_properties(example-tusdquicklook-smoke PROPERTIES TIMEOUT 300)

  # Interactive path under Xvfb. The headless test never touches
  # lui_window_create, the event loop or lui_window_present, so it cannot catch
  # a window-creation hang or a loop that blocks before its first paint. Skips
  # itself when Xvfb / ImageMagick are unavailable.
  add_test(NAME example-tusdquicklook-gui-smoke
           COMMAND ${CMAKE_COMMAND} -E env bash
                   ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-quicklook-gui-smoke.sh
                   $<TARGET_FILE:${EXAMPLE_TARGET}>
                   ${PROJECT_SOURCE_DIR})
  set_tests_properties(example-tusdquicklook-gui-smoke PROPERTIES TIMEOUT 180)
endif()
