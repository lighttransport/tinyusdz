/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-Present Light Transport Entertainment Inc.
 *
 * TinyUSDZ C API — tydra-next render-scene extraction.
 *
 * Convert a loaded stage into GPU-friendly render data (meshes, materials,
 * textures, lights, cameras, node hierarchy) with zero-copy buffer access.
 *
 * Buffer contract: tusd_buffer_view.data stays valid until the owning
 * tusd_render_scene is destroyed. Multi-chunk arrays are flattened once on
 * first request into a cache owned by the scene (thread-safe); single-chunk
 * arrays are returned without copying.
 */

#ifndef TINYUSDZ_RENDER_C_H_
#define TINYUSDZ_RENDER_C_H_

#include "tinyusdz-c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tusd_render_scene tusd_render_scene;

/* ============================================================
 * Conversion
 * ============================================================ */

typedef struct tusd_render_config {
  uint32_t struct_size;
  /* mesh */
  uint8_t triangulate;
  uint8_t compute_normals;
  uint8_t compute_tangents;
  uint8_t build_vertex_indices;
  /* material */
  uint8_t load_textures;
  uint8_t allow_missing_textures;
  uint8_t target_color_space; /* 0=srgb 1=linear 2=raw (tydra ColorSpace) */
  /* point instancer */
  uint8_t duplicate_instance_meshes;
  double time_code;
} tusd_render_config;

TUSD_API void tusd_render_config_init(tusd_render_config* cfg);

TUSD_API tusd_status tusd_render_convert(const tusd_stage* stage,
                                         const tusd_render_config* cfg,
                                         tusd_render_scene** out);
TUSD_API void tusd_render_scene_destroy(tusd_render_scene* scene);
TUSD_API tusd_status tusd_render_scene_warnings(const tusd_render_scene* scene,
                                                tusd_strlist** out);

/* ============================================================
 * Scene-level info
 * ============================================================ */

typedef enum tusd_render_kind {
  TUSD_RENDER_NODE = 0,
  TUSD_RENDER_MESH = 1,
  TUSD_RENDER_MATERIAL = 2,
  TUSD_RENDER_TEXTURE = 3,
  TUSD_RENDER_IMAGE = 4,
  TUSD_RENDER_LIGHT = 5,
  TUSD_RENDER_CAMERA = 6,
  TUSD_RENDER_SKELETON = 7,
  TUSD_RENDER_ANIMATION = 8,
  TUSD_RENDER_INSTANCER = 9,
  TUSD_RENDER_ROOT_NODE = 10
} tusd_render_kind;

TUSD_API size_t tusd_render_count(const tusd_render_scene* scene,
                                  uint8_t kind);
/* Path-index lookup for NODE / MESH / MATERIAL / INSTANCER; -1 if absent. */
TUSD_API int32_t tusd_render_lookup(const tusd_render_scene* scene,
                                    uint8_t kind, const char* prim_path);
TUSD_API int32_t tusd_render_root_node(const tusd_render_scene* scene,
                                       size_t index);

typedef struct tusd_render_scene_info {
  tusd_sv name;
  tusd_sv default_prim;
  float meters_per_unit;
  uint8_t up_axis; /* 0=Y 1=Z */
  double start_time;
  double end_time;
  double frames_per_second;
} tusd_render_scene_info;

TUSD_API tusd_status tusd_render_scene_get_info(const tusd_render_scene* scene,
                                                tusd_render_scene_info* out);

/* ============================================================
 * Per-object info (one call copies the whole POD block)
 * ============================================================ */

typedef struct tusd_render_node_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint8_t type; /* tydra NodeType */
  uint8_t visible;
  int32_t data_id;
  int32_t parent_id;
  uint32_t child_count;
  float local_transform[16];
  float world_transform[16];
} tusd_render_node_info;

TUSD_API tusd_status tusd_render_node_get_info(const tusd_render_scene* scene,
                                               int32_t id,
                                               tusd_render_node_info* out);
/* Copies up to cap child ids; returns total child count. */
TUSD_API size_t tusd_render_node_children(const tusd_render_scene* scene,
                                          int32_t id, int32_t* out,
                                          size_t cap);

typedef struct tusd_render_mesh_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint64_t point_count;
  uint64_t face_count;
  int32_t material_id;
  uint8_t is_triangulated;
  uint8_t has_normals;
  uint8_t has_tangents;
  uint8_t has_texcoords0;
  uint8_t has_texcoords1;
  uint8_t has_colors;
  uint8_t has_skin;
  uint8_t has_bbox;
  uint8_t normals_interp;    /* tydra Interpolation */
  uint8_t texcoords0_interp;
  uint8_t colors_interp;
  uint8_t _pad;
  uint32_t subset_count;
  uint32_t primvar_count;
  uint32_t blend_shape_count;
  int32_t skeleton_id; /* -1 when unskinned */
  float bbox_min[3];
  float bbox_max[3];
} tusd_render_mesh_info;

