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

struct DrawMeshCPU {
  std::string name;
  std::string absPath;

  std::vector<DrawVertex> vertices;
  std::vector<uint32_t> indices;  // triangulated, grouped by submesh/material
  std::vector<DrawSubmesh> submeshes;

  float world[16];  // column-major (light3d::Mat4 layout), world transform
  float aabbMin[3]{0, 0, 0};
  float aabbMax[3]{0, 0, 0};
  bool doubleSided{false};
};

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

  // World-space bounds over all meshes.
  float aabbMin[3]{-1, -1, -1};
  float aabbMax[3]{1, 1, 1};
  bool hasBounds{false};

  // Diagnostics surfaced in the GUI (skipped meshes/textures, UDIM, etc.)
  std::vector<std::string> skipped;
  size_t triangleCount{0};

  // True when a render budget (triangles / VRAM) was hit and the scene was only
  // partially built to avoid freezing / VRAM thrashing.
  bool truncated{false};

  bool empty() const { return meshes.empty(); }
};

}  // namespace tusdview
