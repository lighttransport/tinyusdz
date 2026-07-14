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

  # The ARKit profile is opt-in: --all must not report arkit.* rules. (The
  # asset still draws always-on usdchecker-parity warnings — missing stage
  # metadata, unresolvable textures, shade.normalMap — so assert the absence
  # of arkit.* ids, not a clean pass.)
  add_test(NAME tusdchecker_arkit_opt_in
    COMMAND tusdchecker --all
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/arkit-violations.usda)
  set_tests_properties(tusdchecker_arkit_opt_in PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED"
    FAIL_REGULAR_EXPRESSION "\\[arkit\\.")

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

  # --- usdchecker parity rules ----------------------------------------------
  # Each fixture violates exactly one parity rule family; the test asserts the
  # rule id is reported (exit status ignored by PASS_REGULAR_EXPRESSION).
  function(tusdchecker_parity_rule_test name fixture rule_regex)
    add_test(NAME ${name}
      COMMAND tusdchecker ${CMAKE_CURRENT_SOURCE_DIR}/testdata/${fixture})
    set_tests_properties(${name} PROPERTIES
      PASS_REGULAR_EXPRESSION "${rule_regex}")
  endfunction()

  tusdchecker_parity_rule_test(tusdchecker_parity_stage_upaxis
    parity-stage-metadata.usda "geom.stage.upAxis")
  tusdchecker_parity_rule_test(tusdchecker_parity_stage_metersperunit
    parity-stage-metadata.usda "geom.stage.metersPerUnit")
  tusdchecker_parity_rule_test(tusdchecker_parity_defaultprim_missing
    parity-stage-metadata.usda "core.layer.defaultPrim.missing")
  tusdchecker_parity_rule_test(tusdchecker_parity_imageable_in_material
    parity-imageable-in-material.usda "shade.encapsulation.imageableInMaterial")
  tusdchecker_parity_rule_test(tusdchecker_parity_shader_type_mismatch
    parity-shader-type-mismatch.usda "shade.shader.typeMismatch")
  tusdchecker_parity_rule_test(tusdchecker_parity_unknown_shader_id
    parity-unknown-shader-id.usda "shade.shader.id")
  tusdchecker_parity_rule_test(tusdchecker_parity_subset_parent
    parity-geomsubset.usda "geom.subset.parent")
  tusdchecker_parity_rule_test(tusdchecker_parity_subset_familytype
    parity-geomsubset.usda "geom.subset.familyType")
  tusdchecker_parity_rule_test(tusdchecker_parity_subset_bind_family
    parity-geomsubset.usda "shade.subset.materialBindFamily")
  tusdchecker_parity_rule_test(tusdchecker_parity_unresolvable_dep
    parity-unresolvable-dep.usda "core.dependency.unresolvable")
  tusdchecker_parity_rule_test(tusdchecker_parity_skel_binding_rel
    parity-skel-binding.usda "geom.skel.binding.api")

  # shade.normalMap.* is always-on (not just --arkit), matching pxr's
  # NormalMapTextureValidator.
  tusdchecker_parity_rule_test(tusdchecker_parity_normalmap_always_on
    arkit-violations.usda "shade.normalMap.scaleBias")

  # --no-asset-checks gates only the defaultPrim presence rule (pxr
  # --noAssetChecks); stage metadata rules stay on.
  add_test(NAME tusdchecker_parity_no_asset_checks
    COMMAND tusdchecker --no-asset-checks
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/parity-stage-metadata.usda)
  set_tests_properties(tusdchecker_parity_no_asset_checks PROPERTIES
    PASS_REGULAR_EXPRESSION "geom.stage.upAxis"
    FAIL_REGULAR_EXPRESSION "core.layer.defaultPrim.missing")

  # --usdchecker-compat raises mapped rules to pxr severities: the same asset
  # flips from warnings-only PASS to FAIL.
  add_test(NAME tusdchecker_parity_compat_pass_without
    COMMAND tusdchecker
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/parity-stage-metadata.usda)
  add_test(NAME tusdchecker_parity_compat_fails_with
    COMMAND tusdchecker --usdchecker-compat
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/parity-stage-metadata.usda)
  set_tests_properties(tusdchecker_parity_compat_fails_with PROPERTIES
    WILL_FAIL TRUE)

  # Variant sweep: the defect hides in a NON-selected variant. --composed
  # enumerates all combinations and finds it; --skip-variants must not.
  add_test(NAME tusdchecker_parity_variant_sweep
    COMMAND tusdchecker --composed
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/parity-variant-error.usda)
  set_tests_properties(tusdchecker_parity_variant_sweep PROPERTIES
    PASS_REGULAR_EXPRESSION "shade.shader.typeMismatch")
  add_test(NAME tusdchecker_parity_variant_skip
    COMMAND tusdchecker --composed --skip-variants
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/parity-variant-error.usda)
  set_tests_properties(tusdchecker_parity_variant_skip PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED"
    FAIL_REGULAR_EXPRESSION "shade.shader.typeMismatch")
  add_test(NAME tusdchecker_parity_variant_explicit
    COMMAND tusdchecker --variants quality:bad
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/parity-variant-error.usda)
  set_tests_properties(tusdchecker_parity_variant_explicit PROPERTIES
    PASS_REGULAR_EXPRESSION "shade.shader.typeMismatch")

  add_test(NAME tusdchecker_parity_dump_rules
    COMMAND tusdchecker --dump-rules)
  set_tests_properties(tusdchecker_parity_dump_rules PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[core:core.layer.defaultPrim.missing\\]")

  # next-parser leniency parity with pxr's usda parser: sparse array edits
  # (preserved as raw text), float->int literal coercion, quoted-string asset
  # paths. Each fixture previously failed to parse (exit 2).
  add_test(NAME tusdchecker_parse_array_edit
    COMMAND tusdchecker --groups core
            ${PROJECT_SOURCE_DIR}/tests/usda/array_edit_basic.usda)
  set_tests_properties(tusdchecker_parse_array_edit PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED")
  add_test(NAME tusdchecker_parse_int_coercion
    COMMAND tusdchecker --groups core
            ${PROJECT_SOURCE_DIR}/tests/usda/int-assign-float-001.usda)
  set_tests_properties(tusdchecker_parse_int_coercion PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED")
  add_test(NAME tusdchecker_parse_quoted_asset
    COMMAND tusdchecker --groups core
            ${PROJECT_SOURCE_DIR}/tests/usda/assetpath-array-001.usda)
  set_tests_properties(tusdchecker_parse_quoted_asset PROPERTIES
    PASS_REGULAR_EXPRESSION "Result: PASSED")

  # Differential parity harness vs pxr usdchecker over tests/usda.
  # Self-skips when pxr is not installed.
  add_test(NAME tusdchecker_vs_usdchecker
    COMMAND bash ${PROJECT_SOURCE_DIR}/tests/run-tusdchecker-vs-usdchecker.sh)
  set_tests_properties(tusdchecker_vs_usdchecker PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIP:"
    ENVIRONMENT "TUSDCHECKER_PATH=$<TARGET_FILE:tusdchecker>")
endif()
