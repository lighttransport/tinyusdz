// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "gpu_scene.hh"

namespace lusdview {

inline bool CheckedMulSize(size_t a, size_t b, size_t* out) {
  if (!out || (a != 0 && b > std::numeric_limits<size_t>::max() / a)) {
    return false;
  }
  *out = a * b;
  return true;
}

inline bool FitsUint32(size_t value) {
  return value <= static_cast<size_t>(std::numeric_limits<uint32_t>::max());
}

inline bool ValidateDrawMesh(const DrawMeshCPU& mesh, size_t materialCount,
                             std::string* err) {
  const auto fail = [&](const std::string& reason) {
    if (err) {
      *err = "mesh '" + (mesh.absPath.empty() ? mesh.name : mesh.absPath) +
             "': " + reason;
    }
    return false;
  };
  if (mesh.vertices.empty() != mesh.indices.empty()) {
    return fail("vertices and indices must either both be empty or both be present");
  }
  if (mesh.indices.size() % 3 != 0) {
    return fail("index count is not a multiple of three");
  }
  if (!FitsUint32(mesh.vertices.size()) || !FitsUint32(mesh.indices.size())) {
    return fail("vertex or index count exceeds the 32-bit renderer limit");
  }
  for (size_t i = 0; i < mesh.indices.size(); ++i) {
    if (mesh.indices[i] >= mesh.vertices.size()) {
      return fail("index " + std::to_string(i) + " is outside the vertex array");
    }
  }
  for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
    const DrawSubmesh& sub = mesh.submeshes[i];
    if ((sub.indexOffset % 3) != 0 || (sub.indexCount % 3) != 0 ||
        sub.indexOffset > mesh.indices.size() ||
        sub.indexCount > mesh.indices.size() - sub.indexOffset) {
      return fail("submesh " + std::to_string(i) + " has an invalid index range");
    }
    const auto validMaterial = [materialCount](int id) {
      return id < 0 || static_cast<size_t>(id) < materialCount;
    };
    if (!validMaterial(sub.materialId) || !validMaterial(sub.backfaceMaterialId)) {
      return fail("submesh " + std::to_string(i) + " references an invalid material");
    }
  }
  const size_t nv = mesh.vertices.size();
  const auto parallel = [&](size_t actual, size_t components, const char* name) {
    size_t expected = 0;
    if (!CheckedMulSize(nv, components, &expected) ||
        (actual != 0 && actual != expected)) {
      return fail(std::string(name) + " is not parallel to vertices");
    }
    return true;
  };
  if (!parallel(mesh.vertexColors.size(), 3, "vertexColors") ||
      !parallel(mesh.vertexAlpha.size(), 1, "vertexAlpha") ||
      !parallel(mesh.tangents.size(), 3, "tangents") ||
      !parallel(mesh.binormals.size(), 3, "binormals") ||
      !parallel(mesh.uv1.size(), 2, "uv1") ||
      !parallel(mesh.morphInfluence.size(), 1, "morphInfluence") ||
      !parallel(mesh.jointIdx.size(), 4, "jointIdx") ||
      !parallel(mesh.jointWt.size(), 4, "jointWt") ||
      !parallel(mesh.influenceOffsetCount.size(), 2, "influenceOffsetCount") ||
      !parallel(mesh.morphOffsetCount.size(), 2, "morphOffsetCount")) {
    return false;
  }
  for (size_t i = 0; i < mesh.geomProps.size(); ++i) {
    const DrawGeomPropCPU& prop = mesh.geomProps[i];
    if (prop.name.empty() || prop.components == 0 ||
        (prop.components != 1 && prop.components != 2 &&
         prop.components != 3 && prop.components != 4 &&
         prop.components != 9 && prop.components != 16)) {
      return fail("geomProps[" + std::to_string(i) + "] has invalid metadata");
    }
    size_t expected = 0;
    if (!CheckedMulSize(nv, prop.components, &expected) ||
        prop.values.size() != expected) {
      return fail("geomProps[" + std::to_string(i) + "] is not parallel to vertices");
    }
    for (float value : prop.values) {
      if (!std::isfinite(value)) {
        return fail("geomProps[" + std::to_string(i) + "] contains a non-finite value");
      }
    }
  }
  if (!mesh.sourceFaceId.empty() &&
      mesh.sourceFaceId.size() != mesh.indices.size() / 3) {
    return fail("sourceFaceId is not parallel to triangles");
  }
  if ((mesh.instanceXforms.size() % 12) != 0) {
    return fail("instanceXforms does not contain complete 3x4 matrices");
  }
  const size_t ni = mesh.instanceCount();
  size_t expectedColors = 0;
  if (!CheckedMulSize(ni, 3, &expectedColors) ||
      (!mesh.instanceColors.empty() && mesh.instanceColors.size() != expectedColors) ||
      (!mesh.instanceOpacities.empty() && mesh.instanceOpacities.size() != ni) ||
      !FitsUint32(ni)) {
    return fail("instance arrays are inconsistent or exceed 32-bit limits");
  }
  if (!mesh.morphDeltaHalf.empty() && (mesh.morphDeltaHalf.size() % 4) != 0) {
    return fail("morphDeltaHalf does not contain complete entries");
  }
  if (!mesh.morphChannelId.empty() &&
      mesh.morphChannelId.size() != mesh.morphDeltaHalf.size() / 4) {
    return fail("morphChannelId is not parallel to morph deltas");
  }
  const size_t morphEntries = mesh.morphDeltaHalf.size() / 4;
  for (size_t v = 0; v + 1 < mesh.morphOffsetCount.size(); v += 2) {
    const size_t off = mesh.morphOffsetCount[v];
    const size_t count = mesh.morphOffsetCount[v + 1];
    if (off > morphEntries || count > morphEntries - off) {
      return fail("morph offset/count range is outside the delta table");
    }
  }
  for (const DrawVertex& v : mesh.vertices) {
    if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz) ||
        !std::isfinite(v.nx) || !std::isfinite(v.ny) || !std::isfinite(v.nz) ||
        !std::isfinite(v.u) || !std::isfinite(v.v)) {
      return fail("contains a non-finite vertex value");
    }
  }
  return true;
}

