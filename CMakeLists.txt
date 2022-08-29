cmake_minimum_required(VERSION 3.16)

set(TINYUSDZ_TARGET "tinyusdz") # TODO: drop `z`
set(TINYUSDZ_TARGET_STATIC "tinyusdz_static") # static lib is always built.
set(BUILD_TARGET_C "c-tinyusd")
set(BUILD_TARGET_PY "pytinyusd")
set(BUILD_TARGET_BLENDER_PY "tinyusd_blender")
set(TINYUSDZ_TEST_TARGET "test_tinyusdz")
set(TINYUSDZ_BENCHMARK_TARGET "benchmark_tinyusdz")

project(${TINYUSDZ_TARGET} C CXX)

# options
option(TINYUSDZ_USE_CCACHE "Use ccache for faster recompile." ON)
option(TINYUSDZ_BUILD_SHARED_LIBS "Build as dll?" ${BUILD_SHARED_LIBS})
option(TINYUSDZ_WITH_C_API "Enable C API." ON)
option(TINYUSDZ_BUILD_TESTS "Build tests" ON)
option(TINYUSDZ_BUILD_BENCHMARKS "Build some bechmark tests(e.g. internal C++ data structures)" OFF)
option(TINYUSDZ_BUILD_EXAMPLES
       "Build examples(but not all examples area built in `examples` folder)"
       ON)

option(TINYUSDZ_CXX_EXCEPTIONS "Enable/Disable C++ exceptions(default: Off)"
       OFF)

option(TINYUSDZ_WITH_JSON "Build with JSON serialization support" OFF)
option(TINYUSDZ_WITH_USDOBJ "Build with usdObj support(import wavefront .obj)"
       ON)
option(TINYUSDZ_WITH_USDVOX
       "Build with usdVox support(import MagicaVoxel .vox)" ON)
option(
  TINYUSDZ_WITH_OPENSUBDIV
  "Build with OpenSubdiv(osdCPU. if required, set `osd_DIR` to specify the path to your own OpenSubdiv)"
  ON)
# Use embedded version of OpenSubdiv code by default
set(osd_DIR ${PROJECT_SOURCE_DIR}/src/osd)

option(TINYUSDZ_WITH_AUDIO "Build with Audio support(WAV and MP3)" ON)

option(TINYUSDZ_WITH_PYTHON "Build with Python binding through pybind11" OFF)

option(TINYUSDZ_PREFER_LOCAL_PYTHON_INSTALLATION "Prefer locally-installed Python interpreter than system or conda/brew installed Python. Please specify your Python interpreter with `Python3_EXECUTABLE` cmake option" OFF)

option(TINYUSDZ_WITH_PXR_COMPAT_API "Build with pxr compatible API" ON)

option(
  TINYUSDZ_WITH_BLENDER_ADDON
  "Build with Python module for Blender(`TINYUSDZ_WITH_PYTHON` is force set to ON"
  OFF)

option(TINYUSDZ_USE_SYSTEM_ZLIB
       "Use system's zlib instead of miniz for TinyEXR/TIFF" OFF)

option(TINYUSDZ_PRODUCTION_BUILD
       "Build for production release(e.g. disables debug print, do not include full filepath in error messages)" OFF)

# -- modules --
option(TINYUSDZ_WITH_MODULE_USDA_READER "Build with USDA reader feature" ON)
option(TINYUSDZ_WITH_MODULE_USDA_WRITER "Build with USDA writer feature" ON)

# USDC(Crate binary) reader is mandatory in most situation, so this option is primarily for developer build.
option(TINYUSDZ_WITH_MODULE_USDC_READER "Build with USDC reader feature" ON)
option(TINYUSDZ_WITH_MODULE_USDC_WRITER "Build with USDC writer feature" ON)
# -------------


# -- TIFF --
option(TINYUSDZ_WITH_TIFF "Build with TIFF texture(includes 32bit floating point TIFF) support" OFF)
# ----------

# -- EXR --
option(TINYUSDZ_WITH_EXR "Build with EXR HDR texture support" ON)
# ---------

# -- ColorIO --
option(TINYUSDZ_WITH_COLORIO "Build with Color IO Baked LUT support(through tinycolorio)" ON)
# ---------

# -- optional tool --
option(TINYUSDZ_WITH_TOOL_USDA_PARSER "Build with USDA parser program" ON)
# --------------

# -- For developers --
option(TINYUSDZ_COMPILE_TIME_TRACE "Add -ftime-trace to profile compilation time(clang only)" OFF)
# ---

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

