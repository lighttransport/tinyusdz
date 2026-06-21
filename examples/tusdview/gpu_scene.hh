// SPDX-License-Identifier: Apache-2.0
// tusdview - backend-neutral GPU scene representation.
//
// `DrawScene` is the hand-off between the USD/Tydra side (mesh_build.cc) and the
// graphics backends (gl_renderer / vk_renderer). It contains nothing
// backend-specific: interleaved vertices, a triangulated index buffer grouped
// into per-material submeshes, materials (mapped from UsdPreviewSurface), and
// decoded RGBA8 textures. Both the OpenGL and Vulkan backends consume this
// identical structure.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "light3d/texture.h"  // light3d::Image (CPU texel container)

namespace tusdview {

// Interleaved vertex: matches the GL330 / VK450 shader attribute layout
//   location 0: vec3 aPosition  (offset 0)
//   location 1: vec3 aNormal    (offset 12)
//   location 2: vec2 aUV        (offset 24)  -> shader declares vec3 aUV, reads .xy
struct DrawVertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
};

// One draw call: a contiguous run of indices sharing a single material.
struct DrawSubmesh {
  uint32_t indexOffset{0};  // offset into DrawMeshCPU::indices (in indices)
  uint32_t indexCount{0};
  int materialId{-1};  // index into DrawScene::materials (-1 = default material)
};

// One blendshape target in DrawVertex order (sparse), for per-frame GPU morph.
// `vtx[i]` is the affected DrawVertex; `dpos` holds its position offset (3
// floats, parallel to `vtx`). Normals are regenerated from the morphed
// positions (RegenNormalsOriented), so authored normal offsets are not stored.
// One in-between shape sample of a blendshape: its position-offset deltas at a
// weight in (0,1). Parallel to MorphTargetCPU::vtx (same affected DrawVertices).
struct MorphInbetweenCPU {
  float weight{0.5f};        // USD inbetween weight (0 < w < 1, ascending order)
  std::vector<float> dpos;   // 3 floats per MorphTargetCPU::vtx entry
};

struct MorphTargetCPU {
  std::string name;  // BlendShape prim name == SkelAnimation weight key
  std::vector<uint32_t> vtx;
  std::vector<float> dpos;   // primary (weight == 1.0) offsets, 3 per vtx entry
  // In-between shapes, sorted ascending by weight. Empty = simple linear morph
  // (rest at w=0 -> primary at w=1). With inbetweens the morph piecewise-lerps
  // through (0, rest), each (weight, sample), and (1, primary).
  std::vector<MorphInbetweenCPU> inbetweens;
};

struct DrawMeshCPU {
  std::string name;
  std::string absPath;
  std::string purpose{"default"};  // USD purpose token: default/render/proxy/guide

  std::vector<DrawVertex> vertices;  // rest pose (GPU morph re-derives from this)
  // Optional per-vertex displayColor (rgb, parallel to `vertices`); empty = none.
  // Used by the flat --next preview to tint geometry; the material shader
  // multiplies baseColor by it (default white when absent).
  std::vector<float> vertexColors;
  // True when the mesh has no authored normals: the shader shades it with the
  // geometric (screen-derivative) normal instead of the per-vertex normal, so
  // hard-surface geometry isn't smeared by averaged smooth normals.
  bool geometricNormal{false};
  // Blendshape targets remapped to DrawVertex order; empty = no blendshapes.
  // The GPU path morphs `vertices` per frame and re-uploads them.
  std::vector<MorphTargetCPU> morphs;
  // Optional GPU skinning attributes, parallel to `vertices`.
  // `jointIdx` stores four absolute bone-matrix texture row indices per vertex;
  // `jointWt` stores the corresponding normalized weights. Empty = unskinned.
  std::vector<uint32_t> jointIdx;
  std::vector<float> jointWt;
  // Optional GL3-compatible full influence stream. `influenceOffsetCount` stores
  // two uints per vertex: texel offset and influence count. `influenceTexels`
  // stores one influence per RGBA32F texel: (absoluteJointRow, weight, 0, 0).
  std::vector<uint32_t> influenceOffsetCount;
  std::vector<float> influenceTexels;
  int influenceTexWidth{0};
  int influenceTexHeight{0};
  int maxInfluencesPerVertex{0};
  std::vector<uint32_t> indices;  // triangulated, grouped by submesh/material
  std::vector<DrawSubmesh> submeshes;