TUSD_API tusd_status tusd_render_mesh_get_info(const tusd_render_scene* scene,
                                               int32_t id,
                                               tusd_render_mesh_info* out);

TUSD_API tusd_status tusd_render_mesh_subset(const tusd_render_scene* scene,
                                             int32_t mesh_id, size_t index,
                                             uint32_t* face_start,
                                             uint32_t* face_count,
                                             int32_t* material_id);

typedef struct tusd_render_primvar_info {
  tusd_sv name;
  uint8_t format;        /* tydra VertexFormat */
  uint8_t interpolation; /* tydra Interpolation */
  uint8_t has_indices;
  uint8_t _pad;
  uint64_t element_count;
} tusd_render_primvar_info;

TUSD_API tusd_status tusd_render_mesh_primvar_info(
    const tusd_render_scene* scene, int32_t mesh_id, size_t index,
    tusd_render_primvar_info* out);

/* ============================================================
 * Buffers (zero-copy or flatten-once-cached)
 * ============================================================ */

typedef struct tusd_buffer_view {
  uint8_t component_type; /* tusd_component_type */
  uint8_t components;     /* scalars per logical element */
  uint16_t _pad;
  uint32_t _pad2;
  size_t count; /* logical elements */
  const void* data;
  size_t nbytes;
} tusd_buffer_view;

typedef enum tusd_mesh_buffer_kind {
  TUSD_MESH_BUF_POINTS = 0,          /* f32 x3 */
  TUSD_MESH_BUF_FACE_COUNTS = 1,     /* u32 x1 */
  TUSD_MESH_BUF_FACE_INDICES = 2,    /* u32 x1 */
  TUSD_MESH_BUF_TRI_INDICES = 3,     /* u32 x1 (triangulated) */
  TUSD_MESH_BUF_NORMALS = 4,         /* f32 x3 */
  TUSD_MESH_BUF_TANGENTS = 5,        /* f32 x4 */
  TUSD_MESH_BUF_TEXCOORDS0 = 6,      /* f32 x2 */
  TUSD_MESH_BUF_TEXCOORDS1 = 7,      /* f32 x2 */
  TUSD_MESH_BUF_COLORS = 8,          /* f32 x3 */
  TUSD_MESH_BUF_JOINT_INDICES = 9,   /* u16 x4 (skin) */
  TUSD_MESH_BUF_JOINT_WEIGHTS = 10   /* f32 x4 (skin) */
} tusd_mesh_buffer_kind;

TUSD_API tusd_status tusd_render_mesh_buffer(tusd_render_scene* scene,
                                             int32_t mesh_id, uint8_t kind,
                                             tusd_buffer_view* out);
/* which: 0=data, 1=indices */
TUSD_API tusd_status tusd_render_mesh_primvar_buffer(tusd_render_scene* scene,
                                                     int32_t mesh_id,
                                                     size_t primvar_index,
                                                     uint8_t which,
                                                     tusd_buffer_view* out);
/* which: 0=point_offsets, 1=normal_offsets. name/weight optional. */
TUSD_API tusd_status tusd_render_mesh_blendshape(
    tusd_render_scene* scene, int32_t mesh_id, size_t bs_index, uint8_t which,
    tusd_sv* name, float* weight, tusd_buffer_view* out);

/* ============================================================
 * Materials
 * ============================================================ */

typedef struct tusd_render_material_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint8_t shader_type; /* 0=none 1=preview_surface 2=openpbr */
  uint8_t double_sided;
  uint8_t alpha_mode; /* 0=opaque 1=mask 2=blend */
  uint8_t _pad;
  float alpha_cutoff;
} tusd_render_material_info;

TUSD_API tusd_status tusd_render_material_get_info(
    const tusd_render_scene* scene, int32_t id,
    tusd_render_material_info* out);

/* Named shader parameter. PreviewSurface names: diffuse_color,
 * emissive_color, specular_color, metallic, roughness, clearcoat,
 * clearcoat_roughness, opacity, opacity_threshold, ior, normal,
 * displacement, occlusion. OpenPBR names: base_weight, base_color,
 * base_roughness, base_metalness, specular_weight, specular_color,
 * specular_roughness, specular_ior, transmission_weight, transmission_color,
 * subsurface_weight, subsurface_color, coat_weight, coat_color,
 * coat_roughness, sheen_weight, sheen_color, sheen_roughness,
 * emission_luminance, emission_color, opacity, normal.
 * On success: *texture_id >= 0 means a texture drives the param. */
