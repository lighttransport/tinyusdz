# Assume this project is a root project
cmake_minimum_required(VERSION 3.5.1)

set(BUILD_TARGET "usdz_view")

if (APPLE)
project(${BUILD_TARGET} CXX C OBJC OBJCXX)
else()
project(${BUILD_TARGET} CXX C)
endif()

set(USDVIEW_SDL2_LOCAL_PATH "${PROJECT_SOURCE_DIR}/../common/SDL2-2.30.6/")
option(USDVIEW_USE_NATIVEFILEDIALOG
       "Use nativefiledialog. Requires gtk+-3 libs on linux to build" ON)
option(USDVIEW_USE_LOCAL_SDL2
       "Build SDL2 from source" ON)
#option(USDVIEW_ENABLE_PHYSICS "Enable Physics(through Bullet3)" OFF)

if(EMSCRIPTEN)
  # Disable nfd
  set(USDVIEW_USE_NATIVEFILEDIALOG
      OFF
      CACHE INTERNAL "" FORCE)
endif()

# Use embedded version of OpenSubdiv code by default
set(osd_DIR ${PROJECT_SOURCE_DIR}/../../src/osd)

# imgui_sdl requires C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

if(USDVIEW_USE_NATIVEFILEDIALOG)
  add_subdirectory(${PROJECT_SOURCE_DIR}/../common/nativefiledialog-extended
                   nfd_build)
endif()

# TODO: Support physics using bullet3
#if(USDVIEW_ENABLE_PHYSICS)
#
#  # https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
#  function(ADD_BULLET3_LIB)
#    set(BUILD_CPU_DEMOS
#        OFF
#        CACHE INTERNAL "" FORCE)
#    set(BUILD_UNIT_TESTS
#        OFF
#        CACHE INTERNAL "" FORCE)
#    set(BUILD_EXTRAS
#        OFF
#        CACHE INTERNAL "" FORCE)
#    if(MSVC)
#      # StaticLib conflicts usd_view build itself. so link with dll as a work around.
#      set(USE_MSVC_RUNTIME_LIBRARY_DLL
#          ON
#          CACHE INTERNAL "" FORCE)
#    endif()
#    add_subdirectory(../common/bullet3 bullet3_build)
#  endfunction()
#
#  add_bullet3_lib()
#endif()

#find_package(OpenGL REQUIRED)

# https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
function(ADD_SDL2_LIB)

  if (NOT IS_DIRECTORY "${USDVIEW_SDL2_LOCAL_PATH}")
    message(FATAL_ERROR "SDL2 folder " ${USDVIEW_SDL2_LOCAL_PATH} " is missing! "
      "You probably did not download and unpack it. See <tinyusdz>/examples/common/README.sdl2 for the procedure.")
  endif()


  # Disable OpenGL
  set(VIDEO_OPENGL
      OFF
      CACHE INTERNAL "" FORCE)
  set(VIDEO_OPENGLES
      OFF
      CACHE INTERNAL "" FORCE)
  set(VIDEO_WAYLAND
      OFF
      CACHE INTERNAL "" FORCE)

  # Enable static lib only
  set(SDL_SAHARED
      OFF
      CACHE INTERNAL "" FORCE)
  set(SDL_STATIC
      ON
      CACHE INTERNAL "" FORCE)
  add_subdirectory(${USDVIEW_SDL2_LOCAL_PATH} SDL2)
endfunction()

