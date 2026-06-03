// SPDX-License-Identifier: Apache 2.0
// USD Physics and MuJoCo physics annotation unit tests

#pragma once

void physics_scene_reconstruct_test(void);
void physics_scene_mjc_scene_api_test(void);
void physics_scene_newton_api_test(void);
void physics_scene_newton_xpbd_api_test(void);
void physics_revolute_joint_test(void);
void physics_prismatic_joint_test(void);
void physics_fixed_joint_test(void);
void physics_distance_joint_test(void);
void physics_joint_mjc_api_test(void);
void physics_joint_physx_state_mirror_test(void);
void physics_joint_physx_state_usdc_roundtrip_test(void);
void physics_prismatic_state_init_test(void);
void physics_joint_physx_state_to_json_test(void);
void physics_joint_mjc_usdc_roundtrip_test(void);
void physics_joint_newton_mimic_api_test(void);
void mjc_actuator_test(void);
void newton_actuator_test(void);
void newton_actuator_extended_api_test(void);
void mjc_tendon_test(void);
void mjc_keyframe_test(void);
void physics_pprint_roundtrip_test(void);
void physics_spherical_joint_test(void);
void physics_to_json_test(void);
void physics_collision_group_test(void);
void physics_collision_group_colliders_test(void);
void physics_filtered_pairs_api_test(void);
void physics_collision_group_invert_test(void);
void physics_drive_limit_api_test(void);
void physics_mesh_collider_convention_test(void);
void physics_newton_collision_material_api_test(void);
void urdf_json_newton_api_export_test(void);
void urdf_json_mjcf_contact_export_test(void);
void physics_urdf_upaxis_axis_invariant_test(void);
