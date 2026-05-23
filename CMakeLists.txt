cmake_minimum_required(VERSION 3.16)

set(TINYUSDZ_TARGET "tinyusdz") # TODO: drop `z`
set(TINYUSDZ_TARGET_NS "tinyusdz::tinyusdz") # dll lib. may not be built.
set(TINYUSDZ_TARGET_STATIC_NS "tinyusdz::tinyusdz_static") # static lib is always built.
set(TINYUSDZ_TARGET_STATIC "tinyusdz_static") # alias to ${TINYUSDZ_TARGET_STATIC_NS}
set(BUILD_TARGET_C "c-tinyusd")
set(BUILD_TARGET_PY "pytinyusd")
#set(BUILD_TARGET_BLENDER_PY "tinyusd_blender")
set(TINYUSDZ_TEST_TARGET "test_tinyusdz")
set(TINYUSDZ_BENCHMARK_TARGET "benchmark_tinyusdz")

project(${TINYUSDZ_TARGET} C CXX)

# PROJECT_IS_TOP_LEVEL is available from cmake 3.21
if (CMAKE_VERSION VERSION_LESS 3.21)
  if ("${CMAKE_PROJECT_NAME}" STREQUAL "${TINYUSDZ_TARGET}")
    set(PROJECT_IS_TOP_LEVEL ON)
  endif()
endif ()

