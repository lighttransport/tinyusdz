cmake_minimum_required(VERSION 3.5)

set(BUILD_TARGET "tinyusdz") # TODO: drop `z`
set(BUILD_TARGET_C "c-tinyusd")
set(BUILD_TARGET_PY "pytinyusd")
set(BUILD_TARGET_BLENDER_PY "tinyusd_blender")
set(TEST_TARGET "test_tinyusdz")

project(${BUILD_TARGET} C CXX)

# options
option(TINYUSDZ_USE_CCACHE "Use ccache for faster recompile." ON)
option(TINYUSDZ_BUILD_SHARED_LIBS "Build as dll?" ${BUILD_SHARED_LIBS})
option(TINYUSDZ_WITH_C_API "Enable C API." ON)
option(TINYUSDZ_BUILD_TESTS "Build tests" ON)
option(TINYUSDZ_BUILD_EXAMPLES "Build examples(but not all examples area built in `examples` folder)" ON)
option(TINYUSDZ_WITH_JSON "Build with JSON serialization support" OFF)
option(TINYUSDZ_WITH_USDOBJ "Build with usdObj support(import wavefront .obj)" ON)
option(TINYUSDZ_WITH_OPENSUBDIV "Build with OpenSubdiv(osdCPU. if required, set `osd_DIR` to specify the path to your own OpenSubdiv)" ON)
# Use embedded version of OpenSubdiv code by default
set(osd_DIR ${PROJECT_SOURCE_DIR}/src/osd) 

option(TINYUSDZ_WITH_AUDIO "Build with Audio support(WAV and MP3)" ON)

option(TINYUSDZ_WITH_PYTHON "Build with Python binding through pybind11" OFF)

option(TINYUSDZ_WITH_PXR_COMPAT_API "Build with pxr compatible API" ON)

option(TINYUSDZ_WITH_BLENDER_ADDON "Build with Python module for Blender(`TINYUSDZ_WITH_PYTHON` is force set to ON" OFF)

# -- EXR --
option(TINYUSDZ_WITH_EXR "Build with EXR HDR texture support" ON)
option(TINYUSDZ_TINYEXR_USE_SYSTEM_ZLIB "Use system's zlib instead of miniz for TinyEXR" OFF)
# ---------

# -- optional --
option(TINYUSDZ_WITH_USDA_PARSER "Build with USDA parser program" ON)
# --------------

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

if (TINYUSDZ_TINYEXR_USE_SYSTEM_ZLIB) 
  find_package(ZLIB REQUIRED)
endif ()

# Require strict C++14 mode(e.g. `-std=c++14`)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

if (TINYUSDZ_WITH_BLENDER_ADDON)
  set(TINYUSDZ_WITH_PYTHON ON)
endif ()

if (TINYUSDZ_WITH_PYTHON)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/external/pybind11 pybind11_build)
endif ()


set(TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/src/tinyusdz.cc
    ${PROJECT_SOURCE_DIR}/src/usda-writer.cc
    ${PROJECT_SOURCE_DIR}/src/usda-parser.cc
    ${PROJECT_SOURCE_DIR}/src/prim-types.cc
    ${PROJECT_SOURCE_DIR}/src/io-util.cc
    ${PROJECT_SOURCE_DIR}/src/pprinter.cc
    )

if (TINYUSDZ_WITH_PXR_COMPAT_API)
  list(APPEND TINYUSDZ_SOURCES  ${PROJECT_SOURCE_DIR}/src/pxr-compat.cc)
endif ()

set(TINYUSDZ_C_API_SOURCES
    ${PROJECT_SOURCE_DIR}/src/c-tinyusd.cc
    )

set(TINYUSDZ_DEP_SOURCES
    ${PROJECT_SOURCE_DIR}/src/integerCoding.cpp
    ${PROJECT_SOURCE_DIR}/src/lz4-compression.cc
    ${PROJECT_SOURCE_DIR}/src/pxrLZ4/lz4.cpp
    ${PROJECT_SOURCE_DIR}/src/external/ryu/ryu/s2d.c
    ${PROJECT_SOURCE_DIR}/src/external/ryu/ryu/s2f.c
  )