  // GPU instancing: per-instance 3x4 object-to-world matrices, 12 floats each
  // (3 rows of (x,y,z,translate); the constant bottom row is implicit). When
  // non-empty, the mesh is drawn with glDrawElementsInstanced and `world` is
  // ignored (each instance carries its own placement). Empty = a single
  // non-instanced draw.
  std::vector<float> instanceXforms;
  size_t instanceCount() const { return instanceXforms.size() / 12; }
  // Instance/prototype displayColor for the flat instanced path. When
  // instanceColors is non-empty (3 floats/instance) each instance is tinted
  // individually; otherwise the whole instanced draw uses flatColor (e.g. the
  // prototype's average displayColor). Ignored for non-instanced draws.
  std::vector<float> instanceColors;
  float flatColor[3]{0.8f, 0.8f, 0.8f};

  float world[16];  // column-major (light3d::Mat4 layout), world transform
  // USD row-vector matrix copied with the same convention as `world`.
  float skinGeomBind[16];
  int skelId{-1};
  int skinMatrixBase{-1};  // first matrix row in DrawScene's bone texture layout
  // Optional local-space skinned point samples for dense point-joint helper
  // display. Stored as xyz triples and updated by GPU skinning frame builds.
  std::vector<float> skinnedHelperPoints;
  float aabbMin[3]{0, 0, 0};
  float aabbMax[3]{0, 0, 0};
  bool doubleSided{false};
};

// USD purpose token -> compact id used by the Purpose debug AOV (consistent across
// all backends): 0=default, 1=render, 2=proxy, 3=guide.
inline int PurposeId(const std::string& p) {
  if (p == "render") return 1;
  if (p == "proxy") return 2;
  if (p == "guide") return 3;
  return 0;  // default / unknown
}

enum class AlphaMode : int { Opaque = 0, Mask = 1, Blend = 2 };

struct DrawMaterialCPU {
  std::string name;
  float baseColor[3]{0.8f, 0.8f, 0.8f};
  float metallic{0.0f};
  float roughness{0.5f};
  float emissive[3]{0.0f, 0.0f, 0.0f};
  float alpha{1.0f};
  int alphaMode{static_cast<int>(AlphaMode::Opaque)};
  float alphaCutoff{0.5f};
  // Indices into DrawScene::textures (-1 = no texture)
  int baseColorTex{-1};
  int metalRoughTex{-1};
  int normalTex{-1};
  int emissiveTex{-1};
};

// Wrap modes (match light3d / GL semantics).
enum class WrapMode : int { ClampToEdge = 0, Repeat = 1, Mirror = 2, ClampToBorder = 3 };

struct DrawTextureCPU {
  light3d::Image image;  // always normalized to RGBA8 (channels == 4) on the CPU side
  bool srgb{false};      // sRGB color data (baseColor/emissive) vs linear (normal/metalRough)
  int wrapS{static_cast<int>(WrapMode::Repeat)};
  int wrapT{static_cast<int>(WrapMode::Repeat)};
};

struct DrawScene {
  std::vector<DrawMeshCPU> meshes;
  std::vector<DrawMaterialCPU> materials;
  std::vector<DrawTextureCPU> textures;
  int boneMatrixCount{0};  // height of the per-frame 4xN RGBA32F bone texture

  // World-space bounds over all meshes.
  float aabbMin[3]{-1, -1, -1};
  float aabbMax[3]{1, 1, 1};
  bool hasBounds{false};

  // Stage up axis ("Y" or "Z"); drives camera orbit + grid orientation. Set by
  // the loader (the Tydra path uses RenderScene.meta.upAxis directly instead).
  std::string upAxis{"Y"};

  // Diagnostics surfaced in the GUI (skipped meshes/textures, UDIM, etc.)
  std::vector<std::string> skipped;
  size_t triangleCount{0};

  // True when a render budget (triangles / VRAM) was hit and the scene was only
  // partially built to avoid freezing / VRAM thrashing.
  bool truncated{false};

  bool empty() const { return meshes.empty(); }
};

struct SkinningFrameCPU {
  int matrixCount{0};  // texture height; width is always 4 RGBA texels
  std::vector<float> rgba32f;  // matrixCount * 4 texels * 4 floats
  bool enabled{false};
};

}  // namespace tusdview
