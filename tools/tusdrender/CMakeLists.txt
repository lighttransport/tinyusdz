cmake_minimum_required(VERSION 3.16)

# Tool: tusdrender
# CPU preview raytrace renderer for USD scenes using Tydra + vendored LightRT.

include(CheckCCompilerFlag)

add_library(lightrt_c STATIC
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c.h
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_tri.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_tri.h
)

set_target_properties(lightrt_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

# lightrt's C sources use C11 <stdatomic.h>. MSVC only enables C atomics behind
# /experimental:c11atomics (VS 2022 17.5+); GCC/Clang have them by default.
if(MSVC)
  target_compile_options(lightrt_c PRIVATE /experimental:c11atomics)
endif()

target_include_directories(lightrt_c PUBLIC
  ${PROJECT_SOURCE_DIR}/src/external/lightrt
)

set(LIGHTRT_C_TRI_SIMD_FLAGS "")
if(NOT MSVC)
  check_c_compiler_flag("-msse4.1" TUSDR_LIGHTRT_HAS_SSE41)
  if(TUSDR_LIGHTRT_HAS_SSE41)
    list(APPEND LIGHTRT_C_TRI_SIMD_FLAGS -msse4.1)
  endif()
  check_c_compiler_flag("-mavx2" TUSDR_LIGHTRT_HAS_AVX2)
  if(TUSDR_LIGHTRT_HAS_AVX2)
    list(APPEND LIGHTRT_C_TRI_SIMD_FLAGS -mavx2 -mfma)
  endif()
endif()

if(LIGHTRT_C_TRI_SIMD_FLAGS)
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_tri.c
    PROPERTIES COMPILE_OPTIONS "${LIGHTRT_C_TRI_SIMD_FLAGS}")
endif()

find_package(Threads QUIET)
if(Threads_FOUND)
  target_link_libraries(lightrt_c PUBLIC Threads::Threads)
endif()
if(NOT MSVC)
  target_link_libraries(lightrt_c PUBLIC m)
endif()

add_executable(tusdrender
  tusdrender.cc
  tusdr_args.cc
  tusdr_material.cc
  tusdr_lighting.cc
  tusdr_geom.cc
  tusdr_integrator.cc
  tusdr_scripting.cc
  tusdr_next.cc
  tusdr_legacy.cc
  tusdr_vulkan.cc
)

target_link_libraries(tusdrender
  ${TINYUSDZ_TARGET_STATIC}
  lightrt_c
)

# tusdrender uses the experimental `next` lazy loader (fast, low-memory USDC
# parse) as the default for the RT preview path, plus tydra_next for bit-exact
# world transforms. Build those targets on demand and link them.
# Parallel composition (per-prim opinion fill across cores) needs the next lib's
# std::thread paths; output is byte-identical to the serial fill. Force it on for
# tusdrender's on-demand next build.
set(TINYUSDZ_NEXT_ENABLE_THREAD ON CACHE BOOL "" FORCE)
if(NOT TARGET tinyusdz_next)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/next
                   ${CMAKE_BINARY_DIR}/tusdrender_next_build)
endif()
target_link_libraries(tusdrender tinyusdz_next tydra_next)

# tusdrender itself spawns std::thread workers for the mesh-stream and render
# passes, so link the threading runtime explicitly (not just transitively via
# lightrt_c).
if(Threads_FOUND)
  target_link_libraries(tusdrender Threads::Threads)
endif()

# Windows: GetProcessMemoryInfo (process RSS for the memory budget) lives in psapi.
if(WIN32)
  target_link_libraries(tusdrender psapi)
endif()

target_include_directories(tusdrender PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/external/lightrt
)

# When the project is built with QuickJS, enable tusdrender's -js (scripting)
# and -mcp (MCP stdio control) modes for memory-persistent / interactive
# rendering. The library defines TINYUSDZ_WITH_QJS PRIVATEly, so re-declare it
# for the tool. The JS engine + MCP plumbing come from tinyusdz_static.
if(TINYUSDZ_WITH_QJS)
  target_compile_definitions(tusdrender PRIVATE TINYUSDZ_WITH_QJS)
endif()

# --- LightRT Vulkan backend (optional, runtime-loaded via vkew) -------------
option(TUSDRENDER_WITH_VULKAN "Build the Vulkan GPU-interop backend" ON)
if(TUSDRENDER_WITH_VULKAN)
  set(LIGHTRT_VK_SRC ${PROJECT_SOURCE_DIR}/src/external/lightrt)
  add_library(lightrt_c_vk STATIC
    ${LIGHTRT_VK_SRC}/lightrt_c_vk.c
    ${LIGHTRT_VK_SRC}/lightrt_c_vk.h
    ${LIGHTRT_VK_SRC}/lightrt_vkew.c
    ${LIGHTRT_VK_SRC}/lightrt_vkew.h)
  target_include_directories(lightrt_c_vk PUBLIC ${LIGHTRT_VK_SRC})
  target_compile_definitions(lightrt_c_vk PRIVATE VKEW_IMPLEMENTATION)
  target_link_libraries(lightrt_c_vk ${CMAKE_DL_LIBS})
  set_target_properties(lightrt_c_vk PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

  target_sources(tusdrender PRIVATE ${LIGHTRT_VK_SRC}/lightrt_c_vk.c
                                    ${LIGHTRT_VK_SRC}/lightrt_vkew.c)
  target_compile_definitions(tusdrender PRIVATE HAVE_VULKAN=1)
  target_include_directories(tusdrender PRIVATE ${LIGHTRT_VK_SRC})
endif()

# MinGW (e.g. llvm-mingw): statically link the C++/runtime support libs so the
# .exe is self-contained and needs no libc++.dll / libunwind.dll /
# libwinpthread-1.dll alongside it (mirrors tusdview). Windows system DLLs and
# the UCRT stay dynamic. Opt out with -DTUSDRENDER_STATIC_RUNTIME=OFF.
if(MINGW)
  option(TUSDRENDER_STATIC_RUNTIME
         "Statically link the MinGW C++/unwind/pthread runtime into tusdrender" ON)
  if(TUSDRENDER_STATIC_RUNTIME)
    target_link_options(tusdrender PRIVATE -static)
    message(STATUS "tusdrender: static C++ runtime (-static) for MinGW build")
  endif()
endif()
