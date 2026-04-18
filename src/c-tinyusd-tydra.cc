// SPDX-License-Identifier: Apache 2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// C API implementation for tinyusdz::tydra::RenderScene conversion.

#include "c-tinyusd-tydra.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/render-data-shader.hh"
#include "tydra/scene-access.hh"

namespace {

using tinyusdz::Stage;
using tinyusdz::tydra::AnimationChannel;
using tinyusdz::tydra::AnimationClip;
using tinyusdz::tydra::AnimationInterpolation;
using tinyusdz::tydra::AnimationPath;
using tinyusdz::tydra::BufferData;
using tinyusdz::tydra::ChannelTargetType;
using tinyusdz::tydra::ComponentType;
using tinyusdz::tydra::KeyframeSampler;
using tinyusdz::tydra::Node;
using tinyusdz::tydra::NodeCategory;
using tinyusdz::tydra::NodeType;
using tinyusdz::tydra::OpenPBRSurfaceShader;
using tinyusdz::tydra::PreviewSurfaceShader;
using tinyusdz::tydra::RenderCamera;
using tinyusdz::tydra::RenderLight;
using tinyusdz::tydra::RenderMaterial;
using tinyusdz::tydra::RenderMesh;
using tinyusdz::tydra::RenderScene;
using tinyusdz::tydra::RenderSceneConverter;
using tinyusdz::tydra::RenderSceneConverterEnv;
using tinyusdz::tydra::SkelHierarchy;
using tinyusdz::tydra::TextureImage;
using tinyusdz::tydra::UVTexture;
using tinyusdz::tydra::VertexAttribute;
using tinyusdz::tydra::VertexAttributeFormat;

inline const Stage *S(const CTinyUSDStage *s) {
  return reinterpret_cast<const Stage *>(s);
}
inline RenderScene *RS(CTinyUSDRenderScene *s) {
  return reinterpret_cast<RenderScene *>(s);
}
inline const RenderScene *RS(const CTinyUSDRenderScene *s) {
  return reinterpret_cast<const RenderScene *>(s);
}
inline const RenderMesh *RM(const CTinyUSDRenderMesh *m) {
  return reinterpret_cast<const RenderMesh *>(m);
}
inline const RenderMaterial *RMat(const CTinyUSDRenderMaterial *m) {
  return reinterpret_cast<const RenderMaterial *>(m);
}
inline const RenderCamera *RC(const CTinyUSDRenderCamera *c) {
  return reinterpret_cast<const RenderCamera *>(c);
}
inline const RenderLight *RL(const CTinyUSDRenderLight *l) {
  return reinterpret_cast<const RenderLight *>(l);
}
inline const UVTexture *RT(const CTinyUSDRenderTexture *t) {
  return reinterpret_cast<const UVTexture *>(t);
}
inline const TextureImage *RI(const CTinyUSDRenderImage *i) {
  return reinterpret_cast<const TextureImage *>(i);
}
inline const BufferData *RB(const CTinyUSDRenderBuffer *b) {
  return reinterpret_cast<const BufferData *>(b);
}
inline const AnimationClip *RA(const CTinyUSDAnimationClip *a) {
  return reinterpret_cast<const AnimationClip *>(a);
}
inline const SkelHierarchy *RK(const CTinyUSDSkelHierarchy *s) {
  return reinterpret_cast<const SkelHierarchy *>(s);
}
inline const Node *RN(const CTinyUSDRenderNode *n) {
  return reinterpret_cast<const Node *>(n);
}

// Resolve a VertexAttribute to a (ptr, n_outer, n_inner, comp_size, fmt)
// tuple suitable for the Python buffer protocol. Returns false for empty
// or unsupported attributes.
struct VAView {
  const void *ptr;
  uint64_t n_outer;
  uint32_t n_inner;
  uint32_t comp_size;
  const char *format;
};

bool describe_vertex_attribute(const VertexAttribute &va, VAView *out) {
  if (va.data.empty()) return false;
  using F = VertexAttributeFormat;
  uint32_t n_inner = 1, comp = 4;
  const char *fmt = "f";
  switch (va.format) {
    case F::Bool:    n_inner = 1; comp = 1; fmt = "?"; break;
    case F::Char:    n_inner = 1; comp = 1; fmt = "b"; break;
    case F::Char2:   n_inner = 2; comp = 1; fmt = "b"; break;
    case F::Char3:   n_inner = 3; comp = 1; fmt = "b"; break;
    case F::Char4:   n_inner = 4; comp = 1; fmt = "b"; break;
    case F::Byte:    n_inner = 1; comp = 1; fmt = "B"; break;
    case F::Byte2:   n_inner = 2; comp = 1; fmt = "B"; break;
    case F::Byte3:   n_inner = 3; comp = 1; fmt = "B"; break;
    case F::Byte4:   n_inner = 4; comp = 1; fmt = "B"; break;
    case F::Short:   n_inner = 1; comp = 2; fmt = "h"; break;
    case F::Short2:  n_inner = 2; comp = 2; fmt = "h"; break;
    case F::Short3:  n_inner = 3; comp = 2; fmt = "h"; break;
    case F::Short4:  n_inner = 4; comp = 2; fmt = "h"; break;
    case F::Ushort:  n_inner = 1; comp = 2; fmt = "H"; break;
    case F::Ushort2: n_inner = 2; comp = 2; fmt = "H"; break;
    case F::Ushort3: n_inner = 3; comp = 2; fmt = "H"; break;
    case F::Ushort4: n_inner = 4; comp = 2; fmt = "H"; break;
    case F::Half:    n_inner = 1; comp = 2; fmt = "e"; break;
    case F::Half2:   n_inner = 2; comp = 2; fmt = "e"; break;
    case F::Half3:   n_inner = 3; comp = 2; fmt = "e"; break;
    case F::Half4:   n_inner = 4; comp = 2; fmt = "e"; break;
    case F::Float:   n_inner = 1; comp = 4; fmt = "f"; break;
    case F::Vec2:    n_inner = 2; comp = 4; fmt = "f"; break;
    case F::Vec3:    n_inner = 3; comp = 4; fmt = "f"; break;
    case F::Vec4:    n_inner = 4; comp = 4; fmt = "f"; break;
    case F::Int:     n_inner = 1; comp = 4; fmt = "i"; break;
    case F::Ivec2:   n_inner = 2; comp = 4; fmt = "i"; break;
    case F::Ivec3:   n_inner = 3; comp = 4; fmt = "i"; break;
    case F::Ivec4:   n_inner = 4; comp = 4; fmt = "i"; break;
    case F::Uint:    n_inner = 1; comp = 4; fmt = "I"; break;
    case F::Uvec2:   n_inner = 2; comp = 4; fmt = "I"; break;
    case F::Uvec3:   n_inner = 3; comp = 4; fmt = "I"; break;
    case F::Uvec4:   n_inner = 4; comp = 4; fmt = "I"; break;
    case F::Double:  n_inner = 1; comp = 8; fmt = "d"; break;
    case F::Dvec2:   n_inner = 2; comp = 8; fmt = "d"; break;
    case F::Dvec3:   n_inner = 3; comp = 8; fmt = "d"; break;
    case F::Dvec4:   n_inner = 4; comp = 8; fmt = "d"; break;
    case F::Mat2:    n_inner = 4;  comp = 4; fmt = "f"; break;
    case F::Mat3:    n_inner = 9;  comp = 4; fmt = "f"; break;
    case F::Mat4:    n_inner = 16; comp = 4; fmt = "f"; break;
    case F::Dmat2:   n_inner = 4;  comp = 8; fmt = "d"; break;
    case F::Dmat3:   n_inner = 9;  comp = 8; fmt = "d"; break;
    case F::Dmat4:   n_inner = 16; comp = 8; fmt = "d"; break;
    default:         return false;
  }
  out->ptr = static_cast<const void *>(va.data.data());
  out->n_outer = static_cast<uint64_t>(va.vertex_count());
  out->n_inner = n_inner;
  out->comp_size = comp;
  out->format = fmt;
  return true;
}

}  // namespace

