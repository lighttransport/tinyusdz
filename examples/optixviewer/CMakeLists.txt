# Assume this project is a root project
cmake_minimum_required(VERSION 3.5.1)

set(BUILD_TARGET "usdz_optixview")

project(${BUILD_TARGET} CXX)


if (WIN32)
  set(DEFAULT_OPTIX_INSTALL_DIR "C:/ProgramData/NVIDIA Corporation/OptiX SDK 7.2.0/")
else()
  set(DEFAULT_OPTIX_INSTALL_DIR "$HOME/NVIDIA-OptiX-SDK-7.2.0-linux64-x86_64/")
endif()

set(OptiX_INSTALL_DIR ${DEFAULT_OPTIX_INSTALL_DIR} CACHE PATH "Path to OptiX installed location.")

set(CMAKE_CXX_STANDARD 11)

find_package(OpenGL REQUIRED)

if(WIN32)
    # nothing.
elseif(APPLE)
    find_library(COCOA Cocoa REQUIRED)
else()
    find_package(X11 REQUIRED)
endif()

set(SOURCES
    main.cc
    #render.cc
    #render-config.cc

    # tinyusdz
    ../../src/tinyusdz.cc
    ../../src/integerCoding.cpp
    ../../src/pxrLZ4/lz4.cpp
    ../../src/lz4-compression.cc
)

set(GUI_SOURCES
    ../common/trackball.cc
    ../common/matrix.cc
    ../common/imgui/imgui.cpp
    ../common/imgui/imgui_draw.cpp
    ../common/imgui/imgui_tables.cpp
    ../common/imgui/imgui_widgets.cpp
    ../common/imgui/imgui_impl_glfw.cpp
    ../common/imgui/imgui_impl_opengl2.cpp
    ../common/glad/src/glad.c
    ../common/cuew/cuew.c
    )

# local [glfw3]
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "" FORCE)
set(GLFW_INSTALL OFF CACHE INTERNAL "" FORCE)
#option(GLFW_VULKAN_STATIC "Assume the Vulkan loader is linked with the application" OFF)
add_subdirectory("${CMAKE_SOURCE_DIR}/../common/glfw-3.3.2" glfw-3.3.2 EXCLUDE_FROM_ALL)

add_executable(${BUILD_TARGET} ${SOURCES} ${GUI_SOURCES})

# local glad
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/glad/include")


target_include_directories(${BUILD_TARGET} PUBLIC ${OPENGL_INCLUDE_DIR})

if (UNIX)
  target_include_directories(${BUILD_TARGET} PUBLIC ${X11_INCLUDE_DIR})
endif()

target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common")
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/imgui")

# tinyusdz dir
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../../src/")

target_link_libraries(
    ${BUILD_TARGET}
    ${OPENGL_LIBRARIES}
    glfw
    ${CMAKE_DL_LIBS}
)

if(WIN32)
    # nothing.
elseif(APPLE)
    target_link_libraries(
        ${BUILD_TARGET}
        ${COCOA}
    )
else()
    target_link_libraries(
        ${BUILD_TARGET}
        ${X11_LIBRARIES}
    )
endif()

source_group("Source Files" FILES ${SOURCES})
