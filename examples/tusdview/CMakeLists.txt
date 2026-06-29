# tusdview - OpenGL/Vulkan USD viewer example.
# Configured from the top-level tinyusdz CMake, so PROJECT_SOURCE_DIR is the
# tinyusdz repo root.

set(EXAMPLE_TARGET "tusdview")
set(COMMON_DIR ${PROJECT_SOURCE_DIR}/examples/common)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# Bundled GLFW (guard against another example also adding it).
if(NOT TARGET glfw)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${COMMON_DIR}/glfw-3.3.6 ${CMAKE_BINARY_DIR}/_deps/tusdview-glfw EXCLUDE_FROM_ALL)
endif()

# Vulkan backend. Entry points are resolved at runtime via the vendored volk
# meta-loader (examples/common/volk) — we do NOT link the Vulkan SDK loader, so
# no find_package(Vulkan) / Vulkan::Vulkan dependency. Only a build-time glslang
# is needed to compile the GLSL shaders to SPIR-V; if it is missing the Vulkan
# backend is skipped and the app builds GL-only.
#
# find_package(Vulkan) is still probed (QUIET) purely so vulkan.cmake can fall
# back to the SDK's glslangValidator when no local/system one is found.
option(TUSDVIEW_WITH_VULKAN "Build the Vulkan backend (runtime-loaded via volk)" ON)
find_package(Vulkan QUIET)

# Optional native file dialog (needs GTK3 on Linux). Skipped if unavailable.
set(TUSDVIEW_HAVE_NFD OFF)
if(WIN32 OR APPLE)
  set(TUSDVIEW_HAVE_NFD ON)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 QUIET gtk+-3.0)
    if(GTK3_FOUND)
      set(TUSDVIEW_HAVE_NFD ON)
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(TUSDVIEW_SOURCES
    main.cc
    app.cc
    config.cc
    scene_loader.cc
    next_scene_loader.cc
    mesh_build.cc
    gpu_budget_lod.cc
    displacement_bake.cc
    gizmo_build.cc
    parametric_tess.cc
    skinning.cc
    camera_nav.cc
    frame_packet.cc
    gui.cc
    gui_stringify.cc
    gui_style.cc
    gl/gl_util.cc
    gl/gl_renderer.cc
    cuda/cuda_raytracer.cc
    hip/hip_raytracer.cc
    lod_stream.cc)

set(TUSDVIEW_COMMON_SOURCES
    ${COMMON_DIR}/cuew/cuew.c
    ${PROJECT_SOURCE_DIR}/src/external/hipew/hipew.c
    ${COMMON_DIR}/glad/src/glad.c
    ${COMMON_DIR}/imgui/imgui.cpp
    ${COMMON_DIR}/imgui/imgui_draw.cpp
    ${COMMON_DIR}/imgui/imgui_tables.cpp
    ${COMMON_DIR}/imgui/imgui_widgets.cpp
    ${COMMON_DIR}/imgui/imgui_impl_glfw.cpp
    ${COMMON_DIR}/imgui/imgui_impl_opengl3.cpp
    ${COMMON_DIR}/light3d/math.cpp
    ${COMMON_DIR}/light3d/camera.cpp
    ${COMMON_DIR}/light3d/material.cpp
    ${COMMON_DIR}/light3d/texture.cpp)

add_executable(${EXAMPLE_TARGET} ${TUSDVIEW_SOURCES} ${TUSDVIEW_COMMON_SOURCES})

if(COMMAND add_sanitizers)
  add_sanitizers(${EXAMPLE_TARGET})
endif()

target_compile_features(${EXAMPLE_TARGET} PRIVATE cxx_std_17)