if(TINYUSDZ_WITH_EXR OR TINYUSDZ_WITH_TIFF)
  if(TINYUSDZ_USE_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
  endif()
endif()

if(TINYUSDZ_WITH_PYTHON)
  #set(CMAKE_CXX_STANDARD 17) # nanobind requires C++17

  # for pybind11
  set(CMAKE_CXX_STANDARD 14)
else()
  # Require strict C++14 mode(e.g. `-std=c++14`)
  set(CMAKE_CXX_STANDARD 14)
endif()
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

if(TINYUSDZ_WITH_BLENDER_ADDON)
  set(TINYUSDZ_WITH_PYTHON ON)
endif()

if(TINYUSDZ_WITH_PYTHON)

  # For the time beging, we stick with pybind11, since PyPI manylinux2014(probably mostly used architectrue as of 2022/Aug) does not support C++17.
  # We may switch to nanobind at some point(probably around 2024?)

  # We build monolithic python module, so build libtinyusdz as a static library
  # with PIC enabled.
  set(CMAKE_POSITION_INDEPENDENT_CODE On)

  #[nanobind]
  #set(NB_SHARED
  #    OFF
  #    CACHE INTERNAL "")
  #set(NB_STATIC
  #    ON
  #    CACHE INTERNAL "")

  # workaround: find_package() does not work well inside of nanobind(`Python3::Module` target won't defined), so call it here.
  # and use `FindPython3`, not `FindPython`
  if (TINYUSDZ_PREFER_LOCAL_PYTHON_INSTALLATION)
    #message(STATUS "Local Python") 
    set(Python3_FIND_FRAMEWORK NEVER) # Do not search framework python
    set(Python3_FIND_STRATEGY LOCATION)
    set(Python3_FIND_REGISTRY NEVER) # Windows only
  else ()
    set(Python_FIND_FRAMEWORK LAST) # Prefer Brew/Conda to Apple framework python
  endif ()

  find_package(
    Python3
    COMPONENTS Interpreter Development.Module
    REQUIRED
    )

  #message(STATUS "Python include dirs: " ${Python_INCLUDE_DIRS})
  message(STATUS "Python include dirs: " ${Python3_INCLUDE_DIRS})

  #add_subdirectory(${PROJECT_SOURCE_DIR}/src/external/nanobind nanobind_build)

  add_subdirectory(${PROJECT_SOURCE_DIR}/src/external/pybind11)
endif()

set(TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/src/tinyusdz.cc
    ${PROJECT_SOURCE_DIR}/src/xform.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser.cc
    ${PROJECT_SOURCE_DIR}/src/audio-loader.cc
    ${PROJECT_SOURCE_DIR}/src/usda-reader.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader.cc
    ${PROJECT_SOURCE_DIR}/src/usda-writer.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-writer.cc
    ${PROJECT_SOURCE_DIR}/src/crate-reader.cc
    ${PROJECT_SOURCE_DIR}/src/crate-format.cc
    ${PROJECT_SOURCE_DIR}/src/crate-writer.cc
    ${PROJECT_SOURCE_DIR}/src/crate-pprint.cc
    ${PROJECT_SOURCE_DIR}/src/prim-types.cc
    ${PROJECT_SOURCE_DIR}/src/primvar.cc
    ${PROJECT_SOURCE_DIR}/src/value-pprint.cc
    ${PROJECT_SOURCE_DIR}/src/value-types.cc
    ${PROJECT_SOURCE_DIR}/src/io-util.cc
    ${PROJECT_SOURCE_DIR}/src/image-loader.cc
    ${PROJECT_SOURCE_DIR}/src/image-writer.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom.cc
    ${PROJECT_SOURCE_DIR}/src/usdSkel.cc
    ${PROJECT_SOURCE_DIR}/src/usdShade.cc
    ${PROJECT_SOURCE_DIR}/src/usdLux.cc
    # usdMtlX has less dependency(tinyxml2), so add it to core component
    ${PROJECT_SOURCE_DIR}/src/usdMtlx.cc
    ${PROJECT_SOURCE_DIR}/src/usdObj.cc
    ${PROJECT_SOURCE_DIR}/src/image-loader.cc
    ${PROJECT_SOURCE_DIR}/src/pprinter.cc
    ${PROJECT_SOURCE_DIR}/src/tydra/render-data.cc
    )


if(TINYUSDZ_WITH_PXR_COMPAT_API)
  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/pxr-compat.cc)
endif()

