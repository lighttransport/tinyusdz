cmake_minimum_required(VERSION 3.16)

# Tool: tusdrender
# CPU preview raytrace renderer for USD scenes using Tydra + vendored LightRT.

include(CheckCCompilerFlag)

add_library(lightrt_c STATIC
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c.h
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_tri.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_tri.h
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/mtlxrender/bsdf.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/mtlxrender/bsdf.h
)

set_target_properties(lightrt_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

# Vendored external C code: suppress all warnings.
if(NOT MSVC)
  target_compile_options(lightrt_c PRIVATE -w)
endif()

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
  tusdr_stream.cc
  tusdr_next.cc
  tusdr_lod.cc
  tusdr_rt_lod.cc
  tusdr_legacy.cc
  tusdr_gpu_common.cc
  tusdr_vulkan.cc
  tusdr_d3d.cc
  tusdr_hip.cc
)

target_link_libraries(tusdrender
  ${TINYUSDZ_TARGET_STATIC}
  lightrt_c
)

# Vendored texture tools: the opt-in `-ibl envmap` DomeLight IBL backend.
if(TINYUSDZ_WITH_TEXTOOLS)
  target_link_libraries(tusdrender tinyusdz_textools)
  target_compile_definitions(tusdrender PRIVATE TUSDR_WITH_TEXTOOLS)
endif()

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
# tydra_next's lazy Ptex decoder uses symbols supplied by the main static
# library.  Keep that archive after tydra_next so single-pass static linkers
# resolve the dependency (the initial common-library link above precedes it).
target_link_libraries(tusdrender ${TINYUSDZ_TARGET_STATIC})

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