# Default to Release when configured as the top-level project and no build
# type was given. Without this, single-config generators (Make/Ninja) leave
# CMAKE_BUILD_TYPE empty, which on GCC/Clang means -O0 and silently produces
# binaries that are 2-3x slower than expected (observed: tusdcat parsing
# went from 3.8s -> 1.3s after switching from the empty default to Release).
if (PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "TinyUSDZ: CMAKE_BUILD_TYPE not set; defaulting to Release.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type (Debug|Release|RelWithDebInfo|MinSizeRel)" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
      "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif ()


if (EMSCRIPTEN)
  set(TINYUSDZ_DEFAULT_NO_WERROR ON)
  set(TINYUSDZ_DEFAULT_PRODUCTION_BUILD On)
  set(TINYUSDZ_DEFAULT_WITH_BUILTIN_IMAGE_LOADER On)
  set(TINYUSDZ_DEFAULT_BUILD_TESTS Off)
  set(TINYUSDZ_DEFAULT_EXAMPLES Off)
  set(TINYUSDZ_DEFAULT_WITH_C_API Off)
  set(TINYUSDZ_DEFAULT_WITH_PXR_COMPAT_API Off)
  # TODO: deprecate in next major version
  set(TINYUSDZ_DEFAULT_WITH_USDA_PARSER Off)
  set(TINYUSDZ_DEFAULT_WITH_USDC_PARSER Off)
  set(TINYUSDZ_DEFAULT_WITH_COROUTINE On)
  set(TINYUSDZ_DEFAULT_WITH_MESHOPT On)
elseif(NOT PROJECT_IS_TOP_LEVEL)
  # assume tinyusdz is added from add_subdirectory()
  # disable tools, tests and examples build by default.
  set(TINYUSDZ_DEFAULT_NO_WERROR OFF)
  set(TINYUSDZ_DEFAULT_PRODUCTION_BUILD Off)
  set(TINYUSDZ_DEFAULT_WITH_BUILTIN_IMAGE_LOADER On)
  set(TINYUSDZ_DEFAULT_BUILD_TESTS Off)
  set(TINYUSDZ_DEFAULT_EXAMPLES Off)
  set(TINYUSDZ_DEFAULT_WITH_C_API Off)
  set(TINYUSDZ_DEFAULT_WITH_PXR_COMPAT_API On)
  # TODO: deprecate in next major version
  set(TINYUSDZ_DEFAULT_WITH_USDA_PARSER Off)
  set(TINYUSDZ_DEFAULT_WITH_USDC_PARSER Off)
  set(TINYUSDZ_DEFAULT_WITH_COROUTINE Off)
  set(TINYUSDZ_DEFAULT_WITH_MESHOPT Off)
else()
  set(TINYUSDZ_DEFAULT_NO_WERROR OFF)
  set(TINYUSDZ_DEFAULT_PRODUCTION_BUILD Off)
  set(TINYUSDZ_DEFAULT_WITH_BUILTIN_IMAGE_LOADER On)
  set(TINYUSDZ_DEFAULT_BUILD_TESTS On)
  set(TINYUSDZ_DEFAULT_BUILD_EXAMPLES On)
  set(TINYUSDZ_DEFAULT_WITH_C_API Off)
  set(TINYUSDZ_DEFAULT_WITH_PXR_COMPAT_API On)
  # TODO: deprecate in next major version
  set(TINYUSDZ_DEFAULT_WITH_USDA_PARSER Off)
  set(TINYUSDZ_DEFAULT_WITH_USDC_PARSER Off)
  set(TINYUSDZ_DEFAULT_WITH_COROUTINE Off)
  set(TINYUSDZ_DEFAULT_WITH_MESHOPT Off)

  # For Visual Studio
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif ()

# options
option(TINYUSDZ_USE_CCACHE "Use ccache for faster recompile." ON)
# NOTE: Measured to NOT help (and slightly hurt codegen) on this codebase: the
# compile cost is dominated by template *instantiation* and -O3 codegen, not by
# header parsing, which is all a PCH can cache. Kept as an opt-in knob; default OFF.
option(TINYUSDZ_USE_PCH "Use a precompiled header (does not help this codebase; opt-in)." OFF)
option(TINYUSDZ_BUILD_SHARED_LIBS "Build as dll?" ${BUILD_SHARED_LIBS})
option(TINYUSDZ_ENABLE_THREAD "Build with C++11 std::thread support?(threading support is not implemented yet)" OFF)
option(TINYUSDZ_MSAN "Build with MSAN support(for developer only)" OFF)
option(TINYUSDZ_WITH_C_API "Enable C API." ${TINYUSDZ_DEFAULT_WITH_C_API})
option(TINYUSDZ_BUILD_TESTS "Build tests" ${TINYUSDZ_DEFAULT_BUILD_TESTS})
option(TINYUSDZ_BUILD_BENCHMARKS
       "Build some bechmark tests(e.g. internal C++ data structures)" OFF)
option(TINYUSDZ_BUILD_EXAMPLES
       "Build examples(but not all examples in `examples` folder are being built)"
       ${TINYUSDZ_DEFAULT_BUILD_EXAMPLES})
option(TINYUSDZ_BUILD_TOOLS "Build tools (tusddumpcrate, etc.)" OFF)
option(TINYUSDZ_WITH_BUILTIN_IMAGE_LOADER
       "Build with built-in image loader(stb_image and fpng). When disabled, app need to provide image loader callback to load images."
       ${TINYUSDZ_DEFAULT_WITH_BUILTIN_IMAGE_LOADER})
option(TINYUSDZ_WITH_TYDRA
       "Build with Tydra module(Handly USD scene converter for the renderer, DCC, etc)."
       ON)
option(TINYUSDZ_WITH_MCP_SERVER
       "Build with C++ MCP server(http server) support."
       OFF)
option(TINYUSDZ_WITH_QJS
       "Build with QuickJS(JavaScript) support."
       OFF)

option(TINYUSDZ_WITH_GEOGRAM
       "Build with Geogram library support for advanced geometry processing."
       OFF)

option(TINYUSDZ_WITH_WAMR
       "Build with WAMR (WebAssembly Micro Runtime) support for executing WASM in Tydra."
       OFF)

option(TINYUSDZ_WITH_COROUTINE
       "Build with C++20 coroutine support."
       ${TINYUSDZ_DEFAULT_WITH_COROUTINE})


if(MSVC)
  # it looks it is hard to disable C++ exception for MSVC, so enable exception by default
  set(TINYUSDZ_CXX_EXCEPTIONS_DEFAULT On)
else()
  set(TINYUSDZ_CXX_EXCEPTIONS_DEFAULT Off)
endif()

# Enable /MP flags(multi processor build) for Visual Studio
# Based on VTK's Cmake
# No need to set this if you use Ninja Generator
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # default on.
  # disable MP or reduce cores if you have less memory
  set(TINYUSDZ_CXX_MP_FLAG ON CACHE BOOL "Build with /MP flag enabled")
  set(TINYUSDZ_PROCESSOR_COUNT "$ENV{NUMBER_OF_PROCESSORS}")
  set(TINYUSDZ_CXX_MP_NUM_PROCESSORS ${TINYUSDZ_PROCESSOR_COUNT} CACHE STRING "The maximum number of processes for the /MP flag")


  message(STATUS "Use procesors : " ${TINYUSDZ_PROCESSOR_COUNT})

  # /MP flag is set as target_compile_options, so do not add /MP to global CMAKE_CXX_FLAGS variable.
  #if (CMAKE_CXX_MP_FLAG)
  #  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP${CMAKE_CXX_MP_NUM_PROCESSORS}")
  #  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP${CMAKE_CXX_MP_NUM_PROCESSORS}")
  #endif ()
endif()

option(TINYUSDZ_CXX_EXCEPTIONS
       "Enable/Disable C++ exceptions(default: Off for posix, on for MSVC)"
       ${TINYUSDZ_CXX_EXCEPTIONS_DEFAULT})

option(TINYUSDZ_WITH_USDMTLX "Build with MaterialX support" ON)
option(TINYUSDZ_WITH_JSON "Build with JSON support" ON)
option(TINYUSDZ_WITH_USD_TO_GLTF "Build with USD to glTF example" ON)
option(TINYUSDZ_WITH_USDOBJ "Build with usdObj support(import wavefront .obj)"
       ON)
option(TINYUSDZ_WITH_USDFBX "Build with usdFbx support(import FBX .fbx)"
        OFF)
option(TINYUSDZ_WITH_USDVOX
       "Build with usdVox support(import MagicaVoxel .vox)" ON)

set(TINYUSDZ_DEFAULT_WITH_OPENSUBDIV OFF)
set(TINYUSDZ_DEFAULT_WITH_TINYSUBDIV ON)

option(
  TINYUSDZ_WITH_OPENSUBDIV
  "Build with OpenSubdiv(osdCPU. if required, set `osd_DIR` to specify the path to your own OpenSubdiv)"
  ${TINYUSDZ_DEFAULT_WITH_OPENSUBDIV})
# Use embedded version of OpenSubdiv code by default
set(osd_DIR ${PROJECT_SOURCE_DIR}/src/osd)

option(
  TINYUSDZ_WITH_TINYSUBDIV
  "Build with TinyUSDZ's own dependency-free subdivision surface library (Catmull-Clark, Loop, Bilinear)"
  ${TINYUSDZ_DEFAULT_WITH_TINYSUBDIV})

# Mutual exclusivity check
if(TINYUSDZ_WITH_OPENSUBDIV AND TINYUSDZ_WITH_TINYSUBDIV)
  message(FATAL_ERROR
          "Cannot enable both TINYUSDZ_WITH_OPENSUBDIV and "
          "TINYUSDZ_WITH_TINYSUBDIV. TinySubdiv is the default backend; "
          "disable one subdivision backend before configuring.")
endif()

option(TINYUSDZ_WITH_AUDIO "Build with Audio support(MP3 and WAV)" ON)
option(TINYUSDZ_WITH_ALAC_AUDIO "Build with ALAC(as M4A) Audio support" OFF)

option(TINYUSDZ_WITH_PYTHON "Build with Python binding through pybind11" OFF)

if (TINYUSDZ_WITH_PYTHON)
  # force enable Tydra C API + DLL build
  set(TINYUSDZ_WITH_TYDRA ON CACHE INTERNAL "" FORCE)
  set(TINYUSDZ_WITH_C_API ON CACHE INTERNAL "" FORCE)
  set(TINYUSDZ_BUILD_SHARED_LIBS ON CACHE INTERNAL "" FORCE)
endif()


option(
  TINYUSDZ_PREFER_LOCAL_PYTHON_INSTALLATION
  "Prefer locally-installed Python interpreter than system or conda/brew installed Python. Please specify your Python interpreter with `Python3_EXECUTABLE` cmake option"
  OFF)

option(TINYUSDZ_WITH_PXR_COMPAT_API "Build with pxr compatible API" ${TINYUSDZ_DEFAULT_WITH_PXR_COMPAT_API})

# C API requires Tydra
if (NOT TINYUSDZ_WITH_TYDRA AND TINYUSDZ_WITH_C_API)
  message(WARNING "C API requires Tydra enabled, so disable C API build. Please do not set TINYUSDZ_WITH_TYDRA Off if you need C API library")
  set(TINYUSDZ_WITH_C_API Off CACHE INTERNAL "" FORCE)
endif ()

# deprecated. to be removed
#option(
#  TINYUSDZ_WITH_BLENDER_ADDON
#  "Build with Python module for Blender(`TINYUSDZ_WITH_PYTHON` is force set to ON"
#  OFF)

option(TINYUSDZ_USE_SYSTEM_ZLIB
       "Use system's zlib instead of miniz for TinyEXR/TIFF" OFF)

# -- Zstd compression --
option(TINYUSDZ_WITH_ZSTD_COMPRESSION
       "Enable zstd compression for USD files" ON)
option(TINYUSDZ_USE_SYSTEM_ZSTD
       "Use system's zstd instead of bundled version" OFF)

option(
  TINYUSDZ_PRODUCTION_BUILD
  "Build for production release(e.g. disables debug print, do not include full filepath in error messages)"
  OFF)


option(
  TINYUSDZ_DEBUG_PRINT
  "Enable debug print(For debugging). Disabled when `TINYUSDZ_PRODUCTION_BUILD` is On"
  OFF)

# -- modules --
option(TINYUSDZ_WITH_MODULE_USDA_READER "Build with USDA reader feature" ON)
option(TINYUSDZ_WITH_MODULE_USDA_WRITER "Build with USDA writer feature" ON)

# USDC(Crate binary) reader is mandatory in most situation, so this option is primarily for developer build.
option(TINYUSDZ_WITH_MODULE_USDC_READER "Build with USDC reader feature" ON)
option(TINYUSDZ_WITH_MODULE_USDC_WRITER "Build with USDC writer feature" ON)
# -------------

# -- STB --

#
# In default TinyUSDZ embeds stb_image and stb_image_write to load/save JPG/PNG/BMP images.
# (When TINYUSDZ_WITH_BUILTIN_IMAGE_LOADER is set On)
#

# Set ON if you use stb_image implementation outside of TinyUSDZ library to avoid multiple symbol def error.
option(TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION
       "Do not define STB_IMAGE_IMPLEMENTATION" OFF)

# Set ON if you use stb_image_write implementation outside of TinyUSDZ library in your app to avoid multiple symbol def error.
option(TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION
       "Do not define STB_IMAGE_WRITGE_IMPLEMENTATION" OFF)
# ----------

# -- Wuffs --

#
# Use wuffs to decode jpeg/png/bmp instead of using stb_image.
# (wuffs is well(I guess) fuzz tested image decoder, so has better security than stb_image)
#
option(TINYUSDZ_USE_WUFFS_IMAGE_LOADER
       "Use wuffs to load jpg/png/bmp images" OFF)

# -- nanoimage --
#
# Use nanoimage to decode jpg/png/bmp/tga images.
# nanoimage is a fuzz-tested, memory-safe C image decoder.
# When enabled, replaces the stb_image backend for common image formats.
#
option(TINYUSDZ_WITH_NANOIMAGE
       "Use nanoimage to load jpg/png/bmp/tga images (replaces stb_image for these formats)" OFF)
# -----------

# Set ON if you use wuffs implementation outside of TinyUSDZ library in your app to avoid multiple symbol def error.
option(TINYUSDZ_NO_WUFFS_IMPLEMENTATION
       "Do not define WUFFS_IMPLEMENTATION" OFF)
# -----------

#

# -- TIFF --
option(TINYUSDZ_WITH_TIFF
       "Build with TIFF texture(includes 32bit floating point TIFF) support"
       OFF)
# ----------

# -- EXR --
option(TINYUSDZ_WITH_EXR "Build with EXR HDR texture support" ON)
option(TINYUSDZ_USE_TINYEXR_V3 "Use TinyEXR v3 API (modern C17/C++17 API). Set OFF for legacy v1 API" ON)
# ---------

# -- ColorIO --
option(TINYUSDZ_WITH_COLORIO
       "Build with Color IO Baked LUT support(through tinycolorio)" ON)

option(TINYUSDZ_WITH_MESHOPT
       "Build with meshoptimizer support for mesh optimization" ${TINYUSDZ_DEFAULT_WITH_MESHOPT})

option(TINYUSDZ_WITH_REMOTERY
       "Build with Remotery profiling support" OFF)
# ---------

# -- optional tool --
# TODO: deperecate. Use tusdcat
option(TINYUSDZ_WITH_TOOL_USDA_PARSER "Build with USDA parser program" Off)
option(TINYUSDZ_WITH_TOOL_USDC_PARSER "Build with USDC parser program" Off)
# --------------

# -- For developers --
option(TINYUSDZ_COMPILE_TIME_TRACE
       "Add -ftime-trace to profile compilation time(clang only)" OFF)
option(TINYUSDZ_CUSTOM_COMPILE_FLAGS
       "Use hard-coded custom compile flags(described in CMakeLists.txt). For Developer only)" OFF)
# ---

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

# When ASan is enabled, propagate the runtime link flag to ALL
# executables. add_sanitizers() (from FindSanitizers.cmake) sets
# LINK_FLAGS as a private target property; any exe target that links
# the asan-instrumented tinyusdz_static.a but forgets to call
# add_sanitizers() ends up with undefined references to
# __asan_report_load*, __asan_memcpy, etc. Setting CMAKE_EXE_LINKER_FLAGS
# globally avoids that footgun across examples/, tests/feat/,
# tests/pprint/, ... — every binary in the build tree gets the runtime
# automatically.
if(SANITIZE_ADDRESS)
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
endif()

# [cifuzz]
option(TINYUSDZ_TEST_CIFUZZ "Run cifuzz test" OFF)

if (TINYUSDZ_TEST_CIFUZZ)
  find_package(cifuzz NO_SYSTEM_ENVIRONMENT_PATH)
  enable_fuzz_testing()
endif()

if (TINYUSDZ_ENABLE_THREAD)
  find_package(Threads REQUIRED)
  # prefer adding "-pthread" compile flag
  set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()

if(TINYUSDZ_WITH_EXR OR TINYUSDZ_WITH_TIFF)
  if(TINYUSDZ_USE_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
  endif()
endif()


# Currently TinyUSDZ does not claim CXX version when TinyUSDZ is added as add_subdirectory()
# of toplevel(root) project.
# TODO: Property inherit CXX compiler settings from toplevel(root) project.


if (PROJECT_IS_TOP_LEVEL)
  message(STATUS "TinyUSDZ is being built as toplevel project so set CXX standard here.")
  if(TINYUSDZ_WITH_COROUTINE)
    message(STATUS "Use C++20 for c++20 coroutine.")
    # Coroutine support requires C++20
    set(CMAKE_CXX_STANDARD 20)
  elseif(TINYUSDZ_WITH_PYTHON)
    #set(CMAKE_CXX_STANDARD 17) # nanobind requires C++17

    # for pybind11
    set(CMAKE_CXX_STANDARD 17)
  else()
    # Require strict C++17 mode(e.g. `-std=c++17`)
    set(CMAKE_CXX_STANDARD 17)
  endif()
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# [ccache]
if(TINYUSDZ_USE_CCACHE)
  if(MSVC)
    # No ccache support
  else()
    find_program(CCACHE_EXE ccache)
    if(CCACHE_EXE)

      message(STATUS "Use ccache : " ${CCACHE_EXE})

      # ccache + precompiled headers: ccache will refuse to cache PCH-using TUs
      # unless told to relax the PCH/time-macro hash checks. Wrap the launcher
      # with `env CCACHE_SLOPPINESS=...` (Unix; MSVC path does not use ccache).
      set(CCACHE_LAUNCHER "${CCACHE_EXE}")
      if(TINYUSDZ_USE_PCH AND CMAKE_HOST_UNIX)
        set(CCACHE_LAUNCHER env CCACHE_SLOPPINESS=pch_defines,time_macros "${CCACHE_EXE}")
      endif()

      # CMAKE_C_COMPILER_LAUNCHER = available from cmake 3.4+)
      if(CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}"
                                      ${CCACHE_LAUNCHER})
      else()
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_LAUNCHER})
      endif()

      if(CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}"
                                        ${CCACHE_LAUNCHER})
      else()
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_LAUNCHER})
      endif()

    endif(CCACHE_EXE)
  endif()
endif()


if (TINYUSDZ_WITH_MCP_SERVER)
  set(TINYUSDZ_WITH_JSON On)
endif()