inline bool ValidateDrawScene(const DrawScene& scene, std::string* err) {
  if (scene.textures.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    if (err) *err = "texture count exceeds the renderer interface limit";
    return false;
  }
  for (size_t i = 0; i < scene.textures.size(); ++i) {
    const DrawTextureCPU& texture = scene.textures[i];
    if (texture.deferredDecode) continue;
    const bool hasImage = texture.image.width != 0 || texture.image.height != 0 ||
                          texture.image.channels != 0 ||
                          !texture.image.data.empty() || texture.streamingMutable;
    size_t pixels = 0;
    size_t bytes = 0;
    if (hasImage &&
        (texture.image.width <= 0 || texture.image.height <= 0 ||
         texture.image.channels != 4 ||
         !CheckedMulSize(static_cast<size_t>(texture.image.width),
                         static_cast<size_t>(texture.image.height), &pixels) ||
         !CheckedMulSize(pixels, size_t{4}, &bytes) ||
         (texture.image.data.size() < bytes && !texture.streamingMutable))) {
      if (err) *err = "texture " + std::to_string(i) +
                      " has an invalid RGBA image payload";
      return false;
    }
    int width = texture.image.width;
    int height = texture.image.height;
    for (const light3d::Image& mip : texture.mipImages) {
      width = std::max(1, width / 2);
      height = std::max(1, height / 2);
      size_t mipPixels = 0;
      size_t mipBytes = 0;
      if (!hasImage || mip.width != width || mip.height != height ||
          mip.channels != 4 ||
          !CheckedMulSize(static_cast<size_t>(width),
                          static_cast<size_t>(height), &mipPixels) ||
          !CheckedMulSize(mipPixels, size_t{4}, &mipBytes) ||
          mip.data.size() < mipBytes) {
        if (err) *err = "texture " + std::to_string(i) +
                        " has an invalid RGBA mip payload";
        return false;
      }
    }
  }
  for (size_t i = 0; i < scene.materials.size(); ++i) {
    const DrawMaterialCPU& material = scene.materials[i];
    const auto validTexture = [&](int id) {
      return id == -1 ||
             (id >= 0 && static_cast<size_t>(id) < scene.textures.size());
    };
    const int ids[] = {
        material.baseColorTex, material.metallicTex, material.roughnessTex,
        material.normalTex, material.coatNormalTex, material.emissiveTex,
        material.opacityTex, material.occlusionTex, material.specularColorTex,
        material.coatWeightTex, material.coatColorTex,
        material.coatRoughnessTex, material.displacementTex};
    for (int id : ids) {
      if (!validTexture(id)) {
        if (err) *err = "material " + std::to_string(i) +
                        " references an invalid texture";
        return false;
      }
    }
    const DrawTexSampleCPU* samples[] = {
        &material.baseColorSample, &material.metallicSample,
        &material.roughnessSample, &material.normalSample,
        &material.coatNormalSample, &material.emissiveSample,
        &material.opacitySample, &material.occlusionSample,
        &material.specularColorSample, &material.coatWeightSample,
        &material.coatColorSample, &material.coatRoughnessSample,
        &material.displacementSample};
    for (const DrawTexSampleCPU* sample : samples) {
      if (!validTexture(sample->tex)) {
        if (err) *err = "material " + std::to_string(i) +
                        " has an invalid texture sample";
        return false;
      }
    }
  }
  for (const DrawMeshCPU& mesh : scene.meshes) {
    if (!ValidateDrawMesh(mesh, scene.materials.size(), err)) return false;
  }
  const auto validMaterial = [&](int id) {
    return id < 0 || static_cast<size_t>(id) < scene.materials.size();
  };
  for (const DrawPointsCPU& points : scene.points) {
    if ((points.points.size() % 3) != 0 || !validMaterial(points.materialId)) {
      if (err) *err = "Points '" + points.absPath + "' has invalid geometry or material";
      return false;
    }
    const size_t count = points.points.size() / 3;
    const auto optional = [count](size_t size, size_t components) {
      size_t full = 0;
      return CheckedMulSize(count, components, &full) &&
             (size == 0 || size == components || size == full);
    };
    if (!optional(points.widths.size(), 1) ||
        !optional(points.colors.size(), 3) ||
        !optional(points.opacities.size(), 1)) {
      if (err) *err = "Points '" + points.absPath + "' has malformed attributes";
      return false;
    }
  }
  for (const DrawCurvesCPU& curves : scene.curves) {
    if ((curves.points.size() % 3) != 0 || !validMaterial(curves.materialId)) {
      if (err) *err = "Curves '" + curves.absPath + "' has invalid geometry or material";
      return false;
    }
    size_t total = 0;
    for (uint32_t count : curves.vertexCounts) {
      if (count > std::numeric_limits<size_t>::max() - total) {
        if (err) *err = "Curves '" + curves.absPath + "' count overflow";
        return false;
      }
      total += count;
    }
    if (!curves.vertexCounts.empty() && total != curves.points.size() / 3) {
      if (err) *err = "Curves '" + curves.absPath + "' counts do not match points";
      return false;
    }
  }
  for (const DrawVolumeCPU& volume : scene.volumes) {
    size_t xy = 0, xyz = 0;
    if (volume.dim[0] <= 0 || volume.dim[1] <= 0 || volume.dim[2] <= 0 ||
        !CheckedMulSize(static_cast<size_t>(volume.dim[0]),
                        static_cast<size_t>(volume.dim[1]), &xy) ||
        !CheckedMulSize(xy, static_cast<size_t>(volume.dim[2]), &xyz) ||
        volume.density.size() != xyz ||
        (!volume.emissionField.empty() && volume.emissionField.size() != xyz) ||
        (!volume.temperatureField.empty() &&
         volume.temperatureField.size() != xyz)) {
      if (err) *err = "volume '" + volume.name + "' has invalid dimensions or fields";
      return false;
    }
  }
  for (const DrawLightCPU& light : scene.lights) {
    if (light.envmapTexture < -1 ||
        (light.envmapTexture >= 0 &&
         static_cast<size_t>(light.envmapTexture) >= scene.textures.size())) {
      if (err) *err = "light '" + light.absPath + "' references an invalid texture";
      return false;
    }
  }
  return true;
}

inline bool ValidateDrawPoints(const DrawPointsCPU& points, size_t materialCount,
                               std::string* err) {
  DrawScene scene;
  scene.materials.resize(materialCount);
  scene.points.push_back(points);
  return ValidateDrawScene(scene, err);
}

inline bool ValidateDrawCurves(const DrawCurvesCPU& curves, size_t materialCount,
                               std::string* err) {
  DrawScene scene;
  scene.materials.resize(materialCount);
  scene.curves.push_back(curves);
  return ValidateDrawScene(scene, err);
}

inline bool ValidateDrawVolume(const DrawVolumeCPU& volume, std::string* err) {
  DrawScene scene;
  scene.volumes.push_back(volume);
  return ValidateDrawScene(scene, err);
}

}  // namespace lusdview
