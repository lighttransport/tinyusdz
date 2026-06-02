# tusdview - Vulkan backend wiring. Included from examples/tusdview/CMakeLists.txt
# when find_package(Vulkan) succeeds. EXAMPLE_TARGET / COMMON_DIR are in scope.

message(STATUS "tusdview: Vulkan backend ENABLED (${Vulkan_LIBRARIES})")

# --- SPIR-V generation -----------------------------------------------------
set(TUSDVIEW_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/tusdview_shaders)
file(MAKE_DIRECTORY ${TUSDVIEW_SHADER_GEN_DIR})

# Prefer the compiler discovered by FindVulkan; otherwise search PATH + known prefix.
if(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
  set(GLSLANG ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
else()
  find_program(GLSLANG glslangValidator HINTS $ENV{HOME}/local/bin /usr/bin /usr/local/bin)
endif()

if(NOT GLSLANG)
  message(FATAL_ERROR
    "tusdview: Vulkan found but glslangValidator not found. Install it or "
    "configure with -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON for a GL-only build.")
endif()

set(_shader_srcs mesh.vert mesh.frag)
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
add_custom_target(tusdview_shaders DEPENDS ${_spv_headers})

# --- Backend sources + link ------------------------------------------------
target_sources(${EXAMPLE_TARGET} PRIVATE
    vk/vk_renderer.cc
    ${COMMON_DIR}/imgui/imgui_impl_vulkan.cpp)
target_compile_definitions(${EXAMPLE_TARGET} PRIVATE HAVE_VULKAN=1)
target_include_directories(${EXAMPLE_TARGET} PRIVATE ${TUSDVIEW_SHADER_GEN_DIR})
target_link_libraries(${EXAMPLE_TARGET} PRIVATE Vulkan::Vulkan)
add_dependencies(${EXAMPLE_TARGET} tusdview_shaders)