# Warning flags (same policy as the core library)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(TUSDRENDER_CXX_FLAGS
      "-Weverything -Wno-poison-system-directories -Wno-padded -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-documentation -Wno-documentation-unknown-command -Wno-unused-member-function -Wno-switch-enum -Wno-exit-time-destructors -Wno-sign-conversion -Wno-float-equal -Wno-old-style-cast -Wno-double-promotion -Wno-missing-prototypes -Wno-covered-switch-default -Wno-zero-as-null-pointer-constant -Wno-nrvo -Wno-weak-vtables -Wno-reserved-macro-identifier -Wno-reserved-identifier -Wno-tautological-unsigned-zero-compare -Wno-unknown-warning-option -Wno-undef -Wno-implicit-int-float-conversion -Wno-implicit-float-conversion -Wno-shorten-64-to-32 -Wno-ms-bitfield-padding -Wno-mismatched-tags"
  )
  if(${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 16)
    set(TUSDRENDER_CXX_FLAGS "${TUSDRENDER_CXX_FLAGS} -Wno-unsafe-buffer-usage")
  endif()
  if(${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 17)
    set(TUSDRENDER_CXX_FLAGS "${TUSDRENDER_CXX_FLAGS} -Wno-switch-default")
  endif()
  if(NOT TINYUSDZ_NO_WERROR)
    set(TUSDRENDER_CXX_FLAGS "${TUSDRENDER_CXX_FLAGS} -Werror")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(TUSDRENDER_CXX_FLAGS "-Wall")
  if(NOT TINYUSDZ_NO_WERROR)
    set(TUSDRENDER_CXX_FLAGS "${TUSDRENDER_CXX_FLAGS} -Werror")
  endif()
endif()

# Project-owned C++ sources
set(TUSDRENDER_CXX_SOURCES
  tusdrender.cc
  tusdr_args.cc
  tusdr_material.cc
  tusdr_lighting.cc
  tusdr_geom.cc
  tusdr_integrator.cc
  tusdr_scripting.cc
  tusdr_stream.cc
  tusdr_next.cc
  tusdr_lod.cc
  tusdr_rt_lod.cc
  tusdr_legacy.cc
  tusdr_gpu_common.cc
  tusdr_vulkan.cc
  tusdr_d3d.cc
  tusdr_hip.cc
)
set_source_files_properties(${TUSDRENDER_CXX_SOURCES}
    PROPERTIES COMPILE_FLAGS ${TUSDRENDER_CXX_FLAGS})

# Vendored C third-party code: suppress all warnings.
set_source_files_properties(
  ${PROJECT_SOURCE_DIR}/src/external/civetweb/civetweb.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_vk.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_vkew.c
  ${PROJECT_SOURCE_DIR}/src/external/lightrt/lightrt_c_hip.c
  ${PROJECT_SOURCE_DIR}/src/external/hipew/hipew.c
  PROPERTIES COMPILE_FLAGS "-w")

# When the project is built with QuickJS, enable tusdrender's -js (scripting)
# and -mcp (MCP stdio control) modes for memory-persistent / interactive
# rendering. The library defines TINYUSDZ_WITH_QJS PRIVATEly, so re-declare it
# for the tool. The JS engine + MCP plumbing come from tinyusdz_static.
if(TINYUSDZ_WITH_QJS)
  target_compile_definitions(tusdrender PRIVATE TINYUSDZ_WITH_QJS)
endif()

# --- WebSocket browser streaming server (-streamHttp) -----------------------
# Compile the vendored civetweb (websockets enabled) into tusdrender and enable
# tusdr_stream.cc. Frames are encoded via the shared image writer (JPEG/QOI/PNG).
option(TUSDRENDER_WITH_STREAM "Build the WebSocket browser streaming server" ON)
if(TUSDRENDER_WITH_STREAM)
  target_sources(tusdrender PRIVATE
    ${PROJECT_SOURCE_DIR}/src/external/civetweb/civetweb.c)
  target_include_directories(tusdrender PRIVATE
    ${PROJECT_SOURCE_DIR}/src/external/civetweb
    ${PROJECT_SOURCE_DIR}/src/external/jsonhpp)
  target_compile_definitions(tusdrender PRIVATE
    TUSDRENDER_WITH_STREAM USE_WEBSOCKET=1 NO_SSL NO_CGI)
  # nlohmann/json (used to parse browser input) needs exceptions enabled.
  set_source_files_properties(tusdr_stream.cc PROPERTIES COMPILE_OPTIONS
    "-fexceptions")
  if(Threads_FOUND)
    target_link_libraries(tusdrender Threads::Threads)
  endif()
  target_link_libraries(tusdrender ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(tusdrender ws2_32)
  endif()
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

# --- LightRT HIP/ROCm backend (optional, runtime-loaded via hipew) ----------
# A GPU compute trace path mirroring the Vulkan one. The trace kernel is compiled
# at runtime with hiprtc; libamdhip64 + libhiprtc are opened at runtime via hipew,
# so there is NO link-time ROCm dependency and a build with no ROCm still runs
# (the -hip flag then reports the backend unavailable and falls back).
option(TUSDRENDER_WITH_HIP "Build the HIP/ROCm GPU-interop backend" ON)
if(TUSDRENDER_WITH_HIP AND NOT WIN32)
  set(LIGHTRT_HIP_SRC ${PROJECT_SOURCE_DIR}/src/external/lightrt)
  set(HIPEW_SRC ${PROJECT_SOURCE_DIR}/src/external/hipew)
  target_sources(tusdrender PRIVATE ${LIGHTRT_HIP_SRC}/lightrt_c_hip.c
                                    ${HIPEW_SRC}/hipew.c)
  target_compile_definitions(tusdrender PRIVATE HAVE_HIP=1)
  target_include_directories(tusdrender PRIVATE ${LIGHTRT_HIP_SRC} ${HIPEW_SRC})
  target_link_libraries(tusdrender ${CMAKE_DL_LIBS})
endif()

# --- LightRT Direct3D 11 backend (Windows only) ----------------------------
# A D3D11 compute trace path mirroring the Vulkan one (the trace shader is
# decompiled from the Vulkan SPIR-V via SPIRV-Cross). d3d11 + d3dcompiler +
# dxgi ship with Windows, so no SDK is required. Useful where the AMD amdvlk
# Vulkan path mis-renders but the D3D driver is solid.
option(TUSDRENDER_WITH_D3D11 "Build the Direct3D 11 GPU-interop backend" ON)
if(TUSDRENDER_WITH_D3D11 AND WIN32)
  set(LIGHTRT_D3D_SRC ${PROJECT_SOURCE_DIR}/src/external/lightrt)
  target_sources(tusdrender PRIVATE ${LIGHTRT_D3D_SRC}/lightrt_c_d3d11.cpp)
  target_include_directories(tusdrender PRIVATE ${LIGHTRT_D3D_SRC})
  target_compile_definitions(tusdrender PRIVATE HAVE_D3D11=1)
  target_link_libraries(tusdrender d3d11 dxgi d3dcompiler)
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
