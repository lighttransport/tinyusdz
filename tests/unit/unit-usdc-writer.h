// SPDX-License-Identifier: Apache 2.0
// USDC writer unit tests — usdGeom and usdSkel roundtrip coverage

#pragma once

// Geometry
void usdc_writer_mesh_basic_test(void);
void usdc_writer_mesh_subdiv_test(void);
void usdc_writer_mesh_velocities_test(void);
void usdc_writer_points_test(void);
void usdc_writer_basiscurves_test(void);
void usdc_writer_geomsubset_test(void);
void usdc_writer_camera_test(void);
void usdc_writer_primitives_test(void);

// Skeleton
void usdc_writer_skeleton_test(void);
void usdc_writer_skelanimation_test(void);
void usdc_writer_blendshape_test(void);
void usdc_writer_skelroot_test(void);

// Shader / Material
void usdc_writer_shader_terminal_test(void);
void usdc_writer_material_outputs_test(void);
void usdc_writer_uvtexture_test(void);
void usdc_writer_primvarreader_test(void);
void usdc_writer_transform2d_test(void);
void usdc_writer_previewsurface_full_test(void);

// Curves and Points
void usdc_writer_nurbscurves_test(void);
void usdc_writer_pointinstancer_test(void);
void usdc_writer_pointinstancer_prototypes_test(void);

// Instancing
void usdc_writer_instanceable_test(void);

// Lights
void usdc_writer_sphere_light_test(void);
void usdc_writer_distant_light_test(void);
void usdc_writer_dome_light_test(void);
void usdc_writer_light_shadow_shaping_test(void);

// apiSchemas
void usdc_writer_apischemas_test(void);

// MaterialX
void usdc_writer_materialx_config_test(void);

// Path utilities
void path_lessthan_basic_test(void);
void path_lessthan_variant_test(void);
void path_has_prefix_basic_test(void);
void path_has_prefix_variant_test(void);
void path_get_parent_basic_test(void);
void path_get_parent_variant_test(void);

// Path tree roundtrip (USDC)
void path_tree_flat_siblings_test(void);
void path_tree_deep_hierarchy_test(void);
void path_tree_mixed_props_test(void);
void path_tree_variant_basic_test(void);

// Variant roundtrip tests
void usdc_writer_variant_with_props_test(void);
void usdc_writer_variant_multi_selection_test(void);
void usdc_writer_variant_nested_test(void);
void usdc_writer_variant_with_selection_test(void);
void usdc_writer_variant_with_children_test(void);
void usdc_writer_variant_empty_test(void);
void usdc_writer_variant_multiple_sets_test(void);
void usdc_writer_variant_props_and_children_roundtrip_test(void);
void usdc_writer_variant_3level_nested_test(void);
void usdc_writer_variant_nested_with_props_test(void);