set(TINYUSDZ_C_API_SOURCES ${PROJECT_SOURCE_DIR}/src/c-tinyusd.cc)

set(TINYUSDZ_DEP_SOURCES
    ${PROJECT_SOURCE_DIR}/src/integerCoding.cpp
    ${PROJECT_SOURCE_DIR}/src/lz4-compression.cc
    #${PROJECT_SOURCE_DIR}/src/pxrLZ4/lz4.cpp
    ${PROJECT_SOURCE_DIR}/src/lz4/lz4.c
    ${PROJECT_SOURCE_DIR}/src/external/pystring.cpp
    ${PROJECT_SOURCE_DIR}/src/external/fpng.cpp
    ${PROJECT_SOURCE_DIR}/src/external/staticstruct.cc
    ${PROJECT_SOURCE_DIR}/src/external/tinyxml2/tinyxml2.cpp
    ${PROJECT_SOURCE_DIR}/src/external/string_id/string_id.cpp
    ${PROJECT_SOURCE_DIR}/src/external/string_id/error.cpp
    ${PROJECT_SOURCE_DIR}/src/external/string_id/generator.cpp
    ${PROJECT_SOURCE_DIR}/src/external/string_id/database.cpp
    #${PROJECT_SOURCE_DIR}/src/external/ryu/ryu/s2d.c
    #${PROJECT_SOURCE_DIR}/src/external/ryu/ryu/s2f.c
    )

# Disable SSE for a while, since -mpclmul required to use SSE feature of fpng.
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/external/fpng.cpp
                            PROPERTIES COMPILE_DEFINITIONS "FPNG_NO_SSE=1")

if(TINYUSDZ_WITH_JSON)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/usd-to-json.cc)
endif()

if(TINYUSDZ_WITH_USDOBJ)
  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/tiny_obj_loader.cc)
endif()

if(TINYUSDZ_WITH_USDVOX)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/usdVox.cc)
endif()

set(TINYUSDZ_PYTHON_BINDING_SOURCES
    ${PROJECT_SOURCE_DIR}/src/python-bindings.cc)

set(TINYUSDZ_BLENDER_PYTHON_BINDING_SOURCES
    ${PROJECT_SOURCE_DIR}/src/blender/bindings.cc)

if(TINYUSDZ_WITH_TIFF)
  if(TINYUSDZ_USE_SYSTEM_ZLIB)
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/tiny_dng_loader.cc
      PROPERTIES COMPILE_DEFINITIONS "TINY_DNG_LOADER_USE_SYSTEM_ZLIB=1")
  else()
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/tiny_dng_loader.cc
      PROPERTIES INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/external)
  endif()

  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc)
endif(TINYUSDZ_WITH_TIFF)

if(TINYUSDZ_WITH_EXR)
  if(TINYUSDZ_USE_SYSTEM_ZLIB)
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc
      PROPERTIES COMPILE_DEFINITIONS "TINYEXR_USE_MINIZ=0")
  else()
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc
      PROPERTIES INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/external)
  endif()

  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc)
endif(TINYUSDZ_WITH_EXR)

if(TINYUSDZ_WITH_TIFF OR TINYUSDZ_WITH_EXR)

  if(NOT TINYUSDZ_USE_SYSTEM_ZLIB)
    list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/miniz.c)

    # TODO: Set this only for clang, gcc
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/miniz.c
      PROPERTIES COMPILE_DEFINITIONS "_LARGEFILE64_SOURCE=1")
  endif()

endif()

