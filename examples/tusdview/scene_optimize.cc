// SPDX-License-Identifier: Apache-2.0
#include "scene_optimize.hh"

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "lightrt_mtlx_bridge.hh"

namespace tusdview {
namespace {

struct PackedMaterial {
  std::array<float, kLightRtOpenPBRFloats> lightRt{};
  std::array<float, kRtMaterialTextureParamFloats> rtTextures{};
  std::array<float, kRasterMaterialTextureParamFloats> rasterTextures{};
  std::array<float, 23> scalars{};
  std::array<int, 13> textureIds{};
  int alphaMode{0};
  float alphaCutoff{0.0f};
  uint32_t flags{0};
};

PackedMaterial Pack(const DrawMaterialCPU& material) {
  PackedMaterial packed;
  PackLightRtOpenPBR(material, packed.lightRt.data());
  PackRtMaterialTextureParams(material, packed.rtTextures.data());
  PackRasterMaterialTextureParams(material, packed.rasterTextures.data());
  packed.scalars = {
      material.baseColor[0], material.baseColor[1], material.baseColor[2],
      material.metallic, material.roughness,
      material.emissive[0], material.emissive[1], material.emissive[2],
      material.alpha, material.alphaCutoff,
      material.specularColor[0], material.specularColor[1],
      material.specularColor[2], material.ior, material.occlusion,
      material.coatWeight, material.coatColor[0], material.coatColor[1],
      material.coatColor[2], material.coatRoughness, material.coatIor,
      material.displacementConst, material.displacementTexScale};
  packed.textureIds = {
      material.baseColorTex, material.metallicTex, material.roughnessTex,
      material.normalTex, material.coatNormalTex, material.emissiveTex,
      material.opacityTex, material.occlusionTex, material.specularColorTex,
      material.coatWeightTex, material.coatColorTex,
      material.coatRoughnessTex, material.displacementTex};
  packed.alphaMode = material.alphaMode;
  packed.alphaCutoff = material.alphaCutoff;
  packed.flags = (material.hasUsdPreviewSurface ? 1u : 0u) |
                 (material.hasOpenPBRSurface ? 2u : 0u) |
                 (material.hasDisplacementOutput ? 4u : 0u) |
                 (material.hasVolumeOutput ? 8u : 0u) |
                 (material.useSpecularWorkflow ? 16u : 0u) |
                 (material.openPbrSpecularModel ? 32u : 0u);
  return packed;
}

uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

void RemapMaterial(int* id, const std::vector<int>& remap) {
  if (!id || *id < 0 || static_cast<size_t>(*id) >= remap.size()) return;
  *id = remap[static_cast<size_t>(*id)];
}

}  // namespace

uint64_t DrawMaterialRenderHash(const DrawMaterialCPU& material) {
  const PackedMaterial packed = Pack(material);
  return HashBytes(1469598103934665603ull, &packed, sizeof(packed));
}

bool DrawMaterialsRenderEquivalent(const DrawMaterialCPU& a,
                                   const DrawMaterialCPU& b) {
  const PackedMaterial pa = Pack(a);
  const PackedMaterial pb = Pack(b);
  return std::memcmp(&pa, &pb, sizeof(pa)) == 0;
}

size_t DeduplicateDrawMaterials(DrawScene* scene) {
  if (!scene) return 0;
  if (scene->optimization.sourceMaterials == 0) {
    scene->optimization.sourceMaterials = scene->materials.size();
  }
  if (scene->materials.size() < 2) {
    scene->optimization.uniqueMaterials = scene->materials.size();
    return 0;
  }
  const size_t before = scene->materials.size();
  std::unordered_map<uint64_t, std::vector<int>> candidates;
  candidates.reserve(before);
  std::vector<DrawMaterialCPU> unique;
  unique.reserve(before);
  std::vector<int> remap(before, -1);
  for (size_t i = 0; i < before; ++i) {
    const DrawMaterialCPU& material = scene->materials[i];
    const uint64_t hash = DrawMaterialRenderHash(material);
    int canonical = -1;
    auto& bucket = candidates[hash];
    for (int candidate : bucket) {
      if (DrawMaterialsRenderEquivalent(
              material, unique[static_cast<size_t>(candidate)])) {
        canonical = candidate;
        break;
      }
    }
    if (canonical < 0) {
      canonical = static_cast<int>(unique.size());
      unique.push_back(material);
      bucket.push_back(canonical);
    }
    remap[i] = canonical;
  }
  if (unique.size() == before) {
    scene->optimization.uniqueMaterials = before;
    return 0;
  }
  for (DrawMeshCPU& mesh : scene->meshes) {
    for (DrawSubmesh& submesh : mesh.submeshes) {
      RemapMaterial(&submesh.materialId, remap);
      RemapMaterial(&submesh.backfaceMaterialId, remap);
    }
  }
  for (DrawPointsCPU& points : scene->points) {
    RemapMaterial(&points.materialId, remap);
  }
  for (DrawCurvesCPU& curves : scene->curves) {
    RemapMaterial(&curves.materialId, remap);
  }
  scene->materials = std::move(unique);
  const size_t removed = before - scene->materials.size();
  scene->optimization.uniqueMaterials = scene->materials.size();
  scene->optimization.deduplicatedMaterials += removed;
  return removed;
}

}  // namespace tusdview
