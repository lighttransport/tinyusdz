# Assume this project is a root project
cmake_minimum_required(VERSION 3.5.1)

set(BUILD_TARGET "usdz_view")

project(${BUILD_TARGET} CXX C)

option(TINYUSDZ_USE_CCACHE "Use ccache for faster recompile." ON)
option(TINYUSDZ_WITH_OPENSUBDIV "Build with OpenSubdiv(osdCPU. if required, set `osd_DIR` to specify the path to your own OpenSubdiv)" ON)
option(USDVIEW_USE_NATIVEFILEDIALOG "Use nativefiledialog. Requires gtk+-3 libs on linux to build" ON)
option(USDVIEW_ENABLE_PHYSICS "Enable Physics(through Bullet3)" OFF)

if (EMSCRIPTEN)
  # Disable nfd
  set(USDVIEW_USE_NATIVEFILEDIALOG OFF CACHE INTERNAL "" FORCE)
endif ()

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

if (USDVIEW_USE_NATIVEFILEDIALOG)
 add_subdirectory(${PROJECT_SOURCE_DIR}/../common/nativefiledialog-extended nfd_build)
endif()

if (USDVIEW_ENABLE_PHYSICS)

  # https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
  function ( ADD_BULLET3_LIB )
	  set(BUILD_CPU_DEMOS OFF CACHE INTERNAL "" FORCE)
	  set(BUILD_UNIT_TESTS OFF CACHE INTERNAL "" FORCE)
	  set(BUILD_EXTRAS OFF CACHE INTERNAL "" FORCE)
	  if (MSVC)
	    # StaticLib conflicts usd_view build itself. so link with dll as a work around.
	    set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE INTERNAL "" FORCE)
	  endif()
    add_subdirectory(../common/bullet3 bullet3_build)
  endfunction ()

  ADD_BULLET3_LIB()
endif()

# [ccache]
if(TINYUSDZ_USE_CCACHE)
  if(MSVC)
    # No ccache support
  else()
    find_program(CCACHE_EXE ccache)
    if(CCACHE_EXE)

      message(STATUS "Use ccache : " ${CCACHE_EXE})
      # CMAKE_C_COMPILER_LAUNCHER = available from cmake 3.4+)
      if(CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}"
                                      "${CCACHE_EXE}")
      else()
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_EXE}")
      endif()

      if(CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}"
                                        "${CCACHE_EXE}")
      else()
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXE}")
      endif()

    endif(CCACHE_EXE)
  endif()
endif()

#find_package(OpenGL REQUIRED)

# https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
function ( ADD_SDL2_LIB )
  # Disable OpenGL
  set(VIDEO_OPENGL OFF CACHE INTERNAL "" FORCE)
  set(VIDEO_OPENGLES OFF CACHE INTERNAL "" FORCE)
  set(VIDEO_WAYLAND OFF CACHE INTERNAL "" FORCE)

  # Enable static lib only
  set(SDL_SAHARED OFF CACHE INTERNAL "" FORCE)
  set(SDL_STATIC ON CACHE INTERNAL "" FORCE)
  add_subdirectory(../common/SDL2-2.0.22/ SDL2)
endfunction ()

if (EMSCRIPTEN)
  # Use emcc ported SDL2
  # https://stackoverflow.com/questions/61590519/how-to-use-emscripten-ports-sdl2-and-freetype-with-cmake

  # Assume `suzanne.usdc` is coped to build directory with PRE_BUILD custom command
  # TODO: Read USD file from JS world
  # TODO: Use max memory size.
  set(USE_FLAGS "-s ASSERTIONS=1 -s USE_SDL=2 -s USE_FREETYPE=1 --embed-file suzanne.usdc")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USE_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
  set(CMAKE_EXECUTABLE_SUFFIX .html)

  set(USDVIEW_SDL2_LIBRARIES ${SDL2_LIBRARIES})
else()
  ADD_SDL2_LIB()
  set(USDVIEW_SDL2_LIBRARIES SDL2-static SDL2main)
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