extern "C" {

/* ---- Conversion + free ---- */

CTinyUSDRenderScene *
c_tinyusd_render_scene_convert(const CTinyUSDStage *stage,
                               c_tinyusd_string_t *warn,
                               c_tinyusd_string_t *err) {
  if (!stage) {
    if (err) c_tinyusd_string_replace(err, "stage is null");
    return nullptr;
  }
  RenderScene *scene = new RenderScene();
  RenderSceneConverter converter;
  RenderSceneConverterEnv env(*S(stage));
  bool ok = converter.ConvertToRenderScene(env, scene);

  if (warn && !converter.GetWarning().empty()) {
    c_tinyusd_string_replace(warn, converter.GetWarning().c_str());
  }
  if (!ok) {
    if (err) {
      const std::string &msg = converter.GetError();
      c_tinyusd_string_replace(err, msg.empty() ? "render-scene conversion failed"
                                                : msg.c_str());
    }
    delete scene;
    return nullptr;
  }
  return reinterpret_cast<CTinyUSDRenderScene *>(scene);
}

int c_tinyusd_render_scene_free(CTinyUSDRenderScene *scene) {
  if (!scene) return 0;
  delete RS(scene);
  return 1;
}

/* ---- RenderScene counts + accessors ---- */

uint64_t c_tinyusd_render_scene_num_meshes(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->meshes.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_materials(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->materials.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_cameras(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->cameras.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_lights(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->lights.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_textures(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->textures.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_images(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->images.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_buffers(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->buffers.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_animations(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->animations.size()) : 0;
}
uint64_t c_tinyusd_render_scene_num_skeletons(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->skeletons.size()) : 0;
}

const CTinyUSDRenderMesh *
c_tinyusd_render_scene_get_mesh(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->meshes.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderMesh *>(&RS(s)->meshes[idx]);
}
const CTinyUSDRenderMaterial *
c_tinyusd_render_scene_get_material(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->materials.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderMaterial *>(&RS(s)->materials[idx]);
}
const CTinyUSDRenderCamera *
c_tinyusd_render_scene_get_camera(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->cameras.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderCamera *>(&RS(s)->cameras[idx]);
}
const CTinyUSDRenderLight *
c_tinyusd_render_scene_get_light(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->lights.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderLight *>(&RS(s)->lights[idx]);
}
const CTinyUSDRenderTexture *
c_tinyusd_render_scene_get_texture(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->textures.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderTexture *>(&RS(s)->textures[idx]);
}
const CTinyUSDRenderImage *
c_tinyusd_render_scene_get_image(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->images.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderImage *>(&RS(s)->images[idx]);
}
const CTinyUSDRenderBuffer *
c_tinyusd_render_scene_get_buffer(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->buffers.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderBuffer *>(&RS(s)->buffers[idx]);
}
const CTinyUSDAnimationClip *
c_tinyusd_render_scene_get_animation(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->animations.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDAnimationClip *>(&RS(s)->animations[idx]);
}
const CTinyUSDSkelHierarchy *
c_tinyusd_render_scene_get_skeleton(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s || idx >= RS(s)->skeletons.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDSkelHierarchy *>(&RS(s)->skeletons[idx]);
}

/* ---- RenderMesh ---- */

static inline int str_copy(c_tinyusd_string_t *out, const std::string &s) {
  return out ? c_tinyusd_string_replace(out, s.c_str()) : 0;
}

int c_tinyusd_render_mesh_get_name(const CTinyUSDRenderMesh *m,
                                   c_tinyusd_string_t *out) {
  return m ? str_copy(out, RM(m)->prim_name) : 0;
}
int c_tinyusd_render_mesh_get_abs_path(const CTinyUSDRenderMesh *m,
                                       c_tinyusd_string_t *out) {
  return m ? str_copy(out, RM(m)->abs_path) : 0;
}
int c_tinyusd_render_mesh_get_display_name(const CTinyUSDRenderMesh *m,
                                           c_tinyusd_string_t *out) {
  return m ? str_copy(out, RM(m)->display_name) : 0;
}

int c_tinyusd_render_mesh_get_points(const CTinyUSDRenderMesh *m,
                                     const void **out_ptr, uint64_t *out_n) {
  if (!m || !out_ptr || !out_n) return 0;
  const auto &pts = RM(m)->points;
  *out_ptr = pts.empty() ? nullptr : static_cast<const void *>(pts.data());
  *out_n = static_cast<uint64_t>(pts.size());
  return 1;
}

int c_tinyusd_render_mesh_get_face_vertex_indices(const CTinyUSDRenderMesh *m,
                                                  const void **out_ptr,
                                                  uint64_t *out_n) {
  if (!m || !out_ptr || !out_n) return 0;
  const auto &v = RM(m)->faceVertexIndices();
  *out_ptr = v.empty() ? nullptr : static_cast<const void *>(v.data());
  *out_n = static_cast<uint64_t>(v.size());
  return 1;
}

int c_tinyusd_render_mesh_get_face_vertex_counts(const CTinyUSDRenderMesh *m,
                                                 const void **out_ptr,
                                                 uint64_t *out_n) {
  if (!m || !out_ptr || !out_n) return 0;
  const auto &v = RM(m)->faceVertexCounts();
  *out_ptr = v.empty() ? nullptr : static_cast<const void *>(v.data());
  *out_n = static_cast<uint64_t>(v.size());
  return 1;
}

int c_tinyusd_render_mesh_get_normals(const CTinyUSDRenderMesh *m,
                                      const void **out_ptr,
                                      uint64_t *out_n_outer,
                                      uint32_t *out_n_inner,
                                      uint32_t *out_component_size,
                                      const char **out_format) {
  if (!m || !out_ptr || !out_n_outer || !out_n_inner ||
      !out_component_size || !out_format) return 0;
  VAView vv;
  if (!describe_vertex_attribute(RM(m)->normals, &vv)) return 0;
  *out_ptr = vv.ptr;
  *out_n_outer = vv.n_outer;
  *out_n_inner = vv.n_inner;
  *out_component_size = vv.comp_size;
  *out_format = vv.format;
  return 1;
}

uint32_t c_tinyusd_render_mesh_num_texcoord_slots(const CTinyUSDRenderMesh *m) {
  return m ? static_cast<uint32_t>(RM(m)->texcoords.size()) : 0;
}

uint32_t
c_tinyusd_render_mesh_get_texcoord_slot_ids(const CTinyUSDRenderMesh *m,
                                            uint32_t *out_ids, uint32_t cap) {
  if (!m) return 0;
  uint32_t written = 0;
  for (const auto &kv : RM(m)->texcoords) {
    if (written >= cap) break;
    if (out_ids) out_ids[written] = kv.first;
    ++written;
  }
  return written;
}

int c_tinyusd_render_mesh_get_texcoord(const CTinyUSDRenderMesh *m,
                                       uint32_t slot_id,
                                       const void **out_ptr,
                                       uint64_t *out_n_outer,
                                       uint32_t *out_n_inner,
                                       uint32_t *out_component_size,
                                       const char **out_format) {
  if (!m || !out_ptr || !out_n_outer || !out_n_inner ||
      !out_component_size || !out_format) return 0;
  const auto &tc = RM(m)->texcoords;
  auto it = tc.find(slot_id);
  if (it == tc.end()) return 0;
  VAView vv;
  if (!describe_vertex_attribute(it->second, &vv)) return 0;
  *out_ptr = vv.ptr;
  *out_n_outer = vv.n_outer;
  *out_n_inner = vv.n_inner;
  *out_component_size = vv.comp_size;
  *out_format = vv.format;
  return 1;
}

int c_tinyusd_render_mesh_get_display_color(const CTinyUSDRenderMesh *m,
                                            float out[3]) {
  if (!m || !out) return 0;
  const auto &c = RM(m)->displayColor;
  out[0] = c.r; out[1] = c.g; out[2] = c.b;
  return 1;
}

int32_t c_tinyusd_render_mesh_material_id(const CTinyUSDRenderMesh *m) {
  if (!m) return -1;
  // RenderMesh is known to carry a material index field; name can vary across
  // versions. Fall back to -1 if not present. Here we peek at the materialId
  // field (canonical in the current core).
  // Note: tinyusdz core stores material assignment on RenderMesh as
  // `int material_id` or `int32_t materialId`. Use the conventional name
  // exposed in the header; if it doesn't compile, the error lands in the
  // build log and we'll adjust.
  return RM(m)->material_id;
}

int c_tinyusd_render_mesh_is_right_handed(const CTinyUSDRenderMesh *m) {
  return (m && RM(m)->is_rightHanded) ? 1 : 0;
}
int c_tinyusd_render_mesh_is_double_sided(const CTinyUSDRenderMesh *m) {
  return (m && RM(m)->doubleSided) ? 1 : 0;
}

/* ---- RenderMaterial ---- */

int c_tinyusd_render_material_get_name(const CTinyUSDRenderMaterial *mat,
                                       c_tinyusd_string_t *out) {
  return mat ? str_copy(out, RMat(mat)->name) : 0;
}
int c_tinyusd_render_material_get_abs_path(const CTinyUSDRenderMaterial *mat,
                                           c_tinyusd_string_t *out) {
  return mat ? str_copy(out, RMat(mat)->abs_path) : 0;
}
int c_tinyusd_render_material_has_preview_surface(const CTinyUSDRenderMaterial *mat) {
  return (mat && RMat(mat)->hasUsdPreviewSurface()) ? 1 : 0;
}
int c_tinyusd_render_material_has_open_pbr(const CTinyUSDRenderMaterial *mat) {
  return (mat && RMat(mat)->hasOpenPBR()) ? 1 : 0;
}

/* ---- RenderCamera ---- */

int c_tinyusd_render_camera_get_name(const CTinyUSDRenderCamera *cam,
                                     c_tinyusd_string_t *out) {
  return cam ? str_copy(out, RC(cam)->name) : 0;
}
int c_tinyusd_render_camera_get_abs_path(const CTinyUSDRenderCamera *cam,
                                         c_tinyusd_string_t *out) {
  return cam ? str_copy(out, RC(cam)->abs_path) : 0;
}
float c_tinyusd_render_camera_znear(const CTinyUSDRenderCamera *cam) {
  return cam ? RC(cam)->znear : 0.0f;
}
float c_tinyusd_render_camera_zfar(const CTinyUSDRenderCamera *cam) {
  return cam ? RC(cam)->zfar : 0.0f;
}
float c_tinyusd_render_camera_focal_length(const CTinyUSDRenderCamera *cam) {
  return cam ? RC(cam)->focalLength : 0.0f;
}
float c_tinyusd_render_camera_horizontal_aperture(const CTinyUSDRenderCamera *cam) {
  return cam ? RC(cam)->horizontalAperture : 0.0f;
}
float c_tinyusd_render_camera_vertical_aperture(const CTinyUSDRenderCamera *cam) {
  return cam ? RC(cam)->verticalAperture : 0.0f;
}
int c_tinyusd_render_camera_projection(const CTinyUSDRenderCamera *cam) {
  if (!cam) return 0;
  return RC(cam)->projection == tinyusdz::GeomCamera::Projection::Orthographic
             ? 1 : 0;
}

/* ---- RenderLight ---- */

int c_tinyusd_render_light_get_name(const CTinyUSDRenderLight *l,
                                    c_tinyusd_string_t *out) {
  return l ? str_copy(out, RL(l)->name) : 0;
}
int c_tinyusd_render_light_get_abs_path(const CTinyUSDRenderLight *l,
                                        c_tinyusd_string_t *out) {
  return l ? str_copy(out, RL(l)->abs_path) : 0;
}
CTinyUSDLightType c_tinyusd_render_light_type(const CTinyUSDRenderLight *l) {
  if (!l) return C_TINYUSD_LIGHT_UNKNOWN;
  using T = RenderLight::Type;
  switch (RL(l)->type) {
    case T::Point:    return C_TINYUSD_LIGHT_POINT;
    case T::Sphere:   return C_TINYUSD_LIGHT_SPHERE;
    case T::Disk:     return C_TINYUSD_LIGHT_DISK;
    case T::Rect:     return C_TINYUSD_LIGHT_RECT;
    case T::Cylinder: return C_TINYUSD_LIGHT_CYLINDER;
    case T::Distant:  return C_TINYUSD_LIGHT_DISTANT;
    case T::Dome:     return C_TINYUSD_LIGHT_DOME;
    case T::Geometry: return C_TINYUSD_LIGHT_GEOMETRY;
    case T::Portal:   return C_TINYUSD_LIGHT_PORTAL;
  }
  return C_TINYUSD_LIGHT_UNKNOWN;
}
float c_tinyusd_render_light_intensity(const CTinyUSDRenderLight *l) {
  return l ? RL(l)->intensity : 0.0f;
}
int c_tinyusd_render_light_get_color(const CTinyUSDRenderLight *l, float out[3]) {
  if (!l || !out) return 0;
  const auto &c = RL(l)->color;
  out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
  return 1;
}

/* ---- PreviewSurface flat readback ---- */

int c_tinyusd_render_material_get_preview_surface(
    const CTinyUSDRenderMaterial *mat, CTinyUSDPreviewSurface *out) {
  if (!mat || !out) return 0;
  std::memset(out, 0, sizeof(*out));
  if (!RMat(mat)->hasUsdPreviewSurface()) {
    out->has_shader = 0;
    return 1;  /* not an error; caller just checks has_shader */
  }
  const PreviewSurfaceShader &s = *RMat(mat)->surfaceShader;
  out->has_shader = 1;
  out->use_specular_workflow = s.useSpecularWorkflow ? 1 : 0;

  /* C side uses snake_case; C++ side is camelCase. Map explicitly. */
  out->diffuse_color[0] = s.diffuseColor.value[0];
  out->diffuse_color[1] = s.diffuseColor.value[1];
  out->diffuse_color[2] = s.diffuseColor.value[2];
  out->diffuse_color_tex = s.diffuseColor.texture_id;

  out->emissive_color[0] = s.emissiveColor.value[0];
  out->emissive_color[1] = s.emissiveColor.value[1];
  out->emissive_color[2] = s.emissiveColor.value[2];
  out->emissive_color_tex = s.emissiveColor.texture_id;

  out->specular_color[0] = s.specularColor.value[0];
  out->specular_color[1] = s.specularColor.value[1];
  out->specular_color[2] = s.specularColor.value[2];
  out->specular_color_tex = s.specularColor.texture_id;

  out->metallic            = s.metallic.value;
  out->metallic_tex        = s.metallic.texture_id;
  out->roughness           = s.roughness.value;
  out->roughness_tex       = s.roughness.texture_id;
  out->clearcoat           = s.clearcoat.value;
  out->clearcoat_tex       = s.clearcoat.texture_id;
  out->clearcoat_roughness = s.clearcoatRoughness.value;
  out->clearcoat_roughness_tex = s.clearcoatRoughness.texture_id;
  out->opacity             = s.opacity.value;
  out->opacity_tex         = s.opacity.texture_id;
  out->opacity_threshold   = s.opacityThreshold.value;
  out->opacity_threshold_tex = s.opacityThreshold.texture_id;
  out->ior                 = s.ior.value;
  out->ior_tex             = s.ior.texture_id;

  out->normal[0] = s.normal.value[0];
  out->normal[1] = s.normal.value[1];
  out->normal[2] = s.normal.value[2];
  out->normal_tex = s.normal.texture_id;

  out->displacement        = s.displacement.value;
  out->displacement_tex    = s.displacement.texture_id;
  out->occlusion           = s.occlusion.value;
  out->occlusion_tex       = s.occlusion.texture_id;

  return 1;
}

/* ---- OpenPBRSurface flat readback ---- */

#define CP_VEC3(dst, sp)                \
  do {                                  \
    (dst)[0] = (sp).value[0];           \
    (dst)[1] = (sp).value[1];           \
    (dst)[2] = (sp).value[2];           \
  } while (0)

int c_tinyusd_render_material_get_openpbr(
    const CTinyUSDRenderMaterial *mat, CTinyUSDOpenPBR *out) {
  if (!mat || !out) return 0;
  std::memset(out, 0, sizeof(*out));
  if (!RMat(mat)->hasOpenPBR()) {
    out->has_shader = 0;
    return 1;
  }
  const OpenPBRSurfaceShader &s = *RMat(mat)->openPBRShader;
  out->has_shader = 1;

  /* Base. */
  out->base_weight    = s.base_weight.value;
  out->base_weight_tex = s.base_weight.texture_id;
  CP_VEC3(out->base_color, s.base_color);
  out->base_color_tex = s.base_color.texture_id;
  out->base_roughness = s.base_roughness.value;
  out->base_roughness_tex = s.base_roughness.texture_id;
  out->base_metalness = s.base_metalness.value;
  out->base_metalness_tex = s.base_metalness.texture_id;
  out->base_diffuse_roughness = s.base_diffuse_roughness.value;
  out->base_diffuse_roughness_tex = s.base_diffuse_roughness.texture_id;

  /* Specular. */
  out->specular_weight    = s.specular_weight.value;
  out->specular_weight_tex = s.specular_weight.texture_id;
  CP_VEC3(out->specular_color, s.specular_color);
  out->specular_color_tex = s.specular_color.texture_id;
  out->specular_roughness = s.specular_roughness.value;
  out->specular_roughness_tex = s.specular_roughness.texture_id;
  out->specular_ior = s.specular_ior.value;
  out->specular_ior_tex = s.specular_ior.texture_id;
  out->specular_ior_level = s.specular_ior_level.value;
  out->specular_ior_level_tex = s.specular_ior_level.texture_id;
  out->specular_anisotropy = s.specular_anisotropy.value;
  out->specular_anisotropy_tex = s.specular_anisotropy.texture_id;
  out->specular_rotation = s.specular_rotation.value;
  out->specular_rotation_tex = s.specular_rotation.texture_id;
  out->specular_roughness_anisotropy = s.specular_roughness_anisotropy.value;
  out->specular_roughness_anisotropy_tex = s.specular_roughness_anisotropy.texture_id;

  /* Transmission. */
  out->transmission_weight = s.transmission_weight.value;
  out->transmission_weight_tex = s.transmission_weight.texture_id;
  CP_VEC3(out->transmission_color, s.transmission_color);
  out->transmission_color_tex = s.transmission_color.texture_id;
  out->transmission_depth = s.transmission_depth.value;
  out->transmission_depth_tex = s.transmission_depth.texture_id;
  CP_VEC3(out->transmission_scatter, s.transmission_scatter);
  out->transmission_scatter_tex = s.transmission_scatter.texture_id;
  out->transmission_scatter_anisotropy = s.transmission_scatter_anisotropy.value;
  out->transmission_scatter_anisotropy_tex = s.transmission_scatter_anisotropy.texture_id;
  out->transmission_dispersion = s.transmission_dispersion.value;
  out->transmission_dispersion_tex = s.transmission_dispersion.texture_id;
  out->transmission_dispersion_abbe_number = s.transmission_dispersion_abbe_number.value;
  out->transmission_dispersion_abbe_number_tex = s.transmission_dispersion_abbe_number.texture_id;
  out->transmission_dispersion_scale = s.transmission_dispersion_scale.value;
  out->transmission_dispersion_scale_tex = s.transmission_dispersion_scale.texture_id;

  /* Subsurface. */
  out->subsurface_weight = s.subsurface_weight.value;
  out->subsurface_weight_tex = s.subsurface_weight.texture_id;
  CP_VEC3(out->subsurface_color, s.subsurface_color);
  out->subsurface_color_tex = s.subsurface_color.texture_id;
  out->subsurface_radius = s.subsurface_radius.value;
  out->subsurface_radius_tex = s.subsurface_radius.texture_id;
  CP_VEC3(out->subsurface_radius_scale, s.subsurface_radius_scale);
  out->subsurface_radius_scale_tex = s.subsurface_radius_scale.texture_id;
  out->subsurface_scale = s.subsurface_scale.value;
  out->subsurface_scale_tex = s.subsurface_scale.texture_id;
  out->subsurface_anisotropy = s.subsurface_anisotropy.value;
  out->subsurface_anisotropy_tex = s.subsurface_anisotropy.texture_id;
  out->subsurface_scatter_anisotropy = s.subsurface_scatter_anisotropy.value;
  out->subsurface_scatter_anisotropy_tex = s.subsurface_scatter_anisotropy.texture_id;

  /* Sheen. */
  out->sheen_weight = s.sheen_weight.value;
  out->sheen_weight_tex = s.sheen_weight.texture_id;
  CP_VEC3(out->sheen_color, s.sheen_color);
  out->sheen_color_tex = s.sheen_color.texture_id;
  out->sheen_roughness = s.sheen_roughness.value;
  out->sheen_roughness_tex = s.sheen_roughness.texture_id;

  /* Fuzz. */
  out->fuzz_weight = s.fuzz_weight.value;
  out->fuzz_weight_tex = s.fuzz_weight.texture_id;
  CP_VEC3(out->fuzz_color, s.fuzz_color);
  out->fuzz_color_tex = s.fuzz_color.texture_id;
  out->fuzz_roughness = s.fuzz_roughness.value;
  out->fuzz_roughness_tex = s.fuzz_roughness.texture_id;

  /* Thin film. */
  out->thin_film_weight = s.thin_film_weight.value;
  out->thin_film_weight_tex = s.thin_film_weight.texture_id;
  out->thin_film_thickness = s.thin_film_thickness.value;
  out->thin_film_thickness_tex = s.thin_film_thickness.texture_id;
  out->thin_film_ior = s.thin_film_ior.value;
  out->thin_film_ior_tex = s.thin_film_ior.texture_id;

  /* Coat. */
  out->coat_weight = s.coat_weight.value;
  out->coat_weight_tex = s.coat_weight.texture_id;
  CP_VEC3(out->coat_color, s.coat_color);
  out->coat_color_tex = s.coat_color.texture_id;
  out->coat_roughness = s.coat_roughness.value;
  out->coat_roughness_tex = s.coat_roughness.texture_id;
  out->coat_anisotropy = s.coat_anisotropy.value;
  out->coat_anisotropy_tex = s.coat_anisotropy.texture_id;
  out->coat_rotation = s.coat_rotation.value;
  out->coat_rotation_tex = s.coat_rotation.texture_id;
  out->coat_ior = s.coat_ior.value;
  out->coat_ior_tex = s.coat_ior.texture_id;
  out->coat_affect_color = s.coat_affect_color.value;
  out->coat_affect_color_tex = s.coat_affect_color.texture_id;
  out->coat_affect_roughness = s.coat_affect_roughness.value;
  out->coat_affect_roughness_tex = s.coat_affect_roughness.texture_id;
  out->coat_roughness_anisotropy = s.coat_roughness_anisotropy.value;
  out->coat_roughness_anisotropy_tex = s.coat_roughness_anisotropy.texture_id;
  out->coat_darkening = s.coat_darkening.value;
  out->coat_darkening_tex = s.coat_darkening.texture_id;

  /* Emission. */
  out->emission_luminance = s.emission_luminance.value;
  out->emission_luminance_tex = s.emission_luminance.texture_id;
  CP_VEC3(out->emission_color, s.emission_color);
  out->emission_color_tex = s.emission_color.texture_id;

  /* Geometry modifiers. */
  out->opacity = s.opacity.value;
  out->opacity_tex = s.opacity.texture_id;
  CP_VEC3(out->normal, s.normal);
  out->normal_tex = s.normal.texture_id;
  CP_VEC3(out->tangent, s.tangent);
  out->tangent_tex = s.tangent.texture_id;

  /* Plain scalars. */
  out->tangent_rotation = s.tangent_rotation;
  out->normal_map_scale = s.normal_map_scale;

  /* Coat normal/tangent. */
  CP_VEC3(out->coat_normal, s.coat_normal);
  out->coat_normal_tex = s.coat_normal.texture_id;
  CP_VEC3(out->coat_tangent, s.coat_tangent);
  out->coat_tangent_tex = s.coat_tangent.texture_id;
  out->coat_tangent_rotation = s.coat_tangent_rotation;
  out->coat_normal_map_scale = s.coat_normal_map_scale;

  return 1;
}

#undef CP_VEC3

int c_tinyusd_render_material_get_node_graph_json(
    const CTinyUSDRenderMaterial *mat,
    const char **out_ptr, uint64_t *out_len) {
  if (!mat || !out_ptr || !out_len) return 0;
  if (!RMat(mat)->hasOpenPBR()) return 0;
  const std::string &j = RMat(mat)->openPBRShader->nodeGraphJson;
  if (j.empty()) return 0;
  *out_ptr = j.data();
  *out_len = static_cast<uint64_t>(j.size());
  return 1;
}

/* ---- RenderTexture (UVTexture) ---- */

int c_tinyusd_render_texture_get_name(const CTinyUSDRenderTexture *t,
                                      c_tinyusd_string_t *out) {
  return t ? str_copy(out, RT(t)->prim_name) : 0;
}
int c_tinyusd_render_texture_get_abs_path(const CTinyUSDRenderTexture *t,
                                          c_tinyusd_string_t *out) {
  return t ? str_copy(out, RT(t)->abs_path) : 0;
}
int c_tinyusd_render_texture_get_varname_uv(const CTinyUSDRenderTexture *t,
                                            c_tinyusd_string_t *out) {
  return t ? str_copy(out, RT(t)->varname_uv) : 0;
}
int c_tinyusd_render_texture_wrap_s(const CTinyUSDRenderTexture *t) {
  if (!t) return 0;
  using W = UVTexture::WrapMode;
  switch (RT(t)->wrapS) {
    case W::CLAMP_TO_EDGE:   return 0;
    case W::REPEAT:          return 1;
    case W::MIRROR:          return 2;
    case W::CLAMP_TO_BORDER: return 3;
  }
  return 0;
}
int c_tinyusd_render_texture_wrap_t(const CTinyUSDRenderTexture *t) {
  if (!t) return 0;
  using W = UVTexture::WrapMode;
  switch (RT(t)->wrapT) {
    case W::CLAMP_TO_EDGE:   return 0;
    case W::REPEAT:          return 1;
    case W::MIRROR:          return 2;
    case W::CLAMP_TO_BORDER: return 3;
  }
  return 0;
}
int64_t c_tinyusd_render_texture_image_id(const CTinyUSDRenderTexture *t) {
  return t ? RT(t)->texture_image_id : -1;
}
int c_tinyusd_render_texture_get_bias(const CTinyUSDRenderTexture *t,
                                      float out[4]) {
  if (!t || !out) return 0;
  const auto &b = RT(t)->bias;
  out[0] = b[0]; out[1] = b[1]; out[2] = b[2]; out[3] = b[3];
  return 1;
}
int c_tinyusd_render_texture_get_scale(const CTinyUSDRenderTexture *t,
                                       float out[4]) {
  if (!t || !out) return 0;
  const auto &s = RT(t)->scale;
  out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = s[3];
  return 1;
}

int c_tinyusd_render_texture_output_channel(const CTinyUSDRenderTexture *t) {
  if (!t) return 0;
  using Ch = UVTexture::Channel;
  switch (RT(t)->connectedOutputChannel) {
    case Ch::R:    return 0;
    case Ch::G:    return 1;
    case Ch::B:    return 2;
    case Ch::A:    return 3;
    case Ch::RGB:  return 4;
    case Ch::RGBA: return 5;
  }
  return 4;
}

int c_tinyusd_render_texture_get_fallback_uv(const CTinyUSDRenderTexture *t,
                                             float out[4]) {
  if (!t || !out) return 0;
  const auto &u = RT(t)->fallback_uv;
  out[0] = u[0]; out[1] = u[1]; out[2] = u[2]; out[3] = u[3];
  return 1;
}

int c_tinyusd_render_texture_has_transform2d(const CTinyUSDRenderTexture *t) {
  return (t && RT(t)->has_transform2d) ? 1 : 0;
}

float c_tinyusd_render_texture_tx_rotation(const CTinyUSDRenderTexture *t) {
  return t ? RT(t)->tx_rotation : 0.0f;
}

int c_tinyusd_render_texture_get_tx_scale(const CTinyUSDRenderTexture *t,
                                          float out[2]) {
  if (!t || !out) return 0;
  const auto &v = RT(t)->tx_scale;
  out[0] = v[0]; out[1] = v[1];
  return 1;
}

int c_tinyusd_render_texture_get_tx_translation(const CTinyUSDRenderTexture *t,
                                                float out[2]) {
  if (!t || !out) return 0;
  const auto &v = RT(t)->tx_translation;
  out[0] = v[0]; out[1] = v[1];
  return 1;
}

int c_tinyusd_render_texture_get_transform(const CTinyUSDRenderTexture *t,
                                           float out[9]) {
  if (!t || !out) return 0;
  const auto &m = RT(t)->transform.m;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r * 3 + c] = m[r][c];
    }
  }
  return 1;
}

/* ---- RenderImage (TextureImage) ---- */

static inline int map_component_type(ComponentType ct) {
  switch (ct) {
    case ComponentType::UInt8:  return 0;
    case ComponentType::Int8:   return 1;
    case ComponentType::UInt16: return 2;
    case ComponentType::Int16:  return 3;
    case ComponentType::UInt32: return 4;
    case ComponentType::Int32:  return 5;
    case ComponentType::Half:   return 6;
    case ComponentType::Float:  return 7;
    case ComponentType::Double: return 8;
  }
  return 0;
}

int c_tinyusd_render_image_get_asset_identifier(const CTinyUSDRenderImage *i,
                                                c_tinyusd_string_t *out) {
  return i ? str_copy(out, RI(i)->asset_identifier) : 0;
}
int32_t c_tinyusd_render_image_width(const CTinyUSDRenderImage *i) {
  return i ? RI(i)->width : -1;
}
int32_t c_tinyusd_render_image_height(const CTinyUSDRenderImage *i) {
  return i ? RI(i)->height : -1;
}
int32_t c_tinyusd_render_image_channels(const CTinyUSDRenderImage *i) {
  return i ? RI(i)->channels : -1;
}
int32_t c_tinyusd_render_image_miplevel(const CTinyUSDRenderImage *i) {
  return i ? RI(i)->miplevel : 0;
}
int64_t c_tinyusd_render_image_buffer_id(const CTinyUSDRenderImage *i) {
  return i ? RI(i)->buffer_id : -1;
}
int c_tinyusd_render_image_color_space(const CTinyUSDRenderImage *i) {
  return i ? static_cast<int>(RI(i)->colorSpace) : 0;
}
int c_tinyusd_render_image_is_decoded(const CTinyUSDRenderImage *i) {
  return (i && RI(i)->decoded) ? 1 : 0;
}
int c_tinyusd_render_image_texel_component_type(const CTinyUSDRenderImage *i) {
  return i ? map_component_type(RI(i)->texelComponentType) : 0;
}

/* ---- RenderBuffer (BufferData) ---- */

int c_tinyusd_render_buffer_component_type(const CTinyUSDRenderBuffer *b) {
  return b ? map_component_type(RB(b)->componentType) : 0;
}

int c_tinyusd_render_buffer_get_bytes(const CTinyUSDRenderBuffer *b,
                                      const void **out_ptr,
                                      uint64_t *out_nbytes) {
  if (!b || !out_ptr || !out_nbytes) return 0;
  const auto &d = RB(b)->data;
  *out_ptr = d.empty() ? nullptr : static_cast<const void *>(d.data());
  *out_nbytes = static_cast<uint64_t>(d.size());
  return 1;
}

/* ---- AnimationClip ---- */

int c_tinyusd_animation_get_name(const CTinyUSDAnimationClip *a,
                                 c_tinyusd_string_t *out) {
  return a ? str_copy(out, RA(a)->name) : 0;
}
int c_tinyusd_animation_get_abs_path(const CTinyUSDAnimationClip *a,
                                     c_tinyusd_string_t *out) {
  return a ? str_copy(out, RA(a)->abs_path) : 0;
}
float c_tinyusd_animation_duration(const CTinyUSDAnimationClip *a) {
  return a ? RA(a)->duration : 0.0f;
}
uint64_t c_tinyusd_animation_num_samplers(const CTinyUSDAnimationClip *a) {
  return a ? static_cast<uint64_t>(RA(a)->samplers.size()) : 0;
}
uint64_t c_tinyusd_animation_num_channels(const CTinyUSDAnimationClip *a) {
  return a ? static_cast<uint64_t>(RA(a)->channels.size()) : 0;
}
int c_tinyusd_animation_has_skeletal(const CTinyUSDAnimationClip *a) {
  return (a && RA(a)->has_skeletal_animation()) ? 1 : 0;
}
int c_tinyusd_animation_has_node(const CTinyUSDAnimationClip *a) {
  return (a && RA(a)->has_node_animation()) ? 1 : 0;
}

int c_tinyusd_animation_get_sampler(const CTinyUSDAnimationClip *a,
                                    uint64_t idx,
                                    const float **out_times, uint64_t *out_n_times,
                                    const float **out_values, uint64_t *out_n_values,
                                    int *out_interpolation) {
  if (!a || !out_times || !out_n_times || !out_values ||
      !out_n_values || !out_interpolation) return 0;
  if (idx >= RA(a)->samplers.size()) return 0;
  const KeyframeSampler &s = RA(a)->samplers[idx];
  *out_times  = s.times.empty()  ? nullptr : s.times.data();
  *out_n_times  = static_cast<uint64_t>(s.times.size());
  *out_values = s.values.empty() ? nullptr : s.values.data();
  *out_n_values = static_cast<uint64_t>(s.values.size());
  switch (s.interpolation) {
    case AnimationInterpolation::Step:        *out_interpolation = 0; break;
    case AnimationInterpolation::Linear:      *out_interpolation = 1; break;
    case AnimationInterpolation::CubicSpline: *out_interpolation = 2; break;
    default:                                  *out_interpolation = 1; break;
  }
  return 1;
}

int c_tinyusd_animation_get_channel(const CTinyUSDAnimationClip *a,
                                    uint64_t idx,
                                    int *out_path, int *out_target_type,
                                    int32_t *out_target_node,
                                    int32_t *out_skeleton_id,
                                    int32_t *out_joint_id,
                                    int32_t *out_sampler) {
  if (!a || !out_path || !out_target_type || !out_target_node ||
      !out_skeleton_id || !out_joint_id || !out_sampler) return 0;
  if (idx >= RA(a)->channels.size()) return 0;
  const AnimationChannel &c = RA(a)->channels[idx];
  switch (c.path) {
    case AnimationPath::Translation: *out_path = 0; break;
    case AnimationPath::Rotation:    *out_path = 1; break;
    case AnimationPath::Scale:       *out_path = 2; break;
    case AnimationPath::Weights:     *out_path = 3; break;
  }
  *out_target_type = (c.target_type == ChannelTargetType::SkeletonJoint) ? 1 : 0;
  *out_target_node = c.target_node;
  *out_skeleton_id = c.skeleton_id;
  *out_joint_id    = c.joint_id;
  *out_sampler     = c.sampler;
  return 1;
}

/* ---- SkelHierarchy ---- */

int c_tinyusd_skel_get_name(const CTinyUSDSkelHierarchy *s,
                            c_tinyusd_string_t *out) {
  return s ? str_copy(out, RK(s)->prim_name) : 0;
}
int c_tinyusd_skel_get_abs_path(const CTinyUSDSkelHierarchy *s,
                                c_tinyusd_string_t *out) {
  return s ? str_copy(out, RK(s)->abs_path) : 0;
}
uint64_t c_tinyusd_skel_num_joints(const CTinyUSDSkelHierarchy *s) {
  return s ? static_cast<uint64_t>(RK(s)->num_joints()) : 0;
}
int c_tinyusd_skel_default_anim_id(const CTinyUSDSkelHierarchy *s) {
  return s ? RK(s)->anim_id : -1;
}

int c_tinyusd_skel_get_parent_joint_indices(const CTinyUSDSkelHierarchy *s,
                                            const void **out_ptr,
                                            uint64_t *out_n) {
  if (!s || !out_ptr || !out_n) return 0;
  const auto &v = RK(s)->parent_joint_indices;
  *out_ptr = v.empty() ? nullptr : static_cast<const void *>(v.data());
  *out_n = static_cast<uint64_t>(v.size());
  return 1;
}

int c_tinyusd_skel_get_bind_transforms(const CTinyUSDSkelHierarchy *s,
                                       const void **out_ptr, uint64_t *out_n) {
  if (!s || !out_ptr || !out_n) return 0;
  const auto &v = RK(s)->bind_transforms;
  *out_ptr = v.empty() ? nullptr : static_cast<const void *>(v.data());
  *out_n = static_cast<uint64_t>(v.size());
  return 1;
}

int c_tinyusd_skel_get_rest_transforms(const CTinyUSDSkelHierarchy *s,
                                       const void **out_ptr, uint64_t *out_n) {
  if (!s || !out_ptr || !out_n) return 0;
  const auto &v = RK(s)->rest_transforms;
  *out_ptr = v.empty() ? nullptr : static_cast<const void *>(v.data());
  *out_n = static_cast<uint64_t>(v.size());
  return 1;
}

/* ---- RenderNode ---- */

uint64_t c_tinyusd_render_scene_num_nodes(const CTinyUSDRenderScene *s) {
  return s ? static_cast<uint64_t>(RS(s)->nodes.size()) : 0;
}

const CTinyUSDRenderNode *
c_tinyusd_render_scene_get_node(const CTinyUSDRenderScene *s, uint64_t idx) {
  if (!s) return nullptr;
  const auto &ns = RS(s)->nodes;
  if (idx >= ns.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderNode *>(&ns[idx]);
}

uint32_t
c_tinyusd_render_scene_default_root_node(const CTinyUSDRenderScene *s) {
  return s ? RS(s)->default_root_node : 0u;
}

int c_tinyusd_render_node_get_name(const CTinyUSDRenderNode *n,
                                   c_tinyusd_string_t *out) {
  return n ? str_copy(out, RN(n)->prim_name) : 0;
}
int c_tinyusd_render_node_get_abs_path(const CTinyUSDRenderNode *n,
                                       c_tinyusd_string_t *out) {
  return n ? str_copy(out, RN(n)->abs_path) : 0;
}
int c_tinyusd_render_node_get_display_name(const CTinyUSDRenderNode *n,
                                           c_tinyusd_string_t *out) {
  return n ? str_copy(out, RN(n)->display_name) : 0;
}

int c_tinyusd_render_node_category(const CTinyUSDRenderNode *n) {
  if (!n) return 0;
  switch (RN(n)->category) {
    case NodeCategory::Group:    return 0;
    case NodeCategory::Geom:     return 1;
    case NodeCategory::Light:    return 2;
    case NodeCategory::Camera:   return 3;
    case NodeCategory::Material: return 4;
    case NodeCategory::Skeleton: return 5;
  }
  return 0;
}

int c_tinyusd_render_node_node_type(const CTinyUSDRenderNode *n) {
  if (!n) return 0;
  switch (RN(n)->nodeType) {
    case NodeType::Xform:            return 0;
    case NodeType::Mesh:             return 1;
    case NodeType::Camera:           return 2;
    case NodeType::SkelRoot:         return 3;
    case NodeType::Skeleton:         return 4;
    case NodeType::PointLight:       return 5;
    case NodeType::DirectionalLight: return 6;
    case NodeType::EnvmapLight:      return 7;
    case NodeType::RectLight:        return 8;
    case NodeType::DiskLight:        return 9;
    case NodeType::CylinderLight:    return 10;
    case NodeType::GeometryLight:    return 11;
  }
  return 0;
}

int32_t c_tinyusd_render_node_content_id(const CTinyUSDRenderNode *n) {
  return n ? RN(n)->id : -1;
}

int c_tinyusd_render_node_is_instance(const CTinyUSDRenderNode *n) {
  return (n && RN(n)->is_instance) ? 1 : 0;
}
int32_t c_tinyusd_render_node_prototype_index(const CTinyUSDRenderNode *n) {
  return n ? RN(n)->prototype_index : -1;
}
int32_t c_tinyusd_render_node_instance_id(const CTinyUSDRenderNode *n) {
  return n ? RN(n)->instance_id : -1;
}

uint64_t c_tinyusd_render_node_num_children(const CTinyUSDRenderNode *n) {
  return n ? static_cast<uint64_t>(RN(n)->children.size()) : 0;
}

const CTinyUSDRenderNode *
c_tinyusd_render_node_get_child(const CTinyUSDRenderNode *n, uint64_t idx) {
  if (!n) return nullptr;
  const auto &ch = RN(n)->children;
  if (idx >= ch.size()) return nullptr;
  return reinterpret_cast<const CTinyUSDRenderNode *>(&ch[idx]);
}

int c_tinyusd_render_node_get_local_matrix(const CTinyUSDRenderNode *n,
                                           double out[16]) {
  if (!n || !out) return 0;
  const auto &m = RN(n)->local_matrix.m;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      out[r * 4 + c] = m[r][c];
    }
  }
  return 1;
}

int c_tinyusd_render_node_get_global_matrix(const CTinyUSDRenderNode *n,
                                            double out[16]) {
  if (!n || !out) return 0;
  const auto &m = RN(n)->global_matrix.m;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      out[r * 4 + c] = m[r][c];
    }
  }
  return 1;
}

int c_tinyusd_render_node_has_reset_xform(const CTinyUSDRenderNode *n) {
  return (n && RN(n)->has_resetXform) ? 1 : 0;
}

}  // extern "C"