if (TINYUSDZ_WITH_JSON)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/usd-to-json.cc)
endif()

if (TINYUSDZ_WITH_USDOBJ)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/usdObj.cc)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/tiny_obj_loader.cc)
endif()

set(TINYUSDZ_PYTHON_BINDING_SOURCES
    ${PROJECT_SOURCE_DIR}/src/python-bindings.cc
  )

set(TINYUSDZ_BLENDER_PYTHON_BINDING_SOURCES
    ${PROJECT_SOURCE_DIR}/src/blender/bindings.cc
  )

if (TINYUSDZ_WITH_EXR)
  if (TINYUSDZ_TINYEXR_USE_SYSTEM_ZLIB) 
    set_source_files_properties(${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc PROPERTIES COMPILE_DEFINITIONS "TINYEXR_USE_MINIZ=0")
  else ()
    list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/miniz.c)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc PROPERTIES INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/external)
    # TODO: Set this only for clang, gcc
    set_source_files_properties(${PROJECT_SOURCE_DIR}/src/external/miniz.c PROPERTIES COMPILE_DEFINITIONS "_LARGEFILE64_SOURCE=1")
  endif ()

  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc)
endif (TINYUSDZ_WITH_EXR)



if (TINYUSDZ_WITH_OPENSUBDIV)

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
  list(APPEND TINYUSDZ_EXT_LIBRARIES osd_cpu) 

  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/subdiv.cc)

endif (TINYUSDZ_WITH_OPENSUBDIV)

add_library(${BUILD_TARGET} ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES} ${TINYUSDZ_EXT_SOURCES})
add_sanitizers(${BUILD_TARGET})

