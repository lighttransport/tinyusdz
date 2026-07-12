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

  add_test(NAME tusdchecker_composed
    COMMAND tusdchecker --composed --groups core
            ${PROJECT_SOURCE_DIR}/tests/usda/aousd-defaultprim-reference.usda)

  add_test(NAME tusdchecker_composed_missing_reference
    COMMAND tusdchecker --composed --groups core
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/missing-reference.usda)
  set_tests_properties(tusdchecker_composed_missing_reference PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_schema_placement
    COMMAND tusdchecker --groups geom,physics
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/invalid-schema-placement.usda)
  set_tests_properties(tusdchecker_schema_placement PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_composed_type_mismatch
    COMMAND tusdchecker --composed --groups core
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/type-mismatch-root.usda)
  set_tests_properties(tusdchecker_composed_type_mismatch PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_package_regressions
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/test-package.py
            $<TARGET_FILE:tusdchecker>)

  add_test(NAME tusdchecker_crate_structure
    COMMAND tusdchecker --groups crate
            ${PROJECT_SOURCE_DIR}/tests/usdc/cone-000.usdc)
  set_tests_properties(tusdchecker_crate_structure PROPERTIES
    PASS_REGULAR_EXPRESSION "Checked rule groups: crate")

  add_test(NAME tusdchecker_require_all_groups
    COMMAND tusdchecker --require-all-groups --all
            ${PROJECT_SOURCE_DIR}/tests/usda/cube.usda)
  set_tests_properties(tusdchecker_require_all_groups PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_unselected_variant
    COMMAND tusdchecker --groups geom
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/invalid-variant-body.usda)
  set_tests_properties(tusdchecker_unselected_variant PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_unknown_api_schema
    COMMAND tusdchecker --strict --groups core
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/unknown-api-schema.usda)
  set_tests_properties(tusdchecker_unknown_api_schema PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_relationship_variability
    COMMAND tusdchecker --strict --groups core
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/varying-relationship.usda)
  set_tests_properties(tusdchecker_relationship_variability PROPERTIES
    WILL_FAIL TRUE)

  # --- ARKit / RealityKit profile -----------------------------------------
  add_test(NAME tusdchecker_arkit_clean
    COMMAND tusdchecker --arkit
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-clean.usda)
  set_tests_properties(tusdchecker_arkit_clean PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED - no issues found")

  add_test(NAME tusdchecker_arkit_violations
    COMMAND tusdchecker --arkit
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-violations.usda)
  set_tests_properties(tusdchecker_arkit_violations PROPERTIES
    WILL_FAIL TRUE)

  # PASS_REGULAR_EXPRESSION ignores the exit status, so this asserts the rule
  # ids reported for the same (failing) asset.
  add_test(NAME tusdchecker_arkit_violation_rules
    COMMAND tusdchecker --arkit
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-violations.usda)
  set_tests_properties(tusdchecker_arkit_violation_rules PROPERTIES
    PASS_REGULAR_EXPRESSION "arkit.normalMap.scaleBias")

  # The ARKit profile is opt-in: --all must not report arkit.* rules.
  add_test(NAME tusdchecker_arkit_opt_in
    COMMAND tusdchecker --all
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-violations.usda)
  set_tests_properties(tusdchecker_arkit_opt_in PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED - no issues found")

  add_test(NAME tusdchecker_arkit_json
    COMMAND tusdchecker --arkit --json
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-clean.usda)
  set_tests_properties(tusdchecker_arkit_json PROPERTIES
    PASS_REGULAR_EXPRESSION "\"checkedGroups\":\\[.*\"arkit\"\\]")

  add_test(NAME tusdchecker_arkit_package
    COMMAND tusdchecker --arkit
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-bad-package.usdz)
  set_tests_properties(tusdchecker_arkit_package PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME tusdchecker_arkit_package_rules
    COMMAND tusdchecker --arkit
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-bad-package.usdz)
  set_tests_properties(tusdchecker_arkit_package_rules PROPERTIES
    PASS_REGULAR_EXPRESSION "arkit.package.rootLayer")

  # The same package is only a portability *warning* without --arkit.
  add_test(NAME tusdchecker_arkit_package_opt_in
    COMMAND tusdchecker --groups package
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-bad-package.usdz)
  set_tests_properties(tusdchecker_arkit_package_opt_in PROPERTIES
    PASS_REGULAR_EXPRESSION "package.entry.extension")
endif()
