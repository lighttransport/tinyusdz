# Assume this project is a root project
cmake_minimum_required(VERSION 3.5.1)

project(${BUILD_TARGET} CXX)

set(BUILD_TARGET "usdz_view")

# imgui_sdl requires C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#find_package(OpenGL REQUIRED)

# Disable OpenGL
set(VIDEO_OPENGL OFF CACHE INTERNAL "" FORCE)
set(VIDEO_OPENGLES OFF CACHE INTERNAL "" FORCE)
set(VIDEO_WAYLAND OFF CACHE INTERNAL "" FORCE)
add_subdirectory(../common/SDL2-2.0.12/ SDL2)

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
    ../common/imgui/imgui_widgets.cpp

    ../common/imgui_sdl/imgui_sdl.cpp
    )

add_executable(${BUILD_TARGET} ${SOURCES} ${GUI_SOURCES})

if (UNIX)
  target_include_directories(${BUILD_TARGET} PUBLIC ${X11_INCLUDE_DIR})
endif()

target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common")
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/imgui")

# tinyusdz dir
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../../src/")

target_link_libraries(
    ${BUILD_TARGET}
    SDL2
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
