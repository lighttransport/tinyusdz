cmake_minimum_required(VERSION 3.5)

set(BUILD_TARGET "tinyusdz")
set(TEST_TARGET "test_tinyusdz")

project(${BUILD_TARGET} CXX)

# options
option(TINYUSDZ_USE_CCACHE "Use ccache for faster recompile." ON)
option(TINYUSDZ_BUILD_TESTS "Build tests" ON)
option(TINYUSDZ_BUILD_EXAMPLES "Build examples(but not all examples area built in `examples` folder)" ON)
option(TINYUSDZ_WITH_OPENSUBDIV "Build with OpenSubdiv(osdCPU. Set `osd_DIR` to specify the path to OpenSubdiv)" OFF)
# Use embedded version of OpenSubdiv code by default
set(osd_DIR ${PROJECT_SOURCE_DIR}/src/osd) 

option(TINYUSDZ_WITH_AUDIO "Build with Audio support(WAV and MP3)" ON)
option(TINYUSDZ_WITH_EXR "Build with EXR HDR texture support" ON)

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

# Require strict C++11 mode(e.g. `-std=c++11`)
set(CMAKE_CXX_STANDARD 11)
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

set(TINYUSDZ_SOURCES
    ${PROJECT_SOURCE_DIR}/src/tinyusdz.cc
    )

set(TINYUSDZ_DEP_SOURCES
    ${PROJECT_SOURCE_DIR}/src/integerCoding.cpp
    ${PROJECT_SOURCE_DIR}/src/lz4-compression.cc
    ${PROJECT_SOURCE_DIR}/src/pxrLZ4/lz4.cpp
  )

if (TINYUSDZ_WITH_EXR)
  list(APPEND TINYUSDZ_DEP_SOURCES ${PROJECT_SOURCE_DIR}/src/tinyexr.cc)
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

    # Only build libosdCPU.a
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)

    add_subdirectory(${osd_DIR})

  endfunction ()

  ADD_OSD_LIB()
  list(APPEND TINYUSDZ_EXT_LIBRARIES osd_static_cpu) 

  list(APPEND TINYUSDZ_SOURCES ${PROJECT_SOURCE_DIR}/src/subdiv.cc)

endif (TINYUSDZ_WITH_OPENSUBDIV)

add_library(${BUILD_TARGET} ${TINYUSDZ_SOURCES} ${TINYUSDZ_DEP_SOURCES})
add_sanitizers(${BUILD_TARGET})

target_include_directories(${BUILD_TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${BUILD_TARGET} ${TINYUSDZ_EXT_LIBRARIES} ${CMAKE_DL_LIBS})

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

if (TINYUSDZ_BUILD_EXAMPLES)
  add_subdirectory(examples/simple_dump)
endif (TINYUSDZ_BUILD_EXAMPLES)

if (TINYUSDZ_BUILD_TESTS)

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
