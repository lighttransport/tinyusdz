# 3.13 = target_link_directories
cmake_minimum_required(VERSION 3.13)

set(EXAMPLE_BUILD_TARGET "pxrusd")

project(${EXAMPLE_BUILD_TARGET} C CXX)

# options
set(EXAMPLE_USD_DIR "dist" CACHE PATH "Path to built USD library")

# cmake modules

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/sanitizers)
find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

find_package(Threads REQUIRED)

# Require strict C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# TODO(LTE): Only enable PIC when compiling python module(dll)?.
set(CMAKE_POSITION_INDEPENDENT_CODE On)

#
# === core source files ===
#
set(EXAMPLE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
  )

add_executable(${EXAMPLE_BUILD_TARGET} ${EXAMPLE_SOURCES})
add_sanitizers(${EXAMPLE_BUILD_TARGET})


target_include_directories(${EXAMPLE_BUILD_TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# [usd]
target_compile_definitions(
  ${EXAMPLE_BUILD_TARGET}
  PUBLIC EXAMPLE_USE_USD)

# TODO(LTE): Check if files are exists 
target_include_directories(
  ${EXAMPLE_BUILD_TARGET}
  PUBLIC ${EXAMPLE_USD_DIR}/include)

target_link_directories(${EXAMPLE_BUILD_TARGET} PRIVATE ${EXAMPLE_USD_DIR}/lib)
target_link_libraries(${EXAMPLE_BUILD_TARGET} PRIVATE usd usdGeom usdUtils sdf tf vt)


target_link_libraries(${EXAMPLE_BUILD_TARGET} PUBLIC ${EXAMPLE_EXT_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)

# Increase warning level for clang.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")

  include(CheckCXXCompilerFlag)

  # -Wno-delete-non-abstract-non-virtual-dtor
  # -Wno-delete-non-virtual-dtor
  #  workaround for pbrtParser https://github.com/ingowald/pbrt-parser/issues/39
  #check_cxx_compiler_flag("-Wno-delete-non-abstract-non-virtual-dtor" HAS_NO_DELETE_NON_ABSTRACT_NON_VIRTUAL_DTOR)
  #if (HAS_NO_DELETE_NON_ABSTRACT_NON_VIRTUAL_DTOR)
  #  string(APPEND OPTIONAL_CXX_FLAGS " -Wno-delete-non-abstract-non-virtual-dtor ")
  #endif ()

  #check_cxx_compiler_flag("-Wno-delete-non-virtual-dtor" HAS_NO_DELETE_NON_VIRTUAL_DTOR)
  #if (HAS_NO_DELETE_NON_VIRTUAL_DTOR)
  #  string(APPEND OPTIONAL_CXX_FLAGS " -Wno-delete-non-virtual-dtor ")
  #endif ()

  #set(CLANG_COMPILE_FLAGS "-Weverything -Werror -Wno-padded -Wno-c++98-compat-pedantic -Wno-documentation -Wno-documentation-unknown-command -Wno-reserved-id-macro -Wno-unused-variable  -Wno-exit-time-destructors -Wno-global-constructors ")

  # TODO(LTE): Remove 'Wno-unused-functions'
  set(CLANG_COMPILE_FLAGS "-Weverything -Werror -Wno-padded -Wno-c++98-compat-pedantic -Wno-documentation -Wno-documentation-unknown-command -Wno-reserved-id-macro -Wno-unused-function")

  string(APPEND CLANG_COMPILE_FLAGS ${OPTIONAL_CXX_FLAGS})

  if(MINGW)
    # TODO(LTE): increase warning level for llvm-mingw.
  else()
    set_source_files_properties(
      ${EXAMPLE_SOURCES} ${EXAMPLE_GUI_SOURCES}
      PROPERTIES
      COMPILE_FLAGS
      ${CLANG_COMPILE_FLAGS}
      )
  endif()
endif()

# postbuild
if (WIN32)
  if (LLVM_MINGW_ROOT_ABSPATH)

    # Copy libunwind.dll and libc++.dll
    add_custom_command(TARGET ${EXAMPLE_BUILD_TARGET} POST_BUILD        # Adds a post-build event to MyTest
        COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
            "${LLVM_MINGW_ROOT_ABSPATH}/x86_64-w64-mingw32/bin/libc++.dll"      # src
            $<TARGET_FILE_DIR:${EXAMPLE_BUILD_TARGET}>)           # dest

    add_custom_command(TARGET ${EXAMPLE_BUILD_TARGET} POST_BUILD        # Adds a post-build event to MyTest
        COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
            "${LLVM_MINGW_ROOT_ABSPATH}/x86_64-w64-mingw32/bin/libunwind.dll"      # src
            $<TARGET_FILE_DIR:${EXAMPLE_BUILD_TARGET}>)           # dest

    # TODO(LTE): Copy asan dll only when the app is build with asan support
    add_custom_command(TARGET ${EXAMPLE_BUILD_TARGET} POST_BUILD        # Adds a post-build event to MyTest
        COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
            "${LLVM_MINGW_ROOT_ABSPATH}/x86_64-w64-mingw32/bin/libclang_rt.asan_dynamic-x86_64.dll"      # src
            $<TARGET_FILE_DIR:${EXAMPLE_BUILD_TARGET}>)           # dest

  endif (LLVM_MINGW_ROOT_ABSPATH)

endif (WIN32)

# [VisualStudio]
if(WIN32)
  # Set ${EXAMPLE_BUILD_TARGET} as a startup project for VS IDE
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${EXAMPLE_BUILD_TARGET})

  # For easier debugging in VS IDE(cmake 3.8.0 or later required) Set working
  # directory to ${EXAMPLE_BUILD_TARGET} git repo root.
  if(CMAKE_VERSION VERSION_GREATER 3.8.0)
    set_target_properties(${EXAMPLE_BUILD_TARGET}
                          PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY
                                     "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