# tusdview uses no C++ RTTI and (outside the optional MCP server) no C++
# exceptions; disable both to match the tinyusdz library build (TINYUSDZ_CXX_RTTI
# / _EXCEPTIONS default Off). Scoped to C++ so vendored C sources (glad.c, cuew,
# civetweb.c, ...) don't warn. The MCP server TUs re-enable exceptions below.
if(NOT TINYUSDZ_CXX_RTTI)
  if(MSVC)
    target_compile_options(${EXAMPLE_TARGET}
                           PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
  else()
    target_compile_options(${EXAMPLE_TARGET}
                           PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
  endif()
endif()
if(NOT TINYUSDZ_CXX_EXCEPTIONS)
  if(MSVC)
    target_compile_options(${EXAMPLE_TARGET}
                           PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c->)
  else()
    target_compile_options(${EXAMPLE_TARGET}
                           PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
  endif()
endif()

target_include_directories(${EXAMPLE_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/external
    ${COMMON_DIR}
    ${COMMON_DIR}/cuew
    ${PROJECT_SOURCE_DIR}/src/external/hipew
    ${COMMON_DIR}/glad/include
    ${COMMON_DIR}/imgui)

target_link_libraries(${EXAMPLE_TARGET} PRIVATE
    ${TINYUSDZ_TARGET_STATIC}
    glfw
    OpenGL::GL
    ${CMAKE_DL_LIBS})

# --- `next` lazy loader + tydra-next converter (large-scene preview path) -----
# Mirrors tools/tusdrender/CMakeLists.txt: build the next libs on demand (with
# the std::thread parallel-compose path forced on) and link them. Used by
# next_scene_loader.cc (the --next OpenGL mesh preview).
set(TINYUSDZ_NEXT_ENABLE_THREAD ON CACHE BOOL "" FORCE)
if(NOT TARGET tinyusdz_next)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/next
                   ${CMAKE_BINARY_DIR}/tusdview_next_build)
endif()
if(NOT TARGET tydra_next)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/tydra/next
                   ${CMAKE_BINARY_DIR}/tusdview_tydra_next_build)
endif()
target_link_libraries(${EXAMPLE_TARGET} PRIVATE tinyusdz_next tydra_next)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE ${X11_LIBRARIES})
  find_package(Threads REQUIRED)
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE Threads::Threads)
elseif(APPLE)
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE
      "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# MinGW (e.g. llvm-mingw): statically link the C++/runtime support libs so the
# .exe is self-contained and needs no libc++.dll / libunwind.dll /
# libwinpthread-1.dll alongside it. Only the toolchain runtime is pulled in
# statically; Windows system DLLs (kernel32, user32, ws2_32, …) and the UCRT
# stay dynamic. Opt out with -DTUSDVIEW_STATIC_RUNTIME=OFF.
if(MINGW)
  option(TUSDVIEW_STATIC_RUNTIME
         "Statically link the MinGW C++/unwind/pthread runtime into tusdview" ON)
  if(TUSDVIEW_STATIC_RUNTIME)
    target_link_options(${EXAMPLE_TARGET} PRIVATE -static)
    message(STATUS "tusdview: static C++ runtime (-static) for MinGW build")
  endif()
endif()

# ---------------------------------------------------------------------------
# Vulkan backend (optional) - SPIR-V generation + vk_renderer + imgui_impl_vulkan
# ---------------------------------------------------------------------------
if(TUSDVIEW_WITH_VULKAN)
  include(${CMAKE_CURRENT_SOURCE_DIR}/vk/vulkan.cmake)
endif()

# ---------------------------------------------------------------------------
# Native file dialog (optional)
# ---------------------------------------------------------------------------
if(TUSDVIEW_HAVE_NFD)
  if(NOT TARGET nfd)
    add_subdirectory(${COMMON_DIR}/nativefiledialog-extended
                     ${CMAKE_BINARY_DIR}/_deps/tusdview-nfd EXCLUDE_FROM_ALL)
  endif()
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE nfd)
  target_compile_definitions(${EXAMPLE_TARGET} PRIVATE HAVE_NFD=1)
  message(STATUS "tusdview: native file dialog ENABLED")
