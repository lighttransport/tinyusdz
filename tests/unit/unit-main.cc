#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "acutest.h"

#include "unit-ascii-parse.h"
#include "unit-prim-types.h"
#include "unit-primvar.h"
#include "unit-pathutil.h"
#include "unit-value-types.h"
#include "unit-xform.h"
#include "unit-customdata.h"
#include "unit-handle-allocator.h"
#include "unit-math.h"
#include "unit-ioutil.h"
#include "unit-strutil.h"
#include "unit-timesamples.h"
#include "unit-fp-parse-print.h"
#include "unit-pprint.h"
#include "unit-materialx.h"
#include "unit-task-queue.h"
#include "unit-tydra.h"
//#include "unit-dedup.h"  // Temporarily disabled - needs API updates
#include "unit-crate-writer.h"
#include "unit-stage.h"
#include "unit-tiny-container.h"
#include "unit-usda-roundtrip.h"
#include "unit-half-roundtrip.h"

#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
#include "unit-pxr-compat-api.h"
#endif



TEST_LIST = {
  { "ascii_parse_int64_valid_test", ascii_parse_int64_valid_test },
  { "ascii_parse_int64_excessive_digits_test", ascii_parse_int64_excessive_digits_test },
  { "ascii_parse_uint64_valid_test", ascii_parse_uint64_valid_test },
  { "ascii_parse_uint64_excessive_digits_test", ascii_parse_uint64_excessive_digits_test },
  { "ascii_parse_string_array_test", ascii_parse_string_array_test },
  { "ascii_parse_array_comments_int_test", ascii_parse_array_comments_int_test },
  { "ascii_parse_array_comments_float_test", ascii_parse_array_comments_float_test },
  { "ascii_parse_array_comments_double_test", ascii_parse_array_comments_double_test },
  { "ascii_parse_array_comments_matrix4d_test", ascii_parse_array_comments_matrix4d_test },
  { "ascii_parse_array_comments_token_test", ascii_parse_array_comments_token_test },
  { "ascii_parse_array_comments_quatf_test", ascii_parse_array_comments_quatf_test },
  { "prim_type_test", prim_type_test },
  { "prim_add_test", prim_add_test },
  { "primvar_test", primvar_test },
  { "value_types_test", value_types_test },
  { "role_type_cast_test", role_type_cast_test },
  { "xformOp_test", xformOp_test },
  { "rotation_order_quat_vs_matrix_test", rotation_order_quat_vs_matrix_test },
  { "rotation_order_distinct_test", rotation_order_distinct_test },
  { "rotation_order_inverted_test", rotation_order_inverted_test },
  { "single_axis_rotation_test", single_axis_rotation_test },
  { "customdata_test", customdata_test },
  { "handle_allocator_test", handle_allocator_test },
  { "math_cos_pi_test", math_cos_pi_test },
  { "math_sin_pi_test", math_sin_pi_test },
  { "math_sin_cos_pi_test", math_sin_cos_pi_test },
  { "quat_to_quaternion_test", quat_to_quaternion_test },
  { "quat_to_matrix_roundtrip_test", quat_to_matrix_roundtrip_test },
  { "quat_operator_bracket_test", quat_operator_bracket_test },
  { "quat_decompose_roundtrip_test", quat_decompose_roundtrip_test },
  { "pathutil_test", pathutil_test },
  { "ioutil_test", ioutil_test },
  { "strutil_test", strutil_test },
  { "tinystring_test", tinystring_test },
  { "parse_int_test", parse_int_test },
  { "fp_string_conversion_test", fp_string_conversion_test },
  { "timesamples_test", timesamples_test },
  { "fp_roundtrip_basic_test", fp_roundtrip_basic_test },
  { "fp_roundtrip_edge_cases_test", fp_roundtrip_edge_cases_test },
  { "fp_roundtrip_special_values_test", fp_roundtrip_special_values_test },
  { "fp_roundtrip_precision_test", fp_roundtrip_precision_test },
  { "fp_roundtrip_buffer_test", fp_roundtrip_buffer_test },
  { "fp_shortest_representation_test", fp_shortest_representation_test },
  { "fp_format_range_test", fp_format_range_test },
  { "parse_array_test", parse_array_test },
  { "materialx_config_api_struct_test", materialx_config_api_struct_test },
  { "materialx_config_api_parsing_test", materialx_config_api_parsing_test },
  { "openpbr_surface_reconstruction_test", openpbr_surface_reconstruction_test },
  { "mtlx_standard_surface_reconstruction_test", mtlx_standard_surface_reconstruction_test },
  { "nodegraph_support_test", nodegraph_support_test },
  { "materialx_shader_constants_test", materialx_shader_constants_test },
  { "materialx_shader_fallback_values_test", materialx_shader_fallback_values_test },
  { "task_queue_basic_test", task_queue_basic_test },
  { "task_queue_func_test", task_queue_func_test },
  { "task_queue_full_test", task_queue_full_test },
  { "task_queue_multithreaded_test", task_queue_multithreaded_test },
  { "task_queue_clear_test", task_queue_clear_test },
  { "tydra_connection_validation_test", tydra_connection_validation_test },
  { "tydra_inplace_conversion_guard_test", tydra_inplace_conversion_guard_test },
  { "tydra_geommesh_property_accessor_test", tydra_geommesh_property_accessor_test },
  { "tydra_memory_tracking_test", tydra_memory_tracking_test },
  // Temporarily disabled - unit-dedup needs API updates
  //{ "dedup_float_array_test", dedup_float_array_test },
  //{ "dedup_double_array_test", dedup_double_array_test },
  //{ "dedup_int_array_test", dedup_int_array_test },
  //{ "dedup_unique_arrays_test", dedup_unique_arrays_test },
  //{ "dedup_string_array_test", dedup_string_array_test },
  //{ "dedup_matrix4d_test", dedup_matrix4d_test },
  { "crate_writer_basic_creation_test", crate_writer_basic_creation_test },
  { "crate_writer_simple_prim_test", crate_writer_simple_prim_test },
  { "crate_writer_typename_encoding_test", crate_writer_typename_encoding_test },
  { "crate_writer_timesamples_test", crate_writer_timesamples_test },
  { "crate_writer_pseudoroot_ordering_test", crate_writer_pseudoroot_ordering_test },
  { "crate_writer_roundtrip_test", crate_writer_roundtrip_test },
  { "crate_writer_multiple_prims_test", crate_writer_multiple_prims_test },
  { "crate_writer_nested_prims_test", crate_writer_nested_prims_test },
  { "crate_writer_error_handling_test", crate_writer_error_handling_test },
  { "crate_writer_material_shader_test", crate_writer_material_shader_test },
  { "crate_writer_layer_metadata_test", crate_writer_layer_metadata_test },
  { "crate_writer_usd_preview_surface_test", crate_writer_usd_preview_surface_test },
  { "crate_writer_usd_uv_texture_test", crate_writer_usd_uv_texture_test },
  { "crate_writer_usd_primvar_reader_test", crate_writer_usd_primvar_reader_test },
  { "crate_writer_usd_transform2d_test", crate_writer_usd_transform2d_test },
  { "crate_writer_cone_test", crate_writer_cone_test },
  { "crate_writer_cylinder_test", crate_writer_cylinder_test },
  { "crate_writer_capsule_test", crate_writer_capsule_test },
  { "crate_writer_points_test", crate_writer_points_test },
  { "crate_writer_camera_test", crate_writer_camera_test },
  { "crate_writer_basis_curves_test", crate_writer_basis_curves_test },
  { "crate_writer_nurbs_curves_test", crate_writer_nurbs_curves_test },
  { "crate_writer_geom_subset_test", crate_writer_geom_subset_test },
  { "crate_writer_point_instancer_test", crate_writer_point_instancer_test },
  { "crate_writer_material_binding_test", crate_writer_material_binding_test },
  { "crate_writer_xform_hierarchy_test", crate_writer_xform_hierarchy_test },
  { "crate_writer_model_test", crate_writer_model_test },
  { "crate_writer_scope_test", crate_writer_scope_test },
  { "crate_writer_mesh_advanced_features_test", crate_writer_mesh_advanced_features_test },
  { "crate_writer_blend_shape_test", crate_writer_blend_shape_test },
  { "crate_writer_relationship_features_test", crate_writer_relationship_features_test },
  { "crate_writer_material_shader_enhancements_test", crate_writer_material_shader_enhancements_test },
  { "crate_writer_layer_composition_test", crate_writer_layer_composition_test },
  { "crate_writer_skeletal_animation_test", crate_writer_skeletal_animation_test },
  { "crate_writer_advanced_attributes_test", crate_writer_advanced_attributes_test },
  { "crate_writer_assetinfo_test", crate_writer_assetinfo_test },
  { "crate_writer_shader_types_test", crate_writer_shader_types_test },
  { "crate_writer_skelBinding_test", crate_writer_skelBinding_test },
  { "crate_writer_references_payloads_test", crate_writer_references_payloads_test },
  { "crate_writer_custom_metadata_types_test", crate_writer_custom_metadata_types_test },
  { "crate_writer_complex_hierarchy_test", crate_writer_complex_hierarchy_test },
  { "crate_writer_advanced_geometry_test", crate_writer_advanced_geometry_test },
  { "crate_writer_normal_interpolation_test", crate_writer_normal_interpolation_test },
  { "crate_writer_visibility_purpose_test", crate_writer_visibility_purpose_test },
  { "crate_writer_instance_offsets_test", crate_writer_instance_offsets_test },
  { "crate_writer_large_array_types_test", crate_writer_large_array_types_test },
  { "crate_writer_sphere_light_test", crate_writer_sphere_light_test },
  { "crate_writer_rect_light_test", crate_writer_rect_light_test },
  { "crate_writer_distant_light_test", crate_writer_distant_light_test },
  { "crate_writer_dome_light_test", crate_writer_dome_light_test },
  { "crate_writer_multiple_lights_test", crate_writer_multiple_lights_test },
  { "crate_writer_light_filters_test", crate_writer_light_filters_test },
  { "crate_writer_nodegraph_test", crate_writer_nodegraph_test },
  { "crate_writer_error_context_test", crate_writer_error_context_test },
  { "crate_writer_memory_limit_test", crate_writer_memory_limit_test },
  { "crate_writer_filesize_limit_test", crate_writer_filesize_limit_test },
  { "crate_writer_limit_disable_test", crate_writer_limit_disable_test },
  { "crate_writer_validation_enabled_test", crate_writer_validation_enabled_test },
  { "crate_writer_validation_disabled_test", crate_writer_validation_disabled_test },
  { "crate_writer_compression_test", crate_writer_compression_test },
  { "crate_writer_specializes_test", crate_writer_specializes_test },
  { "stage_get_prim_at_path_test", stage_get_prim_at_path_test },
  { "stage_find_prim_by_id_test", stage_find_prim_by_id_test },
  { "stack_vector_basic_test", stack_vector_basic_test },
  { "stack_vector_overflow_test", stack_vector_overflow_test },
  { "stack_vector_copy_test", stack_vector_copy_test },
  { "stack_vector_move_test", stack_vector_move_test },
  { "stack_vector_iterator_test", stack_vector_iterator_test },
  { "stack_vector_complex_type_test", stack_vector_complex_type_test },
  { "usda_roundtrip_basic_test", usda_roundtrip_basic_test },
  { "usda_roundtrip_xform_test", usda_roundtrip_xform_test },
  { "usda_roundtrip_mesh_test", usda_roundtrip_mesh_test },
  { "usda_roundtrip_material_test", usda_roundtrip_material_test },
  { "usda_roundtrip_timesamples_test", usda_roundtrip_timesamples_test },
  { "half_roundtrip_exhaustive_test", half_roundtrip_exhaustive_test },
  { "half_roundtrip_edge_cases_test", half_roundtrip_edge_cases_test },
  { "half_shortest_representation_test", half_shortest_representation_test },
#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
  { "pxr_compat_api_test", pxr_compat_api_test },
#endif
  { nullptr, nullptr }
};