TUSD_API tusd_status tusd_render_material_param(
    const tusd_render_scene* scene, int32_t id, const char* param,
    int32_t* texture_id, float value[4]);

typedef struct tusd_render_texture_info {
  tusd_sv name;
  tusd_sv prim_path;
  tusd_sv asset_path;
  float uv_offset[2];
  float uv_scale[2];
  float uv_rotation;
  uint8_t wrap_s; /* tydra WrapMode */
  uint8_t wrap_t;
  uint8_t output_channel; /* tydra RenderTexture::Channel */
  uint8_t _pad;
  float bias[4];
  float scale[4];
  int32_t image_id;
} tusd_render_texture_info;

TUSD_API tusd_status tusd_render_texture_get_info(
    const tusd_render_scene* scene, int32_t id,
    tusd_render_texture_info* out);

typedef struct tusd_render_image_info {
  tusd_sv name;
  tusd_sv resolved_path;
  uint32_t width;
  uint32_t height;
  uint8_t channels;
  uint8_t component_type; /* tydra ComponentType */
  uint8_t color_space;    /* tydra ColorSpace */
  uint8_t is_loaded;
  uint64_t nbytes;
} tusd_render_image_info;

TUSD_API tusd_status tusd_render_image_get_info(const tusd_render_scene* scene,
                                                int32_t id,
                                                tusd_render_image_info* out);
TUSD_API tusd_status tusd_render_image_buffer(tusd_render_scene* scene,
                                              int32_t id,
                                              tusd_buffer_view* out);

/* ============================================================
 * Lights / cameras
 * ============================================================ */

typedef struct tusd_render_light_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint8_t type; /* tydra LightType */
  uint8_t normalize;
  uint8_t enable_shadow;
  uint8_t _pad;
  float color[3];
  float intensity;
  float exposure;
  float transform[16];
  /* type-specific scalars: sphere/disk radius, rect w/h, spot angle */
  float param0;
  float param1;
} tusd_render_light_info;

TUSD_API tusd_status tusd_render_light_get_info(const tusd_render_scene* scene,
                                                int32_t id,
                                                tusd_render_light_info* out);

typedef struct tusd_render_camera_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint8_t type; /* 0=perspective 1=orthographic */
  uint8_t _pad[3];
  float focal_length;
  float horizontal_aperture;
  float vertical_aperture;
  float ortho_width;
  float near_clip;
  float far_clip;
  float fov_x;
  float fov_y;
  float transform[16];
} tusd_render_camera_info;

TUSD_API tusd_status tusd_render_camera_get_info(
    const tusd_render_scene* scene, int32_t id, tusd_render_camera_info* out);

/* ============================================================
 * Skeletons / instancers (basic access)
 * ============================================================ */

typedef struct tusd_render_skeleton_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint32_t joint_count;
  int32_t root_joint;
  int32_t animation_id;
} tusd_render_skeleton_info;

TUSD_API tusd_status tusd_render_skeleton_get_info(
    const tusd_render_scene* scene, int32_t id,
    tusd_render_skeleton_info* out);

typedef struct tusd_render_joint_info {
  tusd_sv name;
  tusd_sv path;
  int32_t parent_id;
  float bind_transform[16];
  float rest_transform[16];
} tusd_render_joint_info;

TUSD_API tusd_status tusd_render_skeleton_joint(const tusd_render_scene* scene,
                                                int32_t skeleton_id,
                                                size_t joint_index,
                                                tusd_render_joint_info* out);

typedef struct tusd_render_instancer_info {
  tusd_sv name;
  tusd_sv prim_path;
  uint64_t instance_count;
  uint32_t prototype_count;
  uint8_t valid;
} tusd_render_instancer_info;

TUSD_API tusd_status tusd_render_instancer_get_info(
    const tusd_render_scene* scene, int32_t id,
    tusd_render_instancer_info* out);

typedef enum tusd_instancer_buffer_kind {
  TUSD_INST_BUF_PROTO_INDICES = 0, /* i32 x1 */
  TUSD_INST_BUF_POSITIONS = 1,     /* f32 x3 */
  TUSD_INST_BUF_ORIENTATIONS = 2,  /* f32 x4 */
  TUSD_INST_BUF_SCALES = 3,        /* f32 x3 */
  TUSD_INST_BUF_TRANSFORMS = 4,    /* f32 x16 */
  TUSD_INST_BUF_VISIBLE = 5        /* u8 x1 */
} tusd_instancer_buffer_kind;

TUSD_API tusd_status tusd_render_instancer_buffer(tusd_render_scene* scene,
                                                  int32_t id, uint8_t kind,
                                                  tusd_buffer_view* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TINYUSDZ_RENDER_C_H_ */