if(EMSCRIPTEN)
  # Use emcc ported SDL2
  # https://stackoverflow.com/questions/61590519/how-to-use-emscripten-ports-sdl2-and-freetype-with-cmake

  # Assume `suzanne.usdc` is coped to build directory with PRE_BUILD custom command
  # TODO: Read USD file from JS world
  # TODO: Use max memory size.
  set(USE_COMPILE_FLAGS
      "-sUSE_SDL=2 -sUSE_FREETYPE=1"
  )
  set(USE_LINK_FLAGS
      "--embed-file suzanne.usdc"
  )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_COMPILE_FLAGS}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USE_COMPILE_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} ${USE_LINK_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
  set(CMAKE_EXECUTABLE_SUFFIX .html)

  set(USDVIEW_SDL2_LIBRARIES ${SDL2_LIBRARIES})
else()

  if (USDVIEW_USE_LOCAL_SDL2) 
      ADD_SDL2_LIB()
      # include dir will be automatically added when linking with SDL2-static?
      set(USDVIEW_SDL2_LIBRARIES SDL2-static SDL2main)
  else()
    find_package(SDL2)
    if (SDL2_FOUND)
      message(STATUS "Use system installed SDL2") 
      message(STATUS ${SDL2_LIBRARIES})
      set(USDVIEW_SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
      set(USDVIEW_SDL2_LIBRARIES ${SDL2_LIBRARIES})
    else()
      ADD_SDL2_LIB()
      # include dir will be automatically added when linking with SDL2-static?
      set(USDVIEW_SDL2_LIBRARIES SDL2-static SDL2main)
    endif()
  endif()
endif()

if(WIN32)
  # do nothing.
elseif(APPLE)
  find_library(COCOA Cocoa REQUIRED)
elseif(EMSCRIPTEN)
  # do nothing
else()
  find_package(X11 REQUIRED)
endif()


# [tinyusdz]
add_subdirectory(../../  tinyusdz)

list(
  APPEND
  SOURCES
  main.cc
  simple-render.cc
  gui.cc
)

set(GUI_SOURCES
    ../common/trackball.cc
    ../common/matrix.cc
    ../common/imgui/imgui.cpp
    ../common/imgui/imgui_draw.cpp
    ../common/imgui/imgui_widgets.cpp
    ../common/imgui/imgui_tables.cpp
    ../common/imgui/imgui_impl_sdl.cpp
    ../common/imgui/imgui_impl_sdlrenderer.cpp
    # imnodes
    ../common/imnodes/imnodes.cpp)

add_executable(${BUILD_TARGET} ${SOURCES} ${GUI_SOURCES})
add_sanitizers(${BUILD_TARGET})
target_compile_options(${BUILD_TARGET} PRIVATE ${EXT_COMPILE_OPTIONS})

if(UNIX)
  target_include_directories(${BUILD_TARGET} PUBLIC ${X11_INCLUDE_DIR})
endif()

target_include_directories(${BUILD_TARGET}
                           PRIVATE "${PROJECT_SOURCE_DIR}/../common")
target_include_directories(${BUILD_TARGET}
                           PRIVATE "${PROJECT_SOURCE_DIR}/../common/imgui")
target_include_directories(${BUILD_TARGET}
                           PRIVATE "${PROJECT_SOURCE_DIR}/../common/imnodes")

# tinyusdz dir
target_include_directories(${BUILD_TARGET}
                           PRIVATE "${PROJECT_SOURCE_DIR}/../../src/")

if(USDVIEW_USE_NATIVEFILEDIALOG)
  target_compile_definitions(${BUILD_TARGET}
                             PRIVATE "USDVIEW_USE_NATIVEFILEDIALOG")
  list(APPEND EXT_LIBRARIES nfd)
endif()

#if(USDVIEW_ENABLE_PHYSICS)
#  target_compile_definitions(${BUILD_TARGET} PRIVATE "USDVIEW_USE_BULLET3")
#  target_include_directories(
#    ${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/bullet3/src")
#  list(APPEND EXT_LIBRARIES BulletDynamics BulletCollision Bullet3Common
#       LinearMath)
#endif()

if(WIN32)
  # nothing.
elseif(APPLE)
  list(APPEND EXT_LIBRARIES ${COCOA})
else()
  list(APPEND EXT_LIBRARIES ${X11_LIBRARIES})
endif()

target_include_directories(${BUILD_TARGET} PUBLIC ${USDVIEW_SDL2_INCLUDE_DIRS})
target_link_libraries(${BUILD_TARGET} PRIVATE tinyusdz::tinyusdz_static ${USDVIEW_SDL2_LIBRARIES}
                                              ${EXT_LIBRARIES} PUBLIC ${CMAKE_DL_LIBS})

if(EMSCRIPTEN)
  add_custom_command(
    TARGET ${BUILD_TARGET}
    PRE_BUILD # Adds a post-build event to MyTest
    COMMAND
      ${CMAKE_COMMAND} -E
      copy_if_different # which executes "cmake - E copy_if_different..."
      "${PROJECT_SOURCE_DIR}/../../models/suzanne.usdc" # src
      $<TARGET_FILE_DIR:${BUILD_TARGET}>) # dest
endif()

source_group("Source Files" FILES ${SOURCES})

# [VisualStudio]
if(WIN32)
  # Set ${LIDIA_BUILD_TARGET} as a startup project for VS IDE
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${BUILD_TARGET})

  # For easier debugging in VS IDE(cmake 3.8.0 or later required) Set working
  # directory to ${LIDIA_BUILD_TARGET} git repo root.
  if(CMAKE_VERSION VERSION_GREATER 3.8.0)
    set_target_properties(
      ${BUILD_TARGET} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                 "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
