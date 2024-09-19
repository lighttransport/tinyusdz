# Assume this project is invoked by emcmake.
cmake_minimum_required(VERSION 3.5.1)

set(BUILD_TARGET "tinyusdz")

if (NOT EMSCRIPTEN)
  message(FATAL "Must be compiled with emscripten")
endif()

project(${BUILD_TARGET} CXX C)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# cmake modules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../cmake)
#list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../cmake/sanitizers)
#find_package(Sanitizers) # Address sanitizer (-DSANITIZE_ADDRESS=ON)

# [tinyusdz]
add_subdirectory(../  tinyusdz)

list(
  APPEND
  SOURCES
  binding.cc
)


add_executable(${BUILD_TARGET} ${SOURCES})
add_sanitizers(${BUILD_TARGET})

target_compile_options(${BUILD_TARGET} PRIVATE ${EXT_COMPILE_OPTIONS})

# tinyusdz dir
target_include_directories(${BUILD_TARGET}
                           PRIVATE "${PROJECT_SOURCE_DIR}/../src/")


target_link_libraries(${BUILD_TARGET} PRIVATE tinyusdz::tinyusdz_static
                                              ${EXT_LIBRARIES} PUBLIC ${CMAKE_DL_LIBS})

source_group("Source Files" FILES ${SOURCES})

if (EMSCRIPTEN)
    set_target_properties(
        ${BUILD_TARGET}
        PROPERTIES OUTPUT_NAME tinyusdz
                   SUFFIX ".js"
                   RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/dist)
    #set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

# TODO: Adjust memory size.
# 4MB stack
set_target_properties(${BUILD_TARGET} PROPERTIES LINK_FLAGS "-sENVIRONMENT='web,worker' -sSTACK_SIZE=4000000 -sASSERTIONS -s ALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_ES6 -sINVOKE_RUN=0 --bind")

# ENVIRONMENT=web
# SINGLE_FILE=1