if(TINYUSDZ_WITH_PYTHON)

  # For the time beging, we stick with pybind11, since PyPI manylinux2014(probably mostly used architectrue as of 2022/Aug) does not support C++17.
  # We may switch to nanobind at some point(probably around 2024?)

  # We build monolithic python module, so build libtinyusdz as a static library
  # with PIC enabled.
  # NOTE: TinyUSDZ now builds static lib with PIC enabled by default, so commented out here.
  # set(CMAKE_POSITION_INDEPENDENT_CODE On)

  #[nanobind]
  #set(NB_SHARED
  #    OFF
  #    CACHE INTERNAL "")
  #set(NB_STATIC
  #    ON
  #    CACHE INTERNAL "")

  # workaround: find_package() does not work well inside of nanobind(`Python3::Module` target won't defined), so call it here.
  # and use `FindPython3`, not `FindPython`
  if(TINYUSDZ_PREFER_LOCAL_PYTHON_INSTALLATION)
    #message(STATUS "Local Python")
    set(Python3_FIND_FRAMEWORK NEVER) # Do not search framework python
    set(Python3_FIND_STRATEGY LOCATION)
    set(Python3_FIND_REGISTRY NEVER) # Windows only
  else()
    set(Python_FIND_FRAMEWORK LAST
    )# Prefer Brew/Conda to Apple framework python
  endif()

  find_package(
    Python3
    COMPONENTS Interpreter Development.Module
    REQUIRED)

  #message(STATUS "Python include dirs: " ${Python_INCLUDE_DIRS})
  message(STATUS "Python include dirs: " ${Python3_INCLUDE_DIRS})

  #add_subdirectory(${PROJECT_SOURCE_DIR}/src/external/nanobind nanobind_build)

  add_subdirectory(${PROJECT_SOURCE_DIR}/src/external/pybind11)
endif()

set(TINYUSDZ_SOURCES
    # Crate path utilities (path sorting/tree encoding for USDC writer)
    ${PROJECT_SOURCE_DIR}/src/crate-path-utils/path_sort.cc
    ${PROJECT_SOURCE_DIR}/src/crate-path-utils/tree_encode.cc
    ${PROJECT_SOURCE_DIR}/src/arg-parser.cc
    ${PROJECT_SOURCE_DIR}/src/asset-resolution.cc
    ${PROJECT_SOURCE_DIR}/src/tinyusdz.cc
    ${PROJECT_SOURCE_DIR}/src/xform.cc
    ${PROJECT_SOURCE_DIR}/src/performance.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser-props.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser-entry.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser-basetype.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser-basetype-inst.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser-timesamples.cc
    ${PROJECT_SOURCE_DIR}/src/ascii-parser-timesamples-array.cc
    ${PROJECT_SOURCE_DIR}/src/audio-loader.cc
    ${PROJECT_SOURCE_DIR}/src/base122.cc
    ${PROJECT_SOURCE_DIR}/src/usda-reader.cc
    ${PROJECT_SOURCE_DIR}/src/usda-reader-reconstruct-1.cc
    ${PROJECT_SOURCE_DIR}/src/usda-reader-reconstruct-2.cc
    ${PROJECT_SOURCE_DIR}/src/usda-reader-reconstruct-3.cc
    ${PROJECT_SOURCE_DIR}/src/usda-reader-reconstruct-4.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-property.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-prim.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-prim-reconstruct-1.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-prim-reconstruct-2.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-prim-reconstruct-3.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-prim-reconstruct-4.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-reader-reconstruct.cc
    ${PROJECT_SOURCE_DIR}/src/usda-writer.cc
    ${PROJECT_SOURCE_DIR}/src/usdc-writer.cc
    ${PROJECT_SOURCE_DIR}/src/composition.cc
    ${PROJECT_SOURCE_DIR}/src/composition-reconstruct.cc
    ${PROJECT_SOURCE_DIR}/src/composition-graph.cc
    ${PROJECT_SOURCE_DIR}/src/core/instance-key.cc
    ${PROJECT_SOURCE_DIR}/src/hash-util.cc
    ${PROJECT_SOURCE_DIR}/src/chunk-reader.cc
    ${PROJECT_SOURCE_DIR}/src/crate-reader.cc
    ${PROJECT_SOURCE_DIR}/src/crate-reader-arrays.cc
    ${PROJECT_SOURCE_DIR}/src/crate-reader-values.cc
    ${PROJECT_SOURCE_DIR}/src/crate-reader-paths.cc
    ${PROJECT_SOURCE_DIR}/src/crate-reader-timesamples.cc
    ${PROJECT_SOURCE_DIR}/src/crate-format.cc
    ${PROJECT_SOURCE_DIR}/src/crate-writer.cc
    ${PROJECT_SOURCE_DIR}/src/crate-writer-values.cc
    ${PROJECT_SOURCE_DIR}/src/stage-converter.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-geom.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-physics.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-ar.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-media.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-shader.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-light.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-skel.cc
    ${PROJECT_SOURCE_DIR}/src/sconv-layer.cc
    ${PROJECT_SOURCE_DIR}/src/crate-pprint.cc
    ${PROJECT_SOURCE_DIR}/src/crate-dump.cc
    ${PROJECT_SOURCE_DIR}/src/path-util.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct2.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct3.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-geom.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-geom2.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-geom3.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-lightprim.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-shader.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-shader2.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-shader3.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-shader4.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-physics.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-physics2.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-physics3.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-physics4.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-ar.cc
    ${PROJECT_SOURCE_DIR}/src/prim-reconstruct-media.cc
    ${PROJECT_SOURCE_DIR}/src/prim-composition.cc
    ${PROJECT_SOURCE_DIR}/src/prim-types.cc
    ${PROJECT_SOURCE_DIR}/src/core/prim-enums.cc
    ${PROJECT_SOURCE_DIR}/src/enum-handlers.cc
    ${PROJECT_SOURCE_DIR}/src/layer.cc
    ${PROJECT_SOURCE_DIR}/src/primvar.cc
    ${PROJECT_SOURCE_DIR}/src/str-util.cc
    ${PROJECT_SOURCE_DIR}/src/usd-dump.cc
    ${PROJECT_SOURCE_DIR}/src/value-pprint.cc
    ${PROJECT_SOURCE_DIR}/src/value-pprint-dispatch.cc
    ${PROJECT_SOURCE_DIR}/src/value-types.cc
    ${PROJECT_SOURCE_DIR}/src/value-types-typename.cc
    ${PROJECT_SOURCE_DIR}/src/value-types-cast.cc
    ${PROJECT_SOURCE_DIR}/src/color-space.cc
    ${PROJECT_SOURCE_DIR}/src/color-space.hh
    ${PROJECT_SOURCE_DIR}/src/usd-validation.cc
    ${PROJECT_SOURCE_DIR}/src/tiny-format.cc
    ${PROJECT_SOURCE_DIR}/src/tiny-string.cc
    ${PROJECT_SOURCE_DIR}/src/io-util.cc
    ${PROJECT_SOURCE_DIR}/src/image-loader.cc
    ${PROJECT_SOURCE_DIR}/src/image-writer.cc
    ${PROJECT_SOURCE_DIR}/src/image-util.cc
    ${PROJECT_SOURCE_DIR}/src/linear-algebra.cc
    ${PROJECT_SOURCE_DIR}/src/value-eval-util.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-accessors.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-primvar-inst-scalar-a.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-primvar-inst-scalar-b.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-primvar-inst-vec-a.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-primvar-inst-vec-b.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-primvar-inst-role-a.cc
    ${PROJECT_SOURCE_DIR}/src/usdGeom-primvar-inst-role-b.cc
    ${PROJECT_SOURCE_DIR}/src/usdSkel.cc
    ${PROJECT_SOURCE_DIR}/src/usdShade.cc
    ${PROJECT_SOURCE_DIR}/src/usdLux.cc
    # usdMtlX has less dependency(pugixml), so add it to core component
    ${PROJECT_SOURCE_DIR}/src/mtlx-xml-tokenizer.cc
    ${PROJECT_SOURCE_DIR}/src/mtlx-xml-parser.cc
    ${PROJECT_SOURCE_DIR}/src/mtlx-dom.cc
    ${PROJECT_SOURCE_DIR}/src/mtlx-simple-parser.cc
    ${PROJECT_SOURCE_DIR}/src/usdMtlx.cc
    ${PROJECT_SOURCE_DIR}/src/usdMtlx-write.cc
    ${PROJECT_SOURCE_DIR}/src/usdPhysics.cc
    ${PROJECT_SOURCE_DIR}/src/mjcPhysics.cc
    ${PROJECT_SOURCE_DIR}/src/usdObj.cc
    ${PROJECT_SOURCE_DIR}/src/image-loader.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-enum.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-meta.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-geom.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-shader.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-light.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-skel.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-physics.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-ar.cc
    ${PROJECT_SOURCE_DIR}/src/pprint-media.cc
    ${PROJECT_SOURCE_DIR}/src/pprinter.cc
    ${PROJECT_SOURCE_DIR}/src/timesamples-pprint.cc
    ${PROJECT_SOURCE_DIR}/src/timesamples.cc
    ${PROJECT_SOURCE_DIR}/src/timesamples-eval.cc
    ${PROJECT_SOURCE_DIR}/src/stage.cc
    ${PROJECT_SOURCE_DIR}/src/stage.hh
    ${PROJECT_SOURCE_DIR}/src/uuid-gen.cc
    ${PROJECT_SOURCE_DIR}/src/uuid-gen.hh
    ${PROJECT_SOURCE_DIR}/src/parser-timing.cc
    ${PROJECT_SOURCE_DIR}/src/sha256.cc
    ${PROJECT_SOURCE_DIR}/src/typed-array.cc
    ${PROJECT_SOURCE_DIR}/src/task-queue.cc
    ${PROJECT_SOURCE_DIR}/src/task-queue.hh
    ${PROJECT_SOURCE_DIR}/src/prim-pprint-parallel.cc
    ${PROJECT_SOURCE_DIR}/src/prim-pprint-parallel.hh

    )

