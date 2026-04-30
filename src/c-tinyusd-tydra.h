/* SPDX-License-Identifier: Apache 2.0
 *
 * C API wrapper around tinyusdz::tydra::RenderScene for language bindings.
 *
 * All accessors that return pointers return BORROWED pointers into the
 * owning RenderScene's internal storage. Those pointers are stable until
 * `c_tinyusd_render_scene_free` is called. RenderScene / RenderMesh /
 * RenderMaterial / RenderCamera / RenderLight are opaque C types; only the
 * RenderScene is freeable — everything else is a view into it.
 */
#ifndef C_TINYUSD_TYDRA_H
#define C_TINYUSD_TYDRA_H

#include "c-tinyusd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CTinyUSDRenderScene    CTinyUSDRenderScene;
typedef struct CTinyUSDRenderMesh     CTinyUSDRenderMesh;
typedef struct CTinyUSDRenderMaterial CTinyUSDRenderMaterial;
typedef struct CTinyUSDRenderCamera   CTinyUSDRenderCamera;
typedef struct CTinyUSDRenderLight    CTinyUSDRenderLight;
typedef struct CTinyUSDRenderTexture  CTinyUSDRenderTexture;   /* UVTexture */
typedef struct CTinyUSDRenderImage    CTinyUSDRenderImage;     /* TextureImage */
typedef struct CTinyUSDRenderBuffer   CTinyUSDRenderBuffer;    /* BufferData */
typedef struct CTinyUSDAnimationClip  CTinyUSDAnimationClip;
typedef struct CTinyUSDSkelHierarchy  CTinyUSDSkelHierarchy;
typedef struct CTinyUSDRenderNode     CTinyUSDRenderNode;      /* tydra::Node */

/* Convert the Stage to a RenderScene. Returns an owned RenderScene* that
 * the caller must free with c_tinyusd_render_scene_free. On failure returns
 * NULL and fills `err`.
 */
C_TINYUSD_EXPORT CTinyUSDRenderScene *
c_tinyusd_render_scene_convert(const CTinyUSDStage *stage,
                               c_tinyusd_string_t *warn,
                               c_tinyusd_string_t *err);

C_TINYUSD_EXPORT int
c_tinyusd_render_scene_free(CTinyUSDRenderScene *scene);

/* ----- RenderScene content counts + indexed borrow accessors. ----- */

