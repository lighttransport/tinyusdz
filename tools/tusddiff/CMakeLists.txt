# Tool: tusddiff
# Diff two USD(A/C/Z) files at the Layer / PrimSpec / Attribute level
# (text or JSON output). Files are loaded as Layers (pre-composition).
#
# Assume this cmake is called from tinyusdz root via add_subdirectory().
set(TUSDDIFF_TARGET "tusddiff")

add_executable(${TUSDDIFF_TARGET}
    tusddiff.cc
    )
add_sanitizers(${TUSDDIFF_TARGET})

target_include_directories(${TUSDDIFF_TARGET} PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR})
if(NOT TARGET tinyusdz_next)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/next
                   ${CMAKE_BINARY_DIR}/tusddiff_next_build)
endif()
target_link_libraries(${TUSDDIFF_TARGET} ${TINYUSDZ_TARGET_STATIC} tinyusdz_next)

# Warning flags (same policy as the core library)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(TUSDDIFF_CXX_FLAGS
      "-Weverything -Wno-poison-system-directories -Wno-padded -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-documentation -Wno-documentation-unknown-command -Wno-unused-member-function -Wno-switch-enum -Wno-exit-time-destructors -Wno-sign-conversion -Wno-float-equal -Wno-old-style-cast -Wno-double-promotion -Wno-missing-prototypes -Wno-covered-switch-default -Wno-zero-as-null-pointer-constant -Wno-nrvo -Wno-weak-vtables -Wno-reserved-macro-identifier -Wno-reserved-identifier -Wno-tautological-unsigned-zero-compare -Wno-unknown-warning-option -Wno-undef -Wno-implicit-int-float-conversion -Wno-implicit-float-conversion -Wno-shorten-64-to-32 -Wno-ms-bitfield-padding -Wno-mismatched-tags"
  )
  if(${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 16)
    set(TUSDDIFF_CXX_FLAGS "${TUSDDIFF_CXX_FLAGS} -Wno-unsafe-buffer-usage")
  endif()
  if(${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER 17)
    set(TUSDDIFF_CXX_FLAGS "${TUSDDIFF_CXX_FLAGS} -Wno-switch-default")
  endif()
  if(NOT TINYUSDZ_NO_WERROR)
    set(TUSDDIFF_CXX_FLAGS "${TUSDDIFF_CXX_FLAGS} -Werror")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(TUSDDIFF_CXX_FLAGS "-Wall")
  if(NOT TINYUSDZ_NO_WERROR)
    set(TUSDDIFF_CXX_FLAGS "${TUSDDIFF_CXX_FLAGS} -Werror")
  endif()
endif()
set_source_files_properties(tusddiff.cc PROPERTIES COMPILE_FLAGS "${TUSDDIFF_CXX_FLAGS}")

set_target_properties(${TUSDDIFF_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(TINYUSDZ_BUILD_TESTS)
  add_test(NAME tusddiff_flatten_composed
    COMMAND tusddiff --flatten
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/composed.usda
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/flattened.usda)
  add_test(NAME tusddiff_openusd_cli_aliases
    COMMAND tusddiff -n -q -f
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/composed.usda
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/flattened.usda)
endif()