if (TINYUSDZ_WITH_TYDRA)
    list(APPEND TINYUSDZ_SOURCES
        ${PROJECT_SOURCE_DIR}/src/tydra/facial.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/facial.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/prim-apply.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/prim-apply.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/scene-access.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/scene-access-listprims-inst.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/scene-access-listshaders-inst.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/scene-access.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/scene-analysis.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/scene-analysis.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/attribute-eval.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/attribute-eval.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/attribute-eval-typed-all.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/attribute-eval-typed-inst-scalar.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/attribute-eval-typed-inst-array.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/command-and-history.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/obj-export.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/usd-export.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/shader-network.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/shader-network.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-mesh.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-mesh-tangent.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-material.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-material-mtlx.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-anim.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-internal.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-pprint.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-data-pprint.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/raytracing-data.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/raytracing-data.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/raytracing-scene-converter.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/raytracing-scene-converter.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/material-serializer.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/material-serializer.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/materialx-to-json.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/materialx-to-json.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/physics-to-json.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/physics-to-json.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/urdf-to-usd.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/urdf-to-usd.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/ar-to-json.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/ar-to-json.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/ik-solver.h
        ${PROJECT_SOURCE_DIR}/src/tydra/ik-solver.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/ik-solver.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/rb-math.h
        ${PROJECT_SOURCE_DIR}/src/tydra/rb-collision.h
        ${PROJECT_SOURCE_DIR}/src/tydra/rb-collision.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/rb-dynamics.h
        ${PROJECT_SOURCE_DIR}/src/tydra/rb-dynamics.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/rb-dynamics.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-scene-dump.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/render-scene-dump.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/bone-util.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/bone-util.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/layer-to-renderscene.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/layer-to-renderscene.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/texture-util.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/texture-util.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/common-utils.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/common-utils.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/variant-support.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/variant-support.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/variant-converter.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/variant-converter.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/variant-applier.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/variant-applier.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools-scene.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools-scene.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools-query.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools-query.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools-composition.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-tools-composition.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-resources.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-resources.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-server.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-server.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-context.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-js-bridge.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/mcp-js-bridge.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/value-to-json.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/value-to-json.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/diff-and-compare.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/diff-and-compare.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/js-script.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/js-script.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/js-script-persistent.cc
        ${PROJECT_SOURCE_DIR}/src/tydra/threejs-exporter.hh
        ${PROJECT_SOURCE_DIR}/src/tydra/threejs-exporter.cc
        )
    
    if(TINYUSDZ_WITH_WAMR)
        list(APPEND TINYUSDZ_SOURCES
            ${PROJECT_SOURCE_DIR}/src/tydra/wasm-runtime.cc
            ${PROJECT_SOURCE_DIR}/src/tydra/wasm-runtime.hh
        )
    endif(TINYUSDZ_WITH_WAMR)
endif (TINYUSDZ_WITH_TYDRA)

# Variant extraction utilities handled via variant-support.hh/cc in Tydra section

if(TINYUSDZ_WITH_PXR_COMPAT_API)
  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/pxr-compat.cc)
endif()

set(TINYUSDZ_C_API_SOURCES
    ${PROJECT_SOURCE_DIR}/src/c-tinyusd.cc
    ${PROJECT_SOURCE_DIR}/src/c-tinyusd-helpers.cc
    ${PROJECT_SOURCE_DIR}/src/c-tinyusd-tydra.cc)

set(TINYUSDZ_DEP_SOURCES
    ${PROJECT_SOURCE_DIR}/src/integerCoding.cpp
    ${PROJECT_SOURCE_DIR}/src/lz4-compression.cc
    ${PROJECT_SOURCE_DIR}/src/lz4/lz4.c
    #${PROJECT_SOURCE_DIR}/src/external/pystring.cpp
)

# MikkTSpace tangent computation (public domain, by Morten S. Mikkelsen)
if(TINYUSDZ_WITH_TYDRA)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/mikktspace/mikktspace.c)
endif()

if (TINYUSDZ_WITH_BUILTIN_IMAGE_LOADER)
  list(APPEND TINYUSDZ_DEP_SOURCES  ${PROJECT_SOURCE_DIR}/src/external/fpng.cpp)
  # Disable SSE for a while, since -mpclmul required to use SSE feature of fpng.
  set_source_files_properties(${PROJECT_SOURCE_DIR}/src/external/fpng.cpp
                              PROPERTIES COMPILE_DEFINITIONS "FPNG_NO_SSE=1")
endif()

if(TINYUSDZ_WITH_USDMTLX)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/pugixml.cpp)
endif()

if(TINYUSDZ_WITH_JSON)
  list(APPEND TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/src/json-to-usd.cc
    ${PROJECT_SOURCE_DIR}/src/usd-to-json.cc
    ${PROJECT_SOURCE_DIR}/src/json-writer.cc
    ${PROJECT_SOURCE_DIR}/src/json-util.cc)

  list(APPEND TINYUSDZ_DEP_SOURCES  ${PROJECT_SOURCE_DIR}/src/external/yyjson.c)
endif()

if(TINYUSDZ_WITH_USDFBX)
  list(APPEND TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/src/usdFbx.cc
  )
  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/OpenFBX/src/ofbx.cpp)
endif()

if(TINYUSDZ_WITH_USDOBJ)
  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/tiny_obj_loader.cc)
endif()

if(TINYUSDZ_WITH_USDVOX)
  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/usdVox.cc)
endif()

set(TINYUSDZ_PYTHON_BINDING_SOURCES
    ${PROJECT_SOURCE_DIR}/src/python-bindings.cc)

#set(TINYUSDZ_BLENDER_PYTHON_BINDING_SOURCES
#    ${PROJECT_SOURCE_DIR}/src/blender/bindings.cc)

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

  # NOTE: tiny_dng_loader doesn't depend on tinyexr,
  # tinyexr is now added separately in the TINYUSDZ_WITH_EXR section
endif(TINYUSDZ_WITH_TIFF)

if(TINYUSDZ_WITH_EXR)
  if(TINYUSDZ_USE_TINYEXR_V3)
    # TinyEXR v3 API (modern C17/C++17 API)
    # NOTE: We also compile tinyexr.cc (v1 implementation) for backward compatibility
    # since image-loader.cc uses the v1 API (LoadEXRFromMemory, etc.)
    message(STATUS "TinyUSDZ: Using TinyEXR v3 API")

    if(TINYUSDZ_USE_SYSTEM_ZLIB)
      set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/src/external/tinyexr_c_impl.c
        PROPERTIES COMPILE_DEFINITIONS "TINYEXR_V3_HAS_DEFLATE=1;TINYEXR_V3_NO_MINIZ=1")
      set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc
        PROPERTIES COMPILE_DEFINITIONS "TINYEXR_USE_MINIZ=0")
    else()
      # Use bundled miniz (default)
      set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/src/external/tinyexr_c_impl.c
        PROPERTIES INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/external)
      set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc
        PROPERTIES INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/external)
    endif()

    list(APPEND TINYUSDZ_DEP_SOURCES
         ${PROJECT_SOURCE_DIR}/src/external/tinyexr_c_impl.c
         ${PROJECT_SOURCE_DIR}/src/external/tinyexr.cc)

  else()
    # TinyEXR v1 API (legacy API)
    message(STATUS "TinyUSDZ: Using TinyEXR v1 API (legacy)")

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
  endif()
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

# -- Zstd compression --
if(TINYUSDZ_WITH_ZSTD_COMPRESSION)
  if(NOT TINYUSDZ_USE_SYSTEM_ZSTD)
    list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/zstd.c)
    # Disable ASM to avoid linker issues, suppress warnings for third-party code
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/zstd.c
      PROPERTIES COMPILE_DEFINITIONS "ZSTD_DISABLE_ASM=1"
                 COMPILE_FLAGS "-w")
  else()
    find_package(zstd REQUIRED)
  endif()
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/zstd-compression.cc)
endif()

# -- nanoimage --
if(TINYUSDZ_WITH_NANOIMAGE)
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_alloc.c
    ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_bmp.c
    ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_jpeg.c
    ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_png.c
    ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_tga.c
    PROPERTIES COMPILE_FLAGS "-w")
  # nanoimage_zlib uses miniz (bundled) instead of system zlib
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_zlib.c
    PROPERTIES COMPILE_FLAGS "-w" COMPILE_DEFINITIONS "NI_USE_MINIZ"
               INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/external)
  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_alloc.c
       ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_bmp.c
       ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_jpeg.c
       ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_png.c
       ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_tga.c
       ${PROJECT_SOURCE_DIR}/src/external/nanoimage/nanoimage_zlib.c)
  # Ensure miniz.c is compiled (may already be added by EXR/TIFF, but guard for
  # when nanoimage is the only inflate consumer)
  if(NOT TINYUSDZ_WITH_EXR AND NOT TINYUSDZ_WITH_TIFF)
    list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/external/miniz.c)
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/external/miniz.c
      PROPERTIES COMPILE_DEFINITIONS "_LARGEFILE64_SOURCE=1")
  endif()
endif()

if(TINYUSDZ_WITH_ALAC_AUDIO)
  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/EndianPortable.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/ALACBitUtilities.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/ALACDecoder.cpp
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/ALACEncoder.cpp
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/ag_dec.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/ag_enc.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/dp_dec.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/dp_enc.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/matrix_dec.c
       ${PROJECT_SOURCE_DIR}/src/external/alac/codec/matrix_enc.c
  )
endif(TINYUSDZ_WITH_ALAC_AUDIO)

if(TINYUSDZ_WITH_MCP_SERVER)

  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/civetweb/civetweb.c
  )
endif(TINYUSDZ_WITH_MCP_SERVER)

if(TINYUSDZ_WITH_QJS)

  list(APPEND TINYUSDZ_DEP_SOURCES
       ${PROJECT_SOURCE_DIR}/src/external/quickjs-ng/cutils.c
       ${PROJECT_SOURCE_DIR}/src/external/quickjs-ng/libregexp.c
       ${PROJECT_SOURCE_DIR}/src/external/quickjs-ng/libunicode.c
       ${PROJECT_SOURCE_DIR}/src/external/quickjs-ng/quickjs.c
       ${PROJECT_SOURCE_DIR}/src/external/quickjs-ng/xsum.c
  )
endif(TINYUSDZ_WITH_QJS)

