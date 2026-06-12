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
    mesh_build.cc
    skinning.cc
    camera_nav.cc
    gui.cc
    gui_stringify.cc
    gui_style.cc
    gl/gl_util.cc
    gl/gl_renderer.cc)

set(TUSDVIEW_COMMON_SOURCES
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

target_include_directories(${EXAMPLE_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/external
    ${COMMON_DIR}
    ${COMMON_DIR}/glad/include
    ${COMMON_DIR}/imgui)

target_link_libraries(${EXAMPLE_TARGET} PRIVATE
    ${TINYUSDZ_TARGET_STATIC}
    glfw
    OpenGL::GL
    ${CMAKE_DL_LIBS})

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
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE ${CMAKE_DL_LIBS})
  if(UNIX)
    target_link_libraries(${EXAMPLE_TARGET} PRIVATE Threads::Threads)
  elseif(WIN32)
    # civetweb's HTTP transport uses Winsock (socket/bind/listen/WSAStartup),
    # and its clock_gettime() comes from winpthread under mingw.
    target_link_libraries(${EXAMPLE_TARGET} PRIVATE ws2_32 winpthread)
  endif()
  message(STATUS "tusdview: embedded MCP server ENABLED (stdio + HTTP)")
else()
  message(STATUS "tusdview: embedded MCP server disabled")
endif()

set_target_properties(${EXAMPLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