if(TINYUSDZ_WITH_OPENSUBDIV)

  # https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
  function(ADD_OSD_LIB)

    set(NO_TUTORIALS
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_EXAMPLES
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_REGRESSION
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_DOC
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_OMP
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_TBB
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_CUDA
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_OPENCL
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_OPENGL
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_TESTS
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_GLTESTS
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_GLFW
        ON
        CACHE INTERNAL "" FORCE)
    set(NO_PTEX
        ON
        CACHE INTERNAL "" FORCE)

    # CMakeLists of OSD package is problematic, so provide our own one.
    list(
      APPEND
      OSD_FAR_SOURCES
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
      ${osd_DIR}/opensubdiv/far/topologyRefinerFactory.cpp)

    # CPU only
    list(
      APPEND
      OSD_OSD_SOURCES
      ${osd_DIR}/opensubdiv/osd/cpuEvaluator.cpp
      ${osd_DIR}/opensubdiv/osd/cpuKernel.cpp
      ${osd_DIR}/opensubdiv/osd/cpuPatchTable.cpp
      ${osd_DIR}/opensubdiv/osd/cpuVertexBuffer.cpp)

    list(APPEND OSD_SDC_SOURCES ${osd_DIR}/opensubdiv/sdc/typeTraits.cpp
         ${osd_DIR}/opensubdiv/sdc/crease.cpp)

    list(
      APPEND
      OSD_VTR_SOURCES
      ${osd_DIR}/opensubdiv/vtr/fvarLevel.cpp
      ${osd_DIR}/opensubdiv/vtr/fvarRefinement.cpp
      ${osd_DIR}/opensubdiv/vtr/level.cpp
      ${osd_DIR}/opensubdiv/vtr/quadRefinement.cpp
      ${osd_DIR}/opensubdiv/vtr/refinement.cpp
      ${osd_DIR}/opensubdiv/vtr/sparseSelector.cpp
      ${osd_DIR}/opensubdiv/vtr/triRefinement.cpp)

    add_library(osd_cpu ${OSD_FAR_SOURCES} ${OSD_OSD_SOURCES})
    target_include_directories(osd_cpu PRIVATE ${osd_DIR})

  endfunction()

  add_osd_lib()
  list(APPEND TINYUSDZ_EXT_LIBRARIES osd_cpu)

  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/subdiv.cc)

endif(TINYUSDZ_WITH_OPENSUBDIV)

if(TINYUSDZ_WITH_TIFF OR TINYUSDZ_WITH_EXR)
  if(TINYUSDZ_USE_SYSTEM_ZLIB)
    list(APPEND TINYUSDZ_EXT_LIBRARIES ZLIB::ZLIB)
  endif()
endif()

#
# -- target
#
add_library(${TINYUSDZ_TARGET_STATIC} STATIC
            ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES} ${TINYUSDZ_EXT_SOURCES})
add_sanitizers(${TINYUSDZ_TARGET_STATIC})

if(TINYUSDZ_BUILD_SHARED_LIBS)
  add_library(
    ${TINYUSDZ_TARGET} SHARED ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES}
                              ${TINYUSDZ_EXT_SOURCES})
  add_sanitizers(${TINYUSDZ_TARGET})

  set(TINYUSDZ_LIBS ${TINYUSDZ_TARGET_STATIC} ${TINYUSDZ_TARGET})
else()
  # static only
  set(TINYUSDZ_LIBS ${TINYUSDZ_TARGET_STATIC})
endif()

if(WIN32 AND TINYUSDZ_WITH_PXR_COMPAT_API)
  if(TINYUSDZ_BUILD_SHARED_LIBS)
    target_compile_definitions(${TINYUSDZ_TARGET} PRIVATE "PXR_DYNAMIC")
    target_compile_definitions(${TINYUSDZ_TARGET} PRIVATE "USD_EXPORTS")
  endif()

endif()