if(TINYUSDZ_WITH_OPENSUBDIV)

  # https://stackoverflow.com/questions/41700463/push-pop-a-cmake-variable
  function(ADD_OSD_LIB)

    #set(NO_TUTORIALS
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_EXAMPLES
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_REGRESSION
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_DOC
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_OMP
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_TBB
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_CUDA
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_OPENCL
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_OPENGL
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_TESTS
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_GLTESTS
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_GLFW
    #    ON
    #    CACHE INTERNAL "" FORCE)
    #set(NO_PTEX
    #    ON
    #    CACHE INTERNAL "" FORCE)

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

    #add_library(osd_cpu_static ${OSD_FAR_SOURCES} ${OSD_SDC_SOURCES} ${OSD_VTR_SOURCES} ${OSD_OSD_SOURCES})
    #target_include_directories(osd_cpu_static PRIVATE ${osd_DIR})

    #if (TINYUSDZ_BUILD_SHARED_LIBS)
    #  add_library(osd_cpu_shared SHARED ${OSD_FAR_SOURCES} ${OSD_SDC_SOURCES} ${OSD_VTR_SOURCES} ${OSD_OSD_SOURCES})
    #  target_include_directories(osd_cpu_shared PRIVATE ${osd_DIR})
    #endif()

    #if (MSVC)
    #  if (TINYUSDZ_CXX_MP_FLAG)
    #    target_compile_options(osd_cpu_static PRIVATE /MP${CMAKE_CXX_MP_NUM_PROCESSORS})
    #    if (TINYUSDZ_BUILD_SHARED_LIBS)
    #      target_compile_options(osd_cpu_shared PRIVATE /MP${CMAKE_CXX_MP_NUM_PROCESSORS})
    #    endif()
    #  endif()
    #endif()

    # Build as Object Library(non archival collections)
    add_library(osd_cpu ${OSD_FAR_SOURCES} ${OSD_SDC_SOURCES} ${OSD_VTR_SOURCES} ${OSD_OSD_SOURCES})
    target_include_directories(osd_cpu PRIVATE ${osd_DIR})
    set_target_properties(osd_cpu PROPERTIES POSITION_INDEPENDENT_CODE ON)
    
    # primarily for Visual Studio Solution Explorer.
    set_target_properties(osd_cpu PROPERTIES FOLDER "osd")
    

    #set(TINYUSDZ_OSD_SOURCES ${OSD_FAR_SOURCES} ${OSD_SDC_SOURCES} ${OSD_VTR_SOURCES} ${OSD_OSD_SOURCES})

  endfunction()

  add_osd_lib()

  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/subdiv.cc)
  # Archive the OpenSubdiv object files directly into tinyusdz_static
  # so downstream targets (tusdcat, examples, tests) don't need a
  # separate osd_cpu dependency.
  list(APPEND TINYUSDZ_EXT_SOURCES $<TARGET_OBJECTS:osd_cpu>)

endif(TINYUSDZ_WITH_OPENSUBDIV)

if(TINYUSDZ_WITH_TINYSUBDIV)
  message(STATUS "Using TinyUSDZ's own subdivision surface library")

  # Add our subdivision library sources
  list(APPEND TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/src/subdivision.cc
    ${PROJECT_SOURCE_DIR}/src/subdiv-tinyusdz.cc
  )

  # primvar-interpolation.hh is header-only, no .cc needed

endif(TINYUSDZ_WITH_TINYSUBDIV)

if(TINYUSDZ_WITH_GEOGRAM)
  # Geogram basic core files (header-only minimal set)
  set(GEOGRAM_BASIC_SOURCES
    # Start with empty list - headers are included via include directories
  )

  # For now, just provide headers for Geogram integration
  # Advanced functionality can be added later as needed
  set(GEOGRAM_POINTS_SOURCES
    # Core geometry processing headers are available
  )

  set(GEOGRAM_DELAUNAY_SOURCES
    # Core triangulation headers are available
  )

  # Minimal triangle library (C code, no exceptions)
  set(GEOGRAM_THIRD_PARTY_SOURCES
    ${PROJECT_SOURCE_DIR}/src/external/geogram/geogram/third_party/triangle/triangle.c
  )

  # Combine all Geogram sources
  set(GEOGRAM_ALL_SOURCES
    ${GEOGRAM_BASIC_SOURCES}
    ${GEOGRAM_POINTS_SOURCES}
    ${GEOGRAM_DELAUNAY_SOURCES}
    ${GEOGRAM_THIRD_PARTY_SOURCES}
  )

  # Add Geogram sources to TinyUSDZ
  list(APPEND TINYUSDZ_DEP_SOURCES ${GEOGRAM_ALL_SOURCES})

  # Add Geogram include directory
  set(GEOGRAM_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/src/external/geogram)

  # Set Geogram-specific compile properties
  set_source_files_properties(${GEOGRAM_ALL_SOURCES}
                              PROPERTIES COMPILE_DEFINITIONS "GEO_STATIC_LIBS;GEOGRAM_WITH_LEGACY_NUMERICS")

endif(TINYUSDZ_WITH_GEOGRAM)

if(TINYUSDZ_WITH_WAMR)
  # WAMR core sources (minimal runtime for WebAssembly execution)
  set(WAMR_CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/common/wasm_runtime_common.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/common/wasm_native.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/common/wasm_exec_env.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/common/wasm_memory.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/common/wasm_c_api.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/interpreter/wasm_loader.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/interpreter/wasm_runtime.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/interpreter/wasm_interp_classic.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/libraries/libc-builtin/libc_builtin_wrapper.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/mem-alloc/mem_alloc.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/mem-alloc/ems/ems_alloc.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/mem-alloc/ems/ems_hmu.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/mem-alloc/ems/ems_kfc.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_assert.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_common.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_hashmap.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_list.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_log.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_queue.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/bh_vector.c
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils/runtime_timer.c
  )

  # Add WAMR platform-specific sources
  if(WIN32)
    list(APPEND WAMR_CORE_SOURCES
      ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/platform/windows/platform_init.c
    )
  else()
    list(APPEND WAMR_CORE_SOURCES
      ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/platform/linux/platform_init.c
    )
  endif()

  # Add WAMR sources to TinyUSDZ
  list(APPEND TINYUSDZ_DEP_SOURCES ${WAMR_CORE_SOURCES})

  # Add WAMR include directories
  set(WAMR_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/include
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/interpreter
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/iwasm/common
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/utils
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/mem-alloc
    ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/platform/include
  )
  
  # Add platform-specific include directory
  if(WIN32)
    list(APPEND WAMR_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/platform/windows)
  else()
    list(APPEND WAMR_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/external/wamr/core/shared/platform/linux)
  endif()

  # Set WAMR-specific compile definitions
  if(WIN32)
    set_source_files_properties(${WAMR_CORE_SOURCES}
                                PROPERTIES COMPILE_DEFINITIONS "BH_PLATFORM_WINDOWS=1")
  else()
    set_source_files_properties(${WAMR_CORE_SOURCES}
                                PROPERTIES COMPILE_DEFINITIONS "BH_PLATFORM_LINUX=1")
  endif()

endif(TINYUSDZ_WITH_WAMR)

if(TINYUSDZ_WITH_MESHOPT)
  # meshoptimizer source files
  set(MESHOPTIMIZER_SOURCES
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/allocator.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/clusterizer.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/indexanalyzer.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/indexcodec.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/indexgenerator.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/overdrawoptimizer.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/partition.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/quantization.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/rasterizer.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/simplifier.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/spatialorder.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/stripifier.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/vcacheoptimizer.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/vertexcodec.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/vertexfilter.cpp
    ${PROJECT_SOURCE_DIR}/src/external/meshoptimizer/vfetchoptimizer.cpp
  )

  list(APPEND TINYUSDZ_DEP_SOURCES ${MESHOPTIMIZER_SOURCES})
endif(TINYUSDZ_WITH_MESHOPT)

if(TINYUSDZ_WITH_REMOTERY)
  # Remotery source files
  set(REMOTERY_SOURCES
    ${PROJECT_SOURCE_DIR}/src/external/Remotery/Remotery.c
  )
  
  list(APPEND TINYUSDZ_DEP_SOURCES ${REMOTERY_SOURCES})
  
  # Add Remotery include directory
  list(APPEND TINYUSDZ_DEP_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/src/external/Remotery)
  
  # Define RMT_ENABLED to enable Remotery
  add_definitions(-DRMT_ENABLED=1)
  
  # Platform-specific definitions for Remotery
  if(WIN32)
    add_definitions(-DRMT_USE_D3D11=0)
  elseif(APPLE)
    add_definitions(-DRMT_USE_METAL=0)
  elseif(UNIX)
    add_definitions(-DRMT_USE_OPENGL=0)
  endif()
endif(TINYUSDZ_WITH_REMOTERY)

if(TINYUSDZ_WITH_TIFF OR TINYUSDZ_WITH_EXR)
  if(TINYUSDZ_USE_SYSTEM_ZLIB)
    list(APPEND TINYUSDZ_EXT_LIBRARIES ZLIB::ZLIB)
  endif()
endif()


# Increase warning level for clang.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")

  set(TUSDZ_COMPILE_FLAGS
      "-Weverything -Wno-poison-system-directories -Wno-padded -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-documentation -Wno-unused-member-function -Wno-switch-enum -Wno-exit-time-destructors"
  )

  # Suppress pragma message warnings from external libraries (miniz.c)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-#pragma-messages")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-#pragma-messages")

  option(TINYUSDZ_NO_WERROR "Don't set -Werror when building the tinyusdz library" ${TINYUSDZ_DEFAULT_NO_WERROR})
  if (NOT TINYUSDZ_NO_WERROR)
    set(TUSDZ_COMPILE_FLAGS "${TUSDZ_COMPILE_FLAGS} -Werror")
  endif()

  # clang17 seems enable -Wunsafe-buffer-usage when `-Weverything` is defined,
  # resulting compilation error for some raw array access without bound check.
  # https://reviews.llvm.org/D138940
  # https://github.com/llvm/llvm-project/issues/64646
  # we need to suppress it as a work around.
  if (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 16)
    set(TUSDZ_COMPILE_FLAGS "${TUSDZ_COMPILE_FLAGS} -Wno-unsafe-buffer-usage")
  endif()

  # this is actually a work around of false-positive behavior of clang in Androind NDK 27.
  if (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 17)
    set(TUSDZ_COMPILE_FLAGS "${TUSDZ_COMPILE_FLAGS} -Wno-switch-default")
  endif()

  if(TINYUSDZ_COMPILE_TIME_TRACE)
    set(TUSDZ_COMPILE_FLAGS "${TUSDZ_COMPILE_FLAGS} -ftime-trace ")
  endif()

  set_source_files_properties(${TINYUSDZ_SOURCES}
                              PROPERTIES COMPILE_FLAGS ${TUSDZ_COMPILE_FLAGS})

  if (TINYUSDZ_WITH_C_API)
    set_source_files_properties(${TINYUSDZ_C_API_SOURCES}
                                PROPERTIES COMPILE_FLAGS ${TUSDZ_COMPILE_FLAGS})
  endif()

# Increase warning level for gcc.
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")

  set(TUSDZ_COMPILE_FLAGS
      "-Wall "
  )

  option(TINYUSDZ_NO_WERROR "Don't set -Werror when building the tinyusdz library" ${TINYUSDZ_DEFAULT_NO_WERROR})
  if (NOT TINYUSDZ_NO_WERROR)
    set(TUSDZ_COMPILE_FLAGS "${TUSDZ_COMPILE_FLAGS} -Werror")
  endif()

  set_source_files_properties(${TINYUSDZ_SOURCES}
                              PROPERTIES COMPILE_FLAGS ${TUSDZ_COMPILE_FLAGS})

  if (TINYUSDZ_WITH_C_API)
    set_source_files_properties(${TINYUSDZ_C_API_SOURCES}
                                PROPERTIES COMPILE_FLAGS ${TUSDZ_COMPILE_FLAGS})
  endif()
