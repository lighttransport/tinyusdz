# tusdview - Vulkan backend wiring. Included from examples/tusdview/CMakeLists.txt
# when TUSDVIEW_WITH_VULKAN is ON. EXAMPLE_TARGET / COMMON_DIR are in scope.
#
# The Vulkan API is resolved at runtime via the vendored volk meta-loader
# (examples/common/volk) — cuew-style, no link against the Vulkan loader and no
# Vulkan SDK required to build.
#
# Shaders are pre-compiled to SPIR-V and committed as C headers under
# vk/shaders/embedded/, so the build needs NO glslang and produces a single,
# self-contained binary. Regenerate those headers with vk/shaders/build-shaders.sh
# whenever a shader changes (that script is the only thing that needs glslang).

# --- Pre-compiled (embedded) SPIR-V ----------------------------------------
set(TUSDVIEW_SHADER_EMBED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vk/shaders/embedded)

# The four raster shader headers are mandatory for the Vulkan backend.
set(_required_spv
    ${TUSDVIEW_SHADER_EMBED_DIR}/mesh_vert.spv.h
    ${TUSDVIEW_SHADER_EMBED_DIR}/mesh_frag.spv.h
    ${TUSDVIEW_SHADER_EMBED_DIR}/line_vert.spv.h
    ${TUSDVIEW_SHADER_EMBED_DIR}/line_frag.spv.h)
set(_missing_spv "")
foreach(h ${_required_spv})
  if(NOT EXISTS ${h})
    list(APPEND _missing_spv ${h})
  endif()
endforeach()
if(_missing_spv)
  message(STATUS
    "tusdview: Vulkan backend SKIPPED — embedded SPIR-V headers missing "
    "(${_missing_spv}). Generate them with vk/shaders/build-shaders.sh, then "
    "reconfigure. Building GL-only. Disable with -DTUSDVIEW_WITH_VULKAN=OFF.")
  return()  # leave the target as a GL-only build
endif()

# The ray-query compute shader is optional: present only when it was generated
# by a glslang new enough for GL_EXT_ray_query. Absent -> RT path compiled out
# (TUSDVIEW_HAVE_RT_SHADER=0) and Vulkan falls back to rasterization.
if(EXISTS ${TUSDVIEW_SHADER_EMBED_DIR}/raytrace_comp.spv.h)
  set(_have_rt_shader 1)
  message(STATUS "tusdview: Vulkan backend ENABLED (runtime-loaded via volk; embedded SPIR-V, ray query ON)")
else()
  set(_have_rt_shader 0)
  message(STATUS "tusdview: Vulkan backend ENABLED (runtime-loaded via volk; embedded SPIR-V, rasterization only — no raytrace_comp.spv.h)")
endif()

# --- Backend sources + link ------------------------------------------------
# volk (meta-loader) + vendored Vulkan headers. VK_NO_PROTOTYPES makes both the
# Vulkan headers and volk declare the vk* entry points as function pointers
# (no link-time symbols); IMGUI_IMPL_VULKAN_USE_VOLK makes imgui_impl_vulkan
# pull in volk.h and dispatch through the same pointers.
set(TUSDVIEW_VULKAN_INCLUDE ${COMMON_DIR}/vulkan-headers/include)
set(TUSDVIEW_VOLK_DIR ${COMMON_DIR}/volk)

target_sources(${EXAMPLE_TARGET} PRIVATE
    vk/vk_renderer.cc
    ${TUSDVIEW_VOLK_DIR}/volk.c
    ${COMMON_DIR}/imgui/imgui_impl_vulkan.cpp)
target_compile_definitions(${EXAMPLE_TARGET} PRIVATE HAVE_VULKAN=1
    VK_NO_PROTOTYPES IMGUI_IMPL_VULKAN_USE_VOLK
    TUSDVIEW_HAVE_RT_SHADER=${_have_rt_shader})
target_include_directories(${EXAMPLE_TARGET} PRIVATE
    ${TUSDVIEW_SHADER_EMBED_DIR}
    ${TUSDVIEW_VULKAN_INCLUDE}
    ${TUSDVIEW_VOLK_DIR})
# volk uses dlopen() to find the loader at runtime on non-Windows platforms.
if(UNIX AND NOT APPLE)
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE ${CMAKE_DL_LIBS})
endif()
