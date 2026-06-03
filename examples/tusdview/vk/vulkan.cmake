# tusdview - Vulkan backend wiring. Included from examples/tusdview/CMakeLists.txt
# when find_package(Vulkan) succeeds. EXAMPLE_TARGET / COMMON_DIR are in scope.

message(STATUS "tusdview: Vulkan backend ENABLED (${Vulkan_LIBRARIES})")

# --- SPIR-V generation -----------------------------------------------------
set(TUSDVIEW_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/tusdview_shaders)
file(MAKE_DIRECTORY ${TUSDVIEW_SHADER_GEN_DIR})

# glslang selection order:
#   1. explicit override -DTUSDVIEW_GLSLANG=/path
#   2. the locally built one (examples/common/build-glslang.sh -> common/glslang)
#      — preferred for this checkout because it's new enough for GL_EXT_ray_query
#   3. the FindVulkan compiler, then PATH (system default)
set(_local_glslang ${COMMON_DIR}/glslang/bin/glslangValidator)
if(NOT EXISTS ${_local_glslang})
  set(_local_glslang ${COMMON_DIR}/glslang/bin/glslang)  # newer glslang binary name
endif()
if(TUSDVIEW_GLSLANG)
  set(GLSLANG ${TUSDVIEW_GLSLANG})
elseif(EXISTS ${_local_glslang})
  set(GLSLANG ${_local_glslang})
  message(STATUS "tusdview: using locally built glslang: ${GLSLANG}")
elseif(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
  set(GLSLANG ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
else()
  find_program(GLSLANG glslangValidator HINTS $ENV{HOME}/local/bin /usr/bin /usr/local/bin)
endif()

if(NOT GLSLANG)
  message(FATAL_ERROR
    "tusdview: Vulkan found but glslangValidator not found. Install it or "
    "configure with -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON for a GL-only build.")
endif()

set(_shader_srcs mesh.vert mesh.frag line.vert line.frag)
set(_spv_headers "")
foreach(sh ${_shader_srcs})
  string(REPLACE "." "_" sym ${sh})  # mesh_vert / mesh_frag
  set(out ${TUSDVIEW_SHADER_GEN_DIR}/${sym}.spv.h)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${GLSLANG} -V --vn ${sym}_spv
            -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/vk/shaders/${sh}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vk/shaders/${sh}
    COMMENT "tusdview: glslang ${sh} -> ${sym}.spv.h"
    VERBATIM)
  list(APPEND _spv_headers ${out})
endforeach()

# Ray-query compute shader needs SPIR-V 1.4 (ray query + buffer_reference), which
# requires a glslang with GL_EXT_ray_query support (>= ~11 / a Vulkan SDK build).
# Probe the chosen compiler at configure time; only enable the Vulkan RT path when
# it can actually compile the shader. Older glslang (no ray_query) -> RT compiled
# out, VK falls back to rasterization. Override with -DTUSDVIEW_GLSLANG=/path.
set(_rt_out ${TUSDVIEW_SHADER_GEN_DIR}/raytrace_comp.spv.h)
execute_process(
  COMMAND ${GLSLANG} -V --target-env vulkan1.2 --vn raytrace_comp_spv
          -o ${_rt_out} ${CMAKE_CURRENT_SOURCE_DIR}/vk/shaders/raytrace.comp
  RESULT_VARIABLE _rt_probe_rc OUTPUT_QUIET ERROR_QUIET)
if(_rt_probe_rc EQUAL 0)
  set(_have_rt_shader 1)
  add_custom_command(
    OUTPUT ${_rt_out}
    COMMAND ${GLSLANG} -V --target-env vulkan1.2 --vn raytrace_comp_spv
            -o ${_rt_out} ${CMAKE_CURRENT_SOURCE_DIR}/vk/shaders/raytrace.comp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vk/shaders/raytrace.comp
    COMMENT "tusdview: glslang raytrace.comp -> raytrace_comp.spv.h (vulkan1.2)"
    VERBATIM)
  list(APPEND _spv_headers ${_rt_out})
  message(STATUS "tusdview: Vulkan ray query ENABLED (glslang supports GL_EXT_ray_query)")
else()
  set(_have_rt_shader 0)
  message(STATUS "tusdview: Vulkan ray tracing DISABLED — '${GLSLANG}' cannot compile "
    "GL_EXT_ray_query. Install a newer glslang and reconfigure with "
    "-DTUSDVIEW_GLSLANG=/path/to/glslangValidator. VK uses rasterization.")
endif()

add_custom_target(tusdview_shaders DEPENDS ${_spv_headers})

# --- Backend sources + link ------------------------------------------------
target_sources(${EXAMPLE_TARGET} PRIVATE
    vk/vk_renderer.cc
    ${COMMON_DIR}/imgui/imgui_impl_vulkan.cpp)
target_compile_definitions(${EXAMPLE_TARGET} PRIVATE HAVE_VULKAN=1
    TUSDVIEW_HAVE_RT_SHADER=${_have_rt_shader})
target_include_directories(${EXAMPLE_TARGET} PRIVATE ${TUSDVIEW_SHADER_GEN_DIR})
target_link_libraries(${EXAMPLE_TARGET} PRIVATE Vulkan::Vulkan)
add_dependencies(${EXAMPLE_TARGET} tusdview_shaders)