else()
  message(STATUS "tusdview: native file dialog disabled (pass a file on CLI)")
endif()

# ---------------------------------------------------------------------------
# Embedded MCP (Model Context Protocol) server: stdio + HTTP transports.
# Dependencies (nlohmann/json + civetweb) are vendored in tinyusdz/src/external,
# so this defaults ON (no system packages needed).
# ---------------------------------------------------------------------------
option(TUSDVIEW_ENABLE_MCP "Build the embedded MCP server (stdio + HTTP)" ON)
if(TUSDVIEW_ENABLE_MCP)
  target_sources(${EXAMPLE_TARGET} PRIVATE
      mcp/mcp_server.cc
      mcp/mcp_http.cc
      mcp/mcp_stdio.cc
      mcp/app_mcp.cc
      ${PROJECT_SOURCE_DIR}/src/external/civetweb/civetweb.c)
  target_compile_definitions(${EXAMPLE_TARGET} PRIVATE
      TUSDVIEW_HAVE_MCP=1
      NO_SSL NO_CGI USE_WEBSOCKET=0)  # civetweb: no OpenSSL / CGI / websockets
  # The MCP server uses try/catch around JSON-RPC handling, so it needs C++
  # exceptions even when the rest of tusdview is built -fno-exceptions. The
  # per-source flag is appended after the target option and therefore wins.
  if(NOT TINYUSDZ_CXX_EXCEPTIONS AND NOT MSVC)
    set_source_files_properties(
        mcp/mcp_server.cc mcp/mcp_http.cc mcp/mcp_stdio.cc mcp/app_mcp.cc
        PROPERTIES COMPILE_OPTIONS "-fexceptions")
  elseif(NOT TINYUSDZ_CXX_EXCEPTIONS AND MSVC)
    set_source_files_properties(
        mcp/mcp_server.cc mcp/mcp_http.cc mcp/mcp_stdio.cc mcp/app_mcp.cc
        PROPERTIES COMPILE_OPTIONS "/EHsc")
  endif()
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE ${CMAKE_DL_LIBS})
  if(UNIX)
    target_link_libraries(${EXAMPLE_TARGET} PRIVATE Threads::Threads)
  elseif(WIN32)
    # civetweb's HTTP transport uses Winsock (socket/bind/listen/WSAStartup).
    target_link_libraries(${EXAMPLE_TARGET} PRIVATE ws2_32)
    # civetweb's clock_gettime() comes from winpthread under MinGW; MSVC has its
    # own threading runtime and no winpthread.lib.
    if(MINGW)
      target_link_libraries(${EXAMPLE_TARGET} PRIVATE winpthread)
    endif()
  endif()
  message(STATUS "tusdview: embedded MCP server ENABLED (stdio + HTTP)")
else()
  message(STATUS "tusdview: embedded MCP server disabled")
endif()

# ---------------------------------------------------------------------------
# Experimental multi-threaded rendering (--threaded): a dedicated render thread owns
# the GL context / Vulkan queue so the UI loop never blocks on the GPU. Covers both
# the GL and Vulkan (raster + ray-query) backends; verified byte-identical to the
# single-threaded path. OFF by default — the single-threaded path is unchanged when
# off. See doc/threading-stage2.md. (Macro name kept GL_THREAD for compat.)
# ---------------------------------------------------------------------------
option(TUSDVIEW_ENABLE_GL_THREAD
       "Build the experimental threaded GL/Vulkan render path (--threaded)" OFF)
if(TUSDVIEW_ENABLE_GL_THREAD)
  target_compile_definitions(${EXAMPLE_TARGET} PRIVATE TUSDVIEW_ENABLE_GL_THREAD=1)
  if(UNIX)
    target_link_libraries(${EXAMPLE_TARGET} PRIVATE Threads::Threads)
  endif()
  message(STATUS "tusdview: experimental threaded GL/Vulkan render path ENABLED (--threaded)")
endif()

set_target_properties(${EXAMPLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