endif()

if(TINYUSDZ_CUSTOM_COMPILE_FLAGS)
  #
  # Overwrite existing TUSDZ_COMPILE_FLAGS
  #

  #if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(TUSDZ_CUSTOM_COMPILE_FLAGS "-fstack-usage ")
    set_source_files_properties(${TINYUSDZ_SOURCES}
                              PROPERTIES COMPILE_FLAGS ${TUSDZ_CUSTOM_COMPILE_FLAGS})

    if (TINYUSDZ_WITH_C_API)
      set_source_files_properties(${TINYUSDZ_C_API_SOURCES}
                                  PROPERTIES COMPILE_FLAGS ${TUSDZ_CUSTOM_COMPILE_FLAGS})
    endif()
  #endif()
endif()

#
# -- target
#

#
# Disabled Object library for a while, since it has some issue on MSVC target.
## To save compile time, use Object library(non-archival collections) for SHARED and Static lib.
## NOTE: Static lib is always being built with PIC enabled.
##
##
#add_library(tinyusdz_object OBJECT ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES} ${TINYUSDZ_EXT_SOURCES})
#set_target_properties(tinyusdz_object PROPERTIES FOLDER "tinyusdz")
#set_target_properties(tinyusdz_object PROPERTIES POSITION_INDEPENDENT_CODE ON)

# TinyUSDZ should compile well without /bigobj flag.
# Uncommend if you encounter the following errror: fatal  error C1128: number of sections exceeded object file format limit: compile with /bigobj
#if(MSVC)
#  target_compile_options(tinyusdz_object PRIVATE /bigobj)
#elif(WIN32)
#  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
#    # guess mingw-gcc
#    target_compile_options(tinyusdz_object PRIVATE "-Wa, -mbig-obj")
#  endif()
#endif()

#if (MSVC)
#  if (TINYUSDZ_CXX_MP_FLAG)
#    target_compile_options(tinyusdz_object PRIVATE /MP${CMAKE_CXX_MP_NUM_PROCESSORS})
#  endif()
#endif()
#
#if(TINYUSDZ_DEBUG_PRINT)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_DEBUG_PRINT")
#endif()
#
#if(TINYUSDZ_PRODUCTION_BUILD)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_PRODUCTION_BUILD")
#endif()
#
## default = enable module, so invert definition
#if(NOT TINYUSDZ_WITH_MODULE_USDA_READER)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_DISABLE_MODULE_USDA_READER")
#endif()
#
#if(NOT TINYUSDZ_WITH_MODULE_USDA_WRITER)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_DISABLE_MODULE_USDA_WRITER")
#endif()
#
#if(NOT TINYUSDZ_WITH_MODULE_USDC_READER)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_DISABLE_MODULE_USDC_READER")
#endif()
#
#if(NOT TINYUSDZ_WITH_MODULE_USDC_WRITER)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_DISABLE_MODULE_USDC_WRITER")
#endif()
#
#if(NOT TINYUSDZ_WITH_BUILTIN_IMAGE_LOADER)
#  target_compile_definitions(tinyusdz_object PRIVATE "TINYUSDZ_NO_BUILTIN_IMAGE_LOADER")
#endif()
#
#if(TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION)
#  target_compile_definitions(tinyusdz_object PRIVATE "TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION")
#endif()
#
#if(TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION)
#  target_compile_definitions(tinyusdz_object PRIVATE "TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION")
#endif()
#
#target_include_directories(tinyusdz_object
#                           PRIVATE ${PROJECT_SOURCE_DIR}/src)

#if(TINYUSDZ_WITH_JSON)
##target_include_directories(
##  ${TINYUSDZ_LIB_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src/external/jsonhpp/)
#  target_include_directories(
#    ${TINYUSDZ_LIB_TARGET}
#    PRIVATE ${PROJECT_SOURCE_DIR}/src/external/jsonhpp/)
#endif()

#if(TINYUSDZ_WITH_USDMTLX)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_USE_USDMTLX")
#endif()
#
#if(TINYUSDZ_WITH_USDFBX)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_USE_USDFBX")
#endif()
#
#if(TINYUSDZ_WITH_USDOBJ)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_USE_USDOBJ")
#endif()
#
#if(TINYUSDZ_WITH_USDVOX)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_USE_USDVOX")
#endif()

#target_link_libraries(tinyusdz_object ${TINYUSDZ_EXT_LIBRARIES}
#                      ${CMAKE_DL_LIBS})
#
#if (TINYUSDZ_ENABLE_THREAD)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_ENABLE_THREAD")
#  target_link_libraries(tinyusdz_object Threads::Threads)
#endif()
#
#
#if(IOS)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_BUILD_IOS")
#endif()
#
#if(TINYUSDZ_WITH_TIFF)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_WITH_TIFF")
#endif(TINYUSDZ_WITH_TIFF)
#
#if(TINYUSDZ_WITH_EXR)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_WITH_EXR")
#endif(TINYUSDZ_WITH_EXR)
#
#if(TINYUSDZ_WITH_COLORIO)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_WITH_COLORIO")
#endif(TINYUSDZ_WITH_COLORIO)
#
#if(TINYUSDZ_WITH_AUDIO)
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_WITH_AUDIO")
#endif(TINYUSDZ_WITH_AUDIO)
#
#if(TINYUSDZ_WITH_ALAC_AUDIO)
#  target_compile_definitions(tinuusdz_object
#                             PRIVATE "TINYUSDZ_WITH_ALAC_AUDIO")
#endif(TINYUSDZ_WITH_ALAC_AUDIO)
#
#if(TINYUSDZ_WITH_OPENSUBDIV)
#  target_include_directories(tinyusdz_object PRIVATE ${osd_DIR})
#  target_compile_definitions(tinyusdz_object
#                             PRIVATE "TINYUSDZ_WITH_OPENSUBDIV")
#endif(TINYUSDZ_WITH_OPENSUBDIV)
#
#if(NOT TINYUSDZ_CXX_EXCEPTIONS)
#  if(MSVC)
#    target_compile_options(tinyusdz_object PUBLIC /EHs-c-)
#  else()
#    target_compile_options(tinyusdz_object PUBLIC -fno-exceptions)
#  endif()
#endif()
#
# TODO: USE set_target_properties(... PROPERTIES OUTPUT_NAME outname)?
#add_library(${TINYUSDZ_TARGET_STATIC} STATIC $<TARGET_OBJECTS:tinyusdz_object>)
#add_sanitizers(${TINYUSDZ_TARGET_STATIC})

add_library(${TINYUSDZ_TARGET_STATIC} STATIC ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES} ${TINYUSDZ_EXT_SOURCES})
set_target_properties(${TINYUSDZ_TARGET_STATIC} PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_sanitizers(${TINYUSDZ_TARGET_STATIC})
#set_target_properties(tinyusdz_object PROPERTIES FOLDER "tinyusdz")

# Target with namespace
add_library(${TINYUSDZ_TARGET_STATIC_NS} ALIAS ${TINYUSDZ_TARGET_STATIC})


if(TINYUSDZ_BUILD_SHARED_LIBS)
  #add_library(
  #  ${TINYUSDZ_TARGET} SHARED $<TARGET_OBJECTS:tinyusdz_object>)
  add_library(
    ${TINYUSDZ_TARGET} SHARED ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES} ${TINYUSDZ_EXT_SOURCES})
  add_sanitizers(${TINYUSDZ_TARGET})

  # for dllexport
  target_compile_definitions(${TINYUSDZ_TARGET} PRIVATE "TINYUSDZ_COMPILE_LIBRARY")
  target_compile_definitions(${TINYUSDZ_TARGET} PRIVATE "TINYUSDZ_SHARED_LIBRARY")

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
# CMake's Object libary have some issue on MSVC build, so build STATIC and SHARED lib indepdendently.
#