C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_meshes    (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_materials (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_cameras   (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_lights    (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_textures  (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_images    (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_buffers   (const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_animations(const CTinyUSDRenderScene *s);
C_TINYUSD_EXPORT uint64_t c_tinyusd_render_scene_num_skeletons (const CTinyUSDRenderScene *s);

C_TINYUSD_EXPORT const CTinyUSDRenderMesh *
c_tinyusd_render_scene_get_mesh(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDRenderMaterial *
c_tinyusd_render_scene_get_material(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDRenderCamera *
c_tinyusd_render_scene_get_camera(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDRenderLight *
c_tinyusd_render_scene_get_light(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDRenderTexture *
c_tinyusd_render_scene_get_texture(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDRenderImage *
c_tinyusd_render_scene_get_image(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDRenderBuffer *
c_tinyusd_render_scene_get_buffer(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDAnimationClip *
c_tinyusd_render_scene_get_animation(const CTinyUSDRenderScene *s, uint64_t idx);
C_TINYUSD_EXPORT const CTinyUSDSkelHierarchy *
c_tinyusd_render_scene_get_skeleton(const CTinyUSDRenderScene *s, uint64_t idx);

/* ----- RenderMesh accessors. ----- */

C_TINYUSD_EXPORT int c_tinyusd_render_mesh_get_name        (const CTinyUSDRenderMesh *m, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_mesh_get_abs_path    (const CTinyUSDRenderMesh *m, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_mesh_get_display_name(const CTinyUSDRenderMesh *m, c_tinyusd_string_t *out);

/* Zero-copy views into the RenderMesh's internal vectors.
 *
 * Points: tightly packed float32 triples, n_outer = number of vertices.
 * FaceVertexIndices / FaceVertexCounts: uint32 arrays.
 *
 * Each returns 1 on success (even if the array is empty, *out_ptr may be
 * NULL and *out_n may be 0), 0 on error.
 */
C_TINYUSD_EXPORT int
c_tinyusd_render_mesh_get_points(const CTinyUSDRenderMesh *m,
                                 const void **out_ptr,
                                 uint64_t *out_n);
C_TINYUSD_EXPORT int
c_tinyusd_render_mesh_get_face_vertex_indices(const CTinyUSDRenderMesh *m,
                                              const void **out_ptr,
                                              uint64_t *out_n);
C_TINYUSD_EXPORT int
c_tinyusd_render_mesh_get_face_vertex_counts(const CTinyUSDRenderMesh *m,
                                             const void **out_ptr,
                                             uint64_t *out_n);

/* Generic vertex-attribute view for normals / texcoords / tangents.
 *   *out_n_outer       number of vertex items
 *   *out_n_inner       components per item (2, 3, 4, 1)
 *   *out_component_size size of each inner component in bytes
 *   *out_format         single-char struct format for the component
 *
 * Returns 1 on success, 0 if the attribute is empty or unsupported.
 */
C_TINYUSD_EXPORT int
c_tinyusd_render_mesh_get_normals(const CTinyUSDRenderMesh *m,
                                  const void **out_ptr,
                                  uint64_t *out_n_outer,
                                  uint32_t *out_n_inner,
                                  uint32_t *out_component_size,
                                  const char **out_format);

/* Texcoord slots (USD can declare multiple UV sets). */
C_TINYUSD_EXPORT uint32_t
c_tinyusd_render_mesh_num_texcoord_slots(const CTinyUSDRenderMesh *m);
/* Fill `out_ids` with up to `cap` slot IDs; returns actual number written. */
C_TINYUSD_EXPORT uint32_t
c_tinyusd_render_mesh_get_texcoord_slot_ids(const CTinyUSDRenderMesh *m,
                                            uint32_t *out_ids, uint32_t cap);
C_TINYUSD_EXPORT int
c_tinyusd_render_mesh_get_texcoord(const CTinyUSDRenderMesh *m, uint32_t slot_id,
                                   const void **out_ptr,
                                   uint64_t *out_n_outer,
                                   uint32_t *out_n_inner,
                                   uint32_t *out_component_size,
                                   const char **out_format);

/* Display color (flat RGB). */
C_TINYUSD_EXPORT int
c_tinyusd_render_mesh_get_display_color(const CTinyUSDRenderMesh *m,
                                        float out[3]);

/* Material index assigned to this mesh; -1 if unbound. */
C_TINYUSD_EXPORT int32_t
c_tinyusd_render_mesh_material_id(const CTinyUSDRenderMesh *m);

/* 1 if right-handed / double-sided, 0 otherwise. */
C_TINYUSD_EXPORT int c_tinyusd_render_mesh_is_right_handed(const CTinyUSDRenderMesh *m);
C_TINYUSD_EXPORT int c_tinyusd_render_mesh_is_double_sided(const CTinyUSDRenderMesh *m);

/* ----- RenderMaterial accessors. ----- */

C_TINYUSD_EXPORT int c_tinyusd_render_material_get_name    (const CTinyUSDRenderMaterial *mat, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_material_get_abs_path(const CTinyUSDRenderMaterial *mat, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_material_has_preview_surface(const CTinyUSDRenderMaterial *mat);
C_TINYUSD_EXPORT int c_tinyusd_render_material_has_open_pbr       (const CTinyUSDRenderMaterial *mat);

/* ----- RenderCamera accessors. ----- */

C_TINYUSD_EXPORT int c_tinyusd_render_camera_get_name    (const CTinyUSDRenderCamera *cam, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_camera_get_abs_path(const CTinyUSDRenderCamera *cam, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT float c_tinyusd_render_camera_znear(const CTinyUSDRenderCamera *cam);
C_TINYUSD_EXPORT float c_tinyusd_render_camera_zfar (const CTinyUSDRenderCamera *cam);
C_TINYUSD_EXPORT float c_tinyusd_render_camera_focal_length(const CTinyUSDRenderCamera *cam);
C_TINYUSD_EXPORT float c_tinyusd_render_camera_horizontal_aperture(const CTinyUSDRenderCamera *cam);
C_TINYUSD_EXPORT float c_tinyusd_render_camera_vertical_aperture  (const CTinyUSDRenderCamera *cam);
/* 0 = perspective, 1 = orthographic */
C_TINYUSD_EXPORT int c_tinyusd_render_camera_projection(const CTinyUSDRenderCamera *cam);

/* ----- RenderLight accessors. ----- */

typedef enum {
  C_TINYUSD_LIGHT_POINT    = 0,
  C_TINYUSD_LIGHT_SPHERE   = 1,
  C_TINYUSD_LIGHT_DISK     = 2,
  C_TINYUSD_LIGHT_RECT     = 3,
  C_TINYUSD_LIGHT_CYLINDER = 4,
  C_TINYUSD_LIGHT_DISTANT  = 5,
  C_TINYUSD_LIGHT_DOME     = 6,
  C_TINYUSD_LIGHT_GEOMETRY = 7,
  C_TINYUSD_LIGHT_PORTAL   = 8,
  C_TINYUSD_LIGHT_UNKNOWN  = 0xFFFF
} CTinyUSDLightType;

C_TINYUSD_EXPORT int c_tinyusd_render_light_get_name    (const CTinyUSDRenderLight *l, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_light_get_abs_path(const CTinyUSDRenderLight *l, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT CTinyUSDLightType c_tinyusd_render_light_type(const CTinyUSDRenderLight *l);
C_TINYUSD_EXPORT float c_tinyusd_render_light_intensity(const CTinyUSDRenderLight *l);
C_TINYUSD_EXPORT int   c_tinyusd_render_light_get_color(const CTinyUSDRenderLight *l, float out[3]);

/* ----- PreviewSurface shader (flat struct readback). ----- */

/* Single-call dump of every authored UsdPreviewSurface input. -1 in any
 * `*_tex` field means "no texture connection"; otherwise the value is an
 * index into RenderScene::textures().
 */
typedef struct {
  int32_t has_shader;              /* 1 if mat has a UsdPreviewSurface. */
  int32_t use_specular_workflow;

  float  diffuse_color[3];         int32_t diffuse_color_tex;
  float  emissive_color[3];        int32_t emissive_color_tex;
  float  specular_color[3];        int32_t specular_color_tex;
  float  metallic;                 int32_t metallic_tex;
  float  roughness;                int32_t roughness_tex;
  float  clearcoat;                int32_t clearcoat_tex;
  float  clearcoat_roughness;      int32_t clearcoat_roughness_tex;
  float  opacity;                  int32_t opacity_tex;
  float  opacity_threshold;        int32_t opacity_threshold_tex;
  float  ior;                      int32_t ior_tex;
  float  normal[3];                int32_t normal_tex;
  float  displacement;             int32_t displacement_tex;
  float  occlusion;                int32_t occlusion_tex;
} CTinyUSDPreviewSurface;

C_TINYUSD_EXPORT int
c_tinyusd_render_material_get_preview_surface(
    const CTinyUSDRenderMaterial *mat, CTinyUSDPreviewSurface *out);

/* ----- OpenPBRSurface shader (flat struct readback). -----
 *
 * Single-call dump of every authored MaterialX OpenPBR input. -1 in any
 * `*_tex` field means "no texture connection". The struct mirrors
 * tydra::OpenPBRSurfaceShader field-for-field so new additions in the C++
 * core must be reflected here.
 */

typedef struct {
  int32_t has_shader;                /* 1 if mat has an OpenPBR shader. */

  /* Base layer. */
  float  base_weight;                int32_t base_weight_tex;
  float  base_color[3];              int32_t base_color_tex;
  float  base_roughness;             int32_t base_roughness_tex;
  float  base_metalness;             int32_t base_metalness_tex;
  float  base_diffuse_roughness;     int32_t base_diffuse_roughness_tex;

  /* Specular layer. */
  float  specular_weight;            int32_t specular_weight_tex;
  float  specular_color[3];          int32_t specular_color_tex;
  float  specular_roughness;         int32_t specular_roughness_tex;
  float  specular_ior;               int32_t specular_ior_tex;
  float  specular_ior_level;         int32_t specular_ior_level_tex;
  float  specular_anisotropy;        int32_t specular_anisotropy_tex;
  float  specular_rotation;          int32_t specular_rotation_tex;
  float  specular_roughness_anisotropy; int32_t specular_roughness_anisotropy_tex;

  /* Transmission. */
  float  transmission_weight;        int32_t transmission_weight_tex;
  float  transmission_color[3];      int32_t transmission_color_tex;
  float  transmission_depth;         int32_t transmission_depth_tex;
  float  transmission_scatter[3];    int32_t transmission_scatter_tex;
  float  transmission_scatter_anisotropy; int32_t transmission_scatter_anisotropy_tex;
  float  transmission_dispersion;    int32_t transmission_dispersion_tex;
  float  transmission_dispersion_abbe_number; int32_t transmission_dispersion_abbe_number_tex;
  float  transmission_dispersion_scale; int32_t transmission_dispersion_scale_tex;

  /* Subsurface scattering. */
  float  subsurface_weight;          int32_t subsurface_weight_tex;
  float  subsurface_color[3];        int32_t subsurface_color_tex;
  float  subsurface_radius;          int32_t subsurface_radius_tex;
  float  subsurface_radius_scale[3]; int32_t subsurface_radius_scale_tex;
  float  subsurface_scale;           int32_t subsurface_scale_tex;
  float  subsurface_anisotropy;      int32_t subsurface_anisotropy_tex;
  float  subsurface_scatter_anisotropy; int32_t subsurface_scatter_anisotropy_tex;

  /* Sheen. */
  float  sheen_weight;               int32_t sheen_weight_tex;
  float  sheen_color[3];             int32_t sheen_color_tex;
  float  sheen_roughness;            int32_t sheen_roughness_tex;

  /* Fuzz. */
  float  fuzz_weight;                int32_t fuzz_weight_tex;
  float  fuzz_color[3];              int32_t fuzz_color_tex;
  float  fuzz_roughness;             int32_t fuzz_roughness_tex;

  /* Thin film. */
  float  thin_film_weight;           int32_t thin_film_weight_tex;
  float  thin_film_thickness;        int32_t thin_film_thickness_tex;
  float  thin_film_ior;              int32_t thin_film_ior_tex;

  /* Coat. */
  float  coat_weight;                int32_t coat_weight_tex;
  float  coat_color[3];              int32_t coat_color_tex;
  float  coat_roughness;             int32_t coat_roughness_tex;
  float  coat_anisotropy;            int32_t coat_anisotropy_tex;
  float  coat_rotation;              int32_t coat_rotation_tex;
  float  coat_ior;                   int32_t coat_ior_tex;
  float  coat_affect_color;          int32_t coat_affect_color_tex;
  float  coat_affect_roughness;      int32_t coat_affect_roughness_tex;
  float  coat_roughness_anisotropy;  int32_t coat_roughness_anisotropy_tex;
  float  coat_darkening;             int32_t coat_darkening_tex;

  /* Emission. */
  float  emission_luminance;         int32_t emission_luminance_tex;
  float  emission_color[3];          int32_t emission_color_tex;

  /* Geometry modifiers. */
  float  opacity;                    int32_t opacity_tex;
  float  normal[3];                  int32_t normal_tex;
  float  tangent[3];                 int32_t tangent_tex;

  /* Plain scalars (no ShaderParam). */
  float  tangent_rotation;
  float  normal_map_scale;

  /* Coat normal / tangent separates. */
  float  coat_normal[3];             int32_t coat_normal_tex;
  float  coat_tangent[3];            int32_t coat_tangent_tex;
  float  coat_tangent_rotation;
  float  coat_normal_map_scale;
} CTinyUSDOpenPBR;

C_TINYUSD_EXPORT int
c_tinyusd_render_material_get_openpbr(
    const CTinyUSDRenderMaterial *mat, CTinyUSDOpenPBR *out);

/* Returns a pointer to the MaterialX node-graph JSON string for this
 * material. The pointer is borrowed from the OpenPBR shader's internal
 * storage; *out_len is the byte length (not including a trailing NUL).
 * Returns 0 if the material has no OpenPBR shader or the graph is empty.
 */
C_TINYUSD_EXPORT int
c_tinyusd_render_material_get_node_graph_json(
    const CTinyUSDRenderMaterial *mat,
    const char **out_ptr, uint64_t *out_len);

/* ----- RenderTexture (UVTexture). ----- */

C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_name    (const CTinyUSDRenderTexture *t, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_abs_path(const CTinyUSDRenderTexture *t, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_varname_uv(const CTinyUSDRenderTexture *t, c_tinyusd_string_t *out);
/* 0 = CLAMP_TO_EDGE, 1 = REPEAT, 2 = MIRROR, 3 = CLAMP_TO_BORDER */
C_TINYUSD_EXPORT int c_tinyusd_render_texture_wrap_s(const CTinyUSDRenderTexture *t);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_wrap_t(const CTinyUSDRenderTexture *t);
C_TINYUSD_EXPORT int64_t c_tinyusd_render_texture_image_id(const CTinyUSDRenderTexture *t);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_bias (const CTinyUSDRenderTexture *t, float out[4]);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_scale(const CTinyUSDRenderTexture *t, float out[4]);

/* Connected output channel picked off the shader graph:
 *   0 = R, 1 = G, 2 = B, 3 = A, 4 = RGB, 5 = RGBA
 */
C_TINYUSD_EXPORT int c_tinyusd_render_texture_output_channel(const CTinyUSDRenderTexture *t);

/* Fallback uv (vec4) used when no UV primvar is bound. */
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_fallback_uv(const CTinyUSDRenderTexture *t, float out[4]);

/* UsdTransform2d support.
 *   has_transform2d == 1 means the rotation/scale/translation fields are
 *   authored; transform is the corresponding 3x3 row-major float matrix.
 */
C_TINYUSD_EXPORT int c_tinyusd_render_texture_has_transform2d(const CTinyUSDRenderTexture *t);
C_TINYUSD_EXPORT float c_tinyusd_render_texture_tx_rotation(const CTinyUSDRenderTexture *t);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_tx_scale(const CTinyUSDRenderTexture *t, float out[2]);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_tx_translation(const CTinyUSDRenderTexture *t, float out[2]);
C_TINYUSD_EXPORT int c_tinyusd_render_texture_get_transform(const CTinyUSDRenderTexture *t, float out[9]);

/* ----- RenderImage (TextureImage). ----- */

C_TINYUSD_EXPORT int c_tinyusd_render_image_get_asset_identifier(const CTinyUSDRenderImage *i, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_image_width   (const CTinyUSDRenderImage *i);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_image_height  (const CTinyUSDRenderImage *i);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_image_channels(const CTinyUSDRenderImage *i);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_image_miplevel(const CTinyUSDRenderImage *i);
C_TINYUSD_EXPORT int64_t c_tinyusd_render_image_buffer_id(const CTinyUSDRenderImage *i);
/* 0 = sRGB, 1 = Linear, 2 = Raw, 3 = ACES2065_1, ... — matches tydra::ColorSpace */
C_TINYUSD_EXPORT int c_tinyusd_render_image_color_space(const CTinyUSDRenderImage *i);
C_TINYUSD_EXPORT int c_tinyusd_render_image_is_decoded (const CTinyUSDRenderImage *i);
/* Component type of the texel data: 0 = UInt8, 1 = Int8, 2 = UInt16,
 * 3 = Int16, 4 = UInt32, 5 = Int32, 6 = Half, 7 = Float, 8 = Double. */
C_TINYUSD_EXPORT int c_tinyusd_render_image_texel_component_type(const CTinyUSDRenderImage *i);

/* ----- RenderBuffer (BufferData): raw byte storage shared by
 * images/textures. ----- */

C_TINYUSD_EXPORT int c_tinyusd_render_buffer_component_type(const CTinyUSDRenderBuffer *b);
/* Returns a pointer into the buffer's byte storage and the number of bytes.
 * The buffer stays alive as long as its owning RenderScene is alive.
 */
C_TINYUSD_EXPORT int
c_tinyusd_render_buffer_get_bytes(const CTinyUSDRenderBuffer *b,
                                  const void **out_ptr, uint64_t *out_nbytes);

/* ----- AnimationClip. ----- */

C_TINYUSD_EXPORT int c_tinyusd_animation_get_name    (const CTinyUSDAnimationClip *a, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_animation_get_abs_path(const CTinyUSDAnimationClip *a, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT float c_tinyusd_animation_duration(const CTinyUSDAnimationClip *a);
C_TINYUSD_EXPORT uint64_t c_tinyusd_animation_num_samplers(const CTinyUSDAnimationClip *a);
C_TINYUSD_EXPORT uint64_t c_tinyusd_animation_num_channels(const CTinyUSDAnimationClip *a);
C_TINYUSD_EXPORT int c_tinyusd_animation_has_skeletal(const CTinyUSDAnimationClip *a);
C_TINYUSD_EXPORT int c_tinyusd_animation_has_node    (const CTinyUSDAnimationClip *a);

/* Sampler access — flat times / values arrays plus interpolation kind.
 * Interpolation: 0 = Step, 1 = Linear (default), 2 = CubicSpline. */
C_TINYUSD_EXPORT int
c_tinyusd_animation_get_sampler(const CTinyUSDAnimationClip *a, uint64_t idx,
                                const float **out_times,  uint64_t *out_n_times,
                                const float **out_values, uint64_t *out_n_values,
                                int *out_interpolation);

/* Channel fields.
 *   *out_path         0 = Translation, 1 = Rotation, 2 = Scale, 3 = Weights
 *   *out_target_type  0 = SceneNode, 1 = SkeletonJoint
 */
C_TINYUSD_EXPORT int
c_tinyusd_animation_get_channel(const CTinyUSDAnimationClip *a, uint64_t idx,
                                int *out_path, int *out_target_type,
                                int32_t *out_target_node,
                                int32_t *out_skeleton_id,
                                int32_t *out_joint_id,
                                int32_t *out_sampler);

/* ----- SkelHierarchy. ----- */

C_TINYUSD_EXPORT int c_tinyusd_skel_get_name    (const CTinyUSDSkelHierarchy *s, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_skel_get_abs_path(const CTinyUSDSkelHierarchy *s, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT uint64_t c_tinyusd_skel_num_joints(const CTinyUSDSkelHierarchy *s);
C_TINYUSD_EXPORT int c_tinyusd_skel_default_anim_id(const CTinyUSDSkelHierarchy *s);

/* Zero-copy parent indices: int32[N]. */
C_TINYUSD_EXPORT int
c_tinyusd_skel_get_parent_joint_indices(const CTinyUSDSkelHierarchy *s,
                                        const void **out_ptr, uint64_t *out_n);

/* Bind / rest joint transforms as flat float64 matrices (N * 16). */
C_TINYUSD_EXPORT int
c_tinyusd_skel_get_bind_transforms(const CTinyUSDSkelHierarchy *s,
                                   const void **out_ptr, uint64_t *out_n);
C_TINYUSD_EXPORT int
c_tinyusd_skel_get_rest_transforms(const CTinyUSDSkelHierarchy *s,
                                   const void **out_ptr, uint64_t *out_n);

/* ----- RenderNode (scene graph nodes). ----- */

/* Node high-level category:
 *   0 = Group, 1 = Geom, 2 = Light, 3 = Camera, 4 = Material, 5 = Skeleton
 */
typedef enum {
  C_TINYUSD_NODE_CATEGORY_GROUP    = 0,
  C_TINYUSD_NODE_CATEGORY_GEOM     = 1,
  C_TINYUSD_NODE_CATEGORY_LIGHT    = 2,
  C_TINYUSD_NODE_CATEGORY_CAMERA   = 3,
  C_TINYUSD_NODE_CATEGORY_MATERIAL = 4,
  C_TINYUSD_NODE_CATEGORY_SKELETON = 5
} CTinyUSDNodeCategory;

/* Node specific type within a category — matches tydra::NodeType:
 *   0 Xform, 1 Mesh, 2 Camera, 3 SkelRoot, 4 Skeleton, 5 PointLight,
 *   6 DirectionalLight, 7 EnvmapLight, 8 RectLight, 9 DiskLight,
 *   10 CylinderLight, 11 GeometryLight
 */

C_TINYUSD_EXPORT uint64_t
c_tinyusd_render_scene_num_nodes(const CTinyUSDRenderScene *s);

/* Index into the RenderScene's top-level node list; the returned pointer is
 * a borrow stable for the scene's lifetime. */
C_TINYUSD_EXPORT const CTinyUSDRenderNode *
c_tinyusd_render_scene_get_node(const CTinyUSDRenderScene *s, uint64_t idx);

/* Default root node index (`default_root_node` on RenderScene). */
C_TINYUSD_EXPORT uint32_t
c_tinyusd_render_scene_default_root_node(const CTinyUSDRenderScene *s);

C_TINYUSD_EXPORT int c_tinyusd_render_node_get_name        (const CTinyUSDRenderNode *n, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_node_get_abs_path    (const CTinyUSDRenderNode *n, c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int c_tinyusd_render_node_get_display_name(const CTinyUSDRenderNode *n, c_tinyusd_string_t *out);

C_TINYUSD_EXPORT int c_tinyusd_render_node_category (const CTinyUSDRenderNode *n);
C_TINYUSD_EXPORT int c_tinyusd_render_node_node_type(const CTinyUSDRenderNode *n);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_node_content_id(const CTinyUSDRenderNode *n);

C_TINYUSD_EXPORT int c_tinyusd_render_node_is_instance(const CTinyUSDRenderNode *n);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_node_prototype_index(const CTinyUSDRenderNode *n);
C_TINYUSD_EXPORT int32_t c_tinyusd_render_node_instance_id   (const CTinyUSDRenderNode *n);

C_TINYUSD_EXPORT uint64_t c_tinyusd_render_node_num_children(const CTinyUSDRenderNode *n);
C_TINYUSD_EXPORT const CTinyUSDRenderNode *
c_tinyusd_render_node_get_child(const CTinyUSDRenderNode *n, uint64_t idx);

/* Local/global 4x4 transforms as row-major double[16]. */
C_TINYUSD_EXPORT int
c_tinyusd_render_node_get_local_matrix(const CTinyUSDRenderNode *n, double out[16]);
C_TINYUSD_EXPORT int
c_tinyusd_render_node_get_global_matrix(const CTinyUSDRenderNode *n, double out[16]);

C_TINYUSD_EXPORT int c_tinyusd_render_node_has_reset_xform(const CTinyUSDRenderNode *n);

#ifdef __cplusplus
}
#endif

#endif /* C_TINYUSD_TYDRA_H */
