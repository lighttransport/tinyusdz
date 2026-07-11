cmake_minimum_required(VERSION 3.16)

# tusdchecker uses only the dependency-free next core. Build that target on
# demand when the top-level project has not enabled another next consumer.
if(NOT TARGET tinyusdz_next)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/next
                   ${CMAKE_BINARY_DIR}/tusdchecker_next_build)
endif()

add_executable(tusdchecker tusdchecker.cc)
target_link_libraries(tusdchecker PRIVATE tinyusdz_next)
target_compile_features(tusdchecker PRIVATE cxx_std_17)
target_include_directories(tusdchecker PRIVATE ${PROJECT_SOURCE_DIR}/src)

if(COMMAND add_sanitizers)
  add_sanitizers(tusdchecker)
endif()

set_target_properties(tusdchecker PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(TINYUSDZ_BUILD_TESTS)
  add_test(NAME tusdchecker_valid
    COMMAND tusdchecker --groups core
            ${PROJECT_SOURCE_DIR}/tests/usda/cube.usda)
  set_tests_properties(tusdchecker_valid PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED")

  add_test(NAME tusdchecker_invalid
    COMMAND tusdchecker --groups core
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/invalid-layer.usda)
  set_tests_properties(tusdchecker_invalid PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_json
    COMMAND tusdchecker --json --groups core
            ${PROJECT_SOURCE_DIR}/tests/usda/cube.usda)
  set_tests_properties(tusdchecker_json PROPERTIES
    PASS_REGULAR_EXPRESSION "\"valid\":true")

  add_test(NAME tusdchecker_usdc
    COMMAND tusdchecker --core-only
            ${PROJECT_SOURCE_DIR}/tests/usdc/cone-000.usdc)

  add_test(NAME tusdchecker_usdz
    COMMAND tusdchecker --core-only
            ${PROJECT_SOURCE_DIR}/models/cube.usdz)

  add_test(NAME tusdchecker_strict_warning
    COMMAND tusdchecker --strict --core-only
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/warning-only.usda)
  set_tests_properties(tusdchecker_strict_warning PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_materialx
    COMMAND tusdchecker
            ${PROJECT_SOURCE_DIR}/tests/feat/mtlx/data/basic_openpbr.mtlx)

  add_test(NAME tusdchecker_materialx_invalid
    COMMAND tusdchecker
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/missing-materialx-node.mtlx)
  set_tests_properties(tusdchecker_materialx_invalid PROPERTIES
    WILL_FAIL TRUE)
endif()