#
# -- common target props
#
foreach(TINYUSDZ_LIB_TARGET ${TINYUSDZ_LIBS})

  # Precompiled header to amortize parsing of large, stable headers across the
  # many C++ TUs. Guarded with COMPILE_LANGUAGE:CXX because the library also
  # contains C sources (zstd.c, lz4.c, yyjson.c, ...) which cannot use a C++ PCH.
  if(TINYUSDZ_USE_PCH)
    target_precompile_headers(${TINYUSDZ_LIB_TARGET} PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:${PROJECT_SOURCE_DIR}/src/tinyusdz-pch.hh>")
  endif()

  # Although TinyUSDZ should compile well without /bigobj flag,
  # corrent TinyYSDZ need to use bigobj format otherwise we'll get: fatal  error C1128: number of sections exceeded object file format limit: compile with /bigobj
  if(MSVC)
    target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE /bigobj)
  endif()

  if (MSVC)
    if (TINYUSDZ_CXX_MP_FLAG)
      target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE /MP${CMAKE_CXX_MP_NUM_PROCESSORS})
    endif()
  endif()

  # Set a 8MB default stack size on Windows.
  # It defaults to 1MB on MSVC, which is the same as our current JS stack size,
  # so it will overflow and crash otherwise.
  # On MinGW it defaults to 2MB.
  if (TINYUSDZ_WITH_QJS) 
    if(WIN32)
       if(MSVC)
           target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE /STACK:8388608)
       endif()
    endif()
  endif()

  if(TINYUSDZ_DEBUG_PRINT)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DEBUG_PRINT")
  endif()

  if(TINYUSDZ_PRODUCTION_BUILD)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_PRODUCTION_BUILD")
  endif()

  # default = enable module, so invert definition
  if(NOT TINYUSDZ_WITH_MODULE_USDA_READER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDA_READER")
  endif()

  if(NOT TINYUSDZ_WITH_MODULE_USDA_WRITER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDA_WRITER")
  endif()

  if(NOT TINYUSDZ_WITH_MODULE_USDC_READER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDC_READER")
  endif()

  if(NOT TINYUSDZ_WITH_MODULE_USDC_WRITER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_DISABLE_MODULE_USDC_WRITER")
  endif()

  if(TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET} PRIVATE "TINYUSDZ_NO_STB_IMAGE_IMPLEMENTATION")
  endif()

  if(TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET} PRIVATE "TINYUSDZ_NO_STB_IMAGE_WRITE_IMPLEMENTATION")
  endif()

  if(TINYUSDZ_WITH_NANOIMAGE)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET} PRIVATE "TINYUSDZ_WITH_NANOIMAGE")
  endif()

  if(TINYUSDZ_WITH_ZSTD_COMPRESSION)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET} PRIVATE "TINYUSDZ_WITH_ZSTD_COMPRESSION")
    if(TINYUSDZ_USE_SYSTEM_ZSTD)
      target_link_libraries(${TINYUSDZ_LIB_TARGET} PUBLIC zstd::zstd)
    endif()
  endif()

  target_include_directories(${TINYUSDZ_LIB_TARGET}
                             PRIVATE ${PROJECT_SOURCE_DIR}/src)

  if(TINYUSDZ_WITH_JSON)
    #target_include_directories(
    #  ${TINYUSDZ_LIB_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src/external/jsonhpp/)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET} PRIVATE "TINYUSDZ_WITH_JSON")
  endif()

  if(TINYUSDZ_WITH_USDMTLX)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_USE_USDMTLX")
  endif()

  if(TINYUSDZ_WITH_USDFBX)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_USE_USDFBX")
  endif()

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

  if (TINYUSDZ_ENABLE_THREAD)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_ENABLE_THREAD")
    target_link_libraries(${TINYUSDZ_LIB_TARGET} Threads::Threads)
  endif()

  # On 32-bit systems, 64-bit atomic operations require libatomic
  # (Skip for emscripten as libatomic doesn't exist in wasm environment)
  # (Skip for MSVC as atomics are built into the runtime)
  if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT EMSCRIPTEN AND NOT MSVC)
    target_link_libraries(${TINYUSDZ_LIB_TARGET} atomic)
  endif()

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

  if(TINYUSDZ_WITH_ALAC_AUDIO)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_ALAC_AUDIO")
  endif(TINYUSDZ_WITH_ALAC_AUDIO)

  if(TINYUSDZ_WITH_MCP_SERVER)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_MCP_SERVER")

    # use dll for SSL + OPENSSL_API_3.0
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "OPENSSL_API_3_0")
  endif(TINYUSDZ_WITH_MCP_SERVER)

  if(TINYUSDZ_WITH_QJS)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_QJS")
  endif(TINYUSDZ_WITH_QJS)

  if(TINYUSDZ_WITH_OPENSUBDIV)
    target_include_directories(${TINYUSDZ_LIB_TARGET} PRIVATE ${osd_DIR})
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_OPENSUBDIV")
  endif(TINYUSDZ_WITH_OPENSUBDIV)

  if(TINYUSDZ_WITH_TINYSUBDIV)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_TINYSUBDIV")
  endif(TINYUSDZ_WITH_TINYSUBDIV)

  if(TINYUSDZ_WITH_TYDRA)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_TYDRA")
  endif(TINYUSDZ_WITH_TYDRA)

  if(TINYUSDZ_WITH_GEOGRAM)
    target_include_directories(${TINYUSDZ_LIB_TARGET} PRIVATE ${GEOGRAM_INCLUDE_DIR})
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_GEOGRAM")
    # Geogram-specific definitions
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "GEO_STATIC_LIBS")
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "GEOGRAM_WITH_LEGACY_NUMERICS")
  endif(TINYUSDZ_WITH_GEOGRAM)

  if(TINYUSDZ_WITH_WAMR)
    target_include_directories(${TINYUSDZ_LIB_TARGET} PRIVATE ${WAMR_INCLUDE_DIRS})
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_WAMR")
    # WAMR-specific definitions
    if(WIN32)
      target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                                 PRIVATE "BH_PLATFORM_WINDOWS=1")
    else()
      target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                                 PRIVATE "BH_PLATFORM_LINUX=1")
    endif()
  endif(TINYUSDZ_WITH_WAMR)

  if(TINYUSDZ_WITH_COROUTINE)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_COROUTINE")
  endif(TINYUSDZ_WITH_COROUTINE)

  if(TINYUSDZ_WITH_MESHOPT)
    target_compile_definitions(${TINYUSDZ_LIB_TARGET}
                               PRIVATE "TINYUSDZ_WITH_MESHOPT")
  endif(TINYUSDZ_WITH_MESHOPT)

  if(NOT TINYUSDZ_CXX_EXCEPTIONS)
    if(MSVC)
      target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE /EHs-c-)
    else()
      target_compile_options(${TINYUSDZ_LIB_TARGET} PRIVATE -fno-exceptions)
    endif()
  endif()

endforeach()