if (TINYUSDZ_WITH_OPENSUBDIV)

  # add dummy macros
  macro(_add_doxy_headers headers)
  endmacro()
 
  # https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
  function ( ADD_OSD_LIB )

    set(NO_TUTORIALS ON CACHE INTERNAL "" FORCE)
    set(NO_EXAMPLES ON CACHE INTERNAL "" FORCE)
    set(NO_REGRESSION ON CACHE INTERNAL "" FORCE)
    set(NO_DOC ON CACHE INTERNAL "" FORCE)
    set(NO_OMP ON CACHE INTERNAL "" FORCE)
    set(NO_TBB ON CACHE INTERNAL "" FORCE)
    set(NO_CUDA ON CACHE INTERNAL "" FORCE)
    set(NO_OPENCL ON CACHE INTERNAL "" FORCE)
    set(NO_OPENGL ON CACHE INTERNAL "" FORCE)
    set(NO_TESTS ON CACHE INTERNAL "" FORCE)
    set(NO_GLTESTS ON CACHE INTERNAL "" FORCE)
    set(NO_GLFW ON CACHE INTERNAL "" FORCE)
    set(NO_PTEX ON CACHE INTERNAL "" FORCE)

    # Only build libosdCPU.a
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)

    # add_subdirectory(${osd_DIR})

    # CMakeLists of OSD package is problematic,
    # so provide our own one.
    list(APPEND OSD_FAR_SOURCES
      ${osd_DIR}/opensubdiv/far/bilinearPatchBuilder.cpp
      ${osd_DIR}/opensubdiv/far/catmarkPatchBuilder.cpp
      ${osd_DIR}/opensubdiv/far/error.cpp
      ${osd_DIR}/opensubdiv/far/loopPatchBuilder.cpp
      ${osd_DIR}/opensubdiv/far/patchBasis.cpp
      ${osd_DIR}/opensubdiv/far/patchBuilder.cpp
      ${osd_DIR}/opensubdiv/far/patchDescriptor.cpp
      ${osd_DIR}/opensubdiv/far/patchMap.cpp
      ${osd_DIR}/opensubdiv/far/patchTable.cpp
      ${osd_DIR}/opensubdiv/far/patchTableFactory.cpp
      ${osd_DIR}/opensubdiv/far/ptexIndices.cpp
      ${osd_DIR}/opensubdiv/far/stencilTable.cpp
      ${osd_DIR}/opensubdiv/far/stencilTableFactory.cpp
      ${osd_DIR}/opensubdiv/far/stencilBuilder.cpp
      ${osd_DIR}/opensubdiv/far/topologyDescriptor.cpp
      ${osd_DIR}/opensubdiv/far/topologyRefiner.cpp
      ${osd_DIR}/opensubdiv/far/topologyRefinerFactory.cpp
    )

    # CPU only
    list(APPEND OSD_OSD_SOURCES
      ${osd_DIR}/opensubdiv/osd/cpuEvaluator.cpp
      ${osd_DIR}/opensubdiv/osd/cpuKernel.cpp
      ${osd_DIR}/opensubdiv/osd/cpuPatchTable.cpp
      ${osd_DIR}/opensubdiv/osd/cpuVertexBuffer.cpp
    )

    list(APPEND OSD_SDC_SOURCES
      ${osd_DIR}/opensubdiv/sdc/typeTraits.cpp
      ${osd_DIR}/opensubdiv/sdc/crease.cpp
    )

    list(APPEND OSD_VTR_SOURCES
      ${osd_DIR}/opensubdiv/vtr/fvarLevel.cpp
      ${osd_DIR}/opensubdiv/vtr/fvarRefinement.cpp
      ${osd_DIR}/opensubdiv/vtr/level.cpp
      ${osd_DIR}/opensubdiv/vtr/quadRefinement.cpp
      ${osd_DIR}/opensubdiv/vtr/refinement.cpp
      ${osd_DIR}/opensubdiv/vtr/sparseSelector.cpp
      ${osd_DIR}/opensubdiv/vtr/triRefinement.cpp
    )


    add_library(osd_cpu ${OSD_FAR_SOURCES} ${OSD_OSD_SOURCES})
    target_include_directories(osd_cpu PRIVATE ${osd_DIR})

  endfunction ()

  ADD_OSD_LIB()

  list(APPEND EXT_LIBRARIES osd_cpu) 

  list(APPEND SOUECES ${PROJECT_SOURCE_DIR}/src/subdiv.cc)

