cmake_minimum_required(VERSION 3.16)

# Tool: tusdrender
# CPU preview raytrace renderer for USD scenes using Tydra + vendored LightRT.

include(CheckCCompilerFlag)

# tusdrender pulls several large static archives into one executable. GNU
# ld.bfd spends most of a RelWithDebInfo relink rescanning symbols and merging
# DWARF; prefer an installed parallel linker for this target without changing
# the linker used by the rest of TinyUSDZ. Set this OFF when diagnosing a
# platform linker issue or when a toolchain already selects its own linker.
option(TUSDRENDER_USE_FAST_LINKER
       "Use mold, LLD, or gold for tusdrender when available" ON)

# Build the shared GPU RT implementation even in tools-only/headless
# configurations. When tusdview was configured first this target already
# exists; otherwise define the identical GUI-free source set here.
if(NOT TARGET tusd_gpu_rt_common)
  set(TUSD_GPU_RT_VIEW_DIR ${PROJECT_SOURCE_DIR}/examples/tusdview)
  set(TUSD_GPU_RT_COMMON_DIR ${PROJECT_SOURCE_DIR}/examples/common)
  set(TUSD_GPU_RT_MTLX_DIR ${PROJECT_SOURCE_DIR}/src/external/lightrt/mtlxrender)

  if(NOT TARGET tusdview_lightrt_mtlx_const)
    add_library(tusdview_lightrt_mtlx_const OBJECT
      ${TUSD_GPU_RT_MTLX_DIR}/color.c
      ${TUSD_GPU_RT_MTLX_DIR}/mtlx_doc.c
      ${TUSD_GPU_RT_MTLX_DIR}/mtlx_eval.c
      ${TUSD_GPU_RT_MTLX_DIR}/mtlx_xml.c
      ${TUSD_GPU_RT_MTLX_DIR}/texture.c
      ${TUSD_GPU_RT_VIEW_DIR}/lightrt_mtlx_image_impl.c)
    target_include_directories(tusdview_lightrt_mtlx_const PRIVATE
      ${TUSD_GPU_RT_MTLX_DIR} ${PROJECT_SOURCE_DIR}/src/external)
    target_compile_features(tusdview_lightrt_mtlx_const PRIVATE c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(tusdview_lightrt_mtlx_const PRIVATE -w)
    endif()
  endif()

  set(TUSD_GPU_RT_KERNEL_DIR ${CMAKE_CURRENT_BINARY_DIR}/shared_gpu_generated)
  file(MAKE_DIRECTORY ${TUSD_GPU_RT_KERNEL_DIR})
  if(NOT MSVC)
    set(TUSD_GPU_RT_KERNEL_TXT ${TUSD_GPU_RT_VIEW_DIR}/raytracer_kernel_src.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${TUSD_GPU_RT_KERNEL_TXT})
    file(READ ${TUSD_GPU_RT_KERNEL_TXT} TUSD_GPU_RT_KERNEL_CONTENT)
    file(WRITE ${TUSD_GPU_RT_KERNEL_DIR}/raytracer_kernel.inc
      "const char* kKernelSrc = R\"CUDA(${TUSD_GPU_RT_KERNEL_CONTENT})CUDA\";\n")
  endif()

  set(TUSD_GPU_RT_SOURCES
    ${TUSD_GPU_RT_VIEW_DIR}/lighting_ies.cc
    ${PROJECT_SOURCE_DIR}/src/ptx-loader.cc
    ${TUSD_GPU_RT_VIEW_DIR}/ptex_atlas.cc
    ${TUSD_GPU_RT_VIEW_DIR}/texture_tools.cc
    ${TUSD_GPU_RT_VIEW_DIR}/lightrt_mtlx_bridge.cc
    ${TUSD_GPU_RT_VIEW_DIR}/lightrt_mtlx_graph_compile.cc
    ${TUSD_GPU_RT_VIEW_DIR}/lightrt_mtlx_pack.cc
    ${TUSD_GPU_RT_VIEW_DIR}/displacement_bake.cc
    ${TUSD_GPU_RT_VIEW_DIR}/rt_bvh.cc
    ${TUSD_GPU_RT_VIEW_DIR}/rt_scene_build.cc
    ${TUSD_GPU_RT_VIEW_DIR}/cuda/cuda_raytracer.cc
    ${TUSD_GPU_RT_VIEW_DIR}/hip/hip_raytracer.cc
    ${TUSD_GPU_RT_COMMON_DIR}/light3d/math.cpp
    ${TUSD_GPU_RT_COMMON_DIR}/cuew/cuew.c
    ${PROJECT_SOURCE_DIR}/src/external/hipew/hipew.c)
  if(MSVC)
    enable_language(RC)
    list(APPEND TUSD_GPU_RT_SOURCES
      ${TUSD_GPU_RT_VIEW_DIR}/kernel_resource.cc
      ${TUSD_GPU_RT_VIEW_DIR}/raytracer_kernel_resource.rc)
  endif()
  add_library(tusd_gpu_rt_common STATIC
    ${TUSD_GPU_RT_SOURCES}
    $<TARGET_OBJECTS:tusdview_lightrt_mtlx_const>)
  target_compile_features(tusd_gpu_rt_common PUBLIC cxx_std_17)
  target_include_directories(tusd_gpu_rt_common PUBLIC
    ${TUSD_GPU_RT_VIEW_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/external
    ${TUSD_GPU_RT_COMMON_DIR}
    ${TUSD_GPU_RT_COMMON_DIR}/cuew
    ${PROJECT_SOURCE_DIR}/src/external/hipew)
  if(NOT MSVC)
    target_include_directories(tusd_gpu_rt_common PRIVATE
      ${TUSD_GPU_RT_KERNEL_DIR})
  endif()
  target_link_libraries(tusd_gpu_rt_common PUBLIC
    ${TINYUSDZ_TARGET_STATIC} ${CMAKE_DL_LIBS})
  if(TINYUSDZ_WITH_TEXTOOLS)
    target_link_libraries(tusd_gpu_rt_common PUBLIC tinyusdz_textools)
    target_compile_definitions(tusd_gpu_rt_common PRIVATE TUSDVIEW_WITH_TEXTOOLS=1)
    if(TINYUSDZ_WITH_ZSTD_COMPRESSION)
      target_compile_definitions(tusd_gpu_rt_common PRIVATE
        TINYUSDZ_WITH_ZSTD_COMPRESSION)
    endif()
  endif()
endif()

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
  tusdr_cuda.cc
)

if(TUSDRENDER_USE_FAST_LINKER AND UNIX AND NOT APPLE AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   NOT CMAKE_EXE_LINKER_FLAGS MATCHES "(^| )-fuse-ld=")
  set(_tusdrender_fast_linker "")
  find_program(TUSDRENDER_MOLD_EXECUTABLE NAMES mold)
  find_program(TUSDRENDER_LLD_EXECUTABLE
               NAMES ld.lld lld ld.lld-21 lld-21 ld.lld-22 lld-22
                     ld.lld-20 lld-20 ld.lld-19 lld-19 ld.lld-18 lld-18
                     ld.lld-17 lld-17)
  find_program(TUSDRENDER_GOLD_EXECUTABLE NAMES ld.gold gold)
  if(TUSDRENDER_MOLD_EXECUTABLE)
    set(_tusdrender_fast_linker mold)
  elseif(TUSDRENDER_LLD_EXECUTABLE)
    set(_tusdrender_fast_linker lld)
  elseif(TUSDRENDER_GOLD_EXECUTABLE)
    set(_tusdrender_fast_linker gold)
  endif()
  if(_tusdrender_fast_linker)
    if(_tusdrender_fast_linker STREQUAL "lld")
      # Versioned distro installs may intentionally lack a working unversioned
      # ld.lld alternative. GCC's -fuse-ld=lld honors -B, so point it at the
      # real LLVM bin directory selected above.
      get_filename_component(_tusdrender_lld_real
                             "${TUSDRENDER_LLD_EXECUTABLE}" REALPATH)
      get_filename_component(_tusdrender_lld_dir
                             "${_tusdrender_lld_real}" DIRECTORY)
      target_link_options(tusdrender PRIVATE "-B${_tusdrender_lld_dir}")
    endif()
    target_link_options(tusdrender PRIVATE
                        "-fuse-ld=${_tusdrender_fast_linker}")
    message(STATUS
            "tusdrender: using ${_tusdrender_fast_linker} for faster links")
  endif()
  mark_as_advanced(TUSDRENDER_MOLD_EXECUTABLE TUSDRENDER_LLD_EXECUTABLE
                   TUSDRENDER_GOLD_EXECUTABLE)
  unset(_tusdrender_fast_linker)
  unset(_tusdrender_lld_real)
  unset(_tusdrender_lld_dir)
endif()

target_link_libraries(tusdrender
  ${TINYUSDZ_TARGET_STATIC}
  lightrt_c
)

if(COMMAND add_sanitizers)
  add_sanitizers(tusdrender)
endif()

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
  tusdr_cuda.cc
)
set_source_files_properties(${TUSDRENDER_CXX_SOURCES}
    PROPERTIES COMPILE_FLAGS "${TUSDRENDER_CXX_FLAGS}")

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

# The viewer defines this GUI-free target before pulling tusdrender in for its
# CPU LightRT dependency. Reuse its CUDA/NVRTC renderer and material ABI here.
if(TARGET tusd_gpu_rt_common)
  target_link_libraries(tusdrender tusd_gpu_rt_common)
  target_compile_definitions(tusdrender PRIVATE HAVE_CUDA_RT=1)
  target_include_directories(tusdrender PRIVATE
      ${PROJECT_SOURCE_DIR}/examples/tusdview
      ${PROJECT_SOURCE_DIR}/examples/common)
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
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(
    ${TUSD_GPU_RT_VIEW_DIR}/lightrt_mtlx_graph_compile.cc PROPERTIES
    COMPILE_OPTIONS "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O1>")
endif()