if(TINYUSDZ_WITH_C_API)
  if(TINYUSDZ_BUILD_SHARED_LIBS)
    add_library(${BUILD_TARGET_C} SHARED ${TINYUSDZ_C_API_SOURCES})
    # Link with static(object) version of libtinyusdz to make dll monolitic
    # TODO: Build TINYUSDZ_TARGET_STATIC as Object?
    target_link_libraries(${BUILD_TARGET_C} PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_compile_definitions(${BUILD_TARGET_C} PRIVATE "TINYUSDZ_COMPILE_LIBRARY")
    target_compile_definitions(${BUILD_TARGET_C} PRIVATE "TINYUSDZ_SHARED_LIBRARY")
    target_include_directories(
      ${BUILD_TARGET_C} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  endif() 

  add_library(${BUILD_TARGET_C}_static STATIC ${TINYUSDZ_C_API_SOURCES})
  target_link_libraries(${BUILD_TARGET_C}_static PRIVATE ${TINYUSDZ_TARGET_STATIC})
  target_include_directories(
    ${BUILD_TARGET_C}_static PRIVATE ${PROJECT_SOURCE_DIR}/src)
endif()

if(TINYUSDZ_WITH_PYTHON)

  # TODO: Fully Remove pybind11-based python binding
  ## For the time beging, we stick with pybind11, since PyPI manylinux2014(probably mostly used architectrue as of 2022/Aug) does not support C++17.
  ## We may switch to nanobind at some point(probably around 2024?)

  ## build monolithic .dll
  ##nanobind_add_module(${BUILD_TARGET_PY} ${TINYUSDZ_PYTHON_BINDING_SOURCES})

  #pybind11_add_module(${BUILD_TARGET_PY} ${TINYUSDZ_PYTHON_BINDING_SOURCES})
  #add_sanitizers(${BUILD_TARGET_PY})
  #target_include_directories(
  #  ${BUILD_TARGET_PY} PRIVATE ${PROJECT_SOURCE_DIR}/src)

  #target_link_libraries(${BUILD_TARGET_PY} PRIVATE ${TINYUSDZ_TARGET_STATIC})

  ## Use 'c' prefix
  ## TODO: Use `cpp` prefix?
  #set_target_properties(${BUILD_TARGET_PY} PROPERTIES OUTPUT_NAME "ctinyusdz")

  ## copy python binding .so file to python/
  ## For developer
  ## NOTE: `POST_BUILD` command is not triggered when building python module using
  ## `python setup.py build`
  #add_custom_command(
  #  TARGET ${BUILD_TARGET_PY}
  #  POST_BUILD
  #  COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${BUILD_TARGET_PY}>"
  #          "${CMAKE_SOURCE_DIR}/python/tinyusdz/$<TARGET_FILE_NAME:${BUILD_TARGET_PY}>"
  #  COMMENT "copying python module file to python/tinyusdz"
  #  VERBATIM)
  ## For pypi packaging
  #install(TARGETS ${BUILD_TARGET_PY} LIBRARY DESTINATION tinyusdz)


  # copy C tinyusd .so file to python/tinyusdz/
  # For developer
  # NOTE: `POST_BUILD` command is not triggered when building python module using
  # `python setup.py build`
  add_custom_command(
    TARGET ${BUILD_TARGET_C}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${BUILD_TARGET_C}>"
            "${CMAKE_SOURCE_DIR}/python/tinyusdz/$<TARGET_FILE_NAME:${BUILD_TARGET_C}>"
    COMMENT "Copying libc-tinyusd.so/c-tinyusd.dll file to <tinyusdz>/python/tinyusdz"
    VERBATIM)

  # TODO: Run ctypesgen?

  #
  # target for setup.py
  # TODO: Use custom target and supply `cmake_install_target` in setup.py?
  # https://scikit-build.readthedocs.io/en/latest/usage.html#scikit-build-options
  #
  install(TARGETS ${BUILD_TARGET_C} LIBRARY DESTINATION tinyusdz)


endif()

# TODO: deprecate in next major version
if(TINYUSDZ_WITH_TOOL_USDA_PARSER AND TINYUSDZ_WITH_MODULE_USDA_READER)
  add_executable(
    usda_parser ${CMAKE_CURRENT_SOURCE_DIR}/examples/usda_parser/parser-main.cc)
  add_sanitizers(usda_parser)
  if(TINYUSDZ_PRODUCTION_BUILD)
    target_compile_definitions(usda_parser
                               PRIVATE "TINYUSDZ_PRODUCTION_BUILD")
  endif()
  target_include_directories(usda_parser PRIVATE ${PROJECT_SOURCE_DIR}/src/)
  target_link_libraries(usda_parser PRIVATE ${TINYUSDZ_TARGET_STATIC})
endif()

# TODO: deprecate in next major version
if(TINYUSDZ_WITH_TOOL_USDC_PARSER AND TINYUSDZ_WITH_MODULE_USDC_READER)
  add_executable(
    usdc_parser ${CMAKE_CURRENT_SOURCE_DIR}/examples/usdc_parser/usdc-parser-main.cc)
  add_sanitizers(usdc_parser)
  if(TINYUSDZ_PRODUCTION_BUILD)
    target_compile_definitions(usdc_parser
                               PRIVATE "TINYUSDZ_PRODUCTION_BUILD")
  endif()
  target_include_directories(usdc_parser PRIVATE ${PROJECT_SOURCE_DIR}/src/)
  target_link_libraries(usdc_parser PRIVATE ${TINYUSDZ_TARGET_STATIC})
endif()

#if(TINYUSDZ_WITH_BLENDER_ADDON)
#  pybind11_add_module(${BUILD_TARGET_BLENDER_PY}
#                      ${TINYUSDZ_BLENDER_PYTHON_BINDING_SOURCES})
#  add_sanitizers(${BUILD_TARGET_BLENDER_PY})
#
#  # For Python.h header files. Assume Blender 2.93 LTS(Python 3.9)
#  target_include_directories(
#    ${BUILD_TARGET_BLENDER_PY}
#    PRIVATE ${PROJECT_SOURCE_DIR}/src/blender/python3.9/Include)
#
#endif()

if(TINYUSDZ_BUILD_EXAMPLES)
  add_subdirectory(examples/tusdcat)
  if (TINYUSDZ_WITH_TYDRA)
    add_subdirectory(examples/tydra_api)
    add_subdirectory(examples/tydra_to_renderscene)
    add_subdirectory(examples/variant-lister)
    add_subdirectory(examples/variant-analyzer)
    add_subdirectory(examples/usddiff)
    add_subdirectory(examples/urdf-to-usd)

    if (TINYUSDZ_WITH_MCP_SERVER)
      add_subdirectory(examples/mcp_server)
    endif()

    if (TINYUSDZ_WITH_QJS)
      add_subdirectory(examples/js-script)
    endif()
  endif ()

  add_subdirectory(examples/api_tutorial)
  add_subdirectory(examples/file_format)
  add_subdirectory(examples/asset_resolution)
  add_subdirectory(examples/progressive_composition)
  #if (TINYUSDZ_WITH_JSON)
  #  add_subdirectory(examples/usd_to_json)
  #endif()
  #if (TINYUSDZ_WITH_USD_TO_GLTF)
  #  add_subdirectory(examples/usd_to_gltf)
  #endif()
  if(TINYUSDZ_WITH_MODULE_USDA_WRITER)
    add_subdirectory(examples/save_usda)
  endif()
  if(TINYUSDZ_WITH_C_API)
    add_subdirectory(examples/c_api_example)
  endif()
endif(TINYUSDZ_BUILD_EXAMPLES)

# Tools
if(TINYUSDZ_BUILD_TOOLS)
  add_subdirectory(tools/tusddumpcrate)
endif()

if(TINYUSDZ_BUILD_BENCHMARKS)

  #
  # Standalone benchmark exe.
  #
  set(TINYUSDZ_BENCH_SOURCES ${PROJECT_SOURCE_DIR}/benchmarks/benchmark-main.cc)

  add_executable(${TINYUSDZ_BENCHMARK_TARGET} ${TINYUSDZ_BENCH_SOURCES})
  add_sanitizers(${TINYUSDZ_BENCHMARK_TARGET})

  target_include_directories(
    ${TINYUSDZ_BENCHMARK_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src
                                         ${PROJECT_SOURCE_DIR}/bencharks)
  target_link_libraries(${TINYUSDZ_BENCHMARK_TARGET}
                        PRIVATE ${TINYUSDZ_TARGET_STATIC})

  if(TINYUSDZ_WITH_OPENSUBDIV)
    target_compile_definitions(${TINYUSDZ_BENCHMARK_TARGET}
                               PRIVATE "TINYUSDZ_USE_OPENSUBDIV")
  endif(TINYUSDZ_WITH_OPENSUBDIV)

  if(TINYUSDZ_WITH_TINYSUBDIV)
    target_compile_definitions(${TINYUSDZ_BENCHMARK_TARGET}
                               PRIVATE "TINYUSDZ_USE_TINYSUBDIV")
  endif(TINYUSDZ_WITH_TINYSUBDIV)

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

  target_include_directories(
    ${TINYUSDZ_TEST_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src
                                    ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(${TINYUSDZ_TEST_TARGET}
                        PRIVATE ${TINYUSDZ_TARGET_STATIC})

  if(TINYUSDZ_WITH_OPENSUBDIV)
    target_compile_definitions(${TINYUSDZ_TEST_TARGET}
                               PRIVATE "TINYUSDZ_USE_OPENSUBDIV")
  endif(TINYUSDZ_WITH_OPENSUBDIV)

  #
  # unit tester
  #
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/unit)

  #
  # MCP tests (requires MCP server enabled)
  #
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/mcp)

  #
  # Compression tester
  # (its not involved with tinyusdz codes, and well tested now. so disabled. will be removed in future release)
  #
  # add_subdirectory(${PROJECT_SOURCE_DIR}/tests/decompress-int)

  #
  # Pretty-print benchmark
  #
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/pprint)


  #
  # TODO: Add feature to specify Python interpreter at ctest run
  #

  #
  # USDA parser tester
  #
  if(TINYUSDZ_WITH_MODULE_USDA_READER)
    add_test(
      NAME usda-parser-unit-test
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/usda/unit-runner.py --app
              "$<TARGET_FILE:test_tinyusdz>" --basedir "${PROJECT_SOURCE_DIR}/tests/usda"
      )
  endif()

  #
  # USDA roundtrip tester
  #
  if(TINYUSDZ_WITH_MODULE_USDA_READER)
    set(TINYUSDZ_ROUNDTRIP_TARGET "usda_roundtrip")
    add_executable(${TINYUSDZ_ROUNDTRIP_TARGET} ${PROJECT_SOURCE_DIR}/tests/usda-roundtrip.cc)
    add_sanitizers(${TINYUSDZ_ROUNDTRIP_TARGET})
    target_include_directories(${TINYUSDZ_ROUNDTRIP_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${TINYUSDZ_ROUNDTRIP_TARGET} PRIVATE ${TINYUSDZ_TARGET_STATIC})
    if(TINYUSDZ_WITH_JSON)
      target_compile_definitions(${TINYUSDZ_ROUNDTRIP_TARGET} PRIVATE "TINYUSDZ_WITH_JSON")
    endif()

    add_test(
      NAME usda-roundtrip-test
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/usda/roundtrip-runner.py --app
              "$<TARGET_FILE:usda_roundtrip>" --basedir "${PROJECT_SOURCE_DIR}/tests/usda"
      )
  endif()

  #
  # USDC roundtrip tester (USDA -> USDC -> reparse -> compare)
  #
  if(TINYUSDZ_WITH_MODULE_USDA_READER AND TINYUSDZ_WITH_MODULE_USDC_WRITER AND TINYUSDZ_WITH_MODULE_USDC_READER)
    set(TINYUSDZ_USDC_ROUNDTRIP_TARGET "usdc_roundtrip")
    add_executable(${TINYUSDZ_USDC_ROUNDTRIP_TARGET} ${PROJECT_SOURCE_DIR}/tests/usdc-roundtrip.cc)
    add_sanitizers(${TINYUSDZ_USDC_ROUNDTRIP_TARGET})
    target_include_directories(${TINYUSDZ_USDC_ROUNDTRIP_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${TINYUSDZ_USDC_ROUNDTRIP_TARGET} PRIVATE ${TINYUSDZ_TARGET_STATIC})
    if(TINYUSDZ_WITH_JSON)
      target_compile_definitions(${TINYUSDZ_USDC_ROUNDTRIP_TARGET} PRIVATE "TINYUSDZ_WITH_JSON")
    endif()

    add_test(
      NAME usdc-roundtrip-test
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/usda/usdc-roundtrip-runner.py --app
              "$<TARGET_FILE:usdc_roundtrip>" --basedir "${PROJECT_SOURCE_DIR}/tests/usda"
      )

    # USDC writer roundtrip: tusdcat writes USDC -> read back as USDA -> tusddiff
    # Uses --report-only so it does not fail the suite (informational baseline).
    # Run without --report-only for strict validation:
    #   python3 tests/usdc-writer/usdc-writer-runner.py --tusdcat ./build/tusdcat --tusddiff ./build/tusddiff --basedir tests/usda --verbose
    add_test(
      NAME usdc-writer-diff-test
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/usdc-writer/usdc-writer-runner.py
              --tusdcat "$<TARGET_FILE:tusdcat>"
              --tusddiff "$<TARGET_FILE:tusddiff>"
              --basedir "${PROJECT_SOURCE_DIR}/tests/usda"
              --report-only
      )
  endif()

  #
  # USDA parser tester
  #
  if(TINYUSDZ_WITH_MODULE_USDC_READER)
    add_test(
      NAME usdc-parser-unit-test
      COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/usdc/unit-runner.py --app
              "$<TARGET_FILE:test_tinyusdz>" --basedir "${PROJECT_SOURCE_DIR}/tests/usdc"
      )
    # usdc-parser-unit-test globs all *.usdc in tests/usdc/, including
    # *-runtime.usdc files that are generated by unit-test-tinyusdz.
    # Must run after unit tests to avoid reading partially-written files.
    set_tests_properties(usdc-parser-unit-test PROPERTIES
      DEPENDS unit-test-tinyusdz)
  endif()

  #
  # Feature tests as CTest targets (Phase 0B)
  #
  if(TINYUSDZ_WITH_MODULE_USDA_READER)
    # MaterialX tests
    add_executable(feat-mtlx-parse tests/feat/mtlx/test_mtlx_parse.cc)
    add_sanitizers(feat-mtlx-parse)
    target_link_libraries(feat-mtlx-parse PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(feat-mtlx-parse PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME feat-mtlx-parse COMMAND feat-mtlx-parse
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

    add_executable(feat-mtlx-import tests/feat/mtlx/test_mtlx_import.cc)
    add_sanitizers(feat-mtlx-import)
    target_link_libraries(feat-mtlx-import PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(feat-mtlx-import PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME feat-mtlx-import COMMAND feat-mtlx-import)

    add_executable(feat-mtlx-export tests/feat/mtlx/test_mtlx_export.cc)
    add_sanitizers(feat-mtlx-export)
    target_link_libraries(feat-mtlx-export PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(feat-mtlx-export PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME feat-mtlx-export COMMAND feat-mtlx-export)

    # Variant tests
    add_executable(feat-variant-converter tests/feat/nestedVariantSet/test_variant_converter.cpp)
    add_sanitizers(feat-variant-converter)
    target_link_libraries(feat-variant-converter PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(feat-variant-converter PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME feat-variant-converter COMMAND feat-variant-converter
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

    add_executable(feat-variant-applier tests/feat/nestedVariantSet/test_variant_applier.cpp)
    add_sanitizers(feat-variant-applier)
    target_link_libraries(feat-variant-applier PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(feat-variant-applier PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME feat-variant-applier COMMAND feat-variant-applier
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  endif()

  if(TINYUSDZ_WITH_JSON)
    add_executable(feat-mtlx-grouped-params tests/feat/mtlx/test_grouped_params.cc)
    add_sanitizers(feat-mtlx-grouped-params)
    target_link_libraries(feat-mtlx-grouped-params PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(feat-mtlx-grouped-params PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME feat-mtlx-grouped-params COMMAND feat-mtlx-grouped-params)
  endif()

  #
  # Benchmarks as CTest targets (Phase 0C) - excluded from default run
  #
  if(TINYUSDZ_WITH_MODULE_USDA_READER)
    add_executable(bench-parse-opt tests/feat/parse-opt/test-parse-opt.cc)
    add_sanitizers(bench-parse-opt)
    target_link_libraries(bench-parse-opt PRIVATE ${TINYUSDZ_TARGET_STATIC})
    target_include_directories(bench-parse-opt PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME bench-parse-opt COMMAND bench-parse-opt --quick)
    set_tests_properties(bench-parse-opt PROPERTIES LABELS "benchmark")
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