endif (TINYUSDZ_WITH_OPENSUBDIV)

list(APPEND SOURCES
    main.cc
    simple-render.cc
    gui.cc
    #render.cc
    #render-config.cc

    # tinyusdz
    ../../src/tinyusdz.cc
    ../../src/io-util.cc
    ../../src/prim-types.cc
    ../../src/pprinter.cc
    ../../src/primvar.cc
    ../../src/usda-parser.cc
    ../../src/usda-writer.cc
    ../../src/image-loader.cc
    ../../src/usdObj.cc
    #../../src/integerCoding.cpp
    #../../src/pxrLZ4/lz4.cpp
    #../../src/lz4-compression.cc
)

set(TINYUSDZ_DEP_SOURCES
    ../../src/integerCoding.cpp
    ../../src/lz4-compression.cc
    ../../src/pxrLZ4/lz4.cpp
    ../../src/external/fpng.cpp
    ../../src/external/staticstruct.cc
    ../../src/external/tinyxml2/tinyxml2.cpp
    ../../src/external/ryu/ryu/s2d.c
    ../../src/external/ryu/ryu/s2f.c)


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
    ../common/imnodes/imnodes.cpp
    )

add_executable(${BUILD_TARGET} ${SOURCES} ${GUI_SOURCES} ${TINYUSDZ_DEP_SOURCES})
add_sanitizers(${BUILD_TARGET})
target_compile_options(${BUILD_TARGET} PRIVATE ${EXT_COMPILE_OPTIONS})

if (UNIX)
  target_include_directories(${BUILD_TARGET} PUBLIC ${X11_INCLUDE_DIR})
endif()

target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common")
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/imgui")
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/imnodes")

# tinyusdz dir
target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../../src/")

# ryu
target_include_directories(${BUILD_TARGET}
                             PRIVATE ${PROJECT_SOURCE_DIR}/../../src/external/ryu/)


#target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/SDL2-2.0.12/include"

if (USDVIEW_USE_NATIVEFILEDIALOG)
  target_compile_definitions(${BUILD_TARGET} PRIVATE "USDVIEW_USE_NATIVEFILEDIALOG")
  list(APPEND EXT_LIBRARIES nfd)
endif()

if (USDVIEW_ENABLE_PHYSICS)
  target_compile_definitions(${BUILD_TARGET} PRIVATE "USDVIEW_USE_BULLET3")
  target_include_directories(${BUILD_TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/../common/bullet3/src")
  list(APPEND EXT_LIBRARIES BulletDynamics BulletCollision Bullet3Common LinearMath)
endif()

if(WIN32)
    # nothing.
elseif(APPLE)
    list(APPEND EXT_LIBRARIES ${COCOA})
else()
    list(APPEND EXT_LIBRARIES ${X11_LIBRARIES})
endif()

target_link_libraries(
    ${BUILD_TARGET}
    PRIVATE
    ${USDVIEW_SDL2_LIBRARIES}
    ${EXT_LIBRARIES}
    ${CMAKE_DL_LIBS}
)

if (EMSCRIPTEN)
  add_custom_command(TARGET ${BUILD_TARGET} PRE_BUILD        # Adds a post-build event to MyTest
          COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
              "${PROJECT_SOURCE_DIR}/../../models/suzanne.usdc"      # src
              $<TARGET_FILE_DIR:${BUILD_TARGET}>)           # dest
endif()



source_group("Source Files" FILES ${SOURCES})

# [VisualStudio]
if(WIN32)
  # Set ${LIDIA_BUILD_TARGET} as a startup project for VS IDE
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${BUILD_TARGET})

  # For easier debugging in VS IDE(cmake 3.8.0 or later required) Set working
  # directory to ${LIDIA_BUILD_TARGET} git repo root.
  if(CMAKE_VERSION VERSION_GREATER 3.8.0)
    set_target_properties(${BUILD_TARGET}
                          PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                     "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()