#
# -- common target props
#
foreach(TINYUSDZ_LIB_TARGET ${TINYUSDZ_LIBS})

  if(TINYUSDZ_PRODUCTION_BUILD)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_PRODUCTION_BUILD")
  endif()

  # default = enable module, so invert definition
  if (NOT TINYUSDZ_WITH_MODULE_USDA_READER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDA_READER")
  endif()

  if (NOT TINYUSDZ_WITH_MODULE_USDA_WRITER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDA_WRITER")
  endif()

  if (NOT TINYUSDZ_WITH_MODULE_USDC_READER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDC_READER")
  endif()

  if (NOT TINYUSDZ_WITH_MODULE_USDC_WRITER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDC_WRITER")
  endif()


  target_include_directories(${TINYUSDZ_LIB_TARGET}
                             PRIVATE ${PROJECT_SOURCE_DIR}/src)
  #target_include_directories(${TINYUSDZ_LIB_TARGET}
  #                           PRIVATE ${PROJECT_SOURCE_DIR}/src/external/ryu/)
  target_include_directories(${TINYUSDZ_LIB_TARGET}
                             PRIVATE ${PROJECT_SOURCE_DIR}/src/external/fast_float/include)
  target_include_directories(${TINYUSDZ_LIB_TARGET}
                             PRIVATE ${PROJECT_SOURCE_DIR}/src/external/simple_match/include)

  target_include_directories(
    ${TINYUSDZ_LIB_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src/external/jsonhpp/)

  #if(TINYUSDZ_WITH_JSON)
  #  target_include_directories(
  #    ${TINYUSDZ_LIB_TARGET}
  #    PRIVATE ${PROJECT_SOURCE_DIR}/src/external/jsonhpp/)
  #endif()

  if(TINYUSDZ_WITH_USDOBJ)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_USE_USDOBJ")
  endif()

  if(TINYUSDZ_WITH_USDVOX)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_USE_USDVOX")
  endif()

  target_link_libraries(${TINYUSDZ_LIB_TARGET} ${TINYUSDZ_EXT_LIBRARIES}
                        ${CMAKE_DL_LIBS})

  if(IOS)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_BUILD_IOS")
  endif()

  if(TINYUSDZ_WITH_TIFF)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_TIFF")
  endif(TINYUSDZ_WITH_TIFF)

  if(TINYUSDZ_WITH_EXR)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_EXR")
  endif(TINYUSDZ_WITH_EXR)

  if(TINYUSDZ_WITH_COLORIO)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_COLORIO")
  endif(TINYUSDZ_WITH_COLORIO)

  if(TINYUSDZ_WITH_AUDIO)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_AUDIO")
  endif(TINYUSDZ_WITH_AUDIO)

  if(TINYUSDZ_WITH_OPENSUBDIV)
    target_include_directories(${TINYUSDZ_LIB_TARGET} PRIVATE ${osd_DIR})
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_OPENSUBDIV")
  endif(TINYUSDZ_WITH_OPENSUBDIV)

  if(NOT TINYUSDZ_CXX_EXCEPTIONS)
    if(MSVC)
      target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE /EHs-c-)
    else()
      target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE -fno-exceptions)
    endif()
  endif()

endforeach()

# Increase warning level for clang.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")

  set(TUSDZ_COMPILE_FLAGS "-Weverything -Werror -Wno-padded -Wno-c++98-compat-pedantic -Wno-documentation -Wno-unused-member-function")

  if (TINYUSDZ_COMPILE_TIME_TRACE)
    set(TUSDZ_COMPILE_FLAGS "${TUSDZ_COMPILE_FLAGS} -ftime-trace ")
  endif ()

  set_source_files_properties(
    ${TINYUSDZ_SOURCES}
    PROPERTIES
      COMPILE_FLAGS ${TUSDZ_COMPILE_FLAGS}
  )

endif()

if(TINYUSDZ_WITH_C_API)
  # TODO: dll version
  add_library(${BUILD_TARGET_C} STATIC ${TINYUSDZ_C_API_SOURCES})
  target_link_libraries(${BUILD_TARGET_C} PRIVATE ${TINYUSDZ_TARGET_STATIC})
endif()

if(TINYUSDZ_WITH_PYTHON AND (NOT TINYUSDZ_WITH_BLENDER_ADDON))

  # For the time beging, we stick with pybind11, since PyPI manylinux2014(probably mostly used architectrue as of 2022/Aug) does not support C++17.
  # We may switch to nanobind at some point(probably around 2024?)

  # build monolithic .dll
  #nanobind_add_module(${BUILD_TARGET_PY} ${TINYUSDZ_PYTHON_BINDING_SOURCES})

  pybind11_add_module(${BUILD_TARGET_PY} ${TINYUSDZ_PYTHON_BINDING_SOURCES})
  add_sanitizers(${BUILD_TARGET_PY})
  target_link_libraries(${BUILD_TARGET_PY} PRIVATE ${TINYUSDZ_TARGET_STATIC})
  
  # strip "py" prefix
  set_target_properties(${BUILD_TARGET_PY} PROPERTIES OUTPUT_NAME "tinyusdz")

  # copy python binding .so file to python/
  # For developer
  add_custom_command(
    TARGET ${BUILD_TARGET_PY}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${BUILD_TARGET_PY}>"
            "${CMAKE_SOURCE_DIR}/python/$<TARGET_FILE_NAME:${BUILD_TARGET_PY}>"
    COMMENT "copying python module file to python/"
    VERBATIM)

  # For pypi packaging
  install(TARGETS ${BUILD_TARGET_PY} LIBRARY DESTINATION tinyusdz)

endif()

if(TINYUSDZ_WITH_TOOL_USDA_PARSER AND TINYUSDZ_WITH_MODULE_USDA_READER)
  add_executable(usda_parser
                 ${CMAKE_CURRENT_SOURCE_DIR}/examples/usda_parser/parser-main.cc)
  add_sanitizers(usda_parser)
  target_include_directories(usda_parser PRIVATE ${PROJECT_SOURCE_DIR}/src/)
  target_link_libraries(usda_parser PRIVATE ${TINYUSDZ_TARGET_STATIC})
endif()

if(TINYUSDZ_WITH_BLENDER_ADDON)
  pybind11_add_module(${BUILD_TARGET_BLENDER_PY}
                      ${TINYUSDZ_BLENDER_PYTHON_BINDING_SOURCES})
  add_sanitizers(${BUILD_TARGET_BLENDER_PY})

  # For Python.h header files. Assume Blender 2.93 LTS(Python 3.9)
  target_include_directories(
    ${BUILD_TARGET_BLENDER_PY}
    PRIVATE ${PROJECT_SOURCE_DIR}/src/blender/python3.9/Include)

endif()

if(TINYUSDZ_BUILD_EXAMPLES)
  add_subdirectory(examples/simple_usdz_dump)
  if (TINYUSDZ_WITH_MODULE_USDA_WRITER)
    add_subdirectory(examples/save_usda)
  endif()
endif(TINYUSDZ_BUILD_EXAMPLES)

if(TINYUSDZ_BUILD_BENCHMARKS)

  #
  # Standalone benchmark exe.
  #
  set(TINYUSDZ_BENCH_SOURCES ${PROJECT_SOURCE_DIR}/benchmarks/benchmark-main.cc)

  add_executable(${TINYUSDZ_BENCHMARK_TARGET} ${TINYUSDZ_BENCH_SOURCES})
  add_sanitizers(${TINYUSDZ_BENCHMARK_TARGET})

  target_include_directories(${TINYUSDZ_BENCHMARK_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                    ${PROJECT_SOURCE_DIR}/bencharks)
  target_link_libraries(${TINYUSDZ_BENCHMARK_TARGET} PRIVATE ${TINYUSDZ_TARGET_STATIC})

  if(TINYUSDZ_WITH_OPENSUBDIV)
    target_compile_definitions(${TINYUSDZ_BENCHMARK_TARGET} PRIVATE "TINYUSDZ_USE_OPENSUBDIV")
  endif(TINYUSDZ_WITH_OPENSUBDIV)

endif(TINYUSDZ_BUILD_BENCHMARKS)

# [VisualStudio]
if(WIN32)
  # Set ${TINYUSDZ_TARGET_STATIC} as a startup project for VS IDE
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${TINYUSDZ_TARGET_STATIC})

  # For easier debugging in VS IDE(cmake 3.8.0 or later required) Set working
  # directory to ${TINYUSDZ_TARGET_STATIC} git repo root.
  if(CMAKE_VERSION VERSION_GREATER 3.8.0)
    set_target_properties(
      ${TINYUSDZ_TARGET_STATIC} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                           "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()

if(TINYUSDZ_BUILD_TESTS)

  enable_testing()

  #
  # Standalone tester
  #
  set(TINYUSDZ_TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/test-main.cc)

  add_executable(${TINYUSDZ_TEST_TARGET} ${TINYUSDZ_TEST_SOURCES})
  add_sanitizers(${TINYUSDZ_TEST_TARGET})

  target_include_directories(${TINYUSDZ_TEST_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src
                                                    ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(${TINYUSDZ_TEST_TARGET} PRIVATE ${TINYUSDZ_TARGET_STATIC})

  if(TINYUSDZ_WITH_OPENSUBDIV)
    target_compile_definitions(${TINYUSDZ_TEST_TARGET} PRIVATE "TINYUSDZ_USE_OPENSUBDIV")
  endif(TINYUSDZ_WITH_OPENSUBDIV)

  #
  # unit tester
  #
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/unit)

  #
  # USDA parser tester
  #
  if(TINYUSDZ_WITH_TOOL_USDA_PARSER AND TINYUSDZ_WITH_MODULE_USDA_READER)
    add_test(
      NAME usda-parser-unit-test
      COMMAND python ${PROJECT_SOURCE_DIR}/tests/usda/unit-runner.py --app
              "$<TARGET_FILE:usda_parser>" --basedir "${PROJECT_SOURCE_DIR}")
  endif()

endif(TINYUSDZ_BUILD_TESTS)

# [VisualStudio]
if(WIN32)
  # Set ${TINYUSDZ_TARGET_STATIC} as a startup project for VS IDE
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${TINYUSDZ_TARGET_STATIC})

  # For easier debugging in VS IDE(cmake 3.8.0 or later required) Set working
  # directory to ${TINYUSDZ_TARGET_STATIC} git repo root.
  if(CMAKE_VERSION VERSION_GREATER 3.8.0)
    set_target_properties(
      ${TINYUSDZ_TARGET_STATIC} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                           "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