target_include_directories(${BUILD_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(${BUILD_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src/external/ryu/)

if (TINYUSDZ_WITH_JSON)
  target_include_directories(${BUILD_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src/external/jsonhpp/)
endif ()

if (TINYUSDZ_WITH_USDOBJ)
  target_compile_definitions(${BUILD_TARGET} PRIVATE "TINYUSDZ_USE_USDOBJ")
endif ()

if (TINYUSDZ_WITH_EXR)
  if (TINYUSDZ_TINYEXR_USE_SYSTEM_ZLIB) 
    list(APPEND ${TINYUSDZ_EXT_LIBRARIES} ZLIB::ZLIB)
  endif ()
endif (TINYUSDZ_WITH_EXR)

target_link_libraries(${BUILD_TARGET} ${TINYUSDZ_EXT_LIBRARIES} ${CMAKE_DL_LIBS})

if (WIN32 AND TINYUSDZ_WITH_PXR_COMPAT_API)
  if (TINYUSDZ_BUILD_SHARED_LIBS)
    target_compile_definitions(${BUILD_TARGET} PRIVATE "USD_EXPORTS")
  else ()
    target_compile_definitions(${BUILD_TARGET} PRIVATE "PXR_STATIC")
  endif ()
endif ()

if (IOS)
  target_compile_definitions(${BUILD_TARGET} PRIVATE "TINYUSDZ_BUILD_IOS")
endif ()

if (TINYUSDZ_WITH_EXR)
  target_compile_definitions(${BUILD_TARGET} PRIVATE "TINYUSDZ_SUPPORT_EXR")
endif (TINYUSDZ_WITH_EXR)

if (TINYUSDZ_WITH_AUDIO)
  target_compile_definitions(${BUILD_TARGET} PRIVATE "TINYUSDZ_SUPPORT_AUDIO")
endif (TINYUSDZ_WITH_AUDIO)

if (TINYUSDZ_WITH_OPENSUBDIV)
  target_include_directories(${BUILD_TARGET} PRIVATE ${osd_DIR})
  target_compile_definitions(${BUILD_TARGET} PRIVATE "TINYUSDZ_USE_OPENSUBDIV")
endif (TINYUSDZ_WITH_OPENSUBDIV)

# Increase warning level for clang.
IF (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(${TINYUSDZ_SOURCES} PROPERTIES COMPILE_FLAGS "-Weverything -Werror -Wno-padded -Wno-c++98-compat-pedantic -Wno-documentation -Wno-unused-member-function")
ENDIF ()

if (TINYUSDZ_WITH_C_API)
  add_library(${BUILD_TARGET_C} ${TINYUSDZ_C_API_SOURCES})
  target_link_libraries(${BUILD_TARGET_C} PRIVATE ${BUILD_TARGET})
endif ()

if (TINYUSDZ_WITH_PYTHON AND (NOT TINYUSDZ_WITH_BLENDER_ADDON))
  pybind11_add_module(${BUILD_TARGET_PY} ${TINYUSDZ_PYTHON_BINDING_SOURCES})
  add_sanitizers(${BUILD_TARGET_PY})

  # copy python binding .so file to python/
  add_custom_command(TARGET ${BUILD_TARGET_PY} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${BUILD_TARGET_PY}>" "${CMAKE_SOURCE_DIR}/python/$<TARGET_FILE_NAME:${BUILD_TARGET_PY}>"
    COMMENT "copying python module file to python/" VERBATIM
  )

endif ()

if (TINYUSDZ_WITH_USDA_PARSER)
  add_executable(usda-parser ${CMAKE_CURRENT_SOURCE_DIR}/sandbox/usda/parser-main.cc)
  add_sanitizers(usda-parser)
  target_include_directories(usda-parser PRIVATE ${PROJECT_SOURCE_DIR}/src/)
  target_link_libraries(usda-parser ${BUILD_TARGET})
endif ()

if (TINYUSDZ_WITH_BLENDER_ADDON)
  pybind11_add_module(${BUILD_TARGET_BLENDER_PY} ${TINYUSDZ_BLENDER_PYTHON_BINDING_SOURCES})
  add_sanitizers(${BUILD_TARGET_BLENDER_PY})

  # For Python.h header files.
  # Assume Blender 2.93 LTS(Python 3.9)
  target_include_directories(${BUILD_TARGET_BLENDER_PY} PRIVATE ${PROJECT_SOURCE_DIR}/src/blender/python3.9/Include)

endif ()


if (TINYUSDZ_BUILD_EXAMPLES)
  add_subdirectory(examples/simple_dump)
  add_subdirectory(examples/save_usda)
endif (TINYUSDZ_BUILD_EXAMPLES)

if (TINYUSDZ_BUILD_TESTS)

  enable_testing()

  #
  # Standalone tester
  #
  set(TINYUSDZ_TEST_SOURCES
      ${PROJECT_SOURCE_DIR}/tests/test-main.cc
      )

  add_executable(${TEST_TARGET} ${TINYUSDZ_TEST_SOURCES})
  add_sanitizers(${TEST_TARGET})

  target_include_directories(${TEST_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                    ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(${TEST_TARGET} PRIVATE ${BUILD_TARGET})

  if (TINYUSDZ_WITH_OPENSUBDIV)
    target_compile_definitions(${TEST_TARGET} PRIVATE "TINYUSDZ_USE_OPENSUBDIV")
  endif (TINYUSDZ_WITH_OPENSUBDIV)

  #
  # unit tester
  #
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/unit)


  #
  # USDA parser tester
  #
  if (TINYUSDZ_WITH_USDA_PARSER)
    add_test(
      NAME usda-parser-unit-test
      COMMAND python ${PROJECT_SOURCE_DIR}/tests/usda/unit-runner.py --app "$<TARGET_FILE:usda-parser>" --basedir "${PROJECT_SOURCE_DIR}"
    )
  endif ()

endif (TINYUSDZ_BUILD_TESTS)

# [VisualStudio]
if(WIN32)
  # Set ${BUILD_TARGET} as a startup project for VS IDE
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${BUILD_TARGET})

  # For easier debugging in VS IDE(cmake 3.8.0 or later required) Set working
  # directory to ${BUILD_TARGET} git repo root.
  if(CMAKE_VERSION VERSION_GREATER 3.8.0)
    set_target_properties(${BUILD_TARGET}
                          PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                     "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
